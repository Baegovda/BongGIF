#pragma once
#include "core/Image.h"

#include <string>
#include <vector>

namespace bg {

struct EffectParam {
    const wchar_t* label;
    int lo, hi, def;
    bool toggle = false;    // lo/hi = 0/1 shown as a switch
};
enum EffectNeeds { EN_NONE = 0, EN_RECT = 1, EN_POINT = 2, EN_COLOR = 4, EN_RECT_OPTIONAL = 8 };
struct EffectDef {
    const wchar_t* name;
    std::vector<EffectParam> params;
    int needs = EN_RECT_OPTIONAL;   // what the canvas interaction supplies
    const wchar_t* hint = L"";
};
const std::vector<EffectDef>& effectList();

struct EffectCtx {
    int rx = 0, ry = 0, rw = 0, rh = 0;   // rectangle (image space), w<=0 = none
    int px = -1, py = -1;                 // point (image space), -1 = centre
    uint32_t color = 0xFF00FF00;          // key colour (chroma key)
};
ImagePtr applyEffect(int index, const Image& src, const std::vector<int>& p, const EffectCtx& ctx = {});

// building blocks also used elsewhere (colour compression pre-filter, cartoon, denoise)
void meanShiftFilter(Image& im, int sp, int sr, int maxIter = 5);
void inpaint(Image& im, const std::vector<uint8_t>& mask, int radius = 3);
void boxBlur(Image& im, int r);

ImagePtr overlay(const Image& src, const Image& over, int x, int y, double opacity);

} // namespace bg
