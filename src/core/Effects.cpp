// Image effects: 23 filters + posterize / invert.
// OpenCV-based ones (denoise, cartoon, inpaint)
// are reimplemented: mean-shift filtering, Lab mean-shift cartoon, onion-peel inpaint (radius 3).
#include "core/Effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <queue>

namespace bg {

namespace {

void applyLut(Image& im, const uint8_t* lr, const uint8_t* lg, const uint8_t* lb) {
    for (auto& c : im.px) c = rgb(lr[R(c)], lg[G(c)], lb[B(c)]);
}
void applyLut(Image& im, const uint8_t* l) { applyLut(im, l, l, l); }
int luma601(uint32_t c) { return (R(c) * 0x132 + G(c) * 0x259 + B(c) * 0x75) >> 10; }
uint32_t mix(uint32_t a, uint32_t b, int m) {   // m 0..255 weight of b
    return rgb((R(a) * (255 - m) + R(b) * m) / 255, (G(a) * (255 - m) + G(b) * m) / 255, (B(a) * (255 - m) + B(b) * m) / 255);
}
double smooth(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / std::max(1e-9, e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// ---- simple colour effects
void brightnessContrast(Image& im, int br, int ct) {
    uint8_t l[256];
    for (int i = 0; i < 256; ++i) l[i] = (uint8_t)clamp255((int)std::floor((i - 128) * (ct + 100) / 100.0 + (br + 128) + 0.5));
    applyLut(im, l);
}
void gammaFx(Image& im, int v) {
    double g = std::clamp(v * 5 / 100.0, 0.1, 5.0);
    uint8_t l[256];
    for (int i = 0; i < 256; ++i) l[i] = (uint8_t)clamp255((int)(std::pow(i, 1 / g) / (std::pow(255, 1 / g) / 255)));
    applyLut(im, l);
}
void gray(Image& im) { for (auto& c : im.px) { int y = luma601(c); c = rgb(y, y, y); } }
void sepia(Image& im) {
    for (auto& c : im.px) {
        int r = R(c), g = G(c), b = B(c);
        c = rgb(std::min(255, (r * 0x192 + g * 0x313 + b * 0xc1) >> 10), std::min(255, (r * 0x165 + g * 0x2be + b * 0xac) >> 10),
                std::min(255, (r * 0x116 + g * 0x222 + b * 0x86) >> 10));
    }
}
void saturation(Image& im, int amt) {
    double k = 1 + amt / 100.0;
    for (auto& c : im.px) {
        double y = 0.299 * R(c) + 0.587 * G(c) + 0.114 * B(c);
        c = rgb(clamp255((int)(y + (R(c) - y) * k)), clamp255((int)(y + (G(c) - y) * k)), clamp255((int)(y + (B(c) - y) * k)));
    }
}
void scanline(Image& im, int amt) {
    for (int y = 0; y < im.h; y += 2)
        for (int x = 0; x < im.w; ++x) {
            uint32_t& c = im.row(y)[x];
            c = rgb(clamp255(R(c) + amt), clamp255(G(c) + amt), clamp255(B(c) + amt));
        }
}
void vignette(Image& im, int s) {
    double cx = (im.w - 1) / 2.0, cy = (im.h - 1) / 2.0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double dx = (x - cx) / (im.w / 2.0), dy = (y - cy) / (im.h / 2.0);
            int m = std::clamp((int)((dx * dx + dy * dy) / 2 * s / 100.0 * 2 * 255), 0, 255);
            uint32_t& c = im.row(y)[x];
            c = rgb((R(c) * (255 - m)) >> 8, (G(c) * (255 - m)) >> 8, (B(c) * (255 - m)) >> 8);
        }
}
void sharpen(Image& im, int p) {
    Image bl = im;
    boxBlur(bl, 2);
    for (size_t i = 0; i < im.px.size(); ++i) {
        uint32_t s = im.px[i], b = bl.px[i];
        im.px[i] = rgb(clamp255(R(s) + (((R(s) - R(b)) * p) >> 7)), clamp255(G(s) + (((G(s) - G(b)) * p) >> 7)),
                       clamp255(B(s) + (((B(s) - B(b)) * p) >> 7)));
    }
}
void monotone(Image& im, int hue) {
    double h = (hue % 360) / 60.0, s = 0.63, v = 1.0;
    int i = (int)h % 6;
    double f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double tr[6] = {v, q, p, p, t, v}, tg[6] = {t, v, v, q, p, p}, tb[6] = {p, p, t, v, v, q};
    for (auto& c : im.px) {
        double y = luma601(c) * 1.25;
        c = rgb(clamp255((int)(y * tr[i])), clamp255((int)(y * tg[i])), clamp255((int)(y * tb[i])));
    }
}
void k2rgb(double k, double out[3]) {   // Tanner Helland approximation
    double t = k / 100;
    out[0] = t <= 66 ? 255 : 329.698727446 * std::pow(t - 60, -0.1332047592);
    out[1] = t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661 : 288.1221695283 * std::pow(t - 60, -0.0755148492);
    out[2] = t >= 66 ? 255 : (t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307);
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(out[i], 1.0, 255.0);
}
void temperature(Image& im, int kelvin) {
    double a[3], n[3];
    k2rgb(kelvin, a);
    k2rgb(6500, n);
    uint8_t l[3][256];
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) l[c][i] = (uint8_t)clamp255((int)(i * a[c] / n[c]));
    applyLut(im, l[0], l[1], l[2]);
}
// Autowhite: 2%/98% percentile stretch; per channel only when the luminance 98% point > 150;
// "reference white" replaces the upper bound with the chosen colour
void autowhite(Image& im, bool useRef, uint32_t ref) {
    std::vector<int> hist[4];
    for (auto& h : hist) h.assign(256, 0);
    for (auto c : im.px) { hist[0][R(c)]++; hist[1][G(c)]++; hist[2][B(c)]++; hist[3][luma601(c)]++; }
    auto pct = [&](int ch, double p) {
        size_t target = (size_t)(p / 100 * im.px.size()), acc = 0;
        for (int i = 0; i < 256; ++i) { acc += hist[ch][i]; if (acc > target) return i; }
        return 255;
    };
    uint8_t l[3][256];
    bool per = pct(3, 98) > 150 || useRef;
    int refc[3] = {R(ref), G(ref), B(ref)};
    for (int c = 0; c < 3; ++c) {
        int lo = per ? pct(c, 2) : pct(3, 2), hi = useRef ? std::max(refc[c], 1) : (per ? pct(c, 98) : pct(3, 98));
        for (int i = 0; i < 256; ++i) l[c][i] = (uint8_t)clamp255((i - lo) * 255 / std::max(1, hi - lo));
    }
    applyLut(im, l[0], l[1], l[2]);
}
void colorCurve(Image& im, int shadows, int highlights, int contrast) {
    uint8_t l[256];
    for (int i = 0; i < 256; ++i) {
        double t = i / 255.0;
        t += shadows / 100.0 * 0.35 * (1 - t) * (1 - t) * t * 4 / 1.2;
        t += highlights / 100.0 * 0.35 * t * t * (1 - t) * 4 / 1.2;
        double s = t - 0.5;
        t = 0.5 + s + contrast / 100.0 * (s - 4 * s * s * s) * 0.9;
        l[i] = (uint8_t)clamp255((int)std::lround(t * 255));
    }
    applyLut(im, l);
}
void posterize(Image& im, int levels) {
    levels = std::clamp(levels, 2, 32);
    uint8_t l[256];
    for (int i = 0; i < 256; ++i) l[i] = (uint8_t)std::lround(std::round(i * (levels - 1) / 255.0) * 255.0 / (levels - 1));
    applyLut(im, l);
}

// ---- blur-based
void tiltshift(Image& im, int amt, int band) {
    Image bl = im;
    boxBlur(bl, 1 + amt / 12);
    for (int y = 0; y < im.h; ++y) {
        double d = std::abs(y - im.h / 2.0) / (im.h / 2.0);
        int m = (int)(std::clamp((d - band / 100.0) / std::max(1e-3, 1 - band / 100.0), 0.0, 1.0) * 255);
        for (int x = 0; x < im.w; ++x) im.row(y)[x] = mix(im.row(y)[x], bl.row(y)[x], m);
    }
}
// out-of-focus (sharp around the point, blurred outside) / region blur (blurred around the point)
void focusBlur(Image& im, int amt, int radiusPct, int px, int py, bool invert) {
    Image bl = im;
    boxBlur(bl, 1 + amt / 8);
    if (px < 0) { px = im.w / 2; py = im.h / 2; }
    double r0 = radiusPct / 100.0 * std::hypot(im.w, im.h) / 2, r1 = r0 + std::hypot(im.w, im.h) * 0.12;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double m = smooth(r0, r1, std::hypot(x - px, y - py));
            if (invert) m = 1 - m;
            im.row(y)[x] = mix(im.row(y)[x], bl.row(y)[x], (int)(m * 255));
        }
}
void bloom(Image& im, int shiny, int soften, int contrast) {
    Image glow(im.w, im.h);
    int thr = 255 - shiny * 2;
    for (size_t i = 0; i < im.px.size(); ++i) {
        uint32_t c = im.px[i];
        int y = luma601(c);
        int k = std::max(0, y - thr);
        glow.px[i] = rgb(R(c) * k / 255, G(c) * k / 255, B(c) * k / 255);
    }
    boxBlur(glow, 2 + soften / 6);
    double gain = 0.6 + shiny / 60.0;
    for (size_t i = 0; i < im.px.size(); ++i) {
        uint32_t c = im.px[i], g = glow.px[i];
        im.px[i] = rgb(clamp255(R(c) + (int)(R(g) * gain)), clamp255(G(c) + (int)(G(g) * gain)), clamp255(B(c) + (int)(B(g) * gain)));
    }
    if (contrast) brightnessContrast(im, 0, contrast);
}
void mosaicRect(Image& im, int s, int x0, int y0, int x1, int y1, bool oval) {
    s = std::max(2, s);
    double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0, rx = std::max(1, x1 - x0) / 2.0, ry = std::max(1, y1 - y0) / 2.0;
    for (int by = y0; by < y1; by += s)
        for (int bx = x0; bx < x1; bx += s) {
            int ex = std::min(bx + s, x1), ey = std::min(by + s, y1), n = 0, r = 0, g = 0, b = 0;
            for (int y = by; y < ey; ++y) for (int x = bx; x < ex; ++x) { uint32_t c = im.row(y)[x]; r += R(c); g += G(c); b += B(c); ++n; }
            uint32_t m = rgb(r / n, g / n, b / n);
            for (int y = by; y < ey; ++y)
                for (int x = bx; x < ex; ++x) {
                    if (oval) { double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry; if (dx * dx + dy * dy > 1) continue; }
                    im.row(y)[x] = m;
                }
        }
}
void rectBlur(Image& im, int amt, int x0, int y0, int x1, int y1, bool oval) {
    Image bl = im;
    boxBlur(bl, 1 + amt / 6);
    double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0, rx = std::max(1, x1 - x0) / 2.0, ry = std::max(1, y1 - y0) / 2.0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            if (oval) { double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry; if (dx * dx + dy * dy > 1) continue; }
            im.row(y)[x] = bl.row(y)[x];
        }
}

