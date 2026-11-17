#include "ui/Recorder.h"
#include "ui/Lang.h"

#include "platform/Media.h"
#include "platform/Ocr.h"
#include "ui/App.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

namespace bg {

static const wchar_t* kRecClass = L"BongGIFRecorder";
static const wchar_t* kPickClass = L"BongGIFPicker";
static const wchar_t* kOverlayClass = L"BongGIFOverlay";
// Size presets (keys 1–9), 1280×720 (key 0), then 1024×768 and 1080p
static const int kSizes[][2] = {{320, 200}, {320, 240}, {400, 300}, {480, 360}, {640, 360}, {640, 480},
                                {720, 480}, {800, 600}, {960, 720}, {1280, 720}, {1024, 768}, {1920, 1080}};
static const int kNumSizes = 12;
static const int kFps[] = {1, 5, 10, 15, 20, 25, 30, 33, 50, 60};
static const UINT_PTR TIMER_REC = 1, TIMER_MAG = 3, TIMER_FLASH = 4;

namespace {
struct BarItem { int kind; float w; };
enum { K_BRAND, K_FPS, K_SIZE, K_PICK, K_FIND, K_CURSOR, K_HL, K_CLICK, K_IMG, K_OCR, K_GRID, K_DIRECT, K_MORE,
       K_TIME, K_PAUSE, K_REC, K_CLOSE, K_COUNT };
const float kRowH = 44, kPad = 8, kGap = 6;
std::vector<BarItem> barItems() {
    if (language() == 1)   // English labels are longer
        return {{K_BRAND, 90}, {K_FPS, 92}, {K_SIZE, 132}, {K_PICK, 110}, {K_FIND, 118}, {K_CURSOR, 100}, {K_HL, 120}, {K_CLICK, 96},
                {K_IMG, 34},   {K_OCR, 34}, {K_GRID, 34},   {K_DIRECT, 34}, {K_MORE, 34}, {K_TIME, 72}, {K_PAUSE, 34}, {K_REC, 96}, {K_CLOSE, 34}};
    return {{K_BRAND, 74}, {K_FPS, 92}, {K_SIZE, 132}, {K_PICK, 96}, {K_FIND, 84}, {K_CURSOR, 86}, {K_HL, 86}, {K_CLICK, 86},
            {K_IMG, 34},   {K_OCR, 34}, {K_GRID, 34},   {K_DIRECT, 34}, {K_MORE, 34}, {K_TIME, 72}, {K_PAUSE, 34}, {K_REC, 92}, {K_CLOSE, 34}};
}
// flow layout; the last four items (time, pause, record, close) stay together on the right of the last row
float layoutBar(float W, std::vector<RectF>& out) {
    auto items = barItems();
    out.assign(items.size(), {});
    float x = kPad, row = 0;
    size_t rightStart = items.size() - 4;
    for (size_t i = 0; i < rightStart; ++i) {
        if (x > kPad && x + items[i].w > W - kPad) { ++row; x = kPad; }
        out[i] = {x, kPad / 2 + row * kRowH + 6, items[i].w, 32};
        x += items[i].w + kGap;
    }
    float rightW = 0;
    for (size_t i = rightStart; i < items.size(); ++i) rightW += items[i].w + kGap;
    if (x + rightW > W - kPad) ++row;
    float rx = std::max(kPad, W - kPad - rightW + kGap);
    for (size_t i = rightStart; i < items.size(); ++i) {
        out[i] = {rx, kPad / 2 + row * kRowH + 6, items[i].w, 32};
        rx += items[i].w + kGap;
    }
    return kPad + (row + 1) * kRowH;
}
std::wstring stamp(const wchar_t* prefix) {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &now);
    wchar_t b[96];
    wcsftime(b, 96, L"%Y-%m-%d %H-%M-%S", &tmv);
    return std::wstring(prefix) + b;
}
} // namespace

