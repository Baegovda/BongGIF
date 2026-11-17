#include "platform/ClipExport.h"

#include "platform/ClipSource.h"

#include <webp/encode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace bg {

namespace {

constexpr size_t kAnmfOverhead = 32;   // per-frame animation header allowance used by the estimate

std::string webpErr(int code) { return "WebP 인코딩에 실패했어요 (code " + std::to_string(code) + ")"; }

double jsRound(double v) { return std::floor(v + 0.5); }

// standalone WebP of img[x0..x1) x [y0..y1): lossy quality q, method 3 (lossless at q = 100)
bool encodeStill(const Image& img, const int box[4], int quality, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    const bool lossless = quality >= 100;
    WebPConfig cfg;
    if (!WebPConfigPreset(&cfg, WEBP_PRESET_DEFAULT, lossless ? 75.f : (float)std::clamp(quality, 0, 100))) {
        err = webpErr(-1);
        return false;
    }
    cfg.lossless = lossless ? 1 : 0;
    cfg.method = lossless ? 0 : 3;
    if (!WebPValidateConfig(&cfg)) { err = webpErr(-2); return false; }
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) { err = webpErr(-3); return false; }
    pic.width = box[2] - box[0];
    pic.height = box[3] - box[1];
    pic.use_argb = lossless ? 1 : 0;
    const uint8_t* base = (const uint8_t*)(img.row(box[1]) + box[0]);
    const int stride = img.w * 4;
    // Image bytes are B,G,R,A (opaque)
    int ok = lossless ? WebPPictureImportBGRA(&pic, base, stride) : WebPPictureImportBGRX(&pic, base, stride);
    if (!ok) { WebPPictureFree(&pic); err = webpErr(-4); return false; }
    WebPMemoryWriter wr;
    WebPMemoryWriterInit(&wr);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &wr;
    ok = WebPEncode(&cfg, &pic);
    int code = (int)pic.error_code;
    WebPPictureFree(&pic);
    if (ok) out.assign(wr.mem, wr.mem + wr.size);
    WebPMemoryWriterClear(&wr);
    if (!ok) { err = webpErr(code ? code : -5); return false; }
    return true;
}

