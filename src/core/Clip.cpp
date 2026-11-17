// WebP clip editor model: keyframes, output plan, mask rendering, template tracker, JSON persistence.
// Ported from WebP Clip core.js (logic), render.js (mask effects) and tracker.js (tracker).
#include "core/Clip.h"

#include "core/Edit.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <set>

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#define BG_CLIP_SSE 1
#endif
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>
#define BG_CLIP_AVX 1   // MSVC emits AVX2/FMA intrinsics without /arch; used only after a CPU check
#endif

namespace bg {

// JS Math.round: halves round toward +infinity (-2.5 -> -2)
static double jsRound(double v) {
    double r = std::floor(v);
    return v - r >= 0.5 ? r + 1 : r;
}
static int iround(double v) { return (int)jsRound(v); }

// ---------------------------------------------------------------- project / masks

double defaultExportFps(double srcFps) { return jsRound(std::min(srcFps, 60.0) * 100) / 100; }

ClipProject newClipProject(int frameCount, double srcFps) {
    ClipProject p;
    p.inFrame = 0;
    p.outFrame = std::max(0, frameCount - 1);
    p.ex.fps = defaultExportFps(srcFps);
    return p;
}

ClipMask newClipMask(const std::wstring& name, const ClipMask* base) {
    ClipMask m;
    m.name = name;
    if (base) {
        m.effect = base->effect;
        m.strength = base->strength;
        m.color = base->color;
        m.zoom = base->zoom;
        m.border = base->border;
        m.shape = base->shape;
        m.pad = base->pad;
    }
    return m;
}

// ---------------------------------------------------------------- keyframes

bool interpBox(const std::map<int, Box4>& keys, int f, Box4& out) {
    if (keys.empty()) return false;
    auto hi = keys.lower_bound(f);
    if (hi != keys.end() && hi->first == f) { out = hi->second; return true; }
    if (hi == keys.begin()) { out = hi->second; return true; }
    if (hi == keys.end()) { out = std::prev(hi)->second; return true; }
    auto lo = std::prev(hi);
    double t = double(f - lo->first) / double(hi->first - lo->first);
    for (int i = 0; i < 4; ++i) out[i] = lo->second[i] + (hi->second[i] - lo->second[i]) * t;
    return true;
}

bool maskActive(const ClipMask& m, int f) { return m.enabled && m.start <= f && f <= m.end && !m.keys.empty(); }

void clearKeys(std::map<int, Box4>& keys, int lo, int hi) {
    if (hi < lo) return;
    keys.erase(keys.lower_bound(lo), keys.upper_bound(hi));
}

void smoothKeys(ClipMask& m, int lo, int hi, int win) {
    static const double sg9[9] = {-21, 14, 39, 54, 59, 54, 39, 14, -21};
    static const double sg7[7] = {-2, 3, 6, 7, 6, 3, -2};
    static const double sg5[5] = {-3, 12, 17, 12, -3};
    std::vector<std::vector<int>> runs;
    for (auto it = m.keys.lower_bound(lo); it != m.keys.end() && it->first <= hi; ++it) {
        if (!runs.empty() && it->first == runs.back().back() + 1) runs.back().push_back(it->first);
        else runs.push_back({it->first});
    }
    for (auto& run : runs) {
        int n = (int)run.size();
        if (n < 5) continue;
        std::vector<Box4> arr(n);
        for (int i = 0; i < n; ++i) arr[i] = m.keys[run[i]];
        for (int i = 0; i < n; ++i) {
            int room = std::min(i, n - 1 - i), size = 0;
            for (int s : {9, 7, 5})
                if (s <= win && (s >> 1) <= room) { size = s; break; }
            if (!size) continue;
            const double* c = size == 9 ? sg9 : size == 7 ? sg7 : sg5;
            double div = size == 9 ? 231 : size == 7 ? 21 : 35;
            int half = size >> 1;
            Box4 o{0, 0, 0, 0};
            for (int j = -half; j <= half; ++j)
                for (int k = 0; k < 4; ++k) o[k] += arr[i + j][k] * (c[j + half] / div);
            for (auto& v : o) v = jsRound(v * 100) / 100;
            m.keys[run[i]] = o;
        }
    }
}

std::vector<int> manualKeys(const ClipMask& m) {
    std::vector<int> out;
    for (auto& kv : m.keys)
        if (!m.trackScore.count(kv.first)) out.push_back(kv.first);
    return out;
}

int nextWeak(const ClipMask& m, int f, int dir, double weak) {
    std::set<int> bad;
    for (auto& kv : m.trackScore)
        if (kv.second < weak) bad.insert(kv.first);
    if (bad.empty()) return -1;
    std::vector<int> starts;
    for (int k : bad)
        if (!bad.count(k - 1)) starts.push_back(k);
    int cur = f;
    if (dir > 0) {
        while (bad.count(cur)) ++cur;
        for (int k : starts)
            if (k > cur) return k;
        return -1;
    }
    while (bad.count(cur - 1)) --cur;
    for (auto it = starts.rbegin(); it != starts.rend(); ++it)
        if (*it < cur) return *it;
    return -1;
}

int weakCount(const ClipMask& m, int lo, int hi, double weak) {
    std::set<int> bad;
    for (auto& kv : m.trackScore)
        if (kv.second < weak && kv.first >= lo && kv.first <= hi) bad.insert(kv.first);
    int n = 0;
    for (int k : bad)
        if (!bad.count(k - 1)) ++n;
    return n;
}

TrackResult blendTracks(const TrackResult& fwd, const TrackResult& bwd, int a, int b) {
    TrackResult r;
    const double span = b - a;   // the loop is empty unless b - a >= 2
    for (int f = a + 1; f < b; ++f) {
        auto pf = fwd.rects.find(f), pb = bwd.rects.find(f);
        bool hf = pf != fwd.rects.end(), hb = pb != bwd.rects.end();
        auto sf_ = fwd.scores.find(f), sb_ = bwd.scores.find(f);
        double sf = sf_ != fwd.scores.end() ? sf_->second : 1, sb = sb_ != bwd.scores.end() ? sb_->second : 1;
        if (hf && hb) {
            const Box4 &rf = pf->second, &rb = pb->second;
            double t = (f - a) / span;
            double d = std::hypot(rf[0] + rf[2] / 2 - rb[0] - rb[2] / 2, rf[1] + rf[3] / 2 - rb[1] - rb[3] / 2);
            if (d > std::max({rf[2], rf[3], rb[2], rb[3]}) * 0.5) {
                bool useF = sf > sb || (sf == sb && t <= 0.5);
                r.rects[f] = useF ? rf : rb;
                r.scores[f] = std::min(useF ? sf : sb, 0.59);
            } else {
                Box4 o;
                for (int i = 0; i < 4; ++i) o[i] = jsRound((rf[i] * (1 - t) + rb[i] * t) * 100) / 100;
                r.rects[f] = o;
                r.scores[f] = jsRound((sf * (1 - t) + sb * t) * 100) / 100;
            }
        } else if (hf || hb) {
            r.rects[f] = hf ? pf->second : pb->second;
            r.scores[f] = hf ? sf : sb;
        } else if (sf_ != fwd.scores.end() || sb_ != bwd.scores.end()) {
            r.scores[f] = 0;
        }
    }
    return r;
}

// ---------------------------------------------------------------- geometry

bool clipRect(const Box4& r, int pad, int w, int h, int out[4]) {
    int x0 = std::max(0, iround(r[0] - pad));
    int y0 = std::max(0, iround(r[1] - pad));
    int x1 = std::min(w, iround(r[0] + r[2] + pad));
    int y1 = std::min(h, iround(r[1] + r[3] + pad));
    if (x1 - x0 < 1 || y1 - y0 < 1) return false;
    out[0] = x0; out[1] = y0; out[2] = x1; out[3] = y1;
    return true;
}

void zoomBox(const int box[4], double zoom, int w, int h, int out[4]) {
    double z = std::max(1.0, zoom);
    int ow = std::min(w, iround((box[2] - box[0]) * z));
    int oh = std::min(h, iround((box[3] - box[1]) * z));
    int dx = iround((box[0] + box[2] - ow) / 2.0);
    int dy = iround((box[1] + box[3] - oh) / 2.0);
    dx = std::min(std::max(dx, 0), w - ow);
    dy = std::min(std::max(dy, 0), h - oh);
    out[0] = dx; out[1] = dy; out[2] = dx + ow; out[3] = dy + oh;
}

Box4 defaultDest(const Box4& r, int w, int h) {
    double x = r[0], y = r[1], rw = r[2], rh = r[3];
    double dw = std::min<double>(w, rw * 2), dh = std::min<double>(h, rh * 2);
    double dx = x + rw + 16 + dw <= w ? x + rw + 16 : x - 16 - dw;
    dx = std::min(std::max(dx, 0.0), w - dw);
    double dy = std::min(std::max(y + rh / 2 - dh / 2, 0.0), h - dh);
    return {dx, dy, dw, dh};
}

void cropBox(const ClipProject& p, int w, int h, int out[4]) {
    if (p.hasCrop && clipRect(p.crop, 0, w, h, out)) return;
    out[0] = 0; out[1] = 0; out[2] = w; out[3] = h;
}

void outputSize(const ClipProject& p, int w, int h, int& ow, int& oh) {
    int b[4];
    cropBox(p, w, h, b);
    int cw = b[2] - b[0], ch = b[3] - b[1], tw = p.ex.width;
    if (tw <= 0 || tw >= cw) { ow = cw; oh = ch; return; }
    ow = tw;
    oh = std::max(1, iround(double(ch) * tw / cw));
}

// ---------------------------------------------------------------- output plan

std::vector<ClipRange> clipRanges(const ClipProject& p) {
    std::vector<ClipRange> segs;
    for (auto& s : p.segments)
        if (s.hi >= s.lo) segs.push_back(s);
    if (segs.empty()) segs.push_back({p.inFrame, p.outFrame});
    return segs;
}

double speedAt(const ClipProject& p, int f) {
    double r = 1;
    bool any = false;
    for (auto& s : p.slowmos)
        if (s.lo <= f && f <= s.hi) { r = any ? std::min(r, s.rate) : s.rate; any = true; }
    return r;
}

static void rangeFrames(const ClipProject& p, int lo, int hi, double srcFps, std::vector<int>& out) {
    if (hi < lo) return;
    const ClipExport& ex = p.ex;
    std::vector<std::pair<int, double>> pending;   // sorted by frame (map order)
    for (auto it = p.freezes.lower_bound(lo); it != p.freezes.end() && it->first <= hi; ++it) pending.push_back(*it);
    size_t pi = 0;
    size_t start = out.size();
    auto emit = [&](int src) {
        while (pi < pending.size() && pending[pi].first <= src) {
            int n = iround(pending[pi].second * ex.fps);
            for (int k = 0; k < n; ++k) out.push_back(pending[pi].first);
            ++pi;
        }
        out.push_back(src);
    };
    double step = (ex.speed * srcFps) / ex.fps;
    bool slow = false;
    for (auto& s : p.slowmos)
        if (s.lo <= hi && s.hi >= lo) slow = true;
    if (!slow) {
        int n = std::max(1, iround((hi - lo + 1) / srcFps / ex.speed * ex.fps));
        for (int k = 0; k < n; ++k) emit(std::min(lo + (int)std::floor(k * step + 1e-6), hi));
        return;
    }
    double pos = lo;
    while (pos < hi + 1 - 1e-6 || out.size() == start) {
        int src = std::min((int)std::floor(pos + 1e-6), hi);
        emit(src);
        pos += step * std::max(0.01, speedAt(p, src));
    }
}

std::vector<int> outputFrames(const ClipProject& p, double srcFps) {
    std::vector<int> out;
    if (srcFps <= 0 || p.ex.fps <= 0 || p.ex.speed <= 0) return out;
    for (auto& r : clipRanges(p)) rangeFrames(p, r.lo, r.hi, srcFps, out);
    return out;
}

int frameDurationMs(int k, double fps) { return iround((k + 1) * 1000.0 / fps) - iround(k * 1000.0 / fps); }

double snapFps(double fps) {
    static const double cands[] = {23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 90, 100, 119.88, 120, 144, 240};
    double best = -1, err = 0.01;
    for (double c : cands) {
        double e = std::fabs(fps - c) / c;
        if (e < err) { best = c; err = e; }
    }
    return best >= 0 ? best : jsRound(fps * 1000) / 1000;
}

std::wstring exportFilename(const std::wstring& stem, const std::vector<ClipRange>& ranges) {
    if (ranges.empty()) return stem + L".webp";
    if (ranges.size() > 1) return stem + L"_" + std::to_wstring(ranges.size()) + L"구간.webp";
    return stem + L"_" + std::to_wstring(ranges[0].lo) + L"-" + std::to_wstring(ranges[0].hi) + L".webp";
}

std::vector<FitCandidate> fitCandidates(int fullWidth, double srcFps) {
    static const double ladder[][3] = {{1, 90, 60},    {1, 80, 60},    {1, 75, 50},    {1, 75, 40},    {0.85, 75, 40},
                                       {0.85, 70, 30}, {0.75, 70, 30}, {0.66, 70, 30}, {0.66, 65, 25}, {0.5, 65, 25},
                                       {0.5, 60, 20},  {0.42, 60, 20}, {0.33, 55, 15}, {0.25, 50, 15}, {0.2, 45, 12}};
    double maxFps = std::min(60.0, jsRound(srcFps));
    std::vector<FitCandidate> out;
    for (auto& l : ladder) {
        FitCandidate c;
        c.width = l[0] >= 1 ? 0 : std::max(120, iround(fullWidth * l[0] / 2) * 2);
        c.quality = (int)l[1];
        c.fps = std::min(maxFps, l[2]);
        if (c.width != 0 && c.width >= fullWidth) continue;
        bool seen = false;
        for (auto& o : out)
            if (o.width == c.width && o.quality == c.quality && o.fps == c.fps) seen = true;
        if (!seen) out.push_back(c);
    }
    return out;
}

// ---------------------------------------------------------------- audio

std::vector<float> envelopeDb(const std::vector<float>& mono, int sampleRate, double fps, int count) {
    std::vector<float> db(std::max(0, count), -120.f);
    if (fps <= 0 || sampleRate <= 0) return db;
    double spf = sampleRate / fps;
    long long n = (long long)mono.size();
    for (int f = 0; f < count; ++f) {
        long long a = (long long)(f * spf), b = std::min(n, (long long)((f + 1) * spf));
        if (b <= a) break;
        double s = 0;
        for (long long i = a; i < b; ++i) s += (double)mono[i] * mono[i];
        db[f] = (float)(20 * std::log10(std::max(std::sqrt(s / double(b - a)), 1e-6)));
    }
    return db;
}

std::vector<int> findEvents(const std::vector<float>& db, double fps, double rise, double floorDb, double window, double gap) {
    std::vector<int> ev;
    int n = (int)db.size();
    if (!n) return ev;
    int w = std::max(1, iround(window * fps));
    std::vector<double> cs(n + 1, 0);
    for (int i = 0; i < n; ++i) cs[i + 1] = cs[i] + std::pow(10.0, db[i] / 10.0);
    int minGap = std::max(1, iround(gap * fps));
    for (int i = iround(0.25 * fps); i < n; ++i) {
        int lo = std::max(0, i - w);
        double base = i == 0 ? std::pow(10.0, db[0] / 10.0) : (cs[i] - cs[lo]) / (i - lo);
        double score = db[i] - 10 * std::log10(std::max(base, 1e-12));
        if (score >= rise && db[i] >= floorDb && (ev.empty() || i - ev.back() >= minGap)) ev.push_back(i);
    }
    return ev;
}

// ---------------------------------------------------------------- rendering (render.js)

bool diffBox(const Image& a, const Image& b, int out[4], int tol) {
    if (a.w != b.w || a.h != b.h) {
        out[0] = 0; out[1] = 0; out[2] = b.w; out[3] = b.h;
        return b.w > 0 && b.h > 0;
    }
    int x0 = a.w, y0 = a.h, x1 = -1, y1 = -1;
    for (int y = 0; y < a.h; ++y) {
        const uint32_t *p = a.row(y), *q = b.row(y);
        for (int x = 0; x < a.w; ++x) {
            uint32_t u = p[x], v = q[x];
            if ((u & 0xFFFFFF) == (v & 0xFFFFFF)) continue;
            if (std::abs(R(u) - R(v)) > tol || std::abs(G(u) - G(v)) > tol || std::abs(B(u) - B(v)) > tol) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = y;
            }
        }
    }
    if (x1 < 0) return false;
    out[0] = x0 & ~1; out[1] = y0 & ~1; out[2] = x1 + 1; out[3] = y1 + 1;
    return true;
}

