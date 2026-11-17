// Platform tests: MP4 export + re-import (Media Foundation), export pipeline to files,
// text rendering, 1 s GDI recording of a screen corner (no windows created).
#include "core/Edit.h"
#include "platform/Capture.h"
#include "platform/ClipExport.h"
#include "platform/ClipSource.h"
#include "platform/Export.h"
#include "platform/Media.h"
#include "platform/Codecs.h"
#include "platform/Ocr.h"
#include "core/Compose.h"
#include "core/SuperRes.h"
#include "platform/Webp.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <windows.h>

using namespace bg;

#define PCHECK(c)                                                                     \
    do {                                                                              \
        if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } \
    } while (0)

// Test helper: replaces the stts entries of an MP4 whose moov follows mdat (chunk offsets stay
// valid); sizes of the enclosing boxes are adjusted. Gives a variable frame rate file.
static bool patchStts(std::vector<uint8_t>& f, const std::vector<std::pair<uint32_t, uint32_t>>& entries) {
    auto be = [&](size_t o) { return ((uint32_t)f[o] << 24) | ((uint32_t)f[o + 1] << 16) | ((uint32_t)f[o + 2] << 8) | f[o + 3]; };
    auto put = [&](size_t o, uint32_t v) { f[o] = (uint8_t)(v >> 24); f[o + 1] = (uint8_t)(v >> 16); f[o + 2] = (uint8_t)(v >> 8); f[o + 3] = (uint8_t)v; };
    std::vector<size_t> path;   // offsets of the enclosing boxes
    bool seenMdat = false;
    size_t stts = 0;
    std::function<bool(size_t, size_t, int)> walk = [&](size_t o, size_t end, int depth) {
        while (o + 8 <= end) {
            uint32_t sz = be(o);
            if (sz < 8 || o + sz > end) return false;
            uint32_t t = be(o + 4);
            if (depth == 0 && t == 0x6d646174) seenMdat = true;   // mdat
            if (t == 0x73747473) { stts = o; return true; }       // stts
            if (t == 0x6d6f6f76 || t == 0x7472616b || t == 0x6d646961 || t == 0x6d696e66 || t == 0x7374626c) {
                if (t == 0x6d6f6f76 && !seenMdat) return false;   // moov before mdat: offsets would move
                path.push_back(o);
                if (walk(o + 8, o + sz, depth + 1)) return true;
                path.pop_back();
            }
            o += sz;
        }
        return false;
    };
    if (!walk(0, f.size(), 0)) return false;
    std::vector<uint8_t> box(16 + entries.size() * 8, 0);
    std::swap(f, box);
    put(0, (uint32_t)(16 + entries.size() * 8));
    f[4] = 's'; f[5] = 't'; f[6] = 't'; f[7] = 's';
    put(12, (uint32_t)entries.size());
    for (size_t i = 0; i < entries.size(); ++i) { put(16 + i * 8, entries[i].first); put(20 + i * 8, entries[i].second); }
    std::swap(f, box);
    const uint32_t oldSize = be(stts);
    const int64_t delta = (int64_t)box.size() - oldSize;
    f.erase(f.begin() + stts, f.begin() + stts + oldSize);
    f.insert(f.begin() + stts, box.begin(), box.end());
    for (size_t o : path) put(o, (uint32_t)((int64_t)be(o) + delta));
    return true;
}

