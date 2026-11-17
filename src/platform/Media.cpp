#include <dxgi1_6.h>
#include "platform/Media.h"

#include "core/Edit.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <windows.h>
#include <objbase.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace bg {

static ULONG_PTR g_gdiplus = 0;

void mediaStartup() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&g_gdiplus, &in, nullptr);
}
void mediaShutdown() {
    if (g_gdiplus) Gdiplus::GdiplusShutdown(g_gdiplus);
    MFShutdown();
    CoUninitialize();
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

bool readFile(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out.resize((size_t)f.tellg());
    f.seekg(0);
    f.read((char*)out.data(), out.size());
    return (bool)f;
}

namespace {
// worker threads need their own COM/MF apartment
struct ComScope {
    bool ok;
    ComScope() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() { if (ok) CoUninitialize(); }
};
} // namespace

// ------------------------------------------------------------------ MP4 export
bool encodeMp4(const Frames& frames, const std::wstring& path, int quality, const Progress& prog, const Cancel& cancel,
               std::string& err) {
    if (frames.empty()) { err = "no frames"; return false; }
    ComScope com;
    int W = frames[0].img->w & ~1, H = frames[0].img->h & ~1;
    if (W < 2 || H < 2) { err = "too small"; return false; }
    int64_t total = 0;
    for (auto& f : frames) total += f.dur;
    double avg = (double)total / frames.size();
    int fps = std::clamp((int)std::lround(1e6 / std::max(avg, 1.0)), 5, 60);
    // bits per pixel per frame from quality (0.04 .. 0.35)
    double bpp = 0.04 + 0.31 * std::pow(quality / 100.0, 2.0);
    UINT32 bitrate = (UINT32)std::clamp<double>(W * (double)H * fps * bpp, 200e3, 80e6);

    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 2);
    attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attr->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    ComPtr<IMFSinkWriter> sw;
    HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attr.Get(), &sw);
    if (FAILED(hr)) { err = "MP4 파일을 만들 수 없습니다"; return false; }
    ComPtr<IMFMediaType> outT, inT;
    MFCreateMediaType(&outT);
    outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    outT->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    outT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    outT->SetUINT32(MF_MT_MPEG2_PROFILE, 100 /*eAVEncH264VProfile_High*/);
    MFSetAttributeSize(outT.Get(), MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(outT.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(outT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD stream = 0;
    if (FAILED(sw->AddStream(outT.Get(), &stream))) { err = "H.264 인코더를 찾을 수 없습니다"; return false; }
    MFCreateMediaType(&inT);
    inT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    inT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    inT->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(W * 4));   // top-down
    MFSetAttributeSize(inT.Get(), MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(inT.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(inT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(sw->SetInputMediaType(stream, inT.Get(), nullptr))) { err = "입력 형식 설정 실패"; return false; }
    if (FAILED(sw->BeginWriting())) { err = "인코딩 시작 실패"; return false; }

    int64_t nOut = std::max<int64_t>(1, (int64_t)std::llround(total * fps / 1e6));
    const LONGLONG frameDur = 10000000LL / fps;
    std::vector<int64_t> starts(frames.size());
    for (size_t i = 1; i < frames.size(); ++i) starts[i] = starts[i - 1] + frames[i - 1].dur;
    const DWORD cb = (DWORD)(W * H * 4);
    for (int64_t k = 0; k < nOut; ++k) {
        if (cancel && cancel()) { sw->Finalize(); DeleteFileW(path.c_str()); return false; }
        int64_t t = k * 1000000 / fps;
        size_t i = (size_t)(std::upper_bound(starts.begin(), starts.end(), t) - starts.begin());
        i = i ? i - 1 : 0;
        const Image& im = *frames[i].img;
        ComPtr<IMFMediaBuffer> buf;
        MFCreateMemoryBuffer(cb, &buf);
        BYTE* p = nullptr;
        buf->Lock(&p, nullptr, nullptr);
        for (int y = 0; y < H; ++y) std::memcpy(p + (size_t)y * W * 4, im.row(y), (size_t)W * 4);
        buf->Unlock();
        buf->SetCurrentLength(cb);
        ComPtr<IMFSample> s;
        MFCreateSample(&s);
        s->AddBuffer(buf.Get());
        s->SetSampleTime(k * frameDur);
        s->SetSampleDuration(frameDur);
        if (FAILED(sw->WriteSample(stream, s.Get()))) { err = "프레임 인코딩 실패"; return false; }
        if (prog && k % 4 == 0) prog((double)k / nOut);
    }
    if (FAILED(sw->Finalize())) { err = "MP4 마무리 실패"; return false; }
    if (prog) prog(1.0);
    return true;
}

// ------------------------------------------------------------------ streaming MP4 (direct save)
struct Mp4Stream::Impl {
    ComPtr<IMFSinkWriter> sw;
    DWORD stream = 0;
    int W = 0, H = 0;
    bool comInit = false;
};
Mp4Stream::Mp4Stream() : d_(new Impl) {}
Mp4Stream::~Mp4Stream() { close(); }
bool Mp4Stream::isOpen() const { return d_->sw != nullptr; }

bool Mp4Stream::open(const std::wstring& path, int w, int h, int fps, int quality, std::string& err) {
    d_->comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    int W = w & ~1, H = h & ~1;
    fps = std::clamp(fps, 1, 60);
    double bpp = 0.04 + 0.31 * std::pow(quality / 100.0, 2.0);
    UINT32 bitrate = (UINT32)std::clamp<double>(W * (double)H * std::max(fps, 15) * bpp, 300e3, 80e6);
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 2);
    attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attr->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attr.Get(), &d_->sw))) { err = "MP4 파일을 만들 수 없어요"; return false; }
    ComPtr<IMFMediaType> outT, inT;
    MFCreateMediaType(&outT);
    outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    outT->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    outT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(outT.Get(), MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(outT.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(outT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(d_->sw->AddStream(outT.Get(), &d_->stream))) { d_->sw.Reset(); err = "H.264 인코더를 찾을 수 없어요"; return false; }
    MFCreateMediaType(&inT);
    inT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    inT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    inT->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(W * 4));
    MFSetAttributeSize(inT.Get(), MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(inT.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(inT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(d_->sw->SetInputMediaType(d_->stream, inT.Get(), nullptr)) || FAILED(d_->sw->BeginWriting())) {
        d_->sw.Reset();
        err = "인코딩을 시작하지 못했어요";
        return false;
    }
    d_->W = W;
    d_->H = H;
    return true;
}

bool Mp4Stream::write(const Image& im, int64_t timeUs, int64_t durUs) {
    if (!d_->sw) return false;
    const DWORD cb = (DWORD)(d_->W * d_->H * 4);
    ComPtr<IMFMediaBuffer> buf;
    MFCreateMemoryBuffer(cb, &buf);
    BYTE* p = nullptr;
    buf->Lock(&p, nullptr, nullptr);
    for (int y = 0; y < d_->H; ++y) std::memcpy(p + (size_t)y * d_->W * 4, im.row(std::min(y, im.h - 1)), (size_t)std::min(d_->W, im.w) * 4);
    buf->Unlock();
    buf->SetCurrentLength(cb);
    ComPtr<IMFSample> s;
    MFCreateSample(&s);
    s->AddBuffer(buf.Get());
    s->SetSampleTime(timeUs * 10);
    s->SetSampleDuration(std::max<int64_t>(1, durUs) * 10);
    return SUCCEEDED(d_->sw->WriteSample(d_->stream, s.Get()));
}

bool Mp4Stream::close() {
    bool ok = true;
    if (d_->sw) { ok = SUCCEEDED(d_->sw->Finalize()); d_->sw.Reset(); }
    if (d_->comInit) { CoUninitialize(); d_->comInit = false; }
    return ok;
}

bool loadSrModelResource(int model, int scale, std::vector<uint8_t>& out) {
    int id = 101 + std::clamp(model, 0, 1) * 3 + std::clamp(scale, 2, 4) - 2;
    HMODULE m = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(m, r);
    const uint8_t* p = (const uint8_t*)LockResource(g);
    DWORD n = SizeofResource(m, r);
    if (!p || !n) return false;
    out.assign(p, p + n);
    return true;
}

bool setClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        memcpy(GlobalLock(g), text.c_str(), bytes);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
    return g != nullptr;
}

bool setClipboardImage(HWND owner, const Image& im) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = sizeof(BITMAPINFOHEADER) + im.bytes();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        auto* bi = (BITMAPINFOHEADER*)GlobalLock(g);
        *bi = BITMAPINFOHEADER{sizeof(BITMAPINFOHEADER), im.w, im.h, 1, 32, BI_RGB};
        uint8_t* dst = (uint8_t*)(bi + 1);
        for (int y = 0; y < im.h; ++y) memcpy(dst + (size_t)(im.h - 1 - y) * im.w * 4, im.row(y), (size_t)im.w * 4);   // bottom-up DIB
        GlobalUnlock(g);
        SetClipboardData(CF_DIB, g);
    }
    CloseClipboard();
    return g != nullptr;
}

