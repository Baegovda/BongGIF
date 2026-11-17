#include "platform/Webp.h"

#include "core/Edit.h"

#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

namespace bg {

namespace {

// One frame to encode: pixels in BGRA (alpha used for blended diff frames)
struct WJob {
    std::vector<uint32_t> px;
    int x = 0, y = 0, w = 0, h = 0;
    bool blend = false;          // ANMF blend + loop filter 0
    int durMs = 100;
    std::vector<uint8_t> data;   // encoded bitstream
    bool ok = false;
};

int writer(const uint8_t* d, size_t n, const WebPPicture* pic) {
    auto* v = (std::vector<uint8_t>*)pic->custom_ptr;
    v->insert(v->end(), d, d + n);
    return 1;
}

// frame encoder
bool encodeFrame(const uint32_t* px, int w, int h, float quality, bool lossless, bool diffFrame, std::vector<uint8_t>& out) {
    WebPConfig c;
    if (!WebPConfigInit(&c)) return false;
    c.quality = lossless ? std::min(quality, 80.f) : quality;
    c.lossless = lossless ? 1 : 0;
    c.method = 5;
    c.segments = 4;
    c.sns_strength = 50;
    c.filter_strength = (!lossless && diffFrame) ? 0 : 60;
    c.filter_type = 1;
    c.alpha_compression = 1;
    c.alpha_filtering = 1;
    c.alpha_quality = 100;
    c.pass = 1;
    c.thread_level = 1;
    c.near_lossless = 100;
    c.qmin = 0;
    c.qmax = 100;
    c.use_sharp_yuv = 0;
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return false;
    pic.width = w;
    pic.height = h;
    pic.use_argb = 1;
    if (!WebPPictureImportBGRA(&pic, (const uint8_t*)px, w * 4)) return false;
    out.clear();
    pic.writer = writer;
    pic.custom_ptr = &out;
    bool ok = WebPEncode(&c, &pic) != 0;
    WebPPictureFree(&pic);
    return ok;
}

std::vector<uint32_t> halfSize(const Image& im, int& w, int& h) {
    w = std::max(1, im.w / 2);
    h = std::max(1, im.h / 2);
    auto r = resizeImage(im, w, h);
    return r->px;
}

int tolerance(int q) {   // int((1-s)*7 + s + 0.5), s = sqrt(q/100)
    double s = std::sqrt(std::clamp(q, 0, 100) / 100.0);
    return (int)((1 - s) * 7 + s + 0.5);
}

bool near3(uint32_t a, uint32_t b, int tol) {
    return std::abs(R(a) - R(b)) <= tol && std::abs(G(a) - G(b)) <= tol && std::abs(B(a) - B(b)) <= tol;
}

} // namespace

bool webpAutoLossless(const Frames& f, int quality) {
    size_t n = f.size();
    if (n == 0) return false;
    auto trial = [&](size_t idx) {
        int w, h;
        auto px = halfSize(*f[idx].img, w, h);
        std::vector<uint8_t> ll, lossy;
        std::thread t([&] { encodeFrame(px.data(), w, h, 100.f, true, false, ll); });
        encodeFrame(px.data(), w, h, (float)quality, false, false, lossy);
        t.join();
        return !ll.empty() && ll.size() < lossy.size();
    };
    size_t a = n * 2 / 5, b = n * 5 / 7;
    if (!trial(a)) return false;
    if (a == b) return false;
    return trial(b);
}

bool encodeWebp(const Frames& frames, const WebpOptions& o, const ByteSink& out, const Progress& prog, const Cancel& cancel,
                std::string& err) {
    if (frames.empty()) { err = "no frames"; return false; }
    const int W = frames[0].img->w, H = frames[0].img->h;
    if (W > 16383 || H > 16383) { err = "WebP는 16383px까지만 지원해요"; return false; }
    bool lossless = o.lossless;
    if (o.autoDecide) lossless = webpAutoLossless(frames, o.quality);
    const int tol = tolerance(o.quality);

    // ---- sequential stage: build frame jobs (diff vs previous frame)
    std::vector<WJob> jobs;
    jobs.reserve(frames.size());
    ImagePtr prev;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (cancel && cancel()) return false;
        ImagePtr im = frames[i].img;
        if (im->w != W || im->h != H) im = resizeImage(*im, W, H);
        WJob j;
        j.durMs = (int)std::clamp<int64_t>((frames[i].dur + 500) / 1000, 1, 0xFFFFFF);
        if (o.iphoneCompatible || !prev) {
            j.px = im->px;
            j.w = W; j.h = H;
        } else {
            const Image& p = *prev;
            int x0 = W, y0 = H, x1 = -1, y1 = -1;
            for (int y = 0; y < H; ++y) {
                const uint32_t *a = im->row(y), *b = p.row(y);
                if (!std::memcmp(a, b, W * 4)) continue;
                for (int x = 0; x < W; ++x) if (a[x] != b[x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
                y0 = std::min(y0, y); y1 = y;
            }
            j.blend = true;
            if (x1 < 0) {   // unchanged: 1x1 transparent frame
                j.w = j.h = 1;
                j.px = {0x00000000u};
            } else {
                x0 &= ~1; y0 &= ~1;   // ANMF offsets are in units of 2
                j.x = x0; j.y = y0;
                j.w = x1 - x0 + 1; j.h = y1 - y0 + 1;
                j.px.resize((size_t)j.w * j.h);
                for (int y = 0; y < j.h; ++y) std::memcpy(&j.px[(size_t)y * j.w], im->row(y0 + y) + x0, (size_t)j.w * 4);
                if (lossless) {   // identical pixels become transparent (previous frame shows through)
                    for (int y = 0; y < j.h; ++y)
                        for (int x = 0; x < j.w; ++x)
                            if (j.px[(size_t)y * j.w + x] == p.row(y0 + y)[x0 + x]) j.px[(size_t)y * j.w + x] = 0;
                } else {
                    // 8x8 blocks on the frame grid
                    int blocks = 0, similarBlocks = 0;
                    std::vector<uint32_t> keep = j.px;
                    for (int by = (y0 / 8) * 8; by < y0 + j.h; by += 8)
                        for (int bx = (x0 / 8) * 8; bx < x0 + j.w; bx += 8) {
                            int ex = std::min(bx + 8, x0 + j.w), ey = std::min(by + 8, y0 + j.h);
                            int sx = std::max(bx, x0), sy = std::max(by, y0);
                            bool similar = true;
                            int rr = 0, gg = 0, bb = 0, n = 0;
                            for (int y = sy; y < ey && similar; ++y)
                                for (int x = sx; x < ex; ++x) {
                                    uint32_t c = im->row(y)[x];
                                    if (!near3(c, p.row(y)[x], tol)) { similar = false; break; }
                                    rr += R(c); gg += G(c); bb += B(c); ++n;
                                }
                            ++blocks;
                            if (!similar) continue;
                            ++similarBlocks;
                            uint32_t fillc = o.qualityFirst ? 0u : rgb(rr / std::max(1, n), gg / std::max(1, n), bb / std::max(1, n));
                            for (int y = sy; y < ey; ++y)
                                for (int x = sx; x < ex; ++x) j.px[(size_t)(y - y0) * j.w + (x - x0)] = fillc;
                        }
                    if (o.qualityFirst) {
                        double metric = blocks ? 100.0 * similarBlocks / blocks : 0;
                        if ((double)j.w * j.h > 0.70 * W * H && metric < 50) {   // no gain: plain rectangle
                            j.px = std::move(keep);
                            j.blend = false;
                        }
                    }
                }
            }
        }
        jobs.push_back(std::move(j));
        prev = im;
    }

    // ---- parallel stage: encode frames
    std::atomic<size_t> next{0}, done{0};
    std::atomic<bool> fail{false};
    int nt = o.threads > 0 ? o.threads : std::max(2, (int)std::thread::hardware_concurrency() - 1);
    nt = std::min<int>(nt, (int)jobs.size());
    std::vector<std::thread> th;
    for (int t = 0; t < nt; ++t)
        th.emplace_back([&] {
            for (size_t i; (i = next++) < jobs.size();) {
                if (fail || (cancel && cancel())) { fail = true; return; }
                WJob& j = jobs[i];
                j.ok = encodeFrame(j.px.data(), j.w, j.h, (float)o.quality, lossless, j.blend, j.data);
                std::vector<uint32_t>().swap(j.px);
                if (!j.ok) fail = true;
                size_t d = ++done;
                if (prog) prog(0.95 * d / jobs.size());
            }
        });
    for (auto& t : th) t.join();
    if (fail) { if (!(cancel && cancel())) err = "WebP 프레임 인코딩 실패"; return false; }

    // ---- mux
    WebPMux* mux = WebPMuxNew();
    if (!mux) { err = "WebPMux"; return false; }
    WebPMuxAnimParams ap{0, std::clamp(o.loop, 0, 65535)};
    WebPMuxSetAnimationParams(mux, &ap);
    bool ok = true;
    for (auto& j : jobs) {
        WebPMuxFrameInfo fi{};
        fi.bitstream.bytes = j.data.data();
        fi.bitstream.size = j.data.size();
        fi.x_offset = j.x;
        fi.y_offset = j.y;
        fi.duration = j.durMs;
        fi.id = WEBP_CHUNK_ANMF;
        fi.dispose_method = WEBP_MUX_DISPOSE_NONE;
        fi.blend_method = j.blend ? WEBP_MUX_BLEND : WEBP_MUX_NO_BLEND;
        if (WebPMuxPushFrame(mux, &fi, 1) != WEBP_MUX_OK) { ok = false; break; }
    }
    if (ok && o.comment) {
        char buf[128];
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_s(&tmv, &now);
        int len = std::snprintf(buf, sizeof buf, "%s %s (%04d-%02d-%02d %02d:%02d:%02d); q=%d", kAppName, kAppVersion, tmv.tm_year + 1900,
                                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, o.quality);
        WebPData exif{(const uint8_t*)buf, (size_t)len};
        WebPMuxSetChunk(mux, "EXIF", &exif, 1);
    }
    WebPData outData;
    WebPDataInit(&outData);
    if (ok) ok = WebPMuxAssemble(mux, &outData) == WEBP_MUX_OK;
    if (ok) out(outData.bytes, outData.size);
    else err = "WebP 조립 실패";
    WebPDataClear(&outData);
    WebPMuxDelete(mux);
    if (ok && prog) prog(1.0);
    return ok;
}

bool decodeWebp(const std::vector<uint8_t>& d, Frames& out, std::string& err) {
    out.clear();
    WebPData wd{d.data(), d.size()};
    WebPAnimDecoderOptions o;
    WebPAnimDecoderOptionsInit(&o);
    o.color_mode = MODE_BGRA;
    o.use_threads = 1;
    WebPAnimDecoder* dec = WebPAnimDecoderNew(&wd, &o);
    if (!dec) { err = "WebP 파일을 읽을 수 없어요"; return false; }
    WebPAnimInfo info;
    WebPAnimDecoderGetInfo(dec, &info);
    int prevT = 0;
    while (WebPAnimDecoderHasMoreFrames(dec)) {
        uint8_t* buf = nullptr;
        int ts = 0;
        if (!WebPAnimDecoderGetNext(dec, &buf, &ts)) break;
        auto im = std::make_shared<Image>((int)info.canvas_width, (int)info.canvas_height);
        const uint32_t* s = (const uint32_t*)buf;
        for (size_t i = 0; i < im->px.size(); ++i) {   // flatten on white
            uint32_t c = s[i];
            int a = c >> 24;
            im->px[i] = rgb((R(c) * a + 255 * (255 - a)) / 255, (G(c) * a + 255 * (255 - a)) / 255, (B(c) * a + 255 * (255 - a)) / 255);
        }
        int dur = ts - prevT;
        prevT = ts;
        out.push_back({im, (dur > 0 ? dur : 100) * 1000LL});
    }
    WebPAnimDecoderDelete(dec);
    if (out.empty()) { err = "WebP 프레임이 없어요"; return false; }
    return true;
}

} // namespace bg
