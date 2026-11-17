#pragma once
// DXGI Desktop Duplication capture:
// DuplicateOutput1 with {R16G16B16A16_FLOAT, B8G8R8A8} (falls back to DuplicateOutput), HDR outputs
// (ColorSpace G2084/P2020) are tone-mapped to SDR using the monitor's SDR white level, errors are
// retried every 100 ms and the caller falls back to GDI after 5 s of failure.
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace bg {

class DxgiCapture {
public:
    DxgiCapture();
    ~DxgiCapture();
    bool init(const RECT& screenRect);         // the rect must lie inside one monitor
    // 0 = new frame written to dst (w*h BGRA, opaque), 1 = no change (dst untouched), 2 = failed
    int grab(uint32_t* dst, int w, int h, int timeoutMs);
    bool failedTooLong() const;                // > 5 s of consecutive failures
    bool hdr() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace bg
