# XZ Game Engine - Multi-Platform

Cross-platform game engine using Vulkan API, SDL3, ImGUI

**Tested:** Linux✅, Windows, macOS(M1+)✅, iOS
<div class="row">
    <img src="README_images/README.png?raw=true" width="400">
    <img src="README_images/README1.png?raw=true" width="400">
    <img src="README_images/README_arch.png?raw=true" width="800">
</div>

## Linux (Debian) & macOS(M1+)
### Linux (Debian) Setup (run once)
```bash
chmod +x setup_linux.sh
./setup_linux.sh
```
### macOS(M1+) Setup (run once)
```bash
chmod +x setup_macos.sh
./setup_macos.sh
```
### Build & Run (every time you modify code)
```bash
cmake --build build --target XZGameEngine
./build/src_XZEngine/XZGameEngine

```

```
### Clean
```bash
rm -rf build
rm -rf third_party
```
---
## Windows

### Setup (run once)
---
Download MSVC cpp build tools if you haven't [MSVC Compiler](https://visualstudio.microsoft.com/visual-cpp-build-tools/) Open up the Visual Studio Installer then install C++ build tools

In the Windows search bar, look for "x64 Native Tools Command Prompt for VS 2022"
Verify compiler exists:
```bash
cl
```
Run once for initialization
```bash
powershell -ExecutionPolicy Bypass -File setup.ps1
```
### Build (every time you modify code)
```bash
cmake --build build --config Debug
cmake --build build --target XZGameEngine
```
### Copy .dll to .exe folder
```bash
powershell -ExecutionPolicy Bypass -File copy-dlls.ps1
```
### Run
```bash
build\src_XZEngine\Debug\XZGameEngine.exe
```
### Clean
```bash
rmdir /s /q build
rmdir /s /q third_party
```
---
## iOS
### Setup (do once)
---
- This guide is based on [Intro to SDL w/ XCode](https://github.com/libsdl-org/SDL/blob/main/docs/INTRO-xcode.md). But also on my trial and error.
#### Setup Xcode project
---
- Open Xcode (gui application)
- Click "Create New Project..."
- Choose "App", then Next
- Keep the default which is
    - Interface: Storyboard
    - Language: Objective-C
- Remove the .h and .m files
    - ```bash
      find . \( -name "*.m" -o -name "*.h" \) -type f -delete
        ```
#### Integrate SDL3
---
- Download the [SDL repo](https://github.com/libsdl-org/SDL/tree/main)
    - ```bash
      git clone https://github.com/libsdl-org/SDL.git
        ```
- Add/drag this file in as a reference
    - SDL/Xcode/SDL/SDL.xcodeproj
- Now, set "SDL3" as a build target and press Play button
- Add SDL3.framework to Frameworks, Libraries, and Embedded Content
    - if you miss this step, "SDL.h" will be missing
#### Integrate Vulkan
---
- Download the [Vulkan SDK for Mac](https://vulkan.lunarg.com/sdk/home#mac)
- Add/drag this file in as a reference
    - VulkanSDK/{version}/macOS/lib/MoltenVK.xcframework
- Navigate to: Project dir root icon -> TARGETS -> Project Name -> Build Settings
- Search for "Header Search Paths"
    - Add path to "VulkanSDK/1.4.341.1/macOS/include"
#### More dependencies
---
- If you try to build, errors will be thrown. I found by trial and error you need to following frameworks from Apple SDK.
- Navigate to: Project dir root icon -> TARGETS -> Project Name -> General -> Frameworks, Libraries, and Embedded Content (MoltenVK.xcframework should already be there)
- Click on the plus sign and start adding the following:
    - CoreFoundation.framework
    - Metal.framework
    - IOSurface.framework
    - CoreGraphics.framework
    - Foundation.framework
    - QuartzCore.framework
    - UIKit.framework
    - SDL3.framework
#### Finally, integrate the Project source
---
- Add/drag in the entire folder into the Xcode project ("Part_01_window", "Part_03_minimal_triangle", etc...). Xcode is smart enough to just find source files.
- If we have any include files, then 
    - Navigate to: Project dir root icon -> TARGETS -> Project Name -> Build Settings
    - Search for "Header Search Paths"
    - Add a path to our include directory

### Build & Run (every time you modify code)
- Connect your iPhone to the Mac
- Click the Play button (build & run)
- iPhone might ask you to trust the app:
    - go to Settings -> Privacy & Security, enable the Developer Mode on your iPhone
    - go to Settings -> VPN & Device Management, and trust the app
