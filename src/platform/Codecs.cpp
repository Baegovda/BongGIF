#include "platform/Codecs.h"

#include "core/Edit.h"

#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>
#include <mkvmuxer/mkvmuxer.h>
#include <mkvmuxer/mkvwriter.h>
#include <avif/avif.h>
#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <windows.h>

namespace bg {

// ------------------------------------------------------------------ helpers
static void bgraToI420(const Image& im, int W, int H, uint8_t* y, uint8_t* u, uint8_t* v, int ys, int cs) {
    // BT.601 limited range (what VP8/VP9 decoders assume by default)
    for (int j = 0; j < H; ++j) {
        const uint32_t* row = im.row(std::min(j, im.h - 1));
        for (int i = 0; i < W; ++i) {
            uint32_t c = row[std::min(i, im.w - 1)];
            int r = R(c), g = G(c), b = B(c);
            y[j * ys + i] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    for (int j = 0; j < (H + 1) / 2; ++j)
        for (int i = 0; i < (W + 1) / 2; ++i) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    int yy = std::min(j * 2 + dy, im.h - 1), xx = std::min(i * 2 + dx, im.w - 1);
                    uint32_t c = im.row(yy)[xx];
                    r += R(c); g += G(c); b += B(c); ++n;
                }
            r /= n; g /= n; b /= n;
            u[j * cs + i] = (uint8_t)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            v[j * cs + i] = (uint8_t)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
}

// ------------------------------------------------------------------ WebM
bool encodeWebm(const Frames& frames, const WebmOptions& o, const std::wstring& path, const Progress& prog, const Cancel& cancel,
                std::string& err) {
    if (frames.empty()) { err = "no frames"; return false; }
    const int W = frames[0].img->w & ~1, H = frames[0].img->h & ~1;
    vpx_codec_iface_t* iface = o.vp9 ? vpx_codec_vp9_cx() : vpx_codec_vp8_cx();
    vpx_codec_enc_cfg_t cfg;
    if (vpx_codec_enc_config_default(iface, &cfg, 0)) { err = "VPX 설정 실패"; return false; }
    cfg.g_w = W;
    cfg.g_h = H;
    cfg.g_timebase.num = 1;
    cfg.g_timebase.den = 1000;              // ms timestamps: variable frame rate is preserved
    cfg.rc_end_usage = VPX_VBR;
    cfg.rc_min_quantizer = 63 * (100 - std::clamp(o.quality, 0, 100)) / 100;
    cfg.rc_max_quantizer = std::max<unsigned>(cfg.rc_min_quantizer, 63);
    cfg.rc_target_bitrate = (unsigned)std::clamp<double>(W * (double)H * 30 * (0.05 + 0.25 * o.quality / 100.0) / 1000, 100, 50000);
    cfg.kf_mode = VPX_KF_AUTO;
    cfg.kf_max_dist = 5000;                 // at most 5 s between keyframes (timebase = ms)
    cfg.g_threads = o.threads > 0 ? o.threads : std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    cfg.g_lag_in_frames = 0;
    vpx_codec_ctx_t codec;
    if (vpx_codec_enc_init(&codec, iface, &cfg, 0)) { err = "VPX 인코더를 열 수 없어요"; return false; }
    if (o.vp9) {
        vpx_codec_control(&codec, VP8E_SET_CPUUSED, 4);
        vpx_codec_control(&codec, VP9E_SET_ROW_MT, 1);
        vpx_codec_control(&codec, VP9E_SET_TILE_COLUMNS, 2);
    } else {
        vpx_codec_control(&codec, VP8E_SET_CPUUSED, 4);
    }
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") || !fp) { vpx_codec_destroy(&codec); err = "파일을 쓸 수 없어요"; return false; }
    mkvmuxer::MkvWriter writer(fp);
    mkvmuxer::Segment seg;
    seg.Init(&writer);
    seg.set_mode(mkvmuxer::Segment::kFile);
    seg.OutputCues(true);
    seg.GetSegmentInfo()->set_writing_app("BongGIF 2.0");
    uint64_t track = seg.AddVideoTrack(W, H, 1);
    auto* vt = (mkvmuxer::VideoTrack*)seg.GetTrackByNumber(track);
    vt->set_codec_id(o.vp9 ? mkvmuxer::Tracks::kVp9CodecId : mkvmuxer::Tracks::kVp8CodecId);
    vpx_image_t img;
    vpx_img_alloc(&img, VPX_IMG_FMT_I420, W, H, 1);
    bool ok = true;
    int64_t tms = 0;
    auto drain = [&] {
        vpx_codec_iter_t it = nullptr;
        while (const vpx_codec_cx_pkt_t* pkt = vpx_codec_get_cx_data(&codec, &it)) {
            if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) continue;
            bool key = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
            if (!seg.AddFrame((const uint8_t*)pkt->data.frame.buf, pkt->data.frame.sz, track, (uint64_t)pkt->data.frame.pts * 1000000ull, key))
                ok = false;
        }
    };
    for (size_t i = 0; i < frames.size() && ok; ++i) {
        if (cancel && cancel()) { ok = false; break; }
        bgraToI420(*frames[i].img, W, H, img.planes[0], img.planes[1], img.planes[2], img.stride[0], img.stride[1]);
        int64_t dur = std::max<int64_t>(1, (frames[i].dur + 500) / 1000);
        if (vpx_codec_encode(&codec, &img, tms, (unsigned long)dur, 0, VPX_DL_GOOD_QUALITY)) { err = vpx_codec_error(&codec); ok = false; break; }
        drain();
        tms += dur;
        if (prog) prog(0.97 * (i + 1) / frames.size());
    }
    if (ok) {
        vpx_codec_encode(&codec, nullptr, -1, 1, 0, VPX_DL_GOOD_QUALITY);   // flush
        drain();
        seg.set_duration((double)tms * 1000000.0 / seg.GetSegmentInfo()->timecode_scale());
        ok = seg.Finalize();
    }
    vpx_img_free(&img);
    vpx_codec_destroy(&codec);
    fclose(fp);
    if (!ok) {
        DeleteFileW(path.c_str());
        if (err.empty() && !(cancel && cancel())) err = "WebM 인코딩 실패";
        return false;
    }
    if (prog) prog(1.0);
    return true;
}

// ------------------------------------------------------------------ AVIF
int avifQualityFor(int q) {
    if (q >= 100) return 100;
    return std::max(0, (int)(std::sqrt(q / 100.0) * 100 + 0.5 - 15));
}

bool encodeAvif(const Frames& frames, int quality, int loop, const ByteSink& out, const Progress& prog, const Cancel& cancel,
                std::string& err) {
    if (frames.empty()) { err = "no frames"; return false; }
    const int W = frames[0].img->w & ~1, H = frames[0].img->h & ~1;
    avifPixelFormat fmt = quality >= 74 ? AVIF_PIXEL_FORMAT_YUV444 : quality >= 50 ? AVIF_PIXEL_FORMAT_YUV422 : AVIF_PIXEL_FORMAT_YUV420;
    avifEncoder* enc = avifEncoderCreate();
    if (!enc) { err = "AVIF 인코더를 만들 수 없어요"; return false; }
    enc->codecChoice = AVIF_CODEC_CHOICE_AOM;
    enc->speed = 8;
    enc->quality = avifQualityFor(quality);
    enc->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    enc->timescale = 1000;
    enc->repetitionCount = loop > 0 ? loop : AVIF_REPETITION_COUNT_INFINITE;
    enc->maxThreads = (int)std::max(2u, std::thread::hardware_concurrency());
    enc->keyframeInterval = 0;
    bool ok = true;
    for (size_t i = 0; i < frames.size() && ok; ++i) {
        if (cancel && cancel()) { ok = false; break; }
        avifImage* im = avifImageCreate(W, H, 8, fmt);
        im->yuvRange = AVIF_RANGE_FULL;
        im->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, im);
        rgb.format = AVIF_RGB_FORMAT_BGRA;
        rgb.ignoreAlpha = AVIF_TRUE;
        const Image& src = *frames[i].img;
        std::vector<uint32_t> buf((size_t)W * H);
        for (int y = 0; y < H; ++y) std::copy_n(src.row(std::min(y, src.h - 1)), std::min(W, src.w), buf.data() + (size_t)y * W);
        rgb.pixels = (uint8_t*)buf.data();
        rgb.rowBytes = W * 4;
        if (avifImageRGBToYUV(im, &rgb) != AVIF_RESULT_OK) ok = false;
        uint64_t dur = std::max<int64_t>(1, (frames[i].dur + 500) / 1000);
        avifAddImageFlags fl = frames.size() == 1 ? AVIF_ADD_IMAGE_FLAG_SINGLE : AVIF_ADD_IMAGE_FLAG_NONE;
        if (ok) {
            avifResult r = avifEncoderAddImage(enc, im, dur, fl);
            if (r != AVIF_RESULT_OK) { err = avifResultToString(r); ok = false; }
        }
        avifImageDestroy(im);
        if (prog) prog(0.97 * (i + 1) / frames.size());
    }
    avifRWData data = AVIF_DATA_EMPTY;
    if (ok) {
        avifResult r = avifEncoderFinish(enc, &data);
        if (r != AVIF_RESULT_OK) { err = avifResultToString(r); ok = false; }
    }
    if (ok) out(data.data, data.size);
    avifRWDataFree(&data);
    avifEncoderDestroy(enc);
    if (!ok && err.empty() && !(cancel && cancel())) err = "AVIF 인코딩 실패";
    if (ok && prog) prog(1.0);
    return ok;
}

