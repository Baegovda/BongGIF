#include "ui/Gui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <dwmapi.h>
#include <thread>
#include <windowsx.h>

namespace bg {

static const UINT_PTR kAnimTimer = 0xA11;

// ------------------------------------------------------------------ colour helpers (derive from tokens)
static uint32_t over(uint32_t base, uint32_t top) {   // alpha-composite top over opaque base
    float a = ((top >> 24) & 255) / 255.f;
    auto ch = [&](int sh) { return (uint32_t)std::lround(((base >> sh) & 255) * (1 - a) + ((top >> sh) & 255) * a) << sh; };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}
static uint32_t lerpC(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int sh) { return (uint32_t)std::lround(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t) << sh; };
    return ch(24) | ch(16) | ch(8) | ch(0);
}
static uint32_t withAlpha(uint32_t c, float a) { return ((uint32_t)std::lround(((c >> 24) & 255) * a) << 24) | (c & 0xFFFFFF); }
// solid colour a control has when it sits on a card on the window background
static uint32_t controlOnCard() { return over(over(over(th::background, th::cardBottom), th::cardTop & 0x10FFFFFF), th::control); }

std::wstring fmtBytes(size_t n) {
    wchar_t b[64];
    if (n < 1024) swprintf_s(b, L"%zu B", n);
    else if (n < 1024 * 1024) swprintf_s(b, L"%.1f KB", n / 1024.0);
    else swprintf_s(b, L"%.2f MB", n / 1048576.0);
    return b;
}

void applyWindowStyle(HWND h) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    int corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof corner);
    uint32_t b = over(th::background, th::separator);
    COLORREF border = RGB((b >> 16) & 255, (b >> 8) & 255, b & 255);
    DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
}

// ------------------------------------------------------------------ Gui core
void Gui::attach(HWND h) {
    hwnd = h;
    g.attach(h);
    uint32_t e = controlOnCard();
    editBrush_ = CreateSolidBrush(RGB((e >> 16) & 255, (e >> 8) & 255, e & 255));
}

Gui::~Gui() {
    for (auto& [id, e] : edits_) if (e.h) DestroyWindow(e.h);
    if (editFont_) DeleteObject(editFont_);
    if (editBrush_) DeleteObject(editBrush_);
}

void Gui::syncInput() {
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    runLater();
}

void Gui::runLater() {
    while (!later_.empty()) {
        auto q = std::move(later_);
        later_.clear();
        for (auto& f : q) f();
    }
}

bool Gui::handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& r) {
    r = 0;
    switch (msg) {
    case WM_MOUSEMOVE:
        mx = GET_X_LPARAM(lp) / s();
        my = GET_Y_LPARAM(lp) / s();
        if (!tracking_) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        syncInput();
        return true;
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (!down) { mx = my = -1e6f; syncInput(); }
        return true;
    case WM_RBUTTONDOWN:
        mx = GET_X_LPARAM(lp) / s();
        my = GET_Y_LPARAM(lp) / s();
        rdown = rpressed = true;
        SetCapture(hwnd);
        syncInput();
        return true;
    case WM_RBUTTONUP:
        mx = GET_X_LPARAM(lp) / s();
        my = GET_Y_LPARAM(lp) / s();
        rdown = false;
        rreleased = true;
        if (!down) ReleaseCapture();
        syncInput();
        return true;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (msg == WM_LBUTTONDBLCLK) dblclick = true;
        mx = GET_X_LPARAM(lp) / s();
        my = GET_Y_LPARAM(lp) / s();
        down = pressed = true;
        SetCapture(hwnd);
        SetFocus(hwnd);
        syncInput();
        return true;
    case WM_LBUTTONUP:
        mx = GET_X_LPARAM(lp) / s();
        my = GET_Y_LPARAM(lp) / s();
        down = false;
        released = true;
        ReleaseCapture();
        syncInput();
        return true;
    case WM_CAPTURECHANGED:
        if (down) { down = false; released = true; active = 0; redraw(); }
        rdown = false;
        return false;
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        mx = p.x / s();
        my = p.y / s();
        wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
        syncInput();
        return true;
    }
    case WM_SIZE:
        g.resize();
        redraw();
        return false;
    case WM_TIMER:
        if (wp == kAnimTimer) { redraw(); return true; }
        return false;
    case WM_ERASEBKGND:
        r = 1;
        return true;
    case WM_CTLCOLOREDIT: {
        uint32_t e = controlOnCard();
        SetTextColor((HDC)wp, RGB((th::textPrimary >> 16) & 255, (th::textPrimary >> 8) & 255, th::textPrimary & 255));
        SetBkColor((HDC)wp, RGB((e >> 16) & 255, (e >> 8) & 255, e & 255));
        r = (LRESULT)editBrush_;
        return true;
    }
    case WM_COMMAND: {
        HWND c = (HWND)lp;
        for (auto& [id, e] : edits_)
            if (e.h == c) {
                int code = HIWORD(wp);
                if (code == EN_CHANGE || code == EN_SETFOCUS || code == EN_KILLFOCUS) redraw();
                return true;
            }
        return false;
    }
    }
    return false;
}

