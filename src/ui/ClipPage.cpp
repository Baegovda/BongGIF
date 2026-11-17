#include "ui/ClipPage.h"

#include "platform/ClipExport.h"
#include "platform/Media.h"
#include "ui/Lang.h"

#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace bg {

namespace {
const wchar_t* kEffects[] = {L"모자이크", L"블러", L"박스", L"확대", L"포탈"};
// colours of the original app
constexpr uint32_t kSel = 0xFFFF9F1A, kMaskC = 0xFF38BDF8, kCropC = 0xFFF4F4F5, kPortalC = 0xFFC084FC, kFreezeC = 0xFFFACC15,
                   kSegC = 0xB32DD4BF, kSlowC = 0xFF60A5FA, kRed = 0xFFEF4444, kGreen = 0xFF22C55E, kRangeC = 0x9922C55E,
                   kWave = 0xA638BDF8, kSpanC = 0x73FF9F1A;
constexpr double kMB = 1024.0 * 1024.0;

std::wstring fmtTime(double s) {
    s = std::max(0.0, s);
    int m = (int)(s / 60);
    wchar_t b[32];
    swprintf_s(b, L"%02d:%06.3f", m, s - m * 60);
    return b;
}
std::wstring fmtNum(double v, int dec) {
    wchar_t b[32];
    swprintf_s(b, L"%.*f", dec, v);
    std::wstring s = b;
    if (dec > 0) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    return s;
}
std::wstring fixed(double v, int dec) {
    wchar_t b[32];
    swprintf_s(b, L"%.*f", dec, v);
    return b;
}
// translated printf: the Korean format string is the dictionary key
std::wstring fmt(const wchar_t* ko, ...) {
    std::wstring f = tr(ko);
    wchar_t b[1024];
    va_list a;
    va_start(a, ko);
    vswprintf_s(b, f.c_str(), a);
    va_end(a);
    return b;
}
std::wstring describe(int width, int quality, double fps) {
    std::wstring w = width ? std::to_wstring(width) : tr(L"원본");
    return fmt(L"너비 %s · 화질 %d · %sfps", w.c_str(), quality, fmtNum(fps, 2).c_str());
}
uint64_t fnv(const std::wstring& s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) { h ^= (uint64_t)towlower(c); h *= 1099511628211ull; }
    return h;
}
std::vector<ClipMask> scaledMasks(const std::vector<ClipMask>& ms, double s) {
    std::vector<ClipMask> out = ms;
    for (auto& m : out) {
        for (auto& [k, b] : m.keys) for (auto& v : b) v *= s;
        for (auto& [k, b] : m.destKeys) for (auto& v : b) v *= s;
        m.pad = (int)std::lround(m.pad * s);
        m.border = std::max(m.border > 0 ? 1 : 0, (int)std::lround(m.border * s));
        m.strength = std::max(1, (int)std::lround(m.strength * s));
    }
    return out;
}
uint32_t A(uint32_t c, float a) { return ((uint32_t)std::lround(((c >> 24) & 255) * std::clamp(a, 0.f, 1.f)) << 24) | (c & 0xFFFFFF); }
bool ctrlDown() { return GetKeyState(VK_CONTROL) & 0x8000; }
bool shiftDown() { return GetKeyState(VK_SHIFT) & 0x8000; }
bool altDown() { return GetKeyState(VK_MENU) & 0x8000; }
double round2(double v) { return std::round(v * 100) / 100; }

// dashed drawing (canvas setLineDash equivalents)
void dashLine(Gfx& g, float x1, float y1, float x2, float y2, uint32_t c, float w, float on, float off) {
    float dx = x2 - x1, dy = y2 - y1, len = std::hypot(dx, dy);
    if (len < 0.5f) return;
    if (on <= 0) { g.line(x1, y1, x2, y2, c, w); return; }
    float ux = dx / len, uy = dy / len;
    for (float t = 0; t < len; t += on + off) {
        float e = std::min(len, t + on);
        g.line(x1 + ux * t, y1 + uy * t, x1 + ux * e, y1 + uy * e, c, w);
    }
}
void boxLine(Gfx& g, RectF v, uint32_t c, float w, float on = 0, float off = 0) {
    if (on <= 0) { g.stroke(v, 0, c, w); return; }
    dashLine(g, v.x, v.y, v.r(), v.y, c, w, on, off);
    dashLine(g, v.r(), v.y, v.r(), v.b(), c, w, on, off);
    dashLine(g, v.r(), v.b(), v.x, v.b(), c, w, on, off);
    dashLine(g, v.x, v.b(), v.x, v.y, c, w, on, off);
}
void ellipseLine(Gfx& g, RectF v, uint32_t c, float w, bool dashed) {
    float cx = v.x + v.w / 2, cy = v.y + v.h / 2, rx = std::fabs(v.w / 2), ry = std::fabs(v.h / 2);
    const int n = 64;
    for (int k = 0; k < n; ++k) {
        if (dashed && (k & 1)) continue;
        double a0 = k * 6.2831853 / n, a1 = (k + 1) * 6.2831853 / n;
        g.line(cx + rx * (float)cos(a0), cy + ry * (float)sin(a0), cx + rx * (float)cos(a1), cy + ry * (float)sin(a1), c, w);
    }
}
void handles(Gfx& g, RectF v, uint32_t c) {
    for (float hx : {v.x, v.x + v.w / 2, v.r()})
        for (float hy : {v.y, v.y + v.h / 2, v.b()}) {
            if (hx == v.x + v.w / 2 && hy == v.y + v.h / 2) continue;
            RectF h{hx - 3.5f, hy - 3.5f, 7, 7};
            g.fill(h, 0, c);
            g.stroke(h, 0, 0xFF000000, 1);
        }
}

// Size fit: the best candidate (from index `from` on) whose estimate is within 92% of target.
bool findFit(const std::wstring& path, double fps, const ClipProject& p, size_t target, int from, const std::vector<FitCandidate>& c,
             const std::function<void(const FitCandidate&)>& onStep, const std::function<bool()>& cancel, FitCandidate& out,
             size_t& est, bool& fits, std::string& err) {
    if (c.empty()) { err = "no candidates"; return false; }
    std::map<int, size_t> cache;
    bool fail = false;
    auto E = [&](int i) -> size_t {
        auto it = cache.find(i);
        if (it != cache.end()) return it->second;
        onStep(c[i]);
        ClipProject q = p;
        q.ex.width = c[i].width;
        q.ex.quality = c[i].quality;
        q.ex.fps = c[i].fps;
        size_t b = 0;
        if (!estimateClipBytes(path, fps, q, b, cancel, err)) fail = true;
        cache[i] = b;
        return b;
    };
    double limit = target * 0.92;
    int n = (int)c.size(), lo = std::clamp(from, 0, n - 1), hi = n - 1;
    size_t e = E(lo);
    if (fail) return false;
    if (e <= limit) { out = c[lo]; est = e; fits = true; return true; }
    e = E(hi);
    if (fail) return false;
    if (e > limit) { out = c[hi]; est = e; fits = false; return true; }
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        e = E(mid);
        if (fail) return false;
        if (e <= limit) hi = mid;
        else lo = mid;
    }
    out = c[hi];
    est = E(hi);
    fits = true;
    return true;
}
}   // namespace

ClipPage::ClipPage(HWND hwnd, Gui& gui, Settings& st, std::function<void(const std::wstring&)> status)
    : hwnd_(hwnd), gui_(gui), st_(st), status_(std::move(status)) {}

ClipPage::~ClipPage() {
    stopWorkers();
    if (dirty_) saveProject();
}

void ClipPage::stopWorkers() {
    trackStop_ = true;
    jobCancel_ = true;
    if (trackThread_.joinable()) trackThread_.join();
    if (jobThread_.joinable()) jobThread_.join();
    if (audioThread_.joinable()) audioThread_.join();
    trackDone_ = true;
    jobDone_ = true;
    trackImg_.reset();
    trackHasShow_ = false;
}

bool ClipPage::needsTick() const {
    return playing_ || !trackDone_ || !jobDone_ || audioThread_.joinable() || dirty_ || trackThread_.joinable() ||
           jobThread_.joinable();
}

void ClipPage::leave() { stopPlay(); }

// ================================================================== open
void ClipPage::openDialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = hwnd_;
    of.lpstrFilter = L"영상\0*.mp4;*.mov;*.m4v;*.mkv;*.webm;*.avi;*.wmv\0모든 파일\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&of)) open(file);
}

void ClipPage::open(const std::wstring& path) {
    if (dirty_) saveProject();
    stopPlay();
    stopWorkers();
    std::string err;
    if (!src_.open(path, err, 64)) {
        messageBox(hwnd_, tr(L"영상을 열 수 없어요.") + L"\n" + widen(err), false, true);
        return;
    }
    path_ = path;
    size_t sl = path.find_last_of(L"\\/");
    stem_ = path.substr(sl == std::wstring::npos ? 0 : sl + 1);
    size_t dot = stem_.find_last_of(L'.');
    if (dot != std::wstring::npos) stem_ = stem_.substr(0, dot);
    info_ = src_.info();
    fps_ = info_.fps;
    count_ = std::max(1, info_.count);
    P_ = newClipProject(count_, fps_);
    sel_ = -1;
    // autosaved project for this file (range clamped to the video)
    {
        std::ifstream in(projectFile(), std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            ClipProject p = P_;
            if (clipFromJson(ss.str(), p)) {
                p.inFrame = std::clamp(p.inFrame, 0, count_ - 1);
                p.outFrame = std::clamp(p.outFrame, p.inFrame, count_ - 1);
                P_ = p;
                status_(tr(L"이 영상에서 하던 작업을 불러왔어요"));
            }
        }
    }
    sel_ = P_.masks.empty() ? -1 : 0;
    f_ = P_.inFrame;
    undo_.clear();
    redo_.clear();
    undoTag_.clear();
    zoom_ = 1;
    cx_ = info_.w / 2.0;
    cy_ = info_.h / 2.0;
    tlLo_ = 0;
    tlHi_ = std::max(1, count_ - 1);
    mode_ = 0;
    drag_ = {};
    pan_ = tlPan_ = false;
    slowStart_ = -1;
    segSel_ = slowSel_ = -1;
    holdUntil_ = 0;
    resumeDir_ = 0;
    trackLabel_.clear();
    fitLabel_.clear();
    levels_.clear();
    events_.clear();
    audioSamples_.clear();
    shown_.reset();
    shownF_ = -1;
    playFrame_.reset();
    // player for sound and smooth playback (frames up to 1920 wide)
    player_ = std::make_unique<VideoPlayer>();
    int pw = std::min(info_.w, 1920), ph = std::min(info_.h, 1920);
    std::string perr;
    if (!player_->open(path, pw, ph, perr)) player_.reset();
    loaded_ = true;
    version_++;
    analyzeAudio();
    gui_.redraw();
}

std::wstring ClipPage::projectFile() const {
    wchar_t buf[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = std::wstring(buf) + L"\\BongGIF";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\clips";
    CreateDirectoryW(dir.c_str(), nullptr);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fa);
    wchar_t key[96];
    swprintf_s(key, L"%016llx_%08lx%08lx_%08lx", (unsigned long long)fnv(path_), fa.nFileSizeHigh, fa.nFileSizeLow,
               fa.ftLastWriteTime.dwLowDateTime);
    return dir + L"\\" + key + L".json";
}

void ClipPage::saveProject() {
    dirty_ = false;
    if (!loaded_) return;
    std::ofstream out(projectFile(), std::ios::binary | std::ios::trunc);
    out << clipToJson(P_);
}

// ================================================================== undo
// Same tag as the previous push = merged into one step (typing in a field, dragging a value).
void ClipPage::pushUndo(const std::string& tag) {
    if (!tag.empty() && tag == undoTag_) return;
    undo_.push_back(clipToJson(P_) + "\n#" + std::to_string(sel_));
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    undoTag_ = tag;
}
static void restoreSnap(const std::string& s, ClipProject& p, int& sel) {
    size_t k = s.rfind("\n#");
    ClipProject q = p;
    if (clipFromJson(s.substr(0, k), q)) p = q;
    sel = std::min(sel, (int)p.masks.size() - 1);
}
void ClipPage::undo() {
    if (undo_.empty() || trackThread_.joinable()) return;
    redo_.push_back(clipToJson(P_) + "\n#" + std::to_string(sel_));
    restoreSnap(undo_.back(), P_, sel_);
    undo_.pop_back();
    undoTag_.clear();
    changed();
    saveProject();
}
void ClipPage::redo() {
    if (redo_.empty() || trackThread_.joinable()) return;
    undo_.push_back(clipToJson(P_) + "\n#" + std::to_string(sel_));
    restoreSnap(redo_.back(), P_, sel_);
    redo_.pop_back();
    undoTag_.clear();
    changed();
    saveProject();
}
void ClipPage::changed() {
    version_++;
    dirty_ = true;
    dirtyAt_ = GetTickCount64();
    gui_.redraw();
}

