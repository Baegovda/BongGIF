#include "platform/Export.h"

#include "core/Apng.h"
#include "core/Edit.h"
#include "core/Effects.h"
#include "platform/Codecs.h"
#include "platform/Media.h"
#include "platform/Webp.h"

#include <algorithm>
#include <cstdio>
#include <windows.h>

namespace bg {

const wchar_t* kFormatNames[FMT_COUNT] = {L"GIF", L"APNG", L"MP4", L"WebP", L"WebM", L"AVIF", L"PNG 시퀀스", L"JPG 시퀀스"};
const wchar_t* kFormatExt[FMT_COUNT] = {L".gif", L".png", L".mp4", L".webp", L".webm", L".avif", L".png", L".jpg"};

// quality > 80 -> off, > 69 -> 3, > 59 -> 4, > 49 -> 5, < 40 -> 7, else 6
int colorCompressLevel(const ExportOptions& o) {
    if (!o.colorCompress) return 0;
    bool applies = o.format == FMT_MP4 || o.format == FMT_WEBM || o.format == FMT_AVIF || (o.format == FMT_WEBP && !o.webp.lossless);
    if (!applies) return 0;
    int q = o.quality;
    if (q > 80) return 0;
    if (q > 69) return 3;
    if (q > 59) return 4;
    if (q > 49) return 5;
    if (q < 40) return 7;
    return 6;
}

Frames prepareFrames(const Frames& in, const ExportOptions& o, const Progress& prog) {
    Frames f = in;
    if (o.skipSimilar) f = removeDuplicates(f, 1, 8, true, 0, {});
    if (o.maxFps > 0) {
        int64_t minUs = 1000000 / o.maxFps;
        Frames m;
        for (auto& x : f) {
            if (!m.empty() && m.back().dur < minUs) m.back().dur += x.dur;
            else m.push_back(x);
        }
        f.swap(m);
    }
    if (o.maxFrameMs > 0)
        for (auto& x : f) x.dur = std::min<int64_t>(x.dur, o.maxFrameMs * 1000LL);
    if (!f.empty()) {
        int w = f[0].img->w, h = f[0].img->h;
        if (o.scalePct > 0 && o.scalePct != 100) { w = std::max(8, w * o.scalePct / 100); h = std::max(8, h * o.scalePct / 100); }
        if (o.maxSide > 0 && std::max(w, h) > o.maxSide) {
            double s = (double)o.maxSide / std::max(w, h);
            w = std::max(8, (int)(w * s));
            h = std::max(8, (int)(h * s));
        }
        if (w != f[0].img->w || h != f[0].img->h) f = resize(f, w, h);
    }
    if (o.progressBar && !f.empty()) {
        int64_t total = 0, t = 0;
        for (auto& x : f) total += x.dur;
        for (auto& x : f) {
            t += x.dur;
            auto im = std::make_shared<Image>(*x.img);
            int bh = std::max(2, im->h / 80), bw = (int)(im->w * (double)t / std::max<int64_t>(total, 1));
            for (int y = im->h - bh; y < im->h; ++y) std::fill(im->row(y), im->row(y) + bw, o.barColor);
            x.img = im;
        }
    }
    if (int lvl = colorCompressLevel(o)) {   // mean-shift pre-filter, sp = 15*level, sr = 10*level
        f = mapImages(f, [lvl](const Image& s) {
            auto o2 = std::make_shared<Image>(s);
            meanShiftFilter(*o2, 15 * lvl, 10 * lvl, 4);
            return ImagePtr(o2);
        }, {}, prog ? [&](double v) { prog(0.3 * v); } : Progress{});
    }
    return f;
}

static std::wstring tempPath(const wchar_t* ext) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"bonggif_estimate_" + std::to_wstring(GetCurrentThreadId()) + ext;
}

