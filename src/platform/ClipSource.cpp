#include "platform/ClipSource.h"

#include "core/Clip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <list>
#include <numeric>
#include <share.h>
#include <unordered_map>
#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace bg {

namespace {

// ================================================================ MP4 / MOV boxes

constexpr uint32_t FC(const char (&s)[5]) {
    return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) | ((uint32_t)(uint8_t)s[2] << 8) | (uint8_t)s[3];
}

// bounds-checked big-endian reads (out of range reads give 0)
struct Bytes {
    std::vector<uint8_t> v;
    size_t size() const { return v.size(); }
    uint8_t u8(size_t o) const { return o < v.size() ? v[o] : 0; }
    uint16_t u16(size_t o) const { return (uint16_t)((u8(o) << 8) | u8(o + 1)); }
    uint32_t u32(size_t o) const {
        if (o + 4 > v.size() || o + 4 < o) return 0;
        return ((uint32_t)v[o] << 24) | ((uint32_t)v[o + 1] << 16) | ((uint32_t)v[o + 2] << 8) | v[o + 3];
    }
    int32_t i32(size_t o) const { return (int32_t)u32(o); }
    uint64_t u64(size_t o) const { return ((uint64_t)u32(o) << 32) | u32(o + 4); }
};

struct Box {
    uint32_t type = 0;
    size_t start = 0, end = 0;
    bool ok = false;
};

std::vector<Box> boxes(const Bytes& b, size_t start, size_t end) {
    std::vector<Box> out;
    end = std::min(end, b.size());
    for (size_t o = start; o + 8 <= end;) {
        uint64_t size = b.u32(o);
        size_t head = 8;
        if (size == 1) { size = b.u64(o + 8); head = 16; }
        else if (size == 0) size = end - o;
        if (size < head) break;
        out.push_back({b.u32(o + 4), o + head, (size_t)std::min<uint64_t>(end, o + size), true});
        if (size > end - o) break;
        o += (size_t)size;
    }
    return out;
}

Box child(const Bytes& b, const Box& box, uint32_t t) {
    if (!box.ok) return {};
    for (const Box& x : boxes(b, box.start, box.end))
        if (x.type == t) return x;
    return {};
}

