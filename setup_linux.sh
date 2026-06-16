#!/usr/bin/env bash
set -e

sudo apt-get update
sudo apt-get install -y \
    cmake pkg-config \
    libx11-dev libxext-dev libxcb-xtest0-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev \
    libxi-dev libxss-dev libxkbcommon-dev \
    libwayland-dev wayland-protocols \
    vulkan-tools libvulkan-dev libvulkan1 \
    glslang-tools \
    libglm-dev \
    libassimp-dev

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
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DSDL_X11_XTEST=OFF
    cmake --build . -j
)

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
