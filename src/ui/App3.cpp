// Pages beyond the editor:
// home, file manager, batch convert, settings — plus the side bar navigation.
#include "ui/App.h"

#include "core/Apng.h"
#include "core/Gif.h"
#include "core/Psd.h"
#include "platform/Codecs.h"
#include "platform/Export.h"
#include "platform/Media.h"
#include "platform/Webp.h"
#include "ui/Dialogs.h"
#include "ui/Lang.h"

#include <algorithm>
#include <commdlg.h>
#include <ctime>
#include <shellapi.h>
#include <shlobj.h>

namespace bg {

static const float kSidebarW = 232, kTitleH = 44;
static uint32_t Al(uint32_t c, float a) { return ((uint32_t)std::lround(((c >> 24) & 255) * a) << 24) | (c & 0xFFFFFF); }

static bool isMediaExt(const std::wstring& p) {
    size_t d = p.find_last_of(L'.');
    if (d == std::wstring::npos) return false;
    std::wstring e = p.substr(d);
    for (auto& c : e) c = (wchar_t)towlower(c);
    static const wchar_t* exts[] = {L".gif", L".webp", L".png", L".apng", L".avif", L".mp4", L".webm", L".avi", L".mov", L".mkv", L".wmv", L".m4v",
                                    L".jpg", L".jpeg", L".bmp", L".tif", L".tiff", L".psd", L".svg", L".jxl"};
    for (auto x : exts) if (e == x) return true;
    return false;
}
static bool isVideoExt(const std::wstring& p) {
    size_t d = p.find_last_of(L'.');
    std::wstring e = d == std::wstring::npos ? L"" : p.substr(d);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e == L".mp4" || e == L".webm" || e == L".avi" || e == L".mov" || e == L".mkv" || e == L".wmv" || e == L".m4v";
}

// Shared loader for the file manager preview and batch conversion
bool App::loadForPage(const std::wstring& p, Frames& out, std::string& e, double maxSeconds, int maxSide, const Cancel& cancel) {
    if (isVideoExt(p)) return loadVideo(p, 0, maxSeconds, st.videoMaxFps, maxSide, out, {}, cancel, e);
    std::vector<uint8_t> data;
    if (!readFile(p, data)) { e = "파일을 읽을 수 없어요"; return false; }
    if (data.size() > 6 && !memcmp(data.data(), "GIF", 3)) return decodeGif(data, out, e);
    if (data.size() > 12 && !memcmp(data.data(), "RIFF", 4) && !memcmp(data.data() + 8, "WEBP", 4)) return decodeWebp(data, out, e);
    if (data.size() > 8 && data[0] == 137 && data[1] == 'P' && decodePng(data, out, e)) return true;
    if (data.size() > 12 && !memcmp(data.data() + 4, "ftyp", 4) && (!memcmp(data.data() + 8, "avif", 4) || !memcmp(data.data() + 8, "avis", 4)))
        return decodeAvif(data, out, e);
    if (data.size() > 4 && !memcmp(data.data(), "8BPS", 4)) {
        Image im;
        if (!decodePsd(data, im, e)) return false;
        out.push_back({std::make_shared<Image>(std::move(im)), st.importFrameMs * 1000LL});
        return true;
    }
    size_t d = p.find_last_of(L'.');
    if (d != std::wstring::npos && _wcsicmp(p.c_str() + d, L".svg") == 0) {
        Image im;
        if (!loadSvg(p, 2048, im, e)) return false;
        out.push_back({std::make_shared<Image>(std::move(im)), st.importFrameMs * 1000LL});
        return true;
    }
    out.clear();
    return loadFramesWic(p, out, st.importFrameMs, e);
}

void App::addRecent(const std::wstring& p) {
    std::vector<std::wstring> list;
    size_t a = 0;
    while (a < st.recent.size()) {
        size_t b = st.recent.find(L'|', a);
        std::wstring s = st.recent.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
        if (!s.empty() && _wcsicmp(s.c_str(), p.c_str()) != 0) list.push_back(s);
        if (b == std::wstring::npos) break;
        a = b + 1;
    }
    list.insert(list.begin(), p);
    if (list.size() > 10) list.resize(10);
    st.recent.clear();
    for (auto& s : list) { if (!st.recent.empty()) st.recent += L"|"; st.recent += s; }
    st.save();
}

std::vector<std::wstring> App::recentList() const {
    std::vector<std::wstring> list;
    size_t a = 0;
    while (a < st.recent.size()) {
        size_t b = st.recent.find(L'|', a);
        std::wstring s = st.recent.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
        if (!s.empty() && GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES) list.push_back(s);
        if (b == std::wstring::npos) break;
        a = b + 1;
    }
    return list;
}

// ------------------------------------------------------------------ side bar
void App::drawSidebar(float H) {
    Gfx& g = gui_.g;
    g.fill({0, 0, kSidebarW, H}, 0, th::sidebar);
    g.line(kSidebarW, 0, kSidebarW, H, th::separator, 1);
    gui_.iconTile({18, 16, 36, 36}, ic::Film, th::accent, th::accentEnd);
    g.text(L"BongGIF", {64, 14, 100, 22}, th::textPrimary, th::sectionTitle, 700, A_LEFT, true, false, true);
    float vw = g.textWidth(L"v" + widen(kAppVersion), th::caption - 1, 600) + 12;
    gui_.chip({64 + g.textWidth(L"BongGIF", th::sectionTitle, 700, true) + 8, 17, vw, 17}, L"v" + widen(kAppVersion), th::textSecondary, th::control);
    g.text(L"움짤 녹화·편집기", {64, 36, 150, 18}, th::textSecondary, th::caption);

    float x = 14, w = kSidebarW - 28, y = 72;
    if (gui_.button(gid("sb.rec"), {x, y, w, 40}, L"녹화 시작", BS_PRIMARY, ic::Record)) gui_.later([this] { openRecorder(false); });
    y += 50;
    struct Nav { const char* id; wchar_t icon; uint32_t c1, c2; const wchar_t* t; int page; };
    Nav nav[] = {{"nav.home", ic::Home, th::accent, th::purple, L"홈", P_HOME},
                 {"nav.edit", ic::Edit, th::cyan, th::accent, L"편집", P_EDIT},
                 {"nav.manage", ic::Folder, th::orange, th::yellow, L"파일 관리", P_MANAGE},
                 {"nav.batch", ic::Sync, th::green, th::cyan, L"일괄 변환", P_BATCH}};
    for (auto& n : nav) {
        int pg = n.page;
        if (gui_.navItem(gid(n.id), {x, y, w, 38}, n.icon, n.c1, n.c2, n.t, page_ == pg)) gui_.later([this, pg] { setPage(pg); });
        y += 40;
    }
    float bottomY = H - 104;
    if (page_ == P_EDIT) {
        y += 8;
        g.text(L"프레임 도구", {x + 8, y, w, 20}, th::textTertiary, th::caption, 600);
        y += 22;
        float y0 = gui_.beginScroll(sideScroll_, {0, y, kSidebarW, bottomY - y - 6});
        float yy = y0;
        bool has = !proj.frames.empty();
        struct Item { const char* id; wchar_t icon; uint32_t c1, c2; const wchar_t* t; std::function<void()> fn; };
        Item items[] = {
            {"sb.time", ic::Clock, th::cyan, th::accent, L"시간 설정", [this] { cmdSetTime(false); }},
            {"sb.fast", ic::Fast, th::green, th::cyan, L"10% 빠르게", [this] { cmdSpeed(0.9); }},
            {"sb.slow", ic::Slow, th::green, th::cyan, L"10% 느리게", [this] { cmdSpeed(1.1); }},
            {"sb.rev", ic::Reverse, th::purple, th::pink, L"거꾸로 재생", [this] { if (needFrames()) apply(reverse(proj.frames), L"거꾸로 재생하게 바꿨어요"); }},
            {"sb.yoyo", ic::Repeat, th::purple, th::pink, L"요요 (왕복)", [this] { if (needFrames()) apply(yoyo(proj.frames), L"되돌아오는 프레임을 붙였어요"); }},
            {"sb.trim", ic::Trim, th::orange, th::red, L"구간 남기기", [this] { cmdTrim(); }},
            {"sb.reduce", ic::Reduce, th::orange, th::red, L"프레임 줄이기", [this] { cmdReduce(false); }},
            {"sb.summary", ic::Summary, th::accent, th::cyan, L"비디오 요약", [this] { cmdSummary(); }},
        };
        for (auto& it : items) {
            if (gui_.navItem(gid(it.id), {x, yy, w, 36}, it.icon, it.c1, it.c2, it.t, false, has)) gui_.later(it.fn);
            yy += 38;
        }
        gui_.endScroll(sideScroll_, yy);
    }
    if (gui_.navItem(gid("nav.settings"), {x, bottomY, w, 38}, ic::Settings, th::textTertiary, th::textSecondary, L"설정", page_ == P_SETTINGS))
        gui_.later([this] { setPage(P_SETTINGS); });
    g.text(hotkeyName(st.hotkeyRec) + L"  " + tr(L"녹화 시작·정지") + L"\n" + hotkeyName(st.hotkeyPause) + L"  " + tr(L"일시정지"),
           {x + 8, H - 54, w, 40}, th::textTertiary, th::caption, 400, A_LEFT, false, true);
}

void App::setPage(int p) {
    if (p != P_EDIT) { stopPlay(); }
    page_ = p;
    if (p == P_MANAGE) refreshManage();
    if (p != P_MANAGE) { KillTimer(hwnd, 13); managePreview_.clear(); }
    gui_.redraw();
}

void App::pageHeader(const std::wstring& title, const std::wstring& desc) {
    Gfx& g = gui_.g;
    float cx = kSidebarW + 24;
    g.text(title, {cx, 10, 600, 34}, th::textPrimary, th::pageTitle, 600, A_LEFT, true, false, true);
    if (!desc.empty()) g.text(desc, {cx, 44, 800, 20}, th::textSecondary, th::body);
}

// ------------------------------------------------------------------ home
void App::drawHome(float W, float H) {
    Gfx& g = gui_.g;
    pageHeader(L"홈", L"무엇을 할까요?");
    float cx = kSidebarW + 24, cw = W - cx - 24, y = 84;
    struct C { const char* id; wchar_t icon; uint32_t c1, c2; const wchar_t* t; const wchar_t* d; std::function<void()> fn; };
    C cards[] = {
        {"home.rec", ic::Record, th::red, th::pink, L"새로 녹화", L"화면의 원하는 부분을 움짤로 녹화해요", [this] { openRecorder(false); }},
        {"home.open", ic::Open, th::accent, th::purple, L"편집 (파일 열기)", L"GIF·WebP·APNG·AVIF·영상을 열어 고쳐요", [this] { openDialog(); }},
        {"home.import", ic::Photo, th::orange, th::yellow, L"이미지 가져오기", L"여러 장의 사진을 이어 움짤로 만들어요", [this] { importImages(); }},
        {"home.manage", ic::Folder, th::green, th::cyan, L"파일 관리", L"저장한 움짤을 보고 이름을 바꾸거나 지워요", [this] { setPage(P_MANAGE); }},
        {"home.batch", ic::Sync, th::cyan, th::accent, L"일괄 변환", L"여러 파일을 한 번에 다른 형식으로 바꿔요", [this] { setPage(P_BATCH); }},
    };
    int cols = cw > 900 ? 3 : 2;
    float gap = 14, cardW = (cw - gap * (cols - 1)) / cols, cardH = 112;
    for (int i = 0; i < 5; ++i) {
        RectF r{cx + (i % cols) * (cardW + gap), y + (i / cols) * (cardH + gap), cardW, cardH};
        Id id = gid(cards[i].id);
        bool click = gui_.interact(id, r);
        float t = gui_.hoverT(id);
        gui_.card(r, th::radiusPanel);
        if (t > 0) g.fill(r.inset(1), th::radiusPanel, Al(0x14FFFFFF, t));
        gui_.iconTile({r.x + 18, r.y + 20, 44, 44}, cards[i].icon, cards[i].c1, cards[i].c2);
        g.text(cards[i].t, {r.x + 76, r.y + 22, r.w - 90, 24}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
        g.text(cards[i].d, {r.x + 76, r.y + 50, r.w - 90, 40}, th::textSecondary, th::caption, 400, A_LEFT, false, true);
        if (click) gui_.later(cards[i].fn);
    }
    y += ((5 + cols - 1) / cols) * (cardH + gap) + 12;
    auto recent = recentList();
    g.text(L"최근 파일", {cx, y, 300, 24}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
    y += 32;
    if (recent.empty()) {
        g.text(L"아직 연 파일이 없어요", {cx, y, cw, 22}, th::textTertiary, th::body);
        return;
    }
    RectF box{cx, y, cw, std::min((float)recent.size() * 40 + 8, H - y - 24)};
    gui_.card(box, th::radiusPanel);
    float ry = y + 4;
    for (size_t i = 0; i < recent.size() && ry + 40 <= box.b(); ++i) {
        RectF r{cx + 6, ry, cw - 12, 38};
        Id id = gid(gid("recent"), i);
        if (gui_.interact(id, r)) { std::wstring p = recent[i]; gui_.later([this, p] { if (confirmDiscard()) { openPath(p, false); setPage(P_EDIT); } }); }
        float t = gui_.hoverT(id);
        if (t > 0) g.fill(r, 8, Al(th::control, t));
        size_t sl = recent[i].find_last_of(L"\\/");
        g.icon(isVideoExt(recent[i]) ? ic::Film : ic::Photo, {r.x + 6, r.y, 28, r.h}, th::textSecondary, 13);
        g.text(recent[i].substr(sl + 1), {r.x + 40, r.y, r.w * 0.45f, r.h}, th::textPrimary, th::body);
        g.text(recent[i].substr(0, sl), {r.x + r.w * 0.45f + 48, r.y, r.w * 0.55f - 56, r.h}, th::textTertiary, th::caption);
        ry += 40;
    }
}

// ------------------------------------------------------------------ file manager
void App::refreshManage() {
    manageFiles_.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((st.saveDir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            if (!isMediaExt(name)) continue;
            ManageFile f;
            f.name = name;
            f.path = st.saveDir + L"\\" + name;
            f.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            f.time = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            manageFiles_.push_back(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    sortManage();
    manageSel_ = manageFiles_.empty() ? -1 : std::min(std::max(manageSel_, 0), (int)manageFiles_.size() - 1);
    loadManagePreview();
}

void App::sortManage() {
    std::sort(manageFiles_.begin(), manageFiles_.end(), [this](const ManageFile& a, const ManageFile& b) {
        switch (st.manageSort) {
        case 0: return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        case 2: return a.size > b.size;
        default: return a.time > b.time;
        }
    });
}

void App::loadManagePreview() {
    KillTimer(hwnd, 13);
    managePreview_.clear();
    manageCur_ = 0;
    if (manageSel_ < 0 || manageSel_ >= (int)manageFiles_.size()) return;
    std::wstring p = manageFiles_[manageSel_].path;
    Frames f;
    std::string e;
    // a short preview is enough (videos: first 3 seconds at reduced size)
    if (loadForPage(p, f, e, 3.0, 640, {})) {
        if (f.size() > 400) f.resize(400);
        managePreview_ = std::move(f);
        SetTimer(hwnd, 13, (UINT)std::max<int64_t>(10, managePreview_[0].dur / 1000), nullptr);
    }
}

void App::drawManage(float W, float H) {
    Gfx& g = gui_.g;
    pageHeader(L"파일 관리", st.saveDir);
    float cx = kSidebarW + 24, cw = W - cx - 24, y = 76;
    // toolbar
    RectF bar{cx, y, cw, 44};
    gui_.card(bar, th::radiusPanel, false);
    float bx = bar.x + 8, by = bar.y + 6;
    bool sel = manageSel_ >= 0 && manageSel_ < (int)manageFiles_.size();
    auto tb = [&](const char* id, const wchar_t* t, wchar_t icon, bool en, std::function<void()> fn) {
        float w = g.textWidth(tr(t), th::body, 500) + 46;
        if (gui_.button(gid(id), {bx, by, w, 32}, t, BS_GHOST, icon, en)) gui_.later(std::move(fn));
        bx += w + 4;
    };
    tb("mg.edit", L"편집", ic::Edit, sel, [this] {
        std::wstring p = manageFiles_[manageSel_].path;
        if (confirmDiscard()) { openPath(p, false); setPage(P_EDIT); }
    });
    tb("mg.rename", L"이름 바꾸기", ic::Rename, sel, [this] {
        ManageFile f = manageFiles_[manageSel_];
        size_t dot = f.name.find_last_of(L'.');
        std::wstring base = f.name.substr(0, dot), ext = dot == std::wstring::npos ? L"" : f.name.substr(dot);
        Form fm(hwnd, L"이름 바꾸기", 420);
        fm.body = [&](Gui& gg, float x, float yy, float w) {
            RectF c = gg.optionRow(x, yy, w, L"새 이름", ext, 260, 30, true, false);
            gg.edit(gid("mg.newname"), c, base);
            return yy;
        };
        if (!fm.run() || base.empty()) return;
        std::wstring np = st.saveDir + L"\\" + base + ext;
        if (!MoveFileW(f.path.c_str(), np.c_str())) messageBox(hwnd, L"이름을 바꾸지 못했어요. 같은 이름이 있거나 쓰는 중이에요.", false, true);
        refreshManage();
    });
    tb("mg.del", L"삭제", ic::Delete, sel, [this] {
        ManageFile f = manageFiles_[manageSel_];
        if (!messageBox(hwnd, f.name + L"\n" + tr(st.deleteToRecycle ? L"휴지통으로 보낼까요?" : L"완전히 지울까요? 되돌릴 수 없어요."), true)) return;
        KillTimer(hwnd, 13);
        managePreview_.clear();
        if (st.deleteToRecycle) {
            std::wstring from = f.path + L'\0';
            SHFILEOPSTRUCTW op{};
            op.hwnd = hwnd;
            op.wFunc = FO_DELETE;
            op.pFrom = from.c_str();
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
            SHFileOperationW(&op);
        } else {
            DeleteFileW(f.path.c_str());
        }
        refreshManage();
    });
    tb("mg.reveal", L"폴더에서 보기", ic::Folder, true, [this] {
        std::wstring arg = manageSel_ >= 0 && manageSel_ < (int)manageFiles_.size() ? L"/select,\"" + manageFiles_[manageSel_].path + L"\"" : L"\"" + st.saveDir + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    });
    tb("mg.dir", L"폴더 바꾸기", ic::Open, true, [this] {
        BROWSEINFOW bi{};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = L"보여 줄 폴더";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        if (PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi)) {
            wchar_t p[MAX_PATH];
            if (SHGetPathFromIDListW(pid, p)) { st.saveDir = p; st.save(); }
            CoTaskMemFree(pid);
        }
        refreshManage();
    });
    tb("mg.refresh", L"새로 고침", ic::Reset, true, [this] { refreshManage(); });
    y += 56;
    float listW = std::max(420.f, cw * 0.56f);
    RectF list{cx, y, listW, H - y - 24};
    gui_.card(list, th::radiusPanel);
    // header (click to sort)
    const wchar_t* cols[] = {L"이름", L"날짜", L"크기"};
    float colX[] = {list.x + 14, list.x + list.w - 260, list.x + list.w - 96};
    for (int i = 0; i < 3; ++i) {
        int key = i == 0 ? 0 : i == 1 ? 1 : 2;
        RectF hr{colX[i] - 4, list.y + 8, i == 0 ? list.w - 280 : 100, 24};
        if (gui_.interact(gid(gid("mg.sort"), i), hr)) { st.manageSort = key; st.save(); sortManage(); }
        g.text(cols[i], hr, st.manageSort == key ? th::accent : th::textTertiary, th::caption, 600);
    }
    gui_.separator(list.x + 10, list.r() - 10, list.y + 36);
    RectF view{list.x + 4, list.y + 40, list.w - 8, list.h - 44};
    float y0 = gui_.beginScroll(manageScroll_, view);
    for (size_t i = 0; i < manageFiles_.size(); ++i) {
        RectF r{view.x + 4, y0 + i * 34.f, view.w - 14, 32};
        if (r.b() < view.y || r.y > view.b()) continue;
        Id id = gid(gid("mg.row"), i);
        if (gui_.interact(id, r)) { int k = (int)i; gui_.later([this, k] { manageSel_ = k; loadManagePreview(); gui_.redraw(); }); }
        float t = gui_.hoverT(id);
        if ((int)i == manageSel_) g.fill(r, 6, th::control);
        else if (t > 0) g.fill(r, 6, Al(th::control, t * 0.7f));
        auto& f = manageFiles_[i];
        g.icon(isVideoExt(f.name) ? ic::Film : ic::Photo, {r.x + 2, r.y, 24, r.h}, th::textSecondary, 12);
        g.text(f.name, {r.x + 28, r.y, colX[1] - r.x - 40, r.h}, th::textPrimary, th::body);
        FILETIME ft{(DWORD)f.time, (DWORD)(f.time >> 32)}, lt;
        SYSTEMTIME s;
        FileTimeToLocalFileTime(&ft, &lt);
        FileTimeToSystemTime(&lt, &s);
        wchar_t d[40];
        swprintf_s(d, L"%04d-%02d-%02d %02d:%02d", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute);
        g.text(d, {colX[1] - 4, r.y, 160, r.h}, th::textSecondary, th::caption);
        g.text(fmtBytes((size_t)f.size), {colX[2] - 4, r.y, 86, r.h}, th::textSecondary, th::caption, 400, A_RIGHT);
    }
    if (manageFiles_.empty()) g.text(L"이 폴더에 움짤이 없어요", {view.x + 10, view.y + 10, view.w - 20, 24}, th::textTertiary, th::body);
    gui_.endScroll(manageScroll_, y0 + manageFiles_.size() * 34.f + 4);
    // preview
    RectF pv{list.r() + 14, y, cw - listW - 14, H - y - 24};
    gui_.card(pv, th::radiusPanel);
    RectF well = pv.inset(10);
    g.fill(well, th::radiusCard, th::canvasWell);
    if (!managePreview_.empty()) {
        const ImagePtr& im = managePreview_[std::min(manageCur_, (int)managePreview_.size() - 1)].img;
        float sc = std::min({(well.w - 16) / im->w, (well.h - 60) / im->h, 1.f / gui_.s()});
        RectF dst{well.x + (well.w - im->w * sc) / 2, well.y + (well.h - 44 - im->h * sc) / 2, im->w * sc, im->h * sc};
        g.image(im, dst, true);
        if (sel) {
            auto& f = manageFiles_[manageSel_];
            wchar_t info[160];
            swprintf_s(info, L"%d×%d  ·  %s%s", im->w, im->h, fmtBytes((size_t)f.size).c_str(), isVideoExt(f.name) ? L"  ·  미리보기 3초" : L"");
            g.text(info, {well.x, well.b() - 34, well.w, 24}, th::textSecondary, th::caption, 400, A_CENTER);
        }
    } else if (sel) {
        g.text(L"미리보기를 만들지 못했어요", well, th::textTertiary, th::body, 400, A_CENTER);
    }
}

// ------------------------------------------------------------------ batch conversion
void App::batchAddFiles(const std::vector<std::wstring>& files) {
    std::lock_guard<std::mutex> lk(batchMu_);
    for (auto& f : files) {
        if (!isMediaExt(f)) continue;
        BatchItem it;
        it.path = f;
        it.name = f.substr(f.find_last_of(L"\\/") + 1);
        batch_.push_back(it);
    }
}

void App::startBatch() {
    if (batchRunning_) { batchCancel_ = true; return; }
    if (batchThread_.joinable()) batchThread_.join();
    batchCancel_ = false;
    batchRunning_ = true;
    ExportOptions o;
    o.format = st.batchFormat;
    o.quality = st.quality;
    o.jpgQuality = st.jpgQuality;
    o.gif.method = st.gifMethod;
    o.gif.dither = st.dither;
    o.gif.centerWeighted = st.centerWeighted;
    o.gif.removeSimilar = st.removeSimilar;
    o.gif.skipQuantQ100 = st.skipQuant100;
    o.gif.comment = st.comment;
    o.webp.lossless = st.webpLossless;
    o.webp.autoDecide = st.webpAutoDecide;
    o.webp.iphoneCompatible = st.webpIphoneCompatible;
    o.webp.qualityFirst = st.webpQualityFirst;
    o.webmVp9 = st.webmVp9;
    o.colorCompress = st.colorCompress;
    o.scalePct = st.batchScale;
    o.maxFps = st.batchMaxFps;
    o.loop = st.loopFinite ? st.loopCount : 0;
    std::wstring outDir = st.batchSameDir ? L"" : st.batchDir;
    {
        std::lock_guard<std::mutex> lk(batchMu_);
        for (auto& b : batch_) if (b.state != 2) { b.state = 0; b.progress = 0; b.err.clear(); }
    }
    batchThread_ = std::thread([this, o, outDir] {
        for (size_t i = 0;; ++i) {
            BatchItem it;
            {
                std::lock_guard<std::mutex> lk(batchMu_);
                if (i >= batch_.size()) break;
                if (batch_[i].state == 2) continue;
                batch_[i].state = 1;
                it = batch_[i];
            }
            if (batchCancel_) break;
            InvalidateRect(hwnd, nullptr, FALSE);
            Frames f;
            std::string e;
            bool ok = loadForPage(it.path, f, e, 0, st.videoMaxSide, [this] { return batchCancel_.load(); });
            std::wstring dir = outDir.empty() ? it.path.substr(0, it.path.find_last_of(L"\\/")) : outDir;
            std::wstring base = it.name.substr(0, it.name.find_last_of(L'.'));
            std::wstring out = dir + L"\\" + base + kFormatExt[o.format];
            if (ok && _wcsicmp(out.c_str(), it.path.c_str()) == 0) out = dir + L"\\" + base + L" (BongGIF)" + kFormatExt[o.format];
            size_t bytes = 0;
            if (ok) {
                SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
                ok = exportFrames(f, o, out, bytes, [this, i](double v) {
                    std::lock_guard<std::mutex> lk(batchMu_);
                    if (i < batch_.size()) batch_[i].progress = (float)v;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }, [this] { return batchCancel_.load(); }, e);
            }
            {
                std::lock_guard<std::mutex> lk(batchMu_);
                if (i < batch_.size()) {
                    batch_[i].state = ok ? 2 : (batchCancel_ ? 0 : 3);
                    batch_[i].bytes = bytes;
                    batch_[i].err = widen(e);
                    batch_[i].progress = ok ? 1.f : 0.f;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        batchRunning_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    });
}

void App::drawBatch(float W, float H) {
    Gfx& g = gui_.g;
    pageHeader(L"일괄 변환", L"여러 파일을 한 번에 다른 형식으로 바꿔요. 파일을 끌어다 놓아도 돼요.");
    float cx = kSidebarW + 24, cw = W - cx - 24, y = 76;
    float optW = 340;
    RectF list{cx, y, cw - optW - 14, H - y - 24};
    gui_.card(list, th::radiusPanel);
    bool running = batchRunning_;
    float bx = list.x + 10;
    auto btn = [&](const char* id, const wchar_t* t, wchar_t icon, bool en, std::function<void()> fn) {
        float w = g.textWidth(tr(t), th::body, 500) + 46;
        if (gui_.button(gid(id), {bx, list.y + 10, w, 32}, t, BS_GHOST, icon, en)) gui_.later(std::move(fn));
        bx += w + 4;
    };
    btn("bt.add", L"파일 추가", ic::Add, !running, [this] {
        std::vector<wchar_t> buf(32768, 0);
        OPENFILENAMEW of{sizeof of};
        of.hwndOwner = hwnd;
        of.lpstrFilter = L"움짤·영상·이미지\0*.gif;*.webp;*.png;*.apng;*.avif;*.mp4;*.webm;*.avi;*.mov;*.mkv;*.wmv;*.m4v;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.psd;*.svg\0";
        of.lpstrFile = buf.data();
        of.nMaxFile = (DWORD)buf.size();
        of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
        if (!GetOpenFileNameW(&of)) return;
        std::vector<std::wstring> files;
        std::wstring dir = buf.data();
        const wchar_t* p = buf.data() + dir.size() + 1;
        if (!*p) files.push_back(dir);
        for (; *p; p += wcslen(p) + 1) files.push_back(dir + L"\\" + p);
        batchAddFiles(files);
        gui_.redraw();
    });
    btn("bt.folder", L"폴더 추가", ic::Folder, !running, [this] {
        BROWSEINFOW bi{};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = L"변환할 파일이 있는 폴더";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
        if (!pid) return;
        wchar_t d[MAX_PATH];
        bool ok = SHGetPathFromIDListW(pid, d);
        CoTaskMemFree(pid);
        if (!ok) return;
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(d) + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(std::wstring(d) + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        batchAddFiles(files);
        gui_.redraw();
    });
    btn("bt.clear", L"목록 비우기", ic::Delete, !running, [this] { std::lock_guard<std::mutex> lk(batchMu_); batch_.clear(); gui_.redraw(); });
    gui_.separator(list.x + 10, list.r() - 10, list.y + 52);
    RectF view{list.x + 4, list.y + 56, list.w - 8, list.h - 60};
    float y0 = gui_.beginScroll(batchScroll_, view);
    std::vector<BatchItem> items;
    {
        std::lock_guard<std::mutex> lk(batchMu_);
        items = batch_;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        RectF r{view.x + 6, y0 + i * 46.f, view.w - 16, 42};
        if (r.b() < view.y || r.y > view.b()) continue;
        auto& it = items[i];
        g.fill(r, 8, Al(th::control, 0.5f));
        g.icon(isVideoExt(it.name) ? ic::Film : ic::Photo, {r.x + 6, r.y, 26, r.h}, th::textSecondary, 13);
        g.text(it.name, {r.x + 38, r.y + 4, r.w - 200, 20}, th::textPrimary, th::body);
        std::wstring st2 = it.state == 0 ? tr(L"대기") : it.state == 1 ? tr(L"변환 중") : it.state == 2 ? tr(L"완료") + L" · " + fmtBytes(it.bytes) : tr(L"실패");
        uint32_t sc2 = it.state == 2 ? th::green : it.state == 3 ? th::red : it.state == 1 ? th::accent : th::textTertiary;
        g.text(st2, {r.r() - 160, r.y, 150, r.h}, sc2, th::caption, 600, A_RIGHT);
        if (it.state == 3 && !it.err.empty()) g.text(it.err, {r.x + 38, r.y + 22, r.w - 200, 16}, th::textTertiary, th::caption);
        if (it.state == 1) gui_.progress({r.x + 38, r.y + 30, r.w - 210, 4}, it.progress);
    }
    if (items.empty()) g.text(L"파일 추가나 폴더 추가를 누르거나 파일을 여기로 끌어다 놓아요", {view.x + 12, view.y + 12, view.w - 24, 24}, th::textTertiary, th::body);
    gui_.endScroll(batchScroll_, y0 + items.size() * 46.f + 4);

    // options card
    RectF opt{list.r() + 14, y, optW, H - y - 24};
    gui_.card(opt, th::radiusPanel);
    float ox = opt.x + 16, ow = opt.w - 32, oy = opt.y + 12;
    g.text(L"변환 설정", {ox, oy, ow, 24}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
    oy += 34;
    RectF c = gui_.optionRow(ox, oy, ow, L"형식", L"", 170, 30, !running);
    gui_.dropdown(gid("bt.fmt"), c, {L"GIF", L"APNG", L"MP4 (H.264)", L"WebP", L"WebM", L"AVIF", L"PNG 시퀀스", L"JPG 시퀀스"}, st.batchFormat, !running);
    c = gui_.optionRow(ox, oy, ow, L"화질", L"", 170, 30, !running);
    gui_.slider(gid("bt.q"), {c.x, c.y, c.w - 36, c.h}, st.quality, 1, 100, !running);
    g.text(std::to_wstring(st.quality), {c.r() - 32, c.y, 32, c.h}, th::textPrimary, th::body, 600, A_RIGHT);
    c = gui_.optionRow(ox, oy, ow, L"이미지 크기", L"", 170, 30, !running);
    gui_.slider(gid("bt.scale"), {c.x, c.y, c.w - 44, c.h}, st.batchScale, 10, 100, !running);
    g.text(std::to_wstring(st.batchScale) + L"%", {c.r() - 42, c.y, 42, c.h}, th::textPrimary, th::body, 600, A_RIGHT);
    c = gui_.optionRow(ox, oy, ow, L"FPS 상한", L"0이면 제한 없어요", 130, 30, !running);
    gui_.number(gid("bt.fps"), c, st.batchMaxFps, 0, 60, !running);
    c = gui_.optionRow(ox, oy, ow, L"원본과 같은 폴더에 저장", L"", 40, 20, !running);
    gui_.toggle(gid("bt.same"), c, st.batchSameDir, !running);
    if (!st.batchSameDir) {
        c = gui_.optionRow(ox, oy, ow, L"저장 폴더", L"", 200, 30, !running);
        gui_.edit(gid("bt.dir"), {c.x, c.y, c.w - 36, c.h}, st.batchDir, !running);
        if (gui_.iconButton(gid("bt.browse"), {c.r() - 32, c.y, 32, c.h}, ic::Folder, !running)) {
            gui_.later([this] {
                BROWSEINFOW bi{};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"저장 폴더";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                if (PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi)) {
                    wchar_t p[MAX_PATH];
                    if (SHGetPathFromIDListW(pid, p)) st.batchDir = p;
                    CoTaskMemFree(pid);
                }
                gui_.redraw();
            });
        }
    }
    {
        const wchar_t* note = L"GIF·WebP 세부 설정은 설정 페이지의 GIF·WebP 항목을 따라요.";
        float nh = g.textHeight(note, th::caption, ow);
        g.text(note, {ox, oy + 4, ow, nh + 4}, th::textTertiary, th::caption, 400, A_LEFT, false, true);
        oy += nh + 18;
    }
    int done = 0, total = (int)items.size();
    for (auto& it : items) if (it.state == 2) ++done;
    gui_.progress({ox, oy, ow, 6}, total ? (float)done / total : 0.f);
    g.text(std::to_wstring(done) + L" / " + std::to_wstring(total), {ox, oy + 10, ow, 20}, th::textSecondary, th::caption, 400, A_RIGHT);
    oy += 36;
    if (gui_.button(gid("bt.start"), {ox, oy, ow, 38}, running ? L"멈추기" : L"변환 시작", running ? BS_DANGER : BS_PRIMARY, running ? ic::Stop : ic::Play, total > 0))
        gui_.later([this] { st.save(); startBatch(); gui_.redraw(); });
}

// ------------------------------------------------------------------ settings
void App::drawSettings(float W, float H) {
    Gfx& g = gui_.g;
    pageHeader(L"설정", L"");
    float cx = kSidebarW + 24, cw = W - cx - 24;
    static const wchar_t* cats[] = {L"일반", L"녹화", L"편집", L"단축키", L"GIF", L"WebP", L"WebM", L"MP4", L"APNG", L"AVIF", L"고급", L"언어"};
    const int nc = 12;
    // pill tab bar (scroll-spy)
    RectF bar{cx, 56, cw, 40};
    g.fill(bar, 20, th::menuBackground);
    g.stroke(bar, 20, th::menuStroke, 1);
    float tx = bar.x + 6, fs = th::body, pad = 24, total = 0;
    for (int i = 0; i < nc; ++i) total += g.textWidth(tr(cats[i]), fs, 600) + pad + 2;
    if (total > bar.w - 12) { pad = 14; total = 0; for (int i = 0; i < nc; ++i) total += g.textWidth(tr(cats[i]), fs, 600) + pad + 2; }
    if (total > bar.w - 12) fs = th::caption;
    for (int i = 0; i < nc; ++i) {
        float tw = g.textWidth(tr(cats[i]), fs, 600) + pad;
        RectF t{tx, bar.y + 5, tw, 30};
        Id id = gid(gid("set.tab"), i);
        if (gui_.interact(id, t)) { int k = i; gui_.later([this, k] { settingsJump_ = k; gui_.redraw(); }); }
        if (i == settingsActive_) g.fillH(t, 15, th::accent, th::accentEnd);
        else if (gui_.hoverT(id) > 0) g.fill(t, 15, Al(th::control, gui_.hoverT(id)));
        g.text(cats[i], t, i == settingsActive_ ? th::textPrimary : th::textSecondary, fs, 600, A_CENTER);
        tx += tw + 2;
        if (tx > bar.r() - 10) break;
    }
    RectF view{cx - 6, 106, cw + 12, H - 106 - 12};
    float y0 = gui_.beginScroll(settingsScroll_, view);
    float x = cx + 6, w = std::min(cw - 12, 860.f), y = y0;
    std::vector<float> sectionY;
    bool changed = false;
    auto section = [&](const wchar_t* title, const wchar_t* desc, wchar_t icon, uint32_t c1, uint32_t c2) {
        sectionY.push_back(y);
        gui_.iconTile({x, y + 4, 36, 36}, icon, c1, c2);
        g.text(title, {x + 48, y, w, 26}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
        g.text(desc, {x + 48, y + 24, w, 18}, th::textTertiary, th::caption);
        y += 52;
    };
    // a card that wraps rows; height from the previous frame (rows are drawn on top)
    std::map<int, float>& cardH = settingsCardH_;
    int cardNo = 0;
    float cardTop = 0;
    auto cardBegin = [&]() {
        cardTop = y;
        float h = cardH.count(cardNo) ? cardH[cardNo] : 60;
        gui_.card({x, y, w, h}, th::radiusPanel);
        y += 6;
    };
    auto cardEnd = [&]() {
        y += 6;
        float h = y - cardTop;
        if (std::abs(cardH[cardNo] - h) > 0.5f) { cardH[cardNo] = h; gui_.redraw(); }
        ++cardNo;
        y += 26;
    };
    auto tg = [&](const std::wstring& t, const std::wstring& d, bool& v, bool last = false, bool en = true) {
        RectF c = gui_.optionRow(x + 18, y, w - 36, t, d, 40, 20, en, !last);
        if (gui_.toggle(gid("set") ^ (Id)(uintptr_t)&v, c, v, en)) changed = true;
    };
    auto num = [&](const std::wstring& t, const std::wstring& d, int& v, int lo, int hi, bool last = false) {
        RectF c = gui_.optionRow(x + 18, y, w - 36, t, d, 150, 30, true, !last);
        if (gui_.number(gid("setn") ^ (Id)(uintptr_t)&v, c, v, lo, hi)) changed = true;
    };
    auto sld = [&](const std::wstring& t, const std::wstring& d, int& v, int lo, int hi, const wchar_t* unit, bool last = false) {
        RectF c = gui_.optionRow(x + 18, y, w - 36, t, d, 260, 30, true, !last);
        if (gui_.slider(gid("sets") ^ (Id)(uintptr_t)&v, {c.x, c.y, c.w - 56, c.h}, v, lo, hi)) changed = true;
        g.text(std::to_wstring(v) + unit, {c.r() - 52, c.y, 52, c.h}, th::textPrimary, th::body, 600, A_RIGHT);
    };
    auto drop = [&](const std::wstring& t, const std::wstring& d, int& v, const std::vector<std::wstring>& items, bool last = false) {
        RectF c = gui_.optionRow(x + 18, y, w - 36, t, d, 200, 30, true, !last);
        if (gui_.dropdown(gid("setd") ^ (Id)(uintptr_t)&v, c, items, v)) changed = true;
    };

    section(L"일반", L"저장 위치와 기본 동작이에요", ic::Settings, th::accent, th::purple);
    cardBegin();
    {
        RectF c = gui_.optionRow(x + 18, y, w - 36, L"저장 폴더", L"저장 창과 파일 관리에서 이 폴더를 써요", 340, 30);
        if (gui_.edit(gid("set.dir"), {c.x, c.y, c.w - 38, c.h}, st.saveDir)) changed = true;
        if (gui_.iconButton(gid("set.browse"), {c.r() - 32, c.y, 32, c.h}, ic::Folder)) {
            gui_.later([this] {
                BROWSEINFOW bi{};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"저장 폴더";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                if (PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi)) {
                    wchar_t p[MAX_PATH];
                    if (SHGetPathFromIDListW(pid, p)) { st.saveDir = p; st.save(); }
                    CoTaskMemFree(pid);
                }
                gui_.redraw();
            });
        }
    }
    tg(L"시작할 때 녹화 창 열기", L"프로그램을 켜면 바로 녹화할 수 있어요", st.startWithRecorder);
    tg(L"지울 때 휴지통으로 보내기", L"파일 관리에서 지운 파일을 되살릴 수 있어요", st.deleteToRecycle);
    tg(L"녹화가 끝나면 바로 편집", L"끄면 녹화 창이 그대로 남아요", st.gotoEditAfterRec, true);
    cardEnd();

    section(L"녹화", L"녹화할 때의 동작이에요", ic::Record, th::red, th::pink);
    cardBegin();
    {
        static const int fpsList[] = {1, 5, 10, 15, 20, 25, 30, 33, 50, 60};
        int fi = 3;
        for (int i = 0; i < 10; ++i) if (fpsList[i] == st.fps) fi = i;
        std::vector<std::wstring> fl;
        for (int v : fpsList) fl.push_back(std::to_wstring(v) + L" fps");
        RectF c = gui_.optionRow(x + 18, y, w - 36, L"기본 FPS", L"높을수록 부드럽고 파일이 커져요. 15가 무난해요", 160, 30);
        if (gui_.dropdown(gid("set.fps"), c, fl, fi)) { st.fps = fpsList[fi]; changed = true; }
    }
    num(L"최대 녹화 시간 (초)", L"이 길이가 되면 저절로 끝나요", st.maxRecSec, 5, 3600);
    num(L"메모리 한도 (MB)", L"녹화 프레임이 이만큼 쌓이면 멈춰요", st.maxMemMB, 200, 32000);
    tg(L"같은 화면은 합치기", L"화면이 그대로면 프레임을 늘리지 않고 시간만 늘려요", st.skipSame);
    tg(L"녹화하는 동안 이 창 숨기기", L"전체 화면을 녹화해도 BongGIF 창이 찍히지 않아요", st.excludeSelf);
    tg(L"창 크기 조절할 때 돋보기", L"테두리를 끌면 커서 주변을 크게 보여 줘요", st.useMagnifier);
    tg(L"Ctrl+Alt로 창 끌고 다니기", L"Ctrl+Alt를 누른 채 마우스를 움직이면 녹화 창이 따라와요", st.trackMouse);
    tg(L"Ctrl+PrtSc로 녹화 시작·정지", L"", st.ctrlPrtSc, true);
    cardEnd();
    cardBegin();
    tg(L"바로 저장", L"녹화하면서 바로 MP4로 저장해요. 메모리를 거의 쓰지 않아요", st.directSave);
    sld(L"바로 저장 화질", L"", st.directQuality, 10, 100, L"");
    tg(L"바로 저장할 때 시간 제한 없음", L"", st.directNoTimeLimit);
    tg(L"저장한 뒤 폴더 열기", L"", st.directOpenFolder, true);
    cardEnd();

    section(L"편집", L"불러오기와 편집 기본값이에요", ic::Edit, th::cyan, th::accent);
    cardBegin();
    num(L"이미지 가져오기 프레임 시간 (ms)", L"사진을 이어 붙일 때 한 장이 보이는 시간이에요", st.importFrameMs, 20, 10000);
    num(L"영상 가져오기 최대 FPS", L"", st.videoMaxFps, 1, 60);
    num(L"영상 가져오기 긴 변 최대 (px)", L"0이면 원본 크기예요", st.videoMaxSide, 0, 8000, true);
    cardEnd();

    section(L"단축키", L"버튼을 누른 뒤 원하는 키 조합을 눌러요. Esc로 취소해요", ic::Timer, th::purple, th::pink);
    cardBegin();
    auto hk = [&](const std::wstring& t, int& v, int which, bool last) {
        RectF c = gui_.optionRow(x + 18, y, w - 36, t, L"", 200, 32, true, !last);
        std::wstring label = hotkeyCapture_ == which ? tr(L"키를 누르세요…") : hotkeyName(v);
        if (gui_.button(gid(gid("set.hk"), which), c, label, hotkeyCapture_ == which ? BS_PRIMARY : BS_SECONDARY)) {
            int k = which;
            gui_.later([this, k] { hotkeyCapture_ = k; SetFocus(hwnd); gui_.redraw(); });
        }
    };
    hk(L"녹화 시작·정지", st.hotkeyRec, 1, false);
    hk(L"일시정지", st.hotkeyPause, 2, true);
    cardEnd();

    section(L"GIF", L"GIF 저장 기본값이에요 (저장 창에서도 바꿀 수 있어요)", ic::Photo, th::orange, th::yellow);
    cardBegin();
    sld(L"화질", L"", st.quality, 1, 100, L"");
    drop(L"색 줄이는 방식", L"", st.gifMethod, {L"LIQ (기본)", L"NeuQuant", L"Octree", L"분산 분할"});
    tg(L"디더링", L"", st.dither);
    tg(L"가운데에 색 더 쓰기", L"", st.centerWeighted);
    tg(L"비슷한 픽셀 지우기", L"", st.removeSimilar);
    tg(L"256색 이하면 그대로 저장", L"", st.skipQuant100);
    tg(L"파일에 프로그램 이름 남기기", L"", st.comment);
    tg(L"반복 횟수 정하기", L"끄면 끝없이 반복해요", st.loopFinite);
    num(L"반복 횟수", L"", st.loopCount, 1, 65535, true);
    cardEnd();

    section(L"WebP", L"", ic::Photo, th::green, th::cyan);
    cardBegin();
    tg(L"무손실", L"", st.webpLossless);
    tg(L"손실·무손실 자동 판단", L"", st.webpAutoDecide);
    tg(L"아이폰 호환", L"끄면 바뀐 부분만 저장해 훨씬 작아져요", st.webpIphoneCompatible);
    tg(L"화질 우선", L"", st.webpQualityFirst, true);
    cardEnd();

    section(L"WebM", L"", ic::Film, th::accent, th::cyan);
    cardBegin();
    {
        int codec = st.webmVp9 ? 0 : 1;
        RectF c = gui_.optionRow(x + 18, y, w - 36, L"코덱", L"VP9가 더 작고, VP8은 오래된 기기에서도 잘 돌아가요", 170, 30, true, true);
        if (gui_.segmented(gid("set.codec"), c, {L"VP9", L"VP8"}, codec)) { st.webmVp9 = codec == 0; changed = true; }
    }
    cardEnd();

    section(L"MP4", L"", ic::Film, th::purple, th::accent);
    cardBegin();
    tg(L"색 압축", L"MP4·WebM·AVIF·손실 WebP에서 비슷한 색을 묶어 파일을 5~20% 줄여요", st.colorCompress, true);
    cardEnd();

    section(L"APNG", L"", ic::Photo, th::pink, th::purple);
    cardBegin();
    g.text(L"APNG는 화질 손실 없이 저장해요. 바뀐 부분만 저장해 크기를 줄여요.", {x + 18, y + 8, w - 36, 34}, th::textSecondary, th::caption, 400, A_LEFT, false, true);
    y += 44;
    cardEnd();

    section(L"AVIF", L"", ic::Photo, th::cyan, th::green);
    cardBegin();
    g.text(L"AVIF 화질은 AV1 화질로 바뀌어요 (예: 80 → 74). 74 이상이면 4:4:4, 50~73은 4:2:2, 그 아래는 4:2:0으로 저장해요.",
           {x + 18, y + 8, w - 36, 40}, th::textSecondary, th::caption, 400, A_LEFT, false, true);
    y += 50;
    cardEnd();

    section(L"고급", L"", ic::Sparkle, th::yellow, th::orange);
    cardBegin();
    tg(L"DXGI 캡처", L"HDR 화면도 제 색으로 녹화해요. 문제가 생기면 꺼 주세요", st.useDxgi);
    num(L"JPG 시퀀스 화질", L"", st.jpgQuality, 1, 100, true);
    cardEnd();

    section(L"언어", L"Language", ic::Language, th::accent, th::accentEnd);
    cardBegin();
    {
        int lang = st.lang;
        RectF c = gui_.optionRow(x + 18, y, w - 36, L"화면 언어", L"Display language", 220, 30, true, true);
        if (gui_.segmented(gid("set.lang"), c, {L"한국어", L"English"}, lang)) { st.lang = lang; setLanguage(lang); changed = true; }
    }
    cardEnd();
    float bottom = y + 40;
    gui_.endScroll(settingsScroll_, bottom);
    // scroll-spy + jump
    if (settingsJump_ >= 0 && settingsJump_ < (int)sectionY.size()) {
        settingsScroll_.pos += sectionY[settingsJump_] - view.y;
        settingsPin_ = settingsJump_;      // keep the clicked tab lit until the user scrolls
        settingsPinPos_ = -1e9f;
        settingsJump_ = -1;
        gui_.redraw();
    } else if (settingsPin_ >= 0) {
        if (settingsPinPos_ < -1e8f) settingsPinPos_ = settingsScroll_.pos;   // position after clamping
        else if (std::fabs(settingsScroll_.pos - settingsPinPos_) > 0.5f) settingsPin_ = -1;
    }
    int active = 0;
    for (int i = 0; i < (int)sectionY.size(); ++i)
        if (sectionY[i] <= view.y + 48) active = i;
    if (settingsPin_ >= 0) active = settingsPin_;
    if (active != settingsActive_) { settingsActive_ = active; gui_.redraw(); }
    if (changed) { st.save(); settingsChanged(); }
}

std::wstring App::hotkeyName(int hk) {
    int mods = hk >> 16, vk = hk & 0xFFFF;
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT) s += L"Alt+";
    if (mods & MOD_SHIFT) s += L"Shift+";
    if (mods & MOD_WIN) s += L"Win+";
    wchar_t name[64] = {};
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    if (vk >= VK_PRIOR && vk <= VK_HELP) lp |= 1 << 24;
    if (vk == VK_SNAPSHOT) wcscpy_s(name, L"PrtSc");
    else if (!GetKeyNameTextW(lp, name, 64)) swprintf_s(name, L"%d", vk);
    return s + name;
}

} // namespace bg