// sequential file access (shared read; the user's file is never written)
struct File {
    FILE* f = nullptr;
    uint64_t size = 0;
    explicit File(const std::wstring& path) {
        f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
        if (f && _fseeki64(f, 0, SEEK_END) == 0) size = (uint64_t)_ftelli64(f);
    }
    ~File() { if (f) std::fclose(f); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    bool read(uint64_t off, size_t n, Bytes& out) {
        out.v.clear();
        if (!f || off >= size) return false;
        n = (size_t)std::min<uint64_t>(n, size - off);
        out.v.resize(n);
        if (_fseeki64(f, (long long)off, SEEK_SET) != 0) return false;
        out.v.resize(std::fread(out.v.data(), 1, n, f));
        return true;
    }
};

struct Moof { uint64_t offset = 0, size = 0; size_t head = 8; };
struct Mp4Index {
    bool ok = false;
    Bytes moov;
    std::vector<Moof> moofs;
};

// top-level boxes: moov content and moof positions (the file must start with ftyp)
Mp4Index scanTop(File& file) {
    Mp4Index ix;
    Bytes h;
    bool haveMoov = false;
    for (uint64_t off = 0; off + 8 <= file.size;) {
        if (!file.read(off, 16, h) || h.size() < 8) break;
        uint64_t size = h.u32(0);
        size_t head = 8;
        if (size == 1) { size = h.u64(8); head = 16; }
        else if (size == 0) size = file.size - off;
        if (size < head) break;
        uint32_t t = h.u32(4);
        if (off == 0 && t != FC("ftyp")) return ix;
        if (t == FC("moov")) {
            if (size - head > (512u << 20)) return ix;   // implausible
            file.read(off + head, (size_t)(size - head), ix.moov);
            haveMoov = true;
        } else if (t == FC("moof")) {
            ix.moofs.push_back({off, size, head});
        }
        off += size;
    }
    ix.ok = haveMoov;
    return ix;
}

struct TrackDesc {
    bool ok = false;
    Box stbl, entry;
    uint32_t timescale = 0, id = 0;
    int64_t shift = 0;
    uint32_t dDur = 0, dSize = 0, dFlags = 0;   // trex defaults
};

// the first track with this handler ("vide" / "soun") that has a sample table and an entry
TrackDesc findTrack(const Bytes& moov, uint32_t handler) {
    TrackDesc t;
    for (const Box& trak : boxes(moov, 0, moov.size())) {
        if (trak.type != FC("trak")) continue;
        Box mdia = child(moov, trak, FC("mdia"));
        Box hdlr = child(moov, mdia, FC("hdlr"));
        if (!hdlr.ok || moov.u32(hdlr.start + 8) != handler) continue;
        Box mdhd = child(moov, mdia, FC("mdhd"));
        if (!mdhd.ok) continue;
        t.timescale = moov.u32(mdhd.start + (moov.u8(mdhd.start) == 1 ? 20 : 12));
        t.stbl = child(moov, child(moov, mdia, FC("minf")), FC("stbl"));
        Box stsd = child(moov, t.stbl, FC("stsd"));
        t.entry = {};
        if (stsd.ok) {
            auto es = boxes(moov, stsd.start + 8, stsd.end);
            if (!es.empty()) t.entry = es[0];
        }
        Box tkhd = child(moov, trak, FC("tkhd"));
        t.id = tkhd.ok ? moov.u32(tkhd.start + (moov.u8(tkhd.start) == 1 ? 20 : 12)) : 0;
        // edit list: media time shown at presentation time 0 (first non-empty edit)
        Box elst = child(moov, child(moov, trak, FC("edts")), FC("elst"));
        t.shift = 0;
        if (elst.ok) {
            bool v1 = moov.u8(elst.start) == 1;
            uint32_t n = moov.u32(elst.start + 4);
            size_t o = elst.start + 8;
            for (uint32_t i = 0; i < n && o < elst.end; ++i, o += v1 ? 20 : 12) {
                int64_t mt = v1 ? (int64_t)moov.u64(o + 8) : (int64_t)moov.i32(o + 4);
                if (mt >= 0 && !(v1 && moov.u32(o + 8) == 0xffffffffu)) { t.shift = mt; break; }
            }
        }
        // fragmented MP4 defaults (moov > mvex > trex)
        t.dDur = t.dSize = t.dFlags = 0;
        for (const Box& mv : boxes(moov, 0, moov.size())) {
            if (mv.type != FC("mvex")) continue;
            for (const Box& x : boxes(moov, mv.start, mv.end))
                if (x.type == FC("trex") && moov.u32(x.start + 4) == t.id) {
                    t.dDur = moov.u32(x.start + 12);
                    t.dSize = moov.u32(x.start + 16);
                    t.dFlags = moov.u32(x.start + 20);
                    goto trexDone;
                }
        }
    trexDone:
        if (t.stbl.ok && t.entry.ok) { t.ok = true; return t; }
    }
    return TrackDesc{};
}

constexpr size_t kMaxSamples = 20000000;

// composition times of a regular (moov) sample table, in decode order. false = malformed.
bool sampleTable(const Bytes& m, const Box& stbl, std::vector<int64_t>& cts) {
    cts.clear();
    Box stsz = child(m, stbl, FC("stsz"));
    if (!stsz.ok) return false;
    uint64_t n = m.u32(stsz.start + 8);
    if (n == 0) return true;
    if (n > kMaxSamples) return false;
    Box stco = child(m, stbl, FC("stco")), co64 = child(m, stbl, FC("co64"));
    if (!stco.ok && !co64.ok) return false;
    int64_t chunks = stco.ok ? m.u32(stco.start + 4) : m.u32(co64.start + 4);
    Box stsc = child(m, stbl, FC("stsc")), stts = child(m, stbl, FC("stts")), ctts = child(m, stbl, FC("ctts"));
    if (!stsc.ok || !stts.ok) return false;
    // samples that belong to a chunk (the table is cut there)
    uint32_t nr = std::min<uint32_t>(m.u32(stsc.start + 4), (uint32_t)((stsc.end - stsc.start) / 12 + 1));
    uint64_t s = 0;
    for (uint32_t r = 0; r < nr && s < n; ++r) {
        int64_t first = (int64_t)m.u32(stsc.start + 8 + (size_t)r * 12) - 1;
        uint64_t per = m.u32(stsc.start + 12 + (size_t)r * 12);
        int64_t last = r + 1 < nr ? (int64_t)m.u32(stsc.start + 8 + (size_t)(r + 1) * 12) - 1 : chunks;
        if (per == 0 || last <= first) continue;
        s = std::min<uint64_t>(n, s + (uint64_t)(last - first) * per);
    }
    std::vector<int64_t> c((size_t)n, 0);
    int64_t dts = 0;
    uint32_t ne = m.u32(stts.start + 4);
    size_t k = 0;
    for (uint32_t i = 0; i < ne && k < n; ++i) {
        uint32_t cnt = m.u32(stts.start + 8 + (size_t)i * 8), d = m.u32(stts.start + 12 + (size_t)i * 8);
        for (uint32_t j = 0; j < cnt && k < n; ++j, ++k) { c[k] = dts; dts += d; }
    }
    if (ctts.ok) {
        bool v1 = m.u8(ctts.start) == 1;
        uint32_t nc = m.u32(ctts.start + 4);
        k = 0;
        for (uint32_t i = 0; i < nc && k < n; ++i) {
            uint32_t cnt = m.u32(ctts.start + 8 + (size_t)i * 8);
            int64_t o = v1 ? (int64_t)m.i32(ctts.start + 12 + (size_t)i * 8) : (int64_t)m.u32(ctts.start + 12 + (size_t)i * 8);
            for (uint32_t j = 0; j < cnt && k < n; ++j, ++k) c[k] += o;
        }
    }
    c.resize((size_t)s);
    cts.swap(c);
    return true;
}

// fragmented MP4: moof > traf > tfhd / tfdt / trun of this track
void fragmentSamples(File& file, const std::vector<Moof>& moofs, const TrackDesc& tr, std::vector<int64_t>& cts) {
    cts.clear();
    int64_t nextDts = 0;
    Bytes b;
    for (const Moof& mf : moofs) {
        if (mf.size > (256u << 20) || !file.read(mf.offset, (size_t)mf.size, b)) continue;
        for (const Box& traf : boxes(b, mf.head, b.size())) {
            if (traf.type != FC("traf")) continue;
            Box tfhd = child(b, traf, FC("tfhd"));
            if (!tfhd.ok || b.u32(tfhd.start + 4) != tr.id) continue;
            uint32_t tf = b.u32(tfhd.start) & 0xffffff;
            size_t o = tfhd.start + 8;
            if (tf & 0x1) o += 8;   // base data offset
            if (tf & 0x2) o += 4;   // sample description index
            uint32_t dDur = tr.dDur;
            if (tf & 0x8) { dDur = b.u32(o); o += 4; }
            if (tf & 0x10) o += 4;
            if (tf & 0x20) o += 4;
            Box tfdt = child(b, traf, FC("tfdt"));
            if (tfdt.ok) nextDts = b.u8(tfdt.start) == 1 ? (int64_t)b.u64(tfdt.start + 4) : (int64_t)b.u32(tfdt.start + 4);
            for (const Box& trun : boxes(b, traf.start, traf.end)) {
                if (trun.type != FC("trun")) continue;
                bool v1 = b.u8(trun.start) == 1;
                uint32_t fl = b.u32(trun.start) & 0xffffff, n = b.u32(trun.start + 4);
                size_t p = trun.start + 8;
                if (fl & 0x1) p += 4;
                if (fl & 0x4) p += 4;
                for (uint32_t i = 0; i < n && cts.size() < kMaxSamples; ++i) {
                    uint32_t dur = dDur;
                    if (fl & 0x100) { dur = b.u32(p); p += 4; }
                    if (fl & 0x200) p += 4;
                    if (fl & 0x400) p += 4;
                    int64_t ct = 0;
                    if (fl & 0x800) { ct = v1 ? (int64_t)b.i32(p) : (int64_t)b.u32(p); p += 4; }
                    cts.push_back(nextDts + ct);
                    nextDts += dur;
                }
            }
        }
    }
}

bool trackSamples(File& file, const Mp4Index& ix, const TrackDesc& tr, std::vector<int64_t>& cts) {
    if (!sampleTable(ix.moov, tr.stbl, cts)) return false;
    if (cts.empty() && !ix.moofs.empty()) fragmentSamples(file, ix.moofs, tr, cts);
    return true;
}

// stts-only probe of the first video track: total frames and the most frequent delta
void probeStts(const Bytes& moov, Mp4FrameTable& t) {
    for (const Box& trak : boxes(moov, 0, moov.size())) {
        if (trak.type != FC("trak")) continue;
        Box mdia = child(moov, trak, FC("mdia"));
        Box hdlr = child(moov, mdia, FC("hdlr"));
        if (!hdlr.ok || moov.u32(hdlr.start + 8) != FC("vide")) continue;
        Box mdhd = child(moov, mdia, FC("mdhd"));
        uint32_t ts = mdhd.ok ? moov.u32(mdhd.start + (moov.u8(mdhd.start) == 1 ? 20 : 12)) : 0;
        Box stts = child(moov, child(moov, child(moov, mdia, FC("minf")), FC("stbl")), FC("stts"));
        if (!stts.ok || !ts) return;
        uint32_t n = moov.u32(stts.start + 4);
        uint64_t frames = 0, best = 0, bestDelta = 0;
        for (uint32_t i = 0; i < n && stts.start + 16 + (size_t)i * 8 <= stts.end; ++i) {
            uint32_t c = moov.u32(stts.start + 8 + (size_t)i * 8), d = moov.u32(stts.start + 12 + (size_t)i * 8);
            frames += c;
            if (c > best) { best = c; bestDelta = d; }
        }
        if (!frames || !bestDelta || frames > (uint64_t)INT32_MAX) return;
        t.sttsFrames = (int)frames;
        t.sttsFps = snapFps((double)ts / (double)bestDelta);
        return;
    }
}

bool readFrameTable(File& file, const Mp4Index& ix, Mp4FrameTable& t) {
    t = {};
    if (!ix.ok) return false;
    const Bytes& m = ix.moov;
    probeStts(m, t);
    TrackDesc tr = findTrack(m, FC("vide"));
    if (!tr.ok || !tr.timescale) return false;
    uint32_t et = tr.entry.type;
    if (et != FC("avc1") && et != FC("avc3") && et != FC("hvc1") && et != FC("hev1")) return false;
    bool cfg = false;
    for (const Box& x : boxes(m, tr.entry.start + 78, tr.entry.end))
        if (x.type == FC("avcC") || x.type == FC("hvcC")) cfg = true;
    if (!cfg) return false;
    char cc[5] = {(char)(et >> 24), (char)(et >> 16), (char)(et >> 8), (char)et, 0};
    t.codec = cc;
    std::vector<int64_t> cts;
    if (!trackSamples(file, ix, tr, cts) || cts.empty()) return false;
    // presentation order (stable for equal times, like the JS sort)
    std::vector<size_t> ord(cts.size());
    std::iota(ord.begin(), ord.end(), 0);
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return cts[a] < cts[b]; });
    t.timescale = tr.timescale;
    t.shift = tr.shift;
    t.cts.resize(cts.size());
    for (size_t i = 0; i < ord.size(); ++i) t.cts[i] = cts[ord[i]];
    const int64_t base = tr.shift ? tr.shift : t.cts[0];
    t.times.resize(t.cts.size());
    for (size_t i = 0; i < t.cts.size(); ++i) t.times[i] = (double)(t.cts[i] - base) / tr.timescale;
    std::vector<double> gaps;
    for (size_t f = 1; f < std::min<size_t>(t.times.size(), 600); ++f) gaps.push_back(t.times[f] - t.times[f - 1]);
    std::sort(gaps.begin(), gaps.end());
    double g = gaps.empty() ? 0 : gaps[gaps.size() >> 1];
    t.fps = snapFps(g > 0 ? 1 / g : 30);
    t.ok = true;
    return true;
}

