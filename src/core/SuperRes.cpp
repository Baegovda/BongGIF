#include "core/SuperRes.h"

#include "core/Edit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace bg {

namespace {

// ---------------------------------------------------------------- minimal protobuf reader
struct Pb {
    const uint8_t* p;
    const uint8_t* e;
    bool next(int& field, int& wire, uint64_t& v, const uint8_t*& data, size_t& len) {
        if (p >= e) return false;
        uint64_t key = varint();
        field = (int)(key >> 3);
        wire = (int)(key & 7);
        data = nullptr;
        len = 0;
        switch (wire) {
        case 0: v = varint(); break;
        case 1: data = p; len = 8; p += 8; break;
        case 5: data = p; len = 4; p += 4; break;
        case 2: len = (size_t)varint(); data = p; p += len; break;
        default: p = e; return false;
        }
        return p <= e;
    }
    uint64_t varint() {
        uint64_t r = 0;
        int s = 0;
        while (p < e) {
            uint8_t c = *p++;
            r |= (uint64_t)(c & 0x7F) << s;
            s += 7;
            if (c < 0x80) break;
        }
        return r;
    }
};

struct Tensor {   // NHWC with N = 1, or a plain constant (shape from TensorProto)
    int h = 1, w = 1, c = 1;
    std::vector<float> v;
    std::vector<int64_t> shape;
};

struct Node {
    std::string name, op;
    std::vector<std::string> inputs;
    Tensor value;        // Const
    int blockSize = 0;   // DepthToSpace
    bool same = true;    // Conv2D padding
};

Tensor parseTensor(const uint8_t* d, size_t n) {
    Tensor t;
    Pb pb{d, d + n};
    int f, w;
    uint64_t v;
    const uint8_t* data;
    size_t len;
    std::vector<float> floats;
    while (pb.next(f, w, v, data, len)) {
        if (f == 2 && w == 2) {   // tensor_shape { dim { size } }
            Pb s{data, data + len};
            int f2, w2;
            uint64_t v2;
            const uint8_t* d2;
            size_t l2;
            while (s.next(f2, w2, v2, d2, l2))
                if (f2 == 2 && w2 == 2) {
                    Pb dm{d2, d2 + l2};
                    int f3, w3;
                    uint64_t v3;
                    const uint8_t* d3;
                    size_t l3;
                    while (dm.next(f3, w3, v3, d3, l3)) if (f3 == 1 && w3 == 0) t.shape.push_back((int64_t)v3);
                }
        } else if (f == 4 && w == 2) {   // tensor_content (raw little-endian floats)
            t.v.resize(len / 4);
            memcpy(t.v.data(), data, (len / 4) * 4);
        } else if (f == 5) {             // float_val (packed or single)
            if (w == 2) for (size_t i = 0; i + 4 <= len; i += 4) { float x; memcpy(&x, data + i, 4); floats.push_back(x); }
            else if (w == 5) { float x; memcpy(&x, data, 4); floats.push_back(x); }
        } else if (f == 7) {             // int_val (perm etc.)
            (void)v;
        }
    }
    if (t.v.empty() && !floats.empty()) {
        size_t total = 1;
        for (auto s : t.shape) total *= (size_t)std::max<int64_t>(1, s);
        t.v = floats.size() == 1 && total > 1 ? std::vector<float>(total, floats[0]) : floats;
    }
    return t;
}

void parseAttr(Node& nd, const std::string& key, const uint8_t* d, size_t n) {
    Pb pb{d, d + n};
    int f, w;
    uint64_t v;
    const uint8_t* data;
    size_t len;
    while (pb.next(f, w, v, data, len)) {
        if (key == "value" && f == 8 && w == 2) nd.value = parseTensor(data, len);
        else if (key == "block_size" && f == 3 && w == 0) nd.blockSize = (int)v;
        else if (key == "padding" && f == 2 && w == 2) nd.same = std::string((const char*)data, len) == "SAME";
    }
}

std::vector<Node> parseGraph(const std::vector<uint8_t>& b) {
    std::vector<Node> nodes;
    Pb pb{b.data(), b.data() + b.size()};
    int f, w;
    uint64_t v;
    const uint8_t* data;
    size_t len;
    while (pb.next(f, w, v, data, len)) {
        if (f != 1 || w != 2) continue;
        Node nd;
        Pb np{data, data + len};
        int f2, w2;
        uint64_t v2;
        const uint8_t* d2;
        size_t l2;
        while (np.next(f2, w2, v2, d2, l2)) {
            if (f2 == 1) nd.name.assign((const char*)d2, l2);
            else if (f2 == 2) nd.op.assign((const char*)d2, l2);
            else if (f2 == 3) nd.inputs.emplace_back((const char*)d2, l2);
            else if (f2 == 5 && w2 == 2) {   // map<string, AttrValue> entry
                Pb ap{d2, d2 + l2};
                int f3, w3;
                uint64_t v3;
                const uint8_t* d3;
                size_t l3;
                std::string key;
                const uint8_t* val = nullptr;
                size_t vl = 0;
                while (ap.next(f3, w3, v3, d3, l3)) {
                    if (f3 == 1) key.assign((const char*)d3, l3);
                    else if (f3 == 2) { val = d3; vl = l3; }
                }
                if (val) parseAttr(nd, key, val, vl);
            }
        }
        nodes.push_back(std::move(nd));
    }
    return nodes;
}

// HWIO filter, SAME padding, stride 1
Tensor conv2d(const Tensor& x, const Tensor& f) {
    int kh = (int)f.shape[0], kw = (int)f.shape[1], ci = (int)f.shape[2], co = (int)f.shape[3];
    Tensor y;
    y.h = x.h; y.w = x.w; y.c = co;
    y.v.assign((size_t)y.h * y.w * co, 0.f);
    int ph = (kh - 1) / 2, pw = (kw - 1) / 2;
    parallelFor(y.h, [&](int r) {
        std::vector<float> acc(co);
        for (int c0 = 0; c0 < x.w; ++c0) {
            std::fill(acc.begin(), acc.end(), 0.f);
            for (int dy = 0; dy < kh; ++dy) {
                int sy = r + dy - ph;
                if (sy < 0 || sy >= x.h) continue;
                for (int dx = 0; dx < kw; ++dx) {
                    int sx = c0 + dx - pw;
                    if (sx < 0 || sx >= x.w) continue;
                    const float* in = &x.v[((size_t)sy * x.w + sx) * ci];
                    const float* wt = &f.v[((size_t)dy * kw + dx) * ci * co];
                    for (int i = 0; i < ci; ++i) {
                        float a = in[i];
                        if (a == 0.f) continue;
                        const float* wr = wt + (size_t)i * co;
                        for (int o = 0; o < co; ++o) acc[o] += a * wr[o];
                    }
                }
            }
            std::copy(acc.begin(), acc.end(), &y.v[((size_t)r * y.w + c0) * co]);
        }
    });
    return y;
}

Tensor binary(const Tensor& a, const Tensor& b, int op) {   // 0 add, 1 sub, 2 mul (broadcast scalar / channel)
    const Tensor& big = a.v.size() >= b.v.size() ? a : b;
    const Tensor& small = a.v.size() >= b.v.size() ? b : a;
    bool swapped = &big != &a;
    Tensor y = big;
    size_t n = small.v.size();
    for (size_t i = 0; i < y.v.size(); ++i) {
        float s = n == y.v.size() ? small.v[i] : n == 0 ? 0.f : small.v[i % n];
        float l = big.v[i], av = swapped ? s : l, bv = swapped ? l : s;
        y.v[i] = op == 0 ? av + bv : op == 1 ? av - bv : av * bv;
    }
    return y;
}

} // namespace

