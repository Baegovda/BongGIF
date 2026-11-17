#include "ui/Gfx.h"

#include "ui/Lang.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace bg {

template <class T>
static void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

ID2D1Factory* d2dFactory() {
    static ID2D1Factory* f = [] {
        ID2D1Factory* p = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &p);
        return p;
    }();
    return f;
}
IDWriteFactory* dwFactory() {
    static IDWriteFactory* f = [] {
        IDWriteFactory* p = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&p);
        return p;
    }();
    return f;
}
const wchar_t* iconFontName() {
    static const wchar_t* name = [] {
        IDWriteFontCollection* fc = nullptr;
        dwFactory()->GetSystemFontCollection(&fc);
        UINT32 idx;
        BOOL exists = FALSE;
        if (fc) fc->FindFamilyName(th::fontIcons, &idx, &exists);
        if (fc) fc->Release();
        return exists ? th::fontIcons : th::fontIconsFallback;
    }();
    return name;
}

static D2D1_COLOR_F col(uint32_t c) {
    return D2D1::ColorF(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, ((c >> 24) & 255) / 255.f);
}

Gfx::~Gfx() {
    for (auto& [k, b] : brushes_) b->Release();
    for (auto& [k, s] : stops_) s->Release();
    for (auto& [k, f] : formats_) f->Release();
    release(bmp_);
    release(rt_);
}

bool Gfx::attach(HWND h) {
    hwnd_ = h;
    createTarget();
    return rt_ != nullptr;
}

static UINT windowDpi(HWND h) {
    static UINT forced = [] {
        wchar_t b[16];
        return GetEnvironmentVariableW(L"BONGGIF_TEST_DPI", b, 16) ? (UINT)_wtoi(b) : 0u;
    }();
    return forced ? forced : GetDpiForWindow(h);
}

void Gfx::createTarget() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    UINT dpi = windowDpi(hwnd_);
    s_ = dpi / 96.0f;
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                                   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                                                   (float)dpi, (float)dpi);
    d2dFactory()->CreateHwndRenderTarget(p, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom))), &rt_);
    if (rt_) rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

void Gfx::resize() {
    if (!rt_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    UINT dpi = windowDpi(hwnd_);
    if (dpi / 96.0f != s_) {   // DPI changed: rebuild everything tied to the target
        for (auto& [k, b] : brushes_) b->Release();
        brushes_.clear();
        for (auto& [k, s] : stops_) s->Release();
        stops_.clear();
        release(bmp_);
        bmpSrc_.reset();
        release(rt_);
        createTarget();
        return;
    }
    rt_->Resize(D2D1::SizeU(std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom)));
}

