#pragma once
// Frame-accurate random access to a video for the WebP clip page. Decodes with an IMFSourceReader
// (RGB32), keeps an LRU cache of decoded frames, decodes forward from the nearest keyframe on seeks
// and continues sequentially when reading in order (tracking / export).
//
// Frame numbering follows the WebP Clip web app:
// - MP4/MOV/M4V with H.264/HEVC video: the container's sample table (stts/ctts/elst, or moof/trun
//   for fragmented MP4) gives the exact frame list. Frame f = the f-th frame in presentation order,
//   count = number of samples, fps = snapped median frame gap (first 600 frames). exactFrames() true.
// - other MP4/MOV codecs: count = stts total, fps = timescale / most frequent stts delta; f <-> f/fps.
// - MKV/WebM: fps from the video track's DefaultDuration; count = floor(duration * fps).
// - otherwise fps = smallest timestamp gap of the first 16 decoded frames (snapped).
// Not thread-safe: use one ClipSource per thread.
#include "core/Image.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bg {

struct ClipVideoInfo {
    int w = 0, h = 0;
    double fps = 30;       // detected (see above), snapped (23.976, 29.97, 60, ...)
    double duration = 0;   // seconds (container duration)
    int count = 0;         // frame count (see above)
    bool hasAudio = false;
    bool exact = false;    // frame list from the MP4 sample table (same as ClipSource::exactFrames())
};

class ClipSource {
public:
    ClipSource();
    ~ClipSource();
    bool open(const std::wstring& path, std::string& err, int cacheFrames = 48);
    const ClipVideoInfo& info() const;
    // User override of the source FPS. As in the web app: fps and count = floor(duration * fps)
    // change; with an exact frame table frame f stays the table's f-th frame (indices past the
    // table show its last frame), otherwise frame f = time f / fps.
    void setFps(double fps);
    ImagePtr frame(int f);         // null on failure; cached

    bool exactFrames() const;      // frame <-> time from the MP4 sample table
    int tableCount() const;        // frames in the table (0 when not exact)
    double frameTime(int f) const; // content time (s) of frame f: table PTS minus edit-list start, else f / fps
    int frameAt(double sec) const; // frame shown at content time sec (inverse of frameTime)
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// MP4/MOV video sample table (no decoding). ok = H.264/HEVC track with a non-empty table.
struct Mp4FrameTable {
    bool ok = false;
    std::string codec;               // sample entry fourcc
    uint32_t timescale = 0;
    int64_t shift = 0;               // edit-list media time of presentation time 0 (ticks; 0 = none)
    std::vector<int64_t> cts;        // per frame, presentation order (ticks)
    std::vector<double> times;       // (cts - (shift ? shift : cts[0])) / timescale
    double fps = 0;                  // snapped median gap
    // stts-only probe (any codec): total frames and snapped timescale / most frequent delta
    int sttsFrames = 0;
    double sttsFps = 0;
};
bool readMp4FrameTable(const std::wstring& path, Mp4FrameTable& t);
double mkvDefaultFps(const std::wstring& path);   // 0 when not MKV/WebM or no DefaultDuration

// Decodes the audio track to mono float at sampleRate (0 = keep the track's rate); false if none.
bool decodeAudioMono(const std::wstring& path, std::vector<float>& out, int& sampleRate, std::string& err);

// Per-frame audio level (RMS dBFS, count values) the way the web app computes it:
// - MP4/MOV with AAC/MP3/Opus/PCM audio: decoded at the track's native rate, power averaged over
//   channels, split at the video frames' presentation times (exact table, else f / fps); audio time
//   = sample time minus the audio edit-list start (no edit list: minus 1024 AAC priming samples).
//   Frames without audio repeat the previous level.
// - anything else: mono mix at 8000 Hz, then envelopeDb(mono, 8000, fps, count).
// false (err set) when there is no audio track / it cannot be decoded. progress(0..1) optional.
bool clipAudioLevels(const std::wstring& path, double fps, int count, std::vector<float>& db, std::string& err,
                     const std::function<void(double)>& progress = {});

} // namespace bg