void put24(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back((uint8_t)v);
    o.push_back((uint8_t)(v >> 8));
    o.push_back((uint8_t)(v >> 16));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
    put24(o, v);
    o.push_back((uint8_t)(v >> 24));
}
void putTag(std::vector<uint8_t>& o, const char* t) { o.insert(o.end(), t, t + 4); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Animated WebP assembly: VP8X + ANIM + one ANMF per frame (no blending, no disposal). The last
// frame is held back so merged identical frames can still lengthen it.
struct Muxer {
    std::vector<uint8_t> out;
    bool alpha = false;
    int frames = 0;
    struct Pending { bool on = false; int x = 0, y = 0, w = 0, h = 0, dur = 0; std::vector<uint8_t> parts; } pend;

    void begin(int W, int H, int loop) {
        out.clear();
        putTag(out, "RIFF");
        put32(out, 0);   // patched in finish()
        putTag(out, "WEBP");
        putTag(out, "VP8X");
        put32(out, 10);
        out.push_back(0x02);   // animation (alpha bit patched in finish())
        out.push_back(0);
        out.push_back(0);
        out.push_back(0);
        put24(out, (uint32_t)(W - 1));
        put24(out, (uint32_t)(H - 1));
        putTag(out, "ANIM");
        put32(out, 6);
        put32(out, 0);   // background colour
        out.push_back((uint8_t)loop);
        out.push_back((uint8_t)(loop >> 8));
    }
    // frame data chunks (ALPH / VP8 / VP8L) of a still WebP file
    static bool frameChunks(const std::vector<uint8_t>& still, std::vector<uint8_t>& parts, bool& a) {
        parts.clear();
        a = false;
        if (still.size() < 12 || std::memcmp(still.data(), "RIFF", 4) || std::memcmp(still.data() + 8, "WEBP", 4)) return false;
        for (size_t o = 12; o + 8 <= still.size();) {
            const uint8_t* c = still.data() + o;
            uint32_t size = le32(c + 4);
            size_t total = 8 + (size_t)size + (size & 1);
            bool skip = !std::memcmp(c, "VP8X", 4) || !std::memcmp(c, "ICCP", 4) || !std::memcmp(c, "EXIF", 4) ||
                        !std::memcmp(c, "XMP ", 4) || !std::memcmp(c, "ANIM", 4);
            if (!skip) {
                parts.insert(parts.end(), c, c + std::min(total, still.size() - o));
                if (!std::memcmp(c, "ALPH", 4)) a = true;
            }
            o += total;
        }
        return !parts.empty();
    }
    void flush() {
        if (!pend.on) return;
        const Pending& f = pend;
        size_t payload = 16 + f.parts.size();
        putTag(out, "ANMF");
        put32(out, (uint32_t)payload);
        put24(out, (uint32_t)(f.x >> 1));
        put24(out, (uint32_t)(f.y >> 1));
        put24(out, (uint32_t)(f.w - 1));
        put24(out, (uint32_t)(f.h - 1));
        put24(out, (uint32_t)std::min(0xffffff, f.dur));
        out.push_back(0x02);   // do not blend, no disposal
        out.insert(out.end(), f.parts.begin(), f.parts.end());
        if (payload & 1) out.push_back(0);
        pend.on = false;
    }
    bool add(const std::vector<uint8_t>& still, int x, int y, int w, int h, int dur) {
        flush();
        bool a = false;
        if (!frameChunks(still, pend.parts, a)) return false;
        alpha = alpha || a;
        pend.on = true;
        pend.x = x;
        pend.y = y;
        pend.w = w;
        pend.h = h;
        pend.dur = dur;
        ++frames;
        return true;
    }
    void finish() {
        flush();
        if (out.size() >= 21) {
            if (alpha) out[20] |= 0x10;
            uint32_t riff = (uint32_t)(out.size() - 8);
            out[4] = (uint8_t)riff;
            out[5] = (uint8_t)(riff >> 8);
            out[6] = (uint8_t)(riff >> 16);
            out[7] = (uint8_t)(riff >> 24);
        }
    }
};

struct PlanOut {
    std::vector<size_t> sizes;   // still size per written frame
    size_t bytes = 0;            // sum of sizes
    int w = 0, h = 0;
    Muxer mux;
};

// Encodes plan[0..n) (output indices startIndex..). Changed rectangles only; a frame with the same
// source index or no visible change lengthens the previous frame.
bool encodePlan(ClipSource& src, const ClipProject& p, const int* plan, int n, int startIndex, bool assemble, PlanOut& o,
                const std::function<void(int, int)>& onStep, const std::function<bool()>& cancel, std::string& err) {
    o = PlanOut{};
    if (n <= 0) { err = "내보낼 프레임이 없어요"; return false; }
    const int q = std::clamp(p.ex.quality, 0, 100);
    ImagePtr prev, cur;
    int lastSrc = -1;
    std::vector<uint8_t> still;
    for (int k = 0; k < n; ++k) {
        if (cancel && cancel()) { err.clear(); return false; }
        const int s = plan[k];
        const int dur = frameDurationMs(startIndex + k, p.ex.fps);
        bool changed = false;
        int box[4] = {0, 0, 0, 0};
        if (s != lastSrc) {
            ImagePtr fr = src.frame(s);
            if (!fr) { err = "영상 프레임을 읽지 못했어요 (" + std::to_string(s) + ")"; return false; }
            Image work = *fr;
            renderMasks(work, p.masks, s);
            cur = cropScale(work, p);
            if (!cur || cur->w <= 0 || cur->h <= 0) { err = "출력 크기가 올바르지 않아요"; return false; }
            if (!o.w) {
                o.w = cur->w;
                o.h = cur->h;
                if (o.w > 16383 || o.h > 16383) { err = "WebP는 16383px까지만 지원해요"; return false; }
                if (assemble) o.mux.begin(o.w, o.h, std::clamp(p.ex.loop, 0, 65535));
            }
            if (cur->w != o.w || cur->h != o.h) { err = "프레임 크기가 서로 달라요"; return false; }
            if (prev) changed = clipDiffBox(*prev, *cur, 2, box);
            else { changed = true; box[2] = o.w; box[3] = o.h; }
            lastSrc = s;
        }
        if (!changed) {
            if (assemble && o.mux.pend.on) o.mux.pend.dur += dur;
            if (onStep) onStep(k + 1, n);
            continue;
        }
        if (!encodeStill(*cur, box, q, still, err)) return false;
        o.sizes.push_back(still.size());
        o.bytes += still.size();
        if (assemble && !o.mux.add(still, box[0], box[1], box[2] - box[0], box[3] - box[1], dur)) {
            err = webpErr(-6);
            return false;
        }
        prev = cur;
        if (onStep) onStep(k + 1, n);
    }
    if (assemble) o.mux.finish();
    return true;
}

bool openSource(ClipSource& src, const std::wstring& path, double srcFps, std::string& err) {
    if (!src.open(path, err)) {
        if (err.empty()) err = "영상을 열 수 없어요";
        return false;
    }
    if (srcFps > 0) src.setFps(srcFps);   // no-op when equal to the detected FPS
    return true;
}

bool estimateWith(ClipSource& src, double srcFps, const ClipProject& p, size_t& bytes, const std::function<bool()>& cancel,
                  std::string& err) {
    bytes = 0;
    const std::vector<int> plan = outputFrames(p, srcFps);
    const int n = (int)plan.size();
    if (n == 0) { err = "내보낼 프레임이 없어요"; return false; }
    constexpr int L = 8, B = 6;
    PlanOut r;
    if (n <= L * B) {   // short clip: encode everything
        if (!encodePlan(src, p, plan.data(), n, 0, false, r, {}, cancel, err)) return false;
        bytes = r.bytes + r.sizes.size() * kAnmfOverhead;
        return true;
    }
    double first = 0, diffBytes = 0, diffFrames = 0, encoded = 0, frameCount = 0;
    for (int b = 0; b < B; ++b) {
        const int at = (int)jsRound((double)b * (n - L) / (B - 1));
        if (!encodePlan(src, p, plan.data() + at, L, at, false, r, {}, cancel, err)) return false;
        if (b == 0) first = r.sizes.empty() ? 0 : (double)r.sizes[0];
        // the run's first frame is a full frame: average only the others (0 bytes when unchanged)
        for (size_t i = 1; i < r.sizes.size(); ++i) diffBytes += (double)r.sizes[i];
        diffFrames += L - 1;
        encoded += (double)r.sizes.size() - 1;
        frameCount += L - 1;
    }
    const double perFrame = diffBytes / diffFrames, written = encoded / frameCount;
    bytes = (size_t)(first + perFrame * (n - 1) + (1 + written * (n - 1)) * kAnmfOverhead);
    return true;
}

std::vector<FitCandidate> candidatesFor(const ClipSource& src, double srcFps, const ClipProject& p) {
    int b[4];
    cropBox(p, src.info().w, src.info().h, b);
    return fitCandidates(b[2] - b[0], srcFps);
}

// best candidate from index `from` on whose estimate is within 92% of target
bool findFit(ClipSource& src, double srcFps, const ClipProject& p, size_t targetBytes, int from, ClipFitResult& res,
             const std::function<void(int, int, const FitCandidate&)>& onTry, const std::function<bool()>& cancel,
             std::string& err) {
    res = {};
    const std::vector<FitCandidate> cands = candidatesFor(src, srcFps, p);
    const int nc = (int)cands.size();
    if (nc == 0) { err = "크기 맞춤 후보가 없어요"; return false; }
    const double limit = (double)targetBytes * 0.92;   // estimate error margin
    std::map<int, size_t> cache;
    auto est = [&](int i, size_t& out) -> bool {
        auto it = cache.find(i);
        if (it != cache.end()) { out = it->second; return true; }
        if (onTry) onTry(i, nc, cands[i]);
        ClipProject t = p;
        t.ex.width = cands[i].width;
        t.ex.quality = cands[i].quality;
        t.ex.fps = cands[i].fps;
        if (!estimateWith(src, srcFps, t, out, cancel, err)) return false;
        cache[i] = out;
        return true;
    };
    auto pick = [&](int i, bool fits) {
        res.found = true;
        res.fits = fits;
        res.cand = cands[i];
        res.bytes = cache[i];
        res.index = i;
        return true;
    };
    int lo = std::max(0, from), hi = nc - 1;
    if (lo > hi) return true;   // nothing below the current settings: found = false
    size_t s = 0;
    if (!est(lo, s)) return false;
    if ((double)s <= limit) return pick(lo, true);
    if (!est(hi, s)) return false;
    if ((double)s > limit) return pick(hi, false);
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (!est(mid, s)) return false;
        if ((double)s <= limit) hi = mid;
        else lo = mid;
    }
    return pick(hi, true);
}

} // namespace

