#include "core/Deflate.h"

#include <algorithm>
#include <ctime>

namespace bg {

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        init = true;
    }
    uint32_t c = ~crc;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    while (n) {
        size_t k = n < 5552 ? n : 5552; // largest block without overflow
        n -= k;
        while (k--) { a += *p++; b += a; }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

namespace {

class BitWriter {
public:
    std::vector<uint8_t> out;
    void bits(uint32_t v, int n) { // LSB-first
        acc_ |= (uint64_t)v << cnt_;
        cnt_ += n;
        while (cnt_ >= 8) { out.push_back((uint8_t)acc_); acc_ >>= 8; cnt_ -= 8; }
    }
    void huff(uint32_t code, int len) { // Huffman codes are sent MSB-first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1) << (len - 1 - i);
        bits(r, len);
    }
    void flush() { if (cnt_ > 0) { out.push_back((uint8_t)acc_); acc_ = 0; cnt_ = 0; } }

private:
    uint64_t acc_ = 0;
    int cnt_ = 0;
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void fixedLitLen(BitWriter& w, int sym) {
    if (sym < 144) w.huff(0x30 + sym, 8);
    else if (sym < 256) w.huff(0x190 + (sym - 144), 9);
    else if (sym < 280) w.huff(sym - 256, 7);
    else w.huff(0xC0 + (sym - 280), 8);
}

void emitMatch(BitWriter& w, int len, int dist) {
    int li = 28;
    while (kLenBase[li] > len) --li;
    fixedLitLen(w, 257 + li);
    if (kLenExtra[li]) w.bits(len - kLenBase[li], kLenExtra[li]);
    int di = 29;
    while (kDistBase[di] > dist) --di;
    w.huff(di, 5);
    if (kDistExtra[di]) w.bits(dist - kDistBase[di], kDistExtra[di]);
}

} // namespace

std::vector<uint8_t> deflateRaw(const uint8_t* d, size_t n) {
    BitWriter w;
    w.bits(1, 1); // BFINAL
    w.bits(1, 2); // BTYPE = 01 (fixed Huffman)
    const int kHashBits = 15, kWindow = 32768, kMaxChain = 24, kMaxLen = 258;
    std::vector<int32_t> head(1 << kHashBits, -1), prev(kWindow, -1);
    auto hash = [&](size_t i) {
        return (((uint32_t)d[i] << 10) ^ ((uint32_t)d[i + 1] << 5) ^ d[i + 2]) & ((1u << kHashBits) - 1);
    };
    auto insert = [&](size_t i) {
        if (i + 2 >= n) return;
        uint32_t h = hash(i);
        prev[i & (kWindow - 1)] = head[h];
        head[h] = (int32_t)i;
    };
    size_t i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            // cheap check for runs first (distance 1), which dominate layer images
            if (i > 0 && d[i] == d[i - 1]) {
                size_t m = 0, lim = std::min<size_t>(kMaxLen, n - i);
                while (m < lim && d[i + m] == d[i - 1]) ++m;
                if (m >= 3) { bestLen = (int)m; bestDist = 1; }
            }
            if (bestLen < kMaxLen) {
                int32_t cand = head[hash(i)];
                for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain) {
                    size_t dist = i - (size_t)cand;
                    if (dist == 0 || dist > (size_t)kWindow - 1) break;
                    size_t m = 0, lim = std::min<size_t>(kMaxLen, n - i);
                    while (m < lim && d[cand + m] == d[i + m]) ++m;
                    if ((int)m > bestLen) { bestLen = (int)m; bestDist = (int)dist; if (m == lim) break; }
                    int32_t nx = prev[(size_t)cand & (kWindow - 1)];
                    if (nx >= cand) break; // stale entry from an older window
                    cand = nx;
                }
            }
        }
        if (bestLen >= 3) {
            emitMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + k);
            i += bestLen;
        } else {
            fixedLitLen(w, d[i]);
            insert(i);
            ++i;
        }
    }
    fixedLitLen(w, 256); // end of block
    w.flush();
    return std::move(w.out);
}

std::vector<uint8_t> zlibCompress(const uint8_t* d, size_t n) {
    std::vector<uint8_t> out = {0x78, 0x01};
    std::vector<uint8_t> body = deflateRaw(d, n);
    out.insert(out.end(), body.begin(), body.end());
    uint32_t a = adler32(d, n);
    out.push_back((uint8_t)(a >> 24)); out.push_back((uint8_t)(a >> 16));
    out.push_back((uint8_t)(a >> 8)); out.push_back((uint8_t)a);
    return out;
}

// ---------------------------------------------------------------- inflate (verification only)

