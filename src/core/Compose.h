#pragma once
// Frame compositing tools from edit panels: collage, zoom animation (FxCam), photo frames.
#include "core/Image.h"

#include <vector>

namespace bg {

// Several animations side by side / stacked / in a grid. Each cell letterboxes its animation;
// shorter animations loop until the longest one ends. The output timeline merges all frame changes.
enum CollageLayout { CL_ROW = 0, CL_COLUMN = 1, CL_GRID = 2 };
Frames collage(const std::vector<Frames>& anims, int layout, int cellW, int cellH, int gap, uint32_t bg);

// Zoom into a rectangle starting at frame `start`: zoom-in over `inFrames`, hold for `holdFrames`,
// optionally zoom back out over `outFrames`. Frames are appended (copies of the last one, frameUs each)
// when the clip is shorter than the effect.
Frames zoomAnimation(const Frames& f, int start, int rx, int ry, int rw, int rh, int inFrames, int holdFrames, int outFrames,
                     int64_t frameUs);

// Photo frames ("액자"): 0 solid border, 1 rounded corners, 2 polaroid, 3 film strip, 4 shadow mat, 5 soft white edge
enum FrameStyle { FS_BORDER = 0, FS_ROUNDED, FS_POLAROID, FS_FILM, FS_SHADOW, FS_SOFT };
ImagePtr photoFrame(const Image& src, int style, int thickness, uint32_t color);

} // namespace bg
