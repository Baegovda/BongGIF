// Photoshop PSD/PSB import: reads the merged (composite) image section — 8/16-bit RGB, grayscale
// or CMYK, raw or PackBits RLE.
#include "core/Psd.h"

#include <algorithm>
#include <cstring>

namespace bg {

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

bool decodePsd(const std::vector<uint8_t>& d, Image& out, std::string& err) {
    if (d.size() < 26 || std::memcmp(d.data(), "8BPS", 4) != 0) { err = "PSD 파일이 아니에요"; return false; }
    int version = be16(&d[4]);
    int channels = be16(&d[12]);
    int H = (int)be32(&d[14]), W = (int)be32(&d[18]);
    int depth = be16(&d[22]), mode = be16(&d[24]);
    if (depth != 8 && depth != 16) { err = "8/16비트 PSD만 열 수 있어요"; return false; }
    if (mode != 3 && mode != 1 && mode != 4) { err = "RGB·흑백·CMYK PSD만 열 수 있어요"; return false; }
    size_t p = 26;
    auto skipSection = [&](bool longLen) {
        if (p + (longLen ? 8 : 4) > d.size()) return false;
        uint64_t len = longLen ? (((uint64_t)be32(&d[p]) << 32) | be32(&d[p + 4])) : be32(&d[p]);
        p += (longLen ? 8 : 4) + (size_t)len;
        return p <= d.size();
    };
    if (!skipSection(false) || !skipSection(false) || !skipSection(version == 2)) { err = "PSD 구조가 잘못됐어요"; return false; }
    if (p + 2 > d.size()) { err = "합성 이미지가 없어요"; return false; }
    int comp = be16(&d[p]);
    p += 2;
    int bpc = depth / 8;
    size_t plane = (size_t)W * H * bpc;
    std::vector<uint8_t> planes((size_t)channels * plane);
    if (comp == 0) {
        if (p + planes.size() > d.size()) { err = "PSD 데이터가 잘렸어요"; return false; }
        std::memcpy(planes.data(), &d[p], planes.size());
    } else if (comp == 1) {
        size_t rows = (size_t)channels * H, cntSize = version == 2 ? 4 : 2;
        std::vector<uint32_t> counts(rows);
        for (size_t i = 0; i < rows; ++i) {
            if (p + cntSize > d.size()) { err = "PSD 데이터가 잘렸어요"; return false; }
            counts[i] = cntSize == 4 ? be32(&d[p]) : be16(&d[p]);
            p += cntSize;
        }
        size_t rowBytes = (size_t)W * bpc;
        for (size_t r = 0; r < rows; ++r) {
            uint8_t* dst = &planes[r * rowBytes];
            size_t end = p + counts[r], o = 0;
            while (p < end && p < d.size() && o < rowBytes) {
                int n = (int8_t)d[p++];
                if (n >= 0) {
                    size_t k = std::min<size_t>(n + 1, rowBytes - o);
                    if (p + k > d.size()) break;
                    std::memcpy(dst + o, &d[p], k);
                    p += n + 1;
                    o += k;
                } else if (n != -128) {
                    size_t k = std::min<size_t>(1 - n, rowBytes - o);
                    std::memset(dst + o, p < d.size() ? d[p] : 0, k);
                    ++p;
                    o += k;
                }
            }
            p = end;
        }
    } else {
        err = "압축된 PSD(ZIP)는 열 수 없어요";
        return false;
    }
    out = Image(W, H);
    auto ch = [&](int c, size_t i) -> int {
        if (c >= channels) return c == 3 && mode == 3 ? 255 : 0;
        const uint8_t* s = &planes[c * plane + i * bpc];
        return s[0];   // 16-bit: take the high byte
    };
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        int r, g, b, a = 255;
        if (mode == 3) { r = ch(0, i); g = ch(1, i); b = ch(2, i); if (channels > 3) a = ch(3, i); }
        else if (mode == 1) { r = g = b = ch(0, i); if (channels > 1) a = ch(1, i); }
        else {   // CMYK stored inverted
            int c = 255 - ch(0, i), m = 255 - ch(1, i), y = 255 - ch(2, i), k = 255 - ch(3, i);
            r = (255 - c) * (255 - k) / 255; g = (255 - m) * (255 - k) / 255; b = (255 - y) * (255 - k) / 255;
        }
        out.px[i] = rgb((r * a + 255 * (255 - a)) / 255, (g * a + 255 * (255 - a)) / 255, (b * a + 255 * (255 - a)) / 255);
    }
    return true;
}

} // namespace bg
