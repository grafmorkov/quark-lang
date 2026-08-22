# Quant compiler toolchain: cross-build via MinGW-w64.
#
# Produces a native Windows qu.exe even when building on Linux/macOS.
# Fully static linking, so the exe needs no MinGW runtime DLLs.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
if(EXISTS /usr/bin/x86_64-w64-mingw32-windres)
  set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
endif()

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