namespace {

// canvas path coverage: a rect at integer coordinates (hard edges) or an anti-aliased ellipse
struct Shape {
    bool ell;
    double x, y, w, h, cx, cy, rx, ry;
    Shape(bool e, double x_, double y_, double w_, double h_)
        : ell(e), x(x_), y(y_), w(w_), h(h_), cx(x_ + w_ / 2), cy(y_ + h_ / 2), rx(w_ / 2), ry(h_ / 2) {}
    // signed distance of the pixel centre from the outline (px, positive inside)
    double inside(int px, int py) const {
        double X = px + 0.5, Y = py + 0.5;
        if (!ell) return std::min(std::min(X - x, x + w - X), std::min(Y - y, y + h - Y));
        if (rx <= 0 || ry <= 0) return -1e9;
        double dx = X - cx, dy = Y - cy;
        double f = std::sqrt((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry));
        if (f < 1e-12) return std::min(rx, ry);
        double gx = dx / (rx * rx), gy = dy / (ry * ry);
        double grad = std::sqrt(gx * gx + gy * gy) / f;   // |grad sqrt(f)|: exact distance for circles
        return (1 - f) / grad;
    }
    static double cover(double d) { return std::clamp(d + 0.5, 0.0, 1.0); }
};

inline uint32_t mix(uint32_t a, uint32_t b, double t) {
    if (t >= 1) return b | 0xFF000000u;
    if (t <= 0) return a;
    auto ch = [&](int u, int v) { return (int)(u + (v - u) * t + 0.5); };
    return rgb(ch(R(a), R(b)), ch(G(a), G(b)), ch(B(a), B(b)));
}

Image cropImage(const Image& img, int x0, int y0, int x1, int y1) {
    Image o(x1 - x0, y1 - y0);
    for (int y = y0; y < y1; ++y) std::memcpy(o.row(y - y0), img.row(y) + x0, sizeof(uint32_t) * (size_t)o.w);
    return o;
}

// one axis of a canvas drawImage resample: Mitchell cubic when enlarging (imageSmoothingQuality
// "high"), area average when shrinking (mipmapped), identity at the same size
struct Taps { std::vector<int> idx; std::vector<float> wt; std::vector<int> start, count; };
Taps axisTaps(int src, int dst) {
    Taps t;
    t.start.resize(dst);
    t.count.resize(dst);
    double sc = (double)src / dst;
    for (int o = 0; o < dst; ++o) {
        t.start[o] = (int)t.idx.size();
        if (src == dst) {
            t.idx.push_back(o);
            t.wt.push_back(1.f);
        } else if (dst > src) {
            auto mitchell = [](double x) {   // B = C = 1/3
                x = std::fabs(x);
                if (x < 1) return (7 * x * x * x - 12 * x * x + 16.0 / 3) / 6;
                if (x < 2) return (-7.0 / 3 * x * x * x + 12 * x * x - 20 * x + 32.0 / 3) / 6;
                return 0.0;
            };
            double u = (o + 0.5) * sc - 0.5;
            int i0 = (int)std::floor(u);
            double sum = 0;
            float w4[4];
            for (int k = 0; k < 4; ++k) sum += w4[k] = (float)mitchell(u - (i0 - 1 + k));
            for (int k = 0; k < 4; ++k) {
                t.idx.push_back(std::clamp(i0 - 1 + k, 0, src - 1));
                t.wt.push_back((float)(w4[k] / sum));
            }
        } else {
            double a = o * sc, b = a + sc;
            for (int i = (int)a; i < (int)std::ceil(b) && i < src; ++i) {
                double c = std::min<double>(b, i + 1) - std::max<double>(a, i);
                t.idx.push_back(i);
                t.wt.push_back((float)(c / sc));
            }
        }
        t.count[o] = (int)t.idx.size() - t.start[o];
    }
    return t;
}

Image resampleHQ(const Image& s, int dw, int dh) {
    if (dw == s.w && dh == s.h) return s;
    Taps tx = axisTaps(s.w, dw), ty = axisTaps(s.h, dh);
    std::vector<float> tmp((size_t)dw * s.h * 3);
    for (int y = 0; y < s.h; ++y) {
        const uint32_t* r = s.row(y);
        float* d = &tmp[(size_t)y * dw * 3];
        for (int x = 0; x < dw; ++x) {
            float a = 0, g = 0, b = 0;
            for (int k = tx.start[x], e = k + tx.count[x]; k < e; ++k) {
                uint32_t p = r[tx.idx[k]];
                float w = tx.wt[k];
                a += R(p) * w; g += G(p) * w; b += B(p) * w;
            }
            d[x * 3] = a; d[x * 3 + 1] = g; d[x * 3 + 2] = b;
        }
    }
    Image o(dw, dh);
    for (int y = 0; y < dh; ++y) {
        uint32_t* d = o.row(y);
        for (int x = 0; x < dw; ++x) {
            float a = 0, g = 0, b = 0;
            for (int k = ty.start[y], e = k + ty.count[y]; k < e; ++k) {
                const float* p = &tmp[((size_t)ty.idx[k] * dw + x) * 3];
                float w = ty.wt[k];
                a += p[0] * w; g += p[1] * w; b += p[2] * w;
            }
            d[x] = rgb(clamp255((int)std::lround(a)), clamp255((int)std::lround(g)), clamp255((int)std::lround(b)));
        }
    }
    return o;
}

// paste(): draw src scaled into dest (rounded) clipped to the mask shape, then stroke the outline
// with lineWidth 2t (half of it falls outside the clip, so t px inside)
void pasteShape(Image& img, const ClipMask& m, const Image& src, const Box4& dest) {
    int dx = (int)jsRound(dest[0]), dy = (int)jsRound(dest[1]), dw = (int)jsRound(dest[2]), dh = (int)jsRound(dest[3]);
    if (dw <= 0 || dh <= 0 || src.w <= 0 || src.h <= 0) return;
    int xa = std::max(0, dx), ya = std::max(0, dy), xb = std::min(img.w, dx + dw), yb = std::min(img.h, dy + dh);
    if (xa >= xb || ya >= yb) return;
    Image piece = resampleHQ(src, dw, dh);
    Shape sh(m.shape == CS_ELLIPSE, dx, dy, dw, dh);
    int t = std::min({m.border, dw >> 1, dh >> 1});
    uint32_t col = m.color | 0xFF000000u;
    for (int y = ya; y < yb; ++y) {
        uint32_t* d = img.row(y);
        const uint32_t* s = piece.row(y - dy);
        for (int x = xa; x < xb; ++x) {
            double din = sh.inside(x, y), c = Shape::cover(din);
            if (c <= 0) continue;
            uint32_t p = mix(d[x], s[x - dx], c);
            if (t > 0) p = mix(p, col, c * std::clamp(t + 0.5 - din, 0.0, 1.0));
            d[x] = p;
        }
    }
}

// canvas filter blur(s px): Gaussian with sigma = s over the area grown by 3s (cut at the frame),
// transparent outside it, drawn source-over inside the shape
void blurShape(Image& img, const int b[4], int s, const Shape& sh) {
    const int W = img.w, H = img.h, x0 = b[0], y0 = b[1], x1 = b[2], y1 = b[3], w = x1 - x0, h = y1 - y0;
    const int mg = s * 3;
    const int ex0 = std::max(0, x0 - mg), ey0 = std::max(0, y0 - mg), ex1 = std::min(W, x1 + mg), ey1 = std::min(H, y1 + mg);
    const double sigma = s;
    const int KR = (int)std::ceil(3 * sigma);
    std::vector<float> K(2 * KR + 1), cum(2 * KR + 2, 0);
    double ks = 0;
    for (int k = -KR; k <= KR; ++k) ks += K[k + KR] = (float)std::exp(-(double)k * k / (2 * sigma * sigma));
    for (auto& v : K) v = (float)(v / ks);
    for (int i = 0; i <= 2 * KR; ++i) cum[i + 1] = cum[i] + K[i];
    auto kSum = [&](int lo, int hi) {   // sum of K[k] for k in [lo, hi)
        lo = std::clamp(lo, -KR, KR + 1);
        hi = std::clamp(hi, -KR, KR + 1);
        return hi > lo ? cum[hi + KR] - cum[lo + KR] : 0.f;
    };
    // horizontal pass for rows ry0..ry1 of the source, output columns x0..x1 (planar R, G, B)
    const int ry0 = std::max(ey0, y0 - KR), ry1 = std::min(ey1, y1 + KR), rows = ry1 - ry0;
    std::vector<float> src(3 * (size_t)(ex1 - ex0)), hp(3 * (size_t)rows * w, 0.f);
    const int sw = ex1 - ex0;
    for (int y = ry0; y < ry1; ++y) {
        const uint32_t* r = img.row(y);
        for (int x = ex0; x < ex1; ++x) {
            uint32_t p = r[x];
            src[x - ex0] = (float)R(p);
            src[sw + x - ex0] = (float)G(p);
            src[2 * sw + x - ex0] = (float)B(p);
        }
        for (int c = 0; c < 3; ++c) {
            float* o = &hp[((size_t)c * rows + (y - ry0)) * w];
            const float* sc = &src[(size_t)c * sw];
            for (int k = -KR; k <= KR; ++k) {
                int xa = std::max(x0, ex0 - k), xb = std::min(x1, ex1 - k);
                const float kv = K[k + KR];
                const float* in = sc + (xa + k - ex0);
                float* out = o + (xa - x0);
                for (int i = 0, n = xb - xa; i < n; ++i) out[i] += kv * in[i];
            }
        }
    }
    std::vector<float> vrow(3 * (size_t)w), ax(w);
    for (int x = x0; x < x1; ++x) ax[x - x0] = kSum(ex0 - x, ex1 - x);
    for (int y = y0; y < y1; ++y) {
        std::fill(vrow.begin(), vrow.end(), 0.f);
        int ja = std::max(-KR, ry0 - y), jb = std::min(KR, ry1 - 1 - y);
        for (int c = 0; c < 3; ++c) {
            float* out = &vrow[(size_t)c * w];
            for (int j = ja; j <= jb; ++j) {
                const float kv = K[j + KR];
                const float* in = &hp[((size_t)c * rows + (y + j - ry0)) * w];
                for (int i = 0; i < w; ++i) out[i] += kv * in[i];
            }
        }
        const float ay = kSum(ey0 - y, ey1 - y);
        uint32_t* d = img.row(y);
        for (int x = x0; x < x1; ++x) {
            double c = Shape::cover(sh.inside(x, y));
            if (c <= 0) continue;
            int i = x - x0;
            double a = c * ax[i] * ay, k = 1 - a;   // premultiplied colour over the old pixel
            uint32_t p = d[x];
            int r = (int)(R(p) * k + c * vrow[i] + 0.5), g = (int)(G(p) * k + c * vrow[w + i] + 0.5),
                bl = (int)(B(p) * k + c * vrow[2 * w + i] + 0.5);
            d[x] = rgb(clamp255(r), clamp255(g), clamp255(bl));
        }
    }
}

void applyMask(Image& img, const ClipMask& m, const Box4& rect, int f) {
    const int W = img.w, H = img.h;
    int b[4];
    if (!clipRect(rect, m.pad, W, H, b)) return;
    const int x0 = b[0], y0 = b[1], x1 = b[2], y1 = b[3], w = x1 - x0, h = y1 - y0;
    const int s = std::max(1, m.strength);
    const bool ell = m.shape == CS_ELLIPSE;
    if (m.effect == CE_ZOOM) {
        Image src = cropImage(img, x0, y0, x1, y1);
        int z[4];
        zoomBox(b, m.zoom, W, H, z);
        pasteShape(img, m, src, {(double)z[0], (double)z[1], (double)(z[2] - z[0]), (double)(z[3] - z[1])});
        return;
    }
    if (m.effect == CE_PORTAL) {
        Box4 dest;
        if (interpBox(m.destKeys, f, dest)) pasteShape(img, m, cropImage(img, x0, y0, x1, y1), dest);
        return;
    }
    Shape sh(ell, x0, y0, w, h);
    if (m.effect == CE_BOX) {
        uint32_t col = m.color | 0xFF000000u;
        for (int y = y0; y < y1; ++y) {
            uint32_t* d = img.row(y);
            for (int x = x0; x < x1; ++x) d[x] = mix(d[x], col, Shape::cover(sh.inside(x, y)));
        }
    } else if (m.effect == CE_BLUR) {
        blurShape(img, b, s, sh);
    } else {
        // mosaic: shrink to floor(w/s) x floor(h/s) with smoothing, enlarge back without smoothing
        const int sw = std::max(1, w / s), shh = std::max(1, h / s);
        Image small = resampleHQ(cropImage(img, x0, y0, x1, y1), sw, shh);
        std::vector<int> cx(w);
        for (int x = 0; x < w; ++x) cx[x] = std::min(sw - 1, (int)((x + 0.5) * sw / w));
        for (int y = y0; y < y1; ++y) {
            const uint32_t* c = small.row(std::min(shh - 1, (int)((y - y0 + 0.5) * shh / h)));
            uint32_t* d = img.row(y);
            for (int x = x0; x < x1; ++x) d[x] = mix(d[x], c[cx[x - x0]], Shape::cover(sh.inside(x, y)));
        }
    }
}

} // namespace

