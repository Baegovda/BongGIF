#pragma once
// Animated GIF encoder/decoder.
// Encoder pipeline: sequential diff vs. displayed buffer -> dirty rect +
// transparent unchanged pixels (identical frames merge their delay) -> per-frame quantisation + LZW
// in worker threads -> single ordered writer (GIF89a, NETSCAPE loop, GCE disposal 1, comment).
#include "core/Image.h"

#include <string>

namespace bg {

constexpr const char* kAppName = "BongGIF";
constexpr const char* kAppVersion = "2.0";

struct GifOptions {
    int quality = 80;              // 1..100
    bool dither = true;
    bool centerWeighted = false;
    bool removeSimilar = true;
    bool skipQuantQ100 = true;
    int loop = 0;                  // 0 = infinite
    bool comment = true;
    int threads = 0;               // 0 = auto
    int method = 0;                // 0 LIQ (libimagequant), 1 NeuQuant, 2 Octree, 3 variance split (Wu)
    std::vector<uint32_t> protectColors;   // 0x00RRGGBB colours kept exactly (LIQ only)
};

int colorsForQuality(int q);       // clamp(int(2^(q*6/79+1.6203)), 4, 255)
int toleranceForQuality(int q);    // clamp(int((100-q)/50*14+1), 1, 15)

using ByteSink = std::function<void(const uint8_t*, size_t)>;

// Returns false on cancel. Throws std::runtime_error on bad input.
bool encodeGif(const Frames& frames, const GifOptions& opt, const ByteSink& out, const Progress& prog = {},
               const Cancel& cancel = {});

// Decode (composited to opaque frames on white). Returns false + err on failure.
bool decodeGif(const std::vector<uint8_t>& data, Frames& out, std::string& err);

// LZW (exposed for tests)
std::vector<uint8_t> lzwEncode(const uint8_t* idx, size_t n, int minCode);
bool lzwDecode(const uint8_t* data, size_t n, int minCode, size_t npix, std::vector<uint8_t>& out);

} // namespace bg
