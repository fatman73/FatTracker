# macOS build (included from CMakeLists.txt before the Windows part, which it skips).
# Milestone 1 of the macOS port: the portable core (ftcore) and the test runner (fttest).
# Keep the source lists in step with the Windows part of CMakeLists.txt.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -ffp-contract=off: no fused multiply-add, so float results match MSVC (/fp:precise does not fuse)
add_compile_options(-fno-exceptions -fno-rtti -ffp-contract=off -ffunction-sections -fdata-sections)
add_compile_options("$<$<CONFIG:Release>:-O2>")
add_link_options(-Wl,-dead_strip)
set(FT_WARN -Wall -Wextra)
set(FT_VENDOR_WARN "-w")  # third-party code: no warnings (the Windows build uses /W1)

file(GLOB FT_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/formats/*.cpp)
file(GLOB FT_TFMX_SOURCES CONFIGURE_DEPENDS src/core/tfmx/*.cpp)
file(GLOB FT_HIPPEL_SOURCES CONFIGURE_DEPENDS src/core/hippel/*.cpp)
set(FT_CRSID_SOURCES src/core/crsid/libcRSID.c)
set(FT_AYUMI_SOURCES src/core/ayumi/ayumi.c)
file(GLOB FT_GME_SOURCES CONFIGURE_DEPENDS src/core/gme/*.cpp)
list(APPEND FT_GME_SOURCES src/core/gme/ext/emu2413.c)
set(FT_M68K_SOURCES src/core/m68k/m68kcpu.c src/core/m68k/m68kops.c)
set(FT_HVL_SOURCES src/core/hvl/hvl_replay.c src/core/hvl/hvl_tables.c)
set(FT_VENDOR_SOURCES ${FT_TFMX_SOURCES} ${FT_HIPPEL_SOURCES} ${FT_CRSID_SOURCES} ${FT_AYUMI_SOURCES}
    ${FT_GME_SOURCES} ${FT_M68K_SOURCES} ${FT_HVL_SOURCES})
set_source_files_properties(${FT_VENDOR_SOURCES} PROPERTIES COMPILE_OPTIONS "${FT_VENDOR_WARN}")
# cRSID: C with lambdas (nested functions became lambdas), compiled as C++ like /TP on Windows
set_source_files_properties(${FT_CRSID_SOURCES} PROPERTIES LANGUAGE CXX)
# its host/*.c include "libcRSID.h" from the parent folder (MSVC searches the includer's folder, clang does not)
set_source_files_properties(${FT_CRSID_SOURCES} PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/src/core/crsid)

add_library(ftcore STATIC ${FT_CORE_SOURCES} ${FT_VENDOR_SOURCES})
target_include_directories(ftcore PRIVATE src/core/gme)
target_compile_definitions(ftcore PRIVATE BLARGG_LITTLE_ENDIAN=1 VGM_YM2612_NUKED)  # arm64 and x86_64 are little-endian
target_include_directories(ftcore PUBLIC src)
set_source_files_properties(${FT_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "${FT_WARN}")

# UI sources without platform graphics (testable)
set(FT_UI_PURE_SOURCES src/ui/viewport.cpp src/ui/pattern_layout.cpp src/ui/cursor_art.cpp src/ui/pixel_canvas.cpp
    src/ui/pure_layout.cpp src/ui/demo_fx.cpp src/ui/credits.cpp)

# fttest: WASAPI tests (audio device, capture) are Windows-only; Engine runs without a device (null AudioOut)
file(GLOB FT_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
list(REMOVE_ITEM FT_TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/test_audio_device.cpp
     ${CMAKE_SOURCE_DIR}/tests/test_capture.cpp)
add_executable(fttest ${FT_TEST_SOURCES} src/audio/engine.cpp src/mac/audio_out_null.cpp ${FT_UI_PURE_SOURCES})
target_link_libraries(fttest PRIVATE ftcore)
target_include_directories(fttest PRIVATE src tests)
target_compile_definitions(fttest PRIVATE FT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