void renderMasks(Image& img, const std::vector<ClipMask>& masks, int f) {
    for (auto& m : masks) {
        if (!maskActive(m, f)) continue;
        Box4 r;
        if (interpBox(m.keys, f, r)) applyMask(img, m, r, f);
    }
}

ImagePtr cropScale(const Image& img, const ClipProject& p) {
    int b[4];
    cropBox(p, img.w, img.h, b);
    int cw = b[2] - b[0], ch = b[3] - b[1];
    if (p.ex.width > 0 && p.ex.width < cw) {
        Image c = cropImage(img, b[0], b[1], b[2], b[3]);
        return resizeImage(c, p.ex.width, std::max(1, iround(double(ch) * p.ex.width / cw)));
    }
    if (b[0] == 0 && b[1] == 0 && cw == img.w && ch == img.h) return std::make_shared<Image>(img);
    return std::make_shared<Image>(cropImage(img, b[0], b[1], b[2], b[3]));
}

// ---------------------------------------------------------------- tracker (tracker.js)

namespace {

struct Plane { int w = 0, h = 0, ox = 0, oy = 0; std::vector<float> g; };   // one channel, ox/oy = frame offset
struct Norm { int w = 0, h = 0; double norm = 0; std::vector<float> z; };     // zero-mean template

inline double channelValue(int ch, double r, double g, double b) {
    switch (ch) {
    case TC_R: return r;
    case TC_G: return g;
    case TC_B: return b;
    case TC_RG: return r - g;
    case TC_BY: return b - (r + g) / 2;
    default: return 0.299 * r + 0.587 * g + 0.114 * b;
    }
}

struct GrayLut {   // 0.299 r + 0.587 g + 0.114 b, same products and order as the formula
    double r[256], g[256], b[256];
    GrayLut() {
        for (int i = 0; i < 256; ++i) { r[i] = 0.299 * i; g[i] = 0.587 * i; b[i] = 0.114 * i; }
    }
};
const GrayLut kGray;

// pixels outside the image read as transparent black (getImageData)
inline uint32_t pixelAt(const Image& im, int x, int y) {
    return (x >= 0 && y >= 0 && x < im.w && y < im.h) ? im.row(y)[x] : 0u;
}

template <class F> void projectRows(const Image& im, int x, int y, int w, int h, float* d, F f) {
    for (int j = 0; j < h; ++j, d += w) {
        const uint32_t* s = im.row(y + j) + x;
        for (int i = 0; i < w; ++i) d[i] = f(s[i]);
    }
}

Plane project(const Image& im, int x, int y, int w, int h, int ch) {
    Plane p;
    p.w = w;
    p.h = h;
    p.ox = x;
    p.oy = y;
    p.g.resize((size_t)w * h);
    float* d = p.g.data();
    if (x >= 0 && y >= 0 && x + w <= im.w && y + h <= im.h) {
        // non-gray channels are integers or halves, exact in float
        switch (ch) {
        case TC_R: projectRows(im, x, y, w, h, d, [](uint32_t c) { return (float)R(c); }); break;
        case TC_G: projectRows(im, x, y, w, h, d, [](uint32_t c) { return (float)G(c); }); break;
        case TC_B: projectRows(im, x, y, w, h, d, [](uint32_t c) { return (float)B(c); }); break;
        case TC_RG: projectRows(im, x, y, w, h, d, [](uint32_t c) { return (float)(R(c) - G(c)); }); break;
        case TC_BY: projectRows(im, x, y, w, h, d, [](uint32_t c) { return (float)(2 * B(c) - R(c) - G(c)) * 0.5f; }); break;
        default: projectRows(im, x, y, w, h, d, [](uint32_t c) { return (float)(kGray.r[R(c)] + kGray.g[G(c)] + kGray.b[B(c)]); });
        }
        return p;
    }
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            uint32_t c = pixelAt(im, x + i, y + j);
            d[(size_t)j * w + i] = (float)channelValue(ch, R(c), G(c), B(c));
        }
    return p;
}

