// Core tests (no window): edit behaviour, formulas, LZW/GIF/APNG round trips,
// plus a small encode benchmark.  bonggif-tests.exe [--bench]
#include "core/Apng.h"
#include "core/Edit.h"
#include "core/Effects.h"
#include "core/Gif.h"
#include "core/Psd.h"
#include "core/Quantize.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

using namespace bg;

static int g_fail = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
    } while (0)

static ImagePtr solid(uint32_t c, int w = 32, int h = 24) { return std::make_shared<Image>(w, h, c); }

static Frames base8() {
    uint32_t cols[8] = {rgb(255, 0, 0), rgb(255, 128, 0), rgb(255, 255, 0), rgb(0, 255, 0), rgb(0, 255, 255), rgb(0, 0, 255), rgb(128, 0, 255), rgb(255, 0, 255)};
    int durs[4] = {70, 100, 130, 50};
    Frames f;
    for (int i = 0; i < 8; ++i) f.push_back({solid(cols[i]), durs[i % 4] * 1000LL});
    return f;
}
static std::vector<int64_t> ms(const Frames& f) {
    std::vector<int64_t> v;
    for (auto& x : f) v.push_back(x.dur / 1000);
    return v;
}

static void testFormulas() {
    CHECK(colorsForQuality(100) == 255 && colorsForQuality(80) == 207 && colorsForQuality(60) == 72);
    CHECK(colorsForQuality(50) == 42 && colorsForQuality(20) == 8 && colorsForQuality(30) == 14);
    CHECK(toleranceForQuality(100) == 1 && toleranceForQuality(80) == 6 && toleranceForQuality(1) == 15);
}

static void testEdits() {
    auto b = base8();
    CHECK((ms(changeSpeed(b, 0.9)) == std::vector<int64_t>{63, 90, 117, 45, 63, 90, 117, 45}));
    CHECK((ms(changeSpeed(b, 1.1)) == std::vector<int64_t>{77, 110, 143, 55, 77, 110, 143, 55}));
    auto r = reverse(b);
    CHECK(r[0].img->px[0] == rgb(255, 0, 255) && r[0].dur == 50000);
    auto y = yoyo(b);
    CHECK(y.size() == 16 && y[8].dur == 25000 && y[15].img->px[0] == rgb(255, 0, 0));
    CHECK(fpsToUs(30) == 33333 && secondsToUs(0.001) == 20000);
    CHECK(setTime(b, fpsToUs(20), {})[5].dur == 50000);
    CHECK(trim(b, 2, 5).size() == 4);
    Frames f;
    for (int i = 0; i < 10; ++i) f.push_back({solid(rgb(i, 0, 0)), 100000});
    auto ev = reduceFrames(f, ReduceMode::Even, 3, true, {});
    CHECK(ev.size() == 5 && R(ev[1].img->px[0]) == 2 && ev[0].dur == 200000);
    auto od = reduceFrames(f, ReduceMode::Odd, 3, false, {});
    CHECK(od.size() == 5 && R(od[0].img->px[0]) == 1 && od[0].dur == 100000);
    auto nth = reduceFrames(f, ReduceMode::Nth, 3, true, {});
    int64_t tot = 0;
    for (auto& x : nth) tot += x.dur;
    CHECK(nth.size() == 7 && R(nth[2].img->px[0]) == 3 && tot == 1000000);
    Frames dup;
    for (int i = 0; i < 5; ++i) dup.push_back({solid(rgb(10, 10, 10)), 50000});
    for (int i = 0; i < 3; ++i) dup.push_back({solid(rgb(200, 0, 0)), 50000});
    CHECK(removeDuplicates(dup, 20, -1, true, 0, {}).size() == 2);
    CHECK(removeDuplicates(dup, 20, -1, true, 100000, {}).size() == 5);
    auto s = videoSummary(dup, 50, true, 20, true, 300000, {});
    CHECK(s.size() == 2 && s[0].dur == 50000);
    Frames still(20, Frame{solid(rgb(1, 1, 1)), 100000});
    auto s2 = videoSummary(still, 0, true, 50, true, 300000, {});
    CHECK(s2.size() == 1 && s2[0].dur == 300000);
    auto rot = rotate({Frame{std::make_shared<Image>(4, 2), 1}}, Rot::CW);
    CHECK(rot[0].img->w == 2 && rot[0].img->h == 4);
    auto rs = resize(b, 16, 12);
    CHECK(rs[0].img->w == 16 && rs[0].img->px[5] == rgb(255, 0, 0));
}

