#include <dxgiformat.h>
#include <dxgi1_6.h>
#include <d3d11.h>

#include "platform/Player.h"

#include "core/Edit.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace bg {

namespace {
class Notify : public IMFMediaEngineNotify {
public:
    std::atomic<long> ref{1};
    std::atomic<bool> error{false}, seeking{false}, newFrame{false};
    STDMETHODIMP QueryInterface(REFIID iid, void** pv) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFMediaEngineNotify)) {
            *pv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *pv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref; }
    STDMETHODIMP_(ULONG) Release() override {
        long r = --ref;
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP EventNotify(DWORD ev, DWORD_PTR, DWORD) override {
        switch (ev) {
        case MF_MEDIA_ENGINE_EVENT_ERROR: error = true; break;
        case MF_MEDIA_ENGINE_EVENT_SEEKING: seeking = true; break;
        case MF_MEDIA_ENGINE_EVENT_SEEKED: seeking = false; newFrame = true; break;
        case MF_MEDIA_ENGINE_EVENT_LOADEDDATA: newFrame = true; break;
        default: break;
        }
        return S_OK;
    }
};
}   // namespace

struct VideoPlayer::Impl {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IMFDXGIDeviceManager> mgr;
    ComPtr<IMFMediaEngine> eng;
    ComPtr<IMFMediaEngineEx> engEx;
    ComPtr<ID3D11Texture2D> tex, staging;
    Notify* notify = nullptr;
    int maxW = 960, maxH = 540, tw = 0, th = 0;
    double pendingSeek = -1;
    bool paused = true;
};

VideoPlayer::VideoPlayer() : d_(std::make_unique<Impl>()) {}

VideoPlayer::~VideoPlayer() {
    if (d_->eng) d_->eng->Shutdown();
    d_->eng.Reset();
    d_->engEx.Reset();
    if (d_->notify) d_->notify->Release();
}

bool VideoPlayer::open(const std::wstring& path, int maxW, int maxH, std::string& err) {
    Impl& d = *d_;
    d.maxW = maxW;
    d.maxH = maxH;
    const D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 4, D3D11_SDK_VERSION, &d.dev, nullptr, &d.ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 4,
                                           D3D11_SDK_VERSION, &d.dev, nullptr, &d.ctx);
    if (FAILED(hr)) { err = "D3D11"; return false; }
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(d.dev.As(&mt))) mt->SetMultithreadProtected(TRUE);
    UINT token = 0;
    if (FAILED(MFCreateDXGIDeviceManager(&token, &d.mgr)) || FAILED(d.mgr->ResetDevice(d.dev.Get(), token))) { err = "DXGI manager"; return false; }
    ComPtr<IMFMediaEngineClassFactory> f;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) { err = "Media Engine"; return false; }
    d.notify = new Notify;
    ComPtr<IMFAttributes> a;
    MFCreateAttributes(&a, 3);
    a->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, d.notify);
    a->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, d.mgr.Get());
    a->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (FAILED(f->CreateInstance(0, a.Get(), &d.eng))) { err = "Media Engine"; return false; }
    d.eng.As(&d.engEx);
    d.eng->SetAutoPlay(FALSE);
    d.eng->SetLoop(FALSE);
    BSTR url = SysAllocString(path.c_str());
    hr = d.eng->SetSource(url);
    SysFreeString(url);
    if (FAILED(hr)) { err = "source"; return false; }
    return true;
}

