#!/usr/bin/env bash

set -e

echo "[1/4] Cleaning..."
rm -rf build dist

echo "[2/4] Configuring..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja

echo "[3/4] Building..."
ninja -C build

echo "[4/4] Staging..."
mkdir -p dist
cp build/quark dist/

echo "Done."