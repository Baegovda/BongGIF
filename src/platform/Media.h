#pragma once
// Windows media services: MP4 (H.264, Media Foundation sink writer) export, video import
// (MF source reader), still-image import (WIC), text rendering (GDI+).
#include "core/Image.h"

#include <memory>
#include <string>
#include <windows.h>

namespace bg {

void mediaStartup();     // COM + MF + GDI+ (call once on the UI thread)
void mediaShutdown();

// quality 1..100 -> bitrate; frames are resampled to constant frame rate.
bool encodeMp4(const Frames& frames, const std::wstring& path, int quality, const Progress& prog, const Cancel& cancel,
               std::string& err);

// Streaming H.264 MP4 writer for "direct save" recording: frames go straight
// to Media Foundation with their real timestamps (variable frame rate), nothing is kept in memory.
class Mp4Stream {
public:
    Mp4Stream();
    ~Mp4Stream();
    bool open(const std::wstring& path, int w, int h, int fpsHint, int quality, std::string& err);
    bool write(const Image& im, int64_t timeUs, int64_t durUs);
    bool close();
    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// embedded super-resolution model (model 0 = ESPCN, 1 = FSRCNN; scale 2..4)
bool loadSrModelResource(int model, int scale, std::vector<uint8_t>& out);

bool setClipboardText(HWND owner, const std::wstring& text);
bool setClipboardImage(HWND owner, const Image& im);

struct VideoInfo { int w = 0, h = 0; double fps = 0, duration = 0; };
bool videoInfo(const std::wstring& path, VideoInfo& vi);
bool loadVideo(const std::wstring& path, double startS, double endS, int maxFps, int maxSide, Frames& out,
               const Progress& prog, const Cancel& cancel, std::string& err);

bool loadImageWic(const std::wstring& path, Image& out, bool keepAlpha, std::string& err);
// every frame/page (multi-page TIFF, ICO, ...) via WIC; JPEG XL/HEIC/AVIF work when Windows has the codec
bool loadFramesWic(const std::wstring& path, Frames& out, int frameMs, std::string& err);
bool saveImageWic(const Image& im, const std::wstring& path, bool jpeg, int jpegQuality, std::string& err);

struct TextStyle {
    std::wstring font = L"Malgun Gothic";
    int size = 40;
    uint32_t color = rgb(255, 255, 255);
    bool outline = true;
    bool bold = true;
};
ImagePtr drawText(const Image& src, const std::wstring& text, int x, int y, const TextStyle& st);

// Insert panel
struct BubbleStyle {
    int shape = 0;              // 0 rounded speech bubble, 1 oval, 2 thought (small circles), 3 shout (spiky)
    uint32_t fill = 0xFFFFFFFF, stroke = 0xFF202020, textColor = 0xFF202020;
    int fontSize = 28;
};
ImagePtr drawBubble(const Image& src, int x, int y, int w, int h, int tailX, int tailY, const std::wstring& text, const BubbleStyle& st);
// colour emoji sticker rendered with Segoe UI Emoji (Direct2D colour fonts)
ImagePtr drawSticker(const Image& src, const std::wstring& emoji, int x, int y, int size);
// freehand pen strokes (image coordinates), anti-aliased, round caps
struct Stroke {
    std::vector<POINT> pts;
    uint32_t color = 0xFFFF5D5D;
    float width = 6;
    bool highlighter = false;   // 40% opacity marker
};
ImagePtr drawStrokes(const Image& src, const std::vector<Stroke>& strokes);

bool readFile(const std::wstring& path, std::vector<uint8_t>& out);
std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);

} // namespace bg
