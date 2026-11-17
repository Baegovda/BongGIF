// BongGIF main window in the BongHub style: side bar (record / open / frame tools), glass top bar,
// frame list card, canvas card with a floating play bar, and expandable tool cards on the right.
#include "ui/App.h"

#include "core/Apng.h"
#include "core/Effects.h"
#include "core/Gif.h"
#include "platform/Export.h"
#include "platform/Media.h"
#include "platform/Webp.h"
#include "platform/Codecs.h"
#include "core/Psd.h"
#include "ui/Dialogs.h"
#include "ui/Lang.h"
#include "ui/ClipPage.h"
#include <imm.h>
#include "ui/Recorder.h"

#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

namespace bg {

static const wchar_t* kMainClass = L"BongGIFMain";
static const UINT WM_APP_RECDONE = WM_APP + 2, WM_APP_RECCLOSED = WM_APP + 3;
static const UINT_PTR ID_PLAYTIMER = 10, ID_PREVIEWTIMER = 11;
static Frames g_pendingRec;
static const float kSidebarW = 232, kTitleH = 44;

static uint32_t A(uint32_t c, float a) { return ((uint32_t)std::lround(((c >> 24) & 255) * a) << 24) | (c & 0xFFFFFF); }

App::App() = default;
App::~App() {
    batchCancel_ = true;
    if (batchThread_.joinable()) batchThread_.join();
}

// ================================================================== create
bool App::create(HINSTANCE inst, const std::wstring& initialPath) {
    st.load();
    setLanguage(st.lang);
    textColor_ = st.textColor;
    tSize_ = st.textSize;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    std::wstring title = widen(std::string(kAppName) + " " + kAppVersion);
    hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, st.winX, st.winY, st.winW,
                           st.winH, nullptr, nullptr, inst, this);
    if (!hwnd) return false;
    applyWindowStyle(hwnd);
    gui_.attach(hwnd);
    RECT wr;
    GetWindowRect(hwnd, &wr);
    if (!MonitorFromRect(&wr, MONITOR_DEFAULTTONULL)) SetWindowPos(hwnd, nullptr, 80, 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(hwnd, SW_SHOW);
    registerHotkeys();
    settingsChanged();
    if (!initialPath.empty()) openPath(initialPath, false);
    else if (st.startWithRecorder) openRecorder(false);
    return true;
}

void App::status(const std::wstring& s) {
    status_ = s;
    gui_.redraw();
}

void App::registerHotkeys() {
    UnregisterHotKey(hwnd, 1);
    UnregisterHotKey(hwnd, 2);
    std::wstring failed;
    auto reg = [&](int id, int hk) {
        if (!hk) return;   // 0 = no hotkey
        if (!RegisterHotKey(hwnd, id, (hk >> 16) | MOD_NOREPEAT, hk & 0xFFFF))
            failed += (failed.empty() ? L"" : L", ") + hotkeyName(hk);
    };
    reg(1, st.hotkeyRec);
    reg(2, st.hotkeyPause);
    if (!failed.empty()) status(tr(L"전역 단축키를 등록하지 못했어요. 다른 프로그램이 쓰고 있어요.") + L" (" + failed + L")");
}

void App::settingsChanged() {
    // Ctrl+PrtSc starts/stops recording (bUseCtrlPtrScForRec) - a real hotkey instead of polling
    UnregisterHotKey(hwnd, 3);
    if (st.ctrlPrtSc) RegisterHotKey(hwnd, 3, MOD_CONTROL, VK_SNAPSHOT);
    // Ctrl+Alt drags the record window (bTrackMouse): polled only while that option is on and the recorder is open
    if (st.trackMouse && rec_) SetTimer(hwnd, 12, 15, nullptr);
    else KillTimer(hwnd, 12);
}

// ================================================================== painting
void App::paint() {
    if (!gui_.beginFrame()) return;
    Gfx& g = gui_.g;
    float W = g.width(), H = g.height();
    g.clear(th::background);
    // ambient glow behind the glass (2-stop radial, very faint)
    g.glow(W * 0.28f, H * 0.05f, W * 0.55f, A(th::accent, 0.10f));
    g.glow(W * 0.85f, H * 0.95f, W * 0.45f, A(th::accentEnd, 0.08f));
    g.glow(W * 0.05f, H * 0.9f, W * 0.35f, A(th::cyan, 0.05f));

    drawSidebar(H);
    if (page_ != P_EDIT) {
        switch (page_) {
        case P_HOME: drawHome(W, H); break;
        case P_MANAGE: drawManage(W, H); break;
        case P_BATCH: drawBatch(W, H); break;
        case P_SETTINGS: drawSettings(W, H); break;
        case P_CLIP: drawClip(W, H); break;
        }
        drawCaptionButtons(W);
        gui_.endFrame();
        return;
    }
    if (!proj.frames.empty() && toolSignature() != lastSig_) {
        lastSig_ = toolSignature();
        SetTimer(hwnd, ID_PREVIEWTIMER, 120, nullptr);
    }
    float cx = kSidebarW + 16, cw = W - cx - 16;
    drawTopBar(cx, cw);
    float y0 = kTitleH + 52, bottom = H - 34;
    float listW = 176, panelW = std::min(340.f, std::max(286.f, cw * 0.27f));
    drawFrameList({cx, y0, listW, bottom - y0});
    drawToolPanel({cx + cw - panelW, y0, panelW, bottom - y0});
    drawCanvas({cx + listW + 12, y0, cw - listW - panelW - 24, bottom - y0});
    g.text(status_, {cx + 4, bottom + 6, cw - 8, 22}, th::textTertiary, th::caption);
    drawCaptionButtons(W);
    gui_.endFrame();
}

void App::drawCaptionButtons(float W) {
    Gfx& g = gui_.g;
    bool maxed = IsZoomed(hwnd);
    closeR_ = {W - 46, 0, 46, 32};
    maxR_ = {W - 92, 0, 46, 32};
    minR_ = {W - 138, 0, 46, 32};
    if (gui_.interact(gid("cap.min"), minR_)) gui_.later([this] { ShowWindow(hwnd, SW_MINIMIZE); });
    if (gui_.hot == gid("cap.min")) g.fill(minR_, 0, th::control);
    g.icon(ic::Minimize, minR_, th::textSecondary, 10);
    if (maxHot_) g.fill(maxR_, 0, maxDown_ ? th::controlHover : th::control);
    g.icon(maxed ? ic::Restore : ic::Maximize, maxR_, th::textSecondary, 10);
    if (gui_.interact(gid("cap.close"), closeR_)) gui_.later([this] { PostMessageW(hwnd, WM_CLOSE, 0, 0); });
    if (gui_.hot == gid("cap.close")) g.fill(closeR_, 0, th::red);
    g.icon(ic::Close, closeR_, gui_.hot == gid("cap.close") ? th::textPrimary : th::textSecondary, 10);
}

void App::drawTopBar(float x, float w) {
    Gfx& g = gui_.g;
    // page title + info
    std::wstring name = proj.frames.empty() ? L"새 움짤" : L"편집";
    g.text(name, {x + 4, 6, 300, 34}, th::textPrimary, th::pageTitle, 600, A_LEFT, true, false, true);
    RectF bar{x, kTitleH, w, 44};
    gui_.card(bar, th::radiusPanel, false);
    float bx = bar.x + 8, by = bar.y + 6;
    if (gui_.iconButton(gid("tb.undo"), {bx, by, 34, 32}, ic::Undo, proj.canUndo())) gui_.later([this] { undo(); });
    bx += 36;
    if (gui_.iconButton(gid("tb.redo"), {bx, by, 34, 32}, ic::Redo, proj.canRedo())) gui_.later([this] { redo(); });
    bx += 44;
    g.line(bx - 5, by + 6, bx - 5, by + 26, th::separator);
    if (gui_.iconButton(gid("tb.open"), {bx, by, 34, 32}, ic::Open)) gui_.later([this] { openDialog(); });
    bx += 36;
    if (gui_.iconButton(gid("tb.import"), {bx, by, 34, 32}, ic::Photo)) gui_.later([this] { importImages(); });
    bx += 44;
    g.line(bx - 5, by + 6, bx - 5, by + 26, th::separator);
    if (gui_.segmented(gid("tb.zoom"), {bx, by, 248, 32}, {L"맞춤", L"50%", L"100%", L"200%"}, zoomSel_)) gui_.redraw();
    bx += 258;
    float saveW = 104;
    RectF save{bar.r() - saveW - 6, by, saveW, 32};
    if (gui_.button(gid("tb.save"), save, L"저장", BS_PRIMARY, ic::Save, !proj.frames.empty())) gui_.later([this] { saveDialog(); });
    if (!proj.frames.empty()) {
        wchar_t buf[200];
        swprintf_s(buf, L"%d×%d  ·  %zu프레임  ·  %.2f초  ·  %s", proj.frames[0].img->w, proj.frames[0].img->h, proj.frames.size(),
                   proj.totalUs() / 1e6, fmtBytes(proj.memoryBytes()).c_str());
        g.text(buf, {bx, bar.y, save.x - bx - 12, bar.h}, th::textSecondary, th::caption, 400, A_RIGHT);
    }
}

void App::drawFrameList(RectF r) {
    Gfx& g = gui_.g;
    gui_.card(r, th::radiusPanel);
    g.text(L"프레임", {r.x + 14, r.y + 8, 80, 24}, th::textPrimary, th::body, 600);
    if (!proj.frames.empty()) {
        std::wstring n = std::to_wstring(proj.frames.size());
        float cw = g.textWidth(n, th::caption, 600) + 14;
        gui_.chip({r.r() - 14 - cw, r.y + 12, cw, 18}, n, th::textSecondary, th::control);
    }
    const float rowH = 28, footH = 76;
    RectF view{r.x + 4, r.y + 38, r.w - 8, r.h - 38 - footH};
    if (ensureCurVisible_) {
        float top = cur_ * rowH, bot = top + rowH;
        if (top < listScroll_.pos) listScroll_.pos = top;
        if (bot > listScroll_.pos + view.h) listScroll_.pos = bot - view.h;
        ensureCurVisible_ = false;
    }
    float y0 = gui_.beginScroll(listScroll_, view);
    int n = (int)proj.frames.size();
    int first = std::max(0, (int)((view.y - y0) / rowH) - 1), last = std::min(n, (int)((view.b() - y0) / rowH) + 2);
    for (int i = first; i < last; ++i) {
        RectF row{view.x + 4, y0 + i * rowH, view.w - 14, rowH - 2};
        Id id = gid(gid("list"), i);
        if (gui_.interact(id, row)) {
            bool ctrl = GetKeyState(VK_CONTROL) & 0x8000, shift = GetKeyState(VK_SHIFT) & 0x8000;
            if (shift) {
                sel_.clear();
                for (int k = std::min(anchor_, i); k <= std::max(anchor_, i); ++k) sel_.insert(k);
            } else if (ctrl) {
                if (!sel_.erase(i)) sel_.insert(i);
                anchor_ = i;
            } else {
                sel_ = {i};
                anchor_ = i;
            }
            cur_ = i;
            if (tool_ != T_NONE) lastSig_.clear();
        }
        bool selected = sel_.count(i) > 0, isCur = i == cur_;
        float t = gui_.hoverT(id);
        if (selected) g.fill(row, 6, th::control);
        else if (t > 0) g.fill(row, 6, A(th::control, t * 0.7f));
        if (isCur) g.fillV({row.x, row.y + 6, 3, row.h - 12}, 1.5f, th::accent, th::accentEnd);
        g.text(std::to_wstring(i + 1), {row.x + 12, row.y, 50, row.h}, isCur ? th::textPrimary : th::textTertiary, th::caption, isCur ? 600 : 400);
        wchar_t ms[32];
        swprintf_s(ms, L"%lld ms", (long long)((proj.frames[i].dur + 500) / 1000));
        g.text(ms, {row.x, row.y, row.w - 10, row.h}, selected ? th::textPrimary : th::textSecondary, th::caption, 400, A_RIGHT);
    }
    gui_.endScroll(listScroll_, y0 + n * rowH + 4);
    // footer actions
    gui_.separator(r.x + 10, r.r() - 10, r.b() - footH + 2);
    float bw = (r.w - 24) / 2, fy = r.b() - footH + 10;
    bool has = !proj.frames.empty();
    if (gui_.button(gid("lf.del"), {r.x + 8, fy, bw, 28}, L"삭제", BS_GHOST, ic::Delete, has)) gui_.later([this] { cmdDelete(); });
    if (gui_.button(gid("lf.time"), {r.x + 16 + bw, fy, bw, 28}, L"시간", BS_GHOST, ic::Clock, has)) gui_.later([this] { cmdSetTime(true); });
    if (gui_.button(gid("lf.red"), {r.x + 8, fy + 32, bw, 28}, L"줄이기", BS_GHOST, ic::Reduce, has)) gui_.later([this] { cmdReduce(true); });
    if (gui_.button(gid("lf.add"), {r.x + 16 + bw, fy + 32, bw, 28}, L"추가", BS_GHOST, ic::Add, has)) gui_.later([this] { cmdAppend(); });
}

void App::drawCanvas(RectF r) {
    Gfx& g = gui_.g;
    gui_.card(r, th::radiusPanel);
    RectF well = r.inset(10);
    g.fill(well, th::radiusCard, th::canvasWell);
    canvasR_ = well;
    if (proj.frames.empty()) {
        gui_.iconTile({well.x + well.w / 2 - 28, well.y + well.h / 2 - 80, 56, 56}, ic::Record, th::accent, th::accentEnd);
        g.text(L"녹화하거나 파일을 열어 움짤을 만들어 보세요", {well.x, well.y + well.h / 2 - 12, well.w, 26}, th::textPrimary, th::sectionTitle, 600, A_CENTER);
        g.text(L"GIF·WebP·APNG·MP4 파일이나 영상을 여기로 끌어다 놓아도 돼요", {well.x, well.y + well.h / 2 + 16, well.w, 22}, th::textSecondary, th::body, 400, A_CENTER);
        return;
    }
    ImagePtr im = (preview_ && !playing_) ? preview_ : proj.frames[cur_].img;
    float playH = 48;
    RectF area{well.x + 8, well.y + 8, well.w - 16, well.h - playH - 24};
    // DIP per image pixel; 100% = one image pixel per screen pixel, "fit" never enlarges
    static const float zooms[] = {0, 0.5f, 1.f, 2.f};
    float sc = zoomSel_ == 0 ? std::min({area.w / im->w, area.h / im->h, 1.f / gui_.s()}) : zooms[zoomSel_] / gui_.s();
    viewScale_ = sc;
    float dw = im->w * sc, dh = im->h * sc;
    imageR_ = {area.x + (area.w - dw) / 2, area.y + (area.h - dh) / 2, dw, dh};
    g.pushClip(well);
    g.image(im, imageR_, sc * gui_.s() < 1.0f);
    if (tool_ == T_DRAW && dragging_ && !strokes_.empty()) {   // live stroke while drawing
        auto& s = strokes_.back();
        for (size_t k = 1; k < s.pts.size(); ++k)
            g.line(imageR_.x + s.pts[k - 1].x * sc, imageR_.y + s.pts[k - 1].y * sc, imageR_.x + s.pts[k].x * sc, imageR_.y + s.pts[k].y * sc,
                   s.highlighter ? ((s.color & 0xFFFFFF) | 0x66000000) : s.color, std::max(1.f, s.width * sc));
    }
    if ((tool_ == T_CROP || tool_ == T_EFFECT || tool_ == T_ZOOM || (tool_ == T_INSERT && insertKind_ == 0)) && selRect_.w > 0 && selRect_.h > 0) {
        RectF s{imageR_.x + selRect_.x * sc, imageR_.y + selRect_.y * sc, selRect_.w * sc, selRect_.h * sc};
        if (tool_ == T_CROP) {   // dim outside the crop
            g.fill({imageR_.x, imageR_.y, imageR_.w, s.y - imageR_.y}, 0, 0x99000000);
            g.fill({imageR_.x, s.b(), imageR_.w, imageR_.b() - s.b()}, 0, 0x99000000);
            g.fill({imageR_.x, s.y, s.x - imageR_.x, s.h}, 0, 0x99000000);
            g.fill({s.r(), s.y, imageR_.r() - s.r(), s.h}, 0, 0x99000000);
        }
        g.dashRect(s, th::cyan, 1.5f);
    }
    g.popClip();

    // floating play bar
    float pw = std::min(560.f, well.w - 32);
    RectF pb{well.x + (well.w - pw) / 2, well.b() - playH - 10, pw, playH};
    g.shadow(pb, pb.h / 2);
    g.fill(pb, pb.h / 2, th::menuBackground);
    g.strokeV(pb, pb.h / 2, th::strokeTop, th::strokeBottom, 1);
    float bx = pb.x + 10, by = pb.y + 8;
    int n = (int)proj.frames.size();
    if (gui_.iconButton(gid("pb.first"), {bx, by, 32, 32}, ic::Prev)) gui_.later([this] { gotoFrame(0); });
    bx += 34;
    if (gui_.iconButton(gid("pb.prev"), {bx, by, 32, 32}, ic::ChevronLeft)) gui_.later([this] { step(-1); });
    bx += 34;
    RectF playR{bx + 2, by - 2, 36, 36};
    bool clickPlay = gui_.interact(gid("pb.play"), playR);
    g.fillH(playR, 18, th::accent, th::accentEnd);
    if (gui_.hoverT(gid("pb.play")) > 0) g.fill(playR, 18, A(0x1CFFFFFF, gui_.hoverT(gid("pb.play"))));
    g.icon(playing_ ? ic::Pause : ic::Play, playR, th::textPrimary, 13);
    if (clickPlay) gui_.later([this] { togglePlay(); });
    bx += 42;
    if (gui_.iconButton(gid("pb.next"), {bx, by, 32, 32}, ic::ChevronRight)) gui_.later([this] { step(1); });
    bx += 34;
    if (gui_.iconButton(gid("pb.last"), {bx, by, 32, 32}, ic::Next)) gui_.later([this] { gotoFrame((int)proj.frames.size() - 1); });
    bx += 40;
    bool compact = pb.w < 470;
    float labelW = compact ? 64 : 116;
    RectF sl{bx + 4, by, std::max(20.f, pb.r() - labelW - bx - 14), 32};
    int pos = cur_;
    if (gui_.slider(gid("pb.slider"), sl, pos, 0, std::max(0, n - 1))) { cur_ = pos; sel_ = {pos}; anchor_ = pos; ensureCurVisible_ = true; lastSig_.clear(); }
    int64_t t = 0;
    for (int k = 0; k < cur_; ++k) t += proj.frames[k].dur;
    wchar_t buf[64];
    if (compact) swprintf_s(buf, L"%d / %d", cur_ + 1, n);
    else swprintf_s(buf, L"%d / %d   %.2fs", cur_ + 1, n, t / 1e6);
    g.text(buf, {pb.r() - labelW - 8, pb.y, labelW, pb.h}, th::textSecondary, th::caption, 400, A_RIGHT);

    // canvas interaction (crop / effect region drag, text & logo placement)
    Id cid = gid("canvas");
    bool overBar = pb.contains(gui_.mx, gui_.my);
    if (!overBar) gui_.interact(cid, well);
    auto toImg = [&](float mx, float my, float& ix, float& iy) {
        ix = std::clamp((mx - imageR_.x) / sc, 0.f, (float)im->w);
        iy = std::clamp((my - imageR_.y) / sc, 0.f, (float)im->h);
    };
    if (gui_.active == cid && gui_.pressed) {
        float ix, iy;
        toImg(gui_.mx, gui_.my, ix, iy);
        int needs = tool_ == T_EFFECT ? effectList()[effIdx_].needs : 0;
        if (tool_ == T_EFFECT && (needs & (EN_POINT | EN_COLOR))) {   // pick a point / colour
            effPx_ = std::min((int)ix, im->w - 1);
            effPy_ = std::min((int)iy, im->h - 1);
            effColor_ = proj.frames[cur_].img->row(effPy_)[effPx_];
        }
        if (tool_ == T_CROP || tool_ == T_ZOOM || (tool_ == T_INSERT && insertKind_ == 0) ||
            (tool_ == T_EFFECT && (needs & (EN_RECT | EN_RECT_OPTIONAL)))) {
            dragging_ = true; dragX0_ = ix; dragY0_ = iy; selRect_ = {ix, iy, 0, 0};
        } else if (tool_ == T_DRAW) {
            Stroke s;
            s.color = penColor_;
            s.width = (float)penWidth_;
            s.highlighter = highlighter_;
            s.pts.push_back({(LONG)ix, (LONG)iy});
            strokes_.push_back(s);
            dragging_ = true;
        } else if (tool_ == T_TEXT || tool_ == T_LOGO || tool_ == T_INSERT) { textX_ = (int)ix; textY_ = (int)iy; }
    }
    if (dragging_ && gui_.active == cid && gui_.down) {
        float ix, iy;
        toImg(gui_.mx, gui_.my, ix, iy);
        if (tool_ == T_CROP && ratioSel_ > 0) {
            static const int ratio[][2] = {{0, 0}, {1, 1}, {4, 3}, {16, 9}, {9, 16}, {3, 4}};
            float wd = std::abs(ix - dragX0_), hd = wd * ratio[ratioSel_][1] / ratio[ratioSel_][0];
            iy = dragY0_ + (iy >= dragY0_ ? hd : -hd);
        }
        if (tool_ == T_DRAW) {
            if (!strokes_.empty()) {
                auto& pts = strokes_.back().pts;
                POINT p{(LONG)ix, (LONG)iy};
                if (pts.empty() || std::abs(pts.back().x - p.x) + std::abs(pts.back().y - p.y) >= 2) pts.push_back(p);
            }
        } else
        selRect_ = {std::min(dragX0_, ix), std::min(dragY0_, iy), std::abs(ix - dragX0_), std::abs(iy - dragY0_)};
        if (tool_ == T_CROP) { cX_ = (int)selRect_.x; cY_ = (int)selRect_.y; cW_ = (int)selRect_.w; cH_ = (int)selRect_.h; }
    }
    if (gui_.released && dragging_) { dragging_ = false; lastSig_.clear(); }
    if (gui_.hit(well) && !overBar && gui_.wheel != 0) {
        int d = gui_.wheel > 0 ? -1 : 1;
        gui_.wheel = 0;
        gui_.later([this, d] { step(d); });
    }
}

float App::toolCard(float x, float y, float w, int tool, wchar_t icon, uint32_t c1, uint32_t c2, const std::wstring& title,
                    const std::wstring& desc) {
    Gfx& g = gui_.g;
    bool open = tool_ == tool;
    bool has = !proj.frames.empty();
    float headH = 62;
    float bodyH = 0;
    // body height is known only after layout: measure by drawing into a clipped-off pass is not
    // possible in one pass, so the card is drawn as header + body sections stacked.
    RectF head{x, y, w, headH};
    Id id = gid(gid("tool"), tool);
    bool click = gui_.interact(id, head, has);
    float t = gui_.hoverT(id);
    (void)bodyH;
    // card background is drawn by the caller around header+body; here only header content
    if (t > 0 && !open) g.fill(head.inset(2), th::radiusCard, A(th::control, t * 0.5f));
    gui_.iconTile({x + 14, y + 15, 32, 32}, icon, has ? c1 : A(c1, 0.4f), has ? c2 : A(c2, 0.4f));
    g.text(title, {x + 58, y + 11, w - 100, 22}, gui_.dim(th::textPrimary, has), th::body, 600);
    g.text(desc, {x + 58, y + 32, w - 100, 18}, gui_.dim(th::textTertiary, has), th::caption);
    float rot = gui_.anim(id ^ 0x99, open ? 1.f : 0.f, th::hoverMs);
    g.icon(rot > 0.5f ? ic::ChevronUp : ic::ChevronDown, {x + w - 40, y, 30, headH}, gui_.dim(th::textSecondary, has), 10);
    if (click) gui_.later([this, tool] { setTool(tool_ == tool ? T_NONE : tool); });
    return y + headH;
}

void App::drawToolPanel(RectF r) {
    Gfx& g = gui_.g;
    float y0 = gui_.beginScroll(panelScroll_, {r.x - 6, r.y, r.w + 12, r.h});
    float x = r.x, w = r.w - 6, y = y0;
    g.text(L"편집 및 효과", {x + 4, y, w, 26}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
    g.text(L"카드를 누르면 옵션이 펼쳐져요", {x + 4, y + 26, w, 18}, th::textTertiary, th::caption);
    y += 54;
    struct T { int tool; wchar_t icon; uint32_t c1, c2; const wchar_t* title; const wchar_t* desc; };
    T tools[] = {
        {T_RESIZE, ic::Resize, th::accent, th::cyan, L"크기 조절 · 회전", L"크기를 바꾸거나 돌리고 뒤집어요"},
        {T_CROP, ic::Crop, th::green, th::cyan, L"자르기", L"필요한 부분만 남겨요"},
        {T_TEXT, ic::Font, th::orange, th::yellow, L"글자 넣기", L"움짤 위에 글자를 얹어요"},
        {T_LOGO, ic::Picture, th::pink, th::purple, L"이미지 워터마크", L"로고나 사진을 겹쳐요"},
        {T_TRANS, ic::Transition, th::purple, th::accent, L"화면 전환 효과", L"두 프레임 사이를 부드럽게 이어요"},
        {T_EFFECT, ic::Effect, th::cyan, th::green, L"필터 효과", L"색감·흐림·모자이크·카툰 등 27가지"},
        {T_INSERT, ic::Sticker, th::yellow, th::orange, L"말풍선 · 스티커", L"말풍선이나 이모지를 붙여요"},
        {T_DRAW, ic::Pen, th::red, th::pink, L"그리기", L"마우스로 선을 그려 넣어요"},
        {T_ZOOM, ic::ZoomIn, th::accent, th::purple, L"줌 애니메이션", L"한 곳으로 부드럽게 다가가요"},
        {T_COLLAGE, ic::Collage, th::green, th::accent, L"콜라주", L"여러 움짤을 나란히 붙여요"},
        {T_FRAME, ic::Frame, th::orange, th::yellow, L"액자", L"테두리·폴라로이드·필름 모양을 씌워요"},
    };
    for (auto& t : tools) {
        bool open = tool_ == t.tool;
        // two-pass card: draw content first into place, then the card behind it would cover it, so
        // the card surface is drawn using the previous frame's measured height.
        static std::map<int, float> lastH;
        float hgt = open ? std::max(62.f, lastH[t.tool]) : 62.f;
        gui_.card({x, y, w, hgt}, th::radiusCard);
        float yy = toolCard(x, y, w, t.tool, t.icon, t.c1, t.c2, t.title, t.desc);
        if (open) {
            float end = toolBody(x, yy, w, t.tool);
            float measured = end - y + 12;
            if (std::abs(measured - lastH[t.tool]) > 0.5f) { lastH[t.tool] = measured; gui_.redraw(); }
            y += std::max(measured, hgt);
        } else {
            y += hgt;
        }
        y += th::cardGap;
    }
    // revert
    gui_.card({x, y, w, 58}, th::radiusCard);
    gui_.iconTile({x + 14, y + 13, 32, 32}, ic::Reset, th::red, th::orange);
    g.text(L"처음 상태로 되돌리기", {x + 58, y + 9, w - 160, 22}, gui_.dim(th::textPrimary, !proj.frames.empty()), th::body, 600);
    g.text(L"불러온 직후로 돌아가요", {x + 58, y + 30, w - 160, 18}, gui_.dim(th::textTertiary, !proj.frames.empty()), th::caption);
    if (gui_.button(gid("revert"), {x + w - 92, y + 14, 80, 30}, L"되돌리기", BS_SECONDARY, 0, !proj.frames.empty())) gui_.later([this] { cmdRevert(); });
    y += 58 + 8;
    gui_.endScroll(panelScroll_, y);
}

float App::toolBody(float x, float y, float w, int tool) {
    Gfx& g = gui_.g;
    float ix = x + 26, iw = w - 42;   // indented, with a thin accent line on the left
    float top = y;
    float yy = y + 2;
    auto row = [&](const std::wstring& t, const std::wstring& d, float cw, float ch, bool en = true, bool sep = true) {
        return gui_.optionRow(ix, yy, iw, t, d, cw, ch, en, sep);
    };
    auto scope = [&] {
        RectF c = row(L"적용 범위", L"", 128, 30);
        gui_.segmented(gid("scope"), c, {L"전체", L"선택"}, scope_);
    };
    auto applyBtn = [&](const std::wstring& t, std::function<void()> fn, bool en = true) {
        yy += 8;
        if (gui_.button(gid(gid("apply"), tool), {ix + iw - 120, yy, 120, 34}, t, BS_PRIMARY, ic::Check, en)) gui_.later(std::move(fn));
        if (gui_.button(gid(gid("close"), tool), {ix + iw - 216, yy, 88, 34}, L"닫기", BS_SECONDARY)) gui_.later([this] { setTool(T_NONE); });
        yy += 40;
    };
    if (proj.frames.empty()) return y;
    const Image& im0 = *proj.frames[0].img;
    switch (tool) {
    case T_RESIZE: {
        RectF c = row(L"폭", L"", 150, 30);
        if (gui_.number(gid("rs.w"), c, rW_, 8, 5000) && keepAspect_) rH_ = std::max(8, (int)std::lround(rW_ * (double)im0.h / im0.w));
        c = row(L"높이", L"", 150, 30);
        if (gui_.number(gid("rs.h"), c, rH_, 8, 5000) && keepAspect_) rW_ = std::max(8, (int)std::lround(rH_ * (double)im0.w / im0.h));
        c = row(L"비율 유지", L"폭을 바꾸면 높이도 같이 바뀌어요", 40, 20);
        gui_.toggle(gid("rs.keep"), c, keepAspect_);
        c = row(L"확대 방식", L"초해상도는 AI 모델로 2~4배 키워요 (느려요)", 170, 30);
        gui_.dropdown(gid("rs.method"), c, {L"보통", L"초해상도 ESPCN", L"초해상도 FSRCNN"}, srMethod_);
        if (srMethod_ > 0) {
            c = row(L"배율", L"", 186, 30);
            if (gui_.segmented(gid("rs.sr"), c, {L"2배", L"3배", L"4배"}, srScale_)) {}
            rW_ = im0.w * (srScale_ + 2);
            rH_ = im0.h * (srScale_ + 2);
        }
        c = row(L"빠른 크기", L"", 186, 30);
        int pctSel = -1;
        if (gui_.segmented(gid("rs.pct"), c, {L"25%", L"50%", L"75%"}, pctSel) && pctSel >= 0) {
            int pct[] = {25, 50, 75};
            rW_ = std::max(8, im0.w * pct[pctSel] / 100);
            rH_ = std::max(8, im0.h * pct[pctSel] / 100);
        }
        applyBtn(L"크기 적용", [this] {
            int w2 = rW_, h2 = rH_, m = srMethod_, sc = srScale_ + 2;
            Frames src = proj.frames;
            if (m > 0)
                runBg(L"초해상도로 키우는 중", [=](const Progress& p, const Cancel&) { return superResolve(src, m - 1, sc, p); },
                      std::to_wstring(sc) + L"배 초해상도로 키웠어요");
            else
                runBg(L"크기를 바꾸는 중", [=](const Progress& p, const Cancel&) { return resize(src, w2, h2, p); },
                      L"크기를 " + std::to_wstring(w2) + L"×" + std::to_wstring(h2) + L"로 바꿨어요");
        }, rW_ != im0.w || rH_ != im0.h);
        gui_.separator(ix, ix + iw, yy);
        yy += 10;
        g.text(L"회전 · 뒤집기", {ix, yy, iw, 20}, th::textPrimary, th::body, 600);
        yy += 26;
        struct R { const wchar_t* t; Rot r; } rots[] = {{L"왼쪽", Rot::CCW}, {L"오른쪽", Rot::CW}, {L"180°", Rot::R180}, {L"좌우", Rot::HFlip}, {L"상하", Rot::VFlip}};
        float bw = (iw - 16) / 5;
        for (int i = 0; i < 5; ++i) {
            Rot r = rots[i].r;
            if (gui_.button(gid(gid("rot"), i), {ix + i * (bw + 4), yy, bw, 30}, rots[i].t, BS_SECONDARY))
                gui_.later([this, r] { apply(rotate(proj.frames, r), L"돌리거나 뒤집었어요"); rW_ = proj.frames[0].img->w; rH_ = proj.frames[0].img->h; });
        }
        yy += 38;
        break;
    }
    case T_CROP: {
        float hh = g.textHeight(L"미리보기에서 드래그해 남길 영역을 고르세요.", th::caption, iw);
        g.text(L"미리보기에서 드래그해 남길 영역을 고르세요.", {ix, yy, iw, hh + 2}, th::textSecondary, th::caption, 400, A_LEFT, false, true);
        yy += hh + 8;
        RectF c = row(L"비율", L"", 150, 30);
        gui_.dropdown(gid("cr.ratio"), c, {L"자유", L"1:1", L"4:3", L"16:9", L"9:16", L"3:4"}, ratioSel_);
        bool ch = false;
        c = row(L"X", L"", 150, 30); ch |= gui_.number(gid("cr.x"), c, cX_, 0, im0.w - 1);
        c = row(L"Y", L"", 150, 30); ch |= gui_.number(gid("cr.y"), c, cY_, 0, im0.h - 1);
        c = row(L"폭", L"", 150, 30); ch |= gui_.number(gid("cr.w"), c, cW_, 1, im0.w);
        c = row(L"높이", L"", 150, 30); ch |= gui_.number(gid("cr.h"), c, cH_, 1, im0.h);
        if (ch) selRect_ = {(float)cX_, (float)cY_, (float)cW_, (float)cH_};
        applyBtn(L"자르기", [this] {
            int x0 = (int)selRect_.x, y0 = (int)selRect_.y, w0 = (int)selRect_.w, h0 = (int)selRect_.h;
            if (w0 < 8 || h0 < 8) { messageBox(hwnd, L"영역이 너무 작아요. 8픽셀 이상으로 골라 주세요."); return; }
            Frames f = crop(proj.frames, x0, y0, w0, h0);
            setTool(T_NONE);
            apply(std::move(f), L"" + std::to_wstring(w0) + L"×" + std::to_wstring(h0) + L"로 잘랐어요");
        });
        break;
    }
    case T_TEXT: {
        RectF c = row(L"글자", L"", iw * 0.6f, 30);
        gui_.edit(gid("tx.text"), c, text_);
        c = row(L"크기", L"", 150, 30);
        gui_.number(gid("tx.size"), c, tSize_, 8, 400);
        c = row(L"색", L"", 182, 22);
        uint32_t sw[] = {0xFFFFFFFF, th::yellow, th::orange, th::red, th::pink, th::purple, th::cyan, 0xFF000000};
        for (int i = 0; i < 8; ++i) {
            RectF s{c.x + i * 23, c.y, 20, 20};
            Id sid = gid(gid("tx.col"), i);
            if (gui_.interact(sid, s)) { textColor_ = sw[i] & 0xFFFFFF; }
            g.fill(s, 10, sw[i]);
            g.stroke(s, 10, (textColor_ == (sw[i] & 0xFFFFFF)) ? th::accent : th::separator, (textColor_ == (sw[i] & 0xFFFFFF)) ? 2.f : 1.f);
        }
        c = row(L"테두리", L"글자 둘레에 검은 선을 둘러 잘 보이게 해요", 40, 20);
        gui_.toggle(gid("tx.outline"), c, outline_);
        scope();
        g.text(L"미리보기를 누르면 그 자리에 글자가 들어가요.", {ix, yy + 4, iw, 18}, th::textTertiary, th::caption);
        yy += 24;
        applyBtn(L"글자 넣기", [this] {
            auto fn = toolFn();
            Frames src = proj.frames;
            auto sel = scopeSel();
            st.textSize = tSize_;
            st.textColor = textColor_;
            setTool(T_NONE);
            runBg(L"글자를 넣는 중", [=](const Progress& p, const Cancel&) { return mapImages(src, fn, sel, p); }, L"글자를 넣었어요");
        });
        break;
    }
    case T_LOGO: {
        RectF c = row(L"이미지 파일", logo_ ? std::to_wstring(logo_->w) + L"×" + std::to_wstring(logo_->h) : L"PNG면 투명 부분도 살아나요", 110, 30);
        if (gui_.button(gid("lg.pick"), c, logo_ ? L"바꾸기" : L"고르기", BS_SECONDARY, ic::Folder)) {
            gui_.later([this] {
                wchar_t buf[MAX_PATH] = {};
                OPENFILENAMEW of{sizeof of};
                of.hwndOwner = hwnd;
                of.lpstrFilter = L"이미지\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0";
                of.lpstrFile = buf;
                of.nMaxFile = MAX_PATH;
                of.Flags = OFN_FILEMUSTEXIST;
                if (!GetOpenFileNameW(&of)) return;
                auto im = std::make_shared<Image>();
                std::string e;
                if (loadImageWic(buf, *im, true, e)) { logo_ = im; lastSig_.clear(); gui_.redraw(); }
            });
        }
        c = row(L"크기", L"", 170, 30);
        gui_.slider(gid("lg.scale"), {c.x, c.y, c.w - 44, c.h}, lScale_, 5, 400, logo_ != nullptr);
        g.text(std::to_wstring(lScale_) + L"%", {c.r() - 40, c.y, 40, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        c = row(L"불투명도", L"", 170, 30);
        gui_.slider(gid("lg.op"), {c.x, c.y, c.w - 44, c.h}, lOpacity_, 5, 100, logo_ != nullptr);
        g.text(std::to_wstring(lOpacity_) + L"%", {c.r() - 40, c.y, 40, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        scope();
        g.text(L"미리보기를 누르면 그 자리에 이미지가 놓여요.", {ix, yy + 4, iw, 18}, th::textTertiary, th::caption);
        yy += 24;
        applyBtn(L"워터마크 넣기", [this] {
            auto fn = toolFn();
            if (!fn) return;
            Frames src = proj.frames;
            auto sel = scopeSel();
            setTool(T_NONE);
            runBg(L"워터마크를 넣는 중", [=](const Progress& p, const Cancel&) { return mapImages(src, fn, sel, p); }, L"워터마크를 넣었어요");
        }, logo_ != nullptr);
        break;
    }
    case T_TRANS: {
        g.text(L"지금 프레임과 다음 프레임 사이에 서서히 바뀌는 프레임을 넣어요.", {ix, yy, iw, 34}, th::textSecondary, th::caption, 400, A_LEFT, false, true);
        yy += 38;
        RectF c = row(L"넣을 프레임 수", L"", 150, 30);
        gui_.number(gid("tr.n"), c, tN_, 2, 60);
        c = row(L"전체 시간 (ms)", L"", 150, 30);
        gui_.number(gid("tr.ms"), c, tMs_, 50, 5000);
        applyBtn(L"전환 넣기", [this] {
            if (cur_ >= (int)proj.frames.size() - 1) { messageBox(hwnd, L"마지막 프레임이 아닌 곳을 골라 주세요."); return; }
            apply(crossfade(proj.frames, cur_, tN_, tMs_ * 1000LL), L"화면 전환 효과를 넣었어요");
        }, cur_ < (int)proj.frames.size() - 1);
        break;
    }
    case T_EFFECT: {
        std::vector<std::wstring> names;
        for (auto& e : effectList()) names.push_back(e.name);
        RectF c = row(L"효과", L"", 170, 30);
        int before = effIdx_;
        gui_.dropdown(gid("ef.which"), c, names, effIdx_);
        const auto& def = effectList()[effIdx_];
        if (before != effIdx_ || effVals_.size() != def.params.size()) {
            effVals_.clear();
            for (auto& p : def.params) effVals_.push_back(p.def);
        }
        if (*def.hint) {
            float hh = g.textHeight(def.hint, th::caption, iw);
            g.text(def.hint, {ix, yy + 4, iw, hh + 2}, th::accent, th::caption, 400, A_LEFT, false, true);
            yy += hh + 10;
        }
        for (size_t i = 0; i < def.params.size(); ++i) {
            if (def.params[i].toggle) {
                c = row(def.params[i].label, L"", 40, 20);
                bool v = effVals_[i] != 0;
                if (gui_.toggle(gid(gid("ef.t"), i), c, v)) effVals_[i] = v ? 1 : 0;
                continue;
            }
            c = row(def.params[i].label, L"", 190, 30);
            gui_.slider(gid(gid("ef.p"), i), {c.x, c.y, c.w - 48, c.h}, effVals_[i], def.params[i].lo, def.params[i].hi);
            g.text(std::to_wstring(effVals_[i]), {c.r() - 44, c.y, 44, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        }
        if (def.needs & EN_COLOR) {
            c = row(L"고른 색", L"", 40, 20);
            g.fill(c, 10, 0xFF000000u | effColor_);
            g.stroke(c, 10, th::separator, 1);
        }
        if (def.needs & EN_RECT_OPTIONAL) {
            c = row(L"드래그한 영역만", L"미리보기에서 드래그한 부분에만 효과를 줘요", 40, 20);
            gui_.toggle(gid("ef.region"), c, effRegion_);
        }
        scope();
        applyBtn(L"효과 적용", [this] {
            auto fn = toolFn();
            Frames src = proj.frames;
            auto sel = scopeSel();
            std::wstring nm = effectList()[effIdx_].name;
            runBg(nm + L" 적용 중", [=](const Progress& p, const Cancel&) { return mapImages(src, fn, sel, p); }, nm + L" 효과를 줬어요");
        });
        break;
    }
    default:
        yy = toolBody2(tool, ix, yy, iw);
        break;
    }
    // thin accent line along the expanded options
    g.fillV({x + 14, top + 4, 2, yy - top - 8}, 1, th::accent, th::accentEnd);
    return yy;
}

// ================================================================== navigation / playback
void App::gotoFrame(int i) {
    if (proj.frames.empty()) return;
    cur_ = std::clamp(i, 0, (int)proj.frames.size() - 1);
    if (!playing_) { sel_ = {cur_}; anchor_ = cur_; }
    ensureCurVisible_ = true;
    lastSig_.clear();
    gui_.redraw();
}
void App::step(int d) {
    if (!proj.frames.empty()) gotoFrame((cur_ + d + (int)proj.frames.size()) % (int)proj.frames.size());
}
void App::selectOnly(int i) { sel_ = {i}; anchor_ = i; }

void App::togglePlay() {
    if (proj.frames.empty()) return;
    playing_ = !playing_;
    if (playing_) SetTimer(hwnd, ID_PLAYTIMER, 10, nullptr);
    else { KillTimer(hwnd, ID_PLAYTIMER); gotoFrame(cur_); }
    gui_.redraw();
}
void App::stopPlay() { if (playing_) togglePlay(); }

// ================================================================== operations
void App::apply(Frames f, const std::wstring& msg) {
    if (f.empty()) return;
    proj.apply(std::move(f));
    dirty = true;
    preview_.reset();
    lastSig_.clear();
    cur_ = std::clamp(cur_, 0, (int)proj.frames.size() - 1);
    std::set<int> keep;
    for (int i : sel_) if (i < (int)proj.frames.size()) keep.insert(i);
    sel_ = keep.empty() ? std::set<int>{cur_} : keep;
    if (!msg.empty()) status_ = msg;
    gui_.redraw();
}

void App::runBg(const std::wstring& title, std::function<Frames(const Progress&, const Cancel&)> fn, const std::wstring& msg) {
    stopPlay();
    Frames result;
    bool ok = runWithProgress(hwnd, title, [&](const std::function<void(double)>& p, const std::function<bool()>& c) -> std::string {
        try {
            result = fn(p, c);
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    });
    if (ok && !result.empty()) apply(std::move(result), msg);
}

bool App::needFrames() {
    if (proj.frames.empty()) { messageBox(hwnd, L"먼저 녹화하거나 파일을 열어 주세요."); return false; }
    return true;
}

void App::undo() { if (proj.undo()) { preview_.reset(); lastSig_.clear(); cur_ = std::min(cur_, (int)proj.frames.size() - 1); status(L"되돌렸어요"); } }
void App::redo() { if (proj.redo()) { preview_.reset(); lastSig_.clear(); cur_ = std::min(cur_, (int)proj.frames.size() - 1); status(L"다시 실행했어요"); } }

void App::cmdSetTime(bool selOnly) {
    if (!needFrames()) return;
    auto sel = selOnly ? selection() : std::vector<int>{};
    if (selOnly && sel.empty()) return;
    int64_t us;
    if (!timeDialog(hwnd, selOnly ? L"고른 프레임 시간 정하기" : L"전체 프레임 시간 정하기", proj.frames[cur_].dur, us)) return;
    if (us < 20000 && !messageBox(hwnd, L"GIF는 50fps(20ms)보다 빠르게 재생하지 못해요. 그래도 바꿀까요?", true)) return;
    Frames f = proj.frames;
    if (selOnly) for (int i : sel) f[i].dur = us;
    else for (auto& x : f) x.dur = us;
    wchar_t buf[64];
    swprintf_s(buf, L"프레임 시간을 %.1fms로 바꿨어요", us / 1000.0);
    apply(std::move(f), buf);
}

void App::cmdSpeed(double k) {
    if (needFrames()) apply(changeSpeed(proj.frames, k), k < 1 ? L"10% 빠르게 바꿨어요" : L"10% 느리게 바꿨어요");
}

void App::cmdTrim() {
    if (!needFrames()) return;
    int a, b;
    if (trimDialog(hwnd, proj.frames, a, b))
        apply(trim(proj.frames, a, b), std::to_wstring(a + 1) + L"~" + std::to_wstring(b + 1) + L"번 프레임만 남겼어요");
}

void App::cmdReduce(bool selOnly) {
    if (!needFrames()) return;
    auto sel = selOnly ? selection() : std::vector<int>{};
    size_t n = sel.size() >= 2 ? sel.size() : proj.frames.size();
    if (n < 10) { messageBox(hwnd, L"프레임이 10개 이상일 때 쓸 수 있어요."); return; }
    ReduceParams r;
    if (!reduceDialog(hwnd, st, r)) return;
    Frames src = proj.frames;
    if (r.mode == 3)
        runBg(L"비슷한 프레임을 지우는 중", [=](const Progress&, const Cancel&) { return removeDuplicates(src, r.diff, -1, r.keep, 100000, sel); },
              L"비슷한 프레임을 지웠어요");
    else
        apply(reduceFrames(src, (ReduceMode)r.mode, r.nth, r.keep, sel), L"프레임을 줄였어요");
}

void App::cmdSummary() {
    if (!needFrames() || proj.frames.size() < 2) return;
    SummaryParams p;
    if (!summaryDialog(hwnd, st, p)) return;
    Frames src = proj.frames;
    auto sel = selection();
    runBg(L"영상을 요약하는 중",
          [=](const Progress& pr, const Cancel&) { return videoSummary(src, p.percent, p.scale, p.adjust, p.limit, p.limitUs, sel, pr); },
          L"영상을 요약했어요");
}

void App::cmdDelete() {
    if (proj.frames.empty()) return;
    auto sel = selection();
    if (sel.empty()) sel = {cur_};
    if (sel.size() >= proj.frames.size()) { messageBox(hwnd, L"모든 프레임을 지울 수는 없어요."); return; }
    apply(deleteFrames(proj.frames, sel), std::to_wstring(sel.size()) + L"개 프레임을 지웠어요");
    sel_ = {cur_};
}

void App::cmdRevert() {
    if (needFrames() && messageBox(hwnd, L"처음 불러온 상태로 되돌릴까요?", true)) apply(proj.initial, L"처음 상태로 되돌렸어요");
}

static const wchar_t* kOpenFilter =
    L"움짤·영상·이미지\0*.gif;*.webp;*.png;*.apng;*.avif;*.mp4;*.avi;*.mov;*.mkv;*.wmv;*.m4v;*.webm;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.psd;*.svg;*.jxl;*.heic;*.ico\0모든 파일\0*.*\0";

static std::wstring askOpen(HWND owner, const wchar_t* title) {
    wchar_t buf[MAX_PATH * 4] = {};
    OPENFILENAMEW of{sizeof of};
    of.hwndOwner = owner;
    of.lpstrFilter = kOpenFilter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&of) ? buf : L"";
}

void App::openDialog() {
    if (!confirmDiscard()) return;
    std::wstring p = askOpen(hwnd, L"열기");
    if (!p.empty()) openPath(p, false);
}

void App::cmdAppend() {
    if (!needFrames()) return;
    std::wstring p = askOpen(hwnd, L"뒤에 붙일 파일");
    if (!p.empty()) openPath(p, true);
}

static std::wstring lowerExt(const std::wstring& p) {
    size_t d = p.find_last_of(L'.');
    std::wstring e = d == std::wstring::npos ? L"" : p.substr(d);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

void App::openPath(const std::wstring& p, bool append) {
    std::wstring ext = lowerExt(p);
    Frames out;
    bool isVideo = ext == L".mp4" || ext == L".avi" || ext == L".mov" || ext == L".mkv" || ext == L".wmv" || ext == L".m4v" ||
                   ext == L".webm";
    bool ok;
    if (isVideo) {
        VideoInfo vi;
        if (!videoInfo(p, vi)) { messageBox(hwnd, L"영상을 열 수 없어요.", false, true); return; }
        VideoImportParams vp;
        if (!videoImportDialog(hwnd, p, vi, st, vp)) return;
        ok = runWithProgress(hwnd, L"영상을 읽는 중", [&](const std::function<void(double)>& pr, const std::function<bool()>& c) -> std::string {
            std::string e;
            if (!loadVideo(p, vp.start, vp.end, vp.maxFps, vp.maxSide, out, pr, c, e)) return c() ? "" : e;
            return {};
        });
    } else {
        ok = runWithProgress(hwnd, L"파일을 읽는 중", [&](const std::function<void(double)>&, const std::function<bool()>&) -> std::string {
            std::vector<uint8_t> data;
            if (!readFile(p, data)) return "파일을 읽을 수 없어요";
            std::string e;
            if (data.size() > 6 && !memcmp(data.data(), "GIF", 3)) return decodeGif(data, out, e) ? "" : e;
            if (data.size() > 12 && !memcmp(data.data(), "RIFF", 4) && !memcmp(data.data() + 8, "WEBP", 4)) return decodeWebp(data, out, e) ? "" : e;
            if (data.size() > 8 && data[0] == 137 && data[1] == 'P') { if (decodePng(data, out, e)) return {}; out.clear(); }
            if (data.size() > 12 && !memcmp(data.data() + 4, "ftyp", 4) && (!memcmp(data.data() + 8, "avif", 4) || !memcmp(data.data() + 8, "avis", 4)))
                return decodeAvif(data, out, e) ? "" : e;
            if (data.size() > 4 && !memcmp(data.data(), "8BPS", 4)) {
                Image im;
                if (!decodePsd(data, im, e)) return e;
                out.push_back({std::make_shared<Image>(std::move(im)), st.importFrameMs * 1000LL});
                return {};
            }
            if (ext == L".svg") {
                Image im;
                if (!loadSvg(p, 2048, im, e)) return e;
                out.push_back({std::make_shared<Image>(std::move(im)), st.importFrameMs * 1000LL});
                return {};
            }
            if (loadFramesWic(p, out, st.importFrameMs, e)) return {};   // TIFF pages, JPEG XL/HEIC via Windows codecs, ...
            return e;
        });
    }
    if (!ok || out.empty()) return;
    size_t slash = p.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? p : p.substr(slash + 1);
    if (append && !proj.frames.empty()) {
        int w = proj.frames[0].img->w, h = proj.frames[0].img->h;
        if (out[0].img->w != w || out[0].img->h != h) out = resize(out, w, h);
        Frames f = proj.frames;
        size_t n = out.size();
        f.insert(f.end(), out.begin(), out.end());
        apply(std::move(f), std::to_wstring(n) + L"개 프레임을 뒤에 붙였어요");
    } else {
        setFrames(std::move(out), name, false);
        addRecent(p);
    }
}

void App::importImages() {
    if (!confirmDiscard()) return;
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW of{sizeof of};
    of.hwndOwner = hwnd;
    of.lpstrFilter = L"이미지\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0모든 파일\0*.*\0";
    of.lpstrFile = buf.data();
    of.nMaxFile = (DWORD)buf.size();
    of.lpstrTitle = L"이미지 가져오기 (여러 장 고르기)";
    of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&of)) return;
    std::vector<std::wstring> files;
    std::wstring dir = buf.data();
    const wchar_t* p = buf.data() + dir.size() + 1;
    if (!*p) files.push_back(dir);
    for (; *p; p += wcslen(p) + 1) files.push_back(dir + L"\\" + p);
    std::sort(files.begin(), files.end());
    Frames out;
    int ms = st.importFrameMs;
    bool ok = runWithProgress(hwnd, L"이미지를 가져오는 중", [&](const std::function<void(double)>& pr, const std::function<bool()>& c) -> std::string {
        int W = 0, H = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            if (c()) return {};
            Image im;
            std::string e;
            if (!loadImageWic(files[i], im, false, e)) continue;
            ImagePtr ip = std::make_shared<Image>(std::move(im));
            if (!W) { W = ip->w; H = ip->h; }
            if (ip->w != W || ip->h != H) {   // letterbox into the first image's size
                double s = std::min((double)W / ip->w, (double)H / ip->h);
                auto r = resizeImage(*ip, std::max(1, (int)(ip->w * s)), std::max(1, (int)(ip->h * s)));
                auto canvas = std::make_shared<Image>(W, H, rgb(0, 0, 0));
                int ox = (W - r->w) / 2, oy = (H - r->h) / 2;
                for (int y = 0; y < r->h; ++y) std::copy_n(r->row(y), r->w, canvas->row(oy + y) + ox);
                ip = canvas;
            }
            out.push_back({ip, ms * 1000LL});
            pr((double)(i + 1) / files.size());
        }
        return out.empty() ? "이미지를 읽지 못했어요" : "";
    });
    if (ok && !out.empty()) {
        size_t n = out.size();
        setFrames(std::move(out), L"이미지 " + std::to_wstring(n) + L"장", false);
    }
}

void App::setFrames(Frames f, const std::wstring& label, bool isDirty) {
    stopPlay();
    setTool(T_NONE);
    proj.reset(std::move(f));
    cur_ = 0;
    sel_ = {0};
    anchor_ = 0;
    listScroll_.pos = 0;
    dirty = isDirty;
    if (page_ != P_EDIT) setPage(P_EDIT);
    std::wstring t = widen(kAppName) + L" - " + label;
    SetWindowTextW(hwnd, t.c_str());
    status(label + L" · " + std::to_wstring(proj.frames.size()) + L"프레임을 불러왔어요");
}

bool App::confirmDiscard() {
    return !(dirty && !proj.frames.empty()) || messageBox(hwnd, L"저장하지 않은 작업이 있어요. 버리고 계속할까요?", true);
}

void App::saveDialog() {
    if (!needFrames()) return;
    stopPlay();
    std::wstring path;
    size_t bytes = 0;
    if (bg::saveDialog(hwnd, proj.frames, st, path, bytes)) {
        dirty = false;
        status(L"저장했어요: " + path + L" (" + fmtBytes(bytes) + L")");
    }
}

// ================================================================== recording
void App::openRecorder(bool start) {
    if (rec_) { rec_->show(); if (start) rec_->toggleRecord(); return; }
    if (!confirmDiscard()) return;
    stopPlay();
    ShowWindow(hwnd, SW_HIDE);
    rec_ = std::make_unique<RecorderWindow>(this);
    rec_->show();
    settingsChanged();
    if (start) rec_->startRecord();
}

void App::recordingDone(Frames f) {
    g_pendingRec = std::move(f);
    PostMessageW(hwnd, WM_APP_RECDONE, 0, 0);
}
void App::recorderClosed() { PostMessageW(hwnd, WM_APP_RECCLOSED, 0, 0); }

// ================================================================== tools
void App::setTool(int t) {
    tool_ = t;
    preview_.reset();
    lastSig_.clear();
    selRect_ = {};
    if (proj.frames.empty()) { tool_ = T_NONE; gui_.redraw(); return; }
    const Image& im0 = *proj.frames[0].img;
    if (t == T_RESIZE) { rW_ = im0.w; rH_ = im0.h; }
    if (t == T_CROP) {
        cX_ = im0.w / 8; cY_ = im0.h / 8; cW_ = im0.w * 3 / 4; cH_ = im0.h * 3 / 4;
        selRect_ = {(float)cX_, (float)cY_, (float)cW_, (float)cH_};
    }
    if (t == T_TEXT) { textX_ = im0.w / 20; textY_ = std::max(0, im0.h - tSize_ * 3 / 2); }
    if (t == T_LOGO) { textX_ = 10; textY_ = 10; }
    gui_.redraw();
}

std::vector<int> App::scopeSel() const {
    if (scope_ == 1 && !sel_.empty()) return selection();
    return {};
}

std::wstring App::toolSignature() const {
    wchar_t buf[512];
    std::wstring vals;
    for (int v : effVals_) vals += std::to_wstring(v) + L",";
    swprintf_s(buf, L"%d|%d|%d|%s|%d|%u|%d|%d,%d|%p|%d|%d|%d|%s|%d|%.0f,%.0f,%.0f,%.0f|%p", tool_, cur_, effIdx_, vals.c_str(), tSize_,
               textColor_, outline_, textX_, textY_, (void*)logo_.get(), lScale_, lOpacity_, (int)effRegion_, text_.c_str(), scope_,
               selRect_.x, selRect_.y, selRect_.w, selRect_.h, proj.frames.empty() ? nullptr : (void*)proj.frames[cur_].img.get());
    std::wstring s = buf;
    s += toolSignature2();
    s += L"|" + std::to_wstring(effPx_) + L"," + std::to_wstring(effPy_) + L"," + std::to_wstring(effColor_);
    return s;
}

std::function<ImagePtr(const Image&)> App::toolFn() {
    switch (tool_) {
    case T_EFFECT: {
        int idx = effIdx_;
        auto vals = effVals_;
        const auto& def = effectList()[idx];
        EffectCtx ctx;
        bool useRect = (def.needs & EN_RECT) || ((def.needs & EN_RECT_OPTIONAL) && effRegion_);
        if (useRect && selRect_.w > 2 && selRect_.h > 2) { ctx.rx = (int)selRect_.x; ctx.ry = (int)selRect_.y; ctx.rw = (int)selRect_.w; ctx.rh = (int)selRect_.h; }
        ctx.px = effPx_;
        ctx.py = effPy_;
        ctx.color = effColor_;
        return [=](const Image& im) { return applyEffect(idx, im, vals, ctx); };
    }
    case T_TEXT: {
        std::wstring text = text_;
        TextStyle ts;
        ts.size = tSize_;
        ts.color = 0xFF000000u | textColor_;
        ts.outline = outline_;
        int x = textX_, y = textY_;
        return [=](const Image& im) { return drawText(im, text, x, y, ts); };
    }
    case T_LOGO: {
        if (!logo_) return nullptr;
        double sc = lScale_ / 100.0, op = lOpacity_ / 100.0;
        ImagePtr lg = logo_;
        if (sc != 1.0) {   // nearest-scaled RGBA keeps the logo's alpha
            auto a = std::make_shared<Image>(std::max(1, (int)(logo_->w * sc)), std::max(1, (int)(logo_->h * sc)));
            for (int y = 0; y < a->h; ++y)
                for (int x = 0; x < a->w; ++x)
                    a->row(y)[x] = logo_->row(std::min(logo_->h - 1, (int)(y / sc)))[std::min(logo_->w - 1, (int)(x / sc))];
            lg = a;
        }
        int x = textX_, y = textY_;
        return [=](const Image& im) { return overlay(im, *lg, x, y, op); };
    }
    default: return toolFn2();
    }
}

void App::updatePreview() {
    KillTimer(hwnd, ID_PREVIEWTIMER);
    bool previews = tool_ == T_EFFECT || tool_ == T_TEXT || tool_ == T_LOGO || tool_ == T_INSERT || tool_ == T_DRAW || tool_ == T_FRAME;
    if (proj.frames.empty() || !previews || dragging_) { if (preview_ && !dragging_) { preview_.reset(); gui_.redraw(); } return; }
    auto fn = toolFn();
    preview_ = fn ? fn(*proj.frames[cur_].img) : nullptr;
    gui_.redraw();
}

// ================================================================== keyboard
bool App::preTranslate(MSG& m) {
    if (rec_ && rec_->preTranslate(m)) return true;
    if (hotkeyCapture_ && GetAncestor(m.hwnd, GA_ROOT) == hwnd &&
        (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN ||
         ((m.message == WM_KEYUP || m.message == WM_SYSKEYUP) && m.wParam == VK_SNAPSHOT))) {   // PrtSc only sends key-up
        int vk = (int)m.wParam;
        if (vk == VK_PROCESSKEY) vk = (int)ImmGetVirtualKey(hwnd);
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_LCONTROL ||
            vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LMENU || vk == VK_RMENU || vk == 0)
            return true;
        int mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
        if (vk == VK_CANCEL) vk = VK_PAUSE;   // Ctrl+Pause arrives as VK_CANCEL
        int& slot = hotkeyCapture_ == 1 ? st.hotkeyRec : st.hotkeyPause;
        int other = hotkeyCapture_ == 1 ? st.hotkeyPause : st.hotkeyRec;
        auto finish = [&] { hotkeyCapture_ = 0; st.save(); registerHotkeys(); gui_.redraw(); };
        if (!mods && vk == VK_ESCAPE) { hotkeyCapture_ = 0; registerHotkeys(); gui_.redraw(); return true; }
        if (!mods && (vk == VK_DELETE || vk == VK_BACK)) { slot = 0; finish(); return true; }   // clear
        // keys that type text or move focus would be swallowed everywhere without a modifier
        bool typing = (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || vk == VK_SPACE || vk == VK_RETURN ||
                      vk == VK_TAB || (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
                      vk == VK_OEM_102;
        if (!mods && typing) {
            status(tr(L"글자 키는 Ctrl, Alt, Shift 중 하나와 함께 눌러 주세요"));
            return true;
        }
        int hk = (mods << 16) | vk;
        if (hk == other) {
            status(tr(L"다른 단축키와 같아요") + L" (" + hotkeyName(hk) + L")");
            return true;
        }
        slot = hk;
        finish();
        return true;
    }
    if (m.message != WM_KEYDOWN || GetAncestor(m.hwnd, GA_ROOT) != hwnd) return false;
    if (page_ == P_CLIP && clip_) {   // WebP clip page: its own shortcuts (not while typing in an edit box)
        wchar_t c2[32] = {};
        GetClassNameW(GetFocus(), c2, 32);
        bool ctrlE = m.wParam == 'E' && (GetKeyState(VK_CONTROL) & 0x8000);   // export works while typing too
        if (_wcsicmp(c2, L"Edit") == 0 && m.wParam != VK_ESCAPE && m.wParam != VK_F1 && !ctrlE) return false;
        if (clip_->key((int)m.wParam)) { gui_.redraw(); clipTimer(); return true; }
        return false;
    }
    if (page_ != P_EDIT) {   // other pages: only Ctrl+O works
        if ((GetKeyState(VK_CONTROL) & 0x8000) && m.wParam == 'O') { openDialog(); return true; }
        return false;
    }
    wchar_t cls[32];
    GetClassNameW(m.hwnd, cls, 32);
    bool inEdit = !_wcsicmp(cls, L"EDIT");
    bool ctrl = GetKeyState(VK_CONTROL) & 0x8000, shift = GetKeyState(VK_SHIFT) & 0x8000;
    WPARAM k = m.wParam;
    if (ctrl) {
        switch (k) {
        case 'Z': if (inEdit) return false; undo(); return true;
        case 'Y': if (inEdit) return false; redo(); return true;
        case 'O': openDialog(); return true;
        case 'S': saveDialog(); return true;
        case 'F': shift ? cmdSetTime(false) : cmdSetTime(true); return true;
        case 'K': if (shift) { cmdSpeed(0.9); return true; } break;
        case 'J': if (shift) { cmdSpeed(1.1); return true; } break;
        case 'A':
            if (inEdit) return false;
            sel_.clear();
            for (int i = 0; i < (int)proj.frames.size(); ++i) sel_.insert(i);
            gui_.redraw();
            return true;
        }
        return false;
    }
    if (inEdit) return false;
    switch (k) {
    case VK_SPACE: togglePlay(); return true;
    case VK_LEFT: case VK_UP: step(-1); return true;
    case VK_RIGHT: case VK_DOWN: step(1); return true;
    case VK_HOME: gotoFrame(0); return true;
    case VK_END: gotoFrame((int)proj.frames.size() - 1); return true;
    case VK_DELETE: cmdDelete(); return true;
    case 'X': cmdTrim(); return true;
    case 'Q': cmdReduce(false); return true;
    case 'S': cmdSummary(); return true;
    }
    return false;
}

// ================================================================== window procedure
LRESULT CALLBACK App::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* a = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        a = (App*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
        a->hwnd = h;
    }
    return a ? a->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::hitTest(int sx, int sy) {
    POINT p{sx, sy};
    ScreenToClient(hwnd, &p);
    RECT rc;
    GetClientRect(hwnd, &rc);
    float s = gui_.s();
    int border = (int)(6 * s);
    if (!IsZoomed(hwnd)) {
        bool l = p.x < border, r = p.x >= rc.right - border, t = p.y < border, b = p.y >= rc.bottom - border;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
    }
    float x = p.x / s, y = p.y / s;
    if (maxR_.contains(x, y)) return HTMAXBUTTON;
    if (minR_.contains(x, y) || closeR_.contains(x, y)) return HTCLIENT;
    if (y < kTitleH && (x > kSidebarW + 220 || y < 12)) return HTCAPTION;   // title strip (not over the page title)
    if (y < 64 && x < kSidebarW && !(x > 14 && y > 60)) return HTCAPTION;   // brand area of the side bar
    return HTCLIENT;
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT r;
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) {
            if (IsZoomed(hwnd)) {   // keep content inside the monitor when maximized
                auto* p = (NCCALCSIZE_PARAMS*)lp;
                int f = GetSystemMetricsForDpi(SM_CXSIZEFRAME, GetDpiForWindow(hwnd)) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, GetDpiForWindow(hwnd));
                p->rgrc[0].left += f; p->rgrc[0].right -= f; p->rgrc[0].top += f; p->rgrc[0].bottom -= f;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_NCMOUSEMOVE:
        if (wp == HTMAXBUTTON) {
            if (!maxHot_) {
                maxHot_ = true;
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                TrackMouseEvent(&t);
                gui_.redraw();
            }
            return 0;
        }
        if (maxHot_) { maxHot_ = false; maxDown_ = false; gui_.redraw(); }
        break;
    case WM_NCMOUSELEAVE:
        maxHot_ = maxDown_ = false;
        gui_.redraw();
        break;
    case WM_NCLBUTTONDOWN:
        if (wp == HTMAXBUTTON) { maxDown_ = true; gui_.redraw(); return 0; }
        break;
    case WM_NCLBUTTONUP:
        if (wp == HTMAXBUTTON) {
            if (maxDown_) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            maxDown_ = false;
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (maxHot_) { maxHot_ = false; maxDown_ = false; }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint();
        EndPaint(hwnd, &ps);
        gui_.runLater();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        float s = hwnd ? gui_.s() : 1.f;
        ((MINMAXINFO*)lp)->ptMinTrackSize = {(LONG)(1080 * s), (LONG)(640 * s)};
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* rr = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, rr->left, rr->top, rr->right - rr->left, rr->bottom - rr->top, SWP_NOZORDER | SWP_NOACTIVATE);
        gui_.g.resize();
        return 0;
    }
    case WM_TIMER:
        if (wp == 14) {   // WebP clip page: playback / tracking / export progress
            if (clip_ && page_ == P_CLIP) clip_->tick();
            clipTimer();
            return 0;
        }
        if (wp == ID_PLAYTIMER && playing_ && !proj.frames.empty()) {
            cur_ = (cur_ + 1) % (int)proj.frames.size();
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            SetTimer(hwnd, ID_PLAYTIMER, (UINT)std::max<int64_t>(10, proj.frames[cur_].dur / 1000), nullptr);
            return 0;
        }
        if (wp == ID_PREVIEWTIMER) { updatePreview(); return 0; }
        if (wp == 13) {
            if (page_ != P_MANAGE || managePreview_.size() < 2) { KillTimer(hwnd, 13); return 0; }
            manageCur_ = (manageCur_ + 1) % (int)managePreview_.size();
            InvalidateRect(hwnd, nullptr, FALSE);
            SetTimer(hwnd, 13, (UINT)std::max<int64_t>(10, managePreview_[manageCur_].dur / 1000), nullptr);
            return 0;
        }
        if (wp == 12) {   // Ctrl+Alt follow
            static POINT last{};
            POINT p;
            GetCursorPos(&p);
            if (rec_ && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000)) rec_->followMouse(p.x - last.x, p.y - last.y);
            last = p;
            if (!rec_) KillTimer(hwnd, 12);
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wp == 1 || wp == 3) { if (!rec_) openRecorder(true); else rec_->toggleRecord(); }
        else if (wp == 2 && rec_) rec_->togglePause();
        return 0;
    case WM_APP_RECDONE:
        rec_.reset();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        setFrames(std::move(g_pendingRec), L"녹화", true);
        return 0;
    case WM_APP_RECCLOSED:
        rec_.reset();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        return 0;
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        wchar_t buf[MAX_PATH * 2];
        if (page_ == P_BATCH && !batchRunning_) {   // batch page: add every dropped file
            std::vector<std::wstring> files;
            UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i)
                if (DragQueryFileW(d, i, buf, MAX_PATH * 2)) files.push_back(buf);
            DragFinish(d);
            batchAddFiles(files);
            gui_.redraw();
            return 0;
        }
        bool got = DragQueryFileW(d, 0, buf, MAX_PATH * 2) > 0;
        DragFinish(d);
        if (got && page_ == P_CLIP) {   // WebP clip page opens the video itself
            if (!clip_) clip_ = std::make_unique<ClipPage>(hwnd, gui_, st, [this](const std::wstring& s) { status(s); });
            clip_->open(buf);
            clipTimer();
            return 0;
        }
        if (got && confirmDiscard()) openPath(buf, false);
        return 0;
    }
    case WM_CLOSE:
        if (rec_ && rec_->recording()) return 0;
        if (!confirmDiscard()) return 0;
        {
            WINDOWPLACEMENT wpl{sizeof wpl};
            GetWindowPlacement(hwnd, &wpl);
            st.winX = wpl.rcNormalPosition.left;
            st.winY = wpl.rcNormalPosition.top;
            st.winW = wpl.rcNormalPosition.right - wpl.rcNormalPosition.left;
            st.winH = wpl.rcNormalPosition.bottom - wpl.rcNormalPosition.top;
            st.save();
        }
        rec_.reset();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, 1);
        UnregisterHotKey(hwnd, 2);
        UnregisterHotKey(hwnd, 3);
        PostQuitMessage(0);
        return 0;
    }
    if (gui_.hwnd && gui_.handle(msg, wp, lp, r)) return r;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace bg
