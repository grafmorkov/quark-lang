# Quant compiler toolchain: hosted build for Linux x86-64.
#
# The produced `qu` runs on Linux x86-64 hosts and targets the backends
# selected via QUANT_BACKENDS.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_CXX_COMPILER g++)
