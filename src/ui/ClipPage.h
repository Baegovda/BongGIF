#pragma once
// "WebP 클립" page: a video clip editor that exports animated WebP. The video plays with sound,
// frames are addressed by index at the source FPS, masks (mosaic / blur / box / zoom / portal)
// are keyframed and can be tracked, the timeline shows ranges, slow motion, freeze points, the
// audio waveform with sound events and the selected mask's keys.
#include "core/Clip.h"
#include "platform/ClipSource.h"
#include "platform/Player.h"
#include "platform/Settings.h"
#include "ui/Gui.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bg {

class ClipPage {
public:
    ClipPage(HWND hwnd, Gui& gui, Settings& st, std::function<void(const std::wstring&)> status);
    ~ClipPage();
    void draw(RectF area);
    bool key(int vk);              // page shortcuts; true = handled
    void tick();                   // 15 ms timer while needsTick()
    bool needsTick() const;
    void open(const std::wstring& path);
    void openDialog();
    bool loaded() const { return loaded_; }
    void leave();                  // page hidden: stop playback

private:
    // one tracking pass: results plus how it ended
    struct TrackPass {
        TrackResult r;
        int status = 0;            // 0 ok, 1 stopped, 2 lost, 3 error
        int lostAt = -1;
        std::wstring err;
    };
    // a drag on the video: new mask / mask / portal exit / crop
    enum DragTarget { DT_NONE = 0, DT_NEW, DT_MASK, DT_DEST, DT_CROP };
    struct Drag {
        DragTarget target = DT_NONE;
        int idx = -1;
        int hx = 0, hy = 0;        // handle (-1/0/1 each); move when !handle
        bool handle = false, draw = false;
        Box4 rect{}, live{};
        bool hasLive = false, moved = false;
        double ox = 0, oy = 0;     // source point where the drag started
        float px = 0, py = 0;      // view point where the drag started
    };

    // ---- helpers
    void stopWorkers();
    void pushUndo(const std::string& tag = {});
    void undo();
    void redo();
    void changed();                // project edited: re-render, schedule autosave
    void saveProject();
    std::wstring projectFile() const;
    void seek(int f);              // ignored while tracking
    void ensureVisible(int f);
    void stepFrames(int d);
    void play();
    void stopPlay();
    void togglePlay();
    void jumpPlay(int f);
    std::vector<ClipRange> playSegments() const;
    ClipMask* selMask();
    void selectMask(int i);
    bool maskRect(int i, Box4& out) const;   // active masks only
    bool destRect(int i, Box4& out) const;   // active portals only
    void setProp(const char* name, const std::function<void(ClipMask&)>& fn);
    void addMask(const Box4& r);
    void setCrop(const Box4& r);
    void keyAdd();
    void keyDel();
    void gotoKey(int dir);
    void clearKeysSide(int dir);
    void deleteMask();
    void moveMask(int d);
    void duplicateMask();
    void setIn();
    void setOut();
    void toggleCrop();
    void clearCrop();
    // tracking
    void startTrack(int dir);
    void startTrackBetween();
    void stopTrack();
    void finishTrack();
    void jumpWeak(int dir);
    TrackPass runPass(ClipSource& s, int origin, const Box4& rect, int stop, int dir, const TrackerOptions& opt);
    bool applyTrack(ClipMask& m, const TrackResult& r, int& lo, int& hi);
    std::wstring weakNote(const ClipMask& m, bool has, int lo, int hi);
    // audio
    void analyzeAudio();
    void recomputeEvents();
    void gotoEvent(int dir);
    // ranges
    void segAction(int act);
    void markSlow();
    void toggleFreeze();
    // export
    void exportWebp();
    void fitSize();
    void showHelp();
    void pickColor();
    std::wstring summary();
    std::wstring rangeLabel();
    int cropWidth() const;

