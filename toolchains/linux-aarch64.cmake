# Quant compiler toolchain: cross-build that RUNS on AArch64 Linux.
#
# Uses the aarch64-unknown-linux-gnu cross toolchain to produce an aarch64
# `qu` binary (e.g. for ARM64 servers/dev boards). libstdc++/libgcc are
# linked statically so the binary does not depend on matching runtime
# libraries on the target machine.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_CXX_COMPILER aarch64-unknown-linux-gnu-g++)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