// channel with the largest spread inside the patch (gray wins near-ties)
int pickChannel(const Image& im, int x, int y, int w, int h) {
    int best = TC_GRAY;
    double bestStd = -1, n = (double)w * h;
    for (int ch = TC_GRAY; ch <= TC_BY; ++ch) {
        double s = 0, s2 = 0;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                uint32_t c = pixelAt(im, x + i, y + j);
                double v = channelValue(ch, R(c), G(c), B(c));
                s += v;
                s2 += v * v;
            }
        double sd = std::sqrt(std::max(0.0, s2 / n - (s / n) * (s / n))) * (ch == TC_GRAY ? 1.15 : 1);
        if (sd > bestStd + 1e-9) { best = ch; bestStd = sd; }
    }
    return best;
}

Plane downscale(const Plane& src, int ds) {
    if (ds == 1) return src;
    Plane o;
    o.w = std::max(1, src.w / ds);
    o.h = std::max(1, src.h / ds);
    o.g.resize((size_t)o.w * o.h);
    const double inv = 1.0 / (ds * ds);
    if (src.w >= ds && src.h >= ds) {   // every block inside
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x) {
                double s = 0;
                const float* b = &src.g[(size_t)y * ds * src.w + (size_t)x * ds];
                for (int dy = 0; dy < ds; ++dy, b += src.w)
                    for (int dx = 0; dx < ds; ++dx) s += b[dx];
                o.g[(size_t)y * o.w + x] = (float)(s * inv);
            }
        return o;
    }
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            double s = 0;
            for (int dy = 0; dy < ds; ++dy) {
                const float* row = &src.g[(size_t)std::min(src.h - 1, y * ds + dy) * src.w];
                for (int dx = 0; dx < ds; ++dx) s += row[std::min(src.w - 1, x * ds + dx)];
            }
            o.g[(size_t)y * o.w + x] = (float)(s * inv);
        }
    return o;
}

Plane cropPlane(const Plane& src, int x, int y, int w, int h) {
    Plane o;
    o.w = w;
    o.h = h;
    o.g.resize((size_t)w * h);
    for (int j = 0; j < h; ++j) std::memcpy(&o.g[(size_t)j * w], &src.g[(size_t)(y + j) * src.w + x], sizeof(float) * w);
    return o;
}

// outW x outH samples; output (u, v) centre = src pixel-centre coordinate (x0 + u*step, y0 + v*step).
// Large steps box-average first to avoid aliasing.
Plane resample(const Plane& src, int outW, int outH, double x0, double y0, double step) {
    Plane pre;
    const Plane* s = &src;
    int k = 1;
    if (step >= 2) {
        k = (int)std::floor(step);
        pre = downscale(src, k);
        s = &pre;
    }
    Plane o;
    o.w = outW;
    o.h = outH;
    o.g.resize((size_t)std::max(0, outW) * std::max(0, outH));
    const int W = s->w, H = s->h;
    std::vector<int> xa(outW), xb(outW);
    std::vector<double> fxs(outW);
    for (int u = 0; u < outW; ++u) {
        double X = x0 + (u + 0.5) * step - 0.5;
        if (k > 1) X = (X - (k - 1) / 2.0) / k;
        X = std::min(W - 1.0, std::max(0.0, X));
        xa[u] = (int)std::floor(X);
        xb[u] = std::min(W - 1, xa[u] + 1);
        fxs[u] = X - xa[u];
    }
    for (int v = 0; v < outH; ++v) {
        double Y = y0 + (v + 0.5) * step - 0.5;
        if (k > 1) Y = (Y - (k - 1) / 2.0) / k;
        Y = std::min(H - 1.0, std::max(0.0, Y));
        const int y1 = (int)std::floor(Y), y2 = std::min(H - 1, y1 + 1);
        const double fy = Y - y1;
        const float *r1 = &s->g[(size_t)y1 * W], *r2 = &s->g[(size_t)y2 * W];
        float* d = &o.g[(size_t)v * outW];
        for (int u = 0; u < outW; ++u) {
            const double fx = fxs[u];
            d[u] = (float)((r1[xa[u]] * (1 - fx) + r1[xb[u]] * fx) * (1 - fy) + (r2[xa[u]] * (1 - fx) + r2[xb[u]] * fx) * fy);
        }
    }
    return o;
}

Norm normalize(const Plane& t) {
    Norm n;
    n.w = t.w;
    n.h = t.h;
    double mean = 0;
    for (float v : t.g) mean += v;
    mean /= (double)t.g.size();
    n.z.resize(t.g.size());
    double ss = 0;
    for (size_t i = 0; i < t.g.size(); ++i) {
        n.z[i] = (float)(t.g[i] - mean);
        ss += (double)n.z[i] * n.z[i];
    }
    n.norm = std::sqrt(ss);
    return n;
}

struct Integrals {
    int W = 0;
    std::vector<double> s, s2;
    explicit Integrals(const Plane& a) : W(a.w + 1), s((size_t)W * (a.h + 1), 0.0), s2(s.size(), 0.0) {
        for (int y = 0; y < a.h; ++y) {
            double rs = 0, rs2 = 0;
            const float* r = &a.g[(size_t)y * a.w];
            for (int x = 0; x < a.w; ++x) {
                double v = r[x];
                rs += v;
                rs2 += v * v;
                s[(size_t)(y + 1) * W + x + 1] = s[(size_t)y * W + x + 1] + rs;
                s2[(size_t)(y + 1) * W + x + 1] = s2[(size_t)y * W + x + 1] + rs2;
            }
        }
    }
};

float dotSse(const float* a, const float* b, int n) {
    int i = 0;
#ifdef BG_CLIP_SSE
    __m128 s0 = _mm_setzero_ps(), s1 = _mm_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        s0 = _mm_add_ps(s0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
        s1 = _mm_add_ps(s1, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
    }
    s0 = _mm_add_ps(s0, s1);
    float t[4];
    _mm_storeu_ps(t, s0);
    float s = (t[0] + t[1]) + (t[2] + t[3]);
#else
    float s = 0;
#endif
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

// out[y * nx + x] = sum over template rows j of dot(area row y + j from x, template row j), x < nx, y < ny
void corrGridSse(const float* A, int aw, const float* T, int tw, int th, int nx, int ny, double* out) {
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            double s = 0;
            for (int j = 0; j < th; ++j) s += dotSse(A + (size_t)(y + j) * aw + x, T + (size_t)j * tw, tw);
            out[(size_t)y * nx + x] = s;
        }
}

#ifdef BG_CLIP_AVX
inline float hsum(__m256 v) {
    __m128 q = _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1));
    float t[4];
    _mm_storeu_ps(t, q);
    return (t[0] + t[1]) + (t[2] + t[3]);
}
inline float dotAvx(const float* a, const float* b, int n) {
    __m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps();
    int i = 0;
    for (; i + 16 <= n; i += 16) {
        s0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), s0);
        s1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8), s1);
    }
    if (i + 8 <= n) {
        s0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), s0);
        i += 8;
    }
    float s = hsum(_mm256_add_ps(s0, s1));
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}
// one row of the correlation grid: o[x] += dot(ar + x, tr, n) for x < nx; four neighbouring positions
// share the template loads and one horizontal reduction
void rowCorrAvx(const float* ar, const float* tr, int n, int nx, double* o) {
    int x = 0;
    for (; x + 4 <= nx; x += 4) {
        const float* a = ar + x;
        __m256 p0 = _mm256_setzero_ps(), q0 = p0, p1 = p0, q1 = p0, p2 = p0, q2 = p0, p3 = p0, q3 = p0;
        int i = 0;
        for (; i + 16 <= n; i += 16) {
            const __m256 t0 = _mm256_loadu_ps(tr + i), t1 = _mm256_loadu_ps(tr + i + 8);
            p0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), t0, p0);
            q0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8), t1, q0);
            p1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 1), t0, p1);
            q1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 9), t1, q1);
            p2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 2), t0, p2);
            q2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 10), t1, q2);
            p3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 3), t0, p3);
            q3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 11), t1, q3);
        }
        if (i + 8 <= n) {
            const __m256 t0 = _mm256_loadu_ps(tr + i);
            p0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), t0, p0);
            p1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 1), t0, p1);
            p2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 2), t0, p2);
            p3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 3), t0, p3);
            i += 8;
        }
        // lanes -> [s0 s1 s2 s3] (low and high halves added)
        const __m256 h = _mm256_hadd_ps(_mm256_hadd_ps(_mm256_add_ps(p0, q0), _mm256_add_ps(p1, q1)),
                                        _mm256_hadd_ps(_mm256_add_ps(p2, q2), _mm256_add_ps(p3, q3)));
        __m128 s4 = _mm_add_ps(_mm256_castps256_ps128(h), _mm256_extractf128_ps(h, 1));
        if (i < n) {
            alignas(16) float t[4];
            _mm_store_ps(t, s4);
            for (; i < n; ++i)
                for (int k = 0; k < 4; ++k) t[k] += a[i + k] * tr[i];
            s4 = _mm_load_ps(t);
        }
        _mm256_storeu_pd(o + x, _mm256_add_pd(_mm256_loadu_pd(o + x), _mm256_cvtps_pd(s4)));
    }
    for (; x < nx; ++x) o[x] += dotAvx(ar + x, tr, n);
}
void corrGridAvx(const float* A, int aw, const float* T, int tw, int th, int nx, int ny, double* out) {
    std::fill(out, out + (size_t)nx * ny, 0.0);
    for (int j = 0; j < th; ++j)   // template rows outer: each row and the area rows it meets stay in cache
        for (int y = 0; y < ny; ++y) rowCorrAvx(A + (size_t)(y + j) * aw, T + (size_t)j * tw, tw, nx, out + (size_t)y * nx);
}
bool cpuAvx2Fma() {
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuid(r, 1);
    if (!(r[2] & (1 << 12)) || !(r[2] & (1 << 27)) || !(r[2] & (1 << 28))) return false;   // FMA, OSXSAVE, AVX
    if ((_xgetbv(0) & 6) != 6) return false;
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) != 0;   // AVX2
}
const bool kAvx = cpuAvx2Fma();
const auto dot = kAvx ? dotAvx : dotSse;
const auto corrGrid = kAvx ? corrGridAvx : corrGridSse;
#else
const auto dot = dotSse;
const auto corrGrid = corrGridSse;
#endif

// normalised correlation with the template's top-left at (x, y): -1..1, flat areas 0, outside -1
double nccAt(const Plane& area, const Integrals& ii, const Norm& t, int x, int y) {
    const int tw = t.w, th = t.h;
    if (x < 0 || y < 0 || x + tw > area.w || y + th > area.h || t.norm < 1e-3) return -1;
    const double n = (double)tw * th;
    const int W = ii.W;
    auto box = [&](const std::vector<double>& v) {
        return v[(size_t)(y + th) * W + x + tw] - v[(size_t)y * W + x + tw] - v[(size_t)(y + th) * W + x] + v[(size_t)y * W + x];
    };
    const double s = box(ii.s), varA = box(ii.s2) - s * s / n;
    if (varA < n * 0.25) return 0;   // std below 0.5 per pixel: nothing to compare
    double sxy = 0;
    for (int j = 0; j < th; ++j) sxy += dot(&area.g[(size_t)(y + j) * area.w + x], &t.z[(size_t)j * tw], tw);
    return sxy / (std::sqrt(varA) * t.norm);
}

double ncc(const Plane& patch, const Norm& t) { return nccAt(patch, Integrals(patch), t, 0, 0); }