ImagePtr VideoPlayer::frame() {
    Impl& d = *d_;
    if (!d.eng) return nullptr;
    // a seek requested while another one ran
    if (d.pendingSeek >= 0 && !d.notify->seeking && !d.eng->IsSeeking()) {
        double t = d.pendingSeek;
        d.pendingSeek = -1;
        d.notify->seeking = true;
        d.eng->SetCurrentTime(t);
    }
    LONGLONG pts = 0;
    bool tick = d.eng->OnVideoStreamTick(&pts) == S_OK;
    if (!tick && !d.notify->newFrame) return nullptr;
    d.notify->newFrame = false;
    if (!d.tex) {
        DWORD nw = 0, nh = 0;
        if (FAILED(d.eng->GetNativeVideoSize(&nw, &nh)) || !nw || !nh) return nullptr;
        double s = std::min({1.0, (double)d.maxW / nw, (double)d.maxH / nh});
        d.tw = std::max(2, (int)std::lround(nw * s)) & ~1;
        d.th = std::max(2, (int)std::lround(nh * s)) & ~1;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = d.tw; td.Height = d.th; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d.dev->CreateTexture2D(&td, nullptr, &d.tex))) return nullptr;
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(d.dev->CreateTexture2D(&td, nullptr, &d.staging))) { d.tex.Reset(); return nullptr; }
    }
    MFVideoNormalizedRect src{0, 0, 1, 1};
    RECT dst{0, 0, d.tw, d.th};
    MFARGB border{0, 0, 0, 255};
    if (FAILED(d.eng->TransferVideoFrame(d.tex.Get(), &src, &dst, &border))) return nullptr;
    d.ctx->CopyResource(d.staging.Get(), d.tex.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(d.ctx->Map(d.staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return nullptr;
    auto im = std::make_shared<Image>(d.tw, d.th);
    for (int y = 0; y < d.th; ++y) {
        const uint32_t* s = (const uint32_t*)((const uint8_t*)m.pData + (size_t)y * m.RowPitch);
        uint32_t* o = im->row(y);
        for (int x = 0; x < d.tw; ++x) o[x] = s[x] | 0xFF000000u;
    }
    d.ctx->Unmap(d.staging.Get(), 0);
    return im;
}

void VideoPlayer::play() {
    if (!d_->eng) return;
    d_->eng->Play();
    d_->paused = false;
}
void VideoPlayer::pause() {
    if (!d_->eng) return;
    d_->eng->Pause();
    d_->paused = true;
}
bool VideoPlayer::paused() const { return !d_->eng || d_->paused || d_->eng->IsPaused() || d_->eng->IsEnded(); }

void VideoPlayer::seek(double t) {
    Impl& d = *d_;
    if (!d.eng) return;
    t = std::max(0.0, t);
    if (d.notify->seeking || d.eng->IsSeeking()) { d.pendingSeek = t; return; }
    d.pendingSeek = -1;
    d.notify->seeking = true;
    if (d.paused && d.engEx) d.engEx->SetCurrentTimeEx(t, MF_MEDIA_ENGINE_SEEK_MODE_NORMAL);
    else d.eng->SetCurrentTime(t);
}

double VideoPlayer::time() const {
    if (!d_->eng) return 0;
    return d_->pendingSeek >= 0 ? d_->pendingSeek : d_->eng->GetCurrentTime();
}
double VideoPlayer::duration() const {
    if (!d_->eng) return 0;
    double v = d_->eng->GetDuration();
    return std::isfinite(v) ? v : 0;
}
void VideoPlayer::setMuted(bool m) {
    if (d_->eng) d_->eng->SetMuted(m);
}
void VideoPlayer::setVolume(double v) {
    if (d_->eng) d_->eng->SetVolume(std::clamp(v, 0.0, 1.0));
}
void VideoPlayer::setRate(double r) {
    if (d_->eng && std::fabs(d_->eng->GetPlaybackRate() - r) > 1e-3) d_->eng->SetPlaybackRate(r);
}
int VideoPlayer::frameWidth() const { return d_->tw; }
int VideoPlayer::frameHeight() const { return d_->th; }
bool VideoPlayer::hasAudio() const { return d_->eng && d_->eng->HasAudio(); }
bool VideoPlayer::failed() const { return !d_->eng || d_->notify->error; }

// ------------------------------------------------------------------ thumbnails
bool videoThumbnails(const std::wstring& path, int n, int thumbH, const std::function<void(int, ImagePtr)>& onThumb,
                     const std::function<bool()>& cancel) {
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    bool ok = false;
    {
        ComPtr<IMFAttributes> a;
        MFCreateAttributes(&a, 1);
        a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        ComPtr<IMFSourceReader> rd;
        if (SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), a.Get(), &rd))) {
            rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
            rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
            ComPtr<IMFMediaType> t;
            MFCreateMediaType(&t);
            t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, t.Get()))) {
                ok = true;
                PROPVARIANT pv;
                PropVariantInit(&pv);
                double dur = 0;
                if (SUCCEEDED(rd->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)))
                    dur = pv.uhVal.QuadPart / 1e7;
                PropVariantClear(&pv);
                for (int i = 0; i < n && !(cancel && cancel()); ++i)
                for (int attempt = 0; attempt < 2; ++attempt) {   // near the end a seek can hit end-of-stream: retry earlier
                    double at = std::max(0.0, std::min(dur * (i + 0.5) / n, dur - 0.3) - attempt * 1.0);
                    bool got = false;
                    PROPVARIANT p;
                    PropVariantInit(&p);
                    p.vt = VT_I8;
                    p.hVal.QuadPart = (LONGLONG)(at * 1e7);
                    rd->SetCurrentPosition(GUID_NULL, p);
                    for (int tries = 0; tries < 300 && !(cancel && cancel()); ++tries) {
                        DWORD flags = 0;
                        LONGLONG ts = 0;
                        ComPtr<IMFSample> s;
                        if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &s))) break;
                        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
                        if (!s) continue;
                        if (ts / 1e7 + 0.3 < at && tries < 60) continue;   // decode forward from the keyframe (bounded)
                        ComPtr<IMFMediaType> ct;
                        rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &ct);
                        UINT32 w = 0, h = 0, st = 0;
                        MFGetAttributeSize(ct.Get(), MF_MT_FRAME_SIZE, &w, &h);
                        LONG stride = (LONG)(w * 4);
                        if (SUCCEEDED(ct->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) stride = (LONG)st;
                        ComPtr<IMFMediaBuffer> buf;
                        s->ConvertToContiguousBuffer(&buf);
                        BYTE* px = nullptr;
                        DWORD len = 0;
                        if (!buf || FAILED(buf->Lock(&px, nullptr, &len))) break;
                        Image im((int)w, (int)h);
                        for (UINT32 y = 0; y < h; ++y) {
                            const BYTE* src = stride > 0 ? px + (size_t)y * stride : px + (size_t)(h - 1 - y) * (-stride);
                            if ((size_t)(src - px) + w * 4 > len) break;
                            const uint32_t* s32 = (const uint32_t*)src;
                            uint32_t* o = im.row((int)y);
                            for (UINT32 x = 0; x < w; ++x) o[x] = s32[x] | 0xFF000000u;
                        }
                        buf->Unlock();
                        int tw = std::max(8, (int)std::lround((double)w * thumbH / std::max<UINT32>(1, h)));
                        onThumb(i, resizeImage(im, tw, thumbH));
                        got = true;
                        break;
                    }
                    if (got) break;
                }
            }
        }
    }
    if (com) CoUninitialize();
    return ok;
}

} // namespace bg