// ---- stylised
void printFx(Image& im, int dot) {   // halftone at 45° (sin/cos screen)
    double d = std::max(3, dot), ca = std::cos(0.785398), sa = std::sin(0.785398);
    Image src = im;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double u = x * ca + y * sa, v = -x * sa + y * ca;
            double cu = (std::floor(u / d) + 0.5) * d, cv = (std::floor(v / d) + 0.5) * d;
            int sx = std::clamp((int)(cu * ca - cv * sa), 0, im.w - 1), sy = std::clamp((int)(cu * sa + cv * ca), 0, im.h - 1);
            uint32_t c = src.row(sy)[sx];
            double ink = 1 - luma601(c) / 255.0;
            double rad = d * 0.5 * std::sqrt(ink) * 1.25;
            bool in = std::hypot(u - cu, v - cv) <= rad;
            uint32_t paper = rgb(246, 242, 232);
            uint32_t inkc = rgb(R(c) * 6 / 10, G(c) * 6 / 10, B(c) * 6 / 10);
            im.row(y)[x] = in ? inkc : paper;
        }
}
void asciiFx(Image& im, int cell) {
    // 5x7 glyphs for " .:-=+*#%@" (dark -> bright)
    static const uint8_t glyph[10][7] = {
        {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 4}, {0, 0, 4, 0, 0, 4, 0}, {0, 0, 0, 14, 0, 0, 0}, {0, 0, 31, 0, 31, 0, 0},
        {0, 4, 4, 31, 4, 4, 0}, {0, 21, 14, 31, 14, 21, 0}, {10, 31, 10, 10, 31, 10, 0}, {25, 26, 4, 4, 11, 19, 0},
        {14, 17, 23, 21, 22, 16, 15}};
    int cw = std::max(4, cell), ch = cw * 7 / 5;
    Image src = im;
    std::fill(im.px.begin(), im.px.end(), rgb(0, 0, 0));
    for (int by = 0; by < im.h; by += ch)
        for (int bx = 0; bx < im.w; bx += cw) {
            int ex = std::min(bx + cw, im.w), ey = std::min(by + ch, im.h), n = 0, r = 0, g = 0, b = 0;
            for (int y = by; y < ey; ++y) for (int x = bx; x < ex; ++x) { uint32_t c = src.row(y)[x]; r += R(c); g += G(c); b += B(c); ++n; }
            uint32_t avg = rgb(r / n, g / n, b / n);
            int gi = std::min(9, luma601(avg) * 10 / 256);
            uint32_t col = rgb(std::min(255, R(avg) + 60), std::min(255, G(avg) + 60), std::min(255, B(avg) + 60));
            for (int y = by; y < ey; ++y)
                for (int x = bx; x < ex; ++x) {
                    int gx = (x - bx) * 5 / cw, gy = (y - by) * 7 / ch;
                    if (glyph[gi][gy] & (16 >> gx)) im.row(y)[x] = col;
                }
        }
}
void chromakey(Image& im, int tol, bool adjacent, int px, int py, uint32_t key, bool whiteFill) {
    double t = tol * 4.42;   // 0..100 -> 0..442 (max RGB distance)
    auto match = [&](uint32_t c) { return std::hypot(std::hypot(R(c) - R(key), G(c) - G(key)), B(c) - B(key)) <= t; };
    uint32_t fill = whiteFill ? rgb(255, 255, 255) : rgb(0, 0, 0);
    if (!adjacent || px < 0) {
        for (auto& c : im.px) if (match(c)) c = fill;
        return;
    }
    std::vector<uint8_t> seen(im.px.size(), 0);
    std::queue<int> q;
    int s = py * im.w + px;
    if (!match(im.px[s])) return;
    q.push(s);
    seen[s] = 1;
    while (!q.empty()) {
        int i = q.front();
        q.pop();
        int x = i % im.w, y = i / im.w;
        int nb[4] = {x > 0 ? i - 1 : -1, x + 1 < im.w ? i + 1 : -1, y > 0 ? i - im.w : -1, y + 1 < im.h ? i + im.w : -1};
        for (int n : nb)
            if (n >= 0 && !seen[n] && match(im.px[n])) { seen[n] = 1; q.push(n); }
        im.px[i] = fill;
    }
}

