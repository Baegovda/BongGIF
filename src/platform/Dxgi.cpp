#include "platform/Dxgi.h"

#include <dxgiformat.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace bg {

struct DxgiCapture::Impl {
    RECT rect{};
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGIOutputDuplication> dup;
    ComPtr<ID3D11Texture2D> staging;
    RECT outRect{};
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    bool isHdr = false;
    float whiteNits = 80;
    std::wstring deviceName;
    std::chrono::steady_clock::time_point firstFail{}, lastTry{};
    bool failing = false;

    static float half2float(uint16_t h) {
        uint32_t s = (h & 0x8000u) << 16, e = (h >> 10) & 0x1F, m = h & 0x3FF;
        uint32_t f;
        if (e == 0) {
            if (m == 0) f = s;
            else {   // subnormal
                e = 1;
                while (!(m & 0x400)) { m <<= 1; --e; }
                m &= 0x3FF;
                f = s | ((e + 112) << 23) | (m << 13);
            }
        } else if (e == 31) f = s | 0x7F800000u | (m << 13);
        else f = s | ((e + 112) << 23) | (m << 13);
        float out;
        memcpy(&out, &f, 4);
        return out;
    }

    float queryWhite() {   // monitor SDR white level (DISPLAYCONFIG_SDR_WHITE_LEVEL, 1000 = 80 nits)
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return 80;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return 80;
        for (UINT32 i = 0; i < np; ++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
            src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            src.header.size = sizeof src;
            src.header.adapterId = paths[i].sourceInfo.adapterId;
            src.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || deviceName != src.viewGdiDeviceName) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL wl{};
            wl.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            wl.header.size = sizeof wl;
            wl.header.adapterId = paths[i].targetInfo.adapterId;
            wl.header.id = paths[i].targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&wl.header) == ERROR_SUCCESS) return std::max(80.f, wl.SDRWhiteLevel / 1000.f * 80.f);
        }
        return 80;
    }

    bool open() {
        dup.Reset(); staging.Reset(); ctx.Reset(); dev.Reset();
        ComPtr<IDXGIFactory1> fac;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) return false;
        ComPtr<IDXGIAdapter1> ad;
        for (UINT a = 0; fac->EnumAdapters1(a, &ad) != DXGI_ERROR_NOT_FOUND; ++a) {
            ComPtr<IDXGIOutput> out;
            for (UINT o = 0; ad->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC od;
                out->GetDesc(&od);
                RECT dr = od.DesktopCoordinates;
                if (!od.AttachedToDesktop || rect.left < dr.left || rect.top < dr.top || rect.right > dr.right || rect.bottom > dr.bottom) {
                    out.Reset();
                    continue;
                }
                if (od.Rotation != DXGI_MODE_ROTATION_IDENTITY && od.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) return false;
                D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0};
                if (FAILED(D3D11CreateDevice(ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 1, D3D11_SDK_VERSION,
                                             &dev, nullptr, &ctx)))
                    return false;
                outRect = dr;
                deviceName = od.DeviceName;
                isHdr = false;
                ComPtr<IDXGIOutput6> o6;
                if (SUCCEEDED(out.As(&o6))) {
                    DXGI_OUTPUT_DESC1 d1;
                    if (SUCCEEDED(o6->GetDesc1(&d1)) && d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) isHdr = true;
                }
                ComPtr<IDXGIOutput5> o5;
                DXGI_FORMAT fmts[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
                HRESULT hr = E_FAIL;
                if (SUCCEEDED(out.As(&o5))) hr = o5->DuplicateOutput1(dev.Get(), 0, 2, fmts, &dup);
                if (FAILED(hr)) {
                    ComPtr<IDXGIOutput1> o1;
                    if (FAILED(out.As(&o1)) || FAILED(o1->DuplicateOutput(dev.Get(), &dup))) return false;
                }
                DXGI_OUTDUPL_DESC dd;
                dup->GetDesc(&dd);
                fmt = dd.ModeDesc.Format;
                whiteNits = isHdr ? queryWhite() : 80;
                D3D11_TEXTURE2D_DESC td{};
                td.Width = rect.right - rect.left;
                td.Height = rect.bottom - rect.top;
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = fmt;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_STAGING;
                td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                return SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &staging));
            }
        }
        return false;
    }

    void fail() {
        auto now = std::chrono::steady_clock::now();
        if (!failing) { failing = true; firstFail = now; }
        dup.Reset();
    }
};