// ================================================================ MKV / WebM

double probeMkv(File& file) {
    Bytes b;
    if (!file.read(0, 256 * 1024, b) || b.size() < 4) return 0;
    if (b.u8(0) != 0x1a || b.u8(1) != 0x45 || b.u8(2) != 0xdf || b.u8(3) != 0xa3) return 0;
    struct VInt { double v = 0; int len = 0; bool unknown = false; bool ok = false; };
    auto vint = [&](size_t o, bool keepMarker) {
        VInt r;
        uint8_t first = b.u8(o);
        int len = 1;
        while (len <= 8 && !(first & (0x80 >> (len - 1)))) ++len;
        if (len > 8) return r;
        double v = keepMarker ? first : (first & (0xff >> len));
        for (int i = 1; i < len; ++i) v = v * 256 + b.u8(o + i);
        r.v = v;
        r.len = len;
        r.unknown = !keepMarker && v == std::pow(2.0, 7.0 * len) - 1;
        r.ok = true;
        return r;
    };
    const double SEGMENT = 0x18538067, TRACKS = 0x1654ae6b, ENTRY = 0xae, TYPE = 0x83, DURATION = 0x23e383;
    std::function<double(size_t, size_t)> walk = [&](size_t start, size_t end) -> double {
        int type = 0;
        double dur = 0;
        for (size_t o = start; o < end && o + 2 < b.size();) {
            VInt id = vint(o, true);
            if (!id.ok) return 0;
            VInt size = vint(o + id.len, false);
            if (!size.ok) return 0;
            size_t body = o + id.len + size.len;
            size_t stop = size.unknown ? end : (size_t)std::min<double>((double)end, (double)body + size.v);
            if (stop <= o) return 0;
            if (id.v == SEGMENT || id.v == TRACKS || id.v == ENTRY) {
                double r = walk(body, stop);
                if (r > 0) return r;
            } else if (id.v == TYPE) {
                type = b.u8(body);
            } else if (id.v == DURATION) {
                dur = 0;
                for (int i = 0; i < (int)size.v && i < 8; ++i) dur = dur * 256 + b.u8(body + i);
            }
            o = stop;
        }
        return type == 1 && dur > 0 ? 1e9 / dur : 0;
    };
    double fps = walk(0, b.size());
    return fps > 1 && fps < 1000 ? snapFps(fps) : 0;
}

