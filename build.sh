#!/bin/bash
set -e

echo "Assembling Quark Runtime (Linux)..."

fasm qkrt/common/string.asm   qkrt/common/string.o
fasm qkrt/linux/file.asm      qkrt/linux/file.o
fasm qkrt/linux/io.asm        qkrt/linux/io.o

echo "Assembling program..."
fasm out.asm        out.o

echo "Linking..."
ld -o out \
    out.o \
    qkrt/common/string.o \
    qkrt/linux/file.o \
    qkrt/linux/io.o \
    --no-dynamic-linker

echo "Build successful!"