RecorderWindow::RecorderWindow(App* app) : app_(app) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kRecClass;
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        WNDCLASSEXW pc{sizeof pc};
        pc.lpfnWndProc = PickProc;
        pc.hInstance = GetModuleHandleW(nullptr);
        pc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        pc.lpszClassName = kPickClass;
        RegisterClassExW(&pc);
        WNDCLASSEXW oc{sizeof oc};
        oc.lpfnWndProc = OverlayProc;
        oc.hInstance = GetModuleHandleW(nullptr);
        oc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        oc.lpszClassName = kOverlayClass;
        RegisterClassExW(&oc);
        reg = true;
    }
    const Settings& st = app_->st;
    for (int i = 0; i < 10; ++i) if (kFps[i] == st.fps) fpsSel_ = i;
    cursor_ = st.cursor;
    highlight_ = st.highlight;
    click_ = st.clickFx;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, kRecClass, L"BongGIF - 녹화", WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX,
                            100, 100, 700, 450, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    gui_.attach(hwnd_);
    setCaptureRect(st.recRect);
}

RecorderWindow::~RecorderWindow() {
    rec_.stop();
    if (pickWnd_) DestroyWindow(pickWnd_);
    if (gridWnd_) DestroyWindow(gridWnd_);
    if (magWnd_) DestroyWindow(magWnd_);
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

void RecorderWindow::show() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    updateOverlays();
}

RECT RecorderWindow::captureRect() const {
    if (full_) {
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        return mi.rcMonitor;
    }
    float s = gui_.g.scale();
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    int bw = (int)std::lround(bw_ * s), bar = (int)std::lround(barH_ * s);
    return {wr.left + bw, wr.top + bar + bw, wr.right - bw, wr.bottom - bw};
}

void RecorderWindow::setCaptureRect(const RECT& r) {
    float s = gui_.g.scale();
    int w = std::max<int>(16, r.right - r.left), h = std::max<int>(16, r.bottom - r.top);
    int bw = (int)std::lround(bw_ * s);
    std::vector<RectF> tmp;
    barH_ = layoutBar((w + 2 * bw) / s, tmp);
    int bar = (int)std::lround(barH_ * s);
    SetWindowPos(hwnd_, nullptr, r.left - bw, r.top - bar - bw, w + 2 * bw, h + bar + 2 * bw, SWP_NOZORDER | SWP_NOACTIVATE);
    updateRegion();
    updateOverlays();
}

void RecorderWindow::followMouse(int dx, int dy) {
    if (rec_.running() || full_) return;
    RECT r = captureRect();
    OffsetRect(&r, dx, dy);
    setCaptureRect(r);
}

void RecorderWindow::updateRegion() {
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    float s = gui_.g.scale();
    int bw = (int)std::lround(bw_ * s), bar = (int)std::lround(barH_ * s);
    HRGN outer = CreateRectRgn(0, 0, W, H);
    HRGN hole = CreateRectRgn(bw, bar + bw, W - bw, H - bw);
    CombineRgn(outer, outer, hole, RGN_DIFF);
    DeleteObject(hole);
    SetWindowRgn(hwnd_, outer, TRUE);
}

// ------------------------------------------------------------------ overlays (grid, magnifier)
LRESULT CALLBACK RecorderWindow::OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (RecorderWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_PAINT && self) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (h == self->gridWnd_) {   // rule-of-thirds lines on a colour-keyed (black = transparent) window
            FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            HPEN pen = CreatePen(PS_DOT, 1, RGB((th::accent >> 16) & 255, (th::accent >> 8) & 255, th::accent & 255));
            HGDIOBJ op = SelectObject(dc, pen);
            SetBkMode(dc, TRANSPARENT);
            for (int k = 1; k <= 2; ++k) {
                MoveToEx(dc, rc.right * k / 3, 0, nullptr); LineTo(dc, rc.right * k / 3, rc.bottom);
                MoveToEx(dc, 0, rc.bottom * k / 3, nullptr); LineTo(dc, rc.right, rc.bottom * k / 3);
            }
            SelectObject(dc, op);
            DeleteObject(pen);
        } else {                      // magnifier: 4x zoom around the cursor
            POINT c;
            GetCursorPos(&c);
            int src = rc.right / 4;
            HDC sdc = GetDC(nullptr);
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchBlt(dc, 0, 0, rc.right, rc.bottom, sdc, c.x - src / 2, c.y - src / 2, src, src, SRCCOPY | CAPTUREBLT);
            ReleaseDC(nullptr, sdc);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 80, 80));
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, rc.right / 2, 0, nullptr); LineTo(dc, rc.right / 2, rc.bottom);
            MoveToEx(dc, 0, rc.bottom / 2, nullptr); LineTo(dc, rc.right, rc.bottom / 2);
            SelectObject(dc, op);
            DeleteObject(pen);
            FrameRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