bool Gfx::begin() {
    if (!rt_) createTarget();
    if (!rt_) return false;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    w_ = rc.right / s_;
    h_ = rc.bottom / s_;
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

void Gfx::end() {
    if (!rt_) return;
    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        for (auto& [k, b] : brushes_) b->Release();
        brushes_.clear();
        for (auto& [k, s] : stops_) s->Release();
        stops_.clear();
        release(bmp_);
        bmpSrc_.reset();
        release(rt_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

ID2D1SolidColorBrush* Gfx::brush(uint32_t c) {
    auto it = brushes_.find(c);
    if (it != brushes_.end()) return it->second;
    ID2D1SolidColorBrush* b = nullptr;
    rt_->CreateSolidColorBrush(col(c), &b);
    if (brushes_.size() > 256) { for (auto& [k, v] : brushes_) v->Release(); brushes_.clear(); }
    brushes_[c] = b;
    return b;
}

ID2D1GradientStopCollection* Gfx::stops(uint32_t a, uint32_t b) {
    auto key = std::make_pair(a, b);
    auto it = stops_.find(key);
    if (it != stops_.end()) return it->second;
    D2D1_GRADIENT_STOP st[2] = {{0.f, col(a)}, {1.f, col(b)}};
    ID2D1GradientStopCollection* s = nullptr;
    rt_->CreateGradientStopCollection(st, 2, &s);
    stops_[key] = s;
    return s;
}

IDWriteTextFormat* Gfx::format(float size, int weight, bool display, bool icon) {
    auto key = std::make_tuple((int)(size * 10), weight, display, icon);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    const wchar_t* fam = icon ? iconFontName() : (display ? th::fontDisplay : th::fontBody);
    dwFactory()->CreateTextFormat(fam, nullptr, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ko-kr", &f);
    formats_[key] = f;
    return f;
}

void Gfx::geometry(RectF r, float radius, ID2D1Brush* b, bool fillIt, float w) {
    if (r.w <= 0 || r.h <= 0) return;
    if (fillIt) {
        if (radius > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r.d2d(), radius, radius), b);
        else rt_->FillRectangle(r.d2d(), b);
    } else {
        RectF q = r.inset(w / 2);
        if (radius > 0) rt_->DrawRoundedRectangle(D2D1::RoundedRect(q.d2d(), std::max(0.f, radius - w / 2), std::max(0.f, radius - w / 2)), b, w);
        else rt_->DrawRectangle(q.d2d(), b, w);
    }
}

void Gfx::clear(uint32_t c) { rt_->Clear(col(c)); }
void Gfx::fill(RectF r, float radius, uint32_t c) { geometry(r, radius, brush(c), true, 0); }
void Gfx::stroke(RectF r, float radius, uint32_t c, float w) { geometry(r, radius, brush(c), false, w); }

void Gfx::fillV(RectF r, float radius, uint32_t a, uint32_t b) {
    ID2D1LinearGradientBrush* g = nullptr;
    rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.b())), stops(a, b), &g);
    if (g) { geometry(r, radius, g, true, 0); g->Release(); }
}
void Gfx::fillH(RectF r, float radius, uint32_t a, uint32_t b) {
    ID2D1LinearGradientBrush* g = nullptr;
    rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.r(), r.y)), stops(a, b), &g);
    if (g) { geometry(r, radius, g, true, 0); g->Release(); }
}
void Gfx::strokeV(RectF r, float radius, uint32_t a, uint32_t b, float w) {
    ID2D1LinearGradientBrush* g = nullptr;
    rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.b())), stops(a, b), &g);
    if (g) { geometry(r, radius, g, false, w); g->Release(); }
}
void Gfx::strokeH(RectF r, float radius, uint32_t a, uint32_t b, float w) {
    ID2D1LinearGradientBrush* g = nullptr;
    rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.r(), r.y)), stops(a, b), &g);
    if (g) { geometry(r, radius, g, false, w); g->Release(); }
}
void Gfx::line(float x1, float y1, float x2, float y2, uint32_t c, float w) {
    rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush(c), w);
}
void Gfx::dashRect(RectF r, uint32_t c, float w) {
    static ID2D1StrokeStyle* ss = [] {
        ID2D1StrokeStyle* s = nullptr;
        float dashes[] = {4, 3};
        d2dFactory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                                   D2D1_LINE_JOIN_MITER, 10, D2D1_DASH_STYLE_CUSTOM, 0),
                                        dashes, 2, &s);
        return s;
    }();
    rt_->DrawRectangle(r.d2d(), brush(0xA0000000), w + 1);
    rt_->DrawRectangle(r.d2d(), brush(c), w, ss);
}
void Gfx::circle(float cx, float cy, float rad, uint32_t c) {
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush(c));
}
void Gfx::ring(float cx, float cy, float rad, uint32_t c, float w) {
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush(c), w);
}
void Gfx::glow(float cx, float cy, float rad, uint32_t c) {
    ID2D1RadialGradientBrush* g = nullptr;
    rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rad, rad),
                                   stops(c, c & 0x00FFFFFF), &g);
    if (g) { rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), g); g->Release(); }
}
void Gfx::shadow(RectF r, float radius) {
    // soft & wide: a few expanding low-alpha layers approximating a blur of th::shadowBlur
    const int layers = 6;
    for (int i = layers; i >= 1; --i) {
        float e = th::shadowBlur * i / layers;
        int a = (int)(255 * th::shadowOpacity / layers * 0.9f);
        RectF q{r.x - e * 0.5f, r.y - e * 0.5f + th::shadowOffsetY, r.w + e, r.h + e};
        fill(q, radius + e * 0.5f, ((uint32_t)a << 24) | th::shadow);
    }
}

