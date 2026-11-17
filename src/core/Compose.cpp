#include "core/Compose.h"

#include "core/Edit.h"
#include "core/Effects.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bg {

Frames collage(const std::vector<Frames>& anims, int layout, int cellW, int cellH, int gap, uint32_t bg) {
    if (anims.empty()) return {};
    int n = (int)anims.size();
    int cols = layout == CL_ROW ? n : layout == CL_COLUMN ? 1 : (int)std::ceil(std::sqrt((double)n));
    int rows = (n + cols - 1) / cols;
    int W = cols * cellW + (cols + 1) * gap, H = rows * cellH + (rows + 1) * gap;
    // fitted copies of every frame
    std::vector<Frames> fit(n);
    std::vector<int64_t> total(n, 0);
    int64_t longest = 0;
    for (int i = 0; i < n; ++i) {
        for (auto& f : anims[i]) {
            const Image& s = *f.img;
            double sc = std::min((double)cellW / s.w, (double)cellH / s.h);
            int w = std::max(1, (int)(s.w * sc)), h = std::max(1, (int)(s.h * sc));
            fit[i].push_back({resizeImage(s, w, h), f.dur});
            total[i] += f.dur;
        }
        longest = std::max(longest, total[i]);
    }
    // change points of the merged timeline (shorter animations loop)
    std::set<int64_t> cuts{0};
    for (int i = 0; i < n; ++i) {
        if (total[i] <= 0) continue;
        for (int64_t base = 0; base < longest; base += total[i]) {
            int64_t t = base;
            for (auto& f : fit[i]) {
                if (t < longest) cuts.insert(t);
                t += f.dur;
            }
        }
    }
    std::vector<int64_t> pts(cuts.begin(), cuts.end());
    Frames out;
    for (size_t k = 0; k < pts.size(); ++k) {
        int64_t t = pts[k], next = k + 1 < pts.size() ? pts[k + 1] : longest;
        auto im = std::make_shared<Image>(W, H, bg);
        for (int i = 0; i < n; ++i) {
            if (fit[i].empty()) continue;
            int64_t lt = total[i] > 0 ? t % total[i] : 0, acc = 0;
            size_t fi = 0;
            while (fi + 1 < fit[i].size() && acc + fit[i][fi].dur <= lt) acc += fit[i][fi++].dur;
            const Image& c = *fit[i][fi].img;
            int col = i % cols, row = i / cols;
            int ox = gap + col * (cellW + gap) + (cellW - c.w) / 2, oy = gap + row * (cellH + gap) + (cellH - c.h) / 2;
            for (int y = 0; y < c.h; ++y) std::copy_n(c.row(y), c.w, im->row(oy + y) + ox);
        }
        out.push_back({im, std::max<int64_t>(1, next - t)});
    }
    return out;
}

Frames zoomAnimation(const Frames& in, int start, int rx, int ry, int rw, int rh, int inFrames, int holdFrames, int outFrames, int64_t frameUs) {
    if (in.empty()) return in;
    Frames f = in;
    const int W = f[0].img->w, H = f[0].img->h;
    start = std::clamp(start, 0, (int)f.size() - 1);
    int total = inFrames + holdFrames + outFrames;
    while ((int)f.size() < start + total) f.push_back({f.back().img, frameUs});
    // keep the target's aspect equal to the frame's so the zoom does not stretch
    double ta = (double)W / H;
    double cx = rx + rw / 2.0, cy = ry + rh / 2.0;
    double tw = std::max(8, rw), th = std::max(8, rh);
    if (tw / th > ta) th = tw / ta; else tw = th * ta;
    tw = std::min<double>(tw, W);
    th = std::min<double>(th, H);
    auto ease = [](double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; };
    for (int k = 0; k < total; ++k) {
        double t;
        if (k < inFrames) t = ease((k + 1.0) / inFrames);
        else if (k < inFrames + holdFrames) t = 1;
        else t = 1 - ease((k - inFrames - holdFrames + 1.0) / std::max(1, outFrames));
        double w = W + (tw - W) * t, h = H + (th - H) * t;
        double x = (W / 2.0) + (cx - W / 2.0) * t - w / 2, y = (H / 2.0) + (cy - H / 2.0) * t - h / 2;
        x = std::clamp(x, 0.0, W - w);
        y = std::clamp(y, 0.0, H - h);
        const Image& s = *f[start + k].img;
        int ix = (int)x, iy = (int)y, iw = std::max(1, (int)std::lround(w)), ih = std::max(1, (int)std::lround(h));
        iw = std::min(iw, W - ix);
        ih = std::min(ih, H - iy);
        Image cropped(iw, ih);
        for (int yy = 0; yy < ih; ++yy) std::copy_n(s.row(iy + yy) + ix, iw, cropped.row(yy));
        f[start + k].img = (iw == W && ih == H) ? f[start + k].img : resizeImage(cropped, W, H);
    }
    return f;
}

