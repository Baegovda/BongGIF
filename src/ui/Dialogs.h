#pragma once
#include "core/Image.h"
#include "platform/Media.h"
#include "platform/Settings.h"

#include <string>
#include <windows.h>

namespace bg {

bool timeDialog(HWND owner, const std::wstring& title, int64_t currentUs, int64_t& outUs);
bool trimDialog(HWND owner, const Frames& frames, int& start, int& end);

struct ReduceParams { int mode = 0; int nth = 3; bool keep = true; int diff = 20; };   // mode 0 even,1 odd,2 nth,3 dup
bool reduceDialog(HWND owner, Settings& st, ReduceParams& out);

struct SummaryParams { int percent = 50; bool scale = true; int adjust = 20; bool limit = true; int64_t limitUs = 300000; };
bool summaryDialog(HWND owner, Settings& st, SummaryParams& out);

struct VideoImportParams { double start = 0, end = 10; int maxFps = 30, maxSide = 960; };
bool videoImportDialog(HWND owner, const std::wstring& path, const VideoInfo& vi, Settings& st, VideoImportParams& out);

bool saveDialog(HWND owner, const Frames& frames, Settings& st, std::wstring& savedPath, size_t& bytes);

} // namespace bg
