#pragma once
// Colour quantisation for GIF: Xiaolin Wu's variance-minimising box split (weighted histogram),
// exact palette for <=256 colours, nearest-colour remap with optional Floyd-Steinberg dithering.
#include <cstdint>
#include <vector>

namespace bg {

struct Palette {
    std::vector<uint32_t> colors;   // 0x00RRGGBB
};

// px: w*h BGRA pixels; mask (optional, may be empty): 1 = transparent pixel (ignored).
// weightCenter: pixels inside [w/4,3w/4)x[h/4,3h/4) count 5x (centre-weighted palette).
Palette wuQuantize(const uint32_t* px, int w, int h, const uint8_t* mask, int maxColors, bool weightCenter);

// Returns false if more than `limit` distinct colours.
bool exactPalette(const uint32_t* px, size_t n, const uint8_t* mask, int limit, Palette& out);

// Map pixels to palette indices (dither = Floyd-Steinberg, serpentine). Masked pixels -> tIndex.
void remap(const uint32_t* px, int w, int h, const uint8_t* mask, const Palette& pal, bool dither, uint8_t tIndex,
           std::vector<uint8_t>& out);

// quantisation methods: 0 = libimagequant (default), 1 = NeuQuant (pngnq),
// 2 = Octree (+ merge of near-equal palette entries, tolerance 10 - q/10), 3 = variance box split (Wu).
enum QuantMethod { QM_LIQ = 0, QM_NEU = 1, QM_OCTREE = 2, QM_WU = 3 };
struct QuantRequest {
    const uint32_t* px = nullptr;
    int w = 0, h = 0;
    const uint8_t* mask = nullptr;   // 1 = transparent
    int maxColors = 255;
    int quality = 80;
    QuantMethod method = QM_LIQ;
    bool dither = true;
    bool centerWeighted = false;
    std::vector<uint32_t> fixedColors;   // LIQ only
};
// Fills palette + indices; tIndex = index used for transparent pixels (-1 when none).
void quantizeFrame(const QuantRequest& q, Palette& pal, std::vector<uint8_t>& idx, int& tIndex);
Palette neuQuant(const uint32_t* px, int w, int h, const uint8_t* mask, int colors, int sampleFactor);
Palette octreeQuantize(const uint32_t* px, int w, int h, const uint8_t* mask, int colors, int mergeTol);

} // namespace bg