void platformTests(int& fails) {
    mediaStartup();
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"bonggif_ptest";
    CreateDirectoryW(dir.c_str(), nullptr);
    Frames fr;
    for (int i = 0; i < 20; ++i) {
        auto a = std::make_shared<Image>(320, 240);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x) a->row(y)[x] = rgb(x * 255 / 320, y * 255 / 240, (i * 12) & 255);
        fr.push_back({a, 100000});
    }
    for (int fmt = 0; fmt < FMT_COUNT; ++fmt) {
        ExportOptions o;
        o.format = fmt;
        std::wstring p = dir + L"/out" + kFormatExt[fmt];
        size_t n = 0, est = 0;
        std::string err;
        bool ok = exportFrames(fr, o, p, n, {}, {}, err);
        PCHECK(ok);
        if (!ok) std::printf("  export %ls failed: %s\n", kFormatNames[fmt], err.c_str());
        PCHECK(exportFrames(fr, o, L"", est, {}, {}, err));
        std::printf("  %ls: %zu bytes (estimate %zu)\n", kFormatNames[fmt], n, est);
        PCHECK(n > 0 && est > 0);
    }
    {   // WebP round trip through libwebp (lossless keeps pixels exactly)
        std::vector<uint8_t> buf;
        std::string e;
        for (int iphone = 0; iphone < 2; ++iphone) {
            WebpOptions wo;
            wo.lossless = true;
            wo.iphoneCompatible = iphone != 0;
            buf.clear();
            PCHECK(encodeWebp(fr, wo, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }, {}, {}, e));
            Frames back;
            PCHECK(decodeWebp(buf, back, e) && back.size() == fr.size());
            if (back.size() == fr.size()) PCHECK(back[7].img->px == fr[7].img->px && back[7].dur == 100000);
            std::printf("  webp lossless iphone=%d: %zu bytes\n", iphone, buf.size());
        }
        // lossy, diff mode on a mostly static clip: still frames shrink to 1x1 blended frames
        Frames still;
        for (int i = 0; i < 10; ++i) still.push_back({fr[0].img, 100000});
        still[5].img = fr[6].img;
        for (int iphone = 0; iphone < 2; ++iphone) {
            WebpOptions wo;
            wo.iphoneCompatible = iphone != 0;
            size_t n = 0;
            PCHECK(encodeWebp(still, wo, [&](const uint8_t*, size_t k) { n += k; }, {}, {}, e));
            std::printf("  webp lossy static clip iphone=%d: %zu bytes\n", iphone, n);
        }
        std::printf("  webp auto-decide on gradient: %s\n", webpAutoLossless(fr, 80) ? "lossless" : "lossy");
    }
    {   // AVIF round trip (libavif + libaom)
        std::vector<uint8_t> buf;
        std::string e;
        Frames few(fr.begin(), fr.begin() + 4);
        PCHECK(encodeAvif(few, 90, 0, [&](const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }, {}, {}, e));
        Frames back;
        PCHECK(decodeAvif(buf, back, e) && back.size() == 4);
        if (back.size() == 4) {
            uint32_t a = back[2].img->row(120)[160], b = few[2].img->row(120)[160];
            PCHECK(std::abs(R(a) - R(b)) < 12 && std::abs(G(a) - G(b)) < 12 && std::abs(B(a) - B(b)) < 12);
            PCHECK(back[1].dur == 100000);
        }
        std::printf("  avif 4 frames: %zu bytes\n", buf.size());
    }
    VideoInfo vi;
    PCHECK(videoInfo(dir + L"/out.mp4", vi) && vi.w == 320 && vi.h == 240);
    std::printf("  mp4 info %dx%d %.2f fps %.2f s\n", vi.w, vi.h, vi.fps, vi.duration);
    Frames back;
    std::string err;
    PCHECK(loadVideo(dir + L"/out.mp4", 0, 0, 30, 0, back, {}, {}, err));
    std::printf("  mp4 reimport %zu frames\n", back.size());
    PCHECK(back.size() >= 18 && back[0].img->w == 320);
    if (!back.empty()) {   // colour sanity: centre pixel close to source
        uint32_t a = back[5].img->row(120)[160], b = fr[5].img->row(120)[160];
        PCHECK(std::abs(R(a) - R(b)) < 20 && std::abs(G(a) - G(b)) < 20 && std::abs(B(a) - B(b)) < 25);
    }
    TextStyle ts;
    auto t = drawText(*fr[0].img, L"한글 Text", 10, 10, ts);
    PCHECK(t->px != fr[0].img->px);
    Image logo;
    // record 1 s of a 200x150 screen corner
    Recorder rec;
    RecordOptions ro;
    ro.rect = {0, 0, 200, 150};
    ro.fps = 20;
    ro.cursor = true;
    rec.start(ro, nullptr, 0);
    Sleep(1000);
    rec.stop();
    Frames rf = rec.takeFrames();
    int64_t tot = 0;
    for (auto& f : rf) tot += f.dur;
    std::printf("  recorded %zu frames, %.2f s\n", rf.size(), tot / 1e6);
    PCHECK(!rf.empty() && rf[0].img->w == 200 && tot > 900000 && tot < 1300000);
    {   // DXGI duplication + half size
        Recorder r2;
        RecordOptions o2 = ro;
        o2.useDxgi = true;
        o2.scaleDiv = 2;
        r2.start(o2, nullptr, 0);
        Sleep(800);
        r2.stop();
        Frames f2 = r2.takeFrames();
        std::printf("  dxgi recording: %zu frames, dxgi=%d, %dx%d\n", f2.size(), (int)r2.usedDxgi(), f2.empty() ? 0 : f2[0].img->w,
                    f2.empty() ? 0 : f2[0].img->h);
        PCHECK(!f2.empty() && f2[0].img->w == 100 && f2[0].img->h == 75);
    }
    {   // direct save to MP4 (nothing kept in memory)
        Recorder r3;
        RecordOptions o3 = ro;
        o3.rect = {0, 0, 320, 240};
        o3.directSavePath = dir + L"/direct.mp4";
        r3.start(o3, nullptr, 0);
        Sleep(1200);
        r3.stop();
        PCHECK(r3.takeFrames().empty() && r3.stopReason() == 0);
        VideoInfo dv;
        PCHECK(videoInfo(o3.directSavePath, dv) && dv.w == 320 && dv.duration > 0.8);
        std::printf("  direct save: %dx%d %.2f s\n", dv.w, dv.h, dv.duration);
    }
    {   // super-resolution (ESPCN / FSRCNN x2) beats bilinear on a downscaled detail image
        auto big = std::make_shared<Image>(240, 160, rgb(255, 255, 255));
        TextStyle ts2;
        ts2.size = 40;
        ts2.color = rgb(20, 40, 160);
        ts2.outline = false;
        auto detail = drawText(*big, L"BongGIF 가나다", 6, 50, ts2);
        auto small = resizeImage(*detail, 120, 80);
        auto bil = resizeImage(*small, 240, 160);
        auto err = [](const Image& a, const Image& b) {
            double s = 0;
            for (size_t i = 0; i < a.px.size(); ++i) s += std::abs(G(a.px[i]) - G(b.px[i]));
            return s / a.px.size();
        };
        for (const wchar_t* m : {L"ESPCN_x2", L"FSRCNN_x2"}) {
            std::vector<uint8_t> pb;
            std::wstring mp = std::wstring(BG_SOURCE_DIR L"/res/models/") + std::wstring(m) + L".pb";
            PCHECK(readFile(mp, pb));
            SrModel sr;
            std::string e;
            PCHECK(sr.load(pb, e) && sr.scale() == 2);
            auto up = sr.upscale(*small);
            double es = err(*up, *detail), eb = err(*bil, *detail);
            std::printf("  %ls: error %.2f vs bilinear %.2f\n", m, es, eb);
            PCHECK(up->w == 240 && es < eb);
        }
    }
    {   // collage / zoom / frames / bubble / sticker / strokes
        Frames a(fr.begin(), fr.begin() + 3), b(fr.begin(), fr.begin() + 5);
        auto c = collage({a, b}, CL_ROW, 160, 120, 8, rgb(0, 0, 0));
        PCHECK(!c.empty() && c[0].img->w == 2 * 160 + 3 * 8);
        int64_t tc = 0;
        for (auto& x : c) tc += x.dur;
        PCHECK(tc == 500000);   // the longest (5 x 100 ms)
        auto z = zoomAnimation(fr, 0, 100, 60, 80, 60, 5, 3, 0, 50000);
        PCHECK(z.size() == fr.size() && z[7].img->w == 320 && z[7].img->px != fr[7].img->px);
        for (int st2 = 0; st2 < 6; ++st2) PCHECK(photoFrame(*fr[0].img, st2, 12, rgb(255, 255, 255))->w >= 320);
        BubbleStyle bs;
        auto bub = drawBubble(*fr[0].img, 40, 30, 200, 100, 60, 200, L"안녕하세요", bs);
        PCHECK(bub->row(80)[140] != fr[0].img->row(80)[140]);
        auto stk = drawSticker(*fr[0].img, L"\U0001F600", 100, 80, 64);
        PCHECK(stk->px != fr[0].img->px);
        Stroke sk;
        sk.pts = {{10, 10}, {200, 150}};
        auto dr = drawStrokes(*fr[0].img, {sk});
        PCHECK(dr->row(80)[106] != fr[0].img->row(80)[106] || dr->row(81)[107] != fr[0].img->row(81)[107]);
    }
    {   // ClipSource: frame-accurate random access (frame i = solid grey i*10)
        std::wstring cp = dir + L"/clip.mp4";
        const int N = 24, FPS = 30;
        Mp4Stream ms;
        std::string e;
        PCHECK(ms.open(cp, 160, 120, FPS, 95, e));
        for (int i = 0; i < N; ++i) {
            Image g(160, 120, rgb(i * 10, i * 10, i * 10));
            PCHECK(ms.write(g, (int64_t)i * 1000000 / FPS, 1000000 / FPS));
        }
        PCHECK(ms.close());
        ClipSource cs;
        bool ok = cs.open(cp, e, 8);
        PCHECK(ok);
        if (!ok) std::printf("  clip open failed: %s\n", e.c_str());
        const ClipVideoInfo& ci = cs.info();
        std::printf("  clip %dx%d %.3f fps %.3f s count %d audio %d exact %d\n", ci.w, ci.h, ci.fps, ci.duration, ci.count,
                    (int)ci.hasAudio, (int)cs.exactFrames());
        PCHECK(ci.w == 160 && ci.h == 120 && std::abs(ci.fps - FPS) < 0.01 && ci.count == N && !ci.hasAudio);
        PCHECK(cs.exactFrames() && ci.exact && cs.tableCount() == N);
        PCHECK(std::abs(cs.frameTime(0)) < 1e-6 && std::abs(cs.frameTime(12) - 12.0 / FPS) < 1e-4 && cs.frameAt(12.0 / FPS) == 12);
        auto grey = [](const ImagePtr& im) { return im ? (int)G(im->row(60)[80]) : -1000; };
        auto exact = [&](int i, const ImagePtr& im) {
            bool good = im && im->w == 160 && im->h == 120 && std::abs(grey(im) - i * 10) <= 4;
            if (!good) std::printf("  clip frame %d: grey %d (want %d)\n", i, grey(im), i * 10);
            return good;
        };
        // order: start, end, forward sequential, backward in cache, seek back out of cache, jump forward
        int order[] = {0, ci.count - 1, 5, 6, 7, 2, 12, 13, 1, 20, 9};
        for (int i : order) PCHECK(exact(i, cs.frame(i)));
        PCHECK(grey(cs.frame(4)) != grey(cs.frame(14)));
        PCHECK(cs.frame(-5) && cs.frame(10000));   // clamped
        // FPS override (web app behaviour): count follows duration * fps, the table still picks frame f
        cs.setFps(15);
        std::printf("  clip at 15 fps: count %d, frame 3 grey %d\n", cs.info().count, grey(cs.frame(3)));
        PCHECK(std::abs(cs.info().count - N / 2) <= 1 && exact(3, cs.frame(3)));
        std::vector<float> pcm;
        int sr = 0;
        PCHECK(!decodeAudioMono(cp, pcm, sr, e) && pcm.empty());   // silent file: graceful false
        std::vector<float> lv;
        PCHECK(!clipAudioLevels(cp, FPS, N, lv, e) && lv.empty());
    }
    {   // exact frame table on a variable frame rate MP4: 10 frames at 30 fps, then 10 at 10 fps (stts patched)
        std::wstring cp0 = dir + L"/clipcfr.mp4", cp = dir + L"/clipvfr.mp4";
        const int N = 20;
        Mp4Stream ms;
        std::string e;
        PCHECK(ms.open(cp0, 160, 120, 30, 95, e));
        for (int i = 0; i < N; ++i) {
            Image g(160, 120, rgb(i * 12, i * 12, i * 12));
            PCHECK(ms.write(g, (int64_t)i * 1000000 / 30, 1000000 / 30));
        }
        PCHECK(ms.close());
        Mp4FrameTable t0;
        PCHECK(readMp4FrameTable(cp0, t0) && t0.ok && (int)t0.cts.size() == N);
        std::vector<uint8_t> fb;
        const uint32_t ts = t0.timescale, d30 = ts / 30;
        bool patched = readFile(cp0, fb) && t0.ok && patchStts(fb, {{10, d30}, {N - 10, d30 * 3}});
        PCHECK(patched);
        if (patched) {
            FILE* pf = _wfopen(cp.c_str(), L"wb");
            if (pf) { std::fwrite(fb.data(), 1, fb.size(), pf); std::fclose(pf); }
        }
        std::vector<double> want(N);
        for (int i = 0; i < N; ++i) want[i] = i < 10 ? i / 30.0 : 10 / 30.0 + (i - 10) / 10.0;
        Mp4FrameTable t;
        PCHECK(readMp4FrameTable(cp, t) && t.ok && (int)t.cts.size() == N);
        if (t.ok) {
            double maxErr = 0;
            for (int i = 0; i < N && i < (int)t.times.size(); ++i) maxErr = std::max(maxErr, std::abs(t.times[i] - want[i]));
            std::printf("  vfr table: %s ts %u shift %lld count %d fps %.3f stts %d/%.3f max pts err %.6f s\n", t.codec.c_str(),
                        t.timescale, (long long)t.shift, (int)t.cts.size(), t.fps, t.sttsFrames, t.sttsFps, maxErr);
            PCHECK(maxErr < 1e-6 && t.fps == 30);   // median gap of 10x 1/30 and 9x 1/10
        }
        ClipSource cs;
        PCHECK(cs.open(cp, e, 4));
        std::printf("  vfr clip: count %d fps %.3f exact %d duration %.3f\n", cs.info().count, cs.info().fps, (int)cs.exactFrames(),
                    cs.info().duration);
        PCHECK(cs.exactFrames() && cs.info().count == N);
        PCHECK(std::abs(cs.frameTime(15) - want[15]) < 1e-6 && cs.frameAt(want[15] + 0.05) == 15);
        int bad = 0;
        int order[] = {0, 19, 14, 15, 16, 3, 11, 10, 9, 18, 1, 12};
        for (int i : order) {
            ImagePtr im = cs.frame(i);
            int gv = im ? (int)G(im->row(60)[80]) : -1000;
            if (std::abs(gv - i * 12) > 4) { ++bad; std::printf("  vfr frame %d: grey %d (want %d)\n", i, gv, i * 12); }
        }
        PCHECK(bad == 0);
    }
    {   // WebP clip export: box mask, file round trip, size estimate, size fit
        std::wstring cp = dir + L"/clipx.mp4";
        const int N = 60, FPS = 30;
        Mp4Stream ms;
        std::string e;
        PCHECK(ms.open(cp, 160, 120, FPS, 95, e));
        for (int i = 0; i < N; ++i) {   // moving gradient on the left half; frames 30..39 identical
            const int m = (i >= 30 && i < 40) ? 30 : i;
            Image g(160, 120);
            for (int y = 0; y < 120; ++y)
                for (int x = 0; x < 160; ++x) g.row(y)[x] = x < 80 ? rgb((x + m * 3) & 255, y * 2, (m * 4) & 255) : rgb(40, 90, 160);
            PCHECK(ms.write(g, (int64_t)i * 1000000 / FPS, 1000000 / FPS));
        }
        PCHECK(ms.close());
        ClipProject cpj = newClipProject(N, FPS);
        ClipMask m = newClipMask(L"box");
        m.effect = CE_BOX;
        m.color = rgb(255, 0, 0);
        m.keys[0] = {50, 30, 60, 60};
        cpj.masks.push_back(m);
        std::wstring wp = dir + L"/clipx.webp";
        DeleteFileW(wp.c_str());
        ClipExportResult xr;
        int steps = 0;
        bool ok = exportClipWebp(cp, FPS, cpj, wp, xr, [&](int, int) { ++steps; }, {}, e);
        PCHECK(ok);
        if (!ok) std::printf("  clip export failed: %s\n", e.c_str());
        std::printf("  clip webp: %zu bytes, %d planned, %d encoded, %dx%d\n", xr.bytes, xr.planned, xr.encoded, xr.w, xr.h);
        PCHECK(xr.planned > 0 && steps == xr.planned && xr.encoded >= 1 && xr.w == 160 && xr.h == 120);
        std::vector<uint8_t> wb;
        PCHECK(readFile(wp, wb) && wb.size() == xr.bytes);
        Frames wf;
        PCHECK(decodeWebp(wb, wf, e) && !wf.empty());
        if (!wf.empty()) {
            PCHECK(wf[0].img->w == 160 && wf[0].img->h == 120);
            uint32_t c = wf[wf.size() / 2].img->row(60)[80];
            std::printf("  clip webp %zu frames, box centre %d,%d,%d\n", wf.size(), R(c), G(c), B(c));
            PCHECK(R(c) > 200 && G(c) < 50 && B(c) < 50);
        }
        // container structure: VP8X(animation) + ANIM + ANMF (no blend, no dispose, even offsets)
        if (wb.size() > 30) {
            int anmf = 0, full = 0, narrow = 0, badFlags = 0, odd = 0;
            bool vp8x = !std::memcmp(wb.data() + 12, "VP8X", 4) && (wb[20] & 0x02);
            for (size_t o = 12; o + 8 <= wb.size();) {
                uint32_t sz = wb[o + 4] | (wb[o + 5] << 8) | (wb[o + 6] << 16) | ((uint32_t)wb[o + 7] << 24);
                if (!std::memcmp(wb.data() + o, "ANMF", 4)) {
                    const uint8_t* h = wb.data() + o + 8;
                    int fx = (h[0] | (h[1] << 8) | (h[2] << 16)) * 2, fw = (h[6] | (h[7] << 8) | (h[8] << 16)) + 1;
                    ++anmf;
                    if (fw == 160 && fx == 0) ++full;
                    if (fx + fw <= 96) ++narrow;
                    if (h[15] != 0x02) ++badFlags;
                    if (fx & 1) ++odd;
                }
                o += 8 + sz + (sz & 1);
            }
            std::printf("  clip webp chunks: anmf %d (full %d, left-half %d), flags bad %d\n", anmf, full, narrow, badFlags);
            PCHECK(vp8x && anmf == xr.encoded && full >= 1 && narrow >= 1 && badFlags == 0 && odd == 0);
            PCHECK(xr.encoded < xr.planned);   // identical frames merged
        }
        int db[4];
        Image da(10, 10, rgb(10, 10, 10)), dbb = da;
        PCHECK(!clipDiffBox(da, dbb, 2, db));
        dbb.row(5)[3] = rgb(13, 10, 10);
        PCHECK(clipDiffBox(da, dbb, 2, db) && db[0] == 2 && db[1] == 4 && db[2] == 4 && db[3] == 6);
        size_t est = 0;
        PCHECK(estimateClipBytes(cp, FPS, cpj, est, {}, e) && est > 0);
        std::printf("  clip estimate %zu bytes (actual %zu)\n", est, xr.bytes);
        ClipFitResult fit;
        int tries = 0;
        PCHECK(fitClipToSize(cp, FPS, cpj, (size_t)1 << 30, fit, [&](int, int, const FitCandidate&) { ++tries; }, {}, e));
        PCHECK(fit.found && fit.fits && tries == 1);
        auto cands = fitCandidates(160, FPS);
        if (!cands.empty()) PCHECK(fit.cand.width == cands[0].width && fit.cand.quality == cands[0].quality);
    }
    {   // OCR on rendered text
        auto canvas = std::make_shared<Image>(420, 90, rgb(255, 255, 255));
        TextStyle t2;
        t2.size = 44;
        t2.color = rgb(0, 0, 0);
        t2.outline = false;
        auto txt = drawText(*canvas, L"Hello 2026", 12, 14, t2);
        std::wstring text;
        std::string e;
        bool ok = ocrImage(*txt, text, e);
        std::printf("  ocr: %s -> \"%ls\"\n", ok ? "ok" : e.c_str(), text.c_str());
        PCHECK(ok && text.find(L"2026") != std::wstring::npos);
    }
    mediaShutdown();
}