bool Gui::beginFrame() {
    if (!g.begin()) return false;
    auto now = std::chrono::steady_clock::now();
    dt_ = std::min(1000.f / 30.f, std::chrono::duration<float, std::milli>(now - last_).count());
    last_ = now;
    animating_ = false;
    hot = 0;
    clips.assign(1, RectF{0, 0, g.width(), g.height()});
    for (auto& [id, e] : edits_) e.used = false;
    return true;
}

void Gui::endFrame() {
    g.end();
    for (auto& [id, e] : edits_)
        if (!e.used && e.h && IsWindowVisible(e.h)) ShowWindow(e.h, SW_HIDE);
    if (released) active = 0;
    pressed = released = false;
    rpressed = rreleased = dblclick = false;
    wheel = 0;
    if (animating_) SetTimer(hwnd, kAnimTimer, 16, nullptr);
    else KillTimer(hwnd, kAnimTimer);
}

bool Gui::hit(RectF r) const {
    return enabledInput && r.contains(mx, my) && clips.back().contains(mx, my);
}

bool Gui::interact(Id id, RectF r, bool enabled) {
    bool over = enabled && hit(r);
    if (over && (active == 0 || active == id)) hot = id;
    if (over && pressed && active == 0) active = id;
    return released && active == id && over;
}

float Gui::anim(Id id, float target, float ms) {
    auto it = anim_.find(id);
    float v = it == anim_.end() ? target : it->second;
    if (v != target) {
        float step = dt_ / ms;
        v = v < target ? std::min(target, v + step) : std::max(target, v - step);
        if (v != target) animating_ = true;
    }
    anim_[id] = v;
    return v * v * (3 - 2 * v);   // ease
}

// ------------------------------------------------------------------ surfaces
void Gui::card(RectF r, float radius, bool sh) {
    if (sh) g.shadow(r, radius);
    g.fill(r, radius, th::background);           // cards are opaque glass over the base
    g.fillV(r, radius, th::cardTop, th::cardBottom);
    g.strokeV(r, radius, th::strokeTop, th::strokeBottom, 1);
}

void Gui::separator(float x1, float x2, float y) { g.line(x1, y, x2, y, th::separator, 1); }

void Gui::chip(RectF r, const std::wstring& t, uint32_t fg, uint32_t bg) {
    g.fill(r, th::radiusChip, bg);
    g.text(t, r, fg, th::caption, 600, A_CENTER);
}

void Gui::iconTile(RectF r, wchar_t icon, uint32_t c1, uint32_t c2) {
    g.fillV(r, std::min(r.w, r.h) * 0.27f, c1, c2);
    g.icon(icon, r, th::textPrimary, r.h * 0.55f);
}

// ------------------------------------------------------------------ controls
bool Gui::button(Id id, RectF r, const std::wstring& text, BtnStyle st, wchar_t icon, bool enabled) {
    bool click = interact(id, r, enabled);
    float t = hoverT(id);
    bool press = active == id && down && hit(r);
    float rad = th::radiusControl;
    uint32_t fg = th::textPrimary;
    switch (st) {
    case BS_PRIMARY:
    case BS_DANGER: {
        uint32_t a = st == BS_PRIMARY ? th::accent : th::red, b = st == BS_PRIMARY ? th::accentEnd : th::pink;
        if (!enabled) { a = withAlpha(a, 0.4f); b = withAlpha(b, 0.4f); }
        g.fillH(r, rad, a, b);
        if (t > 0) g.fill(r, rad, withAlpha(0x1CFFFFFF, t));
        if (press) g.fill(r, rad, 0x22000000);
        break;
    }
    case BS_GHOST:
        if (t > 0 || press) g.fill(r, rad, press ? th::controlHover : withAlpha(th::control, t));
        fg = enabled ? th::textPrimary : withAlpha(th::textPrimary, 0.4f);
        break;
    default:
        g.fill(r, rad, lerpC(th::control, th::controlHover, press ? 1.f : t));
        g.stroke(r, rad, th::separator, 1);
        if (!enabled) fg = withAlpha(fg, 0.4f);
    }
    float tw = text.empty() ? 0 : g.textWidth(text, th::body, 500);
    float iw = icon ? 16.f : 0, gap = (icon && !text.empty()) ? 7.f : 0;
    float x = r.x + (r.w - tw - iw - gap) / 2;
    if (icon) g.icon(icon, {x, r.y, iw, r.h}, fg, 13);
    if (!text.empty()) g.text(text, {x + iw + gap, r.y, std::min(tw + 2, r.r() - x - iw - gap), r.h}, fg, th::body, 500);
    return click;
}