// ================================================================ Media Foundation helpers

// COM for the calling thread (UI STA or worker); an existing apartment of another mode is fine
struct ComScope {
    bool ok;
    ComScope() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() { if (ok) CoUninitialize(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

const DWORD kVideo = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
const DWORD kAudio = (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;

// output geometry of the current RGB32 type: frame size, visible area, default stride
struct Geometry {
    UINT32 fw = 0, fh = 0;    // decoded buffer size
    int x0 = 0, y0 = 0, w = 0, h = 0;   // visible area
    LONG stride = 0;
};

Geometry readGeometry(IMFSourceReader* rd) {
    Geometry g;
    ComPtr<IMFMediaType> t;
    if (FAILED(rd->GetCurrentMediaType(kVideo, &t))) return g;
    MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &g.fw, &g.fh);
    g.w = (int)g.fw;
    g.h = (int)g.fh;
    MFVideoArea area{};
    UINT32 sz = 0;
    if (SUCCEEDED(t->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), &sz)) && sz == sizeof(area)) {
        int ax = area.OffsetX.value, ay = area.OffsetY.value;
        int aw = (int)area.Area.cx, ah = (int)area.Area.cy;
        if (aw > 0 && ah > 0 && ax >= 0 && ay >= 0 && ax + aw <= (int)g.fw && ay + ah <= (int)g.fh) {
            g.x0 = ax; g.y0 = ay; g.w = aw; g.h = ah;
        }
    }
    g.stride = (LONG)(g.fw * 4);
    UINT32 st = 0;
    if (SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) g.stride = (LONG)(INT32)st;
    return g;
}

// copies one RGB32 sample into an opaque Image (visible area only)
ImagePtr sampleToImage(IMFSample* s, const Geometry& g) {
    if (!s || g.w <= 0 || g.h <= 0) return nullptr;
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(s->ConvertToContiguousBuffer(&buf))) return nullptr;
    auto im = std::make_shared<Image>(g.w, g.h);
    ComPtr<IMF2DBuffer> b2;
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    bool locked2d = SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2D(&scan0, &pitch));
    BYTE* p = nullptr;
    DWORD len = 0;
    if (!locked2d) {
        if (FAILED(buf->Lock(&p, nullptr, &len))) return nullptr;
        pitch = g.stride;
        // a negative default stride means bottom-up rows in a plain buffer
        scan0 = pitch >= 0 ? p : p + (size_t)(g.fh - 1) * (size_t)(-pitch);
    }
    for (int y = 0; y < g.h; ++y) {
        const BYTE* src = scan0 + (ptrdiff_t)(y + g.y0) * pitch + (size_t)g.x0 * 4;
        if (!locked2d && (src < p || (size_t)(src - p) + (size_t)g.w * 4 > len)) break;
        const uint32_t* s32 = (const uint32_t*)src;
        uint32_t* d = im->row(y);
        for (int x = 0; x < g.w; ++x) d[x] = s32[x] | 0xFF000000u;
    }
    if (locked2d) b2->Unlock2D();
    else buf->Unlock();
    return im;
}

bool openVideoReader(const std::wstring& path, ComPtr<IMFSourceReader>& rd) {
    ComPtr<IMFAttributes> a;
    MFCreateAttributes(&a, 1);
    a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), a.Get(), &rd))) return false;
    rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(rd->SetStreamSelection(kVideo, TRUE))) return false;
    ComPtr<IMFMediaType> t;
    MFCreateMediaType(&t);
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    return SUCCEEDED(rd->SetCurrentMediaType(kVideo, nullptr, t.Get()));
}

// index of the value in sorted v nearest to x
size_t nearestIndex(const std::vector<double>& v, double x) {
    auto it = std::lower_bound(v.begin(), v.end(), x);
    size_t i = (size_t)(it - v.begin());
    if (i >= v.size()) return v.size() - 1;
    if (i > 0 && x - v[i - 1] < v[i] - x) return i - 1;
    return i;
}

// frame shown at content time sec in a presentation-time table (web app contentFrameAt)
int tableFrameAt(const std::vector<double>& t, double sec) {
    if (t.empty() || sec <= t[0]) return 0;
    size_t lo = 0, hi = t.size() - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) >> 1;
        if (t[mid] <= sec + 1e-4) lo = mid;
        else hi = mid - 1;
    }
    return (int)lo;
}

} // namespace

