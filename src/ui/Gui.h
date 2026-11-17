#pragma once
// Immediate-mode UI on Direct2D in the BongHub style. A window's paint pass both lays out,
// draws and reacts to input; mouse messages trigger a synchronous repaint, and actions that open
// windows are queued with later() and run after the paint. Animations repaint on a 16 ms timer
// only while something is moving (idle = no timer).
#include "ui/Gfx.h"
#include "ui/Theme.h"

#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bg {

using Id = uint64_t;
constexpr Id gid(const char* s, uint64_t n = 0) {
    uint64_t h = 1469598103934665603ull ^ n;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ull; }
    return h;
}
inline Id gid(Id base, uint64_t n) { return (base ^ (n + 0x9E3779B97F4A7C15ull)) * 1099511628211ull; }

enum BtnStyle { BS_SECONDARY = 0, BS_PRIMARY, BS_GHOST, BS_DANGER };

struct ScrollArea {
    float pos = 0, content = 0, view = 0;
    float top = 0, x = 0, w = 0;   // view rect (set by beginScroll)
};

class Gui {
public:
    Gfx g;
    HWND hwnd = nullptr;
    float mx = -1e6f, my = -1e6f;
    bool down = false, pressed = false, released = false;
    bool rdown = false, rpressed = false, rreleased = false;   // right button (pan gestures)
    bool dblclick = false;
    float wheel = 0;
    Id hot = 0, active = 0;
    bool enabledInput = true;

    void attach(HWND h);
    ~Gui();
    // route window messages; returns true when handled (result in r)
    bool handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& r);
    bool beginFrame();     // inside WM_PAINT
    void endFrame();
    void later(std::function<void()> fn) { later_.push_back(std::move(fn)); }
    void runLater();
    void redraw() { InvalidateRect(hwnd, nullptr, FALSE); }
    float s() const { return g.scale(); }

    // interaction primitives
    bool hit(RectF r) const;
    bool interact(Id id, RectF r, bool enabled = true);     // true = clicked
    float anim(Id id, float target, float ms);              // 0..1 eased, keeps repainting until settled
    float hoverT(Id id) { return anim(id ^ 0x5151, hot == id ? 1.f : 0.f, th::hoverMs); }

    // surfaces
    void card(RectF r, float radius = th::radiusCard, bool shadow = true);
    void separator(float x1, float x2, float y);
    void chip(RectF r, const std::wstring& t, uint32_t fg, uint32_t bg);
    void iconTile(RectF r, wchar_t icon, uint32_t c1, uint32_t c2);

    // controls (draw + interact). Return true on click / value change.
    bool button(Id id, RectF r, const std::wstring& text, BtnStyle st = BS_SECONDARY, wchar_t icon = 0, bool enabled = true);
    bool iconButton(Id id, RectF r, wchar_t icon, bool enabled = true, uint32_t color = th::textSecondary, float size = 14);
    bool navItem(Id id, RectF r, wchar_t icon, uint32_t tile1, uint32_t tile2, const std::wstring& text, bool selected = false,
                 bool enabled = true);
    bool toggle(Id id, RectF r, bool& v, bool enabled = true);              // r = 40x20 switch
    bool slider(Id id, RectF r, int& v, int lo, int hi, bool enabled = true);
    bool segmented(Id id, RectF r, const std::vector<std::wstring>& items, int& sel, bool enabled = true);
    bool dropdown(Id id, RectF r, const std::vector<std::wstring>& items, int& sel, bool enabled = true);
    bool edit(Id id, RectF r, std::wstring& text, bool enabled = true);
    bool number(Id id, RectF r, int& v, int lo, int hi, bool enabled = true);
    void progress(RectF r, float v);
    // option row: title (+ description) on the left, returns the control rect (cw wide) at the right.
    // Draws a separator under the row unless last. Disabled rows render at 40% opacity.
    RectF optionRow(float x, float& y, float w, const std::wstring& title, const std::wstring& desc, float cw, float ch,
                    bool enabled = true, bool sepBelow = true);
    uint32_t dim(uint32_t c, bool enabled) const { return enabled ? c : ((((c >> 24) * 2 / 5) << 24) | (c & 0xFFFFFF)); }

    // scrolling region
    float beginScroll(ScrollArea& sa, RectF view);   // returns content origin y
    void endScroll(ScrollArea& sa, float contentBottom);

    std::vector<RectF> clips;

