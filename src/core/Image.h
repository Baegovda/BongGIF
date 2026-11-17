#pragma once
// Frame model. Pixels are 32-bit BGRA (GDI byte order, alpha 255), immutable once shared,
// so the undo stack can keep cheap shallow copies of the frame list.
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace bg {

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // 0xAARRGGBB as uint32 = bytes B,G,R,A
    Image() = default;
    Image(int w_, int h_, uint32_t fill = 0xFF000000u) : w(w_), h(h_), px((size_t)w_ * h_, fill) {}
    uint32_t* row(int y) { return px.data() + (size_t)y * w; }
    const uint32_t* row(int y) const { return px.data() + (size_t)y * w; }
    size_t bytes() const { return px.size() * 4; }
};
using ImagePtr = std::shared_ptr<const Image>;

inline uint32_t rgb(int r, int g, int b) { return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
inline int R(uint32_t c) { return (c >> 16) & 255; }
inline int G(uint32_t c) { return (c >> 8) & 255; }
inline int B(uint32_t c) { return c & 255; }
inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

struct Frame {
    ImagePtr img;
    int64_t dur = 100000;   // microseconds
};
using Frames = std::vector<Frame>;

using Progress = std::function<void(double)>;   // 0..1
using Cancel = std::function<bool()>;

} // namespace bg