static void testEffects() {
    std::mt19937 rng(1);
    auto im = std::make_shared<Image>(40, 30);
    for (auto& c : im->px) c = rgb(rng() & 255, rng() & 255, rng() & 255);
    for (int i = 0; i < (int)effectList().size(); ++i) {
        auto o = applyEffect(i, *im, {});
        CHECK(o->w == 40 && o->h == 30);
    }
    CHECK(applyEffect(0, *im, {0, 0})->px == im->px);
    auto g = applyEffect(2, *solid(rgb(255, 0, 0)), {});
    CHECK(R(g->px[0]) == (255 * 0x132) >> 10);
    EffectCtx ctx;
    ctx.rx = 5; ctx.ry = 5; ctx.rw = 10; ctx.rh = 10;
    auto reg = applyEffect(26, *im, {}, ctx);
    CHECK(reg->px[0] == im->px[0] && reg->px[6 * 40 + 6] != im->px[6 * 40 + 6]);
}

static void testLzw() {
    std::mt19937 rng(7);
    for (int minCode : {2, 4, 8}) {
        for (size_t n : {size_t(1), size_t(100), size_t(70000)}) {
            std::vector<uint8_t> v(n);
            for (size_t i = 0; i < n; ++i) v[i] = (uint8_t)((i % 97 < 50 ? (i / 13) : rng()) & ((1 << minCode) - 1));
            auto enc = lzwEncode(v.data(), n, minCode);
            std::vector<uint8_t> raw, dec;
            for (size_t p = 0; p < enc.size() && enc[p];) { raw.insert(raw.end(), enc.begin() + p + 1, enc.begin() + p + 1 + enc[p]); p += enc[p] + 1; }
            CHECK(lzwDecode(raw.data(), raw.size(), minCode, n, dec) && dec == v);
        }
    }
}

static Frames gradientAnim(int W, int H, int n) {
    Frames fr;
    for (int i = 0; i < n; ++i) {
        auto a = std::make_shared<Image>(W, H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) a->row(y)[x] = rgb(x * 255 / W, y * 255 / H, 90);
        for (int y = H / 3; y < H / 3 + H / 4; ++y)
            for (int x = 5 + i * 10; x < 30 + i * 10 && x < W; ++x) a->row(y)[x] = rgb(250, 250, 20);
        fr.push_back({a, 80000});
    }
    return fr;
}

static double mae(const Image& a, const Image& b) {
    double s = 0;
    for (size_t i = 0; i < a.px.size(); ++i)
        s += std::abs(R(a.px[i]) - R(b.px[i])) + std::abs(G(a.px[i]) - G(b.px[i])) + std::abs(B(a.px[i]) - B(b.px[i]));
    return s / (a.px.size() * 3.0);
}

static void testGif() {
    auto fr = gradientAnim(160, 120, 12);
    fr.push_back({fr.back().img, 40000});
    for (int q : {100, 80, 50}) {
        GifOptions o;
        o.quality = q;
        std::vector<uint8_t> buf;
        CHECK(encodeGif(fr, o, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }));
        Frames dec;
        std::string err;
        CHECK(decodeGif(buf, dec, err));
        CHECK(dec.size() == 12);
        int64_t tot = 0;
        double worst = 0;
        for (size_t k = 0; k < dec.size(); ++k) { tot += dec[k].dur; worst = std::max(worst, mae(*dec[k].img, *fr[k].img)); }
        CHECK(tot == 1000000);
        CHECK(worst < (q >= 80 ? 6.0 : 14.0));
        std::printf("  gif q=%d  %zu bytes  worst MAE %.2f\n", q, buf.size(), worst);
    }
    // exact palette at q=100 and loop count
    Frames few = {{solid(rgb(1, 2, 3), 40, 30), 100000}, {solid(rgb(200, 100, 50), 40, 30), 100000}, {solid(rgb(1, 2, 3), 40, 30), 100000}};
    GifOptions o;
    o.quality = 100;
    o.loop = 3;
    std::vector<uint8_t> buf;
    encodeGif(few, o, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); });
    Frames dec;
    std::string err;
    CHECK(decodeGif(buf, dec, err) && dec.size() == 3);
    for (size_t k = 0; k < dec.size(); ++k) CHECK(dec[k].img->px == few[k].img->px);
    // 3rd frame equals 1st but previous was different -> must be a real frame
}

