#include "core/Quantize.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace bg {

namespace {

constexpr int N = 33;
inline int ind(int r, int g, int b) { return (r * N + g) * N + b; }

struct Box { int r0, r1, g0, g1, b0, b1, vol; };

struct Wu {
    std::vector<int64_t> wt, mr, mg, mb;
    std::vector<double> m2;
    Wu() : wt(N * N * N), mr(N * N * N), mg(N * N * N), mb(N * N * N), m2(N * N * N) {}

    void moments() {
        for (int r = 1; r < N; ++r) {
            int64_t aw[N] = {}, ar[N] = {}, ag[N] = {}, ab[N] = {};
            double a2[N] = {};
            for (int g = 1; g < N; ++g) {
                int64_t lw = 0, lr = 0, lg = 0, lb = 0;
                double l2 = 0;
                for (int b = 1; b < N; ++b) {
                    int i1 = ind(r, g, b), i2 = i1 - N * N;
                    lw += wt[i1]; lr += mr[i1]; lg += mg[i1]; lb += mb[i1]; l2 += m2[i1];
                    aw[b] += lw; ar[b] += lr; ag[b] += lg; ab[b] += lb; a2[b] += l2;
                    wt[i1] = wt[i2] + aw[b]; mr[i1] = mr[i2] + ar[b]; mg[i1] = mg[i2] + ag[b];
                    mb[i1] = mb[i2] + ab[b]; m2[i1] = m2[i2] + a2[b];
                }
            }
        }
    }
    template <class T>
    static T vol(const Box& c, const std::vector<T>& m) {
        return m[ind(c.r1, c.g1, c.b1)] - m[ind(c.r1, c.g1, c.b0)] - m[ind(c.r1, c.g0, c.b1)] + m[ind(c.r1, c.g0, c.b0)] -
               m[ind(c.r0, c.g1, c.b1)] + m[ind(c.r0, c.g1, c.b0)] + m[ind(c.r0, c.g0, c.b1)] - m[ind(c.r0, c.g0, c.b0)];
    }
    // dir 0=r 1=g 2=b
    static int64_t bottom(const Box& c, int dir, const std::vector<int64_t>& m) {
        switch (dir) {
        case 0: return -m[ind(c.r0, c.g1, c.b1)] + m[ind(c.r0, c.g1, c.b0)] + m[ind(c.r0, c.g0, c.b1)] - m[ind(c.r0, c.g0, c.b0)];
        case 1: return -m[ind(c.r1, c.g0, c.b1)] + m[ind(c.r1, c.g0, c.b0)] + m[ind(c.r0, c.g0, c.b1)] - m[ind(c.r0, c.g0, c.b0)];
        default: return -m[ind(c.r1, c.g1, c.b0)] + m[ind(c.r1, c.g0, c.b0)] + m[ind(c.r0, c.g1, c.b0)] - m[ind(c.r0, c.g0, c.b0)];
        }
    }
    static int64_t top(const Box& c, int dir, int pos, const std::vector<int64_t>& m) {
        switch (dir) {
        case 0: return m[ind(pos, c.g1, c.b1)] - m[ind(pos, c.g1, c.b0)] - m[ind(pos, c.g0, c.b1)] + m[ind(pos, c.g0, c.b0)];
        case 1: return m[ind(c.r1, pos, c.b1)] - m[ind(c.r1, pos, c.b0)] - m[ind(c.r0, pos, c.b1)] + m[ind(c.r0, pos, c.b0)];
        default: return m[ind(c.r1, c.g1, pos)] - m[ind(c.r1, c.g0, pos)] - m[ind(c.r0, c.g1, pos)] + m[ind(c.r0, c.g0, pos)];
        }
    }
    double var(const Box& c) const {
        double dr = (double)vol(c, mr), dg = (double)vol(c, mg), db = (double)vol(c, mb), w = (double)vol(c, wt);
        if (w <= 0) return 0;
        return vol(c, m2) - (dr * dr + dg * dg + db * db) / w;
    }
    double maximize(const Box& c, int dir, int first, int last, int& cut, int64_t wr, int64_t wg, int64_t wb, int64_t ww) const {
        int64_t br = bottom(c, dir, mr), bgg = bottom(c, dir, mg), bb = bottom(c, dir, mb), bw = bottom(c, dir, wt);
        double best = 0;
        cut = -1;
        for (int i = first; i < last; ++i) {
            double hr = (double)(br + top(c, dir, i, mr)), hg = (double)(bgg + top(c, dir, i, mg));
            double hb = (double)(bb + top(c, dir, i, mb)), hw = (double)(bw + top(c, dir, i, wt));
            if (hw == 0) continue;
            double t = (hr * hr + hg * hg + hb * hb) / hw;
            hr = wr - hr; hg = wg - hg; hb = wb - hb; hw = ww - hw;
            if (hw == 0) continue;
            t += (hr * hr + hg * hg + hb * hb) / hw;
            if (t > best) { best = t; cut = i; }
        }
        return best;
    }
    bool cut(Box& a, Box& b) const {
        int64_t wr = vol(a, mr), wg = vol(a, mg), wb = vol(a, mb), ww = vol(a, wt);
        int cr, cg, cb;
        double maxr = maximize(a, 0, a.r0 + 1, a.r1, cr, wr, wg, wb, ww);
        double maxg = maximize(a, 1, a.g0 + 1, a.g1, cg, wr, wg, wb, ww);
        double maxb = maximize(a, 2, a.b0 + 1, a.b1, cb, wr, wg, wb, ww);
        int dir;
        if (maxr >= maxg && maxr >= maxb) { dir = 0; if (cr < 0) return false; }
        else if (maxg >= maxr && maxg >= maxb) dir = 1;
        else dir = 2;
        b.r1 = a.r1; b.g1 = a.g1; b.b1 = a.b1;
        if (dir == 0) { b.r0 = a.r1 = cr; b.g0 = a.g0; b.b0 = a.b0; }
        else if (dir == 1) { if (cg < 0) return false; b.g0 = a.g1 = cg; b.r0 = a.r0; b.b0 = a.b0; }
        else { if (cb < 0) return false; b.b0 = a.b1 = cb; b.r0 = a.r0; b.g0 = a.g0; }
        a.vol = (a.r1 - a.r0) * (a.g1 - a.g0) * (a.b1 - a.b0);
        b.vol = (b.r1 - b.r0) * (b.g1 - b.g0) * (b.b1 - b.b0);
        return true;
    }
};

} // namespace