bool Gui::iconButton(Id id, RectF r, wchar_t icon, bool enabled, uint32_t color, float size) {
    bool click = interact(id, r, enabled);
    float t = hoverT(id);
    if (t > 0) g.fill(r, th::radiusControl, withAlpha(th::control, t));
    if (active == id && down && hit(r)) g.fill(r, th::radiusControl, th::controlHover);
    g.icon(icon, r, enabled ? lerpC(color, th::textPrimary, t) : withAlpha(color, 0.4f), size);
    return click;
}

bool Gui::navItem(Id id, RectF r, wchar_t icon, uint32_t c1, uint32_t c2, const std::wstring& text, bool selected, bool enabled) {
    bool click = interact(id, r, enabled);
    float t = hoverT(id);
    if (selected) g.fill(r, 8, th::control);
    else if (t > 0) g.fill(r, 8, withAlpha(th::control, t * 0.8f));
    float ts = 26;
    RectF tile{r.x + 8, r.y + (r.h - ts) / 2, ts, ts};
    g.fillV(tile, 7, enabled ? c1 : withAlpha(c1, 0.4f), enabled ? c2 : withAlpha(c2, 0.4f));
    g.icon(icon, tile, enabled ? th::textPrimary : withAlpha(th::textPrimary, 0.4f), 13);
    g.text(text, {tile.r() + 11, r.y, r.r() - tile.r() - 14, r.h}, enabled ? th::textPrimary : withAlpha(th::textPrimary, 0.4f), th::body, 500);
    return click;
}

bool Gui::toggle(Id id, RectF r, bool& v, bool enabled) {
    bool click = interact(id, r.inset(-4), enabled);
    if (click) v = !v;
    float t = anim(id ^ 0x7070, v ? 1.f : 0.f, th::toggleMs);
    float h = r.h;
    float a = enabled ? 1.f : 0.4f;
    g.fill(r, h / 2, withAlpha(lerpC(th::control, th::controlHover, hoverT(id)), a));
    g.stroke(r, h / 2, withAlpha(th::separator, a), 1);
    if (t > 0.001f) g.fillH(r, h / 2, withAlpha(th::accent, t * a), withAlpha(th::accentEnd, t * a));
    float rad = h / 2 - 3;
    float cx = r.x + h / 2 + (r.w - h) * t;
    g.circle(cx, r.y + h / 2, rad, withAlpha(th::textPrimary, a));
    return click;
}

bool Gui::slider(Id id, RectF r, int& v, int lo, int hi, bool enabled) {
    interact(id, r, enabled);
    int old = v;
    if (active == id && down && hi > lo) {
        float f = std::clamp((mx - r.x) / std::max(1.f, r.w), 0.f, 1.f);
        v = lo + (int)std::lround(f * (hi - lo));
    }
    float a = enabled ? 1.f : 0.4f;
    float f = hi > lo ? (float)(v - lo) / (hi - lo) : 0;
    float cy = r.y + r.h / 2;
    RectF track{r.x, cy - 2, r.w, 4};
    g.fill(track, 2, withAlpha(th::control, a));
    g.fill({r.x, cy - 2, r.w * f, 4}, 2, withAlpha(th::accent, a));
    float t = hoverT(id);
    float tx = r.x + r.w * f;
    g.circle(tx, cy, 8 + t, withAlpha(0x40000000, a));
    g.circle(tx, cy, 7 + t, withAlpha(th::textPrimary, a));
    return v != old;
}

