// Modal dialogs in the BongHub style (option rows: title + description left, control right).
#include "ui/Dialogs.h"

#include "core/Edit.h"
#include "core/Gif.h"
#include "platform/Export.h"
#include "ui/Gui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <memory>
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
bool videoImportDialog(HWND owner, const VideoInfo& vi, Settings& st, VideoImportParams& out) {
    Form f(owner, L"영상 가져오기", 480);
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f", std::min(vi.duration > 0 ? vi.duration : 10.0, 10.0));
    std::wstring s0 = L"0", s1 = buf;
    int fps = st.videoMaxFps, side = st.videoMaxSide;
    f.okText = L"가져오기";
    f.body = [&](Gui& g, float x, float y, float w) {
        wchar_t info[128];
        swprintf_s(info, L"%d×%d", vi.w, vi.h);
        float cx = x;
        auto chip = [&](const std::wstring& t) {
            float cw = g.g.textWidth(t, th::caption, 600) + 18;
            g.chip({cx, y, cw, 22}, t, th::textSecondary, th::control);
            cx += cw + 6;
        };
        chip(info);
        swprintf_s(info, L"%.2f fps", vi.fps);
        chip(info);
        swprintf_s(info, L"%.1f초", vi.duration);
        chip(info);
        y += 34;
        RectF c = g.optionRow(x, y, w, L"시작 (초)", L"", 140, 30);
        g.edit(gid("vi.s"), c, s0);
        c = g.optionRow(x, y, w, L"끝 (초)", L"긴 영상은 메모리를 많이 써요", 140, 30);
        g.edit(gid("vi.e"), c, s1);
        c = g.optionRow(x, y, w, L"최대 FPS", L"", 140, 30);
        g.number(gid("vi.fps"), c, fps, 1, 60);
        c = g.optionRow(x, y, w, L"긴 변 최대 (px)", L"0이면 원본 크기 그대로예요", 140, 30, true, false);
        g.number(gid("vi.side"), c, side, 0, 8000);
        return y;
    };
    f.onOk = [&] {
        double a = _wtof(s0.c_str()), b = _wtof(s1.c_str());
        if (b <= a) { messageBox(f.hwnd(), L"끝 시간이 시작보다 커야 해요."); return false; }
        out.start = a;
        out.end = b;
        return true;
    };
    if (!f.run()) return false;
    out.maxFps = fps;
    out.maxSide = side;
    st.videoMaxFps = fps;
    st.videoMaxSide = side;
    return true;
}

} // namespace bg