ImagePtr photoFrame(const Image& s, int style, int t, uint32_t color) {
    t = std::max(1, t);
    switch (style) {
    case FS_ROUNDED:
    case FS_SOFT: {
        auto o = std::make_shared<Image>(s);
        double r = t * 2.0;
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                uint32_t& p = o->row(y)[x];
                if (style == FS_ROUNDED) {
                    double dx = std::max({0.0, r - x, x - (s.w - 1 - r)}), dy = std::max({0.0, r - y, y - (s.h - 1 - r)});
                    double d = std::hypot(dx, dy) - r;   // > 0 outside the rounded rect
                    double a = std::clamp(d + 0.5, 0.0, 1.0);
                    if (a > 0) p = rgb((int)(R(p) * (1 - a) + R(color) * a), (int)(G(p) * (1 - a) + G(color) * a), (int)(B(p) * (1 - a) + B(color) * a));
                } else {
                    double e = std::min({x, y, s.w - 1 - x, s.h - 1 - y}) / (double)t;
                    double a = std::clamp(1 - e, 0.0, 1.0);
                    a = a * a;
                    if (a > 0) p = rgb((int)(R(p) * (1 - a) + R(color) * a), (int)(G(p) * (1 - a) + G(color) * a), (int)(B(p) * (1 - a) + B(color) * a));
                }
            }
        return o;
    }
    case FS_POLAROID: {
        int side = t, bottom = t * 4;
        auto o = std::make_shared<Image>(s.w + side * 2, s.h + side + bottom, color);
        for (int y = 0; y < s.h; ++y) std::copy_n(s.row(y), s.w, o->row(side + y) + side);
        return o;
    }
    case FS_FILM: {
        int band = std::max(12, t * 2);
        auto o = std::make_shared<Image>(s.w, s.h + band * 2, rgb(16, 16, 18));
        for (int y = 0; y < s.h; ++y) std::copy_n(s.row(y), s.w, o->row(band + y));
        int hw = band / 2, hh = band / 2, step = band;
        for (int bx = step / 2; bx + hw < s.w; bx += step)
            for (int by : {band / 4, s.h + band + band / 4})
                for (int y = by; y < by + hh; ++y)
                    for (int x = bx; x < bx + hw; ++x) o->row(y)[x] = rgb(235, 232, 225);
        return o;
    }
    case FS_SHADOW: {
        int m = t * 2;
        auto o = std::make_shared<Image>(s.w + m * 2, s.h + m * 2, color);
        Image sh(o->w, o->h, color);   // soft shadow under the picture
        for (int y = m + t / 2; y < m + s.h + t / 2 && y < sh.h; ++y)
            for (int x = m + t / 3; x < m + s.w + t / 3 && x < sh.w; ++x)
                sh.row(y)[x] = rgb(R(color) * 55 / 100, G(color) * 55 / 100, B(color) * 55 / 100);
        boxBlur(sh, std::max(2, t / 2));
        *o = sh;
        for (int y = 0; y < s.h; ++y) std::copy_n(s.row(y), s.w, o->row(m + y) + m);
        return o;
    }
    default: {   // solid border
        auto o = std::make_shared<Image>(s.w + t * 2, s.h + t * 2, color);
        for (int y = 0; y < s.h; ++y) std::copy_n(s.row(y), s.w, o->row(t + y) + t);
        return o;
    }
    }
}

} // namespace bg
