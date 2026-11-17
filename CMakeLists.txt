cmake_minimum_required(VERSION 3.20)
project(BongGIF VERSION 2.0.1 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: single exe

# third-party libraries are added before BongGIF's own compile flags
add_subdirectory(third_party EXCLUDE_FROM_ALL)
# libaom appends its own flags (_WIN32_WINNT=0x0601, /std:c++14) to the cached release flags: reset them here
set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive- /EHsc /O2 /GL)
  add_link_options(/LTCG /OPT:REF /OPT:ICF)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

add_library(bgcore STATIC
  src/core/Edit.cpp
  src/core/Effects.cpp
  src/core/Quantize.cpp
  src/core/Quantize2.cpp
  src/core/Psd.cpp
  src/core/SuperRes.cpp
  src/core/Compose.cpp
  src/core/Gif.cpp
  src/core/Deflate.cpp
  src/core/Apng.cpp
)
target_include_directories(bgcore PUBLIC src)
target_link_libraries(bgcore PUBLIC imagequant)

add_library(bgplatform STATIC
  src/platform/Capture.cpp
  src/platform/Media.cpp
  src/platform/Settings.cpp
  src/platform/Export.cpp
  src/platform/Webp.cpp
  src/platform/Codecs.cpp
  src/platform/Dxgi.cpp
  src/platform/Ocr.cpp
)
# C++/WinRT (Windows.Media.Ocr) needs C++20 coroutines
set_source_files_properties(src/platform/Ocr.cpp PROPERTIES COMPILE_OPTIONS "/std:c++20")
target_link_libraries(bgplatform PUBLIC bgcore webp webpdemux libwebpmux vpx webmmux avif aom lunasvg d3d11 dxgi windowsapp d2d1 dwrite mfplat mfreadwrite mfuuid mf ole32 windowscodecs gdiplus dwmapi imm32 shell32 user32 gdi32)

add_executable(bonggif-tests tests/tests.cpp tests/tests_platform.cpp)
target_link_libraries(bonggif-tests PRIVATE bgplatform)
target_compile_definitions(bonggif-tests PRIVATE "BG_SOURCE_DIR=L\"${CMAKE_SOURCE_DIR}\"")

add_executable(BongGIF WIN32
  src/main.cpp
  src/ui/App.cpp
  src/ui/App2.cpp
  src/ui/App3.cpp
  src/ui/Lang.cpp
  src/ui/Gfx.cpp
  src/ui/Gui.cpp
  src/ui/Dialogs.cpp
  src/ui/SaveDialog.cpp
  src/ui/Recorder.cpp
  res/app.rc
)
target_link_libraries(BongGIF PRIVATE bgplatform d2d1 dwrite dwmapi comdlg32 shell32 ole32)
if(MSVC)
  target_link_options(BongGIF PRIVATE /MANIFEST:NO)
endif()
