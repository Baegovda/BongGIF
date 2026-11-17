#pragma once
#include "core/Image.h"

#include <string>
#include <vector>

namespace bg {
bool decodePsd(const std::vector<uint8_t>& data, Image& out, std::string& err);
} // namespace bg