// nccAt() for every template position inside area
struct NccGrid {
    int nx = 0, ny = 0;
    std::vector<double> v;
    NccGrid(const Plane& a, const Norm& t) {
        nx = a.w - t.w + 1;
        ny = a.h - t.h + 1;
        if (nx < 1 || ny < 1) { nx = ny = 0; return; }
        v.assign((size_t)nx * ny, -1.0);
        if (t.norm < 1e-3) return;
        std::vector<double> sxy((size_t)nx * ny);
        corrGrid(a.g.data(), a.w, t.z.data(), t.w, t.h, nx, ny, sxy.data());
        Integrals ii(a);
        const double n = (double)t.w * t.h;
        const int W = ii.W;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const size_t i0 = (size_t)y * W + x, i1 = (size_t)(y + t.h) * W + x;
                const double s = ii.s[i1 + t.w] - ii.s[i0 + t.w] - ii.s[i1] + ii.s[i0];
                const double varA = ii.s2[i1 + t.w] - ii.s2[i0 + t.w] - ii.s2[i1] + ii.s2[i0] - s * s / n;
                v[(size_t)y * nx + x] = varA < n * 0.25 ? 0 : sxy[(size_t)y * nx + x] / (std::sqrt(varA) * t.norm);
            }
    }
    double at(int x, int y) const { return x < 0 || y < 0 || x >= nx || y >= ny ? -1 : v[(size_t)y * nx + x]; }
};

struct MatchR { double x = 0, y = 0; int ix = 0, iy = 0; double score = -2; };

// best spot of t in area (area coords), penalised by lam per px from (ex, ey); sub-pixel parabola fit
MatchR match(const Plane& area, const Norm& t, double ex, double ey, double lam, bool subpixel) {
    NccGrid g(area, t);
    double best = -1e9, raw = -2;
    int bx = 0, by = 0;
    for (int y = 0; y < g.ny; ++y)
        for (int x = 0; x < g.nx; ++x) {
            double sc = g.v[(size_t)y * g.nx + x];
            double adj = sc - lam * std::hypot(x - ex, y - ey);
            if (adj > best) { best = adj; bx = x; by = y; raw = sc; }
        }
    double sx = 0, sy = 0;
    if (subpixel && raw > 0) {
        auto peak = [&](double a, double c) {
            if (a < -0.5 || c < -0.5) return 0.0;
            double den = a - 2 * raw + c;
            return den < -1e-9 ? std::max(-0.5, std::min(0.5, (a - c) / (2 * den))) : 0.0;
        };
        sx = peak(g.at(bx - 1, by), g.at(bx + 1, by));
        sy = peak(g.at(bx, by - 1), g.at(bx, by + 1));
    }
    return {bx + sx, by + sy, bx, by, raw};
}

struct Peak { int x, y; float score; };

// up to k non-overlapping peaks of adj(x, y, score), best first
template <class Adj> std::vector<Peak> peaks(const Plane& area, const Norm& t, int k, Adj adj) {
    std::vector<Peak> out;
    const int mw = area.w - t.w + 1, mh = area.h - t.h + 1;
    if (mw < 1 || mh < 1) return out;
    NccGrid g(area, t);
    std::vector<float> raw((size_t)mw * mh), val((size_t)mw * mh);
    for (int y = 0; y < mh; ++y)
        for (int x = 0; x < mw; ++x) {
            double sc = g.v[(size_t)y * mw + x];
            raw[(size_t)y * mw + x] = (float)sc;
            val[(size_t)y * mw + x] = (float)adj(x, y, sc);
        }
    const float NEG = -std::numeric_limits<float>::infinity();
    const int sx = std::max(1, t.w >> 1), sy = std::max(1, t.h >> 1);
    for (int n = 0; n < k; ++n) {
        int bi = -1;
        float bv = NEG;
        for (size_t i = 0; i < val.size(); ++i)
            if (val[i] > bv) { bv = val[i]; bi = (int)i; }
        if (bi < 0 || bv == NEG) break;
        const int bx = bi % mw, by = bi / mw;
        out.push_back({bx, by, raw[bi]});
        for (int y = std::max(0, by - sy + 1); y < std::min(mh, by + sy); ++y)
            for (int x = std::max(0, bx - sx + 1); x < std::min(mw, bx + sx); ++x) val[(size_t)y * mw + x] = NEG;
    }
    return out;
}

// template at full size plus lazily built downscaled versions
struct TplSet {
    Plane raw;
    Norm full;
    std::map<int, Norm> small;
    explicit TplSet(Plane r) : raw(std::move(r)), full(normalize(raw)) {}
    const Norm& at(int ds) {
        if (ds == 1) return full;
        auto it = small.find(ds);
        if (it == small.end()) it = small.emplace(ds, normalize(downscale(raw, ds))).first;
        return it->second;
    }
};
using TplPtr = std::shared_ptr<TplSet>;

struct Spot { double cx = 0, cy = 0, score = 0; };
struct Found {   // a _find / _search result
    double x = 0, y = 0, score = 0, adj = 0;
    std::vector<Spot> others;
};
struct SearchR {
    double cx = 0, cy = 0, score = 0, s = 1;
    bool partial = false;
    std::vector<Spot> others;
    TplPtr set;
};

} // namespace

struct TemplateTracker::Impl {
    const Image* src = nullptr;
    bool fixedSize = false;
    int maxCoast = 15;
    int bw = 4, bh = 4, ds = 1, channel = TC_GRAY;
    double scale = 1, srate = 1, cx = 0, cy = 0, vx = 0, vy = 0, fps = 30;
    int coast = 0;
    long long sinceScan = LLONG_MAX / 2;
    struct Distractor { double cx, cy; int age; };
    std::vector<Distractor> distractors;
    struct Cand { double cx, cy, s; TplPtr set; double vx, vy; int hits; };
    bool hasCand = false;
    Cand cand{};
    TplPtr tpl0, anchor, tpl;
    std::vector<TplPtr> bank;
    std::vector<float> rawA;

    int W() const { return src->w; }
    int H() const { return src->h; }
    double w() const { return bw * scale; }
    double h() const { return bh * scale; }
    Box4 rect() const { return {cx - w() / 2, cy - h() / 2, w(), h()}; }

    void init(const Image& frame, const Box4& r, const TrackerOptions& o) {
        src = &frame;
        fixedSize = o.fixedSize;
        fps = o.fps > 0 ? o.fps : 30;
        maxCoast = std::max(5, (int)jsRound(fps * 0.5));
        bw = std::max(4, std::min(frame.w, (int)jsRound(r[2])));
        bh = std::max(4, std::min(frame.h, (int)jsRound(r[3])));
        cx = r[0] + r[2] / 2;
        cy = r[1] + r[3] / 2;
        ds = std::max(1, (int)std::ceil(std::max(bw, bh) / 40.0));
        // the first look is taken inside the frame (a box over the edge is pushed in)
        int x = (int)jsRound(std::min<double>(frame.w - bw, std::max(0.0, cx - bw / 2.0)));
        int y = (int)jsRound(std::min<double>(frame.h - bh, std::max(0.0, cy - bh / 2.0)));
        channel = o.channel >= TC_GRAY && o.channel <= TC_BY ? o.channel : pickChannel(frame, x, y, bw, bh);
        tpl0 = std::make_shared<TplSet>(project(frame, x, y, bw, bh, channel));
        int nb = 0;
        for (auto& b : o.bank) {
            if (!b || b->w < 2 || b->h < 2 || nb >= 8) continue;
            ++nb;
            Plane f = project(*b, 0, 0, b->w, b->h, channel);
            auto t = std::make_shared<TplSet>(resample(f, bw, bh, 0, 0, (double)b->w / bw));
            if (t->full.norm > 1e-3) bank.push_back(std::move(t));
        }
        setAnchor(tpl0);
        clampCenter();
    }

    void setAnchor(const TplPtr& set) {
        anchor = set;
        rawA = set->raw.g;
        rebuild();
    }
    // 40% of the anchor look stays in, so long tracks don't drift onto the background
    void rebuild() {
        Plane g;
        g.w = bw;
        g.h = bh;
        g.g.resize(rawA.size());
        const std::vector<float>& a = anchor->raw.g;
        for (size_t i = 0; i < g.g.size(); ++i) g.g[i] = (float)(0.4 * a[i] + 0.6 * rawA[i]);
        tpl = std::make_shared<TplSet>(std::move(g));
    }
    // the box may hang over the edge, centre within 20% of its size outside
    void clampCenter() {
        double ww = bw * (scale ? scale : 1), hh = bh * (scale ? scale : 1);
        cx = std::min(W() + ww * 0.2, std::max(-ww * 0.2, cx));
        cy = std::min(H() + hh * 0.2, std::max(-hh * 0.2, cy));
    }
    bool inside(double x, double y, double s) const {
        double ww = bw * s, hh = bh * s;
        return x - ww / 2 >= -0.5 && y - hh / 2 >= -0.5 && x + ww / 2 <= W() + 0.5 && y + hh / 2 <= H() + 0.5;
    }
    double clampScale(double s) const {
        double maxS = std::min({(double)W() / bw, (double)H() / bh, 6.0});
        double minS = std::max(0.25, 6.0 / std::min(bw, bh));
        return std::min(maxS, std::max(std::min(minS, maxS), s));
    }
    // channel values of frame area [x, y, w, h], cut to the frame
    Plane feat(double x, double y, double w, double h) const {
        int x0 = std::max(0, std::min(W() - 1, (int)std::floor(x))), y0 = std::max(0, std::min(H() - 1, (int)std::floor(y)));
        int x1 = std::max(x0 + 1, std::min(W(), (int)std::ceil(x + w))), y1 = std::max(y0 + 1, std::min(H(), (int)std::ceil(y + h)));
        return project(*src, x0, y0, x1 - x0, y1 - y0, channel);
    }
    // box at centre (x, y), scale s, resampled to template size
    Plane patch(double x, double y, double s) const {
        double ww = bw * s, hh = bh * s, px = x - ww / 2, py = y - hh / 2;
        Plane a = feat(px - 1, py - 1, ww + 3, hh + 3);
        return resample(a, bw, bh, px - a.ox, py - a.oy, s);
    }

    bool search(double pcx, double pcy, const TplPtr& set, double r, double lam, int dsc, double s, SearchR& out) {
        double ww = bw * s, hh = bh * s, px = pcx - ww / 2, py = pcy - hh / 2;
        // a box over the frame edge searches with the visible part of the template only
        int tx0 = std::max(0, (int)std::ceil(-px / s - 1e-6)), ty0 = std::max(0, (int)std::ceil(-py / s - 1e-6));
        int tx1 = std::min(bw, (int)std::floor((W() - px) / s + 1e-6)), ty1 = std::min(bh, (int)std::floor((H() - py) / s + 1e-6));
        Found q;
        double ox, oy;
        out.partial = false;
        if (tx0 > 0 || ty0 > 0 || tx1 < bw || ty1 < bh) {
            int tw = tx1 - tx0, th = ty1 - ty0;
            if (tw < 4 || th < 4 || tw * th < 0.4 * bw * bh) return false;   // too little visible
            TplSet sub(cropPlane(set->raw, tx0, ty0, tw, th));
            int d = (int)std::floor(std::min(tw / 6.0, th / 4.0));
            if (d == 0) d = 1;
            if (!find(px + tx0 * s, py + ty0 * s, sub, r, lam, std::max(1, std::min(dsc, d)), s, q)) return false;
            ox = -tx0 * s + ww / 2;
            oy = -ty0 * s + hh / 2;
            out.partial = true;
        } else {
            if (!find(px, py, *set, r, lam, dsc, s, q)) return false;
            ox = ww / 2;
            oy = hh / 2;
        }
        out.cx = q.x + ox;
        out.cy = q.y + oy;
        out.score = q.score;
        out.s = s;
        out.set = nullptr;
        out.others.clear();
        for (auto& o : q.others) out.others.push_back({o.cx + ox, o.cy + oy, o.score});
        return true;
    }