// ------------------------------------------------------------------ video import
static bool openReader(const std::wstring& path, ComPtr<IMFSourceReader>& rd) {
    ComPtr<IMFAttributes> a;
    MFCreateAttributes(&a, 2);
    a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), a.Get(), &rd))) return false;
    rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    ComPtr<IMFMediaType> t;
    MFCreateMediaType(&t);
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    return SUCCEEDED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, t.Get()));
}

bool videoInfo(const std::wstring& path, VideoInfo& vi) {
    ComScope com;
    ComPtr<IMFSourceReader> rd;
    if (!openReader(path, rd)) return false;
    ComPtr<IMFMediaType> t;
    rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t);
    UINT32 w = 0, h = 0, n = 0, d = 1;
    MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &n, &d);
    vi.w = (int)w; vi.h = (int)h;
    vi.fps = d ? (double)n / d : 30;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(rd->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)))
        vi.duration = pv.uhVal.QuadPart / 1e7;
    PropVariantClear(&pv);
    return w > 0;
}

bool loadVideo(const std::wstring& path, double startS, double endS, int maxFps, int maxSide, Frames& out,
               const Progress& prog, const Cancel& cancel, std::string& err) {
    ComScope com;
    ComPtr<IMFSourceReader> rd;
    if (!openReader(path, rd)) { err = "영상을 열 수 없습니다"; return false; }
    ComPtr<IMFMediaType> t;
    rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t);
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
    LONG stride = (LONG)(w * 4);
    UINT32 st = 0;
    if (SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) stride = (LONG)st;
    if (startS > 0) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = (LONGLONG)(startS * 1e7);
        rd->SetCurrentPosition(GUID_NULL, pv);
    }
    const int64_t step = 1000000 / std::max(1, maxFps);
    int64_t nextT = (int64_t)(startS * 1e6);
    int ow = (int)w, oh = (int)h;
    if (maxSide > 0 && std::max(ow, oh) > maxSide) {
        double s = (double)maxSide / std::max(ow, oh);
        ow = std::max(8, (int)(ow * s)); oh = std::max(8, (int)(oh * s));
    }
    for (;;) {
        if (cancel && cancel()) return false;
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> s;
        if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &s))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t);
            MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
            stride = (LONG)(w * 4);
        }
        if (!s) continue;
        int64_t tus = ts / 10;
        if (endS > 0 && tus > (int64_t)(endS * 1e6)) break;
        if (tus + 1 < nextT) continue;
        ComPtr<IMFMediaBuffer> buf;
        s->ConvertToContiguousBuffer(&buf);
        BYTE* p = nullptr;
        DWORD len = 0;
        buf->Lock(&p, nullptr, &len);
        auto im = std::make_shared<Image>((int)w, (int)h);
        for (UINT32 y = 0; y < h; ++y) {
            const BYTE* src = stride > 0 ? p + (size_t)y * stride : p + (size_t)(h - 1 - y) * (-stride);
            if ((size_t)(src - p) + w * 4 > len) break;
            const uint32_t* s32 = (const uint32_t*)src;
            uint32_t* d = im->row((int)y);
            for (UINT32 x = 0; x < w; ++x) d[x] = s32[x] | 0xFF000000u;
        }
        buf->Unlock();
        ImagePtr ip = im;
        if (ow != (int)w || oh != (int)h) ip = resizeImage(*im, ow, oh);
        out.push_back({ip, step});
        nextT += step;
        if (nextT < tus) nextT = tus + step;
        if (prog && endS > startS) prog(std::clamp((tus / 1e6 - startS) / (endS - startS), 0.0, 1.0));
    }
    if (out.empty()) { err = "프레임을 읽지 못했습니다"; return false; }
    return true;
}

