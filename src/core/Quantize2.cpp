// GIF quantisation methods for the quantiser selector:
// LIQ (libimagequant 2.x, speed 4, max_colors, dithering 0/1, fixed colours), NeuQuant (Dekker 1994,
// as in pngnq), Octree with near-colour merge, and the variance box split (Wu) in Quantize.cpp.
#include "core/Quantize.h"

#include <libimagequant.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

namespace bg {

// ------------------------------------------------------------------ NeuQuant
namespace {
struct NeuQuantNet {
    static constexpr int ncycles = 100, netbiasshift = 4, intbiasshift = 16, intbias = 1 << intbiasshift, gammashift = 10,
                         betashift = 10, beta = intbias >> betashift, betagamma = intbias << (gammashift - betashift),
                         radiusbiasshift = 6, radiusbias = 1 << radiusbiasshift, radiusdec = 30, alphabiasshift = 10,
                         initalpha = 1 << alphabiasshift, radbiasshift = 8, radbias = 1 << radbiasshift,
                         alpharadbshift = alphabiasshift + radbiasshift, alpharadbias = 1 << alpharadbshift;
    int n;
    std::vector<std::array<int, 3>> net;   // b, g, r (biased)
    std::vector<int> bias, freq, radpower;
    explicit NeuQuantNet(int size) : n(size), net(size), bias(size, 0), freq(size, intbias / size), radpower(std::max(1, size >> 3)) {
        for (int i = 0; i < n; ++i) {
            int v = (i << (netbiasshift + 8)) / n;
            net[i] = {v, v, v};
        }
    }
    int contest(int b, int g, int r) {
        int bestd = INT32_MAX, bestbiasd = INT32_MAX, bestpos = 0, bestbiaspos = 0;
        for (int i = 0; i < n; ++i) {
            int dist = std::abs(net[i][0] - b) + std::abs(net[i][1] - g) + std::abs(net[i][2] - r);
            if (dist < bestd) { bestd = dist; bestpos = i; }
            int biasdist = dist - (bias[i] >> (intbiasshift - netbiasshift));
            if (biasdist < bestbiasd) { bestbiasd = biasdist; bestbiaspos = i; }
            int betafreq = freq[i] >> betashift;
            freq[i] -= betafreq;
            bias[i] += betafreq << gammashift;
        }
        freq[bestpos] += beta;
        bias[bestpos] -= betagamma;
        return bestbiaspos;
    }
    void alterSingle(int alpha, int i, int b, int g, int r) {
        auto& p = net[i];
        p[0] -= alpha * (p[0] - b) / initalpha;
        p[1] -= alpha * (p[1] - g) / initalpha;
        p[2] -= alpha * (p[2] - r) / initalpha;
    }
    void alterNeigh(int rad, int i, int b, int g, int r) {
        int lo = std::max(i - rad, -1), hi = std::min(i + rad, n);
        int j = i + 1, k = i - 1, m = 1;
        while (j < hi || k > lo) {
            int a = radpower[std::min(m++, (int)radpower.size() - 1)];
            if (j < hi) {
                auto& p = net[j++];
                p[0] -= (int)((int64_t)a * (p[0] - b) / alpharadbias);
                p[1] -= (int)((int64_t)a * (p[1] - g) / alpharadbias);
                p[2] -= (int)((int64_t)a * (p[2] - r) / alpharadbias);
            }
            if (k > lo) {
                auto& p = net[k--];
                p[0] -= (int)((int64_t)a * (p[0] - b) / alpharadbias);
                p[1] -= (int)((int64_t)a * (p[1] - g) / alpharadbias);
                p[2] -= (int)((int64_t)a * (p[2] - r) / alpharadbias);
            }
        }
    }
    void learn(const std::vector<uint32_t>& pix, int samplefac) {
        size_t len = pix.size();
        if (!len) return;
        int alphadec = 30 + ((samplefac - 1) / 3);
        size_t samplepixels = std::max<size_t>(1, len / samplefac);
        size_t delta = std::max<size_t>(1, samplepixels / ncycles);
        int alpha = initalpha;
        int radius = (n >> 3) * radiusbias;
        int rad = radius >> radiusbiasshift;
        if (rad <= 1) rad = 0;
        auto setRad = [&] {
            for (int i = 0; i < rad && i < (int)radpower.size(); ++i)
                radpower[i] = alpha * (((rad * rad - i * i) * radbias) / (rad * rad));
        };
        setRad();
        size_t step;
        if (len < 1509) step = 1;
        else if (len % 499) step = 499;
        else if (len % 491) step = 491;
        else if (len % 487) step = 487;
        else step = 503;
        size_t pos = 0;
        for (size_t i = 0; i < samplepixels;) {
            uint32_t c = pix[pos];
            int b = (c & 255) << netbiasshift, g = ((c >> 8) & 255) << netbiasshift, r = ((c >> 16) & 255) << netbiasshift;
            int j = contest(b, g, r);
            alterSingle(alpha, j, b, g, r);
            if (rad) alterNeigh(rad, j, b, g, r);
            pos = (pos + step) % len;
            ++i;
            if (i % delta == 0) {
                alpha -= alpha / alphadec;
                radius -= radius / radiusdec;
                rad = radius >> radiusbiasshift;
                if (rad <= 1) rad = 0;
                setRad();
            }
        }
    }
};
} // namespace

Palette neuQuant(const uint32_t* px, int w, int h, const uint8_t* mask, int colors, int samplefac) {
    std::vector<uint32_t> pix;
    pix.reserve((size_t)w * h);
    for (size_t i = 0; i < (size_t)w * h; ++i)
        if (!mask || !mask[i]) pix.push_back(px[i] & 0xFFFFFF);
    Palette p;
    if (pix.empty()) { p.colors.push_back(0); return p; }
    int n = std::clamp(colors, 2, 256);
    NeuQuantNet net(n);
    net.learn(pix, std::clamp(samplefac, 1, 30));
    for (auto& v : net.net) {
        int b = std::clamp(v[0] >> NeuQuantNet::netbiasshift, 0, 255), g = std::clamp(v[1] >> NeuQuantNet::netbiasshift, 0, 255),
            r = std::clamp(v[2] >> NeuQuantNet::netbiasshift, 0, 255);
        p.colors.push_back(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
    std::sort(p.colors.begin(), p.colors.end());
    p.colors.erase(std::unique(p.colors.begin(), p.colors.end()), p.colors.end());
    return p;
}

// ------------------------------------------------------------------ Octree
namespace {
struct ONode {
    int64_t r = 0, g = 0, b = 0;
    int64_t count = 0;
    int child[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int level = 0;
    bool leaf = false;
    bool reducedAway = false;
};
} // namespace

Palette octreeQuantize(const uint32_t* px, int w, int h, const uint8_t* mask, int colors, int mergeTol) {
    std::vector<ONode> nodes;
    nodes.reserve(1 << 16);
    nodes.emplace_back();
    int leaves = 0;
    std::vector<std::vector<int>> levelNodes(8);
    const int maxDepth = 8;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        if (mask && mask[i]) continue;
        uint32_t c = px[i];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        int cur = 0;
        for (int lv = 0; lv < maxDepth; ++lv) {
            if (nodes[cur].leaf) break;
            int sh = 7 - lv;
            int idx = (((r >> sh) & 1) << 2) | (((g >> sh) & 1) << 1) | ((b >> sh) & 1);   // (R<<2)|(G<<1)|B
            int nx = nodes[cur].child[idx];
            if (nx < 0) {
                nx = (int)nodes.size();
                nodes[cur].child[idx] = nx;
                nodes.emplace_back();
                nodes[nx].level = lv + 1;
                if (lv + 1 == maxDepth) { nodes[nx].leaf = true; ++leaves; }
                else levelNodes[lv + 1].push_back(nx);
            }
            cur = nx;
        }
        ONode& L = nodes[cur];
        L.r += r; L.g += g; L.b += b; L.count++;
    }
    // subtree counts for reducible ordering
    std::function<int64_t(int)> total = [&](int id) -> int64_t {
        ONode& n = nodes[id];
        if (n.leaf) return n.count;
        int64_t s = 0;
        for (int c : n.child) if (c >= 0) s += total(c);
        n.count = s;
        return s;
    };
    total(0);
    // reduce deepest level first, smallest count first (insertion-sorted by (level, count))
    for (int lv = maxDepth - 1; lv >= 1 && leaves > colors; --lv) {
        auto& list = levelNodes[lv];
        std::sort(list.begin(), list.end(), [&](int a, int b) { return nodes[a].count < nodes[b].count; });
        for (int id : list) {
            if (leaves <= colors) break;
            ONode& n = nodes[id];
            int kids = 0;
            int64_t r = 0, g = 0, b = 0, cnt = 0;
            for (int& c : n.child) {
                if (c < 0) continue;
                ONode& k = nodes[c];
                r += k.r; g += k.g; b += k.b; cnt += k.count;
                ++kids;
                c = -1;
            }
            if (!kids) continue;
            n.r = r; n.g = g; n.b = b; n.count = cnt;
            n.leaf = true;
            leaves -= kids - 1;
        }
    }
    struct E { int r, g, b; int64_t n; };
    std::vector<E> pal;
    std::function<void(int)> collect = [&](int id) {
        ONode& n = nodes[id];
        if (n.leaf) {
            if (n.count > 0) pal.push_back({(int)(n.r / n.count), (int)(n.g / n.count), (int)(n.b / n.count), n.count});
            return;
        }
        for (int c : n.child) if (c >= 0) collect(c);
    };
    collect(0);
    if (pal.size() > (size_t)colors) {   // root-level fallback: keep the most used
        std::sort(pal.begin(), pal.end(), [](const E& a, const E& b) { return a.n > b.n; });
        pal.resize(colors);
    }
    // merge neighbours in the sorted palette whose R/G/B differ by <= tolerance
    if (mergeTol > 0 && pal.size() > 1) {
        std::sort(pal.begin(), pal.end(), [](const E& a, const E& b) { return std::tie(a.r, a.g, a.b) < std::tie(b.r, b.g, b.b); });
        std::vector<E> m;
        for (auto& e : pal) {
            if (!m.empty() && std::abs(m.back().r - e.r) <= mergeTol && std::abs(m.back().g - e.g) <= mergeTol && std::abs(m.back().b - e.b) <= mergeTol) {
                E& p = m.back();
                int64_t t = p.n + e.n;
                p.r = (int)((p.r * p.n + e.r * e.n) / t);
                p.g = (int)((p.g * p.n + e.g * e.n) / t);
                p.b = (int)((p.b * p.n + e.b * e.n) / t);
                p.n = t;
            } else {
                m.push_back(e);
            }
        }
        pal.swap(m);
    }
    Palette p;
    for (auto& e : pal) p.colors.push_back(((uint32_t)e.r << 16) | ((uint32_t)e.g << 8) | (uint32_t)e.b);
    if (p.colors.empty()) p.colors.push_back(0);
    return p;
}

// ------------------------------------------------------------------ dispatcher
static std::vector<uint8_t> toRGBA(const uint32_t* px, int w, int h, const uint8_t* mask) {
    std::vector<uint8_t> v((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        uint32_t c = px[i];
        bool t = mask && mask[i];
        v[i * 4] = (uint8_t)(c >> 16);
        v[i * 4 + 1] = (uint8_t)(c >> 8);
        v[i * 4 + 2] = (uint8_t)c;
        v[i * 4 + 3] = t ? 0 : 255;
    }
    return v;
}

void quantizeFrame(const QuantRequest& q, Palette& pal, std::vector<uint8_t>& idx, int& tIndex) {
    bool hasT = false;
    if (q.mask)
        for (size_t i = 0; i < (size_t)q.w * q.h && !hasT; ++i) hasT = q.mask[i] != 0;
    const uint8_t* mask = hasT ? q.mask : nullptr;
    if (q.method == QM_LIQ) {
        std::vector<uint8_t> rgba = toRGBA(q.px, q.w, q.h, mask);
        liq_attr* attr = liq_attr_create();
        liq_set_max_colors(attr, std::clamp(q.maxColors + (hasT ? 1 : 0), 2, 256));
        liq_set_speed(attr, 4);
        liq_image* target = liq_image_create_rgba(attr, rgba.data(), q.w, q.h, 0);
        // centre-weighted learning image: half-size full frame | full-res centre crop
        std::vector<uint8_t> learnBuf;
        liq_image* learn = target;
        if (q.centerWeighted && q.w >= 8 && q.h >= 8) {
            int hw = q.w / 2, hh = q.h / 2;
            learnBuf.assign((size_t)q.w * hh * 4, 0);
            for (int y = 0; y < hh; ++y)
                for (int x = 0; x < hw; ++x) {
                    int sr = 0, sg = 0, sb = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const uint8_t* s = &rgba[((size_t)(y * 2 + dy) * q.w + x * 2 + dx) * 4];
                            sr += s[0]; sg += s[1]; sb += s[2];
                        }
                    uint8_t* d = &learnBuf[((size_t)y * q.w + x) * 4];
                    d[0] = (uint8_t)(sr / 4); d[1] = (uint8_t)(sg / 4); d[2] = (uint8_t)(sb / 4); d[3] = 255;
                    const uint8_t* c = &rgba[((size_t)(q.h / 4 + y) * q.w + q.w / 4 + x) * 4];
                    std::memcpy(&learnBuf[((size_t)y * q.w + hw + x) * 4], c, 4);
                }
            learn = liq_image_create_rgba(attr, learnBuf.data(), q.w, hh, 0);
        }
        for (uint32_t c : q.fixedColors) liq_image_add_fixed_color(learn, liq_color{(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c, 255});
        liq_result* res = nullptr;
        if (liq_image_quantize(learn, attr, &res) == LIQ_OK && res) {
            liq_set_dithering_level(res, q.dither ? 1.0f : 0.0f);
            idx.assign((size_t)q.w * q.h, 0);
            liq_write_remapped_image(res, target, idx.data(), idx.size());
            const liq_palette* lp = liq_get_palette(res);
            pal.colors.clear();
            tIndex = -1;
            for (unsigned i = 0; i < lp->count; ++i) {
                pal.colors.push_back(((uint32_t)lp->entries[i].r << 16) | ((uint32_t)lp->entries[i].g << 8) | lp->entries[i].b);
                if (hasT && lp->entries[i].a < 128 && tIndex < 0) tIndex = (int)i;
            }
            if (hasT && tIndex < 0) {
                tIndex = (int)pal.colors.size();
                pal.colors.push_back(0);
            }
            if (hasT)
                for (size_t i = 0; i < idx.size(); ++i) if (mask[i]) idx[i] = (uint8_t)tIndex;
            liq_result_destroy(res);
            if (learn != target) liq_image_destroy(learn);
            liq_image_destroy(target);
            liq_attr_destroy(attr);
            return;
        }
        if (res) liq_result_destroy(res);
        if (learn != target) liq_image_destroy(learn);
        liq_image_destroy(target);
        liq_attr_destroy(attr);
        // fall through to Wu if LIQ failed (e.g. tiny image)
    }
    switch (q.method) {
    case QM_NEU: {
        int samplefac = std::max(1, (100 - q.quality) / 5);
        samplefac = std::min(samplefac, ((q.w * q.h) >> 18) + 1);
        pal = neuQuant(q.px, q.w, q.h, mask, q.maxColors, samplefac);
        break;
    }
    case QM_OCTREE:
        pal = octreeQuantize(q.px, q.w, q.h, mask, q.maxColors, 10 - q.quality / 10);
        break;
    default:
        pal = wuQuantize(q.px, q.w, q.h, mask, q.maxColors, q.centerWeighted);
    }
    if ((int)pal.colors.size() > 255 - (hasT ? 1 : 0)) pal.colors.resize(255 - (hasT ? 1 : 0));
    tIndex = hasT ? (int)pal.colors.size() : -1;
    remap(q.px, q.w, q.h, mask, pal, q.dither, (uint8_t)std::max(0, tIndex), idx);
}

} // namespace bg