bool decodeAvif(const std::vector<uint8_t>& data, Frames& out, std::string& err) {
    out.clear();
    avifDecoder* dec = avifDecoderCreate();
    if (!dec) { err = "AVIF"; return false; }
    dec->maxThreads = 4;
    if (avifDecoderSetIOMemory(dec, data.data(), data.size()) != AVIF_RESULT_OK || avifDecoderParse(dec) != AVIF_RESULT_OK) {
        avifDecoderDestroy(dec);
        err = "AVIF 파일을 읽을 수 없어요";
        return false;
    }
    while (avifDecoderNextImage(dec) == AVIF_RESULT_OK) {
        avifRGBImage rgbIm;
        avifRGBImageSetDefaults(&rgbIm, dec->image);
        rgbIm.format = AVIF_RGB_FORMAT_BGRA;
        rgbIm.depth = 8;
        auto im = std::make_shared<Image>((int)dec->image->width, (int)dec->image->height);
        rgbIm.pixels = (uint8_t*)im->px.data();
        rgbIm.rowBytes = im->w * 4;
        if (avifImageYUVToRGB(dec->image, &rgbIm) != AVIF_RESULT_OK) break;
        for (auto& c : im->px) {   // flatten on white
            int a = c >> 24;
            c = rgb((R(c) * a + 255 * (255 - a)) / 255, (G(c) * a + 255 * (255 - a)) / 255, (B(c) * a + 255 * (255 - a)) / 255);
        }
        double d = dec->imageTiming.duration;
        out.push_back({im, (int64_t)((d > 0 ? d : 0.1) * 1e6)});
    }
    avifDecoderDestroy(dec);
    if (out.empty()) { err = "AVIF 프레임이 없어요"; return false; }
    return true;
}

