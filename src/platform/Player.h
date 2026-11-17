#pragma once
// Video preview player for the import dialog: Media Foundation Media Engine in frame-server mode
// (hardware decode, audio through the default output), frames copied to BGRA Images for Direct2D.
// Plus a thumbnail strip extractor that runs on a worker thread.
#include "core/Image.h"

#include <functional>
#include <memory>
#include <string>

namespace bg {

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();
    bool open(const std::wstring& path, int maxW, int maxH, std::string& err);   // frames fit maxW x maxH
    ImagePtr frame();          // the newest decoded frame since the last call, or null
    void play();
    void pause();
    bool paused() const;
    void seek(double seconds); // coalesces while a seek is running
    double time() const;
    double duration() const;
    void setMuted(bool m);
    void setVolume(double v);   // 0..1
    void setRate(double r);     // playback rate (slow motion preview)
    int frameWidth() const;     // size of frames returned by frame() (0 until the first one)
    int frameHeight() const;
    bool hasAudio() const;
    bool failed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// n evenly spaced thumbnails, each thumbH pixels high; onThumb(i, image) is called from this thread
// as each one is ready. Returns false when the video cannot be read.
bool videoThumbnails(const std::wstring& path, int n, int thumbH, const std::function<void(int, ImagePtr)>& onThumb,
                     const std::function<bool()>& cancel);

} // namespace bg