// ------------------------------------------------------------------ WIC still images
bool loadImageWic(const std::wstring& path, Image& out, bool keepAlpha, std::string& err) {
    ComScope com;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) { err = "WIC"; return false; }
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
        err = "이미지를 열 수 없습니다";
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> fr;
    dec->GetFrame(0, &fr);
    ComPtr<IWICFormatConverter> cv;
    f->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        err = "이미지 변환 실패";
        return false;
    }
    UINT w, h;
    cv->GetSize(&w, &h);
    out = Image((int)w, (int)h);
    cv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)out.px.data());
    if (!keepAlpha)
        for (auto& c : out.px) {
            int a = c >> 24;
            c = rgb((R(c) * a + 255 * (255 - a)) / 255, (G(c) * a + 255 * (255 - a)) / 255, (B(c) * a + 255 * (255 - a)) / 255);
        }
    return true;
}

bool loadFramesWic(const std::wstring& path, Frames& out, int frameMs, std::string& err) {
    ComScope com;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) { err = "WIC"; return false; }
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
        err = "이미지를 열 수 없어요 (이 형식의 Windows 코덱이 없을 수 있어요)";
        return false;
    }
    UINT n = 0;
    dec->GetFrameCount(&n);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IWICBitmapFrameDecode> fr;
        if (FAILED(dec->GetFrame(i, &fr))) break;
        ComPtr<IWICFormatConverter> cv;
        f->CreateFormatConverter(&cv);
        if (FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) continue;
        UINT w, h;
        cv->GetSize(&w, &h);
        auto im = std::make_shared<Image>((int)w, (int)h);
        cv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)im->px.data());
        for (auto& c : im->px) {
            int a = c >> 24;
            c = rgb((R(c) * a + 255 * (255 - a)) / 255, (G(c) * a + 255 * (255 - a)) / 255, (B(c) * a + 255 * (255 - a)) / 255);
        }
        if (!out.empty() && (im->w != out[0].img->w || im->h != out[0].img->h)) continue;
        out.push_back({im, frameMs * 1000LL});
    }
    if (out.empty()) { err = "이미지를 읽지 못했어요"; return false; }
    return true;
}