    // ---- drawing
    void drawEmpty(RectF r);
    void drawTop(RectF r);
    void drawView(RectF r);
    void drawTransport(RectF r);
    void drawTimeline(RectF r);
    void drawPanel(RectF r);
    void drawBusy(RectF area);
    ImagePtr currentImage();
    bool decimal(Id id, RectF r, double& v, double lo, double hi, double step, int decimals);

    HWND hwnd_;
    Gui& gui_;
    Settings& st_;
    std::function<void(const std::wstring&)> status_;

    bool loaded_ = false;
    std::wstring path_, stem_;
    ClipSource src_;
    ClipVideoInfo info_;
    double fps_ = 30;
    int count_ = 1;
    std::unique_ptr<VideoPlayer> player_;
    bool playing_ = false;
    int segIdx_ = 0;
    double rate_ = 1;
    ULONGLONG holdUntil_ = 0;  // freeze point hold during playback

    ClipProject P_;
    int sel_ = -1;
    int f_ = 0;
    uint64_t version_ = 0;
    ImagePtr shown_;
    int shownF_ = -1;
    uint64_t shownVer_ = ~0ull;
    bool shownFx_ = true;
    ImagePtr playFrame_;

    // view
    float zoom_ = 1;
    double cx_ = 0, cy_ = 0;   // source point at the view centre
    RectF view_{};
    int mode_ = 0;             // 0 masks, 1 crop
    bool fx_ = true, overlay_ = true, loopRange_ = true;
    Drag drag_;
    bool pan_ = false;
    float panX_ = 0, panY_ = 0;
    double panCx_ = 0, panCy_ = 0;

    // timeline
    double tlLo_ = 0, tlHi_ = 1;
    bool tlPan_ = false;
    float tlPanX_ = 0;
    double tlPanLo_ = 0;

    // audio
    std::vector<float> levels_;
    std::vector<int> events_;
    std::wstring audioStatus_;
    int rise_ = 12, floor_ = -45;
    bool sound_ = true;
    int volume_ = 50;
    std::thread audioThread_;
    std::atomic<bool> audioDone_{false};
    std::vector<float> audioOut_, audioSamples_;
    int audioRate_ = 0;
    std::wstring audioMsg_;

    // lists
    bool segPlay_ = true;
    int segSel_ = -1, slowSel_ = -1;
    int slowStart_ = -1;
    double slowRate_ = 0.5, freezeSec_ = 1.5;

    // undo / autosave
    std::vector<std::string> undo_, redo_;
    std::string undoTag_;
    bool dirty_ = false;
    ULONGLONG dirtyAt_ = 0;

    // tracking worker (results are written to the mask when it ends)
    bool smooth_ = true, fixedSize_ = false;   // "떨림 완화", "박스 크기 고정"
    std::wstring trackLabel_;
    std::thread trackThread_;
    std::atomic<bool> trackStop_{false}, trackDone_{true};
    std::atomic<int> trackPhase_{0};          // between: 1 forward, 2 backward
    std::mutex trackMu_;
    ImagePtr trackImg_;                       // last frame the tracker read (shown raw)
    int trackImgF_ = -1;
    Box4 trackShow_{};                        // tracked box on that frame
    bool trackHasShow_ = false;
    TrackPass trackRes_;
    int trackDir_ = 0, trackMask_ = -1, trackOrigin_ = 0, trackStopF_ = 0, trackFix_ = -1, resumeDir_ = 0;
    bool trackBetween_ = false;
    ULONGLONG trackT0_ = 0;

    // export / fit worker
    std::thread jobThread_;
    std::atomic<bool> jobCancel_{false}, jobDone_{true};
    std::atomic<int> jobK_{0}, jobN_{1};
    std::mutex jobMu_;
    std::wstring jobTitle_, jobMsg_, jobResult_;
    int jobKind_ = 0;   // 1 export, 2 fit
    bool jobApply_ = false;
    FitCandidate jobCand_;
    std::wstring fitLabel_;

    // panel
    ScrollArea panelScroll_;
    std::map<Id, std::wstring> decText_;
};

} // namespace bg
