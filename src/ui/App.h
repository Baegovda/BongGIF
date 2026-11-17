#pragma once
#include "core/Edit.h"
#include "platform/Capture.h"
#include "platform/Media.h"
#include "platform/Settings.h"
#include "ui/Gui.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <set>
#include <string>

namespace bg {

constexpr UINT WM_APP_RECSTOP = WM_APP + 1;   // recorder stopped by itself (wParam = reason)

class RecorderWindow;

class App {
public:
    Settings st;
    Project proj;
    HWND hwnd = nullptr;
    bool dirty = false;

    App();
    ~App();   // stops a running batch conversion
    bool create(HINSTANCE inst, const std::wstring& openPath);
    bool preTranslate(MSG& m);     // keyboard shortcuts; true = handled

    void recordingDone(Frames f);
    void recorderClosed();
    void status(const std::wstring& s);
    void settingsChanged();        // re-register optional hotkeys / timers after an option changed

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    LRESULT hitTest(int x, int y);
    void paint();
    void drawSidebar(float h);
    // pages (App3.cpp)
    enum Page { P_HOME, P_EDIT, P_MANAGE, P_BATCH, P_SETTINGS };
    void setPage(int p);
    void pageHeader(const std::wstring& title, const std::wstring& desc);
    void drawHome(float W, float H);
    void drawManage(float W, float H);
    void drawBatch(float W, float H);
    void drawSettings(float W, float H);
    void refreshManage();
    void sortManage();
    void loadManagePreview();
    void batchAddFiles(const std::vector<std::wstring>& files);
    void startBatch();
    bool loadForPage(const std::wstring& p, Frames& out, std::string& e, double maxSeconds, int maxSide, const Cancel& cancel);
    void addRecent(const std::wstring& p);
    std::vector<std::wstring> recentList() const;
    static std::wstring hotkeyName(int hk);
    void registerHotkeys();
    void drawTopBar(float x, float w);
    void drawFrameList(RectF r);
    void drawCanvas(RectF r);
    void drawToolPanel(RectF r);
    void drawCaptionButtons(float w);
    float toolCard(float x, float y, float w, int tool, wchar_t icon, uint32_t c1, uint32_t c2, const std::wstring& title,
                   const std::wstring& desc);
    float toolBody(float x, float y, float w, int tool);

    void gotoFrame(int i);
    void step(int d);
    void togglePlay();
    void stopPlay();
    void selectOnly(int i);
    std::vector<int> selection() const { return {sel_.begin(), sel_.end()}; }

    void apply(Frames f, const std::wstring& msg);
    void runBg(const std::wstring& title, std::function<Frames(const Progress&, const Cancel&)> fn, const std::wstring& msg);
    bool needFrames();
    void undo();
    void redo();
    void cmdSetTime(bool selOnly);
    void cmdSpeed(double k);
    void cmdTrim();
    void cmdReduce(bool selOnly);
    void cmdSummary();
    void cmdDelete();
    void cmdAppend();
    void cmdRevert();
    void openDialog();
    void openPath(const std::wstring& p, bool append);
    void importImages();
    void setFrames(Frames f, const std::wstring& label, bool isDirty);
    bool confirmDiscard();
    void saveDialog();
    void openRecorder(bool start);

    enum Tool { T_NONE, T_RESIZE, T_CROP, T_TEXT, T_LOGO, T_TRANS, T_EFFECT, T_COLLAGE, T_INSERT, T_DRAW, T_ZOOM, T_FRAME };
    float toolBody2(int tool, float ix, float yy, float iw);
    std::function<ImagePtr(const Image&)> toolFn2();
    std::wstring toolSignature2() const;
    float swatchRow(float x, float y, uint32_t& value, Id base);
    static Frames superResolve(const Frames& src, int model, int scale, const Progress& prog);
    void setTool(int t);
    std::function<ImagePtr(const Image&)> toolFn();
    std::vector<int> scopeSel() const;
    std::wstring toolSignature() const;
    void updatePreview();