bool Gui::segmented(Id id, RectF r, const std::vector<std::wstring>& items, int& sel, bool enabled) {
    int old = sel;
    float a = enabled ? 1.f : 0.4f;
    g.fill(r, 8, withAlpha(th::control, a));
    float w = r.w / std::max<size_t>(1, items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        RectF c{r.x + w * i, r.y, w, r.h};
        Id cid = gid(id, i);
        if (interact(cid, c, enabled)) sel = (int)i;
        RectF pill = c.inset(3);
        if ((int)i == sel) g.fillH(pill, 6, withAlpha(th::accent, a), withAlpha(th::accentEnd, a));
        else {
            float t = hoverT(cid);
            if (t > 0) g.fill(pill, 6, withAlpha(th::control, t));
        }
        g.text(items[i], c, withAlpha((int)i == sel ? th::textPrimary : th::textSecondary, a), th::body - 0.5f, (int)i == sel ? 600 : 400, A_CENTER);
    }
    return sel != old;
}

bool Gui::dropdown(Id id, RectF r, const std::vector<std::wstring>& items, int& sel, bool enabled) {
    bool click = interact(id, r, enabled);
    float t = hoverT(id);
    float a = enabled ? 1.f : 0.4f;
    g.fill(r, th::radiusControl, withAlpha(lerpC(th::control, th::controlHover, t), a));
    g.stroke(r, th::radiusControl, withAlpha(th::separator, a), 1);
    std::wstring cur = sel >= 0 && sel < (int)items.size() ? items[sel] : L"";
    g.text(cur, {r.x + 10, r.y, r.w - 34, r.h}, withAlpha(th::textPrimary, a), th::body);
    g.icon(ic::ChevronDown, {r.r() - 26, r.y, 20, r.h}, withAlpha(th::textSecondary, a), 10);
    if (click) {
        int* target = &sel;
        RECT sr{(LONG)(r.x * s()), (LONG)(r.y * s()), (LONG)(r.r() * s()), (LONG)(r.b() * s())};
        MapWindowPoints(hwnd, nullptr, (POINT*)&sr, 2);
        HWND owner = hwnd;
        later([=] {
            showMenu(owner, sr, items, *target, [this, target, id](int i) {
                *target = i;
                changed_.insert(id);
                redraw();
            });
        });
    }
    if (changed_.count(id)) { changed_.erase(id); return true; }
    return false;
}

bool Gui::edit(Id id, RectF r, std::wstring& text, bool enabled) {
    EditBox& e = edits_[id];
    e.used = true;
    float sc = s();
    if (!editFont_ || editFontScale_ != sc) {
        if (editFont_) DeleteObject(editFont_);
        LOGFONTW lf{};
        lf.lfHeight = -(int)std::lround(th::body * sc);
        lf.lfWeight = FW_NORMAL;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, th::fontBody);
        editFont_ = CreateFontIndirectW(&lf);
        editFontScale_ = sc;
        for (auto& [k, x] : edits_) if (x.h) SendMessageW(x.h, WM_SETFONT, (WPARAM)editFont_, TRUE);
    }
    if (!e.h) {
        e.h = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 10, 10, hwnd, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
        SendMessageW(e.h, WM_SETFONT, (WPARAM)editFont_, TRUE);
        e.last = text;
    }
    bool focus = GetFocus() == e.h;
    float a = enabled ? 1.f : 0.4f;
    uint32_t fill = controlOnCard();
    g.fill(r, th::radiusControl, withAlpha(fill, a));
    g.stroke(r, th::radiusControl, focus ? th::accent : withAlpha(th::separator, a), 1);
    // place the native edit inside, vertically centred
    int fh = (int)std::lround(th::body * sc * 1.35f);
    int px = (int)std::lround((r.x + 9) * sc), pw = (int)std::lround((r.w - 18) * sc);
    int py = (int)std::lround((r.y + r.h / 2) * sc) - fh / 2;
    RectF clip = clips.back();
    bool inside = r.y >= clip.y - 0.5f && r.b() <= clip.b() + 0.5f && r.x >= clip.x - 0.5f && r.r() <= clip.r() + 0.5f;
    if (inside) {
        RECT cur;
        GetWindowRect(e.h, &cur);
        MapWindowPoints(nullptr, hwnd, (POINT*)&cur, 2);
        if (cur.left != px || cur.top != py || cur.right - cur.left != pw || cur.bottom - cur.top != fh)
            SetWindowPos(e.h, nullptr, px, py, pw, fh, SWP_NOZORDER | SWP_NOACTIVATE);
        if (!IsWindowVisible(e.h)) ShowWindow(e.h, SW_SHOWNA);
    } else if (IsWindowVisible(e.h)) {
        ShowWindow(e.h, SW_HIDE);
    }
    if ((bool)IsWindowEnabled(e.h) != enabled) EnableWindow(e.h, enabled);
    int n = GetWindowTextLengthW(e.h);
    std::wstring cur(n, 0);
    GetWindowTextW(e.h, cur.data(), n + 1);
    if (cur != e.last) {   // user typed
        e.last = cur;
        text = cur;
        return true;
    }
    if (text != e.last) {  // program changed the value
        e.last = text;
        SetWindowTextW(e.h, text.c_str());
    }
    return false;
}

