#pragma once
// Animated WebP with libwebp 1.6.0:
//  * per-frame WebPEncode: method 5 (constant), segments 4, sns 50, filter 60 (0 for blended
//    diff frames), filter_type 1, alpha_compression 1, alpha_filtering 1, alpha_quality 100,
//    pass 1, near_lossless 100, use_sharp_yuv 0; lossless quality = min(q, 80)
//  * auto lossy/lossless (bWebpAutoDecide): test frames at n*2/5 and n*5/7, half size,
//    lossless must be smaller both times
//  * iPhone-compatible mode (default on): every frame full canvas, no blend
//    off: first frame full, then changed rectangle (even offsets) + alpha blend; unchanged frame
//    -> 1x1 transparent frame; lossless = identical pixels transparent; lossy "quality first"
//    = 8x8 blocks within tolerance transparent (tolerance int((1-s)*7+s+0.5), s=sqrt(q/100)),
//    dropped back to a plain rectangle if area > 70% and < 50% blocks transparent;
//    lossy otherwise = similar 8x8 blocks flattened to their average colour
//  * WebPMux: ANIM {bg 0, loop}, ANMF frames (dispose none), comment in an EXIF chunk
#include "core/Gif.h"

namespace bg {

struct WebpOptions {
    int quality = 80;
    bool lossless = false;
    bool autoDecide = false;          // bWebpAutoDecide
    bool iphoneCompatible = true;     // bWebpIphoneCompatible
    bool qualityFirst = true;         // bWebpLossyBlendModeQuality
    bool comment = true;
    int loop = 0;
    int threads = 0;
};

bool encodeWebp(const Frames& frames, const WebpOptions& o, const ByteSink& out, const Progress& prog, const Cancel& cancel,
                std::string& err);
bool decodeWebp(const std::vector<uint8_t>& data, Frames& out, std::string& err);
bool webpAutoLossless(const Frames& frames, int quality);   // the auto-decide test, exposed for tests

} // namespace bg
