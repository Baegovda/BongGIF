#include "platform/Capture.h"

#include "platform/Dxgi.h"
#include "platform/Media.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <dwmapi.h>

namespace bg {

void setDpiAware() {
    using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto f = (Fn)GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDpiAwarenessContext"))
        if (f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    SetProcessDPIAware();
}

bool excludeFromCapture(HWND h, bool on) { return SetWindowDisplayAffinity(h, on ? 0x11 /*WDA_EXCLUDEFROMCAPTURE*/ : 0) != 0; }

RECT virtualScreen() {
    RECT r;
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

bool windowRectAt(POINT pt, HWND ignore, RECT& out) {
    HWND h = WindowFromPoint(pt);
    if (!h) return false;
    HWND top = GetAncestor(h, GA_ROOT);
    if (!top || top == ignore) return false;
    // visible frame bounds (without the invisible resize border)
    if (FAILED(DwmGetWindowAttribute(top, DWMWA_EXTENDED_FRAME_BOUNDS, &out, sizeof out))) GetWindowRect(top, &out);
    return true;
}

namespace {

struct Grabber {
    int w, h;
    HDC sdc, mdc;
    HBITMAP bmp, old;
    uint32_t* bits = nullptr;
    Grabber(int w_, int h_) : w(w_), h(h_) {
        sdc = GetDC(nullptr);
        mdc = CreateCompatibleDC(sdc);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        old = (HBITMAP)SelectObject(mdc, bmp);
    }
    ~Grabber() {
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
    }
    // returns cursor position relative to rect (or {-99999,..} if hidden)
    POINT grab(int x, int y, bool cursor) {
        BitBlt(mdc, 0, 0, w, h, sdc, x, y, SRCCOPY | CAPTUREBLT);
        return drawCursor(x, y, cursor);
    }
    POINT drawCursor(int x, int y, bool cursor) {
        POINT cp{-99999, -99999};
        if (cursor) {
            CURSORINFO ci{sizeof ci};
            if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
                ICONINFO ii{};
                int hx = 0, hy = 0;
                if (GetIconInfo(ci.hCursor, &ii)) {
                    hx = ii.xHotspot; hy = ii.yHotspot;
                    if (ii.hbmMask) DeleteObject(ii.hbmMask);
                    if (ii.hbmColor) DeleteObject(ii.hbmColor);
                }
                cp = {ci.ptScreenPos.x - x, ci.ptScreenPos.y - y};
                DrawIconEx(mdc, cp.x - hx, cp.y - hy, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
            }
        }
        GdiFlush();
        return cp;
    }
};

void blendRing(Image& im, int cx, int cy, double radius, uint32_t col, const std::function<int(double)>& alphaAt) {
    int r = (int)radius + 2;
    for (int y = std::max(0, cy - r); y <= std::min(im.h - 1, cy + r); ++y)
        for (int x = std::max(0, cx - r); x <= std::min(im.w - 1, cx + r); ++x) {
            double d = std::sqrt((double)(x - cx) * (x - cx) + (double)(y - cy) * (y - cy));
            int a = alphaAt(d);
            if (a <= 0) continue;
            uint32_t& p = im.row(y)[x];
            p = rgb((R(col) * a + R(p) * (255 - a)) / 255, (G(col) * a + G(p) * (255 - a)) / 255, (B(col) * a + B(p) * (255 - a)) / 255);
        }
}

} // namespace

ImagePtr grabScreen(const RECT& r) {
    int w = r.right - r.left, h = r.bottom - r.top;
    Grabber g(w, h);
    g.grab(r.left, r.top, false);
    auto im = std::make_shared<Image>(w, h);
    for (size_t i = 0; i < im->px.size(); ++i) im->px[i] = g.bits[i] | 0xFF000000u;
    return im;
}

void Recorder::start(const RecordOptions& o, HWND notify, UINT msg) {
    stop();
    o_ = o;
    notify_ = notify;
    msg_ = msg;
    stop_ = false;
    paused_ = false;
    elapsed_ = 0;
    mem_ = 0;
    count_ = 0;
    reason_ = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        frames_.clear();
    }
    th_ = std::thread([this] { run(); });
}

void Recorder::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
}

Frames Recorder::takeFrames() {
    std::lock_guard<std::mutex> lk(m_);
    return std::move(frames_);
}

void Recorder::run() {
    const int x = o_.rect.left, y = o_.rect.top;
    const int w = std::max<int>(2, o_.rect.right - o_.rect.left), h = std::max<int>(2, o_.rect.bottom - o_.rect.top);
    const int div = (o_.scaleDiv == 2 || o_.scaleDiv == 4) ? o_.scaleDiv : 1;
    const int ow = std::max(2, w / div), oh = std::max(2, h / div);
    Grabber g(w, h);
    // optional DXGI duplication; GDI is used when it is off, unavailable, or has failed for 5 s
    std::unique_ptr<DxgiCapture> dx;
    std::vector<uint32_t> clean;   // last DXGI frame without the cursor
    if (o_.useDxgi) {
        dx = std::make_unique<DxgiCapture>();
        if (!dx->init(o_.rect)) dx.reset();
        else clean.assign((size_t)w * h, 0xFF000000u);
    }
    dxgiUsed_ = dx != nullptr;
    // direct save: frames are encoded immediately (one frame is held back until its duration is known)
    Mp4Stream mp4;
    bool direct = !o_.directSavePath.empty();
    if (direct) {
        std::string err;
        if (!mp4.open(o_.directSavePath, ow, oh, o_.fps, o_.directQuality, err)) {
            reason_ = 3;
            if (notify_) PostMessageW(notify_, msg_, reason_, 0);
            return;
        }
    }
    ImagePtr held;
    int64_t heldStart = 0;
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    using clk = std::chrono::steady_clock;
    const double period = 1.0 / std::max(1, o_.fps);
    auto t0 = clk::now();
    double pauseAcc = 0, lastT = 0;
    clk::time_point pauseStart{};
    bool wasPaused = false;
    double nextT = 0;
    struct Click { clk::time_point t; POINT p; bool left; };
    std::deque<Click> clicks;
    bool prevL = false, prevR = false;
    std::vector<uint32_t> last;
    auto secs = [&](clk::time_point t) { return std::chrono::duration<double>(t - t0).count(); };
    auto pace = [&](double target) {
        double wait = target - (secs(clk::now()) - pauseAcc);
        if (wait <= 0) return;
        while (wait > 0.0005 && !stop_) {   // poll clicks while waiting (GetAsyncKeyState, no hook)
            double slice = std::min(wait, 0.01);
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(slice * 1e7);
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
            if (o_.clickFx) {
                bool l = GetAsyncKeyState(VK_LBUTTON) & 0x8000, r = GetAsyncKeyState(VK_RBUTTON) & 0x8000;
                if ((l && !prevL) || (r && !prevR)) {
                    POINT p;
                    GetCursorPos(&p);
                    clicks.push_back({clk::now(), p, l && !prevL});
                    if (clicks.size() > 16) clicks.pop_front();
                }
                prevL = l;
                prevR = r;
            }
            wait = target - (secs(clk::now()) - pauseAcc);
        }
    };
    auto emit = [&](ImagePtr im, double tRel) {   // a new distinct frame starts at tRel
        if (direct) {
            if (held) mp4.write(*held, heldStart, std::max<int64_t>(1, (int64_t)(tRel * 1e6) - heldStart));
            held = im;
            heldStart = (int64_t)(tRel * 1e6);
            count_ = count_ + 1;
            return;
        }
        std::lock_guard<std::mutex> lk(m_);
        frames_.push_back({im, (int64_t)(period * 1e6)});
        mem_ += im->bytes();
        count_ = frames_.size();
    };
    while (!stop_) {
        if (paused_) {
            if (!wasPaused) { pauseStart = clk::now(); wasPaused = true; }
            Sleep(20);
            continue;
        }
        if (wasPaused) {
            pauseAcc += std::chrono::duration<double>(clk::now() - pauseStart).count();
            wasPaused = false;
            nextT = secs(clk::now()) - pauseAcc;
        }
        POINT cp;
        if (dx && !dx->failedTooLong()) {
            int r = dx->grab(clean.data(), w, h, 0);
            if (r == 2 && dx->failedTooLong()) { dx.reset(); dxgiUsed_ = false; continue; }
            memcpy(g.bits, clean.data(), clean.size() * 4);
            cp = g.drawCursor(x, y, o_.cursor);
            GdiFlush();
        } else {
            cp = g.grab(x, y, o_.cursor);
        }
        auto now = clk::now();
        double tRel = secs(now) - pauseAcc;
        auto im = std::make_shared<Image>(w, h);
        for (size_t i = 0; i < im->px.size(); ++i) im->px[i] = g.bits[i] | 0xFF000000u;
        if (o_.cursor && ((o_.highlight && cp.x > -99999) || !clicks.empty())) {
            if (o_.highlight && cp.x > -99999)   // 70 px circle, alpha 120 centre -> 30 edge
                blendRing(*im, cp.x, cp.y, 35, o_.colHighlight, [](double d) { return d <= 35 ? (int)(120 - 90 * d / 35) : 0; });
            for (auto& c : clicks) {   // ring r = 0.05 t, alpha 255 - 0.25 t, while alpha > 50
                double t = std::chrono::duration<double, std::milli>(now - c.t).count();
                double rr = 0.05 * t;
                int a = (int)(255 - 0.25 * t);
                if (a <= 50 || rr < 1) continue;
                blendRing(*im, c.p.x - x, c.p.y - y, rr + 2, c.left ? o_.colL : o_.colR,
                          [rr, a](double d) { return std::abs(d - rr) <= 1.5 ? a : 0; });
            }
            while (!clicks.empty() && std::chrono::duration<double, std::milli>(now - clicks.front().t).count() > 900) clicks.pop_front();
        }
        if (div > 1) {   // "save at half / quarter size": box downscale
            auto sm = std::make_shared<Image>(ow, oh);
            for (int yy = 0; yy < oh; ++yy)
                for (int xx = 0; xx < ow; ++xx) {
                    int r = 0, gg = 0, b = 0;
                    for (int dy = 0; dy < div; ++dy)
                        for (int dx2 = 0; dx2 < div; ++dx2) {
                            uint32_t c = im->row(std::min(h - 1, yy * div + dy))[std::min(w - 1, xx * div + dx2)];
                            r += R(c); gg += G(c); b += B(c);
                        }
                    int n = div * div;
                    sm->row(yy)[xx] = rgb(r / n, gg / n, b / n);
                }
            im = sm;
        }
        bool same = o_.skipSame && count_ > 0 && im->px == last;
        if (!direct) {
            std::lock_guard<std::mutex> lk(m_);
            if (!frames_.empty()) frames_.back().dur = std::max<int64_t>(1, (int64_t)((tRel - lastT) * 1e6));
        }
        elapsed_ = (int64_t)(tRel * 1e6);
        if (!same) {   // identical frames only extend the previous duration
            lastT = tRel;
            emit(im, tRel);
            last = im->px;
            bool timeUp = direct ? (!o_.noTimeLimit && tRel >= o_.maxSec) : tRel >= o_.maxSec;
            if (timeUp) { reason_ = 1; break; }
            if (!direct && mem_ >= o_.maxMemMB * 1024 * 1024) { reason_ = 2; break; }
        }
        nextT += period;
        if (nextT < tRel - period) nextT = tRel;   // running late: resync
        pace(nextT);
    }
    double end = secs(clk::now()) - pauseAcc;
    if (direct) {
        if (held) mp4.write(*held, heldStart, std::max<int64_t>((int64_t)(end * 1e6) - heldStart, (int64_t)(period * 1e6)));
        if (!mp4.close() && !reason_) reason_ = 3;
    } else {
        std::lock_guard<std::mutex> lk(m_);
        if (!frames_.empty()) frames_.back().dur = std::max<int64_t>((int64_t)((end - lastT) * 1e6), (int64_t)(period * 1e6));
    }
    if (timer) CloseHandle(timer);
    if (reason_ && notify_) PostMessageW(notify_, msg_, reason_, 0);
}

} // namespace bg