bool Gui::number(Id id, RectF r, int& v, int lo, int hi, bool enabled) {
    int old = v;
    float bw = 26;
    if (iconButton(gid(id, 1), {r.x, r.y, bw, r.h}, ic::Minus, enabled && v > lo, th::textSecondary, 11)) v = std::max(lo, v - 1);
    if (iconButton(gid(id, 2), {r.r() - bw, r.y, bw, r.h}, ic::Add, enabled && v < hi, th::textSecondary, 11)) v = std::min(hi, v + 1);
    std::wstring t = std::to_wstring(v);
    if (edit(gid(id, 3), {r.x + bw + 2, r.y, r.w - 2 * bw - 4, r.h}, t, enabled)) {
        try { v = std::clamp(std::stoi(t), lo, hi); } catch (...) {}
    }
    return v != old;
}

void Gui::progress(RectF r, float v) {
    g.fill(r, r.h / 2, th::control);
    v = std::clamp(v, 0.f, 1.f);
    if (v > 0) g.fillH({r.x, r.y, std::max(r.h, r.w * v), r.h}, r.h / 2, th::accent, th::accentEnd);
}

RectF Gui::optionRow(float x, float& y, float w, const std::wstring& title, const std::wstring& desc, float cw, float ch,
                     bool enabled, bool sepBelow) {
    float textW = w - cw - 20;
    float dh = desc.empty() ? 0 : g.textHeight(desc, th::caption, textW);
    float h = std::max(desc.empty() ? 42.f : 30.f + dh + 6, ch + 14);
    uint32_t a1 = dim(th::textPrimary, enabled), a2 = dim(th::textTertiary, enabled);
    if (desc.empty()) g.text(title, {x, y, textW, h}, a1, th::body, 600);
    else {
        g.text(title, {x, y + 8, textW, 20}, a1, th::body, 600);
        g.text(desc, {x, y + 28, textW, dh + 2}, a2, th::caption, 400, A_LEFT, false, true);
    }
    RectF c{x + w - cw, y + (h - ch) / 2, cw, ch};
    y += h;
    if (sepBelow) separator(x, x + w, y);
    y += 1;
    return c;
}

float Gui::beginScroll(ScrollArea& sa, RectF view) {
    sa.view = view.h;
    sa.top = view.y;
    sa.x = view.x;
    sa.w = view.w;
    if (wheel != 0 && hit(view)) {
        sa.pos -= wheel * 64;
        wheel = 0;
    }
    float maxPos = std::max(0.f, sa.content - sa.view);
    sa.pos = std::clamp(sa.pos, 0.f, maxPos);
    g.pushClip(view);
    RectF c = clips.back();
    float x1 = std::max(c.x, view.x), y1 = std::max(c.y, view.y), x2 = std::min(c.r(), view.r()), y2 = std::min(c.b(), view.b());
    clips.push_back({x1, y1, std::max(0.f, x2 - x1), std::max(0.f, y2 - y1)});
    return view.y - sa.pos;
}

void Gui::endScroll(ScrollArea& sa, float bottom) {
    sa.content = bottom - (sa.top - sa.pos);
    clips.pop_back();
    g.popClip();
    if (sa.content > sa.view + 1) {
        float thumbH = std::max(24.f, sa.view * sa.view / sa.content);
        float maxPos = sa.content - sa.view;
        float ty = sa.top + (sa.view - thumbH) * (sa.pos / maxPos);
        float x = sa.x + sa.w - 6;
        RectF thumb{x, ty, 4, thumbH};
        Id id = (Id)(uintptr_t)&sa;
        interact(id, thumb.inset(-4, 0), true);
        if (active == id && down) {
            sa.pos = std::clamp((my - sa.top - thumbH / 2) / std::max(1.f, sa.view - thumbH) * maxPos, 0.f, maxPos);
        }
        g.fill(thumb, 2, hot == id || active == id ? 0x66FFFFFF : 0x33FFFFFF);
    }
}

