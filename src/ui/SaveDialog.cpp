// Save window: format, quality, per-format details (GIF quantiser + colour protection, WebP, WebM),
// size-reduction options and a live size estimate from a real background encode.
#include "ui/Dialogs.h"

#include "core/Gif.h"
#include "core/Quantize.h"
#include "platform/Codecs.h"
#include "platform/Export.h"
#include "ui/Gui.h"

#include <algorithm>
#include <atomic>
#include <commdlg.h>
#include <ctime>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>

namespace bg {

namespace {
struct Estimate {
    std::atomic<bool> cancel{false}, done{false};
    std::atomic<double> prog{0};
    size_t bytes = 0;
    std::string err;
    std::thread th;
    ~Estimate() { cancel = true; if (th.joinable()) th.join(); }
};
// dropdown order -> stored format value
const int kUiToFormat[] = {FMT_GIF, FMT_WEBP, FMT_APNG, FMT_MP4, FMT_WEBM, FMT_AVIF, FMT_PNGSEQ, FMT_JPGSEQ};
const int kNumUi = 8;

std::vector<uint32_t> parseColors(const std::wstring& s) {
    std::vector<uint32_t> v;
    size_t p = 0;
    while (p < s.size()) {
        size_t c = s.find(L',', p);
        std::wstring t = s.substr(p, c == std::wstring::npos ? std::wstring::npos : c - p);
        if (t.size() == 6) v.push_back(wcstoul(t.c_str(), nullptr, 16) & 0xFFFFFF);
        if (c == std::wstring::npos) break;
        p = c + 1;
    }
    return v;
}
std::wstring joinColors(const std::vector<uint32_t>& v) {
    std::wstring s;
    for (uint32_t c : v) {
        wchar_t b[16];
        swprintf_s(b, L"%06X", c & 0xFFFFFF);
        if (!s.empty()) s += L",";
        s += b;
    }
    return s;
}
} // namespace

bool saveDialog(HWND owner, const Frames& frames, Settings& st, std::wstring& savedPath, size_t& outBytes) {
    Form f(owner, L"파일 저장", 580);
    int64_t tot = 0;
    for (auto& x : frames) tot += x.dur;
    std::wstring dir = st.saveDir, name;
    {
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_s(&tmv, &now);
        wchar_t buf[64];
        wcsftime(buf, 64, L"BongGIF %Y-%m-%d %H-%M-%S", &tmv);
        name = buf;
    }
    int fmtUi = 0;
    for (int i = 0; i < kNumUi; ++i) if (kUiToFormat[i] == st.format) fmtUi = i;
    int quality = st.quality, loopCount = st.loopCount, scale = st.scalePct, maxFps = st.maxFps, method = st.gifMethod;
    int maxFrameMs = st.maxFrameMs, maxSide = st.maxSide, codecSel = st.webmVp9 ? 0 : 1;
    bool dither = st.dither, center = st.centerWeighted, similar = st.removeSimilar, skipQ = st.skipQuant100, comment = st.comment;
    bool webpLossless = st.webpLossless, webpAuto = st.webpAutoDecide, webpIphone = st.webpIphoneCompatible, webpQual = st.webpQualityFirst;
    bool loopFinite = st.loopFinite, bar = st.progressBar, openFolder = st.openFolder, cc = st.colorCompress, skipSim = st.skipSimilar;
    std::vector<uint32_t> protect = parseColors(st.protectColors);
    bool gifOpen = true, webpOpen = false, webmOpen = false, sizeOpen = false;

    auto options = [&] {
        ExportOptions o;
        o.format = kUiToFormat[fmtUi];
        o.quality = quality;
        o.jpgQuality = quality;
        o.gif.dither = dither;
        o.gif.centerWeighted = center;
        o.gif.removeSimilar = similar;
        o.gif.skipQuantQ100 = skipQ;
        o.gif.comment = comment;
        o.gif.method = method;
        if (method == QM_LIQ) o.gif.protectColors = protect;
        o.webp.lossless = webpLossless;
        o.webp.autoDecide = webpAuto;
        o.webp.iphoneCompatible = webpIphone;
        o.webp.qualityFirst = webpQual;
        o.webmVp9 = codecSel == 0;
        o.loop = loopFinite ? loopCount : 0;
        o.scalePct = scale;
        o.maxFps = maxFps;
        o.skipSimilar = skipSim;
        o.maxFrameMs = maxFrameMs;
        o.maxSide = maxSide;
        o.progressBar = bar;
        o.colorCompress = cc;
        return o;
    };
    auto signature = [&] {
        ExportOptions o = options();
        wchar_t b[256];
        swprintf_s(b, L"%d/%d/%d%d%d%d%d/%d/%d%d%d%d/%d/%d/%d/%d/%d%d/%d/%d/%d/%s", o.format, o.quality, dither, center, similar, skipQ, comment, method,
                   webpLossless, webpAuto, webpIphone, webpQual, codecSel, o.loop, scale, maxFps, (int)bar, (int)cc, (int)skipSim, maxFrameMs, maxSide,
                   joinColors(protect).c_str());
        return std::wstring(b);
    };
    auto remember = [&] {
        st.saveDir = dir;
        st.format = kUiToFormat[fmtUi];
        st.quality = quality;
        st.dither = dither; st.centerWeighted = center; st.removeSimilar = similar; st.skipQuant100 = skipQ; st.comment = comment;
        st.gifMethod = method; st.protectColors = joinColors(protect);
        st.webpLossless = webpLossless; st.webpAutoDecide = webpAuto; st.webpIphoneCompatible = webpIphone; st.webpQualityFirst = webpQual;
        st.webmVp9 = codecSel == 0; st.colorCompress = cc; st.skipSimilar = skipSim; st.maxFrameMs = maxFrameMs; st.maxSide = maxSide;
        st.loopFinite = loopFinite; st.loopCount = loopCount; st.scalePct = scale; st.maxFps = maxFps;
        st.progressBar = bar; st.openFolder = openFolder;
        st.save();
    };

    std::unique_ptr<Estimate> est;
    std::wstring estSig;
    int debounce = 0;
    auto startEstimate = [&] {
        est.reset();
        est = std::make_unique<Estimate>();
        Estimate* e = est.get();
        ExportOptions o = options();
        const Frames* fr = &frames;
        e->th = std::thread([e, o, fr] {
            std::string err;
            size_t n = 0;
            bool ok = exportFrames(*fr, o, L"", n, [e](double v) { e->prog = v; }, [e] { return e->cancel.load(); }, err);
            e->bytes = ok ? n : 0;
            e->err = ok ? "" : (err.empty() ? "cancel" : err);
            e->done = true;
        });
    };
    std::atomic<bool> saving{false}, saveDone{false};
    std::atomic<double> saveProg{0};
    std::string saveErr;
    std::thread saveTh;
    std::wstring path;

    f.okText = L"저장";
    f.body = [&](Gui& g, float x, float y, float w) {
        Gfx& gx = g.g;
        float cx = x;
        auto chip = [&](const std::wstring& t) {
            float cw = gx.textWidth(t, th::caption, 600) + 18;
            g.chip({cx, y, cw, 22}, t, th::textSecondary, th::control);
            cx += cw + 6;
        };
        wchar_t b[64];
        swprintf_s(b, L"%d×%d", frames[0].img->w, frames[0].img->h);
        chip(b);
        chip(std::to_wstring(frames.size()) + L" 프레임");
        swprintf_s(b, L"%.2f초", tot / 1e6);
        chip(b);
        y += 32;
        g.card({x, y, w, 70}, th::radiusCard, false);
        std::wstring big, sub;
        float pv;
        if (saving) { big = L"저장하는 중이에요…"; pv = (float)saveProg; }
        else if (!est || !est->done) { big = L"예상 크기 계산 중…"; sub = L"고른 설정으로 실제로 인코딩해서 재요"; pv = est ? (float)est->prog : 0; }
        else if (est->err.empty()) { big = L"예상 크기  " + fmtBytes(est->bytes); sub = L"설정을 바꾸면 다시 계산해요"; pv = 1; }
        else if (est->err == "cancel") { big = L"예상 크기 계산 중…"; pv = 0; }
        else { big = L"예상 크기를 계산하지 못했어요"; sub = widen(est->err); pv = 0; }
        gx.text(big, {x + 16, y + 10, w - 32, 26}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
        if (!sub.empty()) gx.text(sub, {x + 16, y + 36, w - 32, 16}, th::textTertiary, th::caption);
        g.progress({x + 16, y + 56, w - 32, 4}, pv);
        y += 82;

        RectF c = g.optionRow(x, y, w, L"저장 위치", L"", 340, 30);
        g.edit(gid("sv.dir"), {c.x, c.y, c.w - 38, c.h}, dir);
        if (g.iconButton(gid("sv.browse"), {c.r() - 32, c.y, 32, c.h}, ic::Folder)) {
            g.later([&] {
                BROWSEINFOW bi{};
                bi.hwndOwner = f.hwnd();
                bi.lpszTitle = L"저장할 폴더를 고르세요";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                if (PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi)) {
                    wchar_t p[MAX_PATH];
                    if (SHGetPathFromIDListW(pid, p)) dir = p;
                    CoTaskMemFree(pid);
                }
                f.gui.redraw();
            });
        }
        c = g.optionRow(x, y, w, L"파일 이름", L"", 340, 30);
        g.edit(gid("sv.name"), c, name);
        c = g.optionRow(x, y, w, L"형식", L"", 220, 32);
        g.dropdown(gid("sv.fmt"), c, {L"GIF", L"WebP", L"APNG", L"MP4 (H.264)", L"WebM (VP9·VP8)", L"AVIF", L"PNG 시퀀스", L"JPG 시퀀스"}, fmtUi);
        int fmt = kUiToFormat[fmtUi];
        bool isGif = fmt == FMT_GIF, isWebp = fmt == FMT_WEBP, isApng = fmt == FMT_APNG, isMp4 = fmt == FMT_MP4, isWebm = fmt == FMT_WEBM,
             isAvif = fmt == FMT_AVIF, isSeq = fmt == FMT_PNGSEQ || fmt == FMT_JPGSEQ;
        bool qUsed = !isApng && fmt != FMT_PNGSEQ;
        std::wstring qdesc = isGif ? L"GIF는 최대 " + std::to_wstring(colorsForQuality(quality)) + L"색까지 써요"
                           : isWebp ? (webpLossless ? L"무손실이면 압축 노력 정도로 쓰여요" : L"높을수록 선명하고 파일이 커져요")
                           : isApng || fmt == FMT_PNGSEQ ? L"무손실이라 화질과 관계없어요"
                           : isAvif ? L"AV1 화질 " + std::to_wstring(avifQualityFor(quality)) + (quality >= 74 ? L" · 4:4:4" : quality >= 50 ? L" · 4:2:2" : L" · 4:2:0")
                           : isWebm ? L"최소 양자화 " + std::to_wstring(63 * (100 - quality) / 100) + L"/63"
                           : fmt == FMT_JPGSEQ ? L"JPG 화질이에요"
                                               : L"높을수록 비트레이트가 올라가요";
        if (isSeq) qdesc += L" · 파일 이름 뒤에 _0001처럼 번호가 붙어요";
        c = g.optionRow(x, y, w, L"화질", qdesc, 230, 30, qUsed);
        g.slider(gid("sv.q"), {c.x, c.y, c.w - 40, c.h}, quality, 1, 100, qUsed);
        gx.text(std::to_wstring(quality), {c.r() - 34, c.y, 34, c.h}, g.dim(th::textPrimary, qUsed), th::body, 600, A_RIGHT);

        auto expander = [&](const wchar_t* title, bool& open, bool enabled, const wchar_t* why) {
            RectF h{x, y + 4, w, 30};
            Id id = gid("exp") ^ (Id)(uintptr_t)&open;
            if (g.interact(id, h)) open = !open;
            gx.text(title, {x, y + 4, 220, 30}, g.dim(th::accent, enabled), th::body, 600);
            gx.icon(open ? ic::ChevronUp : ic::ChevronDown, {x + gx.textWidth(title, th::body, 600) + 6, y + 4, 16, 30}, g.dim(th::accent, enabled), 9);
            if (!enabled && why) gx.text(why, {x + 220, y + 4, w - 220, 30}, th::textTertiary, th::caption, 400, A_RIGHT);
            y += 36;
        };
        auto opt = [&](const std::wstring& t, const std::wstring& d, bool& v, bool en, bool sep = true) {
            RectF rc = g.optionRow(x + 18, y, w - 18, t, d, 40, 20, en, sep);
            g.toggle(gid("tg") ^ (Id)(uintptr_t)&v, rc, v, en);
        };
        auto accentLine = [&](float top, bool en) { gx.fillV({x + 4, top + 6, 2, y - top - 12}, 1, g.dim(th::accent, en), g.dim(th::accentEnd, en)); };

        expander(L"GIF 자세히", gifOpen, isGif, L"GIF로 저장할 때만 써요");
        if (gifOpen) {
            float top = y;
            RectF rc = g.optionRow(x + 18, y, w - 18, L"색 줄이는 방식", L"LIQ가 가장 곱고, NeuQuant·Octree는 빠르거나 다른 느낌이에요", 170, 30, isGif);
            g.dropdown(gid("sv.method"), rc, {L"LIQ (기본)", L"NeuQuant", L"Octree", L"분산 분할"}, method, isGif);
            opt(L"디더링", L"색이 적어도 부드럽게 보이게 점을 섞어요", dither, isGif);
            opt(L"가운데에 색 더 쓰기", L"화면 가운데의 색을 더 정확하게 담아요", center, isGif && (method == QM_LIQ || method == QM_WU));
            opt(L"비슷한 픽셀 지우기", L"앞 프레임과 거의 같은 점은 비워 파일을 줄여요", similar, isGif);
            opt(L"256색 이하면 그대로 저장", L"화질 100일 때 색 수가 적으면 색을 줄이지 않아요", skipQ, isGif);
            // colour protection (LIQ only)
            bool pen = isGif && method == QM_LIQ;
            rc = g.optionRow(x + 18, y, w - 18, L"색 보호", pen ? L"고른 색은 그대로 남겨요. 누르면 빠져요" : L"LIQ 방식에서만 쓸 수 있어요", 230, 26, pen);
            float sx = rc.x;
            for (size_t i = 0; i < protect.size() && i < 8; ++i) {
                RectF s{sx, rc.y + 2, 22, 22};
                if (g.interact(gid(gid("prot"), i), s, pen)) { size_t k = i; g.later([&, k] { protect.erase(protect.begin() + k); f.gui.redraw(); }); }
                gx.fill(s, 11, 0xFF000000u | protect[i]);
                gx.stroke(s, 11, th::separator, 1);
                sx += 26;
            }
            if (protect.size() < 8 && g.iconButton(gid("prot.add"), {sx, rc.y, 26, 26}, ic::Add, pen)) {
                g.later([&] {
                    static COLORREF custom[16];
                    CHOOSECOLORW ccw{sizeof ccw};
                    ccw.hwndOwner = f.hwnd();
                    ccw.lpCustColors = custom;
                    ccw.Flags = CC_FULLOPEN;
                    if (ChooseColorW(&ccw))
                        protect.push_back(((uint32_t)GetRValue(ccw.rgbResult) << 16) | ((uint32_t)GetGValue(ccw.rgbResult) << 8) | GetBValue(ccw.rgbResult));
                    f.gui.redraw();
                });
            }
            opt(L"파일에 프로그램 이름 남기기", L"GIF·WebP 주석에 BongGIF와 화질을 적어요", comment, isGif || isWebp, false);
            accentLine(top, isGif);
        }
        expander(L"WebP 자세히", webpOpen, isWebp, L"WebP로 저장할 때만 써요");
        if (webpOpen) {
            float top = y;
            opt(L"무손실", L"화질 손실 없이 저장해요. 파일이 커질 수 있어요", webpLossless, isWebp && !webpAuto);
            opt(L"손실·무손실 자동 판단", L"영상 두 곳을 시험 인코딩해 더 작은 쪽을 골라요", webpAuto, isWebp);
            opt(L"아이폰 호환", L"켜면 모든 프레임을 통째로 저장해요. 끄면 바뀐 부분만 저장해 훨씬 작아지지만 일부 아이폰에서 깨질 수 있어요", webpIphone, isWebp);
            opt(L"화질 우선", L"바뀐 부분만 저장할 때 블록을 투명하게 비워 화질을 지켜요", webpQual, isWebp && !webpIphone && !webpLossless, false);
            accentLine(top, isWebp);
        }
        expander(L"WebM 자세히", webmOpen, isWebm, L"WebM으로 저장할 때만 써요");
        if (webmOpen) {
            float top = y;
            RectF rc = g.optionRow(x + 18, y, w - 18, L"코덱", L"VP9가 더 작고, VP8은 오래된 기기에서도 잘 돌아가요", 170, 30, isWebm, false);
            g.segmented(gid("sv.codec"), rc, {L"VP9", L"VP8"}, codecSel, isWebm);
            accentLine(top, isWebm);
        }
        y += 6;
        gx.text(L"공통", {x, y, w, 24}, th::textPrimary, th::body, 600);
        y += 28;
        bool loopUsed = !isMp4 && !isWebm && !isSeq;
        c = g.optionRow(x, y, w, L"반복 횟수 정하기", L"끄면 끝없이 반복해요", 40, 20, loopUsed);
        g.toggle(gid("sv.loopf"), c, loopFinite, loopUsed);
        if (loopFinite && loopUsed) {
            c = g.optionRow(x, y, w, L"반복 횟수", L"", 140, 30);
            g.number(gid("sv.loop"), c, loopCount, 1, 65535);
        }
        bool ccUsed = isMp4 || isWebm || isAvif || (isWebp && !webpLossless);
        c = g.optionRow(x, y, w, L"색 압축", ccUsed ? L"비슷한 색을 묶어 파일을 5~20% 줄여요. 화질 80 이하에서만 동작해요" : L"MP4·WebM·AVIF·손실 WebP에서만 써요", 40, 20, ccUsed);
        g.toggle(gid("sv.cc"), c, cc, ccUsed);
        c = g.optionRow(x, y, w, L"진행 표시 넣기", L"화면 아래에 재생 위치 막대를 그려요", 40, 20);
        g.toggle(gid("sv.bar"), c, bar);
        c = g.optionRow(x, y, w, L"저장 후 폴더 열기", L"", 40, 20, true, true);
        g.toggle(gid("sv.open"), c, openFolder);

        expander(L"파일 용량 더 줄이기", sizeOpen, true, nullptr);
        if (sizeOpen) {
            float top = y;
            RectF rc = g.optionRow(x + 18, y, w - 18, L"이미지 크기", L"작게 줄이면 파일도 작아져요", 230, 30);
            g.slider(gid("sv.scale"), {rc.x, rc.y, rc.w - 48, rc.h}, scale, 10, 100);
            gx.text(std::to_wstring(scale) + L"%", {rc.r() - 44, rc.y, 44, rc.h}, th::textPrimary, th::body, 600, A_RIGHT);
            rc = g.optionRow(x + 18, y, w - 18, L"이미지 크기 상한 (px)", L"긴 변이 이보다 크면 줄여요. 0이면 제한 없어요", 150, 30);
            g.number(gid("sv.maxside"), rc, maxSide, 0, 8000);
            rc = g.optionRow(x + 18, y, w - 18, L"FPS 상한", L"0이면 제한하지 않아요", 150, 30);
            g.number(gid("sv.maxfps"), rc, maxFps, 0, 60);
            rc = g.optionRow(x + 18, y, w - 18, L"프레임 시간 상한 (ms)", L"한 장면이 너무 오래 머물지 않게 해요. 0이면 제한 없어요", 150, 30);
            g.number(gid("sv.maxms"), rc, maxFrameMs, 0, 60000);
            opt(L"비슷한 프레임 건너뛰기", L"거의 같은 프레임은 하나로 합쳐요 (재생 시간은 그대로)", skipSim, true, false);
            accentLine(top, true);
        }
        return y;
    };
    f.tickMs = 80;
    f.onTick = [&] {
        std::wstring sig = signature();
        if (sig != estSig) { estSig = sig; debounce = 5; }
        if (debounce > 0 && --debounce == 0 && !saving) startEstimate();
        if (saving && saveDone) {
            saving = false;
            saveTh.join();
            if (!saveErr.empty()) { messageBox(f.hwnd(), widen(saveErr), false, true); f.okEnabled = true; return; }
            if (openFolder) {
                std::wstring arg = L"/select,\"" + path + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
            }
            f.close(true);
        }
    };
    f.onOk = [&]() -> bool {
        if (saving) return false;
        ExportOptions o = options();
        std::wstring nm = name.empty() ? L"BongGIF" : name;
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        path = dir + L"\\" + nm + kFormatExt[o.format];
        std::wstring probe = path;
        if (o.format == FMT_PNGSEQ || o.format == FMT_JPGSEQ) probe = dir + L"\\" + nm + L"_0001" + kFormatExt[o.format];
        if (GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES &&
            !messageBox(f.hwnd(), probe + L"\n같은 이름의 파일이 있어요. 덮어쓸까요?", true))
            return false;
        remember();
        est.reset();
        saving = true;
        saveDone = false;
        f.okEnabled = false;
        const Frames* fr = &frames;
        std::wstring p = path;
        saveTh = std::thread([&, o, fr, p] {
            size_t n = 0;
            std::string err;
            bool ok = exportFrames(*fr, o, p, n, [&](double v) { saveProg = v; }, {}, err);
            saveErr = ok ? "" : (err.empty() ? "저장하지 못했어요" : err);
            outBytes = n;
            saveDone = true;
        });
        return false;
    };
    estSig = signature();
    startEstimate();
    bool ok = f.run();
    if (saveTh.joinable()) saveTh.join();
    est.reset();
    if (!ok) { remember(); return false; }
    savedPath = path;
    return saveErr.empty();
}

} // namespace bg