static void testQuantMethods() {
    auto fr = gradientAnim(160, 120, 6);
    for (int m = 0; m < 4; ++m) {
        GifOptions o;
        o.method = m;
        std::vector<uint8_t> buf;
        CHECK(encodeGif(fr, o, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }));
        Frames dec;
        std::string err;
        CHECK(decodeGif(buf, dec, err) && dec.size() == 6);
        double worst = 0;
        for (size_t k = 0; k < dec.size() && k < fr.size(); ++k) worst = std::max(worst, mae(*dec[k].img, *fr[k].img));
        static const char* names[] = {"LIQ", "NeuQuant", "Octree", "Wu"};
        std::printf("  gif %-8s %zu bytes  worst MAE %.2f\n", names[m], buf.size(), worst);
        CHECK(worst < 9.0);
    }
    // colour protection keeps an exact colour that LIQ would otherwise approximate
    auto im = std::make_shared<Image>(64, 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) im->row(y)[x] = rgb(x * 4, y * 4, (x + y) * 2);
    im->row(10)[10] = rgb(13, 201, 77);
    QuantRequest q;
    q.px = im->px.data(); q.w = 64; q.h = 64; q.maxColors = 16; q.dither = false;
    q.fixedColors = {0x0DC94D};
    Palette pal;
    std::vector<uint8_t> idx;
    int t;
    quantizeFrame(q, pal, idx, t);
    CHECK(std::find(pal.colors.begin(), pal.colors.end(), 0x0DC94Du) != pal.colors.end());
}

static void testPsdAndFilters() {
    // minimal raw 2x2 RGB PSD
    std::vector<uint8_t> d = {'8', 'B', 'P', 'S', 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 2, 0, 8, 0, 3};
    for (int i = 0; i < 12; ++i) d.push_back(0);                      // 3 empty sections
    d.push_back(0); d.push_back(0);                                    // raw
    uint8_t planes[12] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    d.insert(d.end(), planes, planes + 12);
    Image im;
    std::string err;
    CHECK(decodePsd(d, im, err) && im.w == 2 && im.px[0] == rgb(255, 0, 0) && im.px[1] == rgb(0, 255, 0) && im.px[3] == rgb(255, 255, 255));
    // inpaint fills a hole from its surroundings
    Image flat(20, 20, rgb(100, 150, 200));
    std::vector<uint8_t> m(400, 0);
    for (int y = 8; y < 12; ++y) for (int x = 8; x < 12; ++x) { flat.row(y)[x] = rgb(255, 0, 0); m[y * 20 + x] = 1; }
    inpaint(flat, m, 3);
    CHECK(flat.row(10)[10] == rgb(100, 150, 200));
    // mean shift keeps a flat image flat
    Image ms(40, 30, rgb(10, 20, 30));
    meanShiftFilter(ms, 30, 20);
    CHECK(ms.px[0] == rgb(10, 20, 30));
}

static void testApng() {
    auto fr = gradientAnim(64, 48, 6);
    fr.push_back({fr.back().img, 20000});
    std::vector<uint8_t> buf;
    CHECK(encodeApng(fr, 0, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }));
    Frames dec;
    std::string err;
    CHECK(decodePng(buf, dec, err));
    CHECK(dec.size() == 6);
    if (dec.size() == 6) {
        for (int k = 0; k < 6; ++k) CHECK(dec[k].img->px == fr[k].img->px);
        CHECK(dec[5].dur == 100000);
    }
}

static void bench() {
    auto fr = gradientAnim(640, 360, 60);
    for (auto& f : fr) {   // add noise region so quantisation has work
        auto im = std::make_shared<Image>(*f.img);
        std::mt19937 rng((unsigned)(size_t)im.get());
        for (int y = 200; y < 330; ++y) for (int x = 300; x < 600; ++x) im->row(y)[x] = rgb(rng() & 255, rng() & 255, rng() & 255);
        f.img = im;
    }
    GifOptions o;
    size_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    encodeGif(fr, o, [&](const uint8_t*, size_t n) { total += n; });
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("bench: 60 frames 640x360 GIF q80 -> %zu bytes in %.3f s\n", total, s);
}

void platformTests(int& fails);

int main(int argc, char** argv) {
    testFormulas();
    testEdits();
    testEffects();
    testLzw();
    testGif();
    testQuantMethods();
    testPsdAndFilters();
    testApng();
    platformTests(g_fail);
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) bench();
    std::printf(g_fail ? "%d FAILED\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