// ------------------------------------------------------------------ SVG
bool loadSvg(const std::wstring& path, int maxSide, Image& out, std::string& err) {
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string p(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, p.data(), n, nullptr, nullptr);
    p.resize(n - 1);
    auto doc = lunasvg::Document::loadFromFile(p);
    if (!doc) { err = "SVG를 읽을 수 없어요"; return false; }
    float w = doc->width(), h = doc->height();
    if (w <= 0 || h <= 0) { w = 512; h = 512; }
    float s = maxSide > 0 ? std::min(1.f, maxSide / std::max(w, h)) : 1.f;
    if (std::max(w, h) < 256) s = 512 / std::max(w, h);   // tiny icons: render at a usable size
    int W = std::max(1, (int)std::lround(w * s)), H = std::max(1, (int)std::lround(h * s));
    auto bmp = doc->renderToBitmap(W, H, 0xFFFFFFFF);   // white background
    if (bmp.isNull()) { err = "SVG를 그릴 수 없어요"; return false; }
    out = Image(W, H);
    for (int y = 0; y < H; ++y) {
        const uint32_t* r = (const uint32_t*)(bmp.data() + (size_t)y * bmp.stride());
        for (int x = 0; x < W; ++x) out.row(y)[x] = r[x] | 0xFF000000u;   // premultiplied ARGB on opaque white
    }
    return true;
}

} // namespace bg
