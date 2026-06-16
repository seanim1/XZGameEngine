
## User Guide:
Try interacting with the UI

## Updated:
modified:   .github/workflows/build.yml
modified:   CMakeLists.txt
modified:   README.md
modified:   setup.ps1
modified:   setup_linux.sh
modified:   setup_macos.sh

## Added:
assets/sword_0.glb
assets/sword_0.jpg

## New:
I modeled a 3d mesh in Blender and exported in glTF 2.0. I did it primarily for UV and vertex positions.
I also 
Assimp include added
SwordMesh struct — vertex buffer, index buffer, index count
create_texture_from_pixels helper — extracted from create_texture so both tetra and sword can share the same texture upload logic
create_sword_texture — loads sword_0.jpg
load_sword — Assimp loads sword_0.glb, triangulates, extracts positions/UVs/normals into Vertex structs, uploads to GPU buffers, stores in app->sword_meshes
sword_uniform_buffers + sword_descriptor_sets — separate UBO and descriptor set for the sword, same layout as tetra
write_descriptor_set helper — reduces boilerplate for writing UBO + sampler pairs
create_descriptors — tetra and sword share the same descriptor_set_layout, each has its own pool and sets
update_uniform_buffer — sword gets its own model matrix (offset right at x=2.0), independent sword_rotation_y auto-spinning, tetra offset left at x=-2.0
record_scene_command_buffers — sword draw calls after face quad, reuses main pipeline
Camera pulled back to z=-6 to fit both objects
Cleanup handles sword meshes, texture, uniforms, descriptor pool

