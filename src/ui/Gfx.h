#pragma once
// Direct2D / DirectWrite drawing for one window (hardware render target, DIP coordinates).
#include "core/Image.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <map>
#include <string>
#include <tuple>

namespace bg {

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    float r() const { return x + w; }
    float b() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    RectF inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    RectF inset(float d) const { return inset(d, d); }
    D2D1_RECT_F d2d() const { return D2D1::RectF(x, y, x + w, y + h); }
};

enum Align { A_LEFT = 0, A_CENTER = 1, A_RIGHT = 2 };

class Gfx {
public:
    ~Gfx();
    bool attach(HWND h);
    void resize();
    bool begin();                 // false if no target
    void end();                   // handles device loss
    float scale() const { return s_; }
    HWND hwnd() const { return hwnd_; }
    float width() const { return w_; }
    float height() const { return h_; }

    void clear(uint32_t argb);
    void fill(RectF r, float radius, uint32_t c);
    void fillV(RectF r, float radius, uint32_t top, uint32_t bottom);
    void fillH(RectF r, float radius, uint32_t left, uint32_t right);
    void stroke(RectF r, float radius, uint32_t c, float w = 1);
    void strokeV(RectF r, float radius, uint32_t top, uint32_t bottom, float w = 1);
    void strokeH(RectF r, float radius, uint32_t left, uint32_t right, float w = 1);
    void line(float x1, float y1, float x2, float y2, uint32_t c, float w = 1);
    void dashRect(RectF r, uint32_t c, float w = 1);
    void circle(float cx, float cy, float rad, uint32_t c);
    void ring(float cx, float cy, float rad, uint32_t c, float w);
    void glow(float cx, float cy, float rad, uint32_t c);   // radial colour -> transparent (2 stops)
    void shadow(RectF r, float radius);                      // soft, wide, low opacity
    void text(const std::wstring& t, RectF r, uint32_t c, float size, int weight = 400, Align a = A_LEFT,
              bool vcenter = true, bool wrap = false, bool display = false);
    float textWidth(const std::wstring& t, float size, int weight = 400, bool display = false);
    float textHeight(const std::wstring& t, float size, float maxW, int weight = 400);
    void icon(wchar_t glyph, RectF r, uint32_t c, float size);
    void image(const ImagePtr& im, RectF dst, bool smooth, float opacity = 1.0f);   // cached by pointer identity
    void pushClip(RectF r);
    void popClip();
    void setOffset(float dx, float dy);

private:
    void createTarget();
    ID2D1SolidColorBrush* brush(uint32_t c);
    ID2D1GradientStopCollection* stops(uint32_t a, uint32_t b);
    IDWriteTextFormat* format(float size, int weight, bool display, bool icon);
    void geometry(RectF r, float radius, ID2D1Brush* b, bool fillIt, float w);

    HWND hwnd_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    float s_ = 1, w_ = 0, h_ = 0;
    std::map<uint32_t, ID2D1SolidColorBrush*> brushes_;
    std::map<std::pair<uint32_t, uint32_t>, ID2D1GradientStopCollection*> stops_;
    std::map<std::tuple<int, int, bool, bool>, IDWriteTextFormat*> formats_;
    ID2D1Bitmap* bmp_ = nullptr;
    ImagePtr bmpSrc_;
    int bmpW_ = 0, bmpH_ = 0;
};

ID2D1Factory* d2dFactory();
IDWriteFactory* dwFactory();
const wchar_t* iconFontName();

} // namespace bg