// ================================================================== navigation / playback
void ClipPage::ensureVisible(int f) {
    double span = tlHi_ - tlLo_;
    if (f < tlLo_ || f > tlHi_) {
        tlLo_ = std::min(std::max(0.0, f - span * 0.1), std::max(0.0, count_ - 1 - span));
        tlHi_ = tlLo_ + span;
    }
}
void ClipPage::seek(int f) {
    if (!loaded_ || trackThread_.joinable()) return;
    f_ = std::clamp(f, 0, count_ - 1);
    ensureVisible(f_);
    if (playing_ && player_) {   // seeking while playing continues from there
        holdUntil_ = 0;
        player_->seek(src_.frameTime(f_) + 0.25 / fps_);
        player_->play();
    }
    gui_.redraw();
}
void ClipPage::stepFrames(int d) {
    stopPlay();
    seek(f_ + d);
}
std::vector<ClipRange> ClipPage::playSegments() const {
    if (!segPlay_ || P_.segments.empty()) return {};
    return clipRanges(P_);
}
void ClipPage::play() {
    if (!loaded_ || !player_ || playing_ || trackThread_.joinable() || jobThread_.joinable()) return;
    auto segs = playSegments();
    if (!segs.empty()) {
        int hit = -1;
        for (size_t i = 0; i < segs.size(); ++i)
            if (segs[i].lo <= f_ && f_ < segs[i].hi) { hit = (int)i; break; }
        segIdx_ = hit >= 0 ? hit : 0;
        if (hit < 0) f_ = segs[0].lo;
    } else if (f_ >= P_.outFrame && loopRange_) f_ = P_.inFrame;
    player_->setMuted(!sound_);
    player_->setVolume(volume_ / 100.0);
    rate_ = speedAt(P_, f_);
    player_->setRate(rate_);
    player_->seek(src_.frameTime(f_) + 0.25 / fps_);
    player_->play();
    playing_ = true;
    holdUntil_ = 0;
    playFrame_.reset();
}
void ClipPage::stopPlay() {
    if (!playing_) return;
    playing_ = false;
    holdUntil_ = 0;
    if (player_) player_->pause();
    playFrame_.reset();
    shownF_ = -1;
    gui_.redraw();
}
void ClipPage::togglePlay() {
    if (trackThread_.joinable()) { stopTrack(); return; }
    if (playing_) stopPlay();
    else play();
}
// jump during playback (range loop, next range, freeze point)
void ClipPage::jumpPlay(int f) {
    f_ = f;
    player_->seek(src_.frameTime(f) + 0.25 / fps_);
    ensureVisible(f);
    auto fz = P_.freezes.find(f);
    if (fz != P_.freezes.end()) {
        player_->pause();
        holdUntil_ = GetTickCount64() + (ULONGLONG)(fz->second * 1000);
        return;
    }
    rate_ = speedAt(P_, f);
    player_->setRate(rate_);
    player_->play();
}

// ================================================================== tick
void ClipPage::tick() {
    if (playing_ && player_) {
        if (auto im = player_->frame()) playFrame_ = im;
        if (holdUntil_) {
            if (GetTickCount64() >= holdUntil_) {
                holdUntil_ = 0;
                rate_ = speedAt(P_, f_);
                player_->setRate(rate_);
                player_->play();
            }
        } else {
            int fr = std::clamp(src_.frameAt(player_->time()), 0, count_ - 1);
            bool ended = player_->paused();
            if (fr != f_ || ended) {
                int prev = f_, wrap = -1;
                bool stop = false;
                auto segs = playSegments();
                if (!segs.empty()) {
                    segIdx_ = std::min(segIdx_, (int)segs.size() - 1);
                    if (fr > segs[segIdx_].hi || ended) {
                        int next = segIdx_ + 1;
                        if (next >= (int)segs.size()) {
                            if (!loopRange_) stop = true;
                            next = 0;
                        }
                        segIdx_ = next;
                        wrap = segs[next].lo;
                    }
                } else {
                    int end = loopRange_ ? P_.outFrame : count_ - 1;
                    if (fr > end || ended) {
                        if (!loopRange_) stop = true;
                        wrap = P_.inFrame;
                    }
                }
                if (stop) stopPlay();
                else if (wrap >= 0) jumpPlay(wrap);
                else {
                    auto fz = P_.freezes.upper_bound(prev);   // first freeze point crossed
                    if (fz != P_.freezes.end() && fz->first <= fr) jumpPlay(fz->first);
                    else {
                        f_ = fr;
                        double r = speedAt(P_, f_);
                        if (r != rate_) { rate_ = r; player_->setRate(r); }
                        ensureVisible(f_);
                    }
                }
            }
        }
    }
    // tracking: show the frame being tracked, apply the result when done
    if (trackThread_.joinable()) {
        {
            std::lock_guard<std::mutex> l(trackMu_);
            if (trackImg_) f_ = trackImgF_;
        }
        ensureVisible(f_);
        if (trackBetween_) {
            bool fwd = trackPhase_ != 2;
            int a = fwd ? trackOrigin_ : trackStopF_, b = fwd ? trackStopF_ : trackOrigin_;
            trackLabel_ = fmt(fwd ? L"키 사이 추적 1/2 (앞으로)… %d (%d → %d) · Esc로 중지" : L"키 사이 추적 2/2 (거꾸로)… %d (%d → %d) · Esc로 중지",
                              f_, a, b);
        } else
            trackLabel_ = fmt(L"추적 중… %d (%d → %d) · 화면 클릭: 멈추고 박스 고치기 · Esc로 중지", f_, trackOrigin_, trackStopF_);
        if (trackDone_) {
            trackThread_.join();
            finishTrack();
        }
    }
    // export / fit
    if (jobThread_.joinable() && jobDone_) {
        jobThread_.join();
        if (jobApply_) {
            pushUndo();
            P_.ex.width = jobCand_.width;
            P_.ex.quality = jobCand_.quality;
            P_.ex.fps = jobCand_.fps;
            changed();
        }
        if (jobKind_ == 2) fitLabel_ = jobResult_;
        else if (!jobResult_.empty()) status_(jobResult_);
        shownF_ = -1;
    }
    // audio analysis
    if (audioThread_.joinable() && audioDone_) {
        audioThread_.join();
        audioStatus_ = audioMsg_;
        if (!audioOut_.empty()) { levels_ = audioOut_; recomputeEvents(); }
    }
    if (dirty_ && GetTickCount64() - dirtyAt_ > 400) saveProject();
    gui_.redraw();
}

// ================================================================== masks
ClipMask* ClipPage::selMask() { return sel_ >= 0 && sel_ < (int)P_.masks.size() ? &P_.masks[sel_] : nullptr; }

void ClipPage::selectMask(int i) {
    sel_ = i >= 0 && i < (int)P_.masks.size() ? i : -1;
    undoTag_.clear();
    gui_.redraw();
}

bool ClipPage::maskRect(int i, Box4& out) const {
    const ClipMask& m = P_.masks[i];
    return maskActive(m, f_) && interpBox(m.keys, f_, out);
}
bool ClipPage::destRect(int i, Box4& out) const {
    const ClipMask& m = P_.masks[i];
    return m.effect == CE_PORTAL && maskActive(m, f_) && interpBox(m.destKeys, f_, out);
}

void ClipPage::setProp(const char* name, const std::function<void(ClipMask&)>& fn) {
    ClipMask* m = selMask();
    if (!m) return;
    pushUndo(std::to_string(sel_) + ":" + name);
    fn(*m);
    changed();
}

void ClipPage::addMask(const Box4& r) {
    pushUndo();
    ClipMask m = newClipMask(tr(L"마스크") + L" " + std::to_wstring(P_.masks.size() + 1), selMask());
    m.start = 0;
    m.end = count_ - 1;
    m.keys[f_] = r;
    if (m.effect == CE_PORTAL) m.destKeys[f_] = defaultDest(r, info_.w, info_.h);
    P_.masks.push_back(m);
    sel_ = (int)P_.masks.size() - 1;
    undoTag_.clear();
    changed();
}

void ClipPage::setCrop(const Box4& r) {
    int b[4];
    if (!clipRect(r, 0, info_.w, info_.h, b)) return;
    pushUndo();
    P_.hasCrop = true;
    P_.crop = {(double)b[0], (double)b[1], (double)(b[2] - b[0]), (double)(b[3] - b[1])};
    changed();
}

void ClipPage::keyAdd() {
    ClipMask* m = selMask();
    Box4 b;
    if (!m || !interpBox(m->keys, f_, b)) return;
    pushUndo();
    m->keys[f_] = b;
    m->trackScore.erase(f_);
    changed();
}
void ClipPage::keyDel() {
    ClipMask* m = selMask();
    if (!m || !m->keys.count(f_) || m->keys.size() < 2) return;
    pushUndo();
    m->keys.erase(f_);
    m->trackScore.erase(f_);
    changed();
}
void ClipPage::gotoKey(int dir) {
    ClipMask* m = selMask();
    if (!m || m->keys.empty()) return;
    if (dir > 0) { auto it = m->keys.upper_bound(f_); if (it != m->keys.end()) seek(it->first); }
    else { auto it = m->keys.lower_bound(f_); if (it != m->keys.begin()) seek(std::prev(it)->first); }
}
// deletes every key before / after this frame; the box here stays as a key
void ClipPage::clearKeysSide(int dir) {
    ClipMask* m = selMask();
    if (!m) return;
    Box4 anchor;
    bool has = interpBox(m->keys, f_, anchor);
    pushUndo();
    int lo = dir < 0 ? INT_MIN : f_ + 1, hi = dir < 0 ? f_ - 1 : INT_MAX;
    clearKeys(m->keys, lo, hi);
    for (auto it = m->trackScore.begin(); it != m->trackScore.end();)
        it = (it->first >= lo && it->first <= hi) ? m->trackScore.erase(it) : std::next(it);
    if (has) m->keys[f_] = anchor;
    m->trackScore.erase(f_);
    changed();
}
void ClipPage::deleteMask() {
    if (!selMask()) return;
    pushUndo();
    P_.masks.erase(P_.masks.begin() + sel_);
    sel_ = std::min(sel_, (int)P_.masks.size() - 1);
    changed();
}
void ClipPage::moveMask(int d) {
    int j = sel_ + d;
    if (!selMask() || j < 0 || j >= (int)P_.masks.size()) return;
    pushUndo();
    std::swap(P_.masks[sel_], P_.masks[j]);
    sel_ = j;
    changed();
}
void ClipPage::duplicateMask() {
    ClipMask* m = selMask();
    if (!m) return;
    pushUndo();
    ClipMask c = *m;
    c.name += L" 복사";
    P_.masks.insert(P_.masks.begin() + sel_ + 1, c);
    sel_++;
    changed();
}
void ClipPage::setIn() {
    pushUndo();
    P_.inFrame = f_;
    P_.outFrame = std::max(P_.outFrame, f_);
    changed();
}
void ClipPage::setOut() {
    pushUndo();
    P_.outFrame = f_;
    P_.inFrame = std::min(P_.inFrame, f_);
    changed();
}
void ClipPage::toggleCrop() {
    mode_ = mode_ == 1 ? 0 : 1;
    status_(mode_ == 1 ? tr(L"자르기: 영상 위를 드래그해 내보낼 영역을 지정하세요.") : L"");
    gui_.redraw();
}
void ClipPage::clearCrop() {
    if (!P_.hasCrop) return;
    pushUndo();
    P_.hasCrop = false;
    changed();
}

// ================================================================== tracking
// One pass from `origin` (box `rect`) towards `stop`. Frames where the target is hidden (the
// tracker only predicts) are not kept as keys; if it shows up again they get score 0 ("check"),
// if it never does the pass ends as lost at the first hidden frame.
ClipPage::TrackPass ClipPage::runPass(ClipSource& s, int origin, const Box4& rect, int stop, int dir, const TrackerOptions& opt) {
    TrackPass res;
    ImagePtr f0 = s.frame(origin);
    if (!f0) { res.status = 3; res.err = tr(L"프레임을 읽을 수 없습니다"); return res; }
    TemplateTracker tracker(*f0, rect, opt);
    std::vector<int> coasted;
    for (int f = origin + dir; dir > 0 ? f <= stop : f >= stop; f += dir) {
        if (trackStop_) { res.status = 1; break; }
        if (dir < 0 && (origin - f) % 64 == 1) {   // backwards: decode the next block forward once
            int b0 = std::max(stop, f - 63);
            for (int k = b0; k <= f && !trackStop_; ++k) s.frame(k);
        }
        ImagePtr im = s.frame(f);
        if (!im) { res.status = 3; res.err = tr(L"프레임을 읽을 수 없습니다"); break; }
        Box4 r;
        double sc = 0;
        TrackState ts = tracker.track(*im, r, sc);
        bool ok = ts != TS_LOST;
        if (ts == TS_OK) {
            for (int g : coasted) res.r.scores[g] = 0;
            coasted.clear();
            for (auto& v : r) v = round2(v);
            res.r.rects[f] = r;
            res.r.scores[f] = round2(sc);
        } else if (ok) coasted.push_back(f);
        else {
            res.status = 2;
            res.lostAt = coasted.empty() ? f : coasted[0];
            break;
        }
        std::lock_guard<std::mutex> l(trackMu_);
        trackImg_ = im;
        trackImgF_ = f;
        trackShow_ = r;
        trackHasShow_ = true;
    }
    if (!coasted.empty() && res.status == 0) { res.status = 2; res.lostAt = coasted[0]; }
    return res;
}

