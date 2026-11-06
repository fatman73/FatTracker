cmake_minimum_required(VERSION 3.28)
if(APPLE)
  # macOS port: Universal Binary (arm64 + x86_64), macOS 13+; see cmake/apple.cmake
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
  project(FatTracker LANGUAGES C CXX OBJCXX)
  include(cmake/apple.cmake)
  return()
endif()
project(FatTracker LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /utf-8 /permissive- /GR- /EHs-c- /D_HAS_EXCEPTIONS=0
                    /DNOMINMAX /DUNICODE /D_UNICODE)
add_compile_options("$<$<CONFIG:Release>:/O1;/GL;/Gy>")
add_link_options("$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF>")

file(GLOB FT_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/formats/*.cpp)
# TFMX decoder adapted from libtfmxaudiodecoder (GPL v2+), see src/core/tfmx/README.txt
file(GLOB FT_TFMX_SOURCES CONFIGURE_DEPENDS src/core/tfmx/*.cpp)
set_source_files_properties(${FT_TFMX_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
# Jochen Hippel / Future Composer decoder from libtfmxaudiodecoder (GPL v2+), see src/core/hippel/README.txt
file(GLOB FT_HIPPEL_SOURCES CONFIGURE_DEPENDS src/core/hippel/*.cpp)
set_source_files_properties(${FT_HIPPEL_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
# C64 SID emulation: cRSID by Hermit (Mihaly Horvath), see src/core/crsid/README.txt; one translation unit
set(FT_CRSID_SOURCES src/core/crsid/libcRSID.c)
set_source_files_properties(${FT_CRSID_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1;/TP")  # C++: nested functions became lambdas
# AY-3-8910 / YM2149 emulation: ayumi by Peter Sovietov (MIT), plain C
set(FT_AYUMI_SOURCES src/core/ayumi/ayumi.c)
set_source_files_properties(${FT_AYUMI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
# Game_Music_Emu by Shay Green and the libgme authors (LGPL 2.1, see src/core/gme/README.txt): SPC, VGM, SAP, AY, NSF, GBS ...
file(GLOB FT_GME_SOURCES CONFIGURE_DEPENDS src/core/gme/*.cpp)
list(APPEND FT_GME_SOURCES src/core/gme/ext/emu2413.c)
set_source_files_properties(${FT_GME_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
# Motorola 68000 for Atari ST SNDH tunes: Musashi by Karl Stenerud (MIT), plain C, opcode tables generated
# once by m68kmake (see src/core/m68k/README.txt)
set(FT_M68K_SOURCES src/core/m68k/m68kcpu.c src/core/m68k/m68kops.c)
set_source_files_properties(${FT_M68K_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
# Amiga AHX / HivelyTracker modules: the HivelyTracker replayer by Pete Gordon (BSD 3-clause), plain C,
# bounds checks added to its loader (see src/core/hvl/README.txt)
set(FT_HVL_SOURCES src/core/hvl/hvl_replay.c src/core/hvl/hvl_tables.c)
set_source_files_properties(${FT_HVL_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
add_library(ftcore STATIC ${FT_CORE_SOURCES} ${FT_TFMX_SOURCES} ${FT_HIPPEL_SOURCES} ${FT_CRSID_SOURCES} ${FT_AYUMI_SOURCES}
            ${FT_GME_SOURCES} ${FT_M68K_SOURCES} ${FT_HVL_SOURCES})
target_include_directories(ftcore PRIVATE src/core/gme)
target_compile_definitions(ftcore PRIVATE BLARGG_LITTLE_ENDIAN=1 VGM_YM2612_NUKED)  # Nuked: the most exact YM2612
target_include_directories(ftcore PUBLIC src)

# Windows-specific modules shared by the app and the test runner
file(GLOB FT_AUDIO_SOURCES CONFIGURE_DEPENDS src/audio/*.cpp)
# UI sources without Direct2D dependencies (testable)
set(FT_UI_PURE_SOURCES)
foreach(f src/ui/viewport.cpp src/ui/pattern_layout.cpp src/ui/cursor_art.cpp src/ui/pixel_canvas.cpp src/ui/pure_layout.cpp src/ui/demo_fx.cpp src/ui/credits.cpp)
  if(EXISTS ${CMAKE_SOURCE_DIR}/${f})
    list(APPEND FT_UI_PURE_SOURCES ${f})
  endif()
endforeach()

file(GLOB FT_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(fttest ${FT_TEST_SOURCES} ${FT_AUDIO_SOURCES} ${FT_UI_PURE_SOURCES})
target_link_libraries(fttest PRIVATE ftcore ole32 avrt uuid)
target_include_directories(fttest PRIVATE src tests)
target_compile_definitions(fttest PRIVATE FT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ftrender: offline render of any song (modules and chip formats) through the Engine, no audio device opened
add_executable(ftrender tools/ftrender.cpp src/app/chip_route.cpp ${FT_AUDIO_SOURCES})
target_link_libraries(ftrender PRIVATE ftcore ole32 avrt uuid)
target_include_directories(ftrender PRIVATE src)

file(GLOB FT_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
file(GLOB FT_APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
set(FT_RES_SOURCES)
if(EXISTS ${CMAKE_SOURCE_DIR}/res/FatTracker.rc)
  list(APPEND FT_RES_SOURCES res/FatTracker.rc)
endif()
add_executable(FatTracker WIN32 ${FT_APP_SOURCES} ${FT_UI_SOURCES} ${FT_AUDIO_SOURCES} ${FT_RES_SOURCES})
target_link_libraries(FatTracker PRIVATE ftcore d2d1 dwrite d3d11 dxgi dxguid ole32 avrt uuid shell32 comdlg32 windowscodecs)
target_include_directories(FatTracker PRIVATE src)
if(EXISTS ${CMAKE_SOURCE_DIR}/res/app.manifest)
  target_link_options(FatTracker PRIVATE /MANIFEST:NO)
endif()
add_custom_command(TARGET FatTracker POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DFILE=$<TARGET_FILE:FatTracker> -P ${CMAKE_SOURCE_DIR}/cmake/sizecheck.cmake)
