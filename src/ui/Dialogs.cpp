// Modal dialogs in the BongHub style (option rows: title + description left, control right).
#include "ui/Dialogs.h"

#include "core/Edit.h"
#include "core/Gif.h"
#include "platform/Export.h"
#include "platform/Player.h"
#include "ui/Lang.h"
#include "ui/Gui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <memory>
#include <mutex>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>

namespace bg {

// ------------------------------------------------------------------ frame time
bool timeDialog(HWND owner, const std::wstring& title, int64_t cur, int64_t& out) {
    Form f(owner, title, 440);
    int mode = 0, fpsSel = 4;
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f", std::max<int64_t>(cur, 20000) / 1e6);
    std::wstring sec = buf;
    std::vector<std::wstring> fps;
    for (int v : kFpsChoices) fps.push_back(std::to_wstring(v) + L" fps");
    f.body = [&](Gui& g, float x, float y, float w) {
        RectF c = g.optionRow(x, y, w, L"정하는 방식", L"", 220, 32);
        g.segmented(gid("t.mode"), c, {L"FPS로", L"초로"}, mode);
        if (mode == 0) {
            c = g.optionRow(x, y, w, L"초당 프레임", L"모든 대상 프레임에 같은 시간이 들어가요", 140, 32);
            g.dropdown(gid("t.fps"), c, fps, fpsSel);
        } else {
            c = g.optionRow(x, y, w, L"프레임 시간 (초)", L"0.02초보다 짧게는 정할 수 없어요", 140, 32, true, false);
            g.edit(gid("t.sec"), c, sec);
        }
        return y;
    };
    f.onOk = [&] {
        if (mode == 0) out = fpsToUs(kFpsChoices[std::clamp(fpsSel, 0, 10)]);
        else {
            double s = _wtof(sec.c_str());
            if (s <= 0) { messageBox(f.hwnd(), L"시간을 숫자로 입력해 주세요."); return false; }
            out = secondsToUs(s);
        }
        return true;
    };
    return f.run();
}

// ------------------------------------------------------------------ trim
bool trimDialog(HWND owner, const Frames& frames, int& a, int& b) {
    Form f(owner, L"구간 남기기", 520);
    int n = (int)frames.size();
    int va = 0, vb = n - 1, shown = 0;
    f.okText = L"구간 남기기";
    f.body = [&](Gui& g, float x, float y, float w) {
        g.g.text(L"고른 구간만 남기고 앞뒤 프레임은 지워요.", {x, y, w, 20}, th::textSecondary, th::caption);
        y += 28;
        RectF well{x, y, w, 240};
        g.g.fill(well, th::radiusCard, th::canvasWell);
        const ImagePtr& im = frames[std::clamp(shown, 0, n - 1)].img;
        float sc = std::min((well.w - 16) / im->w, (well.h - 16) / im->h);
        g.g.image(im, {well.x + (well.w - im->w * sc) / 2, well.y + (well.h - im->h * sc) / 2, im->w * sc, im->h * sc}, true);
        y += 252;
        RectF c = g.optionRow(x, y, w, L"시작", std::to_wstring(va + 1) + L"번 프레임", 300, 30);
        if (g.slider(gid("tr.a"), c, va, 0, n - 1)) { vb = std::max(vb, va); shown = va; }
        c = g.optionRow(x, y, w, L"끝", std::to_wstring(vb + 1) + L"번 프레임", 300, 30, true, false);
        if (g.slider(gid("tr.b"), c, vb, 0, n - 1)) { va = std::min(va, vb); shown = vb; }
        return y;
    };
    if (!f.run()) return false;
    a = va;
    b = vb;
    return true;
}

// ------------------------------------------------------------------ reduce
bool reduceDialog(HWND owner, Settings& st, ReduceParams& out) {
    Form f(owner, L"프레임 줄이기", 520);
    int mode = 0, nth = st.nthFrame, diff = 20;
    bool keep = st.keepSpeed;
    f.okText = L"줄이기";
    f.body = [&](Gui& g, float x, float y, float w) {
        g.segmented(gid("rd.mode"), {x, y, w, 34}, {L"짝수 번째", L"홀수 번째", L"N번째마다", L"비슷한 프레임"}, mode);
        y += 46;
        static const wchar_t* desc[] = {L"2, 4, 6…번째 프레임을 지워요", L"1, 3, 5…번째 프레임을 지워요", L"", L"앞 프레임과 거의 같은 프레임을 지워요"};
        if (mode == 2) {
            RectF c = g.optionRow(x, y, w, L"몇 번째마다 지울까요", L"예: 3이면 3, 6, 9…번째를 지워요", 140, 30);
            g.number(gid("rd.nth"), c, nth, 3, 10);
        } else if (mode == 3) {
            RectF c = g.optionRow(x, y, w, L"차이 허용", L"다른 픽셀이 이 비율 이하면 비슷하다고 봐요", 200, 30);
            g.slider(gid("rd.diff"), {c.x, c.y, c.w - 46, c.h}, diff, 0, 50);
            g.g.text(std::to_wstring(diff) + L"%", {c.r() - 42, c.y, 42, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        } else {
            g.g.text(desc[mode], {x, y, w, 22}, th::textSecondary, th::caption);
            y += 30;
            g.separator(x, x + w, y);
            y += 1;
        }
        RectF c = g.optionRow(x, y, w, L"재생 속도 유지", L"지운 프레임의 시간을 남은 프레임에 나눠 줘서 전체 길이가 그대로예요", 40, 20, true, false);
        g.toggle(gid("rd.keep"), c, keep);
        return y;
    };
    if (!f.run()) return false;
    out.mode = mode;
    out.nth = nth;
    out.diff = diff;
    out.keep = keep;
    st.nthFrame = nth;
    st.keepSpeed = keep;
    return true;
}

// ------------------------------------------------------------------ summary
bool summaryDialog(HWND owner, Settings& st, SummaryParams& out) {
    Form f(owner, L"비디오 요약", 520);
    int pct = st.summaryPct, adj = st.summaryAdjust, lim = st.summaryLimitMs;
    bool scale = true, limit = true;
    f.okText = L"요약하기";
    f.body = [&](Gui& g, float x, float y, float w) {
        g.g.text(L"화면이 거의 안 바뀌는 구간을 줄여서 짧게 만들어요.", {x, y, w, 20}, th::textSecondary, th::caption);
        y += 28;
        RectF c = g.optionRow(x, y, w, L"비슷하다고 볼 기준", L"다른 픽셀이 이 비율 이하인 프레임을 지워요", 200, 30);
        g.slider(gid("sm.pct"), {c.x, c.y, c.w - 46, c.h}, pct, 0, 90);
        g.g.text(std::to_wstring(pct) + L"%", {c.r() - 42, c.y, 42, c.h}, th::textSecondary, th::caption, 400, A_RIGHT);
        c = g.optionRow(x, y, w, L"지운 시간 일부 남기기", L"지운 구간 시간의 일부를 남은 프레임에 더해요", 40, 20);
        g.toggle(gid("sm.scale"), c, scale);
        c = g.optionRow(x, y, w, L"남길 비율 (%)", L"", 140, 30, scale);
        g.number(gid("sm.adj"), c, adj, 0, 100, scale);
        c = g.optionRow(x, y, w, L"프레임 시간 상한", L"한 프레임이 너무 오래 머물지 않게 해요", 40, 20);
        g.toggle(gid("sm.lim"), c, limit);
        c = g.optionRow(x, y, w, L"상한 (ms)", L"", 140, 30, limit, false);
        g.number(gid("sm.limms"), c, lim, 50, 10000, limit);
        return y;
    };
    if (!f.run()) return false;
    out.percent = pct;
    out.scale = scale;
    out.adjust = adj;
    out.limit = limit;
    out.limitUs = lim * 1000LL;
    st.summaryPct = pct;
    st.summaryAdjust = adj;
    st.summaryLimitMs = lim;
    return true;
}

// ------------------------------------------------------------------ video import
// Player-style range picker: the video plays with sound, a thumbnail timeline with start/end handles
// chooses the clip, and only that range is decoded into frames.
static std::wstring fmtClock(double s) {
    s = std::max(0.0, s);
    int m = (int)(s / 60);
    wchar_t b[32];
    swprintf_s(b, L"%d:%05.2f", m, s - m * 60);
    return b;
}

bool videoImportDialog(HWND owner, const std::wstring& path, const VideoInfo& vi, Settings& st, VideoImportParams& out) {
    const double dur = vi.duration > 0 ? vi.duration : 10.0;
    double a = 0, b = std::min(dur, 10.0);
    int fps = st.videoMaxFps, side = st.videoMaxSide;
    bool muted = false, loopSel = true;

    // height of the video well from the owner's work area
    float wellH = 400;
    {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
        float sc = GetDpiForWindow(owner) / 96.0f;
        float avail = (mi.rcWork.bottom - mi.rcWork.top) / std::max(1.0f, sc);
        wellH = std::clamp(avail * 0.85f - 430, 200.0f, 440.0f);
    }

    VideoPlayer pl;
    std::string perr;
    bool havePlayer = pl.open(path, 1280, 720, perr);
    ImagePtr cur;

    // thumbnails on a worker
    const int NT = 16;
    std::mutex tm;
    std::vector<ImagePtr> thumbs(NT);
    std::atomic<int> thumbGen{0};
    std::atomic<bool> stopThumbs{false};
    std::thread thumbThread([&] {
        videoThumbnails(path, NT, 90, [&](int i, ImagePtr im) {
            { std::lock_guard<std::mutex> l(tm); thumbs[i] = std::move(im); }
            ++thumbGen;
        }, [&] { return stopThumbs.load(); });
    });
    struct Join {
        std::atomic<bool>& stop;
        std::thread& t;
        ~Join() { stop = true; if (t.joinable()) t.join(); }
    } join{stopThumbs, thumbThread};
    ImagePtr strip;
    int stripGen = -1;

    int drag = 0;            // 1 start handle, 2 end handle, 3 playhead
    double dragOffset = 0;

    auto playFromRange = [&] {
        double t = pl.time();
        if (t >= b - 0.05 || t < a) pl.seek(a);
        pl.play();
    };

    Form f(owner, L"영상 가져오기", 900);
    f.okText = L"이 구간 가져오기";
    f.tickMs = 15;
    f.onTick = [&] {
        if (!havePlayer) return;
        if (auto im = pl.frame()) cur = im;
        if (!pl.paused() && drag == 0) {
            double t = pl.time();
            if (loopSel && (t >= b - 0.01 || t < a - 0.3)) pl.seek(a);
        }
    };
    f.onKey = [&](int vk) {
        if (!havePlayer) return false;
        bool shift = GetKeyState(VK_SHIFT) & 0x8000;
        switch (vk) {
        case VK_SPACE: if (pl.paused()) playFromRange(); else pl.pause(); return true;
        case 'I': a = std::clamp(pl.time(), 0.0, b - 0.1); return true;
        case 'O': b = std::clamp(pl.time(), a + 0.1, dur); return true;
        case VK_LEFT: case VK_RIGHT: {
            double step = (shift ? 1.0 : 1.0 / std::max(1.0, vi.fps)) * (vk == VK_LEFT ? -1 : 1);
            pl.pause();
            pl.seek(std::clamp(pl.time() + step, 0.0, dur));
            return true;
        }
        case VK_HOME: pl.seek(a); return true;
        case VK_END: pl.seek(b); return true;
        }
        return false;
    };
    f.body = [&](Gui& g, float x, float y, float w) {
        Gfx& gx = g.g;
        // ---- video
        RectF well{x, y, w, wellH};
        gx.fill(well, th::radiusCard, 0xFF000000);
        if (cur) {
            float sc = std::min(well.w / cur->w, well.h / cur->h);
            gx.image(cur, {well.x + (well.w - cur->w * sc) / 2, well.y + (well.h - cur->h * sc) / 2, cur->w * sc, cur->h * sc}, true);
        } else {
            gx.text(havePlayer ? L"영상을 여는 중…" : L"미리보기를 열 수 없어요. 구간은 고를 수 있어요", well, th::textSecondary, th::body, 400,
                    A_CENTER);
        }
        if (havePlayer && g.interact(gid("vp.well"), well)) {
            if (pl.paused()) playFromRange();
            else pl.pause();
        }
        if (havePlayer && pl.paused() && cur) {
            float t = g.hoverT(gid("vp.well"));
            float cx = well.x + well.w / 2, cy = well.y + well.h / 2;
            gx.circle(cx, cy, 30 + 3 * t, 0x99000000);
            gx.icon(ic::Play, {cx - 30, cy - 30, 60, 60}, th::textPrimary, 24);
        }
        y += wellH + 10;

        // ---- transport row
        bool isPaused = !havePlayer || pl.paused();
        if (g.iconButton(gid("vp.play"), {x, y, 36, 34}, isPaused ? ic::Play : ic::Pause, havePlayer, th::textPrimary, 16)) {
            if (isPaused) playFromRange();
            else pl.pause();
        }
        double now = havePlayer ? pl.time() : a;
        gx.text(fmtClock(now) + L"  /  " + fmtClock(dur), {x + 44, y, 220, 34}, th::textSecondary, th::body, 600);
        float rx = x + w;
        if (g.iconButton(gid("vp.mute"), {rx - 34, y, 34, 34}, muted ? (wchar_t)0xE74F : (wchar_t)0xE767, havePlayer && pl.hasAudio(),
                         th::textSecondary, 15)) {
            muted = !muted;
            pl.setMuted(muted);
        }
        RectF lt{rx - 34 - 12 - 40, y + 7, 40, 20};
        g.toggle(gid("vp.loop"), lt, loopSel);
        float lw = gx.textWidth(L"구간 반복", th::caption, 600);
        gx.text(L"구간 반복", {lt.x - lw - 8, y, lw + 2, 34}, th::textSecondary, th::caption, 600);
        y += 44;

        // ---- timeline: thumbnails + range handles + playhead
        RectF tl{x + 10, y + 6, w - 20, 64};
        if (thumbGen.load() != stripGen) {   // tile the thumbnails without stretching: centre-crop each slot
            stripGen = thumbGen.load();
            std::lock_guard<std::mutex> l(tm);
            const int sh = 90, sw = std::max(NT, (int)std::lround(sh * tl.w / tl.h));
            auto im = std::make_shared<Image>(sw, sh);
            for (auto& p : im->px) p = 0xFF15171C;
            bool any = false;
            for (int i = 0; i < NT; ++i) {
                const ImagePtr& t = thumbs[i];
                if (!t || t->h != sh) continue;
                any = true;
                int x0 = sw * i / NT, x1 = sw * (i + 1) / NT, slot = x1 - x0;
                int off = (t->w - slot) / 2;
                for (int yy = 0; yy < sh; ++yy)
                    for (int xx = 0; xx < slot; ++xx) {
                        int sx = std::clamp(off + xx, 0, t->w - 1);
                        im->row(yy)[x0 + xx] = (off + xx >= 0 && off + xx < t->w) ? t->row(yy)[sx] : 0xFF15171C;
                    }
            }
            if (any) strip = im;
        }
        gx.fill(tl, 6, 0xFF15171C);
        if (strip) gx.image(strip, tl, true);
        auto xOf = [&](double t) { return tl.x + (float)(t / dur) * tl.w; };
        auto tOf = [&](float px) { return std::clamp((double)(px - tl.x) / tl.w * dur, 0.0, dur); };
        float xa = xOf(a), xb = xOf(b);
        gx.fill({tl.x, tl.y, xa - tl.x, tl.h}, 0, 0xB0000000);
        gx.fill({xb, tl.y, tl.r() - xb, tl.h}, 0, 0xB0000000);
        gx.fill({xa, tl.y - 3, xb - xa, 3}, 0, th::yellow);
        gx.fill({xa, tl.b(), xb - xa, 3}, 0, th::yellow);
        RectF ha{xa - 10, tl.y - 3, 10, tl.h + 6}, hb{xb, tl.y - 3, 10, tl.h + 6};
        gx.fill(ha, 3, th::yellow);
        gx.fill(hb, 3, th::yellow);
        gx.line(ha.x + 5, tl.y + tl.h / 2 - 8, ha.x + 5, tl.y + tl.h / 2 + 8, 0xFF5A4A00, 2);
        gx.line(hb.x + 5, tl.y + tl.h / 2 - 8, hb.x + 5, tl.y + tl.h / 2 + 8, 0xFF5A4A00, 2);
        // one interaction over the whole strip; where the press lands decides what is dragged
        RectF hitR{tl.x - 14, tl.y - 8, tl.w + 28, tl.h + 16};
        Id tid = gid("vp.tl");
        g.interact(tid, hitR, true);
        if (g.active == tid && g.pressed) {
            if (std::fabs(g.mx - (xa - 5)) <= 10) { drag = 1; dragOffset = g.mx - xa; }
            else if (std::fabs(g.mx - (xb + 5)) <= 10) { drag = 2; dragOffset = g.mx - xb; }
            else { drag = 3; dragOffset = 0; }
            if (havePlayer && drag != 3) pl.pause();
        }
        if (drag && g.active == tid && g.down) {
            double t = tOf(g.mx - (float)dragOffset);
            const double minLen = std::min(0.1, dur);
            if (drag == 1) { a = std::clamp(t, 0.0, b - minLen); if (havePlayer) pl.seek(a); }
            else if (drag == 2) { b = std::clamp(t, a + minLen, dur); if (havePlayer) pl.seek(b); }
            else if (havePlayer) pl.seek(t);
        }
        if (!g.down) {
            if (drag == 2 && havePlayer) pl.seek(a);   // after moving the end, rewind to the start
            drag = 0;
        }
        float xp = xOf(havePlayer ? std::clamp(pl.time(), 0.0, dur) : a);
        gx.fill({xp - 1, tl.y - 7, 2, tl.h + 14}, 1, th::textPrimary);
        gx.circle(xp, tl.y - 7, 5, th::textPrimary);
        y += 6 + 64 + 8;
        gx.text(fmtClock(0), {tl.x, y, 100, 16}, th::textTertiary, th::caption);
        gx.text(fmtClock(dur), {tl.r() - 100, y, 100, 16}, th::textTertiary, th::caption, 400, A_RIGHT);
        y += 26;

        // ---- selection summary + set from playhead
        float cx = x;
        auto chip = [&](const std::wstring& t, uint32_t fg) {
            float cw = gx.textWidth(t, th::caption, 600) + 18;
            g.chip({cx, y + 5, cw, 24}, t, fg, th::control);
            cx += cw + 6;
        };
        double len = b - a, rfps = std::min<double>(fps, vi.fps > 0 ? vi.fps : fps);
        int nFrames = std::max(1, (int)std::lround(len * rfps));
        int ow = vi.w, oh = vi.h;
        if (side > 0 && std::max(ow, oh) > side) {
            double s2 = (double)side / std::max(ow, oh);
            ow = (int)(ow * s2);
            oh = (int)(oh * s2);
        }
        double bytes = (double)nFrames * ow * oh * 4;
        wchar_t buf[96];
        chip(tr(L"시작") + L" " + fmtClock(a), th::textPrimary);
        chip(tr(L"끝") + L" " + fmtClock(b), th::textPrimary);
        swprintf_s(buf, L"%.2f", len);
        chip(tr(L"길이") + L" " + buf + tr(L"초"), th::textPrimary);
        chip(std::to_wstring(nFrames) + tr(L" 프레임") + L" · " + std::to_wstring(ow) + L"×" + std::to_wstring(oh) + L" · " +
                 fmtBytes((size_t)bytes),
             bytes > 1.5e9 ? th::orange : th::textSecondary);
        float bw = 124;
        if (g.button(gid("vp.setB"), {x + w - bw, y, bw, 34}, L"여기를 끝으로", BS_SECONDARY, 0, havePlayer))
            b = std::clamp(pl.time(), a + 0.1, dur);
        if (g.button(gid("vp.setA"), {x + w - bw * 2 - 8, y, bw, 34}, L"여기를 시작으로", BS_SECONDARY, 0, havePlayer))
            a = std::clamp(pl.time(), 0.0, b - 0.1);
        y += 40;
        gx.text(L"Space 재생/멈춤 · ←/→ 한 프레임 (Shift 1초) · I 시작 · O 끝 · Home/End 구간 처음/끝", {x, y, w, 18}, th::textTertiary,
                th::caption);
        y += 28;

        RectF c = g.optionRow(x, y, w, L"최대 FPS", L"영상 FPS보다 높으면 영상 FPS를 써요", 140, 30);
        g.number(gid("vi.fps"), c, fps, 1, 60);
        c = g.optionRow(x, y, w, L"긴 변 최대 (px)", L"0이면 원본 크기 그대로예요", 140, 30, true, false);
        g.number(gid("vi.side"), c, side, 0, 8000);
        return y;
    };
    f.onOk = [&] {
        if (b - a < 0.05) { messageBox(f.hwnd(), L"구간이 너무 짧아요."); return false; }
        return true;
    };
    if (havePlayer) pl.seek(0);
    bool ok = f.run();
    if (havePlayer) pl.pause();
    if (!ok) return false;
    out.start = a;
    out.end = b;
    out.maxFps = fps;
    out.maxSide = side;
    st.videoMaxFps = fps;
    st.videoMaxSide = side;
    return true;
}

} // namespace bg