// sRGB <-> Lab (8-bit packed: L*2.55, a+128, b+128) for the cartoon effect
void rgb2lab(uint32_t c, int& L, int& A, int& Bv) {
    auto lin = [](int v) { double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
    double r = lin(R(c)), g = lin(G(c)), b = lin(B(c));
    double X = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, Y = 0.2126 * r + 0.7152 * g + 0.0722 * b,
           Z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116; };
    double fx = f(X), fy = f(Y), fz = f(Z);
    L = clamp255((int)((116 * fy - 16) * 2.55));
    A = clamp255((int)(500 * (fx - fy) + 128));
    Bv = clamp255((int)(200 * (fy - fz) + 128));
}
uint32_t lab2rgb(int L8, int A8, int B8) {
    double L = L8 / 2.55, a = A8 - 128.0, b = B8 - 128.0;
    double fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - b / 200;
    auto fi = [](double t) { return t * t * t > 0.008856 ? t * t * t : (t - 16.0 / 116) / 7.787; };
    double X = fi(fx) * 0.95047, Y = fi(fy), Z = fi(fz) * 1.08883;
    double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z, g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z, bb = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
    auto gm = [](double v) { v = std::clamp(v, 0.0, 1.0); return clamp255((int)std::lround((v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055) * 255)); };
    return rgb(gm(r), gm(g), gm(bb));
}