// writes a result into the mask: keys and scores in its frame span are replaced, then smoothed
bool ClipPage::applyTrack(ClipMask& m, const TrackResult& r, int& lo, int& hi) {
    lo = INT_MAX;
    hi = INT_MIN;
    if (!r.rects.empty()) { lo = r.rects.begin()->first; hi = r.rects.rbegin()->first; }
    if (!r.scores.empty()) { lo = std::min(lo, r.scores.begin()->first); hi = std::max(hi, r.scores.rbegin()->first); }
    if (lo > hi) return false;
    clearKeys(m.keys, lo, hi);
    for (auto it = m.trackScore.begin(); it != m.trackScore.end();)
        it = (it->first >= lo && it->first <= hi) ? m.trackScore.erase(it) : std::next(it);
    for (auto& [f, b] : r.rects) m.keys[f] = b;
    for (auto& [f, s] : r.scores) m.trackScore[f] = s;
    if (smooth_) smoothKeys(m, lo, hi);
    return true;
}

std::wstring ClipPage::weakNote(const ClipMask& m, bool has, int lo, int hi) {
    int n = has ? weakCount(m, lo, hi) : 0;
    return n ? fmt(L" · 확인 필요 %d곳 (W로 이동)", n) : L"";
}

void ClipPage::startTrack(int dir) {
    if (!loaded_ || trackThread_.joinable() || jobThread_.joinable()) return;
    ClipMask* m = selMask();
    Box4 b;
    if (!m || !interpBox(m->keys, f_, b)) return;
    stopPlay();
    int last = count_ - 1, stop;
    if (dir > 0) {
        stop = std::min(m->end, last);
        if (f_ < P_.outFrame) stop = std::min(stop, P_.outFrame);
        if (stop <= f_) return;
    } else {
        stop = std::max(m->start, 0);
        if (f_ > P_.inFrame) stop = std::max(stop, P_.inFrame);
        if (stop >= f_) return;
    }
    pushUndo();
    resumeDir_ = 0;
    m->keys[f_] = b;
    m->trackScore.erase(f_);
    trackDir_ = dir;
    trackMask_ = sel_;
    trackOrigin_ = f_;
    trackStopF_ = stop;
    trackFix_ = -1;
    trackBetween_ = false;
    trackStop_ = false;
    trackDone_ = false;
    trackHasShow_ = false;
    trackT0_ = GetTickCount64();
    std::wstring path = path_;
    double fps = fps_;
    TrackerOptions opt;
    opt.fixedSize = fixedSize_;
    opt.fps = fps_;
    int origin = f_;
    if (ClipMask* bm = selMask()) {   // remember this look of the target, try the others when it changes
        if (ImagePtr fr = src_.frame(origin)) trackBankAdd(*bm, origin, *fr, b);
        opt.bank = trackBankOf(*bm, origin);
    }
    trackThread_ = std::thread([this, path, fps, opt, origin, stop, dir, b] {
        ClipSource s;
        std::string e;
        TrackPass res;
        if (s.open(path, e, 96)) {
            s.setFps(fps);
            res = runPass(s, origin, b, stop, dir, opt);
        } else {
            res.status = 3;
            res.err = widen(e);
        }
        {
            std::lock_guard<std::mutex> l(trackMu_);
            trackRes_ = std::move(res);
        }
        trackDone_ = true;
    });
}

// Re-tracks only between this frame (key A) and the next manual key B: forward from A, backward
// from B, blended with more weight on the nearer key. Other keys are left alone.
void ClipPage::startTrackBetween() {
    if (!loaded_ || trackThread_.joinable() || jobThread_.joinable()) return;
    ClipMask* m = selMask();
    Box4 ba;
    if (!m || !interpBox(m->keys, f_, ba)) return;
    int b = -1;
    for (int k : manualKeys(*m)) if (k > f_) { b = k; break; }
    if (b < 0 || b - f_ < 2) {
        trackLabel_ = tr(L"이 프레임 뒤에 직접 놓은 키가 없습니다. 끝 지점에서 박스를 맞추거나 K로 키를 만든 뒤, 시작 지점에서 다시 누르세요.");
        return;
    }
    stopPlay();
    pushUndo();
    resumeDir_ = 0;
    m->keys[f_] = ba;
    m->trackScore.erase(f_);
    Box4 bb = m->keys[b];
    trackMask_ = sel_;
    trackBetween_ = true;
    trackOrigin_ = f_;
    trackStopF_ = b;
    trackFix_ = -1;
    trackPhase_ = 1;
    trackStop_ = false;
    trackDone_ = false;
    trackHasShow_ = false;
    trackT0_ = GetTickCount64();
    std::wstring path = path_;
    double fps = fps_;
    TrackerOptions opt;
    opt.fixedSize = fixedSize_;
    opt.fps = fps_;
    int a = f_;
    if (ClipMask* bm = selMask()) {
        if (ImagePtr fr = src_.frame(a)) trackBankAdd(*bm, a, *fr, ba);
        if (ImagePtr fr = src_.frame(b)) trackBankAdd(*bm, b, *fr, bb);
        opt.bank = trackBankOf(*bm, -1);
    }
    trackThread_ = std::thread([this, path, fps, opt, a, b, ba, bb] {
        ClipSource s;
        std::string e;
        TrackPass out;
        if (s.open(path, e, 96)) {
            s.setFps(fps);
            TrackPass fwd = runPass(s, a, ba, b - 1, 1, opt), bwd;
            bwd.status = 1;
            if (!trackStop_ && fwd.status != 3) {
                trackPhase_ = 2;
                bwd = runPass(s, b, bb, a + 1, -1, opt);
            }
            out.r = blendTracks(fwd.r, bwd.r, a, b);
            out.status = trackStop_ ? 1 : 0;
            for (auto* p : {&fwd, &bwd})
                if (p->status == 3) { out.status = 3; out.err = p->err; }
        } else {
            out.status = 3;
            out.err = widen(e);
        }
        {
            std::lock_guard<std::mutex> l(trackMu_);
            trackRes_ = std::move(out);
        }
        trackDone_ = true;
    });
}

void ClipPage::stopTrack() { trackStop_ = true; }

void ClipPage::finishTrack() {
    TrackPass res;
    {
        std::lock_guard<std::mutex> l(trackMu_);
        res = std::move(trackRes_);
        trackRes_ = {};
        trackImg_.reset();
        trackHasShow_ = false;
    }
    shownF_ = -1;
    if (trackMask_ < 0 || trackMask_ >= (int)P_.masks.size()) return;
    ClipMask& m = P_.masks[trackMask_];
    wchar_t secs[32];
    swprintf_s(secs, L"%.1f", (GetTickCount64() - trackT0_) / 1000.0);
    int lo = 0, hi = 0;
    if (trackBetween_) {
        int A = trackOrigin_, B = trackStopF_;
        bool has = applyTrack(m, res.r, lo, hi);
        int n = (int)res.r.rects.size();
        if (res.status == 3) trackLabel_ = fmt(L"추적 중 오류로 멈췄습니다 (%d프레임 저장): %s", n, res.err.c_str());
        else if (res.status == 1) trackLabel_ = fmt(L"중지됨: %d프레임 저장%s", n, weakNote(m, has, lo, hi).c_str());
        else trackLabel_ = fmt(L"키 사이 추적 완료: %d → %d, %d프레임 (%s초)%s", A, B, n, secs, weakNote(m, has, lo, hi).c_str());
        changed();
        int weak = has ? nextWeak(m, A, 1) : -1;
        f_ = weak >= 0 && weak < B ? weak : A;
        ensureVisible(f_);
        return;
    }
    int dir = trackDir_;
    if (trackFix_ >= 0) {   // clicked: keep results up to that frame only (the rest is re-tracked from the fixed box)
        auto beyond = [&](int f) { return dir > 0 ? f > trackFix_ : f < trackFix_; };
        for (auto it = res.r.rects.begin(); it != res.r.rects.end();) it = beyond(it->first) ? res.r.rects.erase(it) : std::next(it);
        for (auto it = res.r.scores.begin(); it != res.r.scores.end();) it = beyond(it->first) ? res.r.scores.erase(it) : std::next(it);
        if (res.status == 2 && beyond(res.lostAt)) res.status = 1;
    }
    bool has = applyTrack(m, res.r, lo, hi);
    int n = (int)res.r.rects.size();
    std::wstring note = weakNote(m, has, lo, hi);
    switch (res.status) {
    case 0: trackLabel_ = fmt(L"추적 완료: %d프레임 (%s초)%s", n, secs, note.c_str()); break;
    case 1: trackLabel_ = fmt(L"중지됨: %d프레임 저장%s · 어긋난 곳에서 박스를 고친 뒤 T로 이어서 추적하세요.", n, note.c_str()); break;
    case 2:
        // clang-format off
        trackLabel_ = fmt(L"%d 프레임에서 대상이 사라졌습니다(가려짐·화면 밖·흐려짐). 비슷한 다른 물체로 옮겨 가지 않도록 여기서 멈췄습니다. 다시 보이는 곳에서 박스를 맞추고 T로 이어서 추적하세요.%s", res.lostAt, note.c_str());
        // clang-format on
        break;
    default: trackLabel_ = fmt(L"추적 중 오류로 멈췄습니다 (%d프레임 저장): %s", n, res.err.c_str()); break;
    }
    int target = res.status == 2 ? res.lostAt
                 : !res.r.rects.empty() ? (dir > 0 ? res.r.rects.rbegin()->first : res.r.rects.begin()->first)
                                        : trackOrigin_;
    if (trackFix_ >= 0 && res.status != 3) {
        target = trackFix_;
        resumeDir_ = dir;
        trackLabel_ = fmt(L"%d 프레임에서 멈췄습니다. 박스를 끌어 고치면 거기서부터 이어서 추적합니다 (Esc: 그만두기).", trackFix_);
    }
    changed();
    f_ = std::clamp(target, 0, count_ - 1);
    ensureVisible(f_);
}

void ClipPage::jumpWeak(int dir) {
    ClipMask* m = selMask();
    if (!m || !loaded_ || trackThread_.joinable()) return;
    int f = nextWeak(*m, f_, dir);
    if (f < 0) {
        trackLabel_ = tr(dir > 0 ? L"이후에 확인할 곳이 없습니다." : L"이전에 확인할 곳이 없습니다.");
        return;
    }
    stopPlay();
    seek(f);
    trackLabel_ = fmt(L"%d 프레임: 추적이 불확실한 곳입니다. 박스가 어긋났으면 고친 뒤 T 또는 B(키 사이)로 다시 추적하세요.", f);
}

// ================================================================== audio
void ClipPage::analyzeAudio() {
    if (audioThread_.joinable()) return;
    audioStatus_ = tr(L"오디오 파형 분석 중…");
    audioDone_ = false;
    std::wstring path = path_;
    double fps = fps_;
    int count = count_;
    bool hasAudio = info_.hasAudio;
    audioThread_ = std::thread([this, path, fps, count, hasAudio] {
        std::string e;
        std::vector<float> db;
        if (clipAudioLevels(path, fps, count, db, e)) {   // per-frame levels the way the web app measures them
            audioOut_ = std::move(db);
            audioMsg_.clear();
        } else {
            audioOut_.clear();
            audioMsg_ = hasAudio && !e.empty() ? fmt(L"오디오를 읽을 수 없습니다 (%s).", widen(e).c_str())
                                               : tr(L"이 영상에는 오디오 트랙이 없습니다.");
        }
        audioDone_ = true;
    });
}
void ClipPage::recomputeEvents() {
    if (levels_.empty()) return;
    events_ = findEvents(levels_, fps_, rise_, floor_);
    audioStatus_ = fmt(L"소리 이벤트 %d개 (타임라인의 빨간 선) · N / Shift+N 으로 이동", (int)events_.size());
}
void ClipPage::gotoEvent(int dir) {
    int c = -1;
    if (dir > 0) { for (int e : events_) if (e > f_) { c = e; break; } }
    else for (auto it = events_.rbegin(); it != events_.rend(); ++it) if (*it < f_) { c = *it; break; }
    if (c < 0) return;
    stopPlay();
    seek(c);
}

// ================================================================== ranges
// 0 add, 1 update, 2 delete, 3 up, 4 down
void ClipPage::segAction(int act) {
    auto& segs = P_.segments;
    if (act == 0) {
        pushUndo();
        segs.push_back({P_.inFrame, P_.outFrame});
        segSel_ = (int)segs.size() - 1;
    } else if (!(segSel_ >= 0 && segSel_ < (int)segs.size())) return;
    else if (act == 1) {
        pushUndo();
        segs[segSel_] = {P_.inFrame, P_.outFrame};
    } else if (act == 2) {
        pushUndo();
        segs.erase(segs.begin() + segSel_);
        segSel_ = std::min(segSel_, (int)segs.size() - 1);
    } else {
        int j = segSel_ + (act == 3 ? -1 : 1);
        if (j < 0 || j >= (int)segs.size()) return;
        pushUndo();
        std::swap(segs[segSel_], segs[j]);
        segSel_ = j;
    }
    changed();
}
void ClipPage::markSlow() {
    if (!loaded_) return;
    if (slowStart_ < 0) {
        slowStart_ = f_;
        status_(fmt(L"슬로우모션 시작 %d. 끝 프레임에서 S를 한 번 더 누르세요 (Esc 취소).", f_));
        gui_.redraw();
        return;
    }
    int lo = std::min(slowStart_, f_), hi = std::max(slowStart_, f_);
    slowStart_ = -1;
    pushUndo();
    P_.slowmos.push_back({lo, hi, round2(slowRate_)});
    slowSel_ = (int)P_.slowmos.size() - 1;
    status_(L"");
    changed();
}
void ClipPage::toggleFreeze() {
    if (!loaded_) return;
    pushUndo();
    if (P_.freezes.count(f_)) P_.freezes.erase(f_);
    else P_.freezes[f_] = freezeSec_ > 0 ? freezeSec_ : 1.5;
    changed();
}