    Gui gui_;
    int cur_ = 0;
    std::set<int> sel_;
    int anchor_ = 0;
    bool playing_ = false;
    int zoomSel_ = 0;              // 0 fit, 1 50%, 2 100%, 3 200%
    std::wstring status_ = L"녹화(Ctrl+Shift+X)나 열기(Ctrl+O)로 시작해요. 파일을 끌어다 놓아도 돼요.";
    ScrollArea listScroll_, panelScroll_;
    bool ensureCurVisible_ = false;
    RectF canvasR_{}, imageR_{};
    float viewScale_ = 1;
    std::unique_ptr<RecorderWindow> rec_;
    // caption buttons (max button is non-client for Windows 11 snap layouts)
    RectF minR_{}, maxR_{}, closeR_{};
    bool maxHot_ = false, maxDown_ = false;

    // tools
    int tool_ = T_NONE;
    ImagePtr preview_;
    std::wstring lastSig_;
    RectF selRect_{};              // image-space selection (crop / effect region)
    bool dragging_ = false;
    float dragX0_ = 0, dragY0_ = 0;
    int textX_ = 10, textY_ = 10;
    int rW_ = 0, rH_ = 0, lastRW_ = 0, lastRH_ = 0;
    bool keepAspect_ = true;
    int ratioSel_ = 0;
    int cX_ = 0, cY_ = 0, cW_ = 0, cH_ = 0;
    std::wstring text_ = L"BongGIF";
    int tSize_ = 40;
    unsigned textColor_ = 0xFFFFFF;
    bool outline_ = true;
    int scope_ = 0;                // 0 all, 1 selected
    std::shared_ptr<Image> logo_;
    int lScale_ = 100, lOpacity_ = 100;
    int tN_ = 10, tMs_ = 500;
    int effIdx_ = 0;
    std::vector<int> effVals_;
    bool effRegion_ = false;
    int effPx_ = -1, effPy_ = -1;
    uint32_t effColor_ = 0xFF00FF00;
    // resize: 0 normal, 1 ESPCN, 2 FSRCNN super-resolution; scale index 0..2 = x2..x4
    int srMethod_ = 0, srScale_ = 0;
    // collage
    std::vector<Frames> collageAnims_;
    std::vector<std::wstring> collageNames_;
    int collageLayout_ = 0, collageGap_ = 8;
    uint32_t collageBg_ = 0xFF000000;
    // insert
    int insertKind_ = 0, bubbleShape_ = 0, bubbleSize_ = 28, tailDir_ = 0, stickerIdx_ = 0, stickerSize_ = 72;
    std::wstring bubbleText_ = L"안녕하세요!";
    uint32_t bubbleFill_ = 0xFFFFFFFF;
    // drawing
    std::vector<Stroke> strokes_;
    uint32_t penColor_ = 0xFFFF5D5D;
    int penWidth_ = 6, drawScope_ = 0;
    bool highlighter_ = false;
    // zoom animation
    int zIn_ = 10, zHold_ = 10, zOut_ = 0;
    // photo frame
    int frameStyle_ = 0, frameThick_ = 16;
    uint32_t frameColor_ = 0xFFFFFFFF;

    // pages
    int page_ = P_HOME;
    ScrollArea sideScroll_, manageScroll_, batchScroll_, settingsScroll_;
    struct ManageFile { std::wstring name, path; uint64_t size = 0, time = 0; };
    std::vector<ManageFile> manageFiles_;
    int manageSel_ = 0, manageCur_ = 0;
    Frames managePreview_;
    struct BatchItem { std::wstring path, name, err; int state = 0; float progress = 0; size_t bytes = 0; };   // 0 wait 1 run 2 done 3 fail
    std::vector<BatchItem> batch_;
    std::mutex batchMu_;
    std::thread batchThread_;
    std::atomic<bool> batchRunning_{false}, batchCancel_{false};
    int settingsJump_ = -1, settingsActive_ = 0, settingsPin_ = -1;
    float settingsPinPos_ = 0;
    std::map<int, float> settingsCardH_;
    int hotkeyCapture_ = 0;        // 1 record, 2 pause: waiting for a key combination
};

} // namespace bg