bool saveImageWic(const Image& im, const std::wstring& path, bool jpeg, int jpegQuality, std::string& err) {
    ComScope com;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) { err = "WIC"; return false; }
    ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) { err = "파일을 쓸 수 없어요"; return false; }
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&fr, &props);
    if (jpeg && props) {
        PROPBAG2 opt{};
        opt.pstrName = (LPOLESTR)L"ImageQuality";
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = std::clamp(jpegQuality, 1, 100) / 100.f;
        props->Write(1, &opt, &v);
    }
    fr->Initialize(props.Get());
    fr->SetSize(im.w, im.h);
    WICPixelFormatGUID pf = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGR;
    fr->SetPixelFormat(&pf);
    HRESULT hr;
    if (jpeg) {
        std::vector<uint8_t> bgr((size_t)im.w * im.h * 3);
        for (size_t i = 0; i < im.px.size(); ++i) { bgr[i * 3] = (uint8_t)B(im.px[i]); bgr[i * 3 + 1] = (uint8_t)G(im.px[i]); bgr[i * 3 + 2] = (uint8_t)R(im.px[i]); }
        hr = fr->WritePixels(im.h, im.w * 3, (UINT)bgr.size(), bgr.data());
    } else {
        hr = fr->WritePixels(im.h, im.w * 4, (UINT)im.bytes(), (BYTE*)im.px.data());
    }
    if (FAILED(hr) || FAILED(fr->Commit()) || FAILED(enc->Commit())) { err = "이미지를 저장하지 못했어요"; return false; }
    return true;
}