    // best top-left of set near (px, py) within r: coarse peaks, then fine match around each
    bool find(double px, double py, TplSet& set, double r, double lam, int dsc, double s, Found& res) {
        const int tw = set.raw.w, th = set.raw.h;
        const double ww = tw * s, hh = th * s;
        Plane area = feat(px - r, py - r, ww + 2 * r, hh + 2 * r);
        Plane rs;
        if (s != 1) rs = resample(area, (int)std::floor(area.w / s), (int)std::floor(area.h / s), 0, 0, s);
        const Plane& work = s == 1 ? area : rs;
        if (work.w < tw || work.h < th) return false;
        const double ex = (px - area.ox) / s, ey = (py - area.oy) / s;
        const Norm& small = set.at(dsc);
        const double cex = ex / dsc, cey = ey / dsc, clam = lam * dsc;
        Plane wd = downscale(work, dsc);
        std::vector<Peak> pk = peaks(wd, small, 3, [&](int x, int y, double sc) { return sc - clam * std::hypot(x - cex, y - cey); });
        std::vector<Peak> coarse;
        for (auto& c : pk)
            if (c.score >= 0.25) coarse.push_back(c);
        if (coarse.empty()) return false;
        // refine the leader, anything close to it, and the candidate nearest the prediction
        double top = -1e9;
        for (auto& c : coarse) top = std::max(top, (double)c.score);
        auto dist = [&](const Peak& c) { return std::hypot(c.x - cex, c.y - cey); };
        size_t nearest = 0;
        for (size_t i = 0; i < coarse.size(); ++i)
            if (coarse[i].score >= 0.4 && dist(coarse[i]) < dist(coarse[nearest])) nearest = i;
        const int m = dsc + 1;
        struct F { double x, y, score, adj; };
        std::vector<F> found;
        for (size_t i = 0; i < coarse.size(); ++i) {
            const Peak& c = coarse[i];
            if (!(i == 0 || i == nearest || c.score >= std::max(0.5, top - 0.15))) continue;
            int fx = std::max(0, c.x * dsc - m), fy = std::max(0, c.y * dsc - m);
            int fw = std::min(work.w - fx, tw + 2 * m), fh = std::min(work.h - fy, th + 2 * m);
            if (fw < tw || fh < th) continue;
            MatchR fine = match(cropPlane(work, fx, fy, fw, fh), set.full, ex - fx, ey - fy, lam, true);
            double adj = fine.score - lam * std::hypot(fx + fine.x - ex, fy + fine.y - ey);
            found.push_back({area.ox + (fx + fine.x) * s, area.oy + (fy + fine.y) * s, fine.score, adj});
        }
        if (found.empty()) return false;
        std::stable_sort(found.begin(), found.end(), [](const F& a, const F& b) { return a.adj > b.adj; });
        res.x = found[0].x;
        res.y = found[0].y;
        res.score = found[0].score;
        res.adj = found[0].adj;
        res.others.clear();
        for (size_t i = 1; i < found.size(); ++i)   // the same spot refined twice counts once
            if (std::fabs(found[i].x - found[0].x) > tw * s * 0.5 || std::fabs(found[i].y - found[0].y) > th * s * 0.5)
                res.others.push_back({found[i].x, found[i].y, found[i].score});
        return true;
    }

    // re-match a few px around (x, y) at scale s (cheap; for comparing sizes)
    SearchR refine(double x, double y, double s, TplSet& set, double lam) {
        const int m = ds + 2;
        const double ww = bw * s, hh = bh * s, ox = x - ww / 2 - m * s, oy = y - hh / 2 - m * s;
        Plane area = feat(ox - 1, oy - 1, ww + 2 * m * s + 3, hh + 2 * m * s + 3);
        Plane work = resample(area, bw + 2 * m, bh + 2 * m, ox - area.ox, oy - area.oy, s);
        MatchR fine = match(work, set.full, m, m, lam, true);
        SearchR r;
        r.cx = ox + fine.x * s + ww / 2;
        r.cy = oy + fine.y * s + hh / 2;
        r.score = fine.score;
        r.s = s;
        return r;
    }

    // try slightly larger / smaller around m; keep stepping while it improves; parabola for the final scale
    SearchR scaleSearch(const SearchR& m, double lam) {
        const double STEP = 1.06, base = m.s;
        std::map<int, std::pair<bool, SearchR>> tried;
        tried[0] = {true, m};
        auto at = [&](int k) -> const std::pair<bool, SearchR>& {
            auto it = tried.find(k);
            if (it == tried.end()) {
                double s = clampScale(base * std::pow(STEP, k));
                if (std::fabs(s / base - std::pow(STEP, k)) > 1e-6) it = tried.emplace(k, std::make_pair(false, SearchR{})).first;
                else it = tried.emplace(k, std::make_pair(true, refine(m.cx, m.cy, s, *tpl, lam))).first;
            }
            return it->second;
        };
        auto sc = [&](int k) { const auto& p = at(k); return p.first ? p.second.score : -2.0; };
        at(-1);
        at(1);
        int best = sc(1) > sc(-1) ? 1 : -1;
        if (sc(best) > sc(0) + 0.003) {
            const int sg = best > 0 ? 1 : -1;
            for (int i = 0; i < 2 && sc(best + sg) > sc(best); ++i) best += sg;
        } else {
            best = 0;   // no clear gain: keep the size (fixed-size targets must not breathe)
        }
        SearchR b = at(best).second;
        const double a = sc(best - 1), c = sc(best + 1), den = a - 2 * b.score + c;
        double off = 0;
        if (a > -2 && c > -2 && den < -1e-9) off = std::max(-0.5, std::min(0.5, (a - c) / (2 * den)));
        b.s = clampScale(b.s * std::pow(STEP, off));
        return b;
    }

    bool nearDistractor(double x, double y, double slack = 0) const {
        const double ww = w(), hh = h();
        for (auto& d : distractors)
            if (std::fabs(d.cx - x) <= ww * 0.5 + slack && std::fabs(d.cy - y) <= hh * 0.5 + slack) return true;
        return false;
    }
    void noteDistractors(const std::vector<Spot>& list, double tcx, double tcy) {
        const double ww = w(), hh = h();
        for (auto& d : distractors) d.age++;
        for (auto& o : list) {
            if (o.score < 0.6) continue;
            if (std::fabs(o.cx - tcx) <= ww * 0.5 && std::fabs(o.cy - tcy) <= hh * 0.5) continue;   // the target itself
            Distractor* hit = nullptr;
            for (auto& e : distractors)
                if (std::fabs(e.cx - o.cx) <= ww * 0.5 && std::fabs(e.cy - o.cy) <= hh * 0.5) { hit = &e; break; }
            if (hit) { hit->cx = o.cx; hit->cy = o.cy; hit->age = 0; }
            else distractors.push_back({o.cx, o.cy, 0});
        }
        const int keep = (int)jsRound(fps * 2);
        std::vector<Distractor> kept;
        for (auto& d : distractors)
            if (d.age <= keep) kept.push_back(d);
        if (kept.size() > 12) kept.erase(kept.begin(), kept.end() - 12);
        distractors.swap(kept);
    }
    // remember look-alikes inside the re-detection range
    void scanDistractors() {
        const double size = std::max(w(), h()), r = jsRound(size * 0.75) + 8;
        const double Rr = jsRound(size * 0.6 + 3 * maxCoast + r);
        SearchR q;
        if (search(cx, cy, tpl0, Rr, 0.0005, ds, scale, q)) {
            std::vector<Spot> l{{q.cx, q.cy, q.score}};
            l.insert(l.end(), q.others.begin(), q.others.end());
            noteDistractors(l, cx, cy);
        }
    }
    // strict re-detection after occlusion: within reach, not a known look-alike, no close rival
    bool redetect(double pcx, double pcy, double r, SearchR& out) {
        const double size = std::max(w(), h()), reach = size * 0.6 + 3 * coast, Rr = jsRound(reach + r);
        std::vector<double> scales{scale};
        if (!fixedSize) scales = {clampScale(scale), clampScale(scale * 1.25), clampScale(scale / 1.25)};
        std::vector<SearchR> all;
        std::vector<TplPtr> sets{tpl0};
        sets.insert(sets.end(), bank.begin(), bank.end());
        for (auto& set : sets)
            for (double s : scales) {
                SearchR q;
                if (!search(pcx, pcy, set, Rr, 0.001, ds, s, q)) continue;
                q.set = set;
                all.push_back(q);
                for (auto& o : q.others) {
                    SearchR e;
                    e.cx = o.cx;
                    e.cy = o.cy;
                    e.score = o.score;
                    e.s = s;
                    e.set = set;
                    all.push_back(e);
                }
            }
        std::vector<SearchR> ok;
        for (auto& q : all)
            if (q.score >= 0.65 && std::hypot(q.cx - pcx, q.cy - pcy) <= reach && !nearDistractor(q.cx, q.cy, 2.0 * coast)) ok.push_back(q);
        if (ok.empty()) return false;
        std::stable_sort(ok.begin(), ok.end(), [](const SearchR& a, const SearchR& b) { return a.score > b.score; });
        const SearchR& best = ok[0];
        for (auto& q : all)
            if (q.score > best.score - 0.08 && (std::fabs(q.cx - best.cx) > w() * 0.5 || std::fabs(q.cy - best.cy) > h() * 0.5)) return false;
        out = best;
        return true;
    }