void RecorderWindow::updateOverlays() {
    bool want = app_->st.showGrid && IsWindowVisible(hwnd_) && !full_;
    if (want && !gridWnd_) {
        gridWnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kOverlayClass, L"",
                                   WS_POPUP, 0, 0, 10, 10, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(gridWnd_, GWLP_USERDATA, (LONG_PTR)this);
        SetLayeredWindowAttributes(gridWnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);
        excludeFromCapture(gridWnd_, true);   // the grid never ends up in the recording
    }
    if (!want && gridWnd_) { DestroyWindow(gridWnd_); gridWnd_ = nullptr; }
    if (gridWnd_) {
        RECT r = captureRect();
        SetWindowPos(gridWnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(gridWnd_, nullptr, TRUE);
    }
}

// ------------------------------------------------------------------ painting
void RecorderWindow::paint() {
    if (!gui_.beginFrame()) return;
    Gfx& g = gui_.g;
    float W = g.width(), H = g.height();
    std::vector<RectF> pos;
    float bh = layoutBar(W, pos);
    if (std::abs(bh - barH_) > 0.5f) {   // wrapping changed: keep the capture area where it is
        RECT cr = captureRect();
        gui_.later([this, cr, bh] { barH_ = bh; setCaptureRect(cr); });
    }
    Settings& st = app_->st;
    bool recording = rec_.running();
    if (recording) g.fillH({0, 0, W, H}, 0, th::red, th::pink);
    else g.fillH({0, 0, W, H}, 0, th::accent, th::accentEnd);
    RectF bar{0, 0, W, barH_};
    g.fill(bar, 0, th::background);
    g.fillV(bar, 0, th::cardTop, th::cardBottom);
    g.line(0, barH_ - 0.5f, W, barH_ - 0.5f, th::separator);
    ctlRects_.clear();
    std::vector<std::wstring> fps;
    for (int v : kFps) fps.push_back(std::to_wstring(v) + L" fps");
    std::vector<std::wstring> sizes;
    for (int i = 0; i < kNumSizes; ++i) {
        std::wstring k = i < 9 ? L"  (" + std::to_wstring(i + 1) + L")" : i == 9 ? L"  (0)" : L"";
        sizes.push_back(std::to_wstring(kSizes[i][0]) + L" × " + std::to_wstring(kSizes[i][1]) + k);
    }
    sizes.push_back(L"전체 화면");
    RECT cr = captureRect();
    int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
    int match = full_ ? kNumSizes : -1;
    for (int i = 0; i < kNumSizes && match < 0; ++i) if (kSizes[i][0] == cw && kSizes[i][1] == ch) match = i;
    if (match < 0) { sizes.push_back(std::to_wstring(cw) + L" × " + std::to_wstring(ch)); match = (int)sizes.size() - 1; }
    sizeSel_ = match;
    bool idle = !recording;
    auto activeTint = [&](RectF r, bool on) { if (on) g.fill(r, th::radiusControl, (th::accent & 0x00FFFFFF) | 0x40000000); };
    for (size_t i = 0; i < pos.size(); ++i) {
        RectF r = pos[i];
        if (i != K_BRAND && i != K_TIME) ctlRects_.push_back(r);
        switch ((int)i) {
        case K_BRAND: {
            gui_.iconTile({r.x, r.y + 3, 26, 26}, ic::Record, recording ? th::red : th::accent, recording ? th::pink : th::accentEnd);
            std::wstring t = flash_.empty() ? L"녹화" : L"";
            g.text(t, {r.x + 34, r.y, r.w - 34, r.h}, th::textPrimary, th::body, 600);
            break;
        }
        case K_FPS:
            gui_.dropdown(gid("rc.fps"), r, fps, fpsSel_, idle);
            break;
        case K_SIZE:
            if (gui_.dropdown(gid("rc.size"), r, sizes, sizeSel_, idle)) {
                if (sizeSel_ < kNumSizes) {
                    full_ = false;
                    RECT c = captureRect();
                    int s0 = sizeSel_;
                    gui_.later([this, c, s0] { setCaptureRect({c.left, c.top, c.left + kSizes[s0][0], c.top + kSizes[s0][1]}); });
                } else if (sizeSel_ == kNumSizes) {
                    full_ = true;
                    gui_.later([this] { updateOverlays(); });
                }
            }
            break;
        case K_PICK:
            if (gui_.button(gid("rc.pick"), r, L"영역 선택", BS_SECONDARY, 0, idle)) gui_.later([this] { pick(false); });
            break;
        case K_FIND:
            if (gui_.button(gid("rc.find"), r, L"창 찾기", BS_SECONDARY, 0, idle)) gui_.later([this] { pick(true); });
            break;
        case K_CURSOR:
        case K_HL:
        case K_CLICK: {
            bool en = idle && (i == K_CURSOR || cursor_);
            bool& v = i == K_CURSOR ? cursor_ : i == K_HL ? highlight_ : click_;
            const wchar_t* t = i == K_CURSOR ? L"커서" : i == K_HL ? L"강조" : L"클릭";
            g.text(t, {r.x, r.y, r.w - 44, r.h}, gui_.dim(th::textPrimary, en), th::body);
            gui_.toggle(gid(gid("rc.tg"), i), {r.r() - 40, r.y + 6, 40, 20}, v, en);
            break;
        }
        case K_IMG:
            if (gui_.iconButton(gid("rc.img"), r, ic::Camera, idle)) gui_.later([this] { captureImage(); });
            break;
        case K_OCR:
            if (gui_.iconButton(gid("rc.ocr"), r, ic::TextRead, idle)) gui_.later([this] { captureText(); });
            break;
        case K_GRID:
            activeTint(r, st.showGrid);
            if (gui_.iconButton(gid("rc.grid"), r, ic::Grid)) gui_.later([this] { app_->st.showGrid = !app_->st.showGrid; app_->st.save(); updateOverlays(); gui_.redraw(); });
            break;
        case K_DIRECT:
            activeTint(r, st.directSave);
            if (gui_.iconButton(gid("rc.direct"), r, ic::Save, idle)) gui_.later([this] {
                app_->st.directSave = !app_->st.directSave;
                app_->st.save();
                flash(app_->st.directSave ? L"바로 저장 켬: 녹화가 끝나면 MP4로 바로 저장돼요" : L"바로 저장 끔");
            });
            break;
        case K_MORE:
            if (gui_.iconButton(gid("rc.more"), r, ic::More, idle)) {
                RECT a{(LONG)(r.x * gui_.s()), (LONG)(r.b() * gui_.s()), (LONG)(r.r() * gui_.s()), (LONG)(r.b() * gui_.s())};
                MapWindowPoints(hwnd_, nullptr, (POINT*)&a, 2);
                gui_.later([this, a] { moreMenu(a); });
            }
            break;
        case K_TIME: {
            double t = rec_.elapsedUs() / 1e6;
            wchar_t buf[32];
            swprintf_s(buf, L"%02d:%04.1f", (int)(t / 60), std::fmod(t, 60.0));
            gui_.chip(r.inset(0, 4), buf, recording ? th::textPrimary : th::textSecondary, recording ? (th::red & 0x55FFFFFF) : th::control);
            break;
        }
        case K_PAUSE:
            if (gui_.iconButton(gid("rc.pause"), r, rec_.paused() ? ic::Play : ic::Pause, recording)) gui_.later([this] { togglePause(); });
            break;
        case K_REC:
            if (gui_.button(gid("rc.rec"), r, recording ? L"정지" : L"녹화", recording ? BS_DANGER : BS_PRIMARY, recording ? ic::Stop : ic::Record))
                gui_.later([this] { toggleRecord(); });
            break;
        case K_CLOSE:
            if (gui_.iconButton(gid("rc.close"), r, ic::Close, true, th::textSecondary, 10)) gui_.later([this] { close(); });
            break;
        }
    }
    if (!flash_.empty()) {   // short status message over the brand label
        RectF fr{pos[K_BRAND].x + 32, pos[K_BRAND].y, std::max(160.f, pos[K_FPS].x - pos[K_BRAND].x + 200), 32};
        float tw = g.textWidth(flash_, th::caption, 600) + 20;
        fr.w = tw;
        g.fill(fr, 16, th::menuBackground);
        g.stroke(fr, 16, th::menuStroke, 1);
        g.text(flash_, fr, th::textPrimary, th::caption, 600, A_CENTER);
    }
    gui_.endFrame();
}

void RecorderWindow::flash(const std::wstring& msg) {
    flash_ = msg;
    SetTimer(hwnd_, TIMER_FLASH, 3000, nullptr);
    gui_.redraw();
}

void RecorderWindow::moreMenu(RECT a) {
    Settings& st = app_->st;
    std::vector<std::wstring> items = {L"원래 크기로 녹화", L"절반 크기로 녹화", L"1/4 크기로 녹화", L"-",
                                       L"위치 1로 옮기기  Ctrl+1", L"위치 2로 옮기기  Ctrl+2", L"위치 3으로 옮기기  Ctrl+3",
                                       L"지금 위치를 1에 저장  Ctrl+Shift+1", L"지금 위치를 2에 저장  Ctrl+Shift+2", L"지금 위치를 3에 저장  Ctrl+Shift+3", L"-",
                                       L"창 크기 조절할 때 돋보기", L"Ctrl+Alt로 창 끌고 다니기", L"Ctrl+PrtSc로 녹화 시작·정지", L"DXGI 캡처 (HDR 화면 지원)",
                                       L"바로 저장할 때 시간 제한 없음", L"녹화하는 동안 이 창 숨기기 (캡처에서 빼기)"};
    std::vector<int> checks = {st.recScaleDiv == 1, st.recScaleDiv == 2, st.recScaleDiv == 4, -1, -1, -1, -1, -1, -1, -1, -1,
                               st.useMagnifier, st.trackMouse, st.ctrlPrtSc, st.useDxgi, st.directNoTimeLimit, st.excludeSelf};
    showMenuChecks(hwnd_, a, items, checks, [this](int i) {
        Settings& s = app_->st;
        switch (i) {
        case 0: s.recScaleDiv = 1; break;
        case 1: s.recScaleDiv = 2; break;
        case 2: s.recScaleDiv = 4; break;
        case 4: case 5: case 6: {
            RECT r = s.recPos[i - 4];
            if (r.right > r.left) { full_ = false; setCaptureRect(r); }
            else flash(L"저장된 위치가 없어요. Ctrl+Shift+숫자로 먼저 저장해요");
            break;
        }
        case 7: case 8: case 9: s.recPos[i - 7] = captureRect(); flash(L"위치 " + std::to_wstring(i - 6) + L"에 저장했어요"); break;
        case 11: s.useMagnifier = !s.useMagnifier; break;
        case 12: s.trackMouse = !s.trackMouse; break;
        case 13: s.ctrlPrtSc = !s.ctrlPrtSc; break;
        case 14: s.useDxgi = !s.useDxgi; break;
        case 15: s.directNoTimeLimit = !s.directNoTimeLimit; break;
        case 16: s.excludeSelf = !s.excludeSelf; break;
        }
        s.save();
        app_->settingsChanged();
        gui_.redraw();
    });
}

// ------------------------------------------------------------------ recording
void RecorderWindow::startRecord() {
    if (rec_.running()) return;
    Settings& st = app_->st;
    st.fps = kFps[std::clamp(fpsSel_, 0, 9)];
    st.cursor = cursor_;
    st.highlight = highlight_;
    st.clickFx = click_;
    RECT r = captureRect();
    if (!full_) st.recRect = r;
    st.save();
    RecordOptions o;
    o.rect = r;
    o.fps = st.fps;
    o.cursor = cursor_;
    o.highlight = cursor_ && highlight_;
    o.clickFx = cursor_ && click_;
    o.skipSame = st.skipSame;
    o.maxSec = st.maxRecSec;
    o.maxMemMB = st.maxMemMB;
    o.useDxgi = st.useDxgi;
    o.scaleDiv = st.recScaleDiv;
    if (st.directSave) {
        SHCreateDirectoryExW(nullptr, st.saveDir.c_str(), nullptr);
        o.directSavePath = st.saveDir + L"\\" + stamp(L"BongGIF ") + L".mp4";
        o.directQuality = st.directQuality;
        o.noTimeLimit = st.directNoTimeLimit;
    }
    if (st.excludeSelf) excludeFromCapture(hwnd_, true);   // exclude own windows from capture
    rec_.start(o, hwnd_, WM_APP_RECSTOP);
    SetTimer(hwnd_, TIMER_REC, 100, nullptr);
    gui_.redraw();
}

void RecorderWindow::togglePause() {
    if (!rec_.running()) return;
    rec_.togglePause();
    gui_.redraw();
}

void RecorderWindow::stopRecord() {
    if (!rec_.running()) return;
    rec_.stop();
    KillTimer(hwnd_, TIMER_REC);
    excludeFromCapture(hwnd_, false);
    int reason = rec_.stopReason();
    std::wstring direct = rec_.directSavePath();
    Frames f = rec_.takeFrames();
    gui_.redraw();
    if (reason == 1) messageBox(hwnd_, L"최대 녹화 시간이 지나서 녹화를 멈췄어요.");
    if (reason == 2) messageBox(hwnd_, L"메모리 한도에 닿아서 녹화를 멈췄어요.");
    if (reason == 3) { messageBox(hwnd_, L"MP4로 바로 저장하지 못했어요.", false, true); return; }
    if (!direct.empty()) {   // direct save: the file is already written, stay on the recorder
        flash(L"저장했어요: " + direct.substr(direct.find_last_of(L'\\') + 1));
        if (app_->st.directOpenFolder) {
            std::wstring arg = L"/select,\"" + direct + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (!f.empty()) {
        ShowWindow(hwnd_, SW_HIDE);
        if (gridWnd_) ShowWindow(gridWnd_, SW_HIDE);
        app_->recordingDone(std::move(f));   // the app destroys this window later
    }
}

void RecorderWindow::close() {
    if (rec_.running()) { stopRecord(); return; }
    if (!full_) { app_->st.recRect = captureRect(); app_->st.save(); }
    if (gridWnd_) ShowWindow(gridWnd_, SW_HIDE);
    app_->recorderClosed();
}

// ------------------------------------------------------------------ image / text capture
void RecorderWindow::captureImage() {
    RECT r = captureRect();
    if (gridWnd_) ShowWindow(gridWnd_, SW_HIDE);
    ImagePtr im = grabScreen(r);
    if (gridWnd_) ShowWindow(gridWnd_, SW_SHOWNA);
    Settings& st = app_->st;
    SHCreateDirectoryExW(nullptr, st.saveDir.c_str(), nullptr);
    std::wstring path = st.saveDir + L"\\" + stamp(L"BongGIF 캡처 ") + L".png";
    std::string err;
    bool saved = saveImageWic(*im, path, false, 90, err);
    setClipboardImage(hwnd_, *im);
    flash(saved ? L"이미지를 저장하고 복사했어요" : L"클립보드에 복사했어요 (파일 저장은 실패)");
}

void RecorderWindow::captureText() {
    RECT r = captureRect();
    if (gridWnd_) ShowWindow(gridWnd_, SW_HIDE);
    ImagePtr im = grabScreen(r);
    if (gridWnd_) ShowWindow(gridWnd_, SW_SHOWNA);
    std::wstring text;
    bool ok = runWithProgress(hwnd_, L"글자를 읽는 중", [&](const std::function<void(double)>&, const std::function<bool()>&) -> std::string {
        std::string e;
        return ocrImage(*im, text, e) ? "" : e;
    });
    if (!ok) return;
    if (text.empty()) { messageBox(hwnd_, L"읽을 수 있는 글자를 찾지 못했어요."); return; }
    setClipboardText(hwnd_, text);
    std::wstring preview = text.size() > 400 ? text.substr(0, 400) + L"…" : text;
    messageBox(hwnd_, L"읽은 글자를 클립보드에 복사했어요.\n\n" + preview);
}

// ------------------------------------------------------------------ region / window picker
void RecorderWindow::pick(bool windowMode) {
    pickWindow_ = windowMode;
    picking_ = false;
    RECT vs = virtualScreen();
    ShowWindow(hwnd_, SW_HIDE);
    if (gridWnd_) ShowWindow(gridWnd_, SW_HIDE);
    pickWnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kPickClass, L"", WS_POPUP, vs.left, vs.top,
                               vs.right - vs.left, vs.bottom - vs.top, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    SetLayeredWindowAttributes(pickWnd_, 0, 110, LWA_ALPHA);
    pickGui_.attach(pickWnd_);
    ShowWindow(pickWnd_, SW_SHOW);
    SetForegroundWindow(pickWnd_);
    SetCapture(pickWnd_);
}

LRESULT CALLBACK RecorderWindow::PickProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (RecorderWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        self = (RecorderWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    auto finish = [self](bool ok, RECT r) {
        ReleaseCapture();
        HWND w = self->pickWnd_;
        self->pickWnd_ = nullptr;
        DestroyWindow(w);
        if (ok) { self->full_ = false; self->setCaptureRect(r); }
        ShowWindow(self->hwnd_, SW_SHOW);
        SetForegroundWindow(self->hwnd_);
        self->updateOverlays();
    };
    switch (msg) {
    case WM_LBUTTONDOWN:
        GetCursorPos(&self->p0_);
        self->p1_ = self->p0_;
        self->picking_ = true;
        return 0;
    case WM_MOUSEMOVE:
        GetCursorPos(&self->p1_);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        POINT p;
        GetCursorPos(&p);
        if (self->pickWindow_) {
            ShowWindow(h, SW_HIDE);
            RECT r;
            bool ok = windowRectAt(p, self->hwnd_, r);
            finish(ok, r);
        } else {
            RECT r{std::min(self->p0_.x, p.x), std::min(self->p0_.y, p.y), std::max(self->p0_.x, p.x), std::max(self->p0_.y, p.y)};
            finish(r.right - r.left > 8 && r.bottom - r.top > 8, r);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) finish(false, {});
        return 0;
    case WM_RBUTTONUP:
        finish(false, {});
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        Gui& g = self->pickGui_;
        if (g.beginFrame()) {
            g.g.clear(th::background);
            RECT vs = virtualScreen();
            float s = g.s();
            POINT cur;
            GetCursorPos(&cur);
            float cx = (cur.x - vs.left) / s, cy = (cur.y - vs.top) / s;
            const wchar_t* t = self->pickWindow_ ? L"녹화할 창을 누르세요 · Esc로 취소해요" : L"드래그해서 녹화할 영역을 고르세요 · Esc로 취소해요";
            RectF pill{cx - 230, cy - 74, 460, 40};
            g.g.fill(pill, 20, th::menuBackground);
            g.g.stroke(pill, 20, th::menuStroke, 1);
            g.g.text(t, pill, th::textPrimary, th::body, 600, A_CENTER);
            if (self->picking_ && !self->pickWindow_) {
                RectF r{(std::min(self->p0_.x, self->p1_.x) - vs.left) / s, (std::min(self->p0_.y, self->p1_.y) - vs.top) / s,
                        std::abs(self->p1_.x - self->p0_.x) / s, std::abs(self->p1_.y - self->p0_.y) / s};
                g.g.fill(r, 0, 0xFFFFFFFF);
                g.g.strokeH(r, 0, th::accent, th::accentEnd, 2);
                wchar_t sz[32];
                swprintf_s(sz, L"%ld × %ld", std::abs(self->p1_.x - self->p0_.x), std::abs(self->p1_.y - self->p0_.y));
                g.g.text(sz, {r.x, r.b() + 6, 160, 20}, th::textPrimary, th::caption, 600);
            }
            g.endFrame();
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ------------------------------------------------------------------ input
bool RecorderWindow::preTranslate(MSG& m) {
    if (m.message != WM_KEYDOWN || GetAncestor(m.hwnd, GA_ROOT) != hwnd_) return false;
    bool ctrl = GetKeyState(VK_CONTROL) & 0x8000, shift = GetKeyState(VK_SHIFT) & 0x8000;
    if (m.wParam == VK_ESCAPE && !rec_.running()) { close(); return true; }
    if (rec_.running()) return false;
    Settings& st = app_->st;
    WPARAM k = m.wParam;
    if (ctrl && k >= '1' && k <= '3') {
        int i = (int)(k - '1');
        if (shift) { st.recPos[i] = captureRect(); st.save(); flash(L"위치 " + std::to_wstring(i + 1) + L"에 저장했어요"); }
        else if (st.recPos[i].right > st.recPos[i].left) { full_ = false; setCaptureRect(st.recPos[i]); }
        return true;
    }
    if (ctrl && shift && k == 'D') { pick(false); return true; }
    if (ctrl && shift && k == 'F') { pick(true); return true; }
    if (ctrl && !shift && k == 'S') { captureImage(); return true; }
    if (!ctrl && !shift && ((k >= '1' && k <= '9') || k == '0')) {   // size presets
        int i = k == '0' ? 9 : (int)(k - '1');
        RECT c = captureRect();
        full_ = false;
        setCaptureRect({c.left, c.top, c.left + kSizes[i][0], c.top + kSizes[i][1]});
        return true;
    }
    int dx = 0, dy = 0;
    switch (k) {
    case VK_LEFT: dx = -1; break;
    case VK_RIGHT: dx = 1; break;
    case VK_UP: dy = -1; break;
    case VK_DOWN: dy = 1; break;
    default: return false;
    }
    int step = ctrl ? 10 : 1;
    RECT r = captureRect();
    if (shift) { r.right += dx * step; r.bottom += dy * step; }
    else OffsetRect(&r, dx * step, dy * step);
    full_ = false;
    setCaptureRect(r);
    return true;
}

LRESULT CALLBACK RecorderWindow::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (RecorderWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        self = (RecorderWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = h;
    }
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT RecorderWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &p);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        float s = gui_.s(), x = p.x / s, y = p.y / s, W = rc.right / s, H = rc.bottom / s;
        if (rec_.running()) {
            for (auto& r : ctlRects_) if (r.contains(x, y)) return HTCLIENT;
            return y < barH_ ? HTCAPTION : HTCLIENT;
        }
        float e = bw_ + 4;
        bool l = x < e, r = x >= W - e, t = y < 4, b = y >= H - e;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b) return HTBOTTOM;
        if (y > barH_ && l) return HTLEFT;
        if (y > barH_ && r) return HTRIGHT;
        if (t) return HTTOP;
        for (auto& c : ctlRects_) if (c.contains(x, y)) return HTCLIENT;
        if (y < barH_) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        paint();
        EndPaint(hwnd_, &ps);
        gui_.runLater();
        return 0;
    }
    case WM_SIZE: {
        LRESULT r;
        gui_.handle(msg, wp, lp, r);
        updateRegion();
        updateOverlays();
        return 0;
    }
    case WM_MOVE:
        updateOverlays();
        return 0;
    case WM_GETMINMAXINFO: {
        float s = gui_.g.scale() > 0 ? gui_.g.scale() : 1.f;
        ((MINMAXINFO*)lp)->ptMinTrackSize = {(LONG)(220 * s), (LONG)((barH_ + 60) * s)};
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        if (app_->st.useMagnifier && !magWnd_) {   // magnifier near the cursor while moving/resizing
            int sz = (int)(168 * gui_.s());
            magWnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED, kOverlayClass, L"",
                                      WS_POPUP, 0, 0, sz, sz, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetWindowLongPtrW(magWnd_, GWLP_USERDATA, (LONG_PTR)this);
            SetLayeredWindowAttributes(magWnd_, 0, 255, LWA_ALPHA);
            excludeFromCapture(magWnd_, true);
            SetTimer(hwnd_, TIMER_MAG, 30, nullptr);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        full_ = false;
        if (magWnd_) { KillTimer(hwnd_, TIMER_MAG); DestroyWindow(magWnd_); magWnd_ = nullptr; }
        gui_.redraw();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_REC) { gui_.redraw(); return 0; }
        if (wp == TIMER_FLASH) { KillTimer(hwnd_, TIMER_FLASH); flash_.clear(); gui_.redraw(); return 0; }
        if (wp == TIMER_MAG && magWnd_) {
            POINT c;
            GetCursorPos(&c);
            RECT mr;
            GetWindowRect(magWnd_, &mr);
            int sz = mr.right - mr.left, off = (int)(24 * gui_.s());
            MONITORINFO mi{sizeof mi};
            GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
            int x = c.x + off, y = c.y + off;
            if (x + sz > mi.rcWork.right) x = c.x - off - sz;
            if (y + sz > mi.rcWork.bottom) y = c.y - off - sz;
            SetWindowPos(magWnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            InvalidateRect(magWnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_APP_RECSTOP:
        stopRecord();
        return 0;
    case WM_CLOSE:
        close();
        return 0;
    }
    LRESULT r;
    if (gui_.hwnd && gui_.handle(msg, wp, lp, r)) return r;
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace bg