// ------------------------------------------------------------------ bubbles / stickers / strokes
ImagePtr drawBubble(const Image& src, int x, int y, int w, int h, int tx, int ty, const std::wstring& text, const BubbleStyle& st) {
    auto o = std::make_shared<Image>(src);
    Gdiplus::Bitmap bmp(o->w, o->h, o->w * 4, PixelFormat32bppARGB, (BYTE*)o->px.data());
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    auto C = [](uint32_t c) { return Gdiplus::Color((BYTE)(c >> 24), (BYTE)(c >> 16), (BYTE)(c >> 8), (BYTE)c); };
    Gdiplus::GraphicsPath path;
    float fx = (float)x, fy = (float)y, fw = (float)std::max(20, w), fh = (float)std::max(16, h);
    float cx = fx + fw / 2, cy = fy + fh / 2;
    float ang = std::atan2((float)ty - cy, (float)tx - cx);
    float bw = std::min(fw, fh) * 0.18f;
    Gdiplus::PointF tail[3] = {{cx + std::cos(ang + 1.5708f) * bw, cy + std::sin(ang + 1.5708f) * bw},
                               {(float)tx, (float)ty},
                               {cx + std::cos(ang - 1.5708f) * bw, cy + std::sin(ang - 1.5708f) * bw}};
    if (st.shape == 3) {   // shout: spiky outline
        std::vector<Gdiplus::PointF> pts;
        int spikes = 14;
        for (int i = 0; i < spikes * 2; ++i) {
            float a = 6.28318f * i / (spikes * 2);
            float rr = (i % 2) ? 0.78f : 1.0f;
            pts.push_back({cx + std::cos(a) * fw / 2 * rr, cy + std::sin(a) * fh / 2 * rr});
        }
        path.AddPolygon(pts.data(), (int)pts.size());
    } else if (st.shape == 1 || st.shape == 2) {
        path.AddEllipse(fx, fy, fw, fh);
    } else {
        float r = std::min(fw, fh) * 0.3f;
        path.AddArc(fx, fy, r, r, 180, 90);
        path.AddArc(fx + fw - r, fy, r, r, 270, 90);
        path.AddArc(fx + fw - r, fy + fh - r, r, r, 0, 90);
        path.AddArc(fx, fy + fh - r, r, r, 90, 90);
        path.CloseFigure();
    }
    Gdiplus::SolidBrush fill(C(st.fill));
    Gdiplus::Pen pen(C(st.stroke), std::max(2.0f, std::min(fw, fh) / 40));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    if (st.shape == 2) {   // thought bubble: small circles toward the tail point
        for (int i = 1; i <= 3; ++i) {
            float k = 1 - i / 4.0f, rr = bw * (1.0f - i * 0.25f);
            float px = tx + (cx - tx) * k * 0.55f, py = ty + (cy - ty) * k * 0.55f;
            g.FillEllipse(&fill, px - rr, py - rr, rr * 2, rr * 2);
            g.DrawEllipse(&pen, px - rr, py - rr, rr * 2, rr * 2);
        }
    } else {
        Gdiplus::GraphicsPath tp;
        tp.AddPolygon(tail, 3);
        g.FillPath(&fill, &tp);
        g.DrawPath(&pen, &tp);
    }
    g.FillPath(&fill, &path);
    g.DrawPath(&pen, &path);
    if (st.shape != 2) {   // cover the seam between tail and body
        Gdiplus::PointF seam[3] = {tail[0], {cx + std::cos(ang) * bw * 1.2f, cy + std::sin(ang) * bw * 1.2f}, tail[2]};
        Gdiplus::GraphicsPath sp;
        sp.AddPolygon(seam, 3);
        g.FillPath(&fill, &sp);
    }
    Gdiplus::FontFamily fam(L"Malgun Gothic");
    Gdiplus::Font font(&fam, (Gdiplus::REAL)st.fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush tb(C(st.textColor));
    Gdiplus::RectF tr(fx + fw * 0.12f, fy + fh * 0.12f, fw * 0.76f, fh * 0.76f);
    g.DrawString(text.c_str(), -1, &font, tr, &fmt, &tb);
    g.Flush(Gdiplus::FlushIntentionSync);
    for (auto& c : o->px) c |= 0xFF000000u;
    return o;
}

ImagePtr drawStrokes(const Image& src, const std::vector<Stroke>& strokes) {
    auto o = std::make_shared<Image>(src);
    Gdiplus::Bitmap bmp(o->w, o->h, o->w * 4, PixelFormat32bppARGB, (BYTE*)o->px.data());
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for (auto& s : strokes) {
        if (s.pts.empty()) continue;
        BYTE a = s.highlighter ? 102 : 255;
        Gdiplus::Color col(a, (BYTE)(s.color >> 16), (BYTE)(s.color >> 8), (BYTE)s.color);
        if (s.pts.size() == 1) {
            Gdiplus::SolidBrush b(col);
            g.FillEllipse(&b, s.pts[0].x - s.width / 2, s.pts[0].y - s.width / 2, s.width, s.width);
            continue;
        }
        Gdiplus::Pen pen(col, s.width);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        std::vector<Gdiplus::Point> p;
        for (auto& q : s.pts) p.push_back(Gdiplus::Point((INT)q.x, (INT)q.y));
        g.DrawLines(&pen, p.data(), (INT)p.size());
    }
    g.Flush(Gdiplus::FlushIntentionSync);
    for (auto& c : o->px) c |= 0xFF000000u;
    return o;
}

ImagePtr drawSticker(const Image& src, const std::wstring& emoji, int x, int y, int size) {
    // Direct2D on a WIC bitmap so colour emoji glyphs render in colour
    ComScope com;
    auto o = std::make_shared<Image>(src);
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return o;
    ComPtr<IWICBitmap> wb;
    if (FAILED(wic->CreateBitmapFromMemory(o->w, o->h, GUID_WICPixelFormat32bppPBGRA, o->w * 4, (UINT)o->bytes(), (BYTE*)o->px.data(), &wb))) return o;
    ComPtr<ID2D1Factory> f;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, f.GetAddressOf());
    ComPtr<IDWriteFactory> dw;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dw.GetAddressOf());
    if (!f || !dw) return o;
    ComPtr<ID2D1RenderTarget> rt;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(f->CreateWicBitmapRenderTarget(wb.Get(), props, &rt))) return o;
    ComPtr<IDWriteTextFormat> tf;
    dw->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, (float)size, L"ko-kr", &tf);
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &br);
    rt->BeginDraw();
    rt->DrawText(emoji.c_str(), (UINT32)emoji.size(), tf.Get(), D2D1::RectF((float)x, (float)y, (float)x + size * 4.f, (float)y + size * 2.f), br.Get(),
                 D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    rt->EndDraw();
    ComPtr<IWICBitmapLock> lk;
    WICRect r{0, 0, o->w, o->h};
    if (SUCCEEDED(wb->Lock(&r, WICBitmapLockRead, &lk))) {
        UINT sz = 0, stride = 0;
        BYTE* p = nullptr;
        lk->GetDataPointer(&sz, &p);
        lk->GetStride(&stride);
        for (int yy = 0; yy < o->h; ++yy) memcpy(o->row(yy), p + (size_t)yy * stride, (size_t)o->w * 4);
    }
    for (auto& c : o->px) c |= 0xFF000000u;
    return o;
}

