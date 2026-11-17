#include "platform/Settings.h"

#include <shlobj.h>

namespace bg {

std::wstring Settings::dir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH * 2);
    std::wstring d = n ? std::wstring(buf) : L".";
    d += L"\\BongGIF";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

namespace {
struct Ini {
    std::wstring path = Settings::dir() + L"\\bonggif.ini";
    int geti(const wchar_t* k, int def) const { return (int)GetPrivateProfileIntW(L"BongGIF", k, def, path.c_str()); }
    std::wstring gets(const wchar_t* k, const std::wstring& def) const {
        wchar_t buf[8192];
        GetPrivateProfileStringW(L"BongGIF", k, def.c_str(), buf, 8192, path.c_str());
        return buf;
    }
    void put(const wchar_t* k, int v) const { WritePrivateProfileStringW(L"BongGIF", k, std::to_wstring(v).c_str(), path.c_str()); }
    void put(const wchar_t* k, const std::wstring& v) const { WritePrivateProfileStringW(L"BongGIF", k, v.c_str(), path.c_str()); }
};
} // namespace

#define BG_FIELDS(X)                                                                                         \
    X(fps) X(useDxgi) X(directSave) X(directQuality) X(directNoTimeLimit) X(directOpenFolder) X(showGrid) X(useMagnifier) X(trackMouse) X(ctrlPrtSc) X(recScaleDiv) X(excludeSelf) X(gotoEditAfterRec) X(lang) X(manageSort) X(deleteToRecycle) X(startWithRecorder) X(batchFormat) X(batchScale) X(batchMaxFps) X(batchSameDir) X(hotkeyRec) X(hotkeyPause) X(gifMethod) X(webmVp9) X(colorCompress) X(skipSimilar) X(maxFrameMs) X(maxSide) X(jpgQuality) X(webpLossless) X(webpAutoDecide) X(webpIphoneCompatible) X(webpQualityFirst) X(cursor) X(highlight) X(clickFx) X(skipSame) X(maxRecSec) X(maxMemMB) X(format) X(quality) X(dither) \
    X(centerWeighted) X(removeSimilar) X(skipQuant100) X(comment) X(loopFinite) X(loopCount) X(scalePct)          \
    X(maxFps) X(progressBar) X(openFolder) X(importFrameMs) X(videoMaxFps) X(videoMaxSide) X(keepSpeed)           \
    X(nthFrame) X(summaryPct) X(summaryAdjust) X(summaryLimitMs) X(textSize) X(winX) X(winY) X(winW) X(winH)

void Settings::load() {
    Ini ini;
    wchar_t pics[MAX_PATH];
    std::wstring defDir = L".";
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pics))) defDir = std::wstring(pics) + L"\\BongGIF";
    saveDir = ini.gets(L"saveDir", defDir);
    protectColors = ini.gets(L"protectColors", L"");
    recent = ini.gets(L"recent", L"");
    batchDir = ini.gets(L"batchDir", defDir + L"\\변환");
#define X(f) f = (decltype(f))ini.geti(L## #f, (int)f);
    BG_FIELDS(X)
#undef X
    textColor = (unsigned)ini.geti(L"textColor", (int)textColor);
    for (int i = 0; i < 3; ++i) {
        std::wstring k = L"recPos" + std::to_wstring(i + 1);
        recPos[i].left = ini.geti((k + L"X").c_str(), 0);
        recPos[i].top = ini.geti((k + L"Y").c_str(), 0);
        recPos[i].right = recPos[i].left + ini.geti((k + L"W").c_str(), 0);
        recPos[i].bottom = recPos[i].top + ini.geti((k + L"H").c_str(), 0);
    }
    recRect.left = ini.geti(L"recX", recRect.left);
    recRect.top = ini.geti(L"recY", recRect.top);
    recRect.right = recRect.left + ini.geti(L"recW", 640);
    recRect.bottom = recRect.top + ini.geti(L"recH", 360);
}

void Settings::save() const {
    Ini ini;
    ini.put(L"saveDir", saveDir);
    ini.put(L"protectColors", protectColors);
    ini.put(L"recent", recent);
    ini.put(L"batchDir", batchDir);
#define X(f) ini.put(L## #f, (int)f);
    BG_FIELDS(X)
#undef X
    ini.put(L"textColor", (int)textColor);
    for (int i = 0; i < 3; ++i) {
        std::wstring k = L"recPos" + std::to_wstring(i + 1);
        ini.put((k + L"X").c_str(), recPos[i].left);
        ini.put((k + L"Y").c_str(), recPos[i].top);
        ini.put((k + L"W").c_str(), recPos[i].right - recPos[i].left);
        ini.put((k + L"H").c_str(), recPos[i].bottom - recPos[i].top);
    }
    ini.put(L"recX", recRect.left);
    ini.put(L"recY", recRect.top);
    ini.put(L"recW", recRect.right - recRect.left);
    ini.put(L"recH", recRect.bottom - recRect.top);
}

} // namespace bg
