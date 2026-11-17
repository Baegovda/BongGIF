# Third-party libraries (all static, MSVC, pure C/C++ — no assembler needed).
#   libwebp 1.6.0 (BSD)      WebP              libvpx 1.15.2 (BSD)  WebM VP8/VP9
#   libwebm 1.0.0.31 (BSD)   WebM muxer        libaom 3.12.1 (BSD)  AV1 encode/decode
#   libavif 1.3.0 (BSD)      AVIF container    libimagequant 2.18 (GPLv3) GIF LIQ quantiser
#   lunasvg 3.2.1 + plutovg (MIT)  SVG import
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---- libwebp
foreach(opt WEBP_BUILD_ANIM_UTILS WEBP_BUILD_CWEBP WEBP_BUILD_DWEBP WEBP_BUILD_GIF2WEBP WEBP_BUILD_IMG2WEBP
            WEBP_BUILD_VWEBP WEBP_BUILD_WEBPINFO WEBP_BUILD_WEBPMUX WEBP_BUILD_EXTRAS)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
add_subdirectory(libwebp EXCLUDE_FROM_ALL)

# ---- libvpx (sources and generic-C dispatch headers produced by vpx_gen.py from libvpx's own
#      configure output in vpxcfg/; regenerate with `python third_party/vpx_gen.py`)
set(VPX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/libvpx)
include(${CMAKE_CURRENT_SOURCE_DIR}/vpxcfg/sources.cmake)
add_library(vpx STATIC ${VPX_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/vpxcfg/vpx_config.c ${CMAKE_CURRENT_SOURCE_DIR}/vpxcfg/vpx_shim.c)
target_include_directories(vpx PUBLIC ${VPX_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/vpxcfg)
target_compile_definitions(vpx PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(vpx PRIVATE /W0 /wd4996)
endif()

# ---- libwebm (muxer only)
add_library(webmmux STATIC
  libwebm/mkvmuxer/mkvmuxer.cc libwebm/mkvmuxer/mkvmuxerutil.cc libwebm/mkvmuxer/mkvwriter.cc)
target_include_directories(webmmux PUBLIC libwebm)
if(MSVC)
  target_compile_options(webmmux PRIVATE /W0)
endif()

# ---- libaom (generic C target; perl from Git for Windows generates the rtcd headers)
if(NOT PERL_EXECUTABLE AND EXISTS "C:/Program Files/Git/usr/bin/perl.exe")
  set(PERL_EXECUTABLE "C:/Program Files/Git/usr/bin/perl.exe" CACHE FILEPATH "" FORCE)
endif()
set(AOM_TARGET_CPU generic CACHE STRING "" FORCE)
foreach(opt ENABLE_EXAMPLES ENABLE_TESTS ENABLE_TOOLS ENABLE_DOCS ENABLE_TESTDATA CONFIG_AV1_HIGHBITDEPTH)
  set(${opt} 0 CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(libaom EXCLUDE_FROM_ALL)
if(MSVC)
  foreach(t aom aom_av1_common aom_av1_encoder aom_av1_decoder aom_dsp aom_dsp_common aom_dsp_encoder aom_dsp_decoder aom_mem aom_ports aom_scale aom_util aom_rtcd aom_version)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE /W0)
    endif()
  endforeach()
endif()
target_include_directories(aom INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libaom ${CMAKE_CURRENT_BINARY_DIR}/libaom)

# ---- libavif (uses the aom target above)
set(AVIF_CODEC_AOM SYSTEM CACHE STRING "" FORCE)
foreach(opt AVIF_LIBYUV AVIF_LIBSHARPYUV AVIF_LIBXML2 AVIF_JPEG AVIF_ZLIBPNG AVIF_CODEC_DAV1D AVIF_CODEC_LIBGAV1 AVIF_CODEC_RAV1E AVIF_CODEC_SVT AVIF_CODEC_AVM)
  set(${opt} OFF CACHE STRING "" FORCE)
endforeach()
set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(libavif EXCLUDE_FROM_ALL)

# ---- libimagequant 2.18 (C)
add_library(imagequant STATIC
  libimagequant/libimagequant.c libimagequant/blur.c libimagequant/mediancut.c libimagequant/mempool.c
  libimagequant/nearest.c libimagequant/pam.c libimagequant/kmeans.c libimagequant/remap.c)
target_include_directories(imagequant PUBLIC libimagequant)
if(MSVC)
  target_compile_options(imagequant PRIVATE /W0 /fp:fast)
endif()

# ---- lunasvg (+ plutovg)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lunasvg EXCLUDE_FROM_ALL)
