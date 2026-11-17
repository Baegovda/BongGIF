// Core tests (no window): edit behaviour, formulas, LZW/GIF/APNG round trips,
// plus a small encode benchmark.  bonggif-tests.exe [--bench]
#include "core/Apng.h"
#include "core/Clip.h"
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

static bool sameMask(const ClipMask& a, const ClipMask& b) {
    return a.name == b.name && a.effect == b.effect && a.strength == b.strength && a.color == b.color && a.zoom == b.zoom &&
           a.border == b.border && a.shape == b.shape && a.pad == b.pad && a.start == b.start && a.end == b.end &&
           a.enabled == b.enabled && a.smooth == b.smooth && a.fixedSize == b.fixedSize && a.keys == b.keys &&
           a.destKeys == b.destKeys && a.trackScore == b.trackScore && a.lost == b.lost;
}

static void testClip() {
    // keyframes
    std::map<int, Box4> keys{{0, {0, 0, 10, 10}}, {10, {100, 50, 20, 30}}};
    Box4 b;
    CHECK(interpBox(keys, 5, b) && b[0] == 50 && b[1] == 25 && b[2] == 15 && b[3] == 20);
    CHECK(interpBox(keys, -3, b) && b[0] == 0 && interpBox(keys, 99, b) && b[0] == 100);
    CHECK(!interpBox({}, 0, b));
    // output plan
    ClipProject p = newClipProject(60, 30);
    CHECK(p.outFrame == 59 && p.ex.fps == 30);
    p.ex.fps = 60;
    auto of = outputFrames(p, 30);
    CHECK(of.size() == 120 && of[0] == 0 && of[1] == 0 && of[2] == 1 && of.back() == 59);
    p.freezes[10] = 0.5;
    auto ofz = outputFrames(p, 30);
    CHECK(ofz.size() == 150 && ofz[19] == 9 && ofz[20] == 10 && ofz[49] == 10 && ofz[50] == 10 && ofz[52] == 11);
    p.freezes.clear();
    p.slowmos.push_back({20, 29, 0.5});
    auto ofs = outputFrames(p, 30);
    CHECK(ofs.size() == 140 && ofs.back() == 59);
    CHECK(speedAt(p, 25) == 0.5 && speedAt(p, 30) == 1);
    CHECK(frameDurationMs(0, 60) == 17 && frameDurationMs(1, 60) == 16 && frameDurationMs(2, 60) == 17);
    CHECK(frameDurationMs(0, 60) + frameDurationMs(1, 60) + frameDurationMs(2, 60) == 50);
    CHECK(snapFps(29.9701) == 29.97 && snapFps(12.3456) == 12.346 && defaultExportFps(29.97) == 29.97 && defaultExportFps(120) == 60);
    auto fc = fitCandidates(1920, 60);
    CHECK(!fc.empty() && fc[0].width == 0 && fc[0].quality == 90 && fc[0].fps == 60);
    CHECK(fc.size() > 5 && fc[4].width == 1632);
    CHECK(exportFilename(L"clip", {{3, 40}}) == L"clip_3-40.webp" && exportFilename(L"clip", {{0, 1}, {5, 9}}) == L"clip_2구간.webp");
    // audio events
    std::vector<float> db(90, -60.f);
    for (int i = 60; i < 90; ++i) db[i] = -10.f;
    auto ev = findEvents(db, 30);
    CHECK(ev.size() == 1 && ev[0] == 60);
    // weak runs
    ClipMask wm = newClipMask(L"w");
    for (int f : {3, 4, 5, 9, 10}) wm.trackScore[f] = 0.3;
    wm.trackScore[6] = 0.9;
    CHECK(weakCount(wm) == 2 && nextWeak(wm, 0, 1) == 3 && nextWeak(wm, 4, 1) == 9 && nextWeak(wm, 10, -1) == 3 && nextWeak(wm, 9, 1) == -1);
    CHECK(weakCount(wm, 4, 9) == 2 && weakCount(wm, 6, 8) == 0 && nextWeak(wm, 0, 1, 0.2) == -1);
    // blendTracks: JS rounding (Math.round(-12.5) == -12) and disagreement marking
    {
        TrackResult fw, bw;
        fw.rects[1] = {-0.125, 0, 10, 10};
        bw.rects[1] = {-0.125, 0, 10, 10};
        fw.scores[1] = 0.9;
        bw.scores[1] = 0.7;
        fw.rects[2] = {0, 0, 10, 10};
        bw.rects[2] = {50, 0, 10, 10};
        fw.scores[2] = 0.8;
        bw.scores[2] = 0.95;
        fw.scores[3] = 0;   // coasted in one direction, nothing from the other
        auto bl = blendTracks(fw, bw, 0, 4);
        CHECK(bl.rects[1][0] == -0.12 && bl.scores[1] == 0.85);   // t = 0.25: 0.9*0.75 + 0.7*0.25
        CHECK(bl.rects[2][0] == 50 && bl.scores[2] == 0.59 && !bl.rects.count(3) && bl.scores.count(3) && bl.scores[3] == 0);
    }
    // geometry by hand from core.js
    {
        int zb[4], box[4] = {20, 20, 40, 40}, cr[4];
        zoomBox(box, 2, 100, 100, zb);   // 40x40, dx = round((20+40-40)/2) = 10
        CHECK(zb[0] == 10 && zb[1] == 10 && zb[2] == 50 && zb[3] == 50);
        Box4 dd = defaultDest({10, 10, 20, 20}, 100, 100);   // 10+20+16+40 <= 100 -> dx 46; dy = 10+10-20 = 0
        CHECK(dd[0] == 46 && dd[1] == 0 && dd[2] == 40 && dd[3] == 40);
        CHECK(clipRect({-0.5, 2.5, 10, 10}, 0, 100, 100, cr) && cr[0] == 0 && cr[1] == 3 && cr[2] == 10 && cr[3] == 13);
        CHECK(clipRect({10.5, 10.5, 10, 10}, 2, 100, 100, cr) && cr[0] == 9 && cr[2] == 23);
        CHECK(clipRect({-3.5, 0, 10, 10}, 0, 100, 100, cr) && cr[2] == 7);   // round(6.5) = 7
    }
    // speed 2 halves the frame count
    {
        ClipProject sp2 = newClipProject(60, 30);
        sp2.ex.fps = 30;
        sp2.ex.speed = 2;
        auto o2 = outputFrames(sp2, 30);
        CHECK(o2.size() == 30 && o2[1] == 2 && o2.back() == 58);
    }
    // diffBox
    {
        Image a(10, 8, rgb(1, 2, 3)), b2 = a;
        int db[4];
        CHECK(!diffBox(a, b2, db));
        b2.row(5)[7] = rgb(1, 2, 6);
        b2.row(3)[3] = rgb(1, 2, 5);   // within tol 2
        CHECK(diffBox(a, b2, db) && db[0] == 6 && db[1] == 4 && db[2] == 8 && db[3] == 6);
    }
    // rendering: box fills the rect; ellipse leaves the corners
    Image im(50, 40, rgb(255, 255, 255));
    ClipMask bm = newClipMask(L"box");
    bm.effect = CE_BOX;
    bm.color = rgb(255, 0, 0);
    bm.keys[0] = {10, 10, 20, 15};
    renderMasks(im, {bm}, 0);
    CHECK(im.row(10)[10] == rgb(255, 0, 0) && im.row(24)[29] == rgb(255, 0, 0) && im.row(9)[10] == rgb(255, 255, 255) &&
          im.row(25)[29] == rgb(255, 255, 255) && im.row(10)[30] == rgb(255, 255, 255));
    Image im2(50, 40, rgb(255, 255, 255));
    bm.shape = CS_ELLIPSE;
    renderMasks(im2, {bm}, 0);
    CHECK(im2.row(10)[10] == rgb(255, 255, 255) && im2.row(17)[20] == rgb(255, 0, 0));
    bm.enabled = false;
    Image im3(50, 40, rgb(255, 255, 255));
    renderMasks(im3, {bm}, 0);
    CHECK(im3.row(17)[20] == rgb(255, 255, 255));
    // mosaic: a 2-colour checker in 4x4 cells averages to grey
    Image mz(16, 16);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) mz.row(y)[x] = ((x + y) & 1) ? rgb(0, 0, 0) : rgb(200, 200, 200);
    ClipMask mm = newClipMask(L"m");
    mm.strength = 4;
    mm.keys[0] = {0, 0, 16, 16};
    renderMasks(mz, {mm}, 0);
    CHECK(mz.row(5)[7] == rgb(100, 100, 100));
    // ellipse edges are anti-aliased (canvas clip): a pixel on the outline is a blend
    {
        Image ie(40, 40, rgb(255, 255, 255));
        ClipMask em = newClipMask(L"e");
        em.effect = CE_BOX;
        em.shape = CS_ELLIPSE;
        em.color = rgb(0, 0, 0);
        em.keys[0] = {0, 0, 40, 40};
        renderMasks(ie, {em}, 0);
        int edge = R(ie.row(20)[0]);   // centre (0.5, 20.5): about 0.5 px inside the circle of radius 20
        CHECK(edge > 0 && edge < 255 && R(ie.row(20)[20]) == 0 && R(ie.row(0)[0]) == 255);
    }
    // portal: nothing without exit keys; with one, the source is drawn there with a border inside
    {
        Image ip(60, 40, rgb(255, 255, 255));
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) ip.row(y)[x] = rgb(0, 0, 255);
        ClipMask pm = newClipMask(L"p");
        pm.effect = CE_PORTAL;
        pm.color = rgb(255, 0, 0);
        pm.border = 2;
        pm.keys[0] = {0, 0, 10, 10};
        Image ip0 = ip;
        renderMasks(ip0, {pm}, 0);
        CHECK(ip0.px == ip.px);
        pm.destKeys[0] = {30.4, 10, 20, 20};
        renderMasks(ip, {pm}, 0);
        CHECK(ip.row(10)[30] == rgb(255, 0, 0) && ip.row(11)[31] == rgb(255, 0, 0) && ip.row(12)[32] == rgb(0, 0, 255) &&
              ip.row(20)[40] == rgb(0, 0, 255) && ip.row(29)[49] == rgb(255, 0, 0) && ip.row(10)[29] == rgb(255, 255, 255) &&
              ip.row(10)[50] == rgb(255, 255, 255));
        // zoom 2: box 10..20 -> zoomBox [5,5,25,25], uniform stays uniform, border t = 2 inside
        Image iz(40, 40, rgb(0, 200, 0));
        ClipMask zm = newClipMask(L"z");
        zm.effect = CE_ZOOM;
        zm.border = 2;
        zm.color = rgb(9, 9, 9);
        zm.keys[0] = {10, 10, 10, 10};
        renderMasks(iz, {zm}, 0);
        CHECK(iz.row(5)[5] == rgb(9, 9, 9) && iz.row(6)[6] == rgb(9, 9, 9) && iz.row(7)[7] == rgb(0, 200, 0) &&
              iz.row(4)[4] == rgb(0, 200, 0) && iz.row(24)[24] == rgb(9, 9, 9) && iz.row(25)[25] == rgb(0, 200, 0));
    }
    // blur = Gaussian with sigma = strength over the area grown by 3 sigma
    {
        Image ib(100, 40);
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 100; ++x) ib.row(y)[x] = x < 50 ? rgb(0, 0, 0) : rgb(200, 200, 200);
        Image flat(100, 40, rgb(90, 120, 150));
        ClipMask gm = newClipMask(L"g");
        gm.effect = CE_BLUR;
        gm.strength = 4;
        gm.keys[0] = {30, 0, 40, 40};
        renderMasks(ib, {gm}, 0);
        renderMasks(flat, {gm}, 0);
        // K0 = 1 / sum(exp(-k^2/32), |k| <= 12) = 0.0997: 200 * (0.5 + K0/2) = 110.0, 200 * (0.5 - K0/2) = 90.0
        int a = R(ib.row(20)[50]), b = R(ib.row(20)[49]);
        CHECK(a == 110 && b == 90 && R(ib.row(20)[30]) == 0 && R(ib.row(20)[69]) == 200 && R(ib.row(20)[29]) == 0);
        // at the frame edge the blurred layer is partly transparent; the frame shows through, so flat stays flat
        CHECK(flat.row(0)[30] == rgb(90, 120, 150) && flat.row(39)[69] == rgb(90, 120, 150) && R(ib.row(0)[50]) > 110);
        auto t0 = std::chrono::steady_clock::now();
        Image hb(1920, 1080, rgb(10, 20, 30));
        gm.strength = 120;
        gm.keys[0] = {600, 300, 400, 400};
        renderMasks(hb, {gm}, 0);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        gm.strength = 12;
        gm.keys[0] = {600, 300, 300, 200};
        t0 = std::chrono::steady_clock::now();
        renderMasks(hb, {gm}, 0);
        double ms12 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("clip blur: 400x400 sigma 120 %.0f ms, 300x200 sigma 12 %.1f ms\n", ms, ms12);
        CHECK(hb.row(500)[800] == rgb(10, 20, 30));
    }
    // tracker: textured square moving 3 px/frame
    {
        std::mt19937 rng(7);
        Image tex(40, 40);
        for (auto& c : tex.px) { int v = rng() & 255; c = rgb(v, (v * 3) & 255, 255 - v); }
        auto frameAt = [&](int k, bool show = true) {
            Image f(320, 240);
            for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) f.row(y)[x] = rgb(60 + x / 8, 80, 90 + y / 8);
            int ox = 40 + 3 * k, oy = 70 + k;
            if (show)
                for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) f.row(oy + y)[ox + x] = tex.row(y)[x];
            return f;
        };
        for (bool fixed : {true, false}) {
            TemplateTracker tr(frameAt(0), {40, 70, 40, 40}, fixed);
            double worst = 0;
            bool okAll = true;
            Box4 o{};
            double sc = 0;
            for (int k = 1; k <= 20; ++k) {
                okAll = okAll && tr.track(frameAt(k), o, sc) == TS_OK;
                worst = std::max({worst, std::abs(o[0] + o[2] / 2 - (60 + 3 * k)), std::abs(o[1] + o[3] / 2 - (90 + k))});
            }
            CHECK(okAll && worst <= 1 && std::abs(o[2] - 40) <= 2 && sc > 0.9);
            CHECK(o[0] == std::round(o[0] * 100) / 100 && sc == std::round(sc * 100) / 100);   // app.js rounding
        }
        // occlusion: coasts with score 0, then picks the target up again
        {
            TrackerOptions opt;
            opt.fixedSize = true;
            opt.fps = 30;
            TemplateTracker tr(frameAt(0), {40, 70, 40, 40}, opt);
            Box4 o{};
            double sc = 0;
            std::vector<int> st;
            for (int k = 1; k <= 14; ++k) st.push_back(tr.track(frameAt(k, k < 6 || k > 9), o, sc));
            CHECK(st[4] == TS_OK && st[5] == TS_COAST && st[8] == TS_COAST && st[9] == TS_OK && st[13] == TS_OK);
            CHECK(std::abs(o[0] - (40 + 3 * 14)) <= 1 && std::abs(o[1] - (70 + 14)) <= 1);
            // gone for good: lost after maxCoast = round(30 * 0.5) = 15 coast frames
            TemplateTracker tg(frameAt(0), {40, 70, 40, 40}, opt);
            int k = 1, coasts = 0;
            for (; k < 40; ++k) {
                TrackState s = tg.track(frameAt(k, false), o, sc);
                if (s == TS_LOST) break;
                coasts += s == TS_COAST && sc == 0;
            }
            CHECK(coasts == 15 && k == 16);
        }
        // template bank: the first look + the latest 5, not for a box over the edge
        ClipMask bm2 = newClipMask(L"b");
        Image f0 = frameAt(0);
        for (int f = 0; f < 8; ++f) trackBankAdd(bm2, f, f0, {40, 70, 40, 40});
        trackBankAdd(bm2, 99, f0, {-5, 70, 40, 40});
        CHECK(bm2.trackBank.size() == 6 && bm2.trackBank[0].f == 0 && bm2.trackBank[1].f == 3 && bm2.trackBank[5].f == 7);
        CHECK(trackBankOf(bm2, 3).size() == 5 && bm2.trackBank[0].img->w == 40);
        trackBankAdd(bm2, 50, f0, {0, 0, 96, 48});
        CHECK(bm2.trackBank.back().img->w == 48 && bm2.trackBank.back().img->h == 24);
        TrackerOptions bo;
        bo.bank = trackBankOf(bm2, -1);
        TemplateTracker tb(frameAt(0), {40, 70, 40, 40}, bo);
        Box4 o{};
        double sc = 0;
        CHECK(tb.track(frameAt(1), o, sc) == TS_OK && std::abs(o[0] - 43) <= 1);
        ClipProject bp;
        bp.masks = {bm2};
        ClipProject bq;
        CHECK(clipFromJson(clipToJson(bp), bq) && bq.masks.size() == 1 && bq.masks[0].trackBank.size() == 6 &&
              bq.masks[0].trackBank.back().f == 50 && bq.masks[0].trackBank.back().img->px == bm2.trackBank.back().img->px);
    }
    // tracker speed: 200x200 box on 1080p
    {
        std::mt19937 rng(3);
        Image big(1920 + 120, 1080);
        for (int y = 0; y < 1080; ++y) for (int x = 0; x < big.w; ++x) {
            int v = (int)((std::sin(x * 0.05) + std::cos(y * 0.07)) * 50 + 128 + (rng() & 31));
            big.row(y)[x] = rgb(clamp255(v), clamp255(v / 2 + 40), clamp255(255 - v));
        }
        auto frameAt = [&](int k) {
            Image f(1920, 1080);
            for (int y = 0; y < 1080; ++y) std::memcpy(f.row(y), big.row(y) + 3 * k, 1920 * 4);   // content moves left 3 px/frame
            return f;
        };
        std::vector<Image> frames;
        for (int k = 0; k <= 30; ++k) frames.push_back(frameAt(k));
        for (bool fixed : {true, false}) {
            TemplateTracker tr(frames[0], {900, 400, 200, 200}, fixed);
            Box4 o{};
            double sc = 0;
            bool ok = true;
            auto t0 = std::chrono::steady_clock::now();
            for (int k = 1; k <= 30; ++k) ok = tr.track(frames[k], o, sc) == TS_OK && ok;
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("clip tracker (%s): 200x200 on 1080p %.0f updates/s (x=%.2f expected 810, w=%.1f, score %.2f)\n",
                        fixed ? "fixed size" : "free size", 30 / s, o[0], o[2], sc);
            CHECK(ok && std::abs(o[0] + o[2] / 2 - 910) <= 2 && std::abs(o[1] + o[3] / 2 - 500) <= 2);
        }
    }
    // smoothing keeps a straight line
    ClipMask sm = newClipMask(L"s");
    for (int f = 0; f < 20; ++f) sm.keys[f] = {f * 2.0, 5, 10, 10};
    sm.keys[10][0] += 1;
    smoothKeys(sm, 0, 19);
    CHECK(sm.keys[0][0] == 0 && std::abs(sm.keys[10][0] - 20) < 0.3 && sm.keys[5][1] == 5);
    // JSON round trip
    ClipProject q = newClipProject(300, 59.94);
    q.inFrame = 12;
    q.outFrame = 250;
    q.hasCrop = true;
    q.crop = {10.5, 20.25, 640, 360.125};
    q.ex.speed = 1.5;
    q.ex.width = 480;
    q.ex.quality = 77;
    q.ex.loop = 1;
    q.ex.targetMB = 9.75;
    q.freezes[40] = 1.5;
    q.freezes[100] = 0.1;
    q.segments = {{12, 80}, {120, 250}};
    q.slowmos = {{30, 60, 0.33}};
    ClipMask m1 = newClipMask(L"모자이크 \"1\"\\\n😀");
    m1.keys[12] = {1, 2, 3, 4};
    m1.keys[13] = {1.1, 2.2, 3.3, 4.4};
    m1.trackScore[13] = 0.55;
    m1.lost = {14, 15};
    m1.smooth = false;
    ClipMask m2 = newClipMask(L"portal", &m1);
    m2.effect = CE_PORTAL;
    m2.shape = CS_ELLIPSE;
    m2.color = 0xFF12ABEFu;
    m2.zoom = 3.7;
    m2.border = 5;
    m2.pad = 7;
    m2.start = 20;
    m2.end = 200;
    m2.fixedSize = false;
    m2.enabled = false;
    m2.keys[20] = {100, 100, 50, 60};
    m2.destKeys[20] = {300, 10, 100, 120};
    m2.destKeys[90] = {310.333, 15, 100, 120};
    q.masks = {m1, m2};
    std::string js = clipToJson(q);
    ClipProject r;
    CHECK(clipFromJson(js, r));
    CHECK(r.inFrame == q.inFrame && r.outFrame == q.outFrame && r.hasCrop && r.crop == q.crop);
    CHECK(r.ex.fps == q.ex.fps && r.ex.speed == q.ex.speed && r.ex.width == q.ex.width && r.ex.quality == q.ex.quality &&
          r.ex.loop == q.ex.loop && r.ex.targetMB == q.ex.targetMB);
    CHECK(r.freezes == q.freezes && r.segments.size() == 2 && r.segments[1].lo == 120 && r.segments[1].hi == 250);
    CHECK(r.slowmos.size() == 1 && r.slowmos[0].lo == 30 && r.slowmos[0].hi == 60 && r.slowmos[0].rate == 0.33);
    CHECK(r.masks.size() == 2 && sameMask(r.masks[0], m1) && sameMask(r.masks[1], m2));
    CHECK(clipToJson(r) == js);
    ClipProject d;
    CHECK(clipFromJson("{\"unknown\":[1,{\"x\":null}],\"inFrame\":5}", d) && d.inFrame == 5 && d.ex.fps == 60);
    CHECK(!clipFromJson("{\"inFrame\":", d));
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
    testClip();
    platformTests(g_fail);
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) bench();
    std::printf(g_fail ? "%d FAILED\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
