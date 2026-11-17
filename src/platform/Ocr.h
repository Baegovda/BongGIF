#pragma once
#include "core/Image.h"

#include <string>

namespace bg {
// Windows OCR (Korean if the language pack exists, otherwise the user's languages). Worker thread only.
bool ocrImage(const Image& im, std::wstring& text, std::string& err);
} // namespace bg