namespace {
class BitReader {
public:
    BitReader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    bool bits(int k, uint32_t& v) {
        v = 0;
        for (int i = 0; i < k; ++i) {
            if (pos_ >= n_ * 8) return false;
            v |= (uint32_t)((d_[pos_ >> 3] >> (pos_ & 7)) & 1) << i;
            ++pos_;
        }
        return true;
    }
    void align() { pos_ = (pos_ + 7) & ~(size_t)7; }
    size_t bytePos() const { return pos_ >> 3; }
    void skipBytes(size_t k) { pos_ += k * 8; }

private:
    const uint8_t* d_;
    size_t n_, pos_ = 0;
};

struct Huff {
    std::vector<uint16_t> count, symbol;
    void build(const uint8_t* lens, int n) {
        count.assign(16, 0);
        symbol.assign(n, 0);
        for (int i = 0; i < n; ++i) count[lens[i]]++;
        count[0] = 0;
        std::vector<uint16_t> offs(16, 0);
        for (int i = 1; i < 16; ++i) offs[i] = offs[i - 1] + count[i - 1];
        for (int i = 0; i < n; ++i) if (lens[i]) symbol[offs[lens[i]]++] = (uint16_t)i;
    }
    bool decode(BitReader& br, int& sym) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            uint32_t b;
            if (!br.bits(1, b)) return false;
            code |= (int)b;
            int c = count[len];
            if (code - c < first) { sym = symbol[index + (code - first)]; return true; }
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }
};
} // namespace

bool inflateRaw(const uint8_t* data, size_t n, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    BitReader br(data, n);
    uint32_t final = 0;
    do {
        uint32_t type;
        if (!br.bits(1, final) || !br.bits(2, type)) { err = "truncated block header"; return false; }
        if (type == 0) {
            br.align();
            size_t p = br.bytePos();
            if (p + 4 > n) { err = "truncated stored block"; return false; }
            uint16_t len = (uint16_t)(data[p] | (data[p + 1] << 8));
            if (p + 4 + len > n) { err = "truncated stored data"; return false; }
            out.insert(out.end(), data + p + 4, data + p + 4 + len);
            br.skipBytes(4 + len);
            continue;
        }
        if (type == 3) { err = "bad block type"; return false; }
        uint8_t ll[320] = {}, dl[32] = {};
        int nlit = 288, ndist = 30;
        if (type == 1) {
            for (int i = 0; i < 144; ++i) ll[i] = 8;
            for (int i = 144; i < 256; ++i) ll[i] = 9;
            for (int i = 256; i < 280; ++i) ll[i] = 7;
            for (int i = 280; i < 288; ++i) ll[i] = 8;
            for (int i = 0; i < 30; ++i) dl[i] = 5;
        } else {
            // dynamic Huffman block (written by other ZIP tools, e.g. vendor sample files)
            uint32_t hlit, hdist, hclen;
            if (!br.bits(5, hlit) || !br.bits(5, hdist) || !br.bits(4, hclen)) { err = "truncated dynamic header"; return false; }
            nlit = (int)hlit + 257;
            ndist = (int)hdist + 1;
            static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cl[19] = {};
            for (uint32_t i = 0; i < hclen + 4; ++i) {
                uint32_t v;
                if (!br.bits(3, v)) { err = "truncated code lengths"; return false; }
                cl[kOrder[i]] = (uint8_t)v;
            }
            Huff clh;
            clh.build(cl, 19);
            uint8_t lens[320] = {};
            for (int i = 0; i < nlit + ndist;) {
                int sym;
                if (!clh.decode(br, sym)) { err = "bad code length symbol"; return false; }
                if (sym < 16) { lens[i++] = (uint8_t)sym; continue; }
                uint32_t rep = 0;
                uint8_t val = 0;
                if (sym == 16) {
                    if (i == 0 || !br.bits(2, rep)) { err = "bad repeat"; return false; }
                    val = lens[i - 1];
                    rep += 3;
                } else if (sym == 17) {
                    if (!br.bits(3, rep)) { err = "truncated"; return false; }
                    rep += 3;
                } else {
                    if (!br.bits(7, rep)) { err = "truncated"; return false; }
                    rep += 11;
                }
                if (i + (int)rep > nlit + ndist) { err = "code lengths overflow"; return false; }
                while (rep--) lens[i++] = val;
            }
            std::copy(lens, lens + nlit, ll);
            std::copy(lens + nlit, lens + nlit + ndist, dl);
        }
        Huff lit, dist;
        lit.build(ll, nlit);
        dist.build(dl, ndist);
        for (;;) {
            int sym;
            if (!lit.decode(br, sym)) { err = "bad literal"; return false; }
            if (sym < 256) { out.push_back((uint8_t)sym); continue; }
            if (sym == 256) break;
            sym -= 257;
            if (sym >= 29) { err = "bad length"; return false; }
            uint32_t e = 0;
            if (!br.bits(kLenExtra[sym], e)) { err = "truncated"; return false; }
            int len = kLenBase[sym] + (int)e;
            int ds;
            if (!dist.decode(br, ds) || ds >= 30) { err = "bad distance"; return false; }
            if (!br.bits(kDistExtra[ds], e)) { err = "truncated"; return false; }
            size_t dd = kDistBase[ds] + e;
            if (dd > out.size()) { err = "distance too far"; return false; }
            size_t from = out.size() - dd;
            for (int k = 0; k < len; ++k) out.push_back(out[from + k]);
        }
    } while (!final);
    return true;
}

} // namespace bg
