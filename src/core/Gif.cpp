#include "core/Gif.h"

#include "core/Edit.h"
#include "core/Quantize.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace bg {

int colorsForQuality(int q) {
    int c = (int)std::pow(2.0, q * 6.0 / 79.0 + 1.6203);
    return std::clamp(c, 4, 255);
}
int toleranceForQuality(int q) { return std::clamp((int)((100 - q) / 50.0 * 14 + 1), 1, 15); }

// ------------------------------------------------------------------ LZW
std::vector<uint8_t> lzwEncode(const uint8_t* idx, size_t n, int minCode) {
    std::vector<uint8_t> out;
    out.reserve(n / 2 + 64);
    std::vector<uint8_t> block;
    block.reserve(255);
    uint32_t acc = 0;
    int nbits = 0;
    const int clear = 1 << minCode, eoi = clear + 1;
    int codeSize = minCode + 1, nextCode = eoi + 1;
    auto flushBlock = [&] {
        if (block.empty()) return;
        out.push_back((uint8_t)block.size());
        out.insert(out.end(), block.begin(), block.end());
        block.clear();
    };
    auto put = [&](int code) {
        acc |= (uint32_t)code << nbits;
        nbits += codeSize;
        while (nbits >= 8) {
            block.push_back((uint8_t)acc);
            if (block.size() == 255) flushBlock();
            acc >>= 8;
            nbits -= 8;
        }
    };
    // hash table: key = (prefix << 8) | byte  -> code
    constexpr int HS = 1 << 14;
    std::vector<int32_t> hkey(HS, -1);
    std::vector<int16_t> hval(HS);
    auto reset = [&] { std::fill(hkey.begin(), hkey.end(), -1); codeSize = minCode + 1; nextCode = eoi + 1; };
    put(clear);
    if (n == 0) { put(eoi); }
    else {
        int prefix = idx[0];
        for (size_t i = 1; i < n; ++i) {
            int c = idx[i];
            int32_t key = (prefix << 8) | c;
            uint32_t h = ((uint32_t)key * 2654435761u) >> 18;
            bool found = false;
            while (hkey[h] != -1) {
                if (hkey[h] == key) { prefix = hval[h]; found = true; break; }
                h = (h + 1) & (HS - 1);
            }
            if (found) continue;
            put(prefix);
            if (nextCode < 4096) {
                hkey[h] = key;
                hval[h] = (int16_t)nextCode;
                if (nextCode == (1 << codeSize) && codeSize < 12) ++codeSize;
                ++nextCode;
                // the decoder bumps its width one code later; mirror giflib: grow when nextCode > max
            } else {
                put(clear);
                reset();
            }
            prefix = c;
        }
        put(prefix);
        put(eoi);
    }
    if (nbits > 0) { block.push_back((uint8_t)acc); }
    flushBlock();
    out.push_back(0);
    return out;
}

bool lzwDecode(const uint8_t* d, size_t n, int minCode, size_t npix, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(npix);
    if (minCode < 2 || minCode > 11) return false;
    const int clear = 1 << minCode, eoi = clear + 1;
    std::vector<uint16_t> prefix(4096);
    std::vector<uint8_t> suffix(4096), stack(4097);
    int codeSize = minCode + 1, next = eoi + 1, prev = -1;
    uint8_t first = 0;
    uint32_t acc = 0;
    int nbits = 0;
    size_t p = 0;
    for (int i = 0; i < clear; ++i) suffix[i] = (uint8_t)i;
    while (out.size() < npix) {
        while (nbits < codeSize) {
            if (p >= n) return out.size() >= npix;
            acc |= (uint32_t)d[p++] << nbits;
            nbits += 8;
        }
        int code = acc & ((1 << codeSize) - 1);
        acc >>= codeSize;
        nbits -= codeSize;
        if (code == clear) { codeSize = minCode + 1; next = eoi + 1; prev = -1; continue; }
        if (code == eoi) break;
        if (prev < 0) {
            if (code >= clear) return false;
            out.push_back((uint8_t)code);
            prev = code;
            first = (uint8_t)code;
            continue;
        }
        int in = code, sp = 0;
        if (code >= next) {
            if (code > next) return false;
            stack[sp++] = first;
            code = prev;
        }
        while (code >= clear) {
            if (sp >= 4096) return false;
            stack[sp++] = suffix[code];
            code = prefix[code];
        }
        first = (uint8_t)code;
        stack[sp++] = first;
        while (sp) out.push_back(stack[--sp]);
        if (next < 4096) {
            prefix[next] = (uint16_t)prev;
            suffix[next] = first;
            ++next;
            if (next == (1 << codeSize) && codeSize < 12) ++codeSize;
        }
        prev = in;
    }
    if (out.size() > npix) out.resize(npix);
    return true;
}