// ================================================================== export
int ClipPage::cropWidth() const {
    int cb[4];
    cropBox(P_, info_.w, info_.h, cb);
    return cb[2] - cb[0];
}

// Exports; when a target size is set and the file is bigger, retries (up to twice) with the
// best lower setting that fits, and keeps that setting.
void ClipPage::exportWebp() {
    if (!loaded_ || trackThread_.joinable() || jobThread_.joinable()) return;
    stopPlay();
    std::wstring name = exportFilename(stem_, clipRanges(P_));
    wchar_t file[MAX_PATH] = {};
    wcsncpy_s(file, name.c_str(), _TRUNCATE);
    std::wstring dir = path_.substr(0, path_.find_last_of(L"\\/"));
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = hwnd_;
    of.lpstrFilter = L"WebP\0*.webp\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrInitialDir = dir.c_str();
    of.lpstrDefExt = L"webp";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    if (!GetSaveFileNameW(&of)) return;
    std::wstring out = file;
    jobKind_ = 1;
    jobCancel_ = false;
    jobDone_ = false;
    jobApply_ = false;
    jobK_ = 0;
    jobN_ = 1;
    jobTitle_ = tr(L"WebP 만드는 중");
    jobMsg_.clear();
    jobResult_.clear();
    ClipProject p = P_;
    std::wstring path = path_;
    double fps = fps_;
    size_t target = (size_t)(std::max(0.0, P_.ex.targetMB) * kMB);
    int fullW = cropWidth();
    ULONGLONG t0 = GetTickCount64();
    jobThread_ = std::thread([this, p, path, fps, out, target, fullW, t0] {
        ClipProject q = p;
        std::vector<std::wstring> notes;
        ClipExportResult r;
        std::string e;
        bool ok = true;
        auto cancel = [this] { return jobCancel_.load(); };
        auto setMsg = [this](const std::wstring& m) {
            std::lock_guard<std::mutex> l(jobMu_);
            jobMsg_ = m;
        };
        for (int attempt = 0;; ++attempt) {
            std::wstring again = attempt ? fmt(L" · 다시 %d번째 (%s)", attempt, describe(q.ex.width, q.ex.quality, q.ex.fps).c_str()) : L"";
            ClipProject qq = q;
            qq.ex.targetMB = 0;   // the retry over the target runs here
            ok = exportClipWebp(path, fps, qq, out, r,
                                [&](int k, int n) {
                                    jobK_ = k;
                                    jobN_ = n;
                                    setMsg(fmt(L"%d / %d프레임", k, n) + again);
                                },
                                cancel, e);
            if (!ok || !target || r.bytes <= target || attempt >= 2) break;
            // over the target: find a lower setting and encode again
            auto cands = fitCandidates(fullW, fps);
            int cur = -1;
            for (size_t i = 0; i < cands.size(); ++i)
                if (cands[i].width == q.ex.width && cands[i].quality == q.ex.quality && cands[i].fps == q.ex.fps) cur = (int)i;
            notes.push_back(fmt(L"%sMB로 목표 초과", fixed(r.bytes / kMB, 1).c_str()));
            setMsg(fmt(L"목표 %sMB를 넘어 설정을 낮추는 중…", fixed(target / kMB, 1).c_str()));
            FitCandidate c;
            size_t est = 0;
            bool fits = false;
            ok = findFit(path, fps, q, target, cur + 1, cands,
                         [&](const FitCandidate& t) { setMsg(fmt(L"시험 인코딩: %s", describe(t.width, t.quality, t.fps).c_str())); }, cancel, c,
                         est, fits, e);
            if (!ok) break;
            q.ex.width = c.width;
            q.ex.quality = c.quality;
            q.ex.fps = c.fps;
        }
        std::lock_guard<std::mutex> l(jobMu_);
        if (ok) {
            if (q.ex.width != p.ex.width || q.ex.quality != p.ex.quality || q.ex.fps != p.ex.fps) {
                jobApply_ = true;
                jobCand_ = {q.ex.width, q.ex.quality, q.ex.fps};
            }
            size_t sl = out.find_last_of(L"\\/");
            wchar_t secs[32];
            swprintf_s(secs, L"%.1f", (GetTickCount64() - t0) / 1000.0);
            jobResult_ = fmt(L"내보내기 완료: %s · %d×%d · %d프레임 · %sMB · %s초", out.substr(sl + 1).c_str(), r.w, r.h, r.planned,
                             fixed(r.bytes / kMB, 2).c_str(), secs);
            if (!notes.empty()) {
                std::wstring j;
                for (auto& n : notes) j += (j.empty() ? L"" : L", ") + n;
                jobResult_ += L" (" + j + L" → " + describe(q.ex.width, q.ex.quality, q.ex.fps) + L")";
            }
            if (target && r.bytes > target) jobResult_ += fmt(L" · ⚠ 목표 %sMB 초과", fixed(target / kMB, 1).c_str());
        } else
            jobResult_ = jobCancel_ ? tr(L"내보내기를 취소했습니다.") : fmt(L"내보내기 실패: %s", widen(e).c_str());
        jobDone_ = true;
    });
}

// Test-encodes short pieces of the clip to pick width / quality / FPS for the target size.
void ClipPage::fitSize() {
    if (!loaded_ || trackThread_.joinable() || jobThread_.joinable()) return;
    if (P_.ex.targetMB <= 0) { fitLabel_ = tr(L"목표 크기를 먼저 정하세요."); return; }
    stopPlay();
    jobKind_ = 2;
    jobCancel_ = false;
    jobDone_ = false;
    jobApply_ = false;
    jobK_ = 0;
    jobN_ = 0;   // indeterminate
    jobTitle_ = tr(L"목표 크기에 맞추는 중");
    jobMsg_.clear();
    jobResult_.clear();
    ClipProject p = P_;
    std::wstring path = path_;
    double fps = fps_;
    size_t target = (size_t)(P_.ex.targetMB * kMB);
    int fullW = cropWidth();
    ULONGLONG t0 = GetTickCount64();
    jobThread_ = std::thread([this, p, path, fps, target, fullW, t0] {
        FitCandidate c;
        size_t est = 0;
        bool fits = false;
        std::string e;
        bool ok = findFit(path, fps, p, target, 0, fitCandidates(fullW, fps),
                          [this](const FitCandidate& t) {
                              std::lock_guard<std::mutex> l(jobMu_);
                              jobMsg_ = fmt(L"시험 인코딩: %s", describe(t.width, t.quality, t.fps).c_str());
                          },
                          [this] { return jobCancel_.load(); }, c, est, fits, e);
        std::lock_guard<std::mutex> l(jobMu_);
        if (ok && !jobCancel_) {
            jobApply_ = true;
            jobCand_ = c;
            wchar_t secs[32];
            swprintf_s(secs, L"%.1f", (GetTickCount64() - t0) / 1000.0);
            jobResult_ = tr(fits ? L"맞춤: " : L"가장 작게 해도 넘음: ") + describe(c.width, c.quality, c.fps) +
                         fmt(L" → 예상 %sMB (%s초)", fixed(est / kMB, 1).c_str(), secs);
        } else
            jobResult_ = jobCancel_ ? tr(L"취소했습니다.") : fmt(L"실패: %s", widen(e).c_str());
        jobDone_ = true;
    });
}

std::wstring ClipPage::summary() {
    int ow = 0, oh = 0;
    outputSize(P_, info_.w, info_.h, ow, oh);
    int n = (int)outputFrames(P_, fps_).size();
    std::wstring s = fmt(L"출력 %d×%d · %d프레임 · %s초", ow, oh, n, fixed(P_.ex.fps > 0 ? n / P_.ex.fps : 0, 2).c_str());
    if (!P_.segments.empty()) s += fmt(L" · %d구간 이어 붙임", (int)clipRanges(P_).size());
    return s;
}
std::wstring ClipPage::rangeLabel() {
    return fmt(L"%d → %d (%s초)", P_.inFrame, P_.outFrame, fixed((P_.outFrame - P_.inFrame + 1) / fps_, 2).c_str());
}

void ClipPage::showHelp() {
    static const wchar_t* rows[][2] = {
        {L"Space", L"재생 / 정지"},
        {L"← / →", L"1프레임 (Ctrl 5 · Shift 10 · Ctrl+Shift 30)"},
        {L"Home / End", L"구간 시작 / 끝으로"},
        {L"I / O", L"구간 시작 / 끝 지정"},
        {L"드래그 (빈 곳)", L"새 마스크"},
        {L"드래그 (박스)", L"이동·크기 조절 → 현재 프레임에 키"},
        {L"K / Shift+K", L"키 고정 / 삭제"},
        {L", / .", L"이전 / 다음 키"},
        {L"T / Shift+T / Esc", L"앞으로 추적 / 역방향 / 중지 (추적 중 Space도 중지)"},
        {L"추적 중 화면 클릭", L"그 프레임에서 멈춤 → 박스를 고쳐 놓으면 이어서 추적"},
        {L"B", L"지금 프레임 ~ 다음 직접 놓은 키 사이만 양방향 추적"},
        {L"W / Shift+W", L"다음 / 이전 확인 필요한 곳 (타임라인 빨간 표시)"},
        {L"N / Shift+N", L"다음 / 이전 소리 이벤트"},
        {L"A", L"지금의 시작~끝을 구간 목록에 추가"},
        {L"S", L"슬로우모션 시작 → 끝에서 다시 S"},
        {L"P", L"정지 지점 추가 / 삭제"},
        {L"C / H / F", L"자르기 모드 / 테두리 숨기기 / 화면 맞춤"},
        {L"휠, 우클릭 드래그", L"확대 / 이동 (영상·타임라인)"},
        {L"Delete", L"선택 마스크 삭제"},
        {L"Ctrl+Z / Ctrl+Y", L"실행 취소 / 다시 실행"},
        {L"Ctrl+E", L"WebP 내보내기"},
    };
    Form f(hwnd_, L"단축키", 560);
    f.showCancel = false;
    f.okText = L"닫기";
    f.body = [&](Gui& g, float x, float y, float w) {
        for (auto& r : rows) {
            g.g.text(r[0], {x, y, 170, 26}, th::textPrimary, th::body, 600);
            g.g.text(r[1], {x + 176, y, w - 176, 26}, th::textSecondary, th::body);
            y += 28;
        }
        return y;
    };
    f.run();
}

void ClipPage::pickColor() {
    ClipMask* m = selMask();
    if (!m) return;
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = hwnd_;
    cc.rgbResult = RGB(R(m->color), G(m->color), B(m->color));
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    uint32_t c = rgb(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    setProp("color", [c](ClipMask& mm) { mm.color = c; });
}

// ================================================================== keyboard
bool ClipPage::key(int vk) {
    bool c = ctrlDown(), s = shiftDown();
    if (vk == VK_F1) { showHelp(); return true; }
    if (c && vk == 'O') { openDialog(); return true; }
    if (!loaded_) return false;
    if (jobThread_.joinable()) {
        if (vk == VK_ESCAPE) { jobCancel_ = true; return true; }
        return false;
    }
    if (c && !s && vk == 'Z') { undo(); return true; }
    if (c && (vk == 'Y' || (s && vk == 'Z'))) { redo(); return true; }
    if (c && vk == 'E') { exportWebp(); return true; }
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        int n = c && s ? 30 : s ? 10 : c ? 5 : 1;
        stepFrames(vk == VK_LEFT ? -n : n);
        return true;
    }
    if (c || altDown()) return false;
    if (trackThread_.joinable()) {   // while tracking: Space / Esc stop, view toggles still work
        switch (vk) {
        case VK_SPACE: case VK_ESCAPE: stopTrack(); return true;
        case 'H': overlay_ = !overlay_; return true;
        case 'F': zoom_ = 1; cx_ = info_.w / 2.0; cy_ = info_.h / 2.0; return true;
        }
        return false;
    }
    switch (vk) {
    case VK_SPACE: togglePlay(); return true;
    case VK_HOME: seek(P_.inFrame); return true;
    case VK_END: seek(P_.outFrame); return true;
    case VK_DELETE: deleteMask(); return true;
    case VK_ESCAPE:
        if (slowStart_ >= 0) { slowStart_ = -1; status_(L""); }
        else if (resumeDir_) { resumeDir_ = 0; trackLabel_ = tr(L"이어서 추적하지 않습니다."); }
        else if (mode_ == 1) toggleCrop();
        SetFocus(hwnd_);
        return true;
    case 'I': setIn(); return true;
    case 'O': setOut(); return true;
    case 'K': if (s) keyDel(); else keyAdd(); return true;
    case 'T': startTrack(s ? -1 : 1); return true;
    case 'B': startTrackBetween(); return true;
    case 'W': jumpWeak(s ? -1 : 1); return true;
    case 'N': gotoEvent(s ? -1 : 1); return true;
    case 'A': segAction(0); return true;
    case 'S': markSlow(); return true;
    case 'P': toggleFreeze(); return true;
    case 'C': toggleCrop(); return true;
    case 'H': overlay_ = !overlay_; return true;
    case 'F': zoom_ = 1; cx_ = info_.w / 2.0; cy_ = info_.h / 2.0; return true;
    }
    if (s) return false;
    switch (vk) {
    case VK_OEM_COMMA: gotoKey(-1); return true;
    case VK_OEM_PERIOD: gotoKey(1); return true;
    case VK_OEM_4: setProp("start", [this](ClipMask& m) { m.start = std::min(f_, m.end); }); return true;   // [
    case VK_OEM_6: setProp("end", [this](ClipMask& m) { m.end = std::max(f_, m.start); }); return true;     // ]
    }
    return false;
}