Image resized(const Image& s, int w, int h) {   // bilinear
    Image o(w, h);
    for (int y = 0; y < h; ++y) {
        double fy = std::clamp((y + 0.5) * s.h / h - 0.5, 0.0, s.h - 1.0);
        int y0 = (int)fy, y1 = std::min(y0 + 1, s.h - 1);
        double ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            double fx = std::clamp((x + 0.5) * s.w / w - 0.5, 0.0, s.w - 1.0);
            int x0 = (int)fx, x1 = std::min(x0 + 1, s.w - 1);
            double tx = fx - x0;
            uint32_t a = s.row(y0)[x0], b = s.row(y0)[x1], c = s.row(y1)[x0], d = s.row(y1)[x1];
            auto L = [&](int sh) {
                double top = ((a >> sh) & 255) * (1 - tx) + ((b >> sh) & 255) * tx, bot = ((c >> sh) & 255) * (1 - tx) + ((d >> sh) & 255) * tx;
                return clamp255((int)std::lround(top * (1 - ty) + bot * ty));
            };
            o.row(y)[x] = rgb(L(16), L(8), L(0));
        }
    }
    return o;
}

} // namespace

// ------------------------------------------------------------------ shared building blocks
void boxBlur(Image& im, int r) {      // 3-pass box blur ~ gaussian
    if (r < 1) return;
    std::vector<uint32_t> tmp(im.px.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < im.h; ++y) {
            const uint32_t* s = im.row(y);
            uint32_t* d = tmp.data() + (size_t)y * im.w;
            int sr = 0, sg = 0, sb = 0, n = 2 * r + 1;
            for (int x = -r; x <= r; ++x) { uint32_t c = s[std::clamp(x, 0, im.w - 1)]; sr += R(c); sg += G(c); sb += B(c); }
            for (int x = 0; x < im.w; ++x) {
                d[x] = rgb(sr / n, sg / n, sb / n);
                uint32_t a = s[std::clamp(x - r, 0, im.w - 1)], b = s[std::clamp(x + r + 1, 0, im.w - 1)];
                sr += R(b) - R(a); sg += G(b) - G(a); sb += B(b) - B(a);
            }
        }
        for (int x = 0; x < im.w; ++x) {
            int sr = 0, sg = 0, sb = 0, n = 2 * r + 1;
            for (int y = -r; y <= r; ++y) { uint32_t c = tmp[(size_t)std::clamp(y, 0, im.h - 1) * im.w + x]; sr += R(c); sg += G(c); sb += B(c); }
            for (int y = 0; y < im.h; ++y) {
                im.row(y)[x] = rgb(sr / n, sg / n, sb / n);
                uint32_t a = tmp[(size_t)std::clamp(y - r, 0, im.h - 1) * im.w + x];
                uint32_t b = tmp[(size_t)std::clamp(y + r + 1, 0, im.h - 1) * im.w + x];
                sr += R(b) - R(a); sg += G(b) - G(a); sb += B(b) - B(a);
            }
        }
    }
}