bool readMp4FrameTable(const std::wstring& path, Mp4FrameTable& t) {
    t = {};
    File file(path);
    if (!file.f) return false;
    Mp4Index ix = scanTop(file);
    return readFrameTable(file, ix, t);
}

double mkvDefaultFps(const std::wstring& path) {
    File file(path);
    return file.f ? probeMkv(file) : 0;
}

// ================================================================ ClipSource

struct ClipSource::Impl {
    ComPtr<IMFSourceReader> rd;
    ClipVideoInfo info;
    Geometry geo;
    // exact mode: frame f = table frame f; MF time (100 ns) = raw[f] * 1e7 + mfOff
    bool exact = false;
    std::vector<double> raw;    // cts / timescale, presentation order
    std::vector<double> times;  // content time per frame
    LONGLONG mfOff = 0;
    LONGLONG offset = 0;        // other modes: first sample time (100 ns), subtracted so frame 0 = first frame
    int lastIdx = -1;           // index of the last sample read from the reader (-1: none / after seek)
    ImagePtr lastImg;
    bool eos = false;
    bool comInit = false, mfInit = false;
    size_t cap = 48;
    // LRU cache: front = most recent
    std::list<std::pair<int, ImagePtr>> lru;
    std::unordered_map<int, std::list<std::pair<int, ImagePtr>>::iterator> map;

    ~Impl() {
        clearCache();
        lastImg.reset();
        rd.Reset();
        if (mfInit) MFShutdown();
        if (comInit) CoUninitialize();
    }
    void clearCache() { lru.clear(); map.clear(); }
    ImagePtr get(int f) {
        auto it = map.find(f);
        if (it == map.end()) return nullptr;
        lru.splice(lru.begin(), lru, it->second);
        return it->second->second;
    }
    void put(int f, const ImagePtr& im) {
        auto it = map.find(f);
        if (it != map.end()) {
            it->second->second = im;
            lru.splice(lru.begin(), lru, it->second);
            return;
        }
        lru.emplace_front(f, im);
        map[f] = lru.begin();
        while (lru.size() > cap) {
            map.erase(lru.back().first);
            lru.pop_back();
        }
    }
    int indexOf(LONGLONG ts) const {
        if (exact) return (int)nearestIndex(raw, (double)(ts - mfOff) / 1e7);
        double s = (double)(ts - offset) / 1e7;
        return (int)std::lround(s * info.fps);
    }
    // reads one sample; false at end of stream / error. img null for gap/tick samples.
    bool readOne(ImagePtr& img, LONGLONG& ts) {
        img.reset();
        DWORD flags = 0;
        ComPtr<IMFSample> s;
        if (FAILED(rd->ReadSample(kVideo, 0, nullptr, &flags, &ts, &s))) { eos = true; return false; }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) geo = readGeometry(rd.Get());
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) { eos = true; return false; }
        if (s) img = sampleToImage(s.Get(), geo);
        return true;
    }
    LONGLONG timeOfIndex(int f) const {   // reader position of frame f (100 ns)
        if (exact) return (LONGLONG)std::llround(raw[(size_t)std::clamp(f, 0, (int)raw.size() - 1)] * 1e7) + mfOff;
        return offset + (LONGLONG)std::llround(f / info.fps * 1e7);
    }
    void seek(LONGLONG pos) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = std::max<LONGLONG>(0, pos);
        rd->SetCurrentPosition(GUID_NULL, pv);
        lastIdx = -1;
        lastImg.reset();
        eos = false;
    }
    // decodes forward from the current reader position until index >= f; caches everything passed
    ImagePtr decodeTo(int f, bool& overshot) {
        overshot = false;
        ImagePtr before = (lastIdx >= 0 && lastIdx <= f) ? lastImg : nullptr;
        bool first = true;
        for (;;) {
            ImagePtr img;
            LONGLONG ts = 0;
            if (!readOne(img, ts)) break;
            if (!img) continue;
            int idx = indexOf(ts);
            lastIdx = idx;
            lastImg = img;
            put(idx, img);
            if (idx == f) return img;
            if (idx > f) {
                if (first) overshot = true;
                return before ? before : img;
            }
            before = img;
            first = false;
        }
        return before ? before : lastImg;   // end of stream: last decoded frame
    }
};

ClipSource::ClipSource() : d_(new Impl) {}
ClipSource::~ClipSource() = default;

const ClipVideoInfo& ClipSource::info() const { return d_->info; }
bool ClipSource::exactFrames() const { return d_->exact; }
int ClipSource::tableCount() const { return d_->exact ? (int)d_->raw.size() : 0; }

double ClipSource::frameTime(int f) const {
    const Impl& d = *d_;
    if (d.exact) return d.times[(size_t)std::clamp(f, 0, (int)d.times.size() - 1)];
    return f / d.info.fps;
}

int ClipSource::frameAt(double sec) const {
    const Impl& d = *d_;
    if (d.exact) return tableFrameAt(d.times, sec);
    return std::clamp((int)std::floor(sec * d.info.fps + 1e-9), 0, std::max(0, d.info.count - 1));
}

