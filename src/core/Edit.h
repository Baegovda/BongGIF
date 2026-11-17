#pragma once
// Frame-level edit operations. All pure.
#include "core/Image.h"

#include <string>
#include <vector>

namespace bg {

constexpr int64_t kMinFrameUs = 20000;
extern const int kFpsChoices[11];

int64_t fpsToUs(int fps);
int64_t secondsToUs(double s);

// "selection" rule: 2+ selected -> those, else all frames
std::vector<int> targets(size_t n, const std::vector<int>& sel);

Frames setTime(const Frames& f, int64_t us, const std::vector<int>& sel);
Frames changeSpeed(const Frames& f, double factor);       // x0.9 faster, x1.1 slower
Frames reverse(const Frames& f);
Frames yoyo(const Frames& f);                              // returning first frame gets 1/2
Frames trim(const Frames& f, int start, int end);          // keep [start,end]
Frames deleteFrames(const Frames& f, const std::vector<int>& idx);

enum class ReduceMode { Even, Odd, Nth };
Frames reduceFrames(const Frames& f, ReduceMode m, int nth, bool keepSpeed, const std::vector<int>& sel);
Frames removeDuplicates(const Frames& f, int diffPercent, int pixelTol, bool keepSpeed, int64_t timeLimitUs,
                        const std::vector<int>& sel);
Frames videoSummary(const Frames& f, int nPercent, bool scaleTime, int adjustPct, bool limit, int64_t limitUs,
                    const std::vector<int>& sel, const Progress& prog = {});
bool framesSimilar(const Image& a, const Image& b, int nPercent, int tol);

Frames crop(const Frames& f, int x, int y, int w, int h);
ImagePtr resizeImage(const Image& src, int w, int h);
Frames resize(const Frames& f, int w, int h, const Progress& prog = {});
enum class Rot { CW, CCW, R180, HFlip, VFlip };
Frames rotate(const Frames& f, Rot r);
Frames crossfade(const Frames& f, int at, int count, int64_t totalUs);
Frames mapImages(const Frames& f, const std::function<ImagePtr(const Image&)>& fn, const std::vector<int>& sel,
                 const Progress& prog = {});   // sel empty -> all (not the 2+ rule)
void parallelFor(int n, const std::function<void(int)>& fn);

class Project {
public:
    Frames frames, initial;
    bool apply(Frames nf);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void reset(Frames f) { frames = initial = std::move(f); undo_.clear(); redo_.clear(); }
    int64_t totalUs() const;
    size_t memoryBytes() const;

private:
    std::vector<Frames> undo_, redo_;
};

} // namespace bg