    TrackState update(Box4& outRect, double& outScore) {
        const double pcx = cx + vx, pcy = cy + vy;
        const double ps = fixedSize ? scale : clampScale(scale * srate);   // size follows its recent rate too
        const double ww = bw * ps, hh = bh * ps, size = std::max(ww, hh);
        const double r = jsRound(size * 0.75) + 8;
        const double LAM = 0.004;   // penalty per px away from the prediction
        SearchR m;
        bool hasM = search(pcx, pcy, tpl, r, LAM, ds, ps, m);
        if (hasM) noteDistractors(m.others, m.cx, m.cy);
        if (hasM && !m.partial && !fixedSize && m.score >= 0.25 && inside(m.cx, m.cy, m.s)) {
            std::vector<Spot> others = m.others;
            m = scaleSearch(m, LAM);
            m.others = std::move(others);
        }
        // a hit further than 30% of the box (+ motion slack) from the prediction is something else
        auto nearP = [&](const SearchR& q) {
            return std::fabs(q.cx - pcx) <= ww * 0.3 + 2 + std::fabs(vx) * 0.5 && std::fabs(q.cy - pcy) <= hh * 0.3 + 2 + std::fabs(vy) * 0.5;
        };
        bool ok = hasM && nearP(m) && m.score >= (m.partial ? 0.5 : 0.35);
        if (hasM && !ok && m.score >= 0.6) noteDistractors({{m.cx, m.cy, m.score}}, pcx, pcy);
        bool found = false;
        // the current look failed: try the remembered looks at the same place
        if (!ok && !bank.empty()) {
            SearchR best;
            bool hb = false;
            std::vector<TplPtr> sets{tpl0};
            sets.insert(sets.end(), bank.begin(), bank.end());
            for (auto& set : sets) {
                if (set == anchor) continue;
                SearchR q;
                if (search(pcx, pcy, set, r, LAM, ds, ps, q) && nearP(q) && q.score >= 0.45 && (!hb || q.score > best.score)) {
                    best = q;
                    best.set = set;
                    hb = true;
                }
            }
            if (hb) {
                m = best;
                hasM = ok = true;
                setAnchor(best.set);
            }
        }
        // re-detected candidate: accept only after 3 consistent frames
        if (!ok && hasCand) {
            Cand& c = cand;
            const double ex = c.cx + c.vx, ey = c.cy + c.vy;
            SearchR q;
            if (search(ex, ey, c.set, jsRound(size * 0.4) + 4, LAM, ds, c.s, q) && q.score >= 0.65 && std::fabs(q.cx - ex) <= ww * 0.3 + 2 &&
                std::fabs(q.cy - ey) <= hh * 0.3 + 2 && !nearDistractor(q.cx, q.cy)) {
                c.vx = q.cx - c.cx;
                c.vy = q.cy - c.cy;
                c.cx = q.cx;
                c.cy = q.cy;
                if (++c.hits >= 3) {
                    m = q;
                    hasM = ok = found = true;
                    if (c.set != anchor) setAnchor(c.set);
                    hasCand = false;
                }
            } else {
                hasCand = false;
            }
        }
        if (!ok && !hasCand && coast > 0 && coast % 3 == 0 && coast <= maxCoast) {
            SearchR re;
            if (redetect(pcx, pcy, r, re)) {
                cand = {re.cx, re.cy, re.s, re.set, 0, 0, 1};
                hasCand = true;
            }
        }
        if (!ok) {
            if (++coast > maxCoast + (hasCand ? 3 : 0)) return TS_LOST;   // a candidate under check buys 3 frames
            // unseen: keep the size, slow down gently
            cx = pcx;
            cy = pcy;
            clampCenter();
            vx *= 0.97;
            vy *= 0.97;
            srate = 1;
            outRect = rect();
            outScore = 0;
            return TS_COAST;
        }
        coast = 0;
        hasCand = false;
        if (++sinceScan >= 15) {
            sinceScan = 0;
            scanDistractors();
        }
        if (found) {
            vx = 0;
            vy = 0;
        } else {
            vx = vx * 0.5 + (m.cx - cx) * 0.5;
            vy = vy * 0.5 + (m.cy - cy) * 0.5;
        }
        if (!fixedSize) {
            const double rate = found ? 1 : m.s / scale;
            srate = std::min(1.1, std::max(0.9, srate * 0.6 + rate * 0.4));
            if (found) srate = 1;
            scale = m.s;
        }
        cx = m.cx;
        cy = m.cy;
        clampCenter();
        if (m.score > 0.7 && inside(cx, cy, scale)) {
            // adapt slowly (light / background changes), only while it still resembles the anchor look
            Plane p = patch(cx, cy, scale);
            if (ncc(p, anchor->full) > 0.5) {
                for (size_t i = 0; i < p.g.size(); ++i) rawA[i] = (float)(rawA[i] * 0.85 + p.g[i] * 0.15);
                rebuild();
            }
        }
        outRect = rect();
        outScore = m.score;
        return TS_OK;
    }
};

TemplateTracker::TemplateTracker(const Image& frame, const Box4& rect, bool fixedSize) : d_(std::make_unique<Impl>()) {
    TrackerOptions o;
    o.fixedSize = fixedSize;
    d_->init(frame, rect, o);
}

TemplateTracker::TemplateTracker(const Image& frame, const Box4& rect, const TrackerOptions& opt) : d_(std::make_unique<Impl>()) {
    d_->init(frame, rect, opt);
}

TemplateTracker::~TemplateTracker() = default;
TemplateTracker::TemplateTracker(TemplateTracker&&) noexcept = default;
TemplateTracker& TemplateTracker::operator=(TemplateTracker&&) noexcept = default;

int TemplateTracker::channel() const { return d_->channel; }

TrackState TemplateTracker::track(const Image& frame, Box4& out, double& score) {
    if (frame.w <= 0 || frame.h <= 0) return TS_LOST;
    d_->src = &frame;
    Box4 r;
    double sc = 0;
    TrackState st = d_->update(r, sc);
    d_->src = nullptr;
    if (st == TS_LOST) return st;
    for (int i = 0; i < 4; ++i) out[i] = jsRound(r[i] * 100) / 100;   // app.js round2
    score = jsRound(sc * 100) / 100;
    return st;
}

bool TemplateTracker::update(const Image& frame, Box4& out, double& score) { return track(frame, out, score) != TS_LOST; }

// app.js bankAdd / bankOf: small looks of the target at manual keys
void trackBankAdd(ClipMask& m, int f, const Image& frame, const Box4& rect) {
    const int x0 = iround(rect[0]), y0 = iround(rect[1]), w = iround(rect[2]), h = iround(rect[3]);
    if (w < 4 || h < 4 || x0 < 0 || y0 < 0 || x0 + w > frame.w || y0 + h > frame.h) return;   // fully visible only
    const double k = std::min(1.0, 48.0 / std::max(w, h));
    const int bw = std::max(4, iround(w * k)), bh = std::max(4, iround(h * k));
    auto img = std::make_shared<Image>(resampleHQ(cropImage(frame, x0, y0, x0 + w, y0 + h), bw, bh));
    std::vector<ClipMask::BankEntry> bank;
    for (auto& e : m.trackBank)
        if (e.f != f) bank.push_back(e);
    bank.push_back({f, img});
    if (bank.size() > 6) {   // the first one + the latest ones
        std::vector<ClipMask::BankEntry> keep{bank[0]};
        keep.insert(keep.end(), bank.end() - 5, bank.end());
        bank.swap(keep);
    }
    m.trackBank = std::move(bank);
}

std::vector<ImagePtr> trackBankOf(const ClipMask& m, int exceptFrame) {
    std::vector<ImagePtr> out;
    for (auto& e : m.trackBank)
        if (e.f != exceptFrame && e.img) out.push_back(e.img);
    return out;
}

// ---------------------------------------------------------------- JSON

namespace {

std::string toUtf8(const std::wstring& w) {
    std::string o;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = (uint16_t)w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < w.size() && (uint16_t)w[i + 1] >= 0xDC00 && (uint16_t)w[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint16_t)w[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else {
            o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63));
            o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63));
        }
    }
    return o;
}

std::wstring fromUtf8(const std::string& s) {
    std::wstring o;
    size_t i = 0, n = s.size();
    while (i < n) {
        uint8_t b = (uint8_t)s[i];
        uint32_t c;
        int extra;
        if (b < 0x80) { c = b; extra = 0; }
        else if ((b & 0xE0) == 0xC0) { c = b & 31; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { c = b & 15; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { c = b & 7; extra = 3; }
        else { o += (wchar_t)0xFFFD; ++i; continue; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= n || ((uint8_t)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
            c = (c << 6) | ((uint8_t)s[i + k] & 63);
        }
        if (!ok) { o += (wchar_t)0xFFFD; ++i; continue; }
        i += extra + 1;
        if (c >= 0x10000 && c <= 0x10FFFF) {
            c -= 0x10000;
            o += (wchar_t)(0xD800 + (c >> 10));
            o += (wchar_t)(0xDC00 + (c & 1023));
        } else if (c > 0x10FFFF) {
            o += (wchar_t)0xFFFD;
        } else {
            o += (wchar_t)c;
        }
    }
    return o;
}

// ---- writer
void putNum(std::string& o, double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v);   // shortest round-trip form
    o.append(buf, r.ptr);
}
void putInt(std::string& o, long long v) { o += std::to_string(v); }
void putStr(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += (char)c;
    }
    o += '"';
}
void putBox(std::string& o, const Box4& b) {
    o += '[';
    for (int i = 0; i < 4; ++i) {
        if (i) o += ',';
        putNum(o, b[i]);
    }
    o += ']';
}
void putBoxMap(std::string& o, const std::map<int, Box4>& m) {
    o += '{';
    bool first = true;
    for (auto& kv : m) {
        if (!first) o += ',';
        first = false;
        o += '"';
        putInt(o, kv.first);
        o += "\":";
        putBox(o, kv.second);
    }
    o += '}';
}
void putNumMap(std::string& o, const std::map<int, double>& m) {
    o += '{';
    bool first = true;
    for (auto& kv : m) {
        if (!first) o += ',';
        first = false;
        o += '"';
        putInt(o, kv.first);
        o += "\":";
        putNum(o, kv.second);
    }
    o += '}';
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64(const std::vector<uint8_t>& d) {
    std::string o;
    for (size_t i = 0; i < d.size(); i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < d.size() ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0);
        o += kB64[v >> 18];
        o += kB64[(v >> 12) & 63];
        o += i + 1 < d.size() ? kB64[(v >> 6) & 63] : '=';
        o += i + 2 < d.size() ? kB64[v & 63] : '=';
    }
    return o;
}
std::vector<uint8_t> unb64(const std::string& s) {
    std::vector<uint8_t> o;
    uint32_t v = 0;
    int n = 0;
    for (char c : s) {
        const char* p = c ? std::strchr(kB64, c) : nullptr;
        if (!p) continue;   // '=' and whitespace
        v = v << 6 | (uint32_t)(p - kB64);
        if (++n == 4) {
            o.push_back((uint8_t)(v >> 16)); o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v);
            v = 0;
            n = 0;
        }
    }
    if (n == 2) o.push_back((uint8_t)(v >> 4));
    if (n == 3) { o.push_back((uint8_t)(v >> 10)); o.push_back((uint8_t)(v >> 2)); }
    return o;
}

const char* kEffectNames[CE_COUNT] = {"mosaic", "blur", "box", "zoom", "portal"};

// ---- reader
struct JV {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<JV> a;
    std::vector<std::pair<std::string, JV>> o;
    const JV* get(const char* k) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct Parser {
    const char* p;
    const char* e;
    int depth = 0;
    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool lit(const char* w) {
        size_t n = std::strlen(w);
        if ((size_t)(e - p) < n || std::memcmp(p, w, n) != 0) return false;
        p += n;
        return true;
    }
    bool hex4(uint32_t& v) {
        if (e - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        p += 4;
        return true;
    }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        std::wstring pendingW;   // \u escapes are gathered as UTF-16 then flushed as UTF-8
        auto flush = [&] { if (!pendingW.empty()) { out += toUtf8(pendingW); pendingW.clear(); } };
        while (p < e && *p != '"') {
            if (*p == '\\') {
                if (++p >= e) return false;
                char c = *p++;
                if (c == 'u') {
                    uint32_t v;
                    if (!hex4(v)) return false;
                    pendingW += (wchar_t)v;
                    continue;
                }
                flush();
                switch (c) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                default: out += c; break;   // " \ /
                }
            } else {
                flush();
                out += *p++;
            }
        }
        flush();
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool value(JV& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= e) return false;
        bool ok = true;
        if (*p == '{') {
            v.t = JV::Obj;
            ++p;
            ws();
            if (p < e && *p == '}') ++p;
            else
                for (;;) {
                    ws();
                    std::pair<std::string, JV> kv;
                    if (!str(kv.first)) { ok = false; break; }
                    ws();
                    if (p >= e || *p != ':') { ok = false; break; }
                    ++p;
                    if (!value(kv.second)) { ok = false; break; }
                    v.o.push_back(std::move(kv));
                    ws();
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == '}') { ++p; break; }
                    ok = false;
                    break;
                }
        } else if (*p == '[') {
            v.t = JV::Arr;
            ++p;
            ws();
            if (p < e && *p == ']') ++p;
            else
                for (;;) {
                    JV x;
                    if (!value(x)) { ok = false; break; }
                    v.a.push_back(std::move(x));
                    ws();
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == ']') { ++p; break; }
                    ok = false;
                    break;
                }
        } else if (*p == '"') {
            v.t = JV::Str;
            ok = str(v.s);
        } else if (lit("true")) {
            v.t = JV::Bool; v.b = true;
        } else if (lit("false")) {
            v.t = JV::Bool; v.b = false;
        } else if (lit("null")) {
            v.t = JV::Null;
        } else {
            v.t = JV::Num;
            const char* q = p;
            if (q < e && *q == '+') ++q;   // from_chars rejects a leading '+'
            auto r = std::from_chars(q, e, v.n);
            ok = r.ec == std::errc();
            p = r.ptr;
        }
        --depth;
        return ok;
    }
};