bool clipDiffBox(const Image& a, const Image& b, int tol, int out[4]) {
    const int w = std::min(a.w, b.w), h = std::min(a.h, b.h);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        const uint32_t* pa = a.row(y);
        const uint32_t* pb = b.row(y);
        for (int x = 0; x < w; ++x) {
            uint32_t ca = pa[x], cb = pb[x];
            if (((ca ^ cb) & 0xFFFFFFu) == 0) continue;
            if (std::abs(R(ca) - R(cb)) > tol || std::abs(G(ca) - G(cb)) > tol || std::abs(B(ca) - B(cb)) > tol) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                y1 = y;
            }
        }
    }
    if (x1 < 0) return false;
    out[0] = x0 & ~1;
    out[1] = y0 & ~1;
    out[2] = x1 + 1;
    out[3] = y1 + 1;
    return true;
}

bool exportClipWebp(const std::wstring& videoPath, double srcFps, const ClipProject& p, const std::wstring& outPath,
                    ClipExportResult& res, const std::function<void(int, int)>& onStep, const std::function<bool()>& cancel,
                    std::string& err) {
    res = {};
    err.clear();
    if (outputFrames(p, srcFps).empty()) { err = "내보낼 프레임이 없어요"; return false; }
    ClipSource src;
    if (!openSource(src, videoPath, srcFps, err)) return false;
    const size_t target = p.ex.targetMB > 0 ? (size_t)(p.ex.targetMB * 1048576.0) : 0;
    ClipProject cur = p;
    PlanOut r;
    std::vector<int> plan;
    for (int attempt = 0;; ++attempt) {
        plan = outputFrames(cur, srcFps);
        res.planned = (int)plan.size();
        if (!encodePlan(src, cur, plan.data(), (int)plan.size(), 0, true, r, onStep, cancel, err)) return false;
        res.attempts = attempt + 1;
        if (!target || r.mux.out.size() <= target || attempt >= 2) break;
        // over the target: find a setting at least one step below the current one and encode again
        const std::vector<FitCandidate> cands = candidatesFor(src, srcFps, cur);
        int ci = -1;
        for (int i = 0; i < (int)cands.size(); ++i)
            if (cands[i].width == cur.ex.width && cands[i].quality == cur.ex.quality && cands[i].fps == cur.ex.fps) { ci = i; break; }
        ClipFitResult fr;
        if (!findFit(src, srcFps, cur, target, ci + 1, fr, {}, cancel, err)) return false;
        if (!fr.found) break;
        cur.ex.width = fr.cand.width;
        cur.ex.quality = fr.cand.quality;
        cur.ex.fps = fr.cand.fps;
        res.adjusted = true;
    }
    const std::vector<uint8_t>& buf = r.mux.out;
    res.bytes = buf.size();
    res.encoded = r.mux.frames;
    res.w = r.w;
    res.h = r.h;
    res.used.width = cur.ex.width;
    res.used.quality = cur.ex.quality;
    res.used.fps = cur.ex.fps;
    res.overTarget = target && buf.size() > target;
    if (!outPath.empty()) {
        FILE* f = _wfopen(outPath.c_str(), L"wb");
        if (!f) { err = "파일을 저장할 수 없어요"; return false; }
        bool ok = std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
        ok = (std::fclose(f) == 0) && ok;
        if (!ok) { _wremove(outPath.c_str()); err = "파일을 저장할 수 없어요"; return false; }
    }
    return true;
}

bool estimateClipBytes(const std::wstring& videoPath, double srcFps, const ClipProject& p, size_t& bytes,
                       const std::function<bool()>& cancel, std::string& err) {
    bytes = 0;
    err.clear();
    if (outputFrames(p, srcFps).empty()) { err = "내보낼 프레임이 없어요"; return false; }
    ClipSource src;
    if (!openSource(src, videoPath, srcFps, err)) return false;
    return estimateWith(src, srcFps, p, bytes, cancel, err);
}

bool fitClipToSize(const std::wstring& videoPath, double srcFps, const ClipProject& p, size_t targetBytes, ClipFitResult& res,
                   const std::function<void(int, int, const FitCandidate&)>& onTry, const std::function<bool()>& cancel,
                   std::string& err) {
    res = {};
    err.clear();
    ClipSource src;
    if (!openSource(src, videoPath, srcFps, err)) return false;
    return findFit(src, srcFps, p, targetBytes, 0, res, onTry, cancel, err);
}

} // namespace bg