// ------------------------------------------------------------------ text (GDI+)
ImagePtr drawText(const Image& src, const std::wstring& text, int x, int y, const TextStyle& st) {
    auto o = std::make_shared<Image>(src);
    Gdiplus::Bitmap bmp(o->w, o->h, o->w * 4, PixelFormat32bppARGB, (BYTE*)o->px.data());
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::FontFamily fam(st.font.c_str());
    Gdiplus::GraphicsPath path;
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    path.AddString(text.c_str(), -1, fam.IsAvailable() ? &fam : Gdiplus::FontFamily::GenericSansSerif(),
                   st.bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, (Gdiplus::REAL)st.size,
                   Gdiplus::PointF((Gdiplus::REAL)x, (Gdiplus::REAL)y), &fmt);
    if (st.outline) {
        Gdiplus::Pen pen(Gdiplus::Color(255, 0, 0, 0), std::max(2.0f, st.size / 8.0f));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&pen, &path);
    }
    Gdiplus::SolidBrush br(Gdiplus::Color(255, (BYTE)R(st.color), (BYTE)G(st.color), (BYTE)B(st.color)));
    g.FillPath(&br, &path);
    g.Flush(Gdiplus::FlushIntentionSync);
    for (auto& c : o->px) c |= 0xFF000000u;
    return o;
}

} // namespace bg