// ================================================================== drawing
bool ClipPage::decimal(Id id, RectF r, double& v, double lo, double hi, double step, int decimals) {
    double old = v;
    float bw = 26;
    if (gui_.iconButton(gid(id, 1), {r.x, r.y, bw, r.h}, ic::Minus, v > lo, th::textSecondary, 11)) v = std::max(lo, v - step);
    if (gui_.iconButton(gid(id, 2), {r.r() - bw, r.y, bw, r.h}, ic::Add, v < hi, th::textSecondary, 11)) v = std::min(hi, v + step);
    v = std::round(v * std::pow(10, decimals)) / std::pow(10, decimals);
    std::wstring t = fmtNum(v, decimals);
    if (gui_.edit(gid(id, 3), {r.x + bw + 2, r.y, r.w - 2 * bw - 4, r.h}, t)) {
        double x = _wtof(t.c_str());
        if (x >= lo && x <= hi) v = x;
    }
    return v != old;
}

ImagePtr ClipPage::currentImage() {
    if (trackThread_.joinable()) {   // tracking: the raw frame being tracked
        std::lock_guard<std::mutex> l(trackMu_);
        if (trackImg_) return trackImg_;
    }
    if (playing_ && playFrame_) {
        if (!fx_ || P_.masks.empty()) return playFrame_;
        auto im = std::make_shared<Image>(*playFrame_);
        double s = (double)im->w / std::max(1, info_.w);
        if (std::fabs(s - 1) > 1e-3) renderMasks(*im, scaledMasks(P_.masks, s), f_);
        else renderMasks(*im, P_.masks, f_);
        return im;
    }
    if (shownF_ != f_ || shownVer_ != version_ || shownFx_ != fx_) {
        ImagePtr raw = src_.frame(f_);
        if (raw) {
            if (fx_ && !P_.masks.empty()) {
                auto im = std::make_shared<Image>(*raw);
                renderMasks(*im, P_.masks, f_);
                shown_ = im;
            } else shown_ = raw;
        }
        shownF_ = f_;
        shownVer_ = version_;
        shownFx_ = fx_;
    }
    return shown_;
}

void ClipPage::draw(RectF area) {
    if (!loaded_) { drawEmpty(area); return; }
    float panelW = std::min(360.f, std::max(300.f, area.w * 0.28f));
    RectF top{area.x, area.y, area.w, 52};
    drawTop(top);
    float y = area.y + 56;
    RectF left{area.x, y, area.w - panelW - 12, area.b() - y};
    RectF panel{left.r() + 12, y, panelW, area.b() - y};
    float tlH = 100, trH = 80;
    RectF viewR{left.x, left.y, left.w, left.h - tlH - trH - 8};
    drawView(viewR);
    drawTimeline({left.x, viewR.b() + 4, left.w, tlH});
    drawTransport({left.x, viewR.b() + 8 + tlH, left.w, trH});
    drawPanel(panel);
    drawBusy(area);
}

void ClipPage::drawEmpty(RectF r) {
    Gfx& g = gui_.g;
    g.text(L"WebP 클립", {r.x, r.y + 10, 600, 34}, th::textPrimary, th::pageTitle, 600, A_LEFT, true, false, true);
    g.text(L"영상에서 구간을 골라 모자이크·추적·슬로우모션을 넣고 WebP로 만들어요", {r.x, r.y + 44, r.w, 20}, th::textSecondary, th::body);
    RectF box{r.x, r.y + 84, r.w, std::min(320.f, r.h - 100)};
    g.fill(box, th::radiusPanel, 0x22FFFFFF);
    g.dashRect(box.inset(8), 0x55FFFFFF, 1.5f);
    g.icon(ic::Film, {box.x, box.y + box.h / 2 - 70, box.w, 50}, th::textTertiary, 34);
    g.text(L"영상 파일을 여기에 끌어다 놓거나 [영상 열기]를 누르세요.", {box.x, box.y + box.h / 2 - 12, box.w, 24}, th::textPrimary, th::body,
           600, A_CENTER);
    g.text(L"영상은 이 PC 안에서만 처리돼요.", {box.x, box.y + box.h / 2 + 14, box.w, 20}, th::textSecondary, th::caption, 400, A_CENTER);
    if (gui_.button(gid("clip.open0"), {box.x + box.w / 2 - 70, box.y + box.h / 2 + 44, 140, 38}, L"영상 열기", BS_PRIMARY, ic::Open))
        gui_.later([this] { openDialog(); });
}

void ClipPage::drawTop(RectF r) {
    Gfx& g = gui_.g;
    bool idle = !trackThread_.joinable() && !jobThread_.joinable();
    g.text(L"WebP 클립", {r.x, r.y + 6, 160, 34}, th::textPrimary, th::pageTitle, 600, A_LEFT, true, false, true);
    std::wstring info = stem_ + fmt(L" · %d×%d · %sfps · %d프레임", info_.w, info_.h, fmtNum(fps_, 3).c_str(), count_);
    float x = r.x + 150;
    float bx = r.r();
    bx -= 150;
    if (gui_.button(gid("clip.export"), {bx, r.y + 8, 150, 36}, L"WebP 내보내기", BS_PRIMARY, ic::Save, idle))
        gui_.later([this] { exportWebp(); });
    bx -= 84;
    if (gui_.button(gid("clip.help"), {bx, r.y + 8, 76, 36}, L"단축키")) gui_.later([this] { showHelp(); });
    bx -= 40;
    if (gui_.iconButton(gid("clip.redo"), {bx, r.y + 8, 36, 36}, ic::Redo, !redo_.empty() && idle)) redo();
    bx -= 40;
    if (gui_.iconButton(gid("clip.undo"), {bx, r.y + 8, 36, 36}, ic::Undo, !undo_.empty() && idle)) undo();
    bx -= 112;
    if (gui_.button(gid("clip.open"), {bx, r.y + 8, 104, 36}, L"영상 열기", BS_SECONDARY, ic::Open, idle)) gui_.later([this] { openDialog(); });
    g.text(info, {x, r.y + 8, bx - x - 8, 36}, th::textSecondary, th::caption);
}