// ------------------------------------------------------------------ popup menu
namespace {
struct Menu {
    HWND h = nullptr;
    Gui gui;
    std::vector<std::wstring> items;
    int sel = 0;
    std::vector<int> checks;
    std::function<void(int)> onPick;
    float t = 0;            // enter/exit progress
    bool closing = false;
    int picked = -1;
    POINT target{};
    std::chrono::steady_clock::time_point t0;
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp);
    void paint();
    void finish();
};

static const float kItemH = 32, kPad = 5;

void Menu::paint() {
    if (!gui.beginFrame()) return;
    Gfx& g = gui.g;
    g.clear(over(th::background, th::menuBackground));
    RectF all{0, 0, g.width(), g.height()};
    g.stroke(all, 8, th::menuStroke, 1);
    float y = kPad;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] == L"-") {
            gui.separator(kPad + 6, g.width() - kPad - 6, y + 4.5f);
            y += 9;
            continue;
        }
        RectF r{kPad, y, g.width() - 2 * kPad, kItemH};
        y += kItemH;
        Id id = gid("menu", i);
        if (gui.interact(id, r)) { picked = (int)i; gui.later([this] { finish(); }); }
        float ht = gui.hoverT(id);
        if (ht > 0) g.fill(r, 6, withAlpha(th::menuHover, ht));
        bool on = checks.empty() ? (int)i == sel : (i < checks.size() && checks[i] == 1);
        if (on) g.icon(ic::Check, {r.x + 4, r.y, 20, r.h}, th::accent, 11);
        g.text(items[i], {r.x + 28, r.y, r.w - 34, r.h}, th::textPrimary, th::body);
    }
    gui.endFrame();
}

void Menu::finish() {
    if (closing) return;
    closing = true;
    t0 = std::chrono::steady_clock::now();
    SetTimer(h, 2, 16, nullptr);
}

LRESULT CALLBACK Menu::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Menu* self = (Menu*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, m, wp, lp);
    LRESULT r;
    if (m == WM_TIMER && wp == 2) {
        float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - self->t0).count();
        if (!self->closing) {   // enter: rise 8 DIP + fade in over 180 ms
            float k = std::min(1.f, ms / th::popupInMs);
            float e = 1 - (1 - k) * (1 - k);
            SetLayeredWindowAttributes(h, 0, (BYTE)(255 * e), LWA_ALPHA);
            SetWindowPos(h, nullptr, self->target.x, self->target.y + (int)(th::popupRise * self->gui.s() * (1 - e)), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (k >= 1) KillTimer(h, 2);
        } else {                // exit: fade out over 120 ms
            float k = std::min(1.f, ms / th::popupOutMs);
            SetLayeredWindowAttributes(h, 0, (BYTE)(255 * (1 - k)), LWA_ALPHA);
            if (k >= 1) {
                KillTimer(h, 2);
                int p = self->picked;
                auto cb = self->onPick;
                DestroyWindow(h);
                if (p >= 0 && cb) cb(p);
            }
        }
        return 0;
    }
    if (self->gui.handle(m, wp, lp, r)) return r;
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        self->paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) self->finish();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) self->finish();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete self;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
} // namespace

void showMenu(HWND owner, RECT anchor, const std::vector<std::wstring>& items, int sel, std::function<void(int)> onPick) {
    std::vector<int> checks(items.size(), 0);
    if (sel >= 0 && sel < (int)items.size()) checks[sel] = 1;
    showMenuChecks(owner, anchor, items, checks, std::move(onPick));
}

