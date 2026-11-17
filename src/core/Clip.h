#pragma once
// WebP clip editor model (the "WebP 클립" page): a clip project over a source video addressed by
// frame index (source FPS), with masks (mosaic / blur / box / zoom / portal) keyframed per frame,
// a template tracker, crop, several ranges joined in order, slow-motion ranges, freeze points and
// WebP export planning (output frame plan, durations, size-fit ladder). Pure logic, no Windows.
// Ported from WebP Clip core.js / render.js / tracker.js (same results, JS Math.round semantics).
#include "core/Image.h"

#include <array>
#include <climits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bg {

using Box4 = std::array<double, 4>;   // x, y, w, h in source pixels

enum ClipEffect { CE_MOSAIC = 0, CE_BLUR, CE_BOX, CE_ZOOM, CE_PORTAL, CE_COUNT };
enum ClipShape { CS_RECT = 0, CS_ELLIPSE };

struct ClipMask {
    std::wstring name;
    int effect = CE_MOSAIC;
    int strength = 12;              // mosaic cell / blur radius, 1..120
    uint32_t color = 0xFF000000;    // box fill, or border colour for zoom / portal
    double zoom = 2;                // 1.1..8
    int border = 2;                 // 0..40 px (zoom / portal)
    int shape = CS_RECT;
    int pad = 0;                    // 0..200 px
    int start = 0, end = 1000000000;   // visible frame range (inclusive)
    bool enabled = true;
    bool smooth = true;             // "떨림 완화" after tracking
    bool fixedSize = false;         // "박스 크기 고정" (off by default, as in the web app)
    std::map<int, Box4> keys;       // frame -> box
    std::map<int, Box4> destKeys;   // portal exit
    std::map<int, double> trackScore;   // frames written by the tracker -> match score (< 0.6 = needs checking, 0 = occluded)
    std::vector<int> lost;          // frames where tracking gave up
    struct BankEntry { int f = 0; ImagePtr img; };
    std::vector<BankEntry> trackBank;   // looks of the target at manual keys (<= 48 px side), first + latest 5
};

struct ClipExport {
    double fps = 60;                // output FPS (default min(source, 60))
    double speed = 1;               // 0.1..8
    int width = 0;                  // 0 = original (after crop)
    int quality = 90;               // 40..100
    int loop = 0;                   // 0 infinite, 1 once
    double targetMB = 0;            // 0 = off
};

struct ClipRange { int lo = 0, hi = 0; };
struct ClipSlow { int lo = 0, hi = 0; double rate = 0.5; };

struct ClipProject {
    int inFrame = 0, outFrame = 0;
    bool hasCrop = false;
    Box4 crop{0, 0, 0, 0};
    std::vector<ClipMask> masks;
    ClipExport ex;
    std::map<int, double> freezes;     // frame -> seconds held
    std::vector<ClipRange> segments;   // empty = [inFrame, outFrame]
    std::vector<ClipSlow> slowmos;
};

constexpr double kWeakScore = 0.6;

ClipProject newClipProject(int frameCount, double srcFps);
ClipMask newClipMask(const std::wstring& name, const ClipMask* base = nullptr);   // copies effect settings from base

// keyframes
bool interpBox(const std::map<int, Box4>& keys, int f, Box4& out);   // false when there are no keys
bool maskActive(const ClipMask& m, int f);
void clearKeys(std::map<int, Box4>& keys, int lo, int hi);
void smoothKeys(ClipMask& m, int lo, int hi, int win = 9);           // Savitzky-Golay (quadratic) on contiguous runs >= 5
std::vector<int> manualKeys(const ClipMask& m);                      // keys not written by the tracker
int nextWeak(const ClipMask& m, int f, int dir, double weak = kWeakScore);          // -1 when none
int weakCount(const ClipMask& m, int lo = INT_MIN, int hi = INT_MAX, double weak = kWeakScore);   // weak runs in [lo, hi]
struct TrackResult { std::map<int, Box4> rects; std::map<int, double> scores; };
TrackResult blendTracks(const TrackResult& fwd, const TrackResult& bwd, int a, int b);

