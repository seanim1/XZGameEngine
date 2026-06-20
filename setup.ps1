$ErrorActionPreference = "Stop"

if (-Not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "Error: CMake not found"
    exit 1
}

# Install Vulkan SDK (includes glslangValidator)
winget install KhronosGroup.VulkanSDK --accept-source-agreements --accept-package-agreements

# Add Vulkan SDK bin to PATH for this session and permanently (User scope, no admin needed)
$vulkanSdkBase = "C:\VulkanSDK"
$vulkanBin = (Get-ChildItem $vulkanSdkBase | Sort-Object Name -Descending | Select-Object -First 1).FullName + "\Bin"
$env:PATH += ";$vulkanBin"
[System.Environment]::SetEnvironmentVariable("PATH", [System.Environment]::GetEnvironmentVariable("PATH", "User") + ";$vulkanBin", "User")
Write-Host "Vulkan SDK bin added to PATH: $vulkanBin"

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
    cmake .. -G "Visual Studio 18 2026" -A x64
    cmake --build . --config Debug
    Pop-Location
    Pop-Location
}

$toolchain = "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
cmake --build build --config Debug