// Mean-shift filtering (spatial radius sp, colour radius sr). Large radii run on a reduced copy
// (like OpenCV's pyramid levels) and the result is refined once at full resolution.
static void meanShiftCore(Image& im, int sp, int sr, int maxIter) {
    Image src = im;
    int sr2 = sr * sr;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double cx = x, cy = y;
            uint32_t c = src.row(y)[x];
            double cr = R(c), cg = G(c), cb = B(c);
            for (int it = 0; it < maxIter; ++it) {
                int x0 = std::max(0, (int)cx - sp), x1 = std::min(im.w - 1, (int)cx + sp);
                int y0 = std::max(0, (int)cy - sp), y1 = std::min(im.h - 1, (int)cy + sp);
                double sx = 0, sy = 0, s1 = 0, s2 = 0, s3 = 0;
                int n = 0;
                for (int yy = y0; yy <= y1; ++yy) {
                    const uint32_t* row = src.row(yy);
                    for (int xx = x0; xx <= x1; ++xx) {
                        uint32_t p = row[xx];
                        double dr = R(p) - cr, dg = G(p) - cg, db = B(p) - cb;
                        if (dr * dr + dg * dg + db * db > sr2) continue;
                        sx += xx; sy += yy; s1 += R(p); s2 += G(p); s3 += B(p); ++n;
                    }
                }
                if (!n) break;
                double nx = sx / n, ny = sy / n, nr = s1 / n, ng = s2 / n, nb = s3 / n;
                double shift = std::abs(nx - cx) + std::abs(ny - cy) + std::abs(nr - cr) + std::abs(ng - cg) + std::abs(nb - cb);
                cx = nx; cy = ny; cr = nr; cg = ng; cb = nb;
                if (shift < 1) break;
            }
            im.row(y)[x] = rgb((int)std::lround(cr), (int)std::lround(cg), (int)std::lround(cb));
        }
}

void meanShiftFilter(Image& im, int sp, int sr, int maxIter) {
    int k = std::max(1, (int)std::ceil(sp / 6.0));
    if (k == 1 || im.w / k < 8 || im.h / k < 8) { meanShiftCore(im, std::max(1, sp), sr, maxIter); return; }
    Image small = resized(im, std::max(8, im.w / k), std::max(8, im.h / k));
    meanShiftCore(small, std::max(1, sp / k), sr, maxIter);
    Image up = resized(small, im.w, im.h);
    // refine: keep the filtered colour where it is close to the original, otherwise one local pass
    Image orig = im;
    for (size_t i = 0; i < im.px.size(); ++i) {
        uint32_t o = orig.px[i], u = up.px[i];
        int d = std::abs(R(o) - R(u)) + std::abs(G(o) - G(u)) + std::abs(B(o) - B(u));
        im.px[i] = d <= sr * 2 ? u : o;
    }
    meanShiftCore(im, 2, sr, 2);
}

// Onion-peel inpainting: fill masked pixels from the outside in with a distance-weighted average
// of known pixels within `radius` (the role of cv::inpaint(radius 3)).
void inpaint(Image& im, const std::vector<uint8_t>& maskIn, int radius) {
    std::vector<uint8_t> mask = maskIn;
    for (int guard = 0; guard < 10000; ++guard) {
        std::vector<std::pair<int, uint32_t>> layer;
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                int i = y * im.w + x;
                if (!mask[i]) continue;
                bool edge = (x > 0 && !mask[i - 1]) || (x + 1 < im.w && !mask[i + 1]) || (y > 0 && !mask[i - im.w]) ||
                            (y + 1 < im.h && !mask[i + im.w]);
                if (!edge) continue;
                double w = 0, r = 0, g = 0, b = 0;
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= im.w || yy >= im.h || mask[yy * im.w + xx]) continue;
                        double d2 = dx * dx + dy * dy;
                        if (d2 > radius * radius) continue;
                        double k = 1.0 / (d2 + 0.5);
                        uint32_t c = im.row(yy)[xx];
                        r += R(c) * k; g += G(c) * k; b += B(c) * k; w += k;
                    }
                if (w > 0) layer.push_back({i, rgb((int)std::lround(r / w), (int)std::lround(g / w), (int)std::lround(b / w))});
            }
        if (layer.empty()) break;
        for (auto& [i, c] : layer) { im.px[i] = c; mask[i] = 0; }
    }
}

// ------------------------------------------------------------------ list + dispatcher
const std::vector<EffectDef>& effectList() {
    static const std::vector<EffectDef> L = {
        {L"밝기/대비", {{L"밝기", -100, 100, 0}, {L"대비", -100, 100, 0}}},
        {L"감마", {{L"감마", 1, 100, 25}}},
        {L"흑백", {}},
        {L"세피아", {}},
        {L"채도", {{L"채도", -100, 100, 30}}},
        {L"색 온도", {{L"K", 2000, 12000, 5000}}},
        {L"단색(Monotone)", {{L"색조", 0, 359, 30}}},
        {L"화이트밸런스", {{L"기준 흰색 사용", 0, 1, 0, true}}, EN_RECT_OPTIONAL | EN_COLOR, L"기준 흰색을 켜면 미리보기에서 흰색이어야 할 곳을 눌러요"},
        {L"컬러 커브", {{L"어두운 부분", -100, 100, 20}, {L"밝은 부분", -100, 100, -10}, {L"대비", -100, 100, 20}}},
        {L"비네팅", {{L"강도", 0, 100, 50}}},
        {L"선명하게", {{L"강도", 0, 300, 64}}},
        {L"흐리게", {{L"강도", 1, 30, 5}}},
        {L"블룸", {{L"빛남", 0, 100, 50}, {L"부드럽게", 0, 100, 40}, {L"대비", -50, 50, 0}}},
        {L"미니어처", {{L"흐림", 0, 100, 50}, {L"선명 영역%", 0, 90, 30}}},
        {L"아웃포커스", {{L"흐림", 1, 100, 50}, {L"선명 반경%", 5, 90, 30}}, EN_POINT, L"미리보기에서 선명하게 남길 곳을 눌러요"},
        {L"영역 흐림", {{L"흐림", 1, 100, 50}, {L"반경%", 5, 90, 25}}, EN_POINT, L"미리보기에서 흐리게 할 곳을 눌러요"},
        {L"사각 흐림", {{L"흐림", 1, 100, 50}, {L"타원", 0, 1, 0, true}}, EN_RECT, L"미리보기에서 흐리게 할 영역을 드래그해요"},
        {L"모자이크", {{L"크기", 2, 64, 12}, {L"타원", 0, 1, 0, true}}, EN_RECT_OPTIONAL, L"영역을 드래그하면 그 부분만 가려요"},
        {L"스캔라인", {{L"강도", 0, 100, 30}}},
        {L"프린트", {{L"망점 크기", 3, 20, 6}}},
        {L"ASCII", {{L"글자 크기", 4, 24, 8}}},
        {L"크로마키", {{L"허용치", 1, 100, 25}, {L"붙어 있는 곳만", 0, 1, 0, true}, {L"흰색으로 채우기", 0, 1, 0, true}}, EN_COLOR | EN_POINT,
         L"미리보기에서 지울 색을 눌러요"},
        {L"노이즈 제거", {{L"레벨", 1, 7, 2}}},
        {L"카툰", {{L"강도", 1, 15, 5}}},
        {L"지우개 (인페인트)", {}, EN_RECT, L"미리보기에서 지울 부분을 드래그하면 주변 색으로 메워요"},
        {L"포스터화", {{L"단계", 2, 32, 6}}},
        {L"반전", {}},
    };
    return L;
}

ImagePtr applyEffect(int idx, const Image& src, const std::vector<int>& pin, const EffectCtx& ctx) {
    const auto& def = effectList()[idx];
    std::vector<int> p;
    for (size_t i = 0; i < def.params.size(); ++i) p.push_back(i < pin.size() ? pin[i] : def.params[i].def);
    int rx = std::clamp(ctx.rx, 0, std::max(0, src.w - 1)), ry = std::clamp(ctx.ry, 0, std::max(0, src.h - 1));
    int rw = std::min(ctx.rw, src.w - rx), rh = std::min(ctx.rh, src.h - ry);
    bool hasRect = rw > 1 && rh > 1;
    auto out = std::make_shared<Image>(src);
    Image& im = *out;
    int px = ctx.px, py = ctx.py;
    if (px >= src.w || py >= src.h) px = py = -1;
    // effects that work on a rectangle of the whole image
    switch (idx) {
    case 16:   // 사각 흐림
        if (hasRect) rectBlur(im, p[0], rx, ry, rx + rw, ry + rh, p[1] != 0);
        return out;
    case 17:   // 모자이크 (whole image when no rectangle)
        if (hasRect) mosaicRect(im, p[0], rx, ry, rx + rw, ry + rh, p[1] != 0);
        else mosaicRect(im, p[0], 0, 0, im.w, im.h, false);
        return out;
    case 24: {  // 지우개 (인페인트)
        if (!hasRect) return out;
        std::vector<uint8_t> m(im.px.size(), 0);
        for (int y = ry; y < ry + rh; ++y) std::fill(m.begin() + (size_t)y * im.w + rx, m.begin() + (size_t)y * im.w + rx + rw, 1);
        inpaint(im, m, 3);
        return out;
    }
    case 14: focusBlur(im, p[0], p[1], px, py, false); return out;
    case 15: focusBlur(im, p[0], p[1], px, py, true); return out;
    case 21: chromakey(im, p[0], p[1] != 0, px, py, ctx.color, p[2] != 0); return out;
    }
    // the rest: optional "apply only inside the rectangle"
    bool region = hasRect && (def.needs & EN_RECT_OPTIONAL);
    Image part;
    if (region) {
        part = Image(rw, rh);
        for (int y = 0; y < rh; ++y) std::copy_n(src.row(ry + y) + rx, rw, part.row(y));
    }
    Image& t = region ? part : im;
    switch (idx) {
    case 0: brightnessContrast(t, p[0], p[1]); break;
    case 1: gammaFx(t, p[0]); break;
    case 2: gray(t); break;
    case 3: sepia(t); break;
    case 4: saturation(t, p[0]); break;
    case 5: temperature(t, p[0]); break;
    case 6: monotone(t, p[0]); break;
    case 7: autowhite(t, p[0] != 0, ctx.color); break;
    case 8: colorCurve(t, p[0], p[1], p[2]); break;
    case 9: vignette(t, p[0]); break;
    case 10: sharpen(t, p[0]); break;
    case 11: boxBlur(t, std::max(1, p[0] / 2)); break;
    case 12: bloom(t, p[0], p[1], p[2]); break;
    case 13: tiltshift(t, p[0], p[1]); break;
    case 18: scanline(t, p[0]); break;
    case 19: printFx(t, p[0]); break;
    case 20: asciiFx(t, p[0]); break;
    case 22: { int l = std::clamp(p[0], 1, 7); meanShiftFilter(t, 15 * l / 3, 10 * l, 5); break; }
    case 23: {   // ½ size -> Lab mean-shift (hs = s, hr = 2s-1) -> back to RGB -> upscale
        int s = std::clamp(p[0], 1, 15);
        Image small = resized(t, std::max(8, t.w / 2), std::max(8, t.h / 2));
        for (auto& c : small.px) { int L, a, b; rgb2lab(c, L, a, b); c = rgb(L, a, b); }
        meanShiftCore(small, s, 2 * s - 1, 5);
        for (auto& c : small.px) c = lab2rgb(R(c), G(c), B(c));
        t = resized(small, t.w, t.h);
        break;
    }
    case 25: posterize(t, p[0]); break;
    case 26: for (auto& c : t.px) c = 0xFF000000u | (~c & 0xFFFFFF); break;
    }
    if (region)
        for (int y = 0; y < rh; ++y) std::copy_n(part.row(y), rw, im.row(ry + y) + rx);
    return out;
}

ImagePtr overlay(const Image& src, const Image& ov, int x, int y, double op) {
    auto o = std::make_shared<Image>(src);
    for (int j = 0; j < ov.h; ++j) {
        int ty = y + j;
        if (ty < 0 || ty >= src.h) continue;
        for (int i = 0; i < ov.w; ++i) {
            int tx = x + i;
            if (tx < 0 || tx >= src.w) continue;
            uint32_t s = ov.row(j)[i];
            int a = (int)(((s >> 24) & 255) * op);
            if (a <= 0) continue;
            uint32_t& d = o->row(ty)[tx];
            d = rgb((R(s) * a + R(d) * (255 - a)) / 255, (G(s) * a + G(d) * (255 - a)) / 255, (B(s) * a + B(d) * (255 - a)) / 255);
        }
    }
    return o;
}

} // namespace bg