void ClipPage::drawView(RectF r) {
    Gfx& g = gui_.g;
    view_ = r;
    g.fill(r, th::radiusCard, 0xFF111114);
    const int W = info_.w, H = info_.h;
    const bool tracking = trackThread_.joinable();
    ImagePtr im = currentImage();
    float fit = std::min(r.w / std::max(1, W), r.h / std::max(1, H));
    float sc = fit * zoom_;
    float ox = r.x + r.w / 2 - (float)cx_ * sc, oy = r.y + r.h / 2 - (float)cy_ * sc;
    auto vrect = [&](const Box4& b) { return RectF{ox + (float)b[0] * sc, oy + (float)b[1] * sc, (float)b[2] * sc, (float)b[3] * sc}; };
    auto srcAt = [&](float px, float py) { return std::pair<double, double>{(px - ox) / sc, (py - oy) / sc}; };
    Box4 trackBox{};
    bool trackHas = false;
    if (tracking) {
        std::lock_guard<std::mutex> l(trackMu_);
        trackBox = trackShow_;
        trackHas = trackHasShow_;
    }

    g.pushClip(r);
    if (im) g.image(im, {ox, oy, W * sc, H * sc}, sc < 2.5f);
    // crop: shade outside, dashed outline, size label, handles in crop mode
    bool cropLive = drag_.target == DT_CROP && drag_.hasLive;
    if (cropLive || P_.hasCrop) {
        Box4 c = cropLive ? drag_.live : P_.crop;
        RectF v = vrect(c), full = vrect({0, 0, (double)W, (double)H});
        uint32_t shade = 0x8C000000;
        auto shadeR = [&](float x0, float y0, float x1, float y1) { if (x1 > x0 && y1 > y0) g.fill({x0, y0, x1 - x0, y1 - y0}, 0, shade); };
        float vy0 = std::clamp(v.y, full.y, full.b()), vy1 = std::clamp(v.b(), full.y, full.b());
        shadeR(full.x, full.y, full.r(), vy0);
        shadeR(full.x, vy1, full.r(), full.b());
        shadeR(full.x, vy0, std::clamp(v.x, full.x, full.r()), vy1);
        shadeR(std::clamp(v.r(), full.x, full.r()), vy0, full.r(), vy1);
        boxLine(g, v, kCropC, 1.5f, 6, 4);
        g.text(fmt(L"자르기 %d×%d", (int)std::lround(c[2]), (int)std::lround(c[3])), {v.x + 3, v.y - 19, 300, 16}, kCropC, 12);
        if (mode_ == 1) handles(g, v, kCropC);
    }
    // masks (always while dragging, so a hidden overlay still shows what is being edited)
    if (overlay_ || drag_.target != DT_NONE) {
        for (int i = 0; i < (int)P_.masks.size(); ++i) {
            const ClipMask& m = P_.masks[i];
            Box4 b;
            if (drag_.target == DT_MASK && drag_.idx == i && drag_.hasLive) b = drag_.live;
            else if (tracking && i == trackMask_ && trackHas) b = trackBox;
            else if (!maskRect(i, b)) continue;
            bool sel = i == sel_, keyed = m.keys.count(f_) > 0;
            uint32_t col = sel ? kSel : kMaskC;
            RectF v = vrect(b);
            if (m.pad) {
                float s = sc * m.pad;
                boxLine(g, {v.x - s, v.y - s, v.w + 2 * s, v.h + 2 * s}, col, 1, 2, 3);
            }
            if (m.effect == CE_ZOOM) {
                int box[4], zb[4];
                if (clipRect(b, m.pad, W, H, box)) {
                    zoomBox(box, m.zoom, W, H, zb);
                    boxLine(g, vrect({(double)zb[0], (double)zb[1], (double)(zb[2] - zb[0]), (double)(zb[3] - zb[1])}), col, 1, 2, 3);
                }
            }
            float lw = sel ? 2.f : 1.5f;
            if (m.shape == CS_ELLIPSE) ellipseLine(g, v, col, lw, !keyed);
            boxLine(g, v, col, lw, keyed ? 0 : 6, 4);
            g.text(m.name + (keyed ? L" ◆" : L""), {v.x + 2, v.y - 19, 300, 16}, col, 12);
            if (sel && mode_ == 0) handles(g, v, col);
            if (m.effect == CE_PORTAL) {   // exit: line from the entry, outline, label, handles
                Box4 d;
                bool liveExit = drag_.target == DT_DEST && drag_.idx == i && drag_.hasLive;
                if (liveExit) d = drag_.live;
                if (liveExit || destRect(i, d)) {
                    RectF dv = vrect(d);
                    bool dk = m.destKeys.count(f_) > 0;
                    dashLine(g, v.x + v.w / 2, v.y + v.h / 2, dv.x + dv.w / 2, dv.y + dv.h / 2, kPortalC, 1, 2, 3);
                    boxLine(g, dv, kPortalC, lw, dk ? 0 : 6, 4);
                    g.text(m.name + L" " + tr(L"출구") + (dk ? L" ◆" : L""), {dv.x + 2, dv.y - 19, 300, 16}, kPortalC, 12);
                    if (sel && mode_ == 0) handles(g, dv, kPortalC);
                }
            }
        }
    }
    if (drag_.target == DT_NEW && drag_.hasLive) boxLine(g, vrect(drag_.live), kSel, 1, 6, 4);
    g.popClip();
    if (tracking && !trackLabel_.empty()) {
        float tw = std::min(r.w - 24, g.textWidth(trackLabel_, th::caption, 600) + 20);
        g.fill({r.x + 12, r.b() - 36, tw, 26}, 6, 0xCC000000);
        g.text(trackLabel_, {r.x + 22, r.b() - 36, tw - 20, 26}, th::yellow, th::caption, 600);
    }

    // ---- input
    Id vid = gid("clip.view");
    gui_.interact(vid, r);
    if (gui_.wheel != 0 && gui_.hit(r)) {   // zoom around the cursor
        auto [bx, by] = srcAt(gui_.mx, gui_.my);
        zoom_ = std::clamp(zoom_ * std::pow(1.25f, gui_.wheel), 0.25f, 32.f);
        float ns = fit * zoom_;
        cx_ = bx - (gui_.mx - (r.x + r.w / 2)) / ns;
        cy_ = by - (gui_.my - (r.y + r.h / 2)) / ns;
        gui_.wheel = 0;
        gui_.redraw();
    }
    if (gui_.rpressed && gui_.hit(r) && !tracking) { pan_ = true; panX_ = gui_.mx; panY_ = gui_.my; panCx_ = cx_; panCy_ = cy_; }
    if (pan_) {
        if (gui_.rdown) {
            cx_ = panCx_ - (gui_.mx - panX_) / sc;
            cy_ = panCy_ - (gui_.my - panY_) / sc;
        } else pan_ = false;
    }
    // hit test: crop in crop mode; else the selected mask first, then top to bottom, portal exit before entry
    auto hitR = [&](const Box4& b, bool withHandles, Drag& d) {
        RectF v = vrect(b);
        if (withHandles) {
            const float xs[3] = {v.x, v.x + v.w / 2, v.r()}, ys[3] = {v.y, v.y + v.h / 2, v.b()};
            for (int hx = -1; hx <= 1; ++hx)
                for (int hy = -1; hy <= 1; ++hy)
                    if ((hx || hy) && std::fabs(gui_.mx - xs[hx + 1]) <= 7 && std::fabs(gui_.my - ys[hy + 1]) <= 7) {
                        d.handle = true;
                        d.hx = hx;
                        d.hy = hy;
                        return true;
                    }
        }
        if (gui_.mx >= v.x - 2 && gui_.mx <= v.r() + 2 && gui_.my >= v.y - 2 && gui_.my <= v.b() + 2) { d.handle = false; return true; }
        return false;
    };
    auto hitAny = [&]() {
        Drag d;
        if (mode_ == 1) {
            if (P_.hasCrop && hitR(P_.crop, true, d)) { d.target = DT_CROP; d.rect = P_.crop; }
            return d;
        }
        std::vector<int> order;
        if (sel_ >= 0 && sel_ < (int)P_.masks.size()) order.push_back(sel_);
        for (int i = (int)P_.masks.size() - 1; i >= 0; --i) if (i != sel_) order.push_back(i);
        for (int i : order) {
            Box4 b;
            if (destRect(i, b) && hitR(b, i == sel_, d)) { d.target = DT_DEST; d.idx = i; d.rect = b; return d; }
            if (maskRect(i, b) && hitR(b, i == sel_, d)) { d.target = DT_MASK; d.idx = i; d.rect = b; return d; }
        }
        return d;
    };
    auto [mxs, mys] = srcAt(gui_.mx, gui_.my);
    if (gui_.active == vid && gui_.pressed && !gui_.rdown) {
        if (tracking) {   // click while tracking: stop on the frame shown; fixing the box resumes tracking
            if (!trackBetween_) { trackFix_ = f_; stopTrack(); }
        } else {
            stopPlay();
            Drag d = hitAny();
            if (d.target != DT_NONE) {
                if (d.target != DT_CROP && d.idx != sel_) selectMask(d.idx);
                d.live = d.rect;
                d.hasLive = true;
            } else {
                d.target = mode_ == 1 ? DT_CROP : DT_NEW;
                d.draw = true;
            }
            d.ox = mxs;
            d.oy = mys;
            d.px = gui_.mx;
            d.py = gui_.my;
            drag_ = d;
        }
    }
    if (drag_.target != DT_NONE && gui_.down) {
        if (std::fabs(gui_.mx - drag_.px) + std::fabs(gui_.my - drag_.py) > 2) drag_.moved = true;
        if (drag_.moved) {
            Drag& d = drag_;
            double sx = mxs, sy = mys;
            if (d.draw) {
                sx = std::clamp(sx, 0.0, (double)W);
                sy = std::clamp(sy, 0.0, (double)H);
                d.live = {std::min(d.ox, sx), std::min(d.oy, sy), std::fabs(sx - d.ox), std::fabs(sy - d.oy)};
            } else {
                double dx = sx - d.ox, dy = sy - d.oy;
                const Box4& o = d.rect;
                if (!d.handle) d.live = {o[0] + dx, o[1] + dy, o[2], o[3]};
                else {
                    double l = o[0], t = o[1], rr = o[0] + o[2], bb = o[1] + o[3];
                    if (d.hx == -1) l += dx; else if (d.hx == 1) rr += dx;
                    if (d.hy == -1) t += dy; else if (d.hy == 1) bb += dy;
                    d.live = {std::min(l, rr), std::min(t, bb), std::fabs(rr - l), std::fabs(bb - t)};
                }
            }
            d.hasLive = true;
        }
    }
    if (drag_.target != DT_NONE && (gui_.released || !gui_.down)) {
        Drag d = drag_;
        drag_ = {};
        bool big = d.hasLive && d.live[2] >= 3 && d.live[3] >= 3;
        if (d.target == DT_NEW) {
            if (d.moved && big) addMask(d.live);
            else if (!d.moved) selectMask(-1);
        } else if (d.target == DT_CROP) {
            if (d.moved && big) setCrop(d.live);
        } else if (d.moved && big && d.idx >= 0 && d.idx < (int)P_.masks.size()) {
            pushUndo();
            ClipMask& m = P_.masks[d.idx];
            if (d.target == DT_DEST) m.destKeys[f_] = d.live;
            else {
                m.keys[f_] = d.live;
                m.trackScore.erase(f_);   // placed by hand
                m.start = std::min(m.start, f_);
                m.end = std::max(m.end, f_);
            }
            changed();
            if (resumeDir_ && d.target == DT_MASK && d.idx == sel_) {
                int dir = resumeDir_;
                resumeDir_ = 0;
                gui_.later([this, dir] { startTrack(dir); });
            }
        }
        gui_.redraw();
    }
    // cursor: resize / move over handles and boxes, crosshair elsewhere
    if (gui_.hit(r) && !tracking && !pan_) {
        Drag h = drag_.target != DT_NONE ? drag_ : hitAny();
        LPCWSTR cur = IDC_CROSS;
        if (h.target != DT_NONE && !h.draw) {
            if (!h.handle) cur = IDC_SIZEALL;
            else if (h.hx == 0) cur = IDC_SIZENS;
            else if (h.hy == 0) cur = IDC_SIZEWE;
            else cur = h.hx == h.hy ? IDC_SIZENWSE : IDC_SIZENESW;
        }
        SetCursor(LoadCursorW(nullptr, cur));
    }
}

void ClipPage::drawTransport(RectF r) {
    Gfx& g = gui_.g;
    float x = r.x, y = r.y + 2, h = 32;
    auto btn = [&](const char* id, const std::wstring& t, float w, bool on = false) {
        bool c = gui_.button(gid(id), {x, y, w, h}, t, on ? BS_PRIMARY : BS_SECONDARY);
        x += w + 4;
        return c;
    };
    // row 1: transport, frame number, time, range start / end
    if (btn("tr.first", L"⏮", 34)) seek(P_.inFrame);
    if (btn("tr.prev", L"◀", 34)) stepFrames(-1);
    if (btn("tr.play", playing_ ? L"❚❚" : L"▶", 40, playing_)) togglePlay();
    if (btn("tr.next", L"▶|", 34)) stepFrames(1);
    if (btn("tr.last", L"⏭", 34)) seek(P_.outFrame);
    if (btn("tr.evp", L"◀♪", 38)) gotoEvent(-1);
    if (btn("tr.evn", L"♪▶", 38)) gotoEvent(1);
    x += 4;
    int fv = f_;
    if (gui_.number(gid("tr.frame"), {x, y, 92, h}, fv, 0, count_ - 1) && fv != f_) seek(fv);
    x += 96;
    std::wstring tl = L"/ " + std::to_wstring(count_ - 1) + L"   " + fmtTime(f_ / fps_);
    float tw = g.textWidth(tl, th::caption, 600);
    g.text(tl, {x, y, tw + 4, h}, th::textSecondary, th::caption, 600);
    x += tw + 16;
    if (btn("tr.in", L"[ 시작 지점", 92)) setIn();
    if (btn("tr.out", L"끝 지점 ]", 84)) setOut();
    g.text(rangeLabel(), {x + 4, y, std::max(0.f, r.r() - x - 4), h}, th::textSecondary, th::caption, 600);
    // row 2: loop, effect preview, outlines, crop
    x = r.x;
    y += h + 6;
    auto tog = [&](const char* id, const std::wstring& t, bool& v) {
        float w = g.textWidth(t, th::caption, 600);
        bool c = gui_.toggle(gid(id), {x, y + 6, 40, 20}, v);
        g.text(t, {x + 46, y, w + 4, h}, th::textSecondary, th::caption, 600);
        x += 46 + w + 18;
        return c;
    };
    tog("tr.loop", L"구간 반복", loopRange_);
    x = std::max(x, r.r() - 520);
    tog("tr.fx", L"효과 미리보기", fx_);
    tog("tr.ov", L"테두리 (H)", overlay_);
    x = std::max(x, r.r() - 172);
    if (btn("tr.crop", L"✂ 자르기 영역", 112, mode_ == 1)) toggleCrop();
    if (btn("tr.full", L"전체", 52)) clearCrop();
}

void ClipPage::drawTimeline(RectF r) {
    Gfx& g = gui_.g;
    g.fill(r, 8, 0xFF202024);
    const float x0 = r.x + 8, w = r.w - 16, T = r.y;
    double lo = tlLo_, hi = std::max(tlLo_ + 1e-9, tlHi_);
    auto tx = [&](double f) { return x0 + (float)((f - lo) / (hi - lo)) * w; };
    auto tf = [&](float px) { return std::clamp((int)std::lround(lo + (px - x0) / std::max(1.f, w) * (hi - lo)), 0, count_ - 1); };
    g.pushClip({r.x + 6, r.y, r.w - 12, r.h});
    // ranges: in/out (green) or the range list (teal, numbered) with in/out outlined
    g.fill({x0, T + 4, w, 12}, 0, 0xFF3A3A40);
    float a = tx(P_.inFrame), b = tx(P_.outFrame);
    if (!P_.segments.empty()) {
        for (size_t n = 0; n < P_.segments.size(); ++n) {
            auto& s = P_.segments[n];
            if (s.hi < s.lo) continue;
            float sx0 = tx(s.lo), sx1 = tx(s.hi);
            g.fill({sx0, T + 4, std::max(2.f, sx1 - sx0), 12}, 0, kSegC);
            if (sx1 - sx0 > 12) g.text(std::to_wstring(n + 1), {sx0, T + 4, sx1 - sx0, 12}, 0xFF0A0A0C, 10, 400, A_CENTER);
        }
        g.stroke({a, T + 4, std::max(2.f, b - a), 12}, 0, kGreen, 2);
    } else g.fill({a, T + 4, std::max(2.f, b - a), 12}, 0, kRangeC);
    for (auto& s : P_.slowmos) g.fill({tx(s.lo), T + 17, std::max(2.f, tx(s.hi) - tx(s.lo)), 3}, 0, kSlowC);
    if (slowStart_ >= 0) g.fill({tx(slowStart_) - 1, T + 14, 3, 8}, 0, kSlowC);
    // waveform and sound events
    const float WT = T + 20, WH = 50, mid = WT + WH / 2;
    g.fill({x0, WT, w, WH}, 0, 0xFF1A1A1E);
    if (!levels_.empty()) {
        int cols = std::max(1, (int)w), n = (int)levels_.size();
        for (int c = 0; c < cols; ++c) {
            int f0 = (int)std::floor(lo + c * (hi - lo + 1) / cols);
            int f1 = std::max(f0 + 1, (int)std::floor(lo + (c + 1) * (hi - lo + 1) / cols));
            float v = 0;
            for (int f = std::max(0, f0); f < std::min(f1, n); ++f) v = std::max(v, std::clamp((levels_[f] + 60) / 60, 0.f, 1.f));
            float hh = v * (WH / 2 - 1);
            if (hh > 0.05f) g.fill({x0 + c, mid - hh, 1, hh * 2 + 0.5f}, 0, kWave);
        }
        for (int e : events_)
            if (e >= lo && e <= hi) g.fill({tx(e), WT, 1, WH}, 0, kRed);
    } else g.text(audioStatus_, {x0 + 6, WT, w - 12, WH}, 0xFF77777F, 12);
    // selected mask: shown span, keys, frames to check
    if (const ClipMask* m = sel_ >= 0 && sel_ < (int)P_.masks.size() ? &P_.masks[sel_] : nullptr) {
        float s0 = tx(std::max(0, m->start)), s1 = tx(std::min(count_ - 1, m->end));
        g.fill({s0, T + 74, std::max(2.f, s1 - s0), 4}, 0, kSpanC);
        float last = -10;
        for (auto& [f, bx] : m->keys) {
            float kx = tx(f);
            if (kx - last >= 1) { g.fill({kx, T + 81, 1, 10}, 0, kSel); last = kx; }
        }
        for (auto& [f, sc] : m->trackScore)
            if (sc < kWeakScore && f >= lo && f <= hi) g.fill({tx(f) - 1, T + 72, std::max(3.f, tx(f + 1) - tx(f)), 20}, 0, kRed);
    }
    // freeze points: triangles at the top
    for (auto& [f, s] : P_.freezes) {
        if (f < lo || f > hi) continue;
        float fx = tx(f);
        for (int i = 0; i < 10; ++i) {
            float hw = 5.f * (10 - i) / 10;
            g.fill({fx - hw, T + 2 + i, hw * 2, 1}, 0, kFreezeC);
        }
    }
    // zoomed in: where the view is within the whole video
    if (hi - lo < count_ - 1.5) {
        float s = x0 + w * (float)(lo / (count_ - 1)), e = x0 + w * (float)(hi / (count_ - 1));
        g.fill({s, r.b() - 3, std::max(3.f, e - s), 3}, 0, 0x80C8C8D2);
    }
    g.fill({tx(f_) - 1, T + 2, 2, r.h - 4}, 0, 0xFFFFFFFF);
    g.popClip();

    // input: click / drag seeks, wheel zooms around the cursor, right-drag pans
    Id id = gid("clip.tl");
    gui_.interact(id, r);
    if (gui_.active == id && gui_.pressed) stopPlay();
    if (gui_.active == id && gui_.down) {
        int f = tf(gui_.mx);
        if (f != f_) seek(f);
    }
    if (gui_.wheel != 0 && gui_.hit(r)) {
        double last = std::max(1, count_ - 1), span = hi - lo;
        double anchor = lo + (gui_.mx - x0) / std::max(1.f, w) * span;
        double ns = std::min(last, std::max(20.0, span * std::pow(0.8, gui_.wheel)));
        double t = (anchor - lo) / std::max(1e-9, span);
        tlLo_ = std::min(std::max(0.0, anchor - ns * t), last - ns);
        tlLo_ = std::max(0.0, tlLo_);
        tlHi_ = tlLo_ + ns;
        gui_.wheel = 0;
        gui_.redraw();
    }
    if (gui_.rpressed && gui_.hit(r)) { tlPan_ = true; tlPanX_ = gui_.mx; tlPanLo_ = tlLo_; }
    if (tlPan_) {
        if (gui_.rdown) {
            double span = tlHi_ - tlLo_;
            tlLo_ = std::clamp(tlPanLo_ - (gui_.mx - tlPanX_) / std::max(1.f, w) * span, 0.0, std::max(0.0, count_ - 1 - span));
            tlHi_ = tlLo_ + span;
        } else tlPan_ = false;
    }
}

