#include "core/Edit.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <set>
#include <thread>

namespace bg {

const int kFpsChoices[11] = {1, 5, 10, 15, 20, 24, 25, 30, 33, 50, 60};

int64_t fpsToUs(int fps) { return 1000000 / std::max(fps, 1); }
int64_t secondsToUs(double s) { return std::max<int64_t>((int64_t)std::llround(s * 1e6), kMinFrameUs); }

void parallelFor(int n, const std::function<void(int)>& fn) {
    int nt = std::max(1, std::min<int>(n, (int)std::thread::hardware_concurrency()));
    if (nt <= 1) { for (int i = 0; i < n; ++i) fn(i); return; }
    std::atomic<int> next{0};
    std::vector<std::thread> th;
    for (int t = 0; t < nt; ++t)
        th.emplace_back([&] { for (int i; (i = next++) < n;) fn(i); });
    for (auto& t : th) t.join();
}

std::vector<int> targets(size_t n, const std::vector<int>& sel) {
    std::set<int> s(sel.begin(), sel.end());
    if (s.size() >= 2) return {s.begin(), s.end()};
    std::vector<int> all(n);
    for (size_t i = 0; i < n; ++i) all[i] = (int)i;
    return all;
}

Frames setTime(const Frames& f, int64_t us, const std::vector<int>& sel) {
    Frames o = f;
    for (int i : targets(f.size(), sel)) o[i].dur = us;
    return o;
}

Frames changeSpeed(const Frames& f, double k) {
    Frames o = f;
    for (auto& x : o) x.dur = std::max<int64_t>(1, (int64_t)(x.dur * k));
    return o;
}

Frames reverse(const Frames& f) { return Frames(f.rbegin(), f.rend()); }

Frames yoyo(const Frames& f) {
    if (f.empty()) return f;
    Frames o = f;
    Frames back(f.rbegin(), f.rend());
    back[0].dur = std::max<int64_t>(1, back[0].dur / 2);
    o.insert(o.end(), back.begin(), back.end());
    return o;
}

Frames trim(const Frames& f, int a, int b) {
    a = std::max(0, a);
    b = std::min((int)f.size() - 1, b);
    if (a > b) return f;
    return Frames(f.begin() + a, f.begin() + b + 1);
}

Frames deleteFrames(const Frames& f, const std::vector<int>& idx) {
    std::set<int> rm(idx.begin(), idx.end());
    Frames o;
    for (size_t i = 0; i < f.size(); ++i)
        if (!rm.count((int)i)) o.push_back(f[i]);
    if (o.empty() && !f.empty()) o.push_back(f[0]);
    return o;
}

Frames reduceFrames(const Frames& f, ReduceMode m, int nth, bool keep, const std::vector<int>& sel) {
    auto t = targets(f.size(), sel);
    std::vector<int64_t> d(f.size());
    for (size_t i = 0; i < f.size(); ++i) d[i] = f[i].dur;
    std::vector<char> rm(f.size(), 0);
    if (m == ReduceMode::Even || m == ReduceMode::Odd) {
        for (size_t p = 0; p + 1 < t.size(); p += 2) {
            int a = t[p], b = t[p + 1];
            int gone = m == ReduceMode::Even ? b : a, kp = m == ReduceMode::Even ? a : b;
            rm[gone] = 1;
            if (keep) d[kp] += d[gone];
        }
    } else {
        int n = std::max(2, nth);
        for (size_t k = n - 1; k < t.size(); k += n) {
            int gone = t[k];
            rm[gone] = 1;
            if (keep) {
                int64_t q = d[gone] / (n - 1), r = d[gone] % (n - 1);
                for (int j = 0; j < n - 1; ++j) d[t[k - (n - 1) + j]] += q + (j < r ? 1 : 0);
            }
        }
    }
    Frames o;
    for (size_t i = 0; i < f.size(); ++i)
        if (!rm[i]) o.push_back({f[i].img, d[i]});
    return o;
}

bool framesSimilar(const Image& a, const Image& b, int n, int tol) {
    if (a.w != b.w || a.h != b.h) return false;
    if (n <= 0) return a.px == b.px;
    size_t limit = (size_t)(n / 100.0 * a.w * a.h), diff = 0;
    for (size_t i = 0; i < a.px.size(); ++i) {
        uint32_t x = a.px[i], y = b.px[i];
        if (x == y) continue;
        int dr = std::abs(R(x) - R(y)), dg = std::abs(G(x) - G(y)), db = std::abs(B(x) - B(y));
        if (dr > tol || dg > tol || db > tol || dr + dg + db > 2 * tol)
            if (++diff > limit) return false;
    }
    return true;
}

Frames removeDuplicates(const Frames& f, int pct, int tol, bool keep, int64_t limitUs, const std::vector<int>& sel) {
    if (tol < 0) tol = std::min(pct, 40);
    auto tv = targets(f.size(), sel);
    std::set<int> t(tv.begin(), tv.end());
    Frames o;
    int64_t merged = 0;
    for (size_t i = 0; i < f.size(); ++i) {
        if (t.count((int)i) && !o.empty() && (limitUs <= 0 || merged + f[i].dur <= limitUs) &&
            framesSimilar(*o.back().img, *f[i].img, pct, tol)) {
            merged += f[i].dur;
            if (keep) o.back().dur += f[i].dur;
            continue;
        }
        o.push_back(f[i]);
        merged = f[i].dur;
    }
    return o;
}

Frames videoSummary(const Frames& f, int n, bool scale, int adj, bool limit, int64_t limitUs,
                    const std::vector<int>& sel, const Progress& prog) {
    int tol = std::min(n, 40);
    auto tv = targets(f.size(), sel);
    std::set<int> t(tv.begin(), tv.end());
    Frames o;
    std::vector<int64_t> merged;
    for (size_t i = 0; i < f.size(); ++i) {
        if (prog && i % 16 == 0) prog((double)i / f.size());
        if (t.count((int)i) && !o.empty() && framesSimilar(*o.back().img, *f[i].img, n, tol)) {
            merged.back() += f[i].dur;
            continue;
        }
        o.push_back(f[i]);
        merged.push_back(f[i].dur);
    }
    for (size_t i = 0; i < o.size(); ++i) {
        int64_t d = o[i].dur;
        if (scale) d = std::max(d, merged[i] * adj / 100);
        if (limit) d = std::min(d, limitUs);
        o[i].dur = d;
    }
    return o;
}

Frames crop(const Frames& f, int x, int y, int w, int h) {
    if (f.empty()) return f;
    const Image& s0 = *f[0].img;
    x = std::clamp(x, 0, s0.w - 1); y = std::clamp(y, 0, s0.h - 1);
    w = std::clamp(w, 1, s0.w - x); h = std::clamp(h, 1, s0.h - y);
    return mapImages(f, [=](const Image& s) {
        auto o = std::make_shared<Image>(w, h);
        for (int r = 0; r < h; ++r) std::copy_n(s.row(y + r) + x, w, o->row(r));
        return ImagePtr(o);
    }, {});
}

// Box-filter downscale / bilinear upscale, separable.
ImagePtr resizeImage(const Image& s, int w, int h) {
    auto o = std::make_shared<Image>(w, h);
    std::vector<float> tmp((size_t)w * s.h * 3);
    // horizontal pass
    double sx = (double)s.w / w;
    for (int y = 0; y < s.h; ++y) {
        const uint32_t* row = s.row(y);
        for (int x = 0; x < w; ++x) {
            float acc[3] = {0, 0, 0};
            if (sx > 1.0) {
                double a = x * sx, b = a + sx, wt = 0;
                for (int i = (int)a; i < (int)std::ceil(b) && i < s.w; ++i) {
                    double c = std::min<double>(b, i + 1) - std::max<double>(a, i);
                    acc[0] += (float)(R(row[i]) * c); acc[1] += (float)(G(row[i]) * c); acc[2] += (float)(B(row[i]) * c);
                    wt += c;
                }
                for (auto& v : acc) v /= (float)wt;
            } else {
                double fx = (x + 0.5) * sx - 0.5;
                int i0 = std::clamp((int)std::floor(fx), 0, s.w - 1), i1 = std::min(i0 + 1, s.w - 1);
                float t = (float)std::clamp(fx - i0, 0.0, 1.0);
                acc[0] = R(row[i0]) * (1 - t) + R(row[i1]) * t;
                acc[1] = G(row[i0]) * (1 - t) + G(row[i1]) * t;
                acc[2] = B(row[i0]) * (1 - t) + B(row[i1]) * t;
            }
            float* d = &tmp[((size_t)y * w + x) * 3];
            d[0] = acc[0]; d[1] = acc[1]; d[2] = acc[2];
        }
    }
    double sy = (double)s.h / h;
    for (int y = 0; y < h; ++y) {
        uint32_t* orow = o->row(y);
        for (int x = 0; x < w; ++x) {
            float acc[3] = {0, 0, 0};
            if (sy > 1.0) {
                double a = y * sy, b = a + sy, wt = 0;
                for (int i = (int)a; i < (int)std::ceil(b) && i < s.h; ++i) {
                    double c = std::min<double>(b, i + 1) - std::max<double>(a, i);
                    const float* p = &tmp[((size_t)i * w + x) * 3];
                    acc[0] += (float)(p[0] * c); acc[1] += (float)(p[1] * c); acc[2] += (float)(p[2] * c);
                    wt += c;
                }
                for (auto& v : acc) v /= (float)wt;
            } else {
                double fy = (y + 0.5) * sy - 0.5;
                int i0 = std::clamp((int)std::floor(fy), 0, s.h - 1), i1 = std::min(i0 + 1, s.h - 1);
                float t = (float)std::clamp(fy - i0, 0.0, 1.0);
                const float* p0 = &tmp[((size_t)i0 * w + x) * 3];
                const float* p1 = &tmp[((size_t)i1 * w + x) * 3];
                for (int c = 0; c < 3; ++c) acc[c] = p0[c] * (1 - t) + p1[c] * t;
            }
            orow[x] = rgb(clamp255((int)(acc[0] + 0.5f)), clamp255((int)(acc[1] + 0.5f)), clamp255((int)(acc[2] + 0.5f)));
        }
    }
    return o;
}

Frames resize(const Frames& f, int w, int h, const Progress& prog) {
    w = std::clamp(w, 8, 5000); h = std::clamp(h, 8, 5000);
    return mapImages(f, [=](const Image& s) { return resizeImage(s, w, h); }, {}, prog);
}

Frames rotate(const Frames& f, Rot r) {
    return mapImages(f, [r](const Image& s) {
        bool swap = r == Rot::CW || r == Rot::CCW;
        auto o = std::make_shared<Image>(swap ? s.h : s.w, swap ? s.w : s.h);
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                int nx = x, ny = y;
                switch (r) {
                case Rot::CW: nx = s.h - 1 - y; ny = x; break;
                case Rot::CCW: nx = y; ny = s.w - 1 - x; break;
                case Rot::R180: nx = s.w - 1 - x; ny = s.h - 1 - y; break;
                case Rot::HFlip: nx = s.w - 1 - x; break;
                case Rot::VFlip: ny = s.h - 1 - y; break;
                }
                o->row(ny)[nx] = s.row(y)[x];
            }
        return ImagePtr(o);
    }, {});
}

