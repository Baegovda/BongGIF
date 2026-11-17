// Extra edit panels: collage, insert (speech bubble / sticker), drawing, zoom animation
// (FxCam), photo frame — plus their previews and canvas overlays.
#include "ui/App.h"

#include "core/Apng.h"
#include "core/Compose.h"
#include "core/Gif.h"
#include "core/Psd.h"
#include "core/SuperRes.h"
#include "platform/Codecs.h"
#include "platform/Media.h"
#include "platform/Webp.h"

#include <algorithm>
#include <cmath>
#include <commdlg.h>

namespace bg {

static const wchar_t* kStickers[] = {L"😀", L"😂", L"🥹", L"😍", L"😎", L"🤔", L"😮", L"😭", L"😡", L"👍", L"👏", L"🙏",
                                     L"❤️", L"🔥", L"✨", L"🎉", L"💯", L"⭐", L"⚡", L"💡", L"✅", L"❌", L"⚠️", L"👉"};
static const int kNumStickers = 24;
static const uint32_t kSwatches[] = {0xFFFFFFFF, 0xFFFFD84D, 0xFFFF9F43, 0xFFFF5D5D, 0xFFFF7AB8, 0xFFA98BFF, 0xFF31D0E0, 0xFF3DDC97, 0xFF000000};

// Loads any supported animation/image file for the collage list (same decoders as "open").
static bool loadAny(const std::wstring& p, Frames& out, int frameMs, std::string& e) {
    std::vector<uint8_t> data;
    if (!readFile(p, data)) { e = "파일을 읽을 수 없어요"; return false; }
    if (data.size() > 6 && !memcmp(data.data(), "GIF", 3)) return decodeGif(data, out, e);
    if (data.size() > 12 && !memcmp(data.data(), "RIFF", 4) && !memcmp(data.data() + 8, "WEBP", 4)) return decodeWebp(data, out, e);
    if (data.size() > 8 && data[0] == 137 && data[1] == 'P' && decodePng(data, out, e)) return true;
    if (data.size() > 12 && !memcmp(data.data() + 4, "ftyp", 4) && (!memcmp(data.data() + 8, "avif", 4) || !memcmp(data.data() + 8, "avis", 4)))
        return decodeAvif(data, out, e);
    out.clear();
    return loadFramesWic(p, out, frameMs, e);
}

float App::swatchRow(float x, float y, uint32_t& value, Id base) {
    Gfx& g = gui_.g;
    for (int i = 0; i < 9; ++i) {
        RectF s{x + i * 23, y, 20, 20};
        if (gui_.interact(gid(base, i), s)) value = kSwatches[i];
        g.fill(s, 10, kSwatches[i]);
        bool on = (value | 0xFF000000u) == kSwatches[i];
        g.stroke(s, 10, on ? th::accent : th::separator, on ? 2.f : 1.f);
    }
    return 9 * 23;
}

float App::toolBody2(int tool, float ix, float yy, float iw) {
    Gfx& g = gui_.g;
    auto row = [&](const std::wstring& t, const std::wstring& d, float cw, float ch, bool en = true, bool sep = true) {
        return gui_.optionRow(ix, yy, iw, t, d, cw, ch, en, sep);
    };
    auto hint = [&](const std::wstring& t) {
        float hh = g.textHeight(t, th::caption, iw);
        g.text(t, {ix, yy, iw, hh + 2}, th::textSecondary, th::caption, 400, A_LEFT, false, true);
        yy += hh + 8;
    };
    auto applyBtn = [&](const std::wstring& t, std::function<void()> fn, bool en = true) {
        yy += 8;
        if (gui_.button(gid(gid("apply2"), tool), {ix + iw - 120, yy, 120, 34}, t, BS_PRIMARY, ic::Check, en)) gui_.later(std::move(fn));
        if (gui_.button(gid(gid("close2"), tool), {ix + iw - 216, yy, 88, 34}, L"닫기", BS_SECONDARY)) gui_.later([this] { setTool(T_NONE); });
        yy += 40;
    };
    const Image& im0 = *proj.frames[0].img;
    switch (tool) {
    case T_COLLAGE: {
        hint(L"지금 움짤과 다른 파일을 나란히 붙여요. 짧은 움짤은 반복돼요.");
        for (size_t i = 0; i < collageNames_.size(); ++i) {
            RectF c = row(L"" + std::to_wstring(i + 2) + L". " + collageNames_[i], std::to_wstring(collageAnims_[i].size()) + L"프레임", 34, 30);
            if (gui_.iconButton(gid(gid("cl.del"), i), c, ic::Delete)) {
                size_t k = i;
                gui_.later([this, k] { collageAnims_.erase(collageAnims_.begin() + k); collageNames_.erase(collageNames_.begin() + k); gui_.redraw(); });
            }
        }
        RectF c = row(L"붙일 파일", L"GIF·WebP·APNG·AVIF·이미지", 110, 30);
        if (gui_.button(gid("cl.add"), c, L"추가", BS_SECONDARY, ic::Add, collageNames_.size() < 8)) {
            gui_.later([this] {
                wchar_t buf[MAX_PATH] = {};
                OPENFILENAMEW of{sizeof of};
                of.hwndOwner = hwnd;
                of.lpstrFilter = L"움짤·이미지\0*.gif;*.webp;*.png;*.apng;*.avif;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0";
                of.lpstrFile = buf;
                of.nMaxFile = MAX_PATH;
                of.Flags = OFN_FILEMUSTEXIST;
                if (!GetOpenFileNameW(&of)) return;
                Frames f;
                std::string e;
                std::wstring p = buf;
                bool ok = runWithProgress(hwnd, L"파일을 읽는 중", [&](const std::function<void(double)>&, const std::function<bool()>&) -> std::string {
                    return loadAny(p, f, st.importFrameMs, e) ? "" : e;
                });
                if (!ok || f.empty()) return;
                collageAnims_.push_back(std::move(f));
                collageNames_.push_back(p.substr(p.find_last_of(L"\\/") + 1));
                gui_.redraw();
            });
        }
        c = row(L"배치", L"", 200, 30);
        gui_.segmented(gid("cl.layout"), c, {L"가로", L"세로", L"격자"}, collageLayout_);
        c = row(L"간격 (px)", L"", 150, 30);
        gui_.number(gid("cl.gap"), c, collageGap_, 0, 100);
        c = row(L"바탕색", L"", 9 * 23, 20);
        swatchRow(c.x, c.y, collageBg_, gid("cl.bg"));
        applyBtn(L"콜라주 만들기", [this] {
            std::vector<Frames> all{proj.frames};
            all.insert(all.end(), collageAnims_.begin(), collageAnims_.end());
            int cw = proj.frames[0].img->w, ch = proj.frames[0].img->h, layout = collageLayout_, gap = collageGap_;
            uint32_t bg = collageBg_;
            runBg(L"콜라주를 만드는 중", [=](const Progress&, const Cancel&) { return collage(all, layout, cw, ch, gap, bg); }, L"콜라주를 만들었어요");
            collageAnims_.clear();
            collageNames_.clear();
            setTool(T_NONE);
        }, !collageAnims_.empty());
        break;
    }
    case T_INSERT: {
        RectF c = row(L"넣을 것", L"", 200, 30);
        gui_.segmented(gid("in.kind"), c, {L"말풍선", L"스티커"}, insertKind_);
        if (insertKind_ == 0) {
            hint(L"미리보기에서 말풍선 자리를 드래그해요.");
            c = row(L"모양", L"", 220, 30);
            gui_.segmented(gid("in.shape"), c, {L"둥근", L"타원", L"생각", L"외침"}, bubbleShape_);
            c = row(L"글자", L"", iw * 0.6f, 30);
            gui_.edit(gid("in.text"), c, bubbleText_);
            c = row(L"글자 크기", L"", 150, 30);
            gui_.number(gid("in.size"), c, bubbleSize_, 8, 200);
            c = row(L"꼬리 방향", L"", 220, 30);
            gui_.segmented(gid("in.tail"), c, {L"↙", L"↘", L"↖", L"↗"}, tailDir_);
            c = row(L"말풍선 색", L"", 9 * 23, 20);
            swatchRow(c.x, c.y, bubbleFill_, gid("in.fill"));
        } else {
            hint(L"스티커를 고르고 미리보기를 누르면 그 자리에 붙어요.");
            float sx = ix, sy = yy;
            for (int i = 0; i < kNumStickers; ++i) {
                RectF s{sx + (i % 8) * 34.f, sy + (i / 8) * 36.f, 32, 32};
                Id sid = gid(gid("in.st"), i);
                if (gui_.interact(sid, s)) stickerIdx_ = i;
                float t = gui_.hoverT(sid);
                if (stickerIdx_ == i) g.fill(s, 6, th::control);
                else if (t > 0) g.fill(s, 6, (th::control & 0x00FFFFFF) | ((uint32_t)(0x26 * t) << 24));
                g.text(kStickers[i], s, th::textPrimary, 20, 400, A_CENTER);
            }
            yy += 36 * 3 + 6;
            c = row(L"크기", L"", 170, 30);
            gui_.slider(gid("in.ssize"), {c.x, c.y, c.w - 44, c.h}, stickerSize_, 16, 400);
            g.text(std::to_wstring(stickerSize_), {c.r() - 40, c.y, 40, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        }
        c = row(L"적용 범위", L"", 128, 30);
        gui_.segmented(gid("scope"), c, {L"전체", L"선택"}, scope_);
        applyBtn(L"넣기", [this] {
            auto fn = toolFn();
            if (!fn) return;
            Frames src = proj.frames;
            auto sel = scopeSel();
            setTool(T_NONE);
            runBg(L"넣는 중", [=](const Progress& p, const Cancel&) { return mapImages(src, fn, sel, p); }, L"넣었어요");
        }, insertKind_ == 1 || selRect_.w > 8);
        break;
    }
    case T_DRAW: {
        hint(L"미리보기 위에 마우스로 그려요.");
        RectF c = row(L"펜 색", L"", 9 * 23, 20);
        swatchRow(c.x, c.y, penColor_, gid("dr.col"));
        c = row(L"굵기", L"", 170, 30);
        gui_.slider(gid("dr.w"), {c.x, c.y, c.w - 40, c.h}, penWidth_, 1, 60);
        g.text(std::to_wstring(penWidth_), {c.r() - 36, c.y, 36, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        c = row(L"형광펜", L"반투명하게 칠해요", 40, 20);
        gui_.toggle(gid("dr.hl"), c, highlighter_);
        c = row(L"그린 선", std::to_wstring(strokes_.size()) + L"개", 170, 30);
        if (gui_.button(gid("dr.undo"), {c.x, c.y, 80, c.h}, L"하나 빼기", BS_SECONDARY, 0, !strokes_.empty())) gui_.later([this] { if (!strokes_.empty()) strokes_.pop_back(); lastSig_.clear(); gui_.redraw(); });
        if (gui_.button(gid("dr.clear"), {c.x + 86, c.y, 84, c.h}, L"모두 지우기", BS_SECONDARY, 0, !strokes_.empty())) gui_.later([this] { strokes_.clear(); lastSig_.clear(); gui_.redraw(); });
        c = row(L"그릴 프레임", L"", 200, 30);
        gui_.segmented(gid("dr.scope"), c, {L"지금", L"선택", L"전체"}, drawScope_);
        applyBtn(L"그리기 적용", [this] {
            auto fn = toolFn();
            Frames src = proj.frames;
            std::vector<int> sel;
            if (drawScope_ == 0) sel = {cur_};
            else if (drawScope_ == 1) sel = selection();
            strokes_.clear();
            setTool(T_NONE);
            runBg(L"그리는 중", [=](const Progress& p, const Cancel&) { return mapImages(src, fn, sel, p); }, L"그림을 넣었어요");
        }, !strokes_.empty());
        break;
    }
    case T_ZOOM: {
        hint(L"미리보기에서 확대할 곳을 드래그해요. 지금 프레임부터 움직여요.");
        RectF c = row(L"확대하는 프레임 수", L"", 150, 30);
        gui_.number(gid("zm.in"), c, zIn_, 1, 300);
        c = row(L"머무는 프레임 수", L"", 150, 30);
        gui_.number(gid("zm.hold"), c, zHold_, 0, 300);
        c = row(L"다시 축소하는 프레임 수", L"0이면 확대된 채로 끝나요", 150, 30);
        gui_.number(gid("zm.out"), c, zOut_, 0, 300);
        applyBtn(L"줌 넣기", [this] {
            Frames src = proj.frames;
            int s0 = cur_, a = zIn_, h = zHold_, o = zOut_;
            RectF r = selRect_;
            int64_t fu = src[cur_].dur > 0 ? src[cur_].dur : 66666;
            setTool(T_NONE);
            runBg(L"줌 애니메이션을 만드는 중",
                  [=](const Progress&, const Cancel&) { return zoomAnimation(src, s0, (int)r.x, (int)r.y, (int)r.w, (int)r.h, a, h, o, fu); },
                  L"줌 애니메이션을 넣었어요");
        }, selRect_.w > 8 && selRect_.h > 8);
        break;
    }
    case T_FRAME: {
        RectF c = row(L"모양", L"", 200, 30);
        gui_.dropdown(gid("fr.style"), c, {L"테두리", L"둥근 모서리", L"폴라로이드", L"필름", L"그림자 액자", L"부드러운 가장자리"}, frameStyle_);
        c = row(L"두께", L"", 170, 30);
        gui_.slider(gid("fr.t"), {c.x, c.y, c.w - 40, c.h}, frameThick_, 2, 120);
        g.text(std::to_wstring(frameThick_), {c.r() - 36, c.y, 36, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        c = row(L"색", L"", 9 * 23, 20, frameStyle_ != FS_FILM);
        swatchRow(c.x, c.y, frameColor_, gid("fr.col"));
        applyBtn(L"액자 적용", [this] {
            auto fn = toolFn();
            Frames src = proj.frames;
            setTool(T_NONE);
            runBg(L"액자를 씌우는 중", [=](const Progress& p, const Cancel&) { return mapImages(src, fn, {}, p); }, L"액자를 씌웠어요");
        });
        break;
    }
    }
    (void)im0;
    return yy;
}

std::function<ImagePtr(const Image&)> App::toolFn2() {
    switch (tool_) {
    case T_INSERT: {
        if (insertKind_ == 1) {
            std::wstring e = kStickers[std::clamp(stickerIdx_, 0, kNumStickers - 1)];
            int x = textX_, y = textY_, s = stickerSize_;
            return [=](const Image& im) { return drawSticker(im, e, x - s / 2, y - s / 2, s); };
        }
        if (selRect_.w <= 8) return nullptr;
        BubbleStyle bs;
        bs.shape = bubbleShape_;
        bs.fontSize = bubbleSize_;
        bs.fill = bubbleFill_;
        bool dark = R(bubbleFill_) * 0.299 + G(bubbleFill_) * 0.587 + B(bubbleFill_) * 0.114 < 128;
        bs.textColor = dark ? 0xFFFFFFFF : 0xFF202020;
        bs.stroke = dark ? 0xFFFFFFFF : 0xFF202020;
        RectF r = selRect_;
        float tx = (tailDir_ == 1 || tailDir_ == 3) ? r.r() - r.w * 0.2f : r.x + r.w * 0.2f;
        float ty = tailDir_ < 2 ? r.b() + r.h * 0.45f : r.y - r.h * 0.45f;
        std::wstring text = bubbleText_;
        return [=](const Image& im) { return drawBubble(im, (int)r.x, (int)r.y, (int)r.w, (int)r.h, (int)tx, (int)ty, text, bs); };
    }
    case T_DRAW: {
        auto s = strokes_;
        return [=](const Image& im) { return drawStrokes(im, s); };
    }
    case T_FRAME: {
        int st2 = frameStyle_, t = frameThick_;
        uint32_t c = frameColor_ | 0xFF000000u;
        return [=](const Image& im) { return photoFrame(im, st2, t, c); };
    }
    default: return nullptr;
    }
}

std::wstring App::toolSignature2() const {
    wchar_t b[256];
    size_t pts = 0;
    for (auto& s : strokes_) pts += s.pts.size();
    swprintf_s(b, L"|%d,%d,%s,%d,%d,%u|%d,%d|%zu,%zu,%u,%d,%d|%d,%d,%u", insertKind_, bubbleShape_, bubbleText_.c_str(), bubbleSize_, tailDir_, bubbleFill_,
               stickerIdx_, stickerSize_, strokes_.size(), pts, penColor_, penWidth_, (int)highlighter_, frameStyle_, frameThick_, frameColor_);
    return b;
}

// SR upscale used by the resize panel: model 0 = ESPCN, 1 = FSRCNN
Frames App::superResolve(const Frames& src, int model, int scale, const Progress& prog) {
    std::vector<uint8_t> pb;
    if (!loadSrModelResource(model, scale, pb)) throw std::runtime_error("초해상도 모델을 찾지 못했어요");
    SrModel sr;
    std::string e;
    if (!sr.load(pb, e)) throw std::runtime_error(e);
    Frames out = src;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].img = sr.upscale(*src[i].img);
        if (prog) prog((double)(i + 1) / out.size());
    }
    return out;
}

} // namespace bg