void ClipPage::drawPanel(RectF r) {
    Gfx& g = gui_.g;
    gui_.card(r, th::radiusPanel, false);
    RectF inner = r.inset(6);
    float y = gui_.beginScroll(panelScroll_, inner);
    float x = inner.x + 10, w = inner.w - 20;
    const bool tracking = trackThread_.joinable();
    const bool inputWas = gui_.enabledInput;
    if (tracking) gui_.enabledInput = false;   // the panel is locked while tracking (except Stop)
    auto section = [&](const std::wstring& t) {
        y += 6;
        g.text(t, {x, y, w, 24}, th::textPrimary, th::sectionTitle, 600);
        y += 30;
    };
    auto note = [&](const std::wstring& t) {
        float h = g.textHeight(t, th::caption, w);
        g.text(t, {x, y, w, h + 2}, th::textTertiary, th::caption, 400, A_LEFT, false, true);
        y += h + 8;
    };
    auto label = [&](const std::wstring& t, float lw = 96) {
        g.text(t, {x, y, lw, 30}, th::textSecondary, th::caption, 600);
        return RectF{x + lw, y, w - lw, 30};
    };
    auto rowEnd = [&] { y += 36; };
    auto btnRow = [&](std::vector<std::pair<std::wstring, std::function<void()>>> bs, const char* base, bool enabled = true) {
        float bw = (w - 6 * (bs.size() - 1)) / bs.size();
        for (size_t i = 0; i < bs.size(); ++i)
            if (gui_.button(gid(gid(base), i), {x + i * (bw + 6), y, bw, 30}, bs[i].first, BS_SECONDARY, 0, enabled)) gui_.later(bs[i].second);
        y += 36;
    };
    // list with an optional check box per item; returns the clicked item, *toggled = item whose box was clicked
    auto listBox = [&](const char* id, const std::vector<std::wstring>& items, int sel, float minH, const std::vector<bool>* checks = nullptr,
                       int* toggled = nullptr) -> int {
        float h = std::max(minH, items.size() * 26.f + 4);
        RectF lb{x, y, w, h};
        g.fill(lb, 6, 0x33000000);
        int clicked = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            RectF it{x + 2, y + 2 + i * 26, w - 4, 24};
            float tx = it.x + 8;
            if (checks) {
                RectF cb{it.x + 6, it.y + 5, 14, 14};
                Id cid = gid(gid(id), 1000 + i);
                if (gui_.interact(cid, cb.inset(-3)) && toggled) *toggled = (int)i;
                bool on = (*checks)[i];
                g.fill(cb, 3, on ? th::accent : 0x22FFFFFF);
                g.stroke(cb, 3, on ? th::accent : 0x66FFFFFF);
                if (on) g.icon(ic::Check, cb, 0xFFFFFFFF, 10);
                tx = cb.r() + 8;
            }
            Id iid = gid(gid(id), i);
            if (gui_.interact(iid, {tx - 4, it.y, it.r() - tx + 4, it.h})) clicked = (int)i;
            if ((int)i == sel) g.fill(it, 4, A(th::accent, 0.35f));
            else if (gui_.hoverT(iid) > 0) g.fill(it, 4, A(th::control, gui_.hoverT(iid)));
            if (checks) {   // redraw the box above the highlight
                RectF cb{it.x + 6, it.y + 5, 14, 14};
                bool on = (*checks)[i];
                g.fill(cb, 3, on ? th::accent : 0x22FFFFFF);
                g.stroke(cb, 3, on ? th::accent : 0x66FFFFFF);
                if (on) g.icon(ic::Check, cb, 0xFFFFFFFF, 10);
            }
            g.text(items[i], {tx, it.y, it.r() - tx - 4, it.h}, th::textPrimary, th::caption);
        }
        if (items.empty()) g.text(L"없음", lb.inset(10, 0), th::textTertiary, th::caption);
        y += h + 6;
        return clicked;
    };

    // ---- audio
    section(L"오디오 · 장면 찾기");
    note(audioStatus_.empty() ? tr(L"영상을 열면 파형을 분석합니다.") : audioStatus_);
    {
        RectF c = label(L"이벤트 감도");
        if (gui_.number(gid("cp.rise"), {c.x, c.y, c.w - 30, 30}, rise_, 3, 40)) recomputeEvents();
        g.text(L"dB", {c.r() - 26, c.y, 26, 30}, th::textTertiary, th::caption);
        rowEnd();
        c = label(L"최소 크기");
        if (gui_.number(gid("cp.floor"), {c.x, c.y, c.w - 30, 30}, floor_, -80, 0)) recomputeEvents();
        g.text(L"dB", {c.r() - 26, c.y, 26, 30}, th::textTertiary, th::caption);
        rowEnd();
        if (gui_.toggle(gid("cp.sound"), {x, y + 5, 40, 20}, sound_) && player_) player_->setMuted(!sound_);
        g.text(L"재생할 때 소리", {x + 46, y, 120, 30}, th::textSecondary, th::caption, 600);
        g.text(L"볼륨", {x + 170, y, 40, 30}, th::textSecondary, th::caption, 600);
        if (gui_.slider(gid("cp.vol"), {x + 206, y, w - 250, 30}, volume_, 0, 100) && player_) player_->setVolume(volume_ / 100.0);
        g.text(std::to_wstring(volume_) + L"%", {x + w - 40, y, 40, 30}, th::textSecondary, th::caption, 400, A_RIGHT);
        rowEnd();
        c = label(L"원본 FPS");
        double fps = fps_;
        if (decimal(gid("cp.srcfps"), c, fps, 1, 240, 0.001, 3) && fps != fps_) {
            fps_ = fps;
            src_.setFps(fps_);
            count_ = std::max(1, (int)std::floor(info_.duration * fps_ + 1e-6));
            P_.outFrame = std::min(P_.outFrame, count_ - 1);
            P_.inFrame = std::min(P_.inFrame, P_.outFrame);
            f_ = std::min(f_, count_ - 1);
            tlLo_ = 0;
            tlHi_ = std::max(1, count_ - 1);
            shownF_ = -1;
            changed();
        }
        rowEnd();
        note(tr(L"자동 추정, 틀리면 고치세요"));
    }

    // ---- masks
    section(L"마스크");
    note(tr(L"영상의 빈 곳을 드래그하면 새 마스크가 생깁니다."));
    {
        std::vector<std::wstring> items;
        std::vector<bool> on;
        for (auto& m : P_.masks) {
            items.push_back(m.name + L" · " + tr(kEffects[std::clamp(m.effect, 0, 4)]) + L" · " + tr(L"키") + L" " + std::to_wstring(m.keys.size()));
            on.push_back(m.enabled);
        }
        int tog = -1;
        int c = listBox("cp.masks", items, sel_, 30, &on, &tog);
        if (tog >= 0) {
            pushUndo();
            P_.masks[tog].enabled = !P_.masks[tog].enabled;
            changed();
        } else if (c >= 0) selectMask(c);
        bool has = sel_ >= 0;
        btnRow({{L"▲", [this] { moveMask(-1); }}, {L"▼", [this] { moveMask(1); }}, {L"복제", [this] { duplicateMask(); }},
                {L"삭제", [this] { deleteMask(); }}},
               "cp.mbtn", has);
    }

    // ---- selected mask
    if (ClipMask* m = selMask()) {
        section(L"선택한 마스크");
        RectF c = label(L"이름");
        std::wstring nm = m->name;
        if (gui_.edit(gid("cp.name"), c, nm, !tracking) && nm != m->name) setProp("name", [nm](ClipMask& mm) { mm.name = nm; });
        rowEnd();
        c = label(L"효과");
        int eff = m->effect;
        std::vector<std::wstring> effs(kEffects, kEffects + 5);
        if (gui_.dropdown(gid("cp.eff"), c, effs, eff) && eff != m->effect)
            setProp("effect", [this, eff](ClipMask& mm) {
                mm.effect = eff;
                Box4 b;
                if (eff == CE_PORTAL && mm.destKeys.empty() && interpBox(mm.keys, f_, b)) mm.destKeys[f_] = defaultDest(b, info_.w, info_.h);
            });
        rowEnd();
        if (m->effect == CE_MOSAIC || m->effect == CE_BLUR) {
            c = label(m->effect == CE_BLUR ? L"블러 세기" : L"블록 크기");
            int v = m->strength;
            if (gui_.number(gid("cp.str"), c, v, 1, 120)) setProp("strength", [v](ClipMask& mm) { mm.strength = std::max(1, v); });
            rowEnd();
        }
        if (m->effect == CE_BOX || m->effect == CE_ZOOM || m->effect == CE_PORTAL) {
            c = label(m->effect == CE_BOX ? L"박스 색" : L"테두리 색");
            g.fill({c.x, c.y + 3, 40, 24}, 5, m->color);
            g.stroke({c.x, c.y + 3, 40, 24}, 5, th::separator);
            if (gui_.button(gid("cp.color"), {c.x + 48, c.y, 90, 30}, L"색 고르기")) gui_.later([this] { pickColor(); });
            rowEnd();
        }
        if (m->effect == CE_ZOOM) {
            c = label(L"확대 배율");
            double z = m->zoom;
            if (decimal(gid("cp.zoom"), c, z, 1.1, 8, 0.25, 2)) setProp("zoom", [z](ClipMask& mm) { mm.zoom = std::max(1.1, z); });
            rowEnd();
        }
        if (m->effect == CE_ZOOM || m->effect == CE_PORTAL) {
            c = label(L"테두리");
            int v = m->border;
            if (gui_.number(gid("cp.border"), {c.x, c.y, c.w - 30, 30}, v, 0, 40)) setProp("border", [v](ClipMask& mm) { mm.border = std::max(0, v); });
            g.text(L"px", {c.r() - 26, c.y, 26, 30}, th::textTertiary, th::caption);
            rowEnd();
        }
        if (m->effect == CE_PORTAL) {
            c = label(L"포탈 출구");
            int nd = (int)m->destKeys.size();
            std::wstring t = nd == 1 ? tr(L"고정") : fmt(L"키 %d개", nd) + (m->destKeys.count(f_) ? L" · ◆" : L"");
            g.text(t, {c.x, c.y, 76, 30}, th::textPrimary, th::caption, 600);
            float bw = (c.w - 84) / 2;
            if (gui_.button(gid("cp.exdel"), {c.x + 80, c.y, bw, 30}, L"키 삭제") && m->destKeys.count(f_) && nd >= 2) {
                pushUndo();
                m->destKeys.erase(f_);
                changed();
            }
            if (gui_.button(gid("cp.expin"), {c.x + 84 + bw, c.y, bw, 30}, L"고정")) {
                Box4 d;
                if (interpBox(m->destKeys, f_, d)) {
                    pushUndo();
                    m->destKeys.clear();
                    m->destKeys[f_] = d;
                    changed();
                }
            }
            rowEnd();
        }
        c = label(L"모양");
        int shp = m->shape;
        if (gui_.segmented(gid("cp.shape"), c, {L"사각형", L"타원"}, shp) && shp != m->shape) setProp("shape", [shp](ClipMask& mm) { mm.shape = shp; });
        rowEnd();
        c = label(L"여백");
        int pad = m->pad;
        if (gui_.number(gid("cp.pad"), {c.x, c.y, c.w - 30, 30}, pad, 0, 200)) setProp("pad", [pad](ClipMask& mm) { mm.pad = std::max(0, pad); });
        g.text(L"px", {c.r() - 26, c.y, 26, 30}, th::textTertiary, th::caption);
        rowEnd();
        c = label(L"표시 시작");
        int st = std::min(m->start, count_ - 1);
        if (gui_.number(gid("cp.ms"), {c.x, c.y, c.w - 62, 30}, st, 0, count_ - 1)) setProp("start", [st](ClipMask& mm) { mm.start = std::min(st, mm.end); });
        if (gui_.button(gid("cp.msh"), {c.r() - 56, c.y, 56, 30}, L"현재")) setProp("start", [this](ClipMask& mm) { mm.start = std::min(f_, mm.end); });
        rowEnd();
        c = label(L"표시 끝");
        int en = std::min(m->end, count_ - 1);
        if (gui_.number(gid("cp.me"), {c.x, c.y, c.w - 62, 30}, en, 0, count_ - 1)) setProp("end", [en](ClipMask& mm) { mm.end = std::max(en, mm.start); });
        if (gui_.button(gid("cp.meh"), {c.r() - 56, c.y, 56, 30}, L"현재")) setProp("end", [this](ClipMask& mm) { mm.end = std::max(f_, mm.start); });
        rowEnd();
        c = label(L"키프레임");
        std::wstring ks = std::to_wstring(m->keys.size()) + tr(L"개") + L" · ";
        if (f_ < m->start || f_ > m->end) ks += tr(L"표시 구간 밖");
        else if (m->keys.count(f_)) ks += tr(L"◆ 이 프레임은 키");
        else ks += tr(L"보간된 위치");
        g.text(ks, c, th::textPrimary, th::caption, 600);
        rowEnd();
        btnRow({{L"◆ 키", [this] { keyAdd(); }}, {L"키 삭제", [this] { keyDel(); }}, {L"◀", [this] { gotoKey(-1); }},
                {L"▶", [this] { gotoKey(1); }}},
               "cp.kbtn");
        btnRow({{L"이전 키 모두 삭제", [this] { clearKeysSide(-1); }}, {L"이후 키 모두 삭제", [this] { clearKeysSide(1); }}}, "cp.kclr");

        // ---- tracking
        section(L"추적");
        gui_.toggle(gid("cp.smooth"), {x, y + 5, 40, 20}, smooth_);
        g.text(L"떨림 완화", {x + 46, y, 90, 30}, th::textSecondary, th::caption, 600);
        gui_.toggle(gid("cp.fixed"), {x + w / 2, y + 5, 40, 20}, fixedSize_);
        g.text(L"박스 크기 고정", {x + w / 2 + 46, y, 110, 30}, th::textSecondary, th::caption, 600);
        rowEnd();
        btnRow({{L"◀ 역방향", [this] { startTrack(-1); }}, {L"추적 ▶", [this] { startTrack(1); }}}, "cp.trk", !tracking);
        gui_.enabledInput = inputWas;
        if (gui_.button(gid("cp.trstop"), {x, y, w, 30}, L"중지", BS_SECONDARY, 0, tracking)) stopTrack();
        if (tracking) gui_.enabledInput = false;
        y += 36;
        btnRow({{L"↔ 키 사이", [this] { startTrackBetween(); }}, {L"◀ 확인 필요", [this] { jumpWeak(-1); }}, {L"확인 필요 ▶", [this] { jumpWeak(1); }}},
               "cp.trk2", !tracking);
        note(trackLabel_.empty() ? tr(L"박스를 대상에 맞춘 뒤 추적하세요. 추적 중 어긋나면 화면을 클릭해 멈추고 박스를 고치면 이어서 추적합니다.")
                                 : trackLabel_);
    }

    // ---- segments
    section(L"구간 목록 (여러 구간 이어 붙이기)");
    note(tr(L"비어 있으면 시작~끝 한 구간만 내보냅니다. 추가한 구간은 목록 순서대로 이어 붙입니다."));
    {
        std::vector<std::wstring> items;
        for (size_t n = 0; n < P_.segments.size(); ++n) {
            auto& s = P_.segments[n];
            items.push_back(fmt(L"%d.  %d → %d  ·  %s초", (int)n + 1, s.lo, s.hi, fixed((s.hi - s.lo + 1) / fps_, 2).c_str()));
        }
        if (segSel_ >= (int)P_.segments.size()) segSel_ = (int)P_.segments.size() - 1;
        int c = listBox("cp.segs", items, segSel_, 30);
        if (c >= 0) {
            segSel_ = c;
            P_.inFrame = P_.segments[c].lo;
            P_.outFrame = P_.segments[c].hi;
            stopPlay();
            seek(P_.inFrame);
            changed();
        }
        btnRow({{L"+ 현재 구간 추가", [this] { segAction(0); }}, {L"갱신", [this] { segAction(1); }}}, "cp.sadd");
        btnRow({{L"▲", [this] { segAction(3); }}, {L"▼", [this] { segAction(4); }}, {L"삭제", [this] { segAction(2); }}}, "cp.smove");
        gui_.toggle(gid("cp.segplay"), {x, y + 5, 40, 20}, segPlay_);
        g.text(L"재생할 때 목록 순서로 이어서", {x + 46, y, w - 46, 30}, th::textSecondary, th::caption, 600);
        rowEnd();
    }

    // ---- slow motion
    section(L"슬로우모션 구간");
    note(tr(L"시작 프레임에서 S, 끝 프레임에서 다시 S (Esc 취소)."));
    {
        std::vector<std::wstring> items;
        for (auto& s : P_.slowmos) {
            double sec = (s.hi - s.lo + 1) / fps_;
            items.push_back(fmt(L"%d → %d · %s초 → %s초 (%s×)", s.lo, s.hi, fixed(sec, 2).c_str(), fixed(sec / s.rate, 2).c_str(),
                                fmtNum(s.rate, 2).c_str()));
        }
        if (slowSel_ >= (int)P_.slowmos.size()) slowSel_ = (int)P_.slowmos.size() - 1;
        int c = listBox("cp.slows", items, slowSel_, 30);
        if (c >= 0) { slowSel_ = c; slowRate_ = P_.slowmos[c].rate; seek(P_.slowmos[c].lo); }
        RectF rc = label(L"배속");
        if (decimal(gid("cp.slowrate"), {rc.x, rc.y, rc.w - 20, 30}, slowRate_, 0.1, 1, 0.05, 2) && slowSel_ >= 0 &&
            slowSel_ < (int)P_.slowmos.size()) {
            pushUndo("slow" + std::to_string(slowSel_));
            P_.slowmos[slowSel_].rate = std::clamp(slowRate_, 0.1, 1.0);
            changed();
        }
        g.text(L"×", {rc.r() - 16, rc.y, 16, 30}, th::textTertiary, th::caption);
        rowEnd();
        btnRow({{slowStart_ >= 0 ? L"여기서 끝 → 추가" : L"여기서 시작", [this] { markSlow(); }},
                {L"삭제", [this] {
                     if (slowSel_ >= 0 && slowSel_ < (int)P_.slowmos.size()) {
                         pushUndo();
                         P_.slowmos.erase(P_.slowmos.begin() + slowSel_);
                         changed();
                     }
                 }}},
               "cp.slowb");
    }

    // ---- freezes
    section(L"정지 지점");
    note(tr(L"결과물이 이 프레임에서 지정한 시간만큼 멈췄다가 이어집니다."));
    {
        std::vector<std::wstring> items;
        std::vector<int> frames;
        for (auto& [f, s] : P_.freezes) {
            items.push_back(fmt(L"%d · %s · %s초 정지", f, fmtTime(f / fps_).c_str(), fmtNum(s, 2).c_str()));
            frames.push_back(f);
        }
        int sel = -1;
        for (size_t i = 0; i < frames.size(); ++i) if (frames[i] == f_) sel = (int)i;
        int c = listBox("cp.frz", items, sel, 30);
        if (c >= 0) seek(frames[c]);
        RectF rc = label(L"멈출 시간");
        if (decimal(gid("cp.frzsec"), {rc.x, rc.y, rc.w - 20, 30}, freezeSec_, 0.1, 30, 0.5, 2) && P_.freezes.count(f_)) {
            pushUndo("freeze" + std::to_string(f_));
            P_.freezes[f_] = std::max(0.1, freezeSec_);
            changed();
        }
        g.text(L"초", {rc.r() - 16, rc.y, 16, 30}, th::textTertiary, th::caption);
        rowEnd();
        bool here = P_.freezes.count(f_) > 0;
        if (gui_.button(gid("cp.frzbtn"), {x, y, w, 30}, here ? L"현재 프레임의 정지 지점 삭제" : L"현재 프레임에 추가")) toggleFreeze();
        y += 36;
    }

    // ---- export
    section(L"WebP 내보내기");
    {
        ClipExport ex = P_.ex;
        RectF c = label(L"FPS");
        decimal(gid("cp.exfps"), c, ex.fps, 1, 100, 1, 2);
        rowEnd();
        c = label(L"속도");
        decimal(gid("cp.exspd"), {c.x, c.y, c.w - 20, 30}, ex.speed, 0.1, 8, 0.25, 2);
        g.text(L"×", {c.r() - 16, c.y, 16, 30}, th::textTertiary, th::caption);
        rowEnd();
        c = label(L"너비");
        gui_.number(gid("cp.exw"), {c.x, c.y, c.w - 90, 30}, ex.width, 0, 8192);
        g.text(L"px · 0 = 원본", {c.r() - 86, c.y, 86, 30}, th::textTertiary, th::caption);
        rowEnd();
        c = label(L"화질");
        gui_.slider(gid("cp.exq"), {c.x, c.y, c.w - 40, 30}, ex.quality, 40, 100);
        g.text(std::to_wstring(ex.quality), {c.r() - 34, c.y, 34, 30}, th::textPrimary, th::caption, 600, A_RIGHT);
        rowEnd();
        c = label(L"반복");
        gui_.segmented(gid("cp.exloop"), c, {L"무한 반복", L"1회"}, ex.loop);
        rowEnd();
        c = label(L"목표 크기");
        decimal(gid("cp.extgt"), {c.x, c.y, c.w - 30, 30}, ex.targetMB, 0, 500, 0.5, 2);
        g.text(L"MB", {c.r() - 26, c.y, 26, 30}, th::textTertiary, th::caption);
        rowEnd();
        note(tr(L"0 = 끔 · 디스코드 10MB"));
        if (ex.fps != P_.ex.fps || ex.speed != P_.ex.speed || ex.width != P_.ex.width || ex.quality != P_.ex.quality ||
            ex.loop != P_.ex.loop || ex.targetMB != P_.ex.targetMB) {
            pushUndo("export");
            P_.ex = ex;
            changed();
        }
        if (gui_.button(gid("cp.fit"), {x, y, w, 32}, L"크기에 맞추기", BS_SECONDARY, 0, jobDone_)) gui_.later([this] { fitSize(); });
        y += 38;
        if (!fitLabel_.empty()) note(fitLabel_);
        note(summary());
        if (gui_.button(gid("cp.export"), {x, y, w, 38}, L"WebP 내보내기", BS_PRIMARY, ic::Save, jobDone_)) gui_.later([this] { exportWebp(); });
        y += 46;
    }
    gui_.enabledInput = inputWas;
    gui_.endScroll(panelScroll_, y + 10);
}