bool asNum(const JV* v, double& out) {
    if (!v) return false;
    if (v->t == JV::Num) { out = v->n; return true; }
    if (v->t == JV::Bool) { out = v->b ? 1 : 0; return true; }
    return false;
}
void readD(const JV& o, const char* k, double& dst) { double v; if (asNum(o.get(k), v)) dst = v; }
void readI(const JV& o, const char* k, int& dst) {
    double v;
    if (asNum(o.get(k), v)) dst = (int)std::clamp(jsRound(v), -2147483648.0, 2147483647.0);
}
void readB(const JV& o, const char* k, bool& dst) {
    const JV* v = o.get(k);
    if (v && v->t == JV::Bool) dst = v->b;
    else if (v && v->t == JV::Num) dst = v->n != 0;
}
bool keyInt(const std::string& s, int& k) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), k);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}
bool readBox(const JV& v, Box4& b) {
    if (v.t != JV::Arr || v.a.size() < 4) return false;
    for (int i = 0; i < 4; ++i)
        if (!asNum(&v.a[i], b[i])) return false;
    return true;
}
void readBoxMap(const JV& o, const char* k, std::map<int, Box4>& dst) {
    const JV* v = o.get(k);
    if (!v || v->t != JV::Obj) return;
    dst.clear();
    for (auto& kv : v->o) {
        int f;
        Box4 b;
        if (keyInt(kv.first, f) && readBox(kv.second, b)) dst[f] = b;
    }
}
void readNumMap(const JV& o, const char* k, std::map<int, double>& dst) {
    const JV* v = o.get(k);
    if (!v || v->t != JV::Obj) return;
    dst.clear();
    for (auto& kv : v->o) {
        int f;
        double d;
        if (keyInt(kv.first, f) && asNum(&kv.second, d)) dst[f] = d;
    }
}

} // namespace

std::string clipToJson(const ClipProject& p) {
    std::string o;
    o += "{\"version\":1,\"inFrame\":";
    putInt(o, p.inFrame);
    o += ",\"outFrame\":";
    putInt(o, p.outFrame);
    o += ",\"hasCrop\":";
    o += p.hasCrop ? "true" : "false";
    o += ",\"crop\":";
    putBox(o, p.crop);
    o += ",\"export\":{\"fps\":";
    putNum(o, p.ex.fps);
    o += ",\"speed\":";
    putNum(o, p.ex.speed);
    o += ",\"width\":";
    putInt(o, p.ex.width);
    o += ",\"quality\":";
    putInt(o, p.ex.quality);
    o += ",\"loop\":";
    putInt(o, p.ex.loop);
    o += ",\"targetMB\":";
    putNum(o, p.ex.targetMB);
    o += "},\"freezes\":";
    putNumMap(o, p.freezes);
    o += ",\"segments\":[";
    for (size_t i = 0; i < p.segments.size(); ++i) {
        if (i) o += ',';
        o += '[';
        putInt(o, p.segments[i].lo);
        o += ',';
        putInt(o, p.segments[i].hi);
        o += ']';
    }
    o += "],\"slowmos\":[";
    for (size_t i = 0; i < p.slowmos.size(); ++i) {
        if (i) o += ',';
        o += '[';
        putInt(o, p.slowmos[i].lo);
        o += ',';
        putInt(o, p.slowmos[i].hi);
        o += ',';
        putNum(o, p.slowmos[i].rate);
        o += ']';
    }
    o += "],\"masks\":[";
    for (size_t i = 0; i < p.masks.size(); ++i) {
        const ClipMask& m = p.masks[i];
        if (i) o += ',';
        o += "{\"name\":";
        putStr(o, toUtf8(m.name));
        o += ",\"effect\":";
        if (m.effect >= 0 && m.effect < CE_COUNT) putStr(o, kEffectNames[m.effect]);
        else putInt(o, m.effect);
        o += ",\"strength\":";
        putInt(o, m.strength);
        o += ",\"color\":";
        putInt(o, (long long)m.color);
        o += ",\"zoom\":";
        putNum(o, m.zoom);
        o += ",\"border\":";
        putInt(o, m.border);
        o += ",\"shape\":";
        putStr(o, m.shape == CS_ELLIPSE ? "ellipse" : "rect");
        o += ",\"pad\":";
        putInt(o, m.pad);
        o += ",\"start\":";
        putInt(o, m.start);
        o += ",\"end\":";
        putInt(o, m.end);
        o += ",\"enabled\":";
        o += m.enabled ? "true" : "false";
        o += ",\"smooth\":";
        o += m.smooth ? "true" : "false";
        o += ",\"fixedSize\":";
        o += m.fixedSize ? "true" : "false";
        o += ",\"keys\":";
        putBoxMap(o, m.keys);
        o += ",\"destKeys\":";
        putBoxMap(o, m.destKeys);
        o += ",\"trackScore\":";
        putNumMap(o, m.trackScore);
        o += ",\"lost\":[";
        for (size_t k = 0; k < m.lost.size(); ++k) {
            if (k) o += ',';
            putInt(o, m.lost[k]);
        }
        o += "],\"trackBank\":[";   // RGB bytes, base64 (as the web app stores them)
        bool firstB = true;
        for (auto& e : m.trackBank) {
            if (!e.img) continue;
            if (!firstB) o += ',';
            firstB = false;
            std::vector<uint8_t> rgbBytes;
            rgbBytes.reserve(e.img->px.size() * 3);
            for (uint32_t c : e.img->px) { rgbBytes.push_back((uint8_t)R(c)); rgbBytes.push_back((uint8_t)G(c)); rgbBytes.push_back((uint8_t)B(c)); }
            o += "{\"f\":";
            putInt(o, e.f);
            o += ",\"w\":";
            putInt(o, e.img->w);
            o += ",\"h\":";
            putInt(o, e.img->h);
            o += ",\"data\":";
            putStr(o, b64(rgbBytes));
            o += '}';
        }
        o += "]}";
    }
    o += "]}";
    return o;
}

bool clipFromJson(const std::string& s, ClipProject& p) {
    Parser ps{s.data(), s.data() + s.size()};
    if (s.size() >= 3 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) ps.p += 3;   // BOM
    JV root;
    if (!ps.value(root) || root.t != JV::Obj) return false;
    ClipProject q = p;   // missing keys keep the caller's values
    readI(root, "inFrame", q.inFrame);
    readI(root, "outFrame", q.outFrame);
    readB(root, "hasCrop", q.hasCrop);
    if (const JV* c = root.get("crop")) {
        Box4 b;
        if (readBox(*c, b)) q.crop = b;
        else if (c->t == JV::Null) q.hasCrop = false;
    }
    if (const JV* ex = root.get("export"); ex && ex->t == JV::Obj) {
        readD(*ex, "fps", q.ex.fps);
        readD(*ex, "speed", q.ex.speed);
        readI(*ex, "width", q.ex.width);
        readI(*ex, "quality", q.ex.quality);
        readI(*ex, "loop", q.ex.loop);
        readD(*ex, "targetMB", q.ex.targetMB);
    }
    readNumMap(root, "freezes", q.freezes);
    if (const JV* a = root.get("segments"); a && a->t == JV::Arr) {
        q.segments.clear();
        for (auto& e : a->a) {
            double lo, hi;
            if (e.t == JV::Arr && e.a.size() >= 2 && asNum(&e.a[0], lo) && asNum(&e.a[1], hi))
                q.segments.push_back({(int)jsRound(lo), (int)jsRound(hi)});
        }
    }
    if (const JV* a = root.get("slowmos"); a && a->t == JV::Arr) {
        q.slowmos.clear();
        for (auto& e : a->a) {
            double lo, hi, rate;
            if (e.t == JV::Arr && e.a.size() >= 3 && asNum(&e.a[0], lo) && asNum(&e.a[1], hi) && asNum(&e.a[2], rate))
                q.slowmos.push_back({(int)jsRound(lo), (int)jsRound(hi), rate});
        }
    }
    if (const JV* a = root.get("masks"); a && a->t == JV::Arr) {
        q.masks.clear();
        for (auto& e : a->a) {
            if (e.t != JV::Obj) continue;
            ClipMask m;
            if (const JV* n = e.get("name"); n && n->t == JV::Str) m.name = fromUtf8(n->s);
            if (const JV* ef = e.get("effect")) {
                if (ef->t == JV::Str) {
                    for (int k = 0; k < CE_COUNT; ++k)
                        if (ef->s == kEffectNames[k]) m.effect = k;
                    if (ef->s == "solid") m.effect = CE_BOX;
                } else {
                    readI(e, "effect", m.effect);
                }
            }
            readI(e, "strength", m.strength);
            double col;
            if (asNum(e.get("color"), col)) m.color = (uint32_t)(long long)col;
            readD(e, "zoom", m.zoom);
            readI(e, "border", m.border);
            if (const JV* sh = e.get("shape")) {
                if (sh->t == JV::Str) m.shape = sh->s == "ellipse" ? CS_ELLIPSE : CS_RECT;
                else readI(e, "shape", m.shape);
            }
            readI(e, "pad", m.pad);
            readI(e, "start", m.start);
            readI(e, "end", m.end);
            readB(e, "enabled", m.enabled);
            readB(e, "smooth", m.smooth);
            readB(e, "fixedSize", m.fixedSize);
            readBoxMap(e, "keys", m.keys);
            readBoxMap(e, "destKeys", m.destKeys);
            readNumMap(e, "trackScore", m.trackScore);
            if (const JV* l = e.get("lost"); l && l->t == JV::Arr)
                for (auto& x : l->a) {
                    double d;
                    if (asNum(&x, d)) m.lost.push_back((int)jsRound(d));
                }
            if (const JV* tb = e.get("trackBank"); tb && tb->t == JV::Arr)
                for (auto& x : tb->a) {
                    int f = 0, w = 0, h = 0;
                    const JV* data = x.get("data");
                    readI(x, "f", f);
                    readI(x, "w", w);
                    readI(x, "h", h);
                    if (!data || data->t != JV::Str || w < 1 || h < 1 || w > 4096 || h > 4096) continue;
                    std::vector<uint8_t> d = unb64(data->s);
                    if (d.size() < (size_t)w * h * 3) continue;
                    auto img = std::make_shared<Image>(w, h);
                    for (size_t i = 0; i < img->px.size(); ++i) img->px[i] = rgb(d[i * 3], d[i * 3 + 1], d[i * 3 + 2]);
                    m.trackBank.push_back({f, img});
                }
            q.masks.push_back(std::move(m));
        }
    }
    p = std::move(q);
    return true;
}

} // namespace bg