private:
    struct EditBox { HWND h = nullptr; bool used = false; std::wstring last; bool focus = false; };
    std::map<Id, EditBox> edits_;
    std::vector<std::function<void()>> later_;
    std::unordered_map<Id, float> anim_;
    std::set<Id> changed_;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
    float dt_ = 0;
    bool animating_ = false, tracking_ = false;
    HFONT editFont_ = nullptr;
    float editFontScale_ = 0;
    HBRUSH editBrush_ = nullptr;
    void syncInput();
};

// Popup menu (dropdown list) in menu.* colours, enter animation 180 ms.
void showMenu(HWND owner, RECT screenAnchor, const std::vector<std::wstring>& items, int sel, std::function<void(int)> onPick);
// checks[i]: 1 = checked, 0 = unchecked, -1 = no check column; item L"-" = separator
void showMenuChecks(HWND owner, RECT screenAnchor, const std::vector<std::wstring>& items, const std::vector<int>& checks,
                    std::function<void(int)> onPick);

// Borderless modal window (glass style, custom title bar, DWM shadow + rounded corners).
class Form {
public:
    Gui gui;
    std::wstring title;
    float width;
    // body(gui, x, y, w) draws content starting at y and returns the bottom y
    std::function<float(Gui&, float, float, float)> body;
    std::function<bool()> onOk;          // false keeps the form open
    std::function<void()> onTick;        // called every tickMs while open (0 = none)
    std::function<bool(int)> onKey;      // virtual key outside edit boxes; true = handled
    int tickMs = 0;
    std::wstring okText = L"확인", cancelText = L"취소";
    bool showCancel = true, okEnabled = true;

    Form(HWND owner, std::wstring title, float width);
    ~Form();
    bool run();
    void close(bool ok) { ok_ = ok; done_ = true; }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    HWND owner_, hwnd_ = nullptr;
    bool done_ = false, ok_ = false;
    float contentH_ = 0;
    ScrollArea scroll_;
    RectF closeBtn_;
};

int messageBox(HWND owner, const std::wstring& text, bool yesNo = false, bool error = false);   // 1 = OK/yes
bool runWithProgress(HWND owner, const std::wstring& title,
                     const std::function<std::string(const std::function<void(double)>&, const std::function<bool()>&)>& work);
std::wstring fmtBytes(size_t n);
void applyWindowStyle(HWND h);      // dark mode, rounded corners, border colour

// Segoe Fluent Icons code points
namespace ic {
constexpr wchar_t Record = 0xE7C8, Stop = 0xE71A, Pause = 0xE769, Play = 0xE768, Prev = 0xE892, Next = 0xE893,
                  First = 0xE892, Undo = 0xE7A7, Redo = 0xE7A6, Open = 0xE8E5, Folder = 0xE8B7, Photo = 0xE91B,
                  Save = 0xE74E, Zoom = 0xE71E, Clock = 0xE916, Fast = 0xEB9D, Slow = 0xEC57, Reverse = 0xE72B,
                  Repeat = 0xE8EE, Trim = 0xE78A, Reduce = 0xE71C, Summary = 0xE9D9, Delete = 0xE74D, Add = 0xE710,
                  Resize = 0xE740, Crop = 0xE7A8, Font = 0xE8D2, Picture = 0xE8B9, Transition = 0xE8AB, Effect = 0xE790,
                  Reset = 0xE72C, ChevronDown = 0xE70D, ChevronUp = 0xE70E, ChevronRight = 0xE76C, ChevronLeft = 0xE76B, Close = 0xE8BB,
                  Minimize = 0xE921, Maximize = 0xE922, Restore = 0xE923, Check = 0xE73E, Region = 0xF407,
                  Window = 0xE737, Cursor = 0xE8B0, Info = 0xE946, Minus = 0xE738, Rotate = 0xE7AD, Edit = 0xE70F,
                  Film = 0xE8B2, Timer = 0xE121, Camera = 0xE722, TextRead = 0xE8D2, Grid = 0xE80A, More = 0xE712,
                  Home = 0xE80F, Settings = 0xE713, Sync = 0xE895, Rename = 0xE8AC, Pen = 0xEE56, Sticker = 0xE76E, Collage = 0xE8A9,
                  ZoomIn = 0xE8A3, Frame = 0xE91B, Sparkle = 0xE945, Language = 0xF2B7, Eraser = 0xE75C;
}

} // namespace bg
