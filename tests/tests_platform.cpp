// Platform tests: MP4 export + re-import (Media Foundation), export pipeline to files,
// text rendering, 1 s GDI recording of a screen corner (no windows created).
#include "core/Edit.h"
#include "platform/Capture.h"
#include "platform/Export.h"
#include "platform/Media.h"
#include "platform/Codecs.h"
#include "platform/Ocr.h"
#include "core/Compose.h"
#include "core/SuperRes.h"
#include "platform/Webp.h"

#include <cstdio>
#include <windows.h>

using namespace bg;

#define PCHECK(c)                                                                     \
    do {                                                                              \
        if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } \
    } while (0)

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
