#pragma once
// Screen capture (GDI BitBlt SRCCOPY|CAPTUREBLT), software cursor + highlight + click rings,
// recorder thread (high-res waitable timer, µs timestamps, identical frames only extend duration).
#include "core/Image.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

namespace bg {

struct RecordOptions {
    RECT rect{};             // screen rect to capture
    int fps = 15;
    bool cursor = false, highlight = false, clickFx = false, skipSame = true;
    int maxSec = 60;
    size_t maxMemMB = 3000;
    uint32_t colHighlight = rgb(255, 255, 0), colL = rgb(255, 0, 0), colR = rgb(0, 0, 255);
    bool useDxgi = false;          // DXGI desktop duplication (HDR aware), falls back to GDI
    int scaleDiv = 1;              // 1, 2 (half) or 4 (quarter size)
    std::wstring directSavePath;   // non-empty: encode straight to this MP4, keep nothing in memory
    int directQuality = 90;        // nRecDirectSaveQuality
    bool noTimeLimit = false;      // bRecDirectSaveNoTimeLimit
};

class Recorder {
public:
    ~Recorder() { stop(); }
    void start(const RecordOptions& o, HWND notify, UINT msg);   // posts msg when it stops by itself
    void stop();
    void togglePause() { paused_ = !paused_; }
    bool paused() const { return paused_; }
    bool running() const { return th_.joinable(); }
    Frames takeFrames();
    int64_t elapsedUs() const { return elapsed_; }
    size_t memBytes() const { return mem_; }
    size_t frameCount() const { return count_; }
    int stopReason() const { return reason_; }   // 0 user, 1 time, 2 memory, 3 direct-save error
    bool usedDxgi() const { return dxgiUsed_; }
    const std::wstring& directSavePath() const { return o_.directSavePath; }

private:
    void run();
    RecordOptions o_;
    HWND notify_ = nullptr;
    UINT msg_ = 0;
    std::thread th_;
    std::atomic<bool> stop_{false}, paused_{false};
    std::atomic<int64_t> elapsed_{0};
    std::atomic<size_t> mem_{0}, count_{0};
    std::atomic<int> reason_{0};
    std::atomic<bool> dxgiUsed_{false};
    std::mutex m_;
    Frames frames_;
};

void setDpiAware();
bool excludeFromCapture(HWND h, bool on);
RECT virtualScreen();
bool windowRectAt(POINT pt, HWND ignore, RECT& out);
ImagePtr grabScreen(const RECT& r);   // one-shot grab (no cursor)

} // namespace bg