// ------------------------------------------------------------------ encoder
namespace {

struct Job {
    std::vector<uint32_t> px;
    std::vector<uint8_t> mask;
    int x = 0, y = 0, w = 0, h = 0;
    bool full = false, hasT = false;
    int delay = 0;
    std::vector<uint8_t> block;   // image descriptor + LCT + LZW data
    int tIndex = -1;
    int state = 0;                // 0 queued, 1 working, 2 done
};

void encodeJob(Job& j, const GifOptions& o) {
    const uint8_t* mask = j.hasT ? j.mask.data() : nullptr;
    int limit = colorsForQuality(o.quality) - (j.hasT ? 1 : 0);
    Palette pal;
    std::vector<uint8_t> idx;
    bool exact = false;
    if (o.quality >= 100 && o.skipQuantQ100) exact = exactPalette(j.px.data(), j.px.size(), mask, 256 - (j.hasT ? 1 : 0), pal);
    int t = -1;
    if (exact) {
        t = j.hasT ? (int)pal.colors.size() : -1;
        remap(j.px.data(), j.w, j.h, mask, pal, false, (uint8_t)std::max(0, t), idx);
    } else {
        QuantRequest q;
        q.px = j.px.data();
        q.w = j.w;
        q.h = j.h;
        q.mask = mask;
        q.maxColors = std::max(2, limit);
        q.quality = o.quality;
        q.method = (QuantMethod)o.method;
        q.dither = o.dither;
        q.centerWeighted = o.centerWeighted && j.full;
        q.fixedColors = o.protectColors;
        quantizeFrame(q, pal, idx, t);
    }
    int ncol = (int)pal.colors.size();
    int total = std::max(ncol, t + 1);
    int bits = 1;
    while ((1 << bits) < total) ++bits;
    j.tIndex = j.hasT ? t : -1;
    auto& b = j.block;
    b.clear();
    auto u16 = [&](int v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    b.push_back(0x2C);
    u16(j.x); u16(j.y); u16(j.w); u16(j.h);
    b.push_back((uint8_t)(0x80 | (bits - 1)));
    for (int i = 0; i < (1 << bits); ++i) {
        uint32_t c = i < ncol ? pal.colors[i] : 0;
        b.push_back((uint8_t)(c >> 16)); b.push_back((uint8_t)(c >> 8)); b.push_back((uint8_t)c);
    }
    int minCode = std::max(2, bits);
    b.push_back((uint8_t)minCode);
    auto lzw = lzwEncode(idx.data(), idx.size(), minCode);
    b.insert(b.end(), lzw.begin(), lzw.end());
    std::vector<uint32_t>().swap(j.px);
    std::vector<uint8_t>().swap(j.mask);
}

std::vector<int> delaysCs(const Frames& f) {   // cumulative rounding, min 2 cs
    std::vector<int> d;
    int64_t t = 0, prev = 0;
    for (auto& x : f) {
        t += x.dur;
        int64_t cur = (t + 5000) / 10000;
        int64_t cs = std::max<int64_t>(2, cur - prev);
        d.push_back((int)cs);
        prev += cs;
    }
    return d;
}

} // namespace

bool encodeGif(const Frames& frames, const GifOptions& o, const ByteSink& out, const Progress& prog, const Cancel& cancel) {
    if (frames.empty()) throw std::runtime_error("no frames");
    const int W = frames[0].img->w, H = frames[0].img->h;
    const int tol = o.removeSimilar ? toleranceForQuality(o.quality) : 0;
    const auto delays = delaysCs(frames);
    int nthreads = o.threads > 0 ? o.threads : std::max(2, (int)std::thread::hardware_concurrency() - 1);

    // header
    std::vector<uint8_t> hdr = {'G', 'I', 'F', '8', '9', 'a', (uint8_t)W, (uint8_t)(W >> 8), (uint8_t)H, (uint8_t)(H >> 8), 0x70, 0, 0};
    const uint8_t ns[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01};
    hdr.insert(hdr.end(), ns, ns + sizeof(ns));
    hdr.push_back((uint8_t)(o.loop & 255)); hdr.push_back((uint8_t)((o.loop >> 8) & 255)); hdr.push_back(0);
    if (o.comment) {
        char buf[128];
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_s(&tmv, &now);
        int len = std::snprintf(buf, sizeof buf, "%s %s (%04d-%02d-%02d %02d:%02d:%02d); q=%d", kAppName, kAppVersion,
                                tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, o.quality);
        hdr.push_back(0x21); hdr.push_back(0xFE); hdr.push_back((uint8_t)len);
        hdr.insert(hdr.end(), buf, buf + len);
        hdr.push_back(0);
    }
    out(hdr.data(), hdr.size());

    std::deque<Job> jobs;
    std::mutex m;
    std::condition_variable cv;
    size_t nextTake = 0;
    bool stop = false;
    std::vector<std::thread> workers;
    for (int t = 0; t < nthreads; ++t)
        workers.emplace_back([&] {
            for (;;) {
                Job* j;
                {
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk, [&] { return stop || nextTake < jobs.size(); });
                    if (stop && nextTake >= jobs.size()) return;
                    j = &jobs[nextTake++];
                    j->state = 1;
                }
                encodeJob(*j, o);
                { std::lock_guard<std::mutex> lk(m); j->state = 2; }
                cv.notify_all();
            }
        });

