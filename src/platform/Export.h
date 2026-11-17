#pragma once
// Save-time pipeline: options (FPS cap, similar-frame skip, frame-time cap, size caps, progress bar,
// colour-compression pre-filter) -> format encoder.
#include "core/Gif.h"
#include "platform/Webp.h"

#include <string>

namespace bg {

// stored values (settings key `format`)
enum Format { FMT_GIF = 0, FMT_APNG = 1, FMT_MP4 = 2, FMT_WEBP = 3, FMT_WEBM = 4, FMT_AVIF = 5, FMT_PNGSEQ = 6, FMT_JPGSEQ = 7, FMT_COUNT };
extern const wchar_t* kFormatNames[FMT_COUNT];
extern const wchar_t* kFormatExt[FMT_COUNT];

struct ExportOptions {
    int format = FMT_GIF;
    int quality = 80;
    GifOptions gif;
    WebpOptions webp;
    bool webmVp9 = true;
    int jpgQuality = 90;          // nJpgExportQuality
    int loop = 0;
    int scalePct = 100;
    int maxFps = 0;               // FPS cap
    bool skipSimilar = false;     // drop near-identical frames (keeps timing)
    int maxFrameMs = 0;           // frame-time cap (0 = none)
    int maxSide = 0;              // image size cap, longest side in px (0 = none)
    bool progressBar = false;
    uint32_t barColor = rgb(255, 200, 0);
    bool colorCompress = false;   // Bandi colour compression (MP4/WebM/AVIF/lossy WebP)
};

int colorCompressLevel(const ExportOptions& o);   // 0 = off, else 3..7
Frames prepareFrames(const Frames& f, const ExportOptions& o, const Progress& prog = {});
// path empty -> encode to a byte counter only (size estimate; file formats use a temp file)
bool exportFrames(const Frames& f, const ExportOptions& o, const std::wstring& path, size_t& bytes, const Progress& prog,
                  const Cancel& cancel, std::string& err);

} // namespace bg