bool ClipSource::open(const std::wstring& path, std::string& err, int cacheFrames) {
    d_.reset(new Impl);
    Impl& d = *d_;
    // held for the lifetime of the reader (released in ~Impl on the same thread)
    d.comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    d.mfInit = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    d.cap = (size_t)std::max(1, cacheFrames);

    // container tables first (no decoding)
    Mp4FrameTable tab;
    double mkvFps = 0;
    {
        File file(path);
        if (file.f) {
            Mp4Index ix = scanTop(file);
            readFrameTable(file, ix, tab);
            if (!ix.ok) mkvFps = probeMkv(file);
        }
    }

    if (!openVideoReader(path, d.rd)) { d.rd.Reset(); err = "cannot open video"; return false; }
    d.geo = readGeometry(d.rd.Get());
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(d.rd->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)))
        d.info.duration = (double)pv.uhVal.QuadPart / 1e7;
    PropVariantClear(&pv);
    {   // audio presence: probe the native type of the first audio stream
        ComPtr<IMFMediaType> at;
        d.info.hasAudio = SUCCEEDED(d.rd->GetNativeMediaType(kAudio, 0, &at));
    }

    // first decoded samples (up to 16 when the FPS has to be measured from timestamps)
    const bool measure = !tab.ok && tab.sttsFrames <= 0 && mkvFps <= 0;
    std::vector<std::pair<LONGLONG, ImagePtr>> head;
    for (int tries = 0; tries < 64 && (int)head.size() < (measure ? 16 : 1); ++tries) {
        ImagePtr img;
        LONGLONG ts = 0;
        if (!d.readOne(img, ts)) break;
        if (img) head.push_back({ts, img});
    }
    if (head.empty()) { d.rd.Reset(); err = "cannot decode video"; return false; }
    d.info.w = d.geo.w;   // the type may have changed on the first sample
    d.info.h = d.geo.h;

    const LONGLONG ts0 = head[0].first;
    if (tab.ok) {
        d.exact = true;
        d.info.exact = true;
        d.info.fps = tab.fps;
        d.info.count = (int)tab.cts.size();
        d.times = tab.times;
        d.raw.resize(tab.cts.size());
        for (size_t i = 0; i < tab.cts.size(); ++i) d.raw[i] = (double)tab.cts[i] / tab.timescale;
        // how the reader's clock relates to the table: raw composition time, or edit-list shifted
        // (checked at the first frame, then at any frame in case leading frames were dropped)
        const double t0 = (double)ts0 / 1e7, sh = (double)tab.shift / tab.timescale, tol = 0.0015;
        double off;
        if (std::abs(t0 - d.raw[0]) < tol) off = 0;
        else if (std::abs(t0 - (d.raw[0] - sh)) < tol) off = -sh;
        else if (std::abs(d.raw[nearestIndex(d.raw, t0)] - t0) < tol) off = 0;
        else if (std::abs(d.raw[nearestIndex(d.raw, t0 + sh)] - (t0 + sh)) < tol) off = -sh;
        else off = t0 - d.raw[0];
        d.mfOff = (LONGLONG)std::llround(off * 1e7);
    } else {
        d.offset = ts0;
        if (tab.sttsFrames > 0) {
            d.info.fps = tab.sttsFps;
            d.info.count = tab.sttsFrames;
        } else {
            double fps = mkvFps;
            if (fps <= 0) {   // smallest timestamp gap of the first frames
                double minGap = 0;
                for (size_t i = 1; i < head.size(); ++i) {
                    double g = (double)(head[i].first - head[i - 1].first) / 1e7;
                    if (g > 1e-4 && (minGap == 0 || g < minGap)) minGap = g;
                }
                fps = minGap > 0 ? snapFps(1 / minGap) : 30;
            }
            d.info.fps = fps;
            d.info.count = std::max(1, (int)std::floor(d.info.duration * fps + 1e-6));
        }
    }
    for (auto& h : head) d.put(d.indexOf(h.first), h.second);
    d.lastIdx = d.indexOf(head.back().first);
    d.lastImg = head.back().second;
    return true;
}

void ClipSource::setFps(double fps) {
    Impl& d = *d_;
    if (!(fps > 0) || !std::isfinite(fps) || std::abs(fps - d.info.fps) < 1e-9) return;
    d.info.fps = fps;
    d.info.count = std::max(1, (int)std::floor(d.info.duration * fps + 1e-6));
    if (d.exact) return;   // the table still decides which frame f is
    d.clearCache();
    d.lastIdx = -1;   // index mapping changed: force a seek on the next miss
    d.lastImg.reset();
}

ImagePtr ClipSource::frame(int f) {
    Impl& d = *d_;
    if (!d.rd) return nullptr;
    f = std::clamp(f, 0, std::max(0, d.info.count - 1));
    if (d.exact) f = std::min(f, (int)d.raw.size() - 1);
    if (ImagePtr hit = d.get(f)) return hit;
    ComScope com;
    // past the end already: nothing more to read
    if (d.eos && d.lastIdx >= 0 && f > d.lastIdx) return d.lastImg;
    bool overshot = false;
    if (d.lastIdx >= 0 && !d.eos && f > d.lastIdx && f <= d.lastIdx + 90) {
        return d.decodeTo(f, overshot);   // forward and close: keep reading
    }
    const LONGLONG pos = d.timeOfIndex(f);
    d.seek(pos);
    ImagePtr im = d.decodeTo(f, overshot);
    if ((overshot || !im) && f > 0) {
        // the seek landed past f (inexact seeking) or failed (e.g. past a wrong container
        // duration): back off and decode forward again, finally from the start
        d.seek(pos - 30000000);
        im = d.decodeTo(f, overshot);
        if (!im || overshot) {
            d.seek(0);
            im = d.decodeTo(f, overshot);
        }
    }
    return im;
}

// ================================================================ audio

