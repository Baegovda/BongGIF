#pragma once
// WebP export for the clip page, structured like the WebP Clip web app: decodes the planned source
// frames in order, applies masks, crop and width, and for each output frame encodes only the
// rectangle that changed since the last written frame (even x/y offsets, tolerance 2 per channel)
// as a standalone lossy WebP (canvas-equivalent settings: quality q, method 3; q = 100 lossless).
// A frame with no change only lengthens the previous frame. The stills are muxed into ANMF chunks
// with "no blend, no dispose" (each rectangle overwrites the canvas). Memory stays at a few frames.
#include "core/Clip.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bg {

struct ClipExportResult {
    size_t bytes = 0;
    int planned = 0;   // output frames in the plan
    int encoded = 0;   // ANMF frames written (identical frames merged)
    int w = 0, h = 0;
    // target size (p.ex.targetMB > 0): when the file was over the target the export retried with
    // lower fit candidates (at most 2 retries). used = the export settings of the final file.
    bool adjusted = false;
    bool overTarget = false;   // final file still larger than the target
    int attempts = 1;
    FitCandidate used;
};

// srcFps: the source FPS used for frame numbering (user may override the detected one).
// outPath empty = encode to memory only (size measurement). onStep(k, n) reports output frames.
bool exportClipWebp(const std::wstring& videoPath, double srcFps, const ClipProject& p, const std::wstring& outPath,
                    ClipExportResult& res, const std::function<void(int, int)>& onStep, const std::function<bool()>& cancel,
                    std::string& err);

// Estimated file size: plans with <= 48 frames are encoded whole (sum of stills + 32 bytes per
// frame); otherwise 6 runs of 8 frames spread over the clip: first still + mean change size of the
// other frames * (n - 1) + 32 bytes per expected written frame.
bool estimateClipBytes(const std::wstring& videoPath, double srcFps, const ClipProject& p, size_t& bytes,
                       const std::function<bool()>& cancel, std::string& err);

// Size fit: binary search over fitCandidates() for the best candidate whose estimate is within
// 92% of targetBytes. onTry(i, n, cand) is called before each trial.
struct ClipFitResult { bool found = false, fits = false; FitCandidate cand; size_t bytes = 0; int index = -1; };
bool fitClipToSize(const std::wstring& videoPath, double srcFps, const ClipProject& p, size_t targetBytes, ClipFitResult& res,
                   const std::function<void(int, int, const FitCandidate&)>& onTry, const std::function<bool()>& cancel,
                   std::string& err);

// Changed area between two equally sized images: x0,y0 (rounded down to even), x1,y1 exclusive.
// false when no channel differs by more than tol.
bool clipDiffBox(const Image& a, const Image& b, int tol, int out[4]);

} // namespace bg