Palette wuQuantize(const uint32_t* px, int w, int h, const uint8_t* mask, int maxColors, bool center) {
    Wu q;
    int cx0 = w / 4, cx1 = w * 3 / 4, cy0 = h / 4, cy1 = h * 3 / 4;
    for (int y = 0; y < h; ++y) {
        bool cy = center && y >= cy0 && y < cy1;
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            if (mask && mask[i]) continue;
            uint32_t c = px[i];
            int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            int k = ind((r >> 3) + 1, (g >> 3) + 1, (b >> 3) + 1);
            int wgt = (cy && x >= cx0 && x < cx1) ? 5 : 1;
            q.wt[k] += wgt; q.mr[k] += r * wgt; q.mg[k] += g * wgt; q.mb[k] += b * wgt;
            q.m2[k] += (double)(r * r + g * g + b * b) * wgt;
        }
    }
    q.moments();
    std::vector<Box> cube(std::max(1, maxColors));
    std::vector<double> vv(cube.size(), 0);
    cube[0] = {0, 32, 0, 32, 0, 32, 0};
    int n = 1, next = 0;
    for (int i = 1; i < maxColors; ++i) {
        if (q.cut(cube[next], cube[i])) {
            vv[next] = cube[next].vol > 1 ? q.var(cube[next]) : 0;
            vv[i] = cube[i].vol > 1 ? q.var(cube[i]) : 0;
            ++n;
        } else {
            vv[next] = 0;
            --i;
        }
        next = 0;
        double t = vv[0];
        for (int k = 1; k <= i; ++k) if (vv[k] > t) { t = vv[k]; next = k; }
        if (t <= 0) break;
    }
    Palette p;
    for (int k = 0; k < n; ++k) {
        int64_t ww = Wu::vol(cube[k], q.wt);
        if (ww <= 0) continue;
        int r = (int)((Wu::vol(cube[k], q.mr) + ww / 2) / ww), g = (int)((Wu::vol(cube[k], q.mg) + ww / 2) / ww),
            b = (int)((Wu::vol(cube[k], q.mb) + ww / 2) / ww);
        p.colors.push_back(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
    if (p.colors.empty()) p.colors.push_back(0);
    return p;
}

bool exactPalette(const uint32_t* px, size_t n, const uint8_t* mask, int limit, Palette& out) {
    std::vector<uint32_t> v;
    v.reserve(1024);
    std::unordered_map<uint32_t, int> seen;
    for (size_t i = 0; i < n; ++i) {
        if (mask && mask[i]) continue;
        uint32_t c = px[i] & 0xFFFFFF;
        if (seen.emplace(c, 0).second) {
            v.push_back(c);
            if ((int)v.size() > limit) return false;
        }
    }
    if (v.empty()) v.push_back(0);
    std::sort(v.begin(), v.end());
    out.colors = v;
    return true;
}

void remap(const uint32_t* px, int w, int h, const uint8_t* mask, const Palette& pal, bool dither, uint8_t tIndex,
           std::vector<uint8_t>& out) {
    out.assign((size_t)w * h, tIndex);
    const int np = (int)pal.colors.size();
    std::vector<int> pr(np), pg(np), pb(np);
    for (int i = 0; i < np; ++i) { pr[i] = (pal.colors[i] >> 16) & 255; pg[i] = (pal.colors[i] >> 8) & 255; pb[i] = pal.colors[i] & 255; }
    struct CacheE { uint32_t key; uint8_t idx; };
    std::vector<CacheE> cache(8192, {0xFFFFFFFFu, 0});
    auto nearest = [&](int r, int g, int b) -> uint8_t {
        uint32_t key = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        CacheE& e = cache[(key * 2654435761u) >> 19];
        if (e.key == key) return e.idx;
        int best = 0, bd = INT32_MAX;
        for (int i = 0; i < np; ++i) {
            int dr = r - pr[i], dg = g - pg[i], db = b - pb[i];
            int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bd) { bd = d; best = i; if (!d) break; }
        }
        e = {key, (uint8_t)best};
        return (uint8_t)best;
    };
    if (!dither) {
        for (size_t i = 0; i < out.size(); ++i) {
            if (mask && mask[i]) continue;
            uint32_t c = px[i];
            out[i] = nearest((c >> 16) & 255, (c >> 8) & 255, c & 255);
        }
        return;
    }
    // Floyd-Steinberg, serpentine, error in 1/16 units, errors not spread into masked pixels
    std::vector<int> e0((w + 2) * 3, 0), e1((w + 2) * 3, 0);
    for (int y = 0; y < h; ++y) {
        bool rev = y & 1;
        std::fill(e1.begin(), e1.end(), 0);
        for (int k = 0; k < w; ++k) {
            int x = rev ? w - 1 - k : k;
            size_t i = (size_t)y * w + x;
            if (mask && mask[i]) continue;
            uint32_t c = px[i];
            int* ec = &e0[(x + 1) * 3];
            int r = std::clamp(((int)((c >> 16) & 255)) + ec[0] / 16, 0, 255);
            int g = std::clamp(((int)((c >> 8) & 255)) + ec[1] / 16, 0, 255);
            int b = std::clamp(((int)(c & 255)) + ec[2] / 16, 0, 255);
            uint8_t q = nearest(r, g, b);
            out[i] = q;
            int er = r - pr[q], eg = g - pg[q], eb = b - pb[q];
            // damp error a little (reduces noise / file size, like LIQ's dithering)
            er = er * 7 / 8; eg = eg * 7 / 8; eb = eb * 7 / 8;
            int dx = rev ? -1 : 1;
            int* nx = &e0[(x + 1 + dx) * 3];
            nx[0] += er * 7; nx[1] += eg * 7; nx[2] += eb * 7;
            int* bl = &e1[(x + 1 - dx) * 3];
            bl[0] += er * 3; bl[1] += eg * 3; bl[2] += eb * 3;
            int* bm = &e1[(x + 1) * 3];
            bm[0] += er * 5; bm[1] += eg * 5; bm[2] += eb * 5;
            int* br = &e1[(x + 1 + dx) * 3];
            br[0] += er; br[1] += eg; br[2] += eb;
        }
        std::swap(e0, e1);
    }
}

} // namespace bg