namespace {

// float PCM at the track's native rate (or the requested one); calls sink(ts100ns, interleaved, frames, ch, rate)
bool readAudioFloat(const std::wstring& path, int wantRate,
                    const std::function<void(LONGLONG, const float*, size_t, UINT32, UINT32)>& sink, UINT32& rateOut,
                    std::string& err) {
    ComPtr<IMFSourceReader> rd;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &rd))) { err = "cannot open file"; return false; }
    rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(rd->SetStreamSelection(kAudio, TRUE))) { err = "no audio stream"; return false; }
    auto request = [&](int rate) {
        ComPtr<IMFMediaType> t;
        MFCreateMediaType(&t);
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (rate > 0) t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
        return SUCCEEDED(rd->SetCurrentMediaType(kAudio, nullptr, t.Get()));
    };
    if (!(wantRate > 0 && request(wantRate)) && !request(0)) { err = "cannot decode audio"; return false; }
    ComPtr<IMFMediaType> cur;
    if (FAILED(rd->GetCurrentMediaType(kAudio, &cur))) { err = "cannot decode audio"; return false; }
    UINT32 rate = MFGetAttributeUINT32(cur.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    UINT32 ch = std::max<UINT32>(1, MFGetAttributeUINT32(cur.Get(), MF_MT_AUDIO_NUM_CHANNELS, 1));
    if (!rate) { err = "unknown sample rate"; return false; }
    rateOut = rate;
    for (;;) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> s;
        if (FAILED(rd->ReadSample(kAudio, 0, nullptr, &flags, &ts, &s))) break;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            cur.Reset();
            if (SUCCEEDED(rd->GetCurrentMediaType(kAudio, &cur)))
                ch = std::max<UINT32>(1, MFGetAttributeUINT32(cur.Get(), MF_MT_AUDIO_NUM_CHANNELS, ch));
        }
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
        if (!s) continue;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(s->ConvertToContiguousBuffer(&buf))) continue;
        BYTE* p = nullptr;
        DWORD len = 0;
        if (FAILED(buf->Lock(&p, nullptr, &len))) continue;
        size_t frames = len / (sizeof(float) * ch);
        if (frames) sink(ts, (const float*)p, frames, ch, rate);
        buf->Unlock();
    }
    return true;
}

struct MfSession {
    ComScope com;
    bool mf;
    MfSession() { mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)); }
    ~MfSession() { if (mf) MFShutdown(); }
};

// MPEG-4 descriptor length (7 bits per byte)
void descLen(const Bytes& b, size_t o, uint32_t& n, size_t& skip) {
    n = 0;
    int i = 0;
    for (; i < 4; ++i) {
        uint8_t v = b.u8(o + i);
        n = (n << 7) | (v & 0x7f);
        if (!(v & 0x80)) break;
    }
    skip = (size_t)i + 1;
}

// esds: object type and whether an AudioSpecificConfig is present
void parseEsds(const Bytes& b, size_t start, size_t end, int& objectType, bool& asc) {
    objectType = 0;
    asc = false;
    size_t o = start + 4;
    for (int guard = 0; o < end && guard < 64; ++guard) {
        uint8_t tag = b.u8(o);
        uint32_t n = 0;
        size_t skip = 0;
        descLen(b, o + 1, n, skip);
        size_t body = o + 1 + skip;
        if (tag == 0x03) {
            uint8_t fl = b.u8(body + 2);
            o = body + 3 + (fl & 0x80 ? 2 : 0) + (fl & 0x40 ? 1 + b.u8(body + 3) : 0) + (fl & 0x20 ? 2 : 0);
            continue;
        }
        if (tag == 0x04) { objectType = b.u8(body); o = body + 13; continue; }
        if (tag == 0x05) { asc = n > 0; break; }
        o = body + n;
    }
}

struct AudioTrack {
    bool ok = false;      // a codec the web app decodes per sample (AAC / MP3 / Opus / PCM)
    bool aac = false;
    uint32_t sampleRate = 0, timescale = 0;
    int64_t shift = 0;
    std::vector<int64_t> cts;
};

AudioTrack parseAudioTrack(File& file, const Mp4Index& ix, const TrackDesc& t) {
    AudioTrack a;
    const Bytes& m = ix.moov;
    const Box& e = t.entry;
    uint16_t ver = m.u16(e.start + 8);
    uint32_t rate = m.u16(e.start + 24);
    a.sampleRate = rate ? rate : t.timescale;
    a.timescale = t.timescale;
    a.shift = t.shift;
    auto kids = boxes(m, e.start + 28 + (ver == 1 ? 16 : ver == 2 ? 36 : 0), e.end);
    auto find = [&](uint32_t k) {
        for (const Box& x : kids)
            if (x.type == k) return x;
        for (const Box& x : boxes(m, e.start + 28, e.end))
            if (x.type == k) return x;
        return Box{};
    };
    bool ok = false;
    if (e.type == FC("mp4a")) {
        Box esds = find(FC("esds"));
        if (!esds.ok) esds = child(m, find(FC("wave")), FC("esds"));
        if (!esds.ok) return a;
        int ot = 0;
        bool asc = false;
        parseEsds(m, esds.start, esds.end, ot, asc);
        if (ot == 0x6b || ot == 0x69) ok = true;
        else if (asc) ok = a.aac = true;
    } else if (e.type == FC("Opus")) {
        ok = find(FC("dOps")).ok;
    } else if (e.type == FC("sowt") || e.type == FC("twos")) {
        ok = true;
    }
    if (!ok) return a;
    if (!trackSamples(file, ix, t, a.cts)) return a;
    a.ok = true;
    return a;
}

} // namespace

