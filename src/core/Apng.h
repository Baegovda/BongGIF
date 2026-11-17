#pragma once
// APNG writer (8-bit RGB, first frame full, later frames = changed rectangle with blend SOURCE,
// dispose NONE, identical frames merged) and reader (8-bit gray/RGB/RGBA/palette, all APNG ops).
#include "core/Gif.h"

namespace bg {

bool encodeApng(const Frames& frames, int loop, const ByteSink& out, const Progress& prog = {}, const Cancel& cancel = {});
bool decodePng(const std::vector<uint8_t>& data, Frames& out, std::string& err);

} // namespace bg