Frames crossfade(const Frames& f, int at, int count, int64_t total) {
    if (at < 0 || at + 1 >= (int)f.size()) return f;
    const Image &a = *f[at].img, &b = *f[at + 1].img;
    if (a.w != b.w || a.h != b.h) return f;
    Frames o(f.begin(), f.begin() + at + 1);
    int64_t step = std::max<int64_t>(1, total / count);
    for (int k = 1; k <= count; ++k) {
        float t = (float)k / (count + 1);
        auto m = std::make_shared<Image>(a.w, a.h);
        for (size_t i = 0; i < a.px.size(); ++i) {
            uint32_t p = a.px[i], q = b.px[i];
            m->px[i] = rgb((int)(R(p) + (R(q) - R(p)) * t), (int)(G(p) + (G(q) - G(p)) * t), (int)(B(p) + (B(q) - B(p)) * t));
        }
        o.push_back({m, step});
    }
    o.insert(o.end(), f.begin() + at + 1, f.end());
    return o;
}

Frames mapImages(const Frames& f, const std::function<ImagePtr(const Image&)>& fn, const std::vector<int>& sel,
                 const Progress& prog) {
    Frames o = f;
    std::vector<int> idx;
    if (sel.empty()) for (size_t i = 0; i < f.size(); ++i) idx.push_back((int)i);
    else idx = sel;
    std::atomic<int> done{0};
    parallelFor((int)idx.size(), [&](int k) {
        o[idx[k]].img = fn(*f[idx[k]].img);
        int d = ++done;
        if (prog && d % 4 == 0) prog((double)d / idx.size());
    });
    return o;
}

bool Project::apply(Frames nf) {
    if (nf.empty()) return false;
    undo_.push_back(std::move(frames));
    if (undo_.size() > 30) undo_.erase(undo_.begin());
    redo_.clear();
    frames = std::move(nf);
    return true;
}
bool Project::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(frames));
    frames = std::move(undo_.back());
    undo_.pop_back();
    return true;
}
bool Project::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(frames));
    frames = std::move(redo_.back());
    redo_.pop_back();
    return true;
}
int64_t Project::totalUs() const {
    int64_t t = 0;
    for (auto& f : frames) t += f.dur;
    return t;
}
size_t Project::memoryBytes() const {
    std::set<const Image*> seen;
    size_t t = 0;
    for (auto& f : frames)
        if (seen.insert(f.img.get()).second) t += f.img->bytes();
    return t;
}

} // namespace bg