bool decodeAudioMono(const std::wstring& path, std::vector<float>& out, int& sampleRate, std::string& err) {
    out.clear();
    MfSession mfs;
    const int want = sampleRate;
    UINT32 rate = 0;
    bool ok = readAudioFloat(
        path, want,
        [&](LONGLONG, const float* fs, size_t frames, UINT32 ch, UINT32) {
            size_t base = out.size();
            out.resize(base + frames);
            const float inv = 1.0f / ch;
            for (size_t i = 0; i < frames; ++i) {
                float acc = 0;
                for (UINT32 c = 0; c < ch; ++c) acc += fs[i * ch + c];
                out[base + i] = acc * inv;
            }
        },
        rate, err);
    if (!ok) return false;
    // the reader could not resample: do it here (linear)
    if (want > 0 && (int)rate != want && !out.empty()) {
        double ratio = (double)rate / want;
        size_t n = (size_t)std::floor((out.size() - 1) / ratio) + 1;
        std::vector<float> rs(n);
        for (size_t i = 0; i < n; ++i) {
            double x = i * ratio;
            size_t k = (size_t)x;
            double fr = x - k;
            float a = out[k], b = out[std::min(k + 1, out.size() - 1)];
            rs[i] = (float)(a + (b - a) * fr);
        }
        out.swap(rs);
        rate = (UINT32)want;
    }
    sampleRate = (int)rate;
    return true;
}

bool clipAudioLevels(const std::wstring& path, double fps, int count, std::vector<float>& db, std::string& err,
                     const std::function<void(double)>& progress) {
    db.clear();
    err.clear();
    if (count <= 0 || !(fps > 0)) { err = "no frames"; return false; }
    Mp4FrameTable vt;
    AudioTrack at;
    bool mp4 = false;
    {
        File file(path);
        if (file.f) {
            Mp4Index ix = scanTop(file);
            if (ix.ok) {
                mp4 = true;
                readFrameTable(file, ix, vt);
                TrackDesc t = findTrack(ix.moov, FC("soun"));
                if (!t.ok) { err = "no audio stream"; return false; }
                at = parseAudioTrack(file, ix, t);
                if (at.ok && at.cts.empty()) { err = "no audio stream"; return false; }
            }
        }
    }
    MfSession mfs;
    if (mp4 && at.ok && at.timescale) {
        // video clock: exact frame table, else f / fps
        auto frameAt = [&](double t) -> long long {
            if (vt.ok) return tableFrameAt(vt.times, t);
            return (long long)std::floor(t * fps);
        };
        auto timeOf = [&](long long f) -> double {
            if (vt.ok) return vt.times[(size_t)std::clamp<long long>(f, 0, (long long)vt.times.size() - 1)];
            return f / fps;
        };
        // audio time of presentation 0; without an edit list the AAC priming (1024 samples) is removed
        const double t0 = at.shift ? (double)at.shift / at.timescale : at.aac ? 1024.0 / at.sampleRate : 0;
        const double cts0 = (double)at.cts[0] / at.timescale, sh = (double)at.shift / at.timescale;
        std::vector<double> power((size_t)count, 0), hits((size_t)count, 0);
        std::vector<float> e;
        bool first = true, synced = false;
        double aOff = 0;   // reader time - composition time (s)
        double pos = 0;    // decoded sample frames so far
        double total = (double)at.cts.size() * 1024, done = 0;
        UINT32 rate = 0;
        bool ok = readAudioFloat(
            path, 0,
            [&](LONGLONG ts, const float* fs, size_t n, UINT32 ch, UINT32 r) {
                const double t = (double)ts / 1e7;
                first = false;
                // composition time of this buffer from the decoded sample count (the reader clamps
                // times before an edit-list start to 0); reader times resync it after gaps
                double raw = cts0 + pos / r;
                if (!synced && t > 0.001) {
                    synced = true;
                    aOff = t - raw;
                    if (std::abs(aOff) < 0.002) aOff = 0;
                    else if (std::abs(aOff + sh) < 0.002) aOff = -sh;
                } else if (synced && std::abs(t - aOff - raw) > 0.01) {
                    raw = t - aOff;
                    pos = (raw - cts0) * r;
                }
                pos += (double)n;
                e.resize(n);
                for (size_t k = 0; k < n; ++k) {
                    double s = 0;
                    for (UINT32 c = 0; c < ch; ++c) s += (double)fs[k * ch + c] * fs[k * ch + c];
                    e[k] = (float)(s / ch);
                }
                const double start = raw - t0;
                size_t i = 0;
                long long f = frameAt(start);
                while (i < n && f < count) {
                    double next = f + 1 < count ? timeOf(f + 1) : std::numeric_limits<double>::infinity();
                    double jj = std::ceil((next - start) * r);
                    size_t j = n;
                    if (jj < (double)n) j = (size_t)std::max<double>((double)(i + 1), jj);
                    double s = 0;
                    for (size_t k = i; k < j; ++k) s += e[k];
                    if (f >= 0) {
                        power[(size_t)f] += s;
                        hits[(size_t)f] += (double)(j - i);
                    }
                    i = j;
                    ++f;
                }
                done += (double)n;
                if (progress) progress(std::min(1.0, done / std::max(1.0, total)));
            },
            rate, err);
        if (ok && !first) {
            if (progress) progress(1);
            db.assign((size_t)count, std::numeric_limits<float>::quiet_NaN());
            for (int f = 0; f < count; ++f)
                if (hits[f] > 0) db[f] = (float)(20 * std::log10(std::max(std::sqrt(power[f] / hits[f]), 1e-6)));
            // frames without audio (ends, gaps) take the previous level so no fake jumps appear
            float last = -120;
            for (float v : db)
                if (!std::isnan(v)) { last = v; break; }
            for (float& v : db) {
                if (std::isnan(v)) v = last;
                else last = v;
            }
            return true;
        }
        err.clear();
        // could not decode per track: fall through to the whole-file mono path
    }
    std::vector<float> mono;
    int rate = 8000;
    if (!decodeAudioMono(path, mono, rate, err)) return false;
    if (mono.empty()) { err = "no audio"; return false; }
    db = envelopeDb(mono, rate, fps, count);
    if (progress) progress(1);
    return true;
}

} // namespace bg
