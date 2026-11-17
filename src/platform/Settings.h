#pragma once
// Settings in %APPDATA%\BongGIF\bonggif.ini.
#include <string>
#include <windows.h>

namespace bg {

struct Settings {
    int fps = 15;
    RECT recRect{200, 200, 840, 560};
    bool cursor = false, highlight = false, clickFx = false, skipSame = true;
    int maxRecSec = 60, maxMemMB = 3000;
    std::wstring saveDir;
    int format = 0;            // 0 GIF, 1 APNG, 2 MP4, 3 WebP
    bool webpLossless = false, webpAutoDecide = false, webpIphoneCompatible = true, webpQualityFirst = true;
    int gifMethod = 0;                 // nGifQuantMethod: 0 LIQ, 1 NEU, 2 OCTREE, 3 variance split
    std::wstring protectColors;        // "RRGGBB,RRGGBB,..." (colour protection, LIQ)
    bool webmVp9 = true, colorCompress = false, skipSimilar = false;
    int maxFrameMs = 0, maxSide = 0, jpgQuality = 90;
    int quality = 80;
    bool dither = true, centerWeighted = false, removeSimilar = true, skipQuant100 = true, comment = true;
    bool loopFinite = false;
    int loopCount = 1;
    int scalePct = 100, maxFps = 0;
    bool progressBar = false, openFolder = false;
    int importFrameMs = 100, videoMaxFps = 30, videoMaxSide = 960;
    bool keepSpeed = true;
    int nthFrame = 3, summaryPct = 50, summaryAdjust = 20, summaryLimitMs = 300;
    int textSize = 40;
    unsigned textColor = 0xFFFFFF;
    int winX = 80, winY = 60, winW = 1280, winH = 800;
    // recording extras
    bool useDxgi = false;              // useDXGI
    bool directSave = false;           // bRecDirectSave
    int directQuality = 90;            // nRecDirectSaveQuality
    bool directNoTimeLimit = false;    // bRecDirectSaveNoTimeLimit
    bool directOpenFolder = true;      // bRecDirectSaveOpenFolderAfterSave
    bool showGrid = false;             // bRecShowGrid
    bool useMagnifier = true;          // bUseMagnifier
    bool trackMouse = false;           // bTrackMouse (Ctrl+Alt moves the record window)
    bool ctrlPrtSc = false;            // bUseCtrlPtrScForRec
    int recScaleDiv = 1;               // full screen "save at 1/2, 1/4 size"
    RECT recPos[3]{};                  // rtRecWndPosSaved1..3
    bool excludeSelf = true;           // exclude own windows from capture
    bool gotoEditAfterRec = true;
    // pages / general
    std::wstring recent;               // recent files, '|' separated (newest first)
    std::wstring batchDir;
    int lang = 0;                      // 0 Korean, 1 English
    int manageSort = 1;                // 0 name, 1 date, 2 size
    bool deleteToRecycle = true, startWithRecorder = false;
    int batchFormat = 0, batchScale = 100, batchMaxFps = 0;
    bool batchSameDir = true;
    int hotkeyRec = (MOD_CONTROL | MOD_SHIFT) << 16 | 'X';    // (modifiers << 16) | virtual key
    int hotkeyPause = (MOD_CONTROL | MOD_SHIFT) << 16 | 'P';

    void load();
    void save() const;
    static std::wstring dir();
};

} // namespace bg
