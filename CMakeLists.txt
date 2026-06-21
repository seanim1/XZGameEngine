cmake_minimum_required(VERSION 3.22)
project(SDL3HelloTriangle VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On Windows use vcpkg SDL3 (has Vulkan support), on other platforms use source-built
if(NOT WIN32)
    set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/build")
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)
find_package(assimp REQUIRED)

add_library(sdl3_deps INTERFACE)
target_link_libraries(sdl3_deps INTERFACE SDL3::SDL3)

add_library(vulkan_deps INTERFACE)
target_include_directories(vulkan_deps INTERFACE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(vulkan_deps INTERFACE Vulkan::Vulkan)

# Platform-specific Vulkan defines
if(APPLE)
    target_compile_definitions(vulkan_deps INTERFACE
        VK_USE_PLATFORM_METAL_EXT
        VK_ENABLE_BETA_EXTENSIONS
    )
elseif(WIN32)
    target_compile_definitions(vulkan_deps INTERFACE VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    target_compile_definitions(vulkan_deps INTERFACE VK_USE_PLATFORM_XLIB_KHR)
endif()

# --- ImGui ---
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
# -------------

# --- glm ---
add_library(glm_deps INTERFACE)
target_link_libraries(glm_deps INTERFACE glm::glm)
# -----------

# --- stb ---
add_library(stb_deps INTERFACE)
target_include_directories(stb_deps INTERFACE vendor)
# -----------

# --- assimp ---
add_library(assimp_deps INTERFACE)
target_link_libraries(assimp_deps INTERFACE assimp::assimp)
# --------------

add_subdirectory(src_XZEngine)