struct SrModel::Impl {
    std::vector<Node> nodes;
    int scale = 2;
};

SrModel::SrModel() : d_(new Impl) {}
SrModel::~SrModel() = default;
int SrModel::scale() const { return d_->scale; }

bool SrModel::load(const std::vector<uint8_t>& pb, std::string& err) {
    d_->nodes = parseGraph(pb);
    for (auto& n : d_->nodes)
        if (n.op == "DepthToSpace") d_->scale = n.blockSize;
    if (d_->nodes.empty() || d_->scale < 2) { err = "초해상도 모델을 읽지 못했어요"; return false; }
    return true;
}

ImagePtr SrModel::upscale(const Image& src) const {
    const int s = d_->scale, W = src.w, H = src.h;
    // YCbCr (BT.601 full range, as OpenCV's COLOR_BGR2YCrCb)
    Tensor in;
    in.h = H; in.w = W; in.c = 1;
    in.v.resize((size_t)W * H);
    for (size_t i = 0; i < in.v.size(); ++i) {
        uint32_t c = src.px[i];
        in.v[i] = (0.299f * R(c) + 0.587f * G(c) + 0.114f * B(c)) / 255.f;
    }
    std::map<std::string, Tensor> vals;
    for (auto& n : d_->nodes) {
        auto arg = [&](size_t k) -> const Tensor& { return vals[n.inputs[k]]; };
        Tensor t;
        if (n.op == "Const") t = n.value;
        else if (n.op == "Placeholder") t = in;
        else if (n.op == "Conv2D") t = conv2d(arg(0), arg(1));
        else if (n.op == "Add" || n.op == "BiasAdd" || n.op == "AddV2") t = binary(arg(0), arg(1), 0);
        else if (n.op == "Sub") t = binary(arg(0), arg(1), 1);
        else if (n.op == "Mul") t = binary(arg(0), arg(1), 2);
        else if (n.op == "Relu") { t = arg(0); for (auto& x : t.v) x = std::max(0.f, x); }
        else if (n.op == "Abs") { t = arg(0); for (auto& x : t.v) x = std::fabs(x); }
        else if (n.op == "Tanh") { t = arg(0); for (auto& x : t.v) x = std::tanh(x); }
        else if (n.op == "DepthToSpace") {
            const Tensor& x = arg(0);
            int b = n.blockSize, oc = x.c / (b * b);
            t.h = x.h * b; t.w = x.w * b; t.c = oc;
            t.v.resize((size_t)t.h * t.w * oc);
            for (int y = 0; y < x.h; ++y)
                for (int xx = 0; xx < x.w; ++xx)
                    for (int ch = 0; ch < x.c; ++ch) {   // NHWC DCR order: channel = (by * b + bx) * oc + c
                        int c = ch % oc, bxy = ch / oc, by = bxy / b, bx = bxy % b;
                        t.v[((size_t)(y * b + by) * t.w + xx * b + bx) * oc + c] = x.v[((size_t)y * x.w + xx) * x.c + ch];
                    }
        } else if (n.op == "Transpose" || n.op == "Identity") t = arg(0);   // final NHWC->NCHW (1 channel): no-op
        else continue;
        vals[n.name] = std::move(t);
    }
    // the last real output is the upscaled Y plane
    Tensor out;
    for (auto it = d_->nodes.rbegin(); it != d_->nodes.rend(); ++it)
        if (it->op == "Tanh" || it->op == "BiasAdd" || it->op == "Transpose") { out = std::move(vals[it->name]); break; }
    ImagePtr up = resizeImage(src, W * s, H * s);   // bicubic-ish chroma
    auto o = std::make_shared<Image>(W * s, H * s);
    for (int y = 0; y < o->h; ++y)
        for (int x = 0; x < o->w; ++x) {
            uint32_t c = up->row(y)[x];
            float Y = out.v.empty() ? 0 : out.v[(size_t)y * o->w + x] * 255.f;
            float cb = -0.168736f * R(c) - 0.331264f * G(c) + 0.5f * B(c);
            float cr = 0.5f * R(c) - 0.418688f * G(c) - 0.081312f * B(c);
            int r = clamp255((int)std::lround(Y + 1.402f * cr)), g = clamp255((int)std::lround(Y - 0.344136f * cb - 0.714136f * cr)),
                b = clamp255((int)std::lround(Y + 1.772f * cb));
            o->row(y)[x] = rgb(r, g, b);
        }
    return o;
}

} // namespace bg
