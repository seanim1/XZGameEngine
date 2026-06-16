$ErrorActionPreference = "Stop"

if (-Not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "Error: CMake not found"
    exit 1
}

if (-Not (Test-Path "C:\vcpkg")) {
    git clone https://github.com/microsoft/vcpkg C:\vcpkg
    & C:\vcpkg\bootstrap-vcpkg.bat
}

$packages = @("sdl3:x64-windows", "vulkan:x64-windows", "glm:x64-windows", "assimp:x64-windows")
foreach ($pkg in $packages) {
    & C:\vcpkg\vcpkg.exe install $pkg
}

if (-Not (Test-Path "third_party\SDL3")) {
    New-Item -ItemType Directory -Force -Path third_party | Out-Null
    git clone --depth 1 https://github.com/libsdl-org/SDL.git third_party\SDL3
}

if (-Not (Test-Path "third_party\imgui")) {
    New-Item -ItemType Directory -Force -Path third_party | Out-Null
    git clone --depth 1 https://github.com/ocornut/imgui.git third_party\imgui
}

if (-Not (Test-Path "third_party\SDL3\build")) {
    Push-Location third_party\SDL3
    New-Item -ItemType Directory -Force -Path build | Out-Null
    Push-Location build
    cmake .. -G "Visual Studio 17 2022" -A x64
    cmake --build . --config Debug
    Pop-Location
    Pop-Location
}

$toolchain = "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
cmake --build build --config Debug