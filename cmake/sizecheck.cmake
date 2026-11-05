# Usage: cmake -DFILE=<path> -P sizecheck.cmake
file(SIZE "${FILE}" _size)
math(EXPR _kb "${_size} / 1024")
message(STATUS "FatTracker.exe size: ${_size} bytes (${_kb} KB)")
if(_size GREATER 1048576)
  message(WARNING "FatTracker.exe exceeds the 1 MB budget (${_size} bytes)")
endif()
