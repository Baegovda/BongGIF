#include "core/Apng.h"

#include "core/Deflate.h"
#include "core/Edit.h"

#include <algorithm>
#include <cstring>

namespace bg {

namespace {

void be32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24)); b.push_back((uint8_t)(v >> 16)); b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v);
}
void be16(std::vector<uint8_t>& b, uint32_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
uint32_t rd32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

void chunk(const ByteSink& out, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> b;
    be32(b, (uint32_t)data.size());
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), data.begin(), data.end());
    uint32_t c = crc32(b.data() + 4, b.size() - 4);
    be32(b, c);
    out(b.data(), b.size());
}

int paeth(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

// RGB rows with adaptive filter choice (minimum sum of absolute values)
std::vector<uint8_t> filtered(const Image& im, int x0, int y0, int w, int h) {
    const int bpr = w * 3;
    std::vector<uint8_t> out;
    out.reserve((size_t)(bpr + 1) * h);
    std::vector<uint8_t> prev(bpr, 0), cur(bpr), cand(bpr), best(bpr);
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = im.row(y0 + y) + x0;
        for (int x = 0; x < w; ++x) { cur[x * 3] = (uint8_t)R(row[x]); cur[x * 3 + 1] = (uint8_t)G(row[x]); cur[x * 3 + 2] = (uint8_t)B(row[x]); }
        long bestSum = -1;
        int bestF = 0;
        for (int f = 0; f < 5; ++f) {
            long sum = 0;
            for (int i = 0; i < bpr; ++i) {
                int a = i >= 3 ? cur[i - 3] : 0, b = y ? prev[i] : 0, c = (i >= 3 && y) ? prev[i - 3] : 0, v;
                switch (f) {
                case 0: v = cur[i]; break;
                case 1: v = cur[i] - a; break;
                case 2: v = cur[i] - b; break;
                case 3: v = cur[i] - ((a + b) >> 1); break;
                default: v = cur[i] - paeth(a, b, c); break;
                }
                cand[i] = (uint8_t)v;
                sum += (int8_t)cand[i] < 0 ? -(int8_t)cand[i] : (int8_t)cand[i];
            }
            if (bestSum < 0 || sum < bestSum) { bestSum = sum; bestF = f; best.swap(cand); }
        }
        out.push_back((uint8_t)bestF);
        out.insert(out.end(), best.begin(), best.end());
        prev = cur;
    }
    return out;
}

} // namespace