void showMenuChecks(HWND owner, RECT anchor, const std::vector<std::wstring>& items, const std::vector<int>& checks,
                    std::function<void(int)> onPick) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof wc};
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = Menu::Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"BongGIFMenu";
        RegisterClassExW(&wc);
        reg = true;
    }
    auto* m = new Menu;
    m->items = items;
    m->checks = checks;
    m->onPick = std::move(onPick);
    UINT dpi = GetDpiForWindow(owner);
    float s = dpi / 96.f;
    int w = std::max<int>(anchor.right - anchor.left, (int)(160 * s));
    float contentH = 2 * kPad;
    for (auto& it : items) contentH += it == L"-" ? 9 : kItemH;
    int hgt = (int)(contentH * s);
    float maxTextW = 0;
    {
        HDC dc = GetDC(nullptr);
        for (auto& it : items) maxTextW = std::max(maxTextW, (float)it.size() * 13.5f);
        ReleaseDC(nullptr, dc);
    }
    w = std::max(w, (int)((maxTextW + 48) * s));
    HMONITOR mon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    int y = anchor.bottom + (int)(4 * s);
    if (y + hgt > mi.rcWork.bottom) y = anchor.top - hgt - (int)(4 * s);
    m->target = {anchor.left, y};
    m->h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"BongGIFMenu", L"", WS_POPUP, anchor.left,
                           y + (int)(th::popupRise * s), w, hgt, GetAncestor(owner, GA_ROOT), nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(m->h, GWLP_USERDATA, (LONG_PTR)m);
    int corner = 3;   // DWMWCP_ROUNDSMALL
    DwmSetWindowAttribute(m->h, 33, &corner, sizeof corner);
    m->gui.attach(m->h);
    SetLayeredWindowAttributes(m->h, 0, 0, LWA_ALPHA);
    m->t0 = std::chrono::steady_clock::now();
    ShowWindow(m->h, SW_SHOW);
    SetForegroundWindow(m->h);
    SetTimer(m->h, 2, 16, nullptr);
}

// ------------------------------------------------------------------ Form
static const wchar_t* kFormClass = L"BongGIFForm";
static const float kTitleH = 48, kFooterH = 64;

Form::Form(HWND owner, std::wstring t, float w) : title(std::move(t)), width(w), owner_(owner) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kFormClass;
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        reg = true;
    }
    hwnd_ = CreateWindowExW(0, kFormClass, title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, 0, 0, 100, 100,
                            owner ? GetAncestor(owner, GA_ROOT) : nullptr, nullptr, GetModuleHandleW(nullptr), this);
    applyWindowStyle(hwnd_);
    gui.attach(hwnd_);
}

Form::~Form() { if (IsWindow(hwnd_)) DestroyWindow(hwnd_); }

void Form::paint() {
    if (!gui.beginFrame()) return;
    Gfx& g = gui.g;
    float W = g.width(), H = g.height();
    g.clear(th::background);
    g.glow(W * 0.15f, -40, W * 0.7f, withAlpha(th::accent, 0.10f));
    g.glow(W * 0.95f, H + 40, W * 0.6f, withAlpha(th::accentEnd, 0.07f));
    g.fillV({0, 0, W, H}, 0, th::cardTop & 0x14FFFFFF, th::cardBottom & 0x08FFFFFF);
    g.text(title, {22, 0, W - 80, kTitleH}, th::textPrimary, th::sectionTitle, 600, A_LEFT, true, false, true);
    closeBtn_ = {W - 46, 8, 36, 32};
    if (gui.iconButton(gid("form.close"), closeBtn_, ic::Close, true, th::textSecondary, 11)) gui.later([this] { close(false); });
    RectF view{0, kTitleH, W, H - kTitleH - kFooterH};
    float y0 = gui.beginScroll(scroll_, view);
    float bottom = body ? body(gui, 22, y0 + 4, W - 44) : y0;
    gui.endScroll(scroll_, bottom + 12);
    contentH_ = bottom + 12 - y0;
    // footer
    gui.separator(0, W, H - kFooterH);
    float bw = std::max(96.f, g.textWidth(okText, th::body, 500) + 40);
    RectF ok{W - 22 - bw, H - kFooterH + 16, bw, 34};
    if (gui.button(gid("form.ok"), ok, okText, BS_PRIMARY, 0, okEnabled))
        gui.later([this] { if (!onOk || onOk()) close(true); });
    if (showCancel) {
        float cw = std::max(84.f, g.textWidth(cancelText, th::body, 500) + 36);
        if (gui.button(gid("form.cancel"), {ok.x - 10 - cw, ok.y, cw, 34}, cancelText, BS_SECONDARY)) gui.later([this] { close(false); });
    }
    gui.endFrame();
}