    size_t written = 0;
    bool cancelled = false;
    const size_t window = (size_t)nthreads * 3;
    auto writeReady = [&](bool final) {
        std::unique_lock<std::mutex> lk(m);
        for (;;) {
            // job k is final once a later job exists (its delay can no longer grow) or input ended
            if (written >= jobs.size() || (!final && written + 1 >= jobs.size())) break;
            bool mustWait = final || (jobs.size() - written) > window;
            if (jobs[written].state != 2) {
                if (!mustWait) break;
                cv.wait(lk, [&] { return jobs[written].state == 2; });
            }
            Job& j = jobs[written];
            lk.unlock();
            uint8_t gce[8] = {0x21, 0xF9, 0x04, (uint8_t)(0x04 | (j.tIndex >= 0 ? 1 : 0)), (uint8_t)j.delay,
                              (uint8_t)(j.delay >> 8), (uint8_t)(j.tIndex >= 0 ? j.tIndex : 0), 0};
            out(gce, 8);
            out(j.block.data(), j.block.size());
            std::vector<uint8_t>().swap(j.block);
            lk.lock();
            ++written;
        }
    };

    std::vector<uint32_t> prev;
    std::vector<uint8_t> changed((size_t)W * H);
    try {
        for (size_t i = 0; i < frames.size(); ++i) {
            if (cancel && cancel()) { cancelled = true; break; }
            ImagePtr src = frames[i].img;
            if (src->w != W || src->h != H) src = resizeImage(*src, W, H);
            const auto& px = src->px;
            if (prev.empty()) {
                Job j;
                j.px = px; j.w = W; j.h = H; j.full = true; j.delay = delays[i];
                prev = px;
                { std::lock_guard<std::mutex> lk(m); jobs.push_back(std::move(j)); }
                cv.notify_all();
                continue;
            }
            int x0 = W, y0 = H, x1 = -1, y1 = -1;
            const int t2 = tol * tol;
            for (int y = 0; y < H; ++y) {
                const uint32_t* a = &px[(size_t)y * W];
                const uint32_t* b = &prev[(size_t)y * W];
                uint8_t* ch = &changed[(size_t)y * W];
                for (int x = 0; x < W; ++x) {
                    uint32_t p = a[x], q = b[x];
                    bool c;
                    if (p == q) c = false;
                    else if (tol == 0) c = true;
                    else {
                        int dr = R(p) - R(q), dg = G(p) - G(q), db = B(p) - B(q);
                        c = dr * dr + dg * dg + db * db > t2;
                    }
                    ch[x] = c;
                    if (c) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; y1 = y; }
                }
            }
            if (x1 < 0) {   // identical: extend previous delay
                std::lock_guard<std::mutex> lk(m);
                jobs.back().delay = std::min(65535, jobs.back().delay + delays[i]);
            } else {
                Job j;
                j.x = x0; j.y = y0; j.w = x1 - x0 + 1; j.h = y1 - y0 + 1;
                j.full = j.w == W && j.h == H;
                j.delay = delays[i];
                j.px.resize((size_t)j.w * j.h);
                j.mask.resize(j.px.size());
                bool anyT = false;
                for (int y = 0; y < j.h; ++y) {
                    size_t so = (size_t)(y0 + y) * W + x0;
                    for (int x = 0; x < j.w; ++x) {
                        bool c = changed[so + x];
                        j.px[(size_t)y * j.w + x] = px[so + x];
                        j.mask[(size_t)y * j.w + x] = !c;
                        if (c) prev[so + x] = px[so + x];
                        else anyT = true;
                    }
                }
                j.hasT = anyT;
                if (!anyT) std::vector<uint8_t>().swap(j.mask);
                { std::lock_guard<std::mutex> lk(m); jobs.push_back(std::move(j)); }
                cv.notify_all();
            }
            writeReady(false);
            if (prog) prog(0.98 * (i + 1) / frames.size());
        }
        if (!cancelled) writeReady(true);
    } catch (...) {
        { std::lock_guard<std::mutex> lk(m); stop = true; nextTake = jobs.size(); }
        cv.notify_all();
        for (auto& t : workers) t.join();
        throw;
    }
    {
        std::lock_guard<std::mutex> lk(m);
        stop = true;
        if (cancelled) nextTake = jobs.size();
    }
    cv.notify_all();
    for (auto& t : workers) t.join();
    if (cancelled) return false;
    uint8_t trailer = 0x3B;
    out(&trailer, 1);
    if (prog) prog(1.0);
    return true;
}