DxgiCapture::DxgiCapture() : d_(new Impl) {}
DxgiCapture::~DxgiCapture() = default;
bool DxgiCapture::hdr() const { return d_->isHdr; }
bool DxgiCapture::failedTooLong() const {
    return d_->failing && std::chrono::steady_clock::now() - d_->firstFail > std::chrono::seconds(5);
}

bool DxgiCapture::init(const RECT& r) {
    d_->rect = r;
    return d_->open();
}

int DxgiCapture::grab(uint32_t* dst, int w, int h, int timeoutMs) {
    Impl& d = *d_;
    auto now = std::chrono::steady_clock::now();
    if (!d.dup) {
        if (now - d.lastTry < std::chrono::milliseconds(100)) return 2;   // retry every 100 ms
        d.lastTry = now;
        if (!d.open()) { d.fail(); return 2; }
    }
    DXGI_OUTDUPL_FRAME_INFO fi;
    ComPtr<IDXGIResource> res;
    HRESULT hr = d.dup->AcquireNextFrame(timeoutMs, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return 1;
    if (FAILED(hr)) { d.fail(); return 2; }
    d.failing = false;
    if (fi.LastPresentTime.QuadPart == 0 && fi.AccumulatedFrames == 0) { d.dup->ReleaseFrame(); return 1; }
    ComPtr<ID3D11Texture2D> tex;
    res.As(&tex);
    D3D11_BOX box{(UINT)(d.rect.left - d.outRect.left), (UINT)(d.rect.top - d.outRect.top), 0, (UINT)(d.rect.right - d.outRect.left),
                  (UINT)(d.rect.bottom - d.outRect.top), 1};
    d.ctx->CopySubresourceRegion(d.staging.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
    d.dup->ReleaseFrame();
    D3D11_MAPPED_SUBRESOURCE m;
    for (int tries = 0;; ++tries) {   // DO_NOT_WAIT + Sleep(1) for up to 1 s
        hr = d.ctx->Map(d.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING || tries > 1000) break;
        Sleep(1);
    }
    if (FAILED(hr)) { d.fail(); return 2; }
    int cw = std::min<int>(w, d.rect.right - d.rect.left), ch = std::min<int>(h, d.rect.bottom - d.rect.top);
    if (d.fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        // scRGB linear (1.0 = 80 nits) -> SDR: divide by the SDR white level, soft knee above 0.8, sRGB encode
        float inv = 80.f / d.whiteNits;
        static uint8_t lut[4097];
        static bool lutInit = false;
        if (!lutInit) {
            for (int i = 0; i <= 4096; ++i) {
                double v = i / 4096.0;
                lut[i] = (uint8_t)std::lround(255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055));
            }
            lutInit = true;
        }
        auto tone = [&](float x) {
            x *= inv;
            if (x < 0) x = 0;
            if (x > 0.8f) x = 0.8f + (x - 0.8f) / (1 + (x - 0.8f) / 0.2f);
            return lut[std::min(4096, (int)(x * 4096 + 0.5f))];
        };
        for (int y = 0; y < ch; ++y) {
            const uint16_t* s = (const uint16_t*)((const uint8_t*)m.pData + (size_t)y * m.RowPitch);
            uint32_t* o = dst + (size_t)y * w;
            for (int x = 0; x < cw; ++x) {
                uint8_t r = tone(Impl::half2float(s[x * 4])), g = tone(Impl::half2float(s[x * 4 + 1])), b = tone(Impl::half2float(s[x * 4 + 2]));
                o[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    } else {
        for (int y = 0; y < ch; ++y) {
            const uint32_t* s = (const uint32_t*)((const uint8_t*)m.pData + (size_t)y * m.RowPitch);
            uint32_t* o = dst + (size_t)y * w;
            for (int x = 0; x < cw; ++x) o[x] = s[x] | 0xFF000000u;
        }
    }
    d.ctx->Unmap(d.staging.Get(), 0);
    return 0;
}

} // namespace bg