bool Form::run() {
    float s = gui.s();
    // measure pass on the hidden window, then size to content (max 85% of the work area)
    SetWindowPos(hwnd_, nullptr, 0, 0, (int)(width * s), (int)(600 * s), SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    gui.g.resize();
    paint();
    HMONITOR mon = MonitorFromWindow(owner_ ? owner_ : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    int maxH = (int)((mi.rcWork.bottom - mi.rcWork.top) * 0.85);
    int w = (int)(width * s), h = std::min(maxH, (int)((kTitleH + contentH_ + kFooterH) * s));
    RECT orc = mi.rcWork;
    if (owner_ && IsWindowVisible(owner_)) GetWindowRect(GetAncestor(owner_, GA_ROOT), &orc);
    int x = (orc.left + orc.right - w) / 2, y = std::max<int>(mi.rcWork.top, (orc.top + orc.bottom - h) / 2);
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    gui.g.resize();
    HWND root = owner_ ? GetAncestor(owner_, GA_ROOT) : nullptr;
    if (root) EnableWindow(root, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    if (tickMs > 0) SetTimer(hwnd_, 3, tickMs, nullptr);
    MSG m;
    while (!done_ && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && GetAncestor(m.hwnd, GA_ROOT) == hwnd_) {
            if (m.wParam == VK_ESCAPE) { close(false); continue; }
            if (onKey) {   // not while typing in an edit box
                wchar_t cls[16] = {};
                GetClassNameW(GetFocus(), cls, 16);
                if (_wcsicmp(cls, L"Edit") != 0 && onKey((int)m.wParam)) { gui.redraw(); continue; }
            }
            if (m.wParam == VK_RETURN && okEnabled) { if (!onOk || onOk()) close(true); continue; }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    KillTimer(hwnd_, 3);
    if (root) { EnableWindow(root, TRUE); SetForegroundWindow(root); }
    ShowWindow(hwnd_, SW_HIDE);
    return ok_;
}

LRESULT CALLBACK Form::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Form* f = (Form*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        f = (Form*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)f);
        f->hwnd_ = h;
    }
    if (!f) return DefWindowProcW(h, msg, wp, lp);
    if (msg == WM_NCCALCSIZE && wp) return 0;   // client covers the whole window (custom title bar)
    if (msg == WM_NCHITTEST) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        float s = f->gui.s(), x = p.x / s, y = p.y / s;
        if (y < kTitleH && !f->closeBtn_.contains(x, y)) return HTCAPTION;
        return HTCLIENT;
    }
    LRESULT r;
    if (f->gui.handle(msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        f->paint();
        EndPaint(h, &ps);
        f->gui.runLater();
        return 0;
    }
    case WM_TIMER:
        if (wp == 3 && f->onTick) { f->onTick(); f->gui.redraw(); }
        return 0;
    case WM_CLOSE:
        f->close(false);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int messageBox(HWND owner, const std::wstring& text, bool yesNo, bool error) {
    Form f(owner, error ? L"문제가 생겼어요" : L"BongGIF", 420);
    f.okText = yesNo ? L"예" : L"확인";
    f.cancelText = L"아니요";
    f.showCancel = yesNo;
    f.body = [&](Gui& g, float x, float y, float w) {
        RectF tile{x, y + 4, 34, 34};
        g.iconTile(tile, error ? ic::Info : ic::Info, error ? th::red : th::accent, error ? th::pink : th::accentEnd);
        float th_ = g.g.textHeight(text, th::body, w - 50);
        g.g.text(text, {x + 48, y + 6, w - 50, th_ + 4}, th::textPrimary, th::body, 400, A_LEFT, false, true);
        return y + std::max(44.f, th_ + 14);
    };
    return f.run() ? 1 : 0;
}

bool runWithProgress(HWND owner, const std::wstring& title,
                     const std::function<std::string(const std::function<void(double)>&, const std::function<bool()>&)>& work) {
    Form f(owner, title, 380);
    std::atomic<double> prog{0};
    std::atomic<bool> cancel{false}, done{false};
    std::string err;
    f.okText = L"취소";
    f.showCancel = false;
    f.body = [&](Gui& g, float x, float y, float w) {
        g.g.text(cancel ? L"취소하는 중이에요…" : L"잠시만 기다려 주세요.", {x, y, w, 22}, th::textSecondary, th::body);
        g.progress({x, y + 32, w, 6}, (float)prog);
        return y + 50;
    };
    f.onOk = [&] { cancel = true; return false; };
    f.tickMs = 50;
    f.onTick = [&] { if (done) f.close(true); };
    std::thread th([&] {
        err = work([&](double v) { prog = v; }, [&] { return cancel.load(); });
        done = true;
    });
    bool finished = f.run();
    if (!finished) cancel = true;
    th.join();
    if (cancel) return false;
    if (!err.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, err.data(), (int)err.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, err.data(), (int)err.size(), w.data(), n);
        messageBox(owner, w, false, true);
        return false;
    }
    return true;
}

} // namespace bg
