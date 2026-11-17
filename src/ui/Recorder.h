#pragma once
// Recording window in the BongHub style: glass tool bar on top, accent-gradient frame (red while
// recording) and a real hole in the middle (window region), so the capture area is see-through and
// click-through. Extras: image capture, text capture (OCR), rule-of-thirds grid,
// direct save to MP4, magnifier while moving/resizing, saved positions 1–3, size keys 1–9/0,
// half/quarter recording size, Ctrl+Alt follow, DXGI capture.
#include "platform/Capture.h"
#include "ui/Gui.h"

namespace bg {

class App;

class RecorderWindow {
public:
    explicit RecorderWindow(App* app);
    ~RecorderWindow();
    void show();
    void startRecord();
    void stopRecord();
    void toggleRecord() { rec_.running() ? stopRecord() : startRecord(); }
    void togglePause();
    bool recording() const { return rec_.running(); }
    bool preTranslate(MSG& m);
    HWND hwnd() const { return hwnd_; }
    void followMouse(int dx, int dy);   // Ctrl+Alt tracking (called by the app timer)

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PickProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK OverlayProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void paint();
    RECT captureRect() const;
    void setCaptureRect(const RECT& r);
    void updateRegion();
    void updateOverlays();
    void pick(bool windowMode);
    void close();
    void captureImage();
    void captureText();
    void moreMenu(RECT anchor);
    void flash(const std::wstring& msg);

    App* app_;
    HWND hwnd_ = nullptr;
    Gui gui_;
    float barH_ = 52, bw_ = 4;     // DIPs
    bool full_ = false;
    int fpsSel_ = 3, sizeSel_ = -1;
    bool cursor_ = false, highlight_ = false, click_ = false;
    Recorder rec_;
    std::vector<RectF> ctlRects_;  // tool bar controls (not draggable)
    std::wstring flash_;           // short status message on the bar
    // grid + magnifier overlays (excluded from capture)
    HWND gridWnd_ = nullptr, magWnd_ = nullptr;
    bool sizing_ = false;
    // region picker
    HWND pickWnd_ = nullptr;
    bool pickWindow_ = false, picking_ = false;
    POINT p0_{}, p1_{};
    Gui pickGui_;
};

} // namespace bg