// ------------------------------------------------------------------ decoder
bool decodeGif(const std::vector<uint8_t>& d, Frames& out, std::string& err) {
    out.clear();
    if (d.size() < 13 || std::memcmp(d.data(), "GIF", 3) != 0) { err = "GIF 파일이 아닙니다"; return false; }
    int W = d[6] | (d[7] << 8), H = d[8] | (d[9] << 8);
    if (W <= 0 || H <= 0) { err = "잘못된 크기"; return false; }
    size_t p = 13;
    std::vector<uint32_t> gct;
    if (d[10] & 0x80) {
        int n = 1 << ((d[10] & 7) + 1);
        if (p + 3 * n > d.size()) { err = "잘린 팔레트"; return false; }
        for (int i = 0; i < n; ++i) gct.push_back(rgb(d[p + 3 * i], d[p + 3 * i + 1], d[p + 3 * i + 2]));
        p += 3 * n;
    }
    Image canvas(W, H, 0xFFFFFFFFu);
    int delay = 10, disposal = 0, tidx = -1;
    while (p < d.size()) {
        uint8_t b = d[p++];
        if (b == 0x3B) break;
        if (b == 0x21) {
            if (p >= d.size()) break;
            uint8_t label = d[p++];
            if (label == 0xF9 && p + 5 < d.size() && d[p] == 4) {
                disposal = (d[p + 1] >> 2) & 7;
                delay = d[p + 2] | (d[p + 3] << 8);
                tidx = (d[p + 1] & 1) ? d[p + 4] : -1;
            }
            while (p < d.size() && d[p]) p += d[p] + 1;
            ++p;
            continue;
        }
        if (b != 0x2C) break;
        if (p + 9 > d.size()) break;
        int fx = d[p] | (d[p + 1] << 8), fy = d[p + 2] | (d[p + 3] << 8), fw = d[p + 4] | (d[p + 5] << 8), fh = d[p + 6] | (d[p + 7] << 8);
        uint8_t pk = d[p + 8];
        p += 9;
        std::vector<uint32_t> lct;
        if (pk & 0x80) {
            int n = 1 << ((pk & 7) + 1);
            if (p + 3 * n > d.size()) break;
            for (int i = 0; i < n; ++i) lct.push_back(rgb(d[p + 3 * i], d[p + 3 * i + 1], d[p + 3 * i + 2]));
            p += 3 * n;
        }
        const auto& pal = lct.empty() ? gct : lct;
        if (p >= d.size()) break;
        int minCode = d[p++];
        std::vector<uint8_t> data;
        while (p < d.size() && d[p]) {
            size_t len = d[p];
            if (p + 1 + len > d.size()) break;
            data.insert(data.end(), d.begin() + p + 1, d.begin() + p + 1 + len);
            p += len + 1;
        }
        ++p;
        std::vector<uint8_t> idx;
        lzwDecode(data.data(), data.size(), minCode, (size_t)fw * fh, idx);
        idx.resize((size_t)fw * fh, 0);
        Image before = canvas;
        // interlace row order
        std::vector<int> rows;
        if (pk & 0x40) {
            for (int y = 0; y < fh; y += 8) rows.push_back(y);
            for (int y = 4; y < fh; y += 8) rows.push_back(y);
            for (int y = 2; y < fh; y += 4) rows.push_back(y);
            for (int y = 1; y < fh; y += 2) rows.push_back(y);
        } else {
            for (int y = 0; y < fh; ++y) rows.push_back(y);
        }
        for (int r = 0; r < fh; ++r) {
            int y = fy + rows[r];
            if (y < 0 || y >= H) continue;
            for (int x = 0; x < fw; ++x) {
                int cx = fx + x;
                if (cx < 0 || cx >= W) continue;
                int v = idx[(size_t)r * fw + x];
                if (v == tidx || v >= (int)pal.size()) continue;
                canvas.row(y)[cx] = pal[v];
            }
        }
        int64_t us = (delay < 2 ? 10 : delay) * 10000LL;
        out.push_back({std::make_shared<Image>(canvas), us});
        if (disposal == 2) {
            for (int y = std::max(0, fy); y < std::min(H, fy + fh); ++y)
                for (int x = std::max(0, fx); x < std::min(W, fx + fw); ++x) canvas.row(y)[x] = 0xFFFFFFFFu;
        } else if (disposal == 3) {
            canvas = std::move(before);
        }
        delay = 10; disposal = 0; tidx = -1;
    }
    if (out.empty()) { err = "프레임이 없습니다"; return false; }
    return true;
}

} // namespace bg
