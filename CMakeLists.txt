cmake_minimum_required(VERSION 3.28)
project(FatTracker LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /utf-8 /permissive- /GR- /EHs-c- /D_HAS_EXCEPTIONS=0
                    /DNOMINMAX /DUNICODE /D_UNICODE)
add_compile_options("$<$<CONFIG:Release>:/O1;/GL;/Gy>")
add_link_options("$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF>")

file(GLOB FT_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/formats/*.cpp)
add_library(ftcore STATIC ${FT_CORE_SOURCES})
target_include_directories(ftcore PUBLIC src)

# Windows-specific modules shared by the app and the test runner
file(GLOB FT_AUDIO_SOURCES CONFIGURE_DEPENDS src/audio/*.cpp)
# UI sources without Direct2D dependencies (testable)
set(FT_UI_PURE_SOURCES)
foreach(f src/ui/viewport.cpp src/ui/font_data.cpp src/ui/pattern_layout.cpp)
  if(EXISTS ${CMAKE_SOURCE_DIR}/${f})
    list(APPEND FT_UI_PURE_SOURCES ${f})
  endif()
endforeach()

file(GLOB FT_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(fttest ${FT_TEST_SOURCES} ${FT_AUDIO_SOURCES} ${FT_UI_PURE_SOURCES})
target_link_libraries(fttest PRIVATE ftcore ole32 avrt uuid)
target_include_directories(fttest PRIVATE src tests)
target_compile_definitions(fttest PRIVATE FT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

file(GLOB FT_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
file(GLOB FT_APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
set(FT_RES_SOURCES)
if(EXISTS ${CMAKE_SOURCE_DIR}/res/FatTracker.rc)
  list(APPEND FT_RES_SOURCES res/FatTracker.rc)
endif()
add_executable(FatTracker WIN32 ${FT_APP_SOURCES} ${FT_UI_SOURCES} ${FT_AUDIO_SOURCES} ${FT_RES_SOURCES})
target_link_libraries(FatTracker PRIVATE ftcore d2d1 dwrite d3d11 dxgi ole32 avrt uuid shell32 comdlg32)
target_include_directories(FatTracker PRIVATE src)
if(EXISTS ${CMAKE_SOURCE_DIR}/res/app.manifest)
  target_link_options(FatTracker PRIVATE /MANIFEST:NO)
endif()
add_custom_command(TARGET FatTracker POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DFILE=$<TARGET_FILE:FatTracker> -P ${CMAKE_SOURCE_DIR}/cmake/sizecheck.cmake)