void ClipPage::drawBusy(RectF area) {
    if (jobDone_ && !jobThread_.joinable()) return;
    Gfx& g = gui_.g;
    g.fill(area, 0, 0x88000000);
    RectF c{area.x + area.w / 2 - 220, area.y + area.h / 2 - 80, 440, 160};
    gui_.card(c);
    g.text(jobTitle_, {c.x + 20, c.y + 16, c.w - 40, 26}, th::textPrimary, th::sectionTitle, 600);
    int n = jobN_.load(), k = jobK_.load();
    RectF bar{c.x + 20, c.y + 56, c.w - 40, 8};
    if (n > 0) gui_.progress(bar, (float)k / n);
    else {   // indeterminate: a sweeping block
        g.fill(bar, 4, 0x33FFFFFF);
        float t = (float)(GetTickCount64() % 1400) / 1400.f, bw = bar.w * 0.3f;
        float bx = bar.x + (bar.w + bw) * t - bw;
        float x0 = std::max(bar.x, bx), x1 = std::min(bar.r(), bx + bw);
        if (x1 > x0) g.fill({x0, bar.y, x1 - x0, bar.h}, 4, th::accent);
    }
    std::wstring msg;
    {
        std::lock_guard<std::mutex> l(jobMu_);
        msg = jobMsg_;
    }
    if (msg.empty() && n > 0) msg = std::to_wstring(k) + L" / " + std::to_wstring(n);
    g.text(msg, {c.x + 20, c.y + 72, c.w - 40, 22}, th::textSecondary, th::caption);
    if (gui_.button(gid("clip.jobcancel"), {c.r() - 110, c.b() - 48, 90, 32}, L"취소")) jobCancel_ = true;
}

} // namespace bg