void Gfx::text(const std::wstring& t0, RectF r, uint32_t c, float size, int weight, Align a, bool vcenter, bool wrap, bool display) {
    const std::wstring t = tr(t0);
    if (t.empty() || r.w <= 0) return;
    IDWriteTextFormat* f = format(size, weight, display, false);
    IDWriteTextLayout* lay = nullptr;
    dwFactory()->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, r.w, std::max(1.f, r.h), &lay);
    if (!lay) return;
    lay->SetTextAlignment(a == A_LEFT ? DWRITE_TEXT_ALIGNMENT_LEADING : a == A_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
    lay->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    lay->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {
        DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        IDWriteInlineObject* ell = nullptr;
        dwFactory()->CreateEllipsisTrimmingSign(f, &ell);
        lay->SetTrimming(&tr, ell);
        if (ell) ell->Release();
    }
    rt_->DrawTextLayout(D2D1::Point2F(r.x, r.y), lay, brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    lay->Release();
}

float Gfx::textWidth(const std::wstring& t0, float size, int weight, bool display) {
    const std::wstring t = tr(t0);
    IDWriteTextLayout* lay = nullptr;
    dwFactory()->CreateTextLayout(t.c_str(), (UINT32)t.size(), format(size, weight, display, false), 10000, 100, &lay);
    if (!lay) return 0;
    DWRITE_TEXT_METRICS m;
    lay->GetMetrics(&m);
    lay->Release();
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::textHeight(const std::wstring& t0, float size, float maxW, int weight) {
    const std::wstring t = tr(t0);
    IDWriteTextLayout* lay = nullptr;
    dwFactory()->CreateTextLayout(t.c_str(), (UINT32)t.size(), format(size, weight, false, false), std::max(1.f, maxW), 10000, &lay);
    if (!lay) return size * 1.4f;
    DWRITE_TEXT_METRICS m;
    lay->GetMetrics(&m);
    lay->Release();
    return m.height;
}

void Gfx::icon(wchar_t glyph, RectF r, uint32_t c, float size) {
    wchar_t s[2] = {glyph, 0};
    IDWriteTextFormat* f = format(size, 400, false, true);
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt_->DrawText(s, 1, f, r.d2d(), brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Gfx::image(const ImagePtr& ip, RectF dst, bool smooth, float opacity) {
    if (!ip || ip->w <= 0 || ip->h <= 0) return;
    const Image& im = *ip;
    if (!bmp_ || bmpW_ != im.w || bmpH_ != im.h) {
        release(bmp_);
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        rt_->CreateBitmap(D2D1::SizeU(im.w, im.h), im.px.data(), im.w * 4, bp, &bmp_);
        bmpW_ = im.w; bmpH_ = im.h;
        bmpSrc_ = ip;
    } else if (bmpSrc_ != ip) {
        bmp_->CopyFromMemory(nullptr, im.px.data(), im.w * 4);
        bmpSrc_ = ip;
    }
    if (bmp_) rt_->DrawBitmap(bmp_, dst.d2d(), opacity, smooth ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void Gfx::pushClip(RectF r) { rt_->PushAxisAlignedClip(r.d2d(), D2D1_ANTIALIAS_MODE_ALIASED); }
void Gfx::popClip() { rt_->PopAxisAlignedClip(); }
void Gfx::setOffset(float dx, float dy) { rt_->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy)); }

} // namespace bg