// geometry
bool clipRect(const Box4& r, int pad, int w, int h, int out[4]);     // -> x0,y0,x1,y1; false if empty
void zoomBox(const int box[4], double zoom, int w, int h, int out[4]);
Box4 defaultDest(const Box4& r, int w, int h);
void cropBox(const ClipProject& p, int w, int h, int out[4]);
void outputSize(const ClipProject& p, int w, int h, int& ow, int& oh);

// output plan
std::vector<ClipRange> clipRanges(const ClipProject& p);
double speedAt(const ClipProject& p, int f);
std::vector<int> outputFrames(const ClipProject& p, double srcFps);  // source frame per output frame
int frameDurationMs(int k, double fps);                              // cumulative rounding: 60 fps -> 17/16/17
double defaultExportFps(double srcFps);
double snapFps(double fps);
std::wstring exportFilename(const std::wstring& stem, const std::vector<ClipRange>& ranges);
struct FitCandidate { int width = 0, quality = 90; double fps = 60; };
std::vector<FitCandidate> fitCandidates(int fullWidth, double srcFps);

// audio
std::vector<float> envelopeDb(const std::vector<float>& mono, int sampleRate, double fps, int count);   // per-frame RMS dBFS
std::vector<int> findEvents(const std::vector<float>& db, double fps, double rise = 12, double floorDb = -45, double window = 1,
                            double gap = 0.4);

// changed area of b vs a (any channel differs by > tol) as x0,y0,x1,y1 with x0,y0 rounded down to even;
// false when the images are the same (export writes only this rectangle per frame)
bool diffBox(const Image& a, const Image& b, int out[4], int tol = 2);

// rendering: applies every active mask of frame f to img in place (list order)
void renderMasks(Image& img, const std::vector<ClipMask>& masks, int f);
// crop + optional downscale to the export width
ImagePtr cropScale(const Image& img, const ClipProject& p);

// template tracker (tracker.js): normalised cross-correlation on the highest-contrast colour channel,
// coarse search with a few peaks, fine search with sub-pixel peak, optional scale search, coasting
// through occlusion (0.5 s), distractor memory, re-detection with 3-frame confirmation, template bank.
enum TrackState { TS_OK = 0, TS_COAST, TS_LOST };
enum TrackChannel { TC_GRAY = 0, TC_R, TC_G, TC_B, TC_RG, TC_BY };

struct TrackerOptions {
    bool fixedSize = false;          // keep the box size
    double fps = 30;                 // source FPS (how long to coast, distractor memory)
    std::vector<ImagePtr> bank;      // other looks of the target (trackBankOf), tried when the current one fails
    int channel = -1;                // TC_* or -1 = pick the highest-contrast one
};

class TemplateTracker {
public:
    TemplateTracker(const Image& frame, const Box4& rect, bool fixedSize = false);
    TemplateTracker(const Image& frame, const Box4& rect, const TrackerOptions& opt);
    ~TemplateTracker();
    TemplateTracker(TemplateTracker&&) noexcept;
    TemplateTracker& operator=(TemplateTracker&&) noexcept;
    // next frame. TS_OK: out/score found; TS_COAST: occluded, out = predicted box, score 0 (the web app
    // keeps no key for it, only score 0 once the target is found again); TS_LOST: out untouched.
    // out is rounded to 0.01 px and score to 0.01 (as the app stores them).
    TrackState track(const Image& frame, Box4& out, double& score);
    bool update(const Image& frame, Box4& out, double& score);   // track() != TS_LOST (coast -> true, score 0)
    int channel() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// template bank: remember the target's look at a manual key (only when the box is fully on screen)
void trackBankAdd(ClipMask& m, int f, const Image& frame, const Box4& rect);
std::vector<ImagePtr> trackBankOf(const ClipMask& m, int exceptFrame);

// project persistence (UTF-8 JSON) - stable across versions; unknown keys ignored
std::string clipToJson(const ClipProject& p);
bool clipFromJson(const std::string& s, ClipProject& p);

} // namespace bg