static size_t fileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return 0;
    return ((size_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}

bool exportFrames(const Frames& in, const ExportOptions& o, const std::wstring& path, size_t& bytes, const Progress& prog,
                  const Cancel& cancel, std::string& err) {
    bytes = 0;
    if (in.empty()) { err = "프레임이 없어요"; return false; }
    bool cc = colorCompressLevel(o) > 0;
    Frames f = prepareFrames(in, o, prog);
    Progress p2 = prog;
    if (cc && prog) p2 = [&](double v) { prog(0.3 + 0.7 * v); };
    // formats written straight to a file
    if (o.format == FMT_MP4 || o.format == FMT_WEBM) {
        std::wstring target = path.empty() ? tempPath(kFormatExt[o.format]) : path;
        bool ok;
        if (o.format == FMT_MP4) ok = encodeMp4(f, target, o.quality, p2, cancel, err);
        else {
            WebmOptions w;
            w.quality = o.quality;
            w.vp9 = o.webmVp9;
            ok = encodeWebm(f, w, target, p2, cancel, err);
        }
        if (ok) bytes = fileSize(target);
        if (path.empty()) DeleteFileW(target.c_str());
        return ok;
    }
    if (o.format == FMT_PNGSEQ || o.format == FMT_JPGSEQ) {
        // path "dir\name.png" -> dir\name_0001.png ... ; estimate = total of all files
        std::wstring base = path, ext = kFormatExt[o.format];
        if (!base.empty() && base.size() > ext.size() && base.substr(base.size() - ext.size()) == ext) base.resize(base.size() - ext.size());
        for (size_t i = 0; i < f.size(); ++i) {
            if (cancel && cancel()) return false;
            wchar_t num[16];
            swprintf_s(num, L"_%04zu", i + 1);
            std::wstring target = path.empty() ? tempPath(ext.c_str()) : base + num + ext;
            if (!saveImageWic(*f[i].img, target, o.format == FMT_JPGSEQ, o.jpgQuality, err)) return false;
            bytes += fileSize(target);
            if (path.empty()) DeleteFileW(target.c_str());
            if (p2) p2((double)(i + 1) / f.size());
            if (path.empty() && i >= 9) {   // estimate from the first 10 frames
                bytes = bytes * f.size() / (i + 1);
                break;
            }
        }
        return true;
    }
    FILE* fp = nullptr;
    std::wstring tmpPath;
    if (!path.empty()) {
        tmpPath = path + L".part";
        if (_wfopen_s(&fp, tmpPath.c_str(), L"wb") || !fp) { err = "파일을 쓸 수 없어요"; return false; }
    }
    ByteSink sink = [&](const uint8_t* p, size_t n) {
        bytes += n;
        if (fp) fwrite(p, 1, n, fp);
    };
    bool ok = false;
    try {
        if (o.format == FMT_GIF) {
            GifOptions g = o.gif;
            g.quality = o.quality;
            g.loop = o.loop;
            ok = encodeGif(f, g, sink, p2, cancel);
        } else if (o.format == FMT_WEBP) {
            WebpOptions w = o.webp;
            w.quality = o.quality;
            w.loop = o.loop;
            w.comment = o.gif.comment;
            ok = encodeWebp(f, w, sink, p2, cancel, err);
        } else if (o.format == FMT_AVIF) {
            ok = encodeAvif(f, o.quality, o.loop, sink, p2, cancel, err);
        } else {
            ok = encodeApng(f, o.loop, sink, p2, cancel);
        }
    } catch (const std::exception& e) {
        err = e.what();
        ok = false;
    }
    if (fp) {
        bool wok = !ferror(fp);
        fclose(fp);
        if (ok && wok) {
            if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { err = "파일 이름을 바꾸지 못했어요"; ok = false; }
        } else {
            if (ok) err = "파일을 쓰다가 문제가 생겼어요";
            ok = false;
        }
        if (!ok) DeleteFileW(tmpPath.c_str());
    }
    return ok;
}

} // namespace bg