bool encodeApng(const Frames& frames, int loop, const ByteSink& out, const Progress& prog, const Cancel& cancel) {
    if (frames.empty()) return false;
    const int W = frames[0].img->w, H = frames[0].img->h;
    // merge identical frames and compute change rectangles
    struct F { ImagePtr img; int64_t dur; int x, y, w, h; };
    std::vector<F> fs;
    for (size_t i = 0; i < frames.size(); ++i) {
        ImagePtr im = frames[i].img;
        if (im->w != W || im->h != H) im = resizeImage(*im, W, H);
        if (fs.empty()) { fs.push_back({im, frames[i].dur, 0, 0, W, H}); continue; }
        const Image& p = *fs.back().img;
        int x0 = W, y0 = H, x1 = -1, y1 = -1;
        for (int y = 0; y < H; ++y) {
            const uint32_t *a = im->row(y), *b = p.row(y);
            if (std::memcmp(a, b, W * 4) == 0) continue;
            for (int x = 0; x < W; ++x)
                if (a[x] != b[x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
            y0 = std::min(y0, y); y1 = y;
        }
        if (x1 < 0) { fs.back().dur += frames[i].dur; continue; }
        fs.push_back({im, frames[i].dur, x0, y0, x1 - x0 + 1, y1 - y0 + 1});
    }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out(sig, 8);
    std::vector<uint8_t> d;
    be32(d, W); be32(d, H); d.push_back(8); d.push_back(2); d.push_back(0); d.push_back(0); d.push_back(0);
    chunk(out, "IHDR", d);
    d.clear(); be32(d, (uint32_t)fs.size()); be32(d, (uint32_t)loop);
    chunk(out, "acTL", d);
    std::string sw = std::string(kAppName) + " " + kAppVersion;
    d.assign({'S', 'o', 'f', 't', 'w', 'a', 'r', 'e', 0});
    d.insert(d.end(), sw.begin(), sw.end());
    chunk(out, "tEXt", d);
    uint32_t seq = 0;
    for (size_t i = 0; i < fs.size(); ++i) {
        if (cancel && cancel()) return false;
        const F& f = fs[i];
        d.clear();
        be32(d, seq++); be32(d, f.w); be32(d, f.h); be32(d, f.x); be32(d, f.y);
        int64_t ms = std::max<int64_t>(1, (f.dur + 500) / 1000);
        if (ms > 65535) ms = 65535;
        be16(d, (uint32_t)ms); be16(d, 1000);
        d.push_back(0);   // dispose NONE
        d.push_back(0);   // blend SOURCE
        chunk(out, "fcTL", d);
        auto raw = filtered(*f.img, f.x, f.y, f.w, f.h);
        auto z = zlibCompress(raw.data(), raw.size());
        if (i == 0) chunk(out, "IDAT", z);
        else {
            d.clear(); be32(d, seq++);
            d.insert(d.end(), z.begin(), z.end());
            chunk(out, "fdAT", d);
        }
        if (prog) prog((double)(i + 1) / fs.size());
    }
    chunk(out, "IEND", {});
    return true;
}

bool decodePng(const std::vector<uint8_t>& data, Frames& out, std::string& err) {
    out.clear();
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (data.size() < 33 || std::memcmp(data.data(), sig, 8) != 0) { err = "PNG가 아닙니다"; return false; }
    int W = 0, H = 0, depth = 0, ctype = 0, interlace = 0;
    std::vector<uint32_t> plte;
    std::vector<uint8_t> trns;
    struct Fc { int w, h, x, y, dn, dd, dop, bop; std::vector<uint8_t> z; };
    std::vector<Fc> fcs;
    std::vector<uint8_t> idat;
    bool animated = false, idatInFrame = false;
    size_t p = 8;
    while (p + 8 <= data.size()) {
        uint32_t len = rd32(&data[p]);
        const char* t = (const char*)&data[p + 4];
        const uint8_t* c = &data[p + 8];
        if (p + 12 + len > data.size()) break;
        if (!std::memcmp(t, "IHDR", 4)) {
            W = (int)rd32(c); H = (int)rd32(c + 4); depth = c[8]; ctype = c[9]; interlace = c[12];
        } else if (!std::memcmp(t, "PLTE", 4)) {
            for (uint32_t i = 0; i + 2 < len; i += 3) plte.push_back(rgb(c[i], c[i + 1], c[i + 2]));
        } else if (!std::memcmp(t, "tRNS", 4)) {
            trns.assign(c, c + len);
        } else if (!std::memcmp(t, "acTL", 4)) {
            animated = true;
        } else if (!std::memcmp(t, "fcTL", 4) && len >= 26) {
            fcs.push_back({(int)rd32(c + 4), (int)rd32(c + 8), (int)rd32(c + 12), (int)rd32(c + 16), (c[20] << 8) | c[21],
                           (c[22] << 8) | c[23], c[24], c[25], {}});
        } else if (!std::memcmp(t, "IDAT", 4)) {
            idat.insert(idat.end(), c, c + len);
            if (!fcs.empty()) { idatInFrame = true; fcs.back().z.insert(fcs.back().z.end(), c, c + len); }
        } else if (!std::memcmp(t, "fdAT", 4) && len >= 4 && !fcs.empty()) {
            fcs.back().z.insert(fcs.back().z.end(), c + 4, c + len);
        } else if (!std::memcmp(t, "IEND", 4)) break;
        p += 12 + len;
    }
    if (W <= 0 || H <= 0) { err = "잘못된 PNG"; return false; }
    if (depth != 8 || interlace) { err = "8비트 비인터레이스 PNG만 지원합니다"; return false; }
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch) { err = "지원하지 않는 PNG 형식"; return false; }
    auto decodeRect = [&](const std::vector<uint8_t>& z, int w, int h, std::vector<uint32_t>& rgba) -> bool {
        if (z.size() < 6) return false;
        std::vector<uint8_t> raw;
        std::string e;
        if (!inflateRaw(z.data() + 2, z.size() - 2, raw, e)) return false;
        size_t bpr = (size_t)w * ch;
        if (raw.size() < (bpr + 1) * h) return false;
        std::vector<uint8_t> prev(bpr, 0), cur(bpr);
        rgba.resize((size_t)w * h);
        for (int y = 0; y < h; ++y) {
            const uint8_t* s = &raw[(bpr + 1) * y];
            int f = s[0];
            ++s;
            for (size_t i = 0; i < bpr; ++i) {
                int a = i >= (size_t)ch ? cur[i - ch] : 0, b = prev[i], c = i >= (size_t)ch ? prev[i - ch] : 0;
                int v = s[i];
                switch (f) {
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) >> 1; break;
                case 4: v += paeth(a, b, c); break;
                }
                cur[i] = (uint8_t)v;
            }
            for (int x = 0; x < w; ++x) {
                const uint8_t* q = &cur[(size_t)x * ch];
                uint32_t px;
                switch (ctype) {
                case 0: px = rgb(q[0], q[0], q[0]) & ((trns.size() >= 2 && trns[1] == q[0]) ? 0x00FFFFFF : 0xFFFFFFFF); break;
                case 2: px = rgb(q[0], q[1], q[2]); break;
                case 3: {
                    px = q[0] < plte.size() ? plte[q[0]] : 0xFF000000u;
                    int a = q[0] < trns.size() ? trns[q[0]] : 255;
                    px = (px & 0xFFFFFF) | ((uint32_t)a << 24);
                    break;
                }
                case 4: px = (rgb(q[0], q[0], q[0]) & 0xFFFFFF) | ((uint32_t)q[1] << 24); break;
                default: px = ((uint32_t)q[3] << 24) | ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2]; break;
                }
                rgba[(size_t)y * w + x] = px;
            }
            prev = cur;
        }
        return true;
    };
    // composite on an RGBA canvas, then flatten on white per output frame
    std::vector<uint32_t> canvas((size_t)W * H, 0);
    auto flatten = [&](int64_t us) {
        auto im = std::make_shared<Image>(W, H);
        for (size_t i = 0; i < canvas.size(); ++i) {
            uint32_t c = canvas[i];
            int a = c >> 24;
            im->px[i] = rgb((R(c) * a + 255 * (255 - a)) / 255, (G(c) * a + 255 * (255 - a)) / 255, (B(c) * a + 255 * (255 - a)) / 255);
        }
        out.push_back({im, us});
    };
    if (!animated || fcs.empty()) {
        std::vector<uint32_t> px;
        if (!decodeRect(idat, W, H, px)) { err = "PNG 데이터 오류"; return false; }
        canvas = px;
        flatten(100000);
        return true;
    }
    for (auto& f : fcs) {
        std::vector<uint32_t> px;
        if (f.z.empty() || !decodeRect(f.z, f.w, f.h, px)) continue;
        std::vector<uint32_t> saved;
        if (f.dop == 2) saved = canvas;
        for (int y = 0; y < f.h && f.y + y < H; ++y)
            for (int x = 0; x < f.w && f.x + x < W; ++x) {
                uint32_t s = px[(size_t)y * f.w + x];
                uint32_t& dst = canvas[(size_t)(f.y + y) * W + f.x + x];
                if (f.bop == 0) dst = s;
                else {
                    int sa = s >> 24, da = dst >> 24;
                    int oa = sa + da * (255 - sa) / 255;
                    if (oa == 0) { dst = 0; continue; }
                    auto mix = [&](int sc, int dc) { return (sc * sa + dc * da * (255 - sa) / 255) / oa; };
                    dst = ((uint32_t)oa << 24) | ((uint32_t)mix(R(s), R(dst)) << 16) | ((uint32_t)mix(G(s), G(dst)) << 8) | (uint32_t)mix(B(s), B(dst));
                }
            }
        int dd = f.dd ? f.dd : 100;
        int64_t us = (int64_t)f.dn * 1000000 / dd;
        if (us < 10000) us = 100000;
        flatten(us);
        if (f.dop == 1) {
            for (int y = 0; y < f.h && f.y + y < H; ++y)
                for (int x = 0; x < f.w && f.x + x < W; ++x) canvas[(size_t)(f.y + y) * W + f.x + x] = 0;
        } else if (f.dop == 2) canvas = saved;
    }
    if (out.empty()) { err = "APNG 프레임이 없습니다"; return false; }
    return true;
}

} // namespace bg
