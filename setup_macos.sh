#!/usr/bin/env bash
set -e
if ! command -v brew &> /dev/null; then
    echo "Error: Homebrew not found"
    exit 1
fi
brew install cmake vulkan-headers vulkan-loader glslang glm assimp
if [ ! -d third_party/SDL3 ]; then
    mkdir -p third_party
    git clone --depth 1 https://github.com/libsdl-org/SDL.git third_party/SDL3
fi
if [ ! -d third_party/imgui ]; then
    mkdir -p third_party
    git clone --depth 1 https://github.com/ocornut/imgui.git third_party/imgui
fi
(
    cd third_party/SDL3
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64
    cmake --build . -j
)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug