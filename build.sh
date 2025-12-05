#!/usr/bin/env bash
set -e -o pipefail

# Change to the directory where this script is located
cd "$(dirname "$0")"

mkdir -p build/cmake

cd build/cmake
cmake ../..

cmake --build . --parallel --config Release
cmake --build . --parallel --config Debug
