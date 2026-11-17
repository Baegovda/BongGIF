#pragma once
// WebM (libvpx VP8/VP9 + libwebm), AVIF (libavif + libaom), SVG import (lunasvg).
// Encoder parameters:
//   WebM: VBR, rc_min_quantizer = 63*(100-q)/100, keyframe interval max 5 s
//   AVIF: libaom speed 8, quality = q==100 ? 100 : max(0, int(sqrt(q/100)*100 + 0.5 - 15)),
//         q >= 74 -> 4:4:4, 50..73 -> 4:2:2, < 50 -> 4:2:0, even width/height
#include "core/Gif.h"

#include <string>

namespace bg {

struct WebmOptions {
    int quality = 80;
    bool vp9 = true;     // false = VP8
    int threads = 0;
};
bool encodeWebm(const Frames& frames, const WebmOptions& o, const std::wstring& path, const Progress& prog, const Cancel& cancel,
                std::string& err);

int avifQualityFor(int q);
bool encodeAvif(const Frames& frames, int quality, int loop, const ByteSink& out, const Progress& prog, const Cancel& cancel,
                std::string& err);
bool decodeAvif(const std::vector<uint8_t>& data, Frames& out, std::string& err);

bool loadSvg(const std::wstring& path, int maxSide, Image& out, std::string& err);

} // namespace bg
