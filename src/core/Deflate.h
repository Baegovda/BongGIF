#pragma once
// DEFLATE / zlib helpers (taken from the Sliceworks project): fixed-Huffman LZ77 compressor
// and a full inflate (stored, fixed and dynamic blocks).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bg {
uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0);
uint32_t adler32(const uint8_t* p, size_t n);
std::vector<uint8_t> deflateRaw(const uint8_t* data, size_t n);
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t n);
bool inflateRaw(const uint8_t* data, size_t n, std::vector<uint8_t>& out, std::string& err);
} // namespace bg
