add_executable(XZGameEngine main.cpp XZRenderer.cpp)
target_link_libraries(XZGameEngine PRIVATE sdl3_deps vulkan_deps imgui glm_deps stb_deps assimp_deps)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/")
set(ASSET_OUTPUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/assets/")

add_custom_target(XZ_copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND glslangValidator -V -S vert "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/vert.spv"
    COMMAND glslangValidator -V -S vert "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.vert" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/quad.spv"
    COMMAND glslangValidator -V -S vert "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles3d.vert" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/particles3d.spv"
    COMMAND glslangValidator -V -S frag "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/frag.spv"
    COMMAND glslangValidator -V -S frag "${CMAKE_CURRENT_SOURCE_DIR}/shaders/face.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/face.spv"
    COMMAND glslangValidator -V -S frag "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sparkParticles.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/sparkParticles.spv"
    COMMAND glslangValidator -V -S vert "${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/postprocess.spv"
    COMMAND glslangValidator -V -S frag "${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/shaders/postprocessfrag.spv"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/face.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles3d.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sparkParticles.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag"

    COMMENT "Compiling shaders"
)

add_custom_target(XZ_copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/uv_checker.png"
        "${CMAKE_CURRENT_BINARY_DIR}/assets/uv_checker.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/sword_0.glb"
        "${CMAKE_CURRENT_BINARY_DIR}/assets/sword_0.glb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/sword_0.jpg"
        "${CMAKE_CURRENT_BINARY_DIR}/assets/sword_0.jpg"
    COMMENT "Copying assets"
)

add_dependencies(XZGameEngine XZ_copy_shaders XZ_copy_assets)

target_compile_definitions(XZGameEngine PRIVATE
    SHADER_OUTPUT_DIR="${SHADER_OUTPUT_DIR}"
    ASSET_OUTPUT_DIR="${ASSET_OUTPUT_DIR}"
)