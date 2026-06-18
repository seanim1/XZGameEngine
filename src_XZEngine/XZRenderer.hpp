#pragma once

// ============================================================
//  XZRenderer — Vulkan + SDL3 + ImGui rendering module
//  Namespace : XZRenderer
//
//  User code only needs to #include "XZRenderer.hpp".
//  No Vulkan, SDL, ImGui or Assimp headers leak into this file.
// ============================================================

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace XZRenderer {

// ----- Forward declarations of opaque implementation types -----
struct RendererImpl;
struct MeshObjectImpl;
struct CustomShaderQuadImpl;

// ---------------------------------------------------------------
//  Vertex  —  public vertex format for loadFromVertices()
// ---------------------------------------------------------------
struct Vertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec3 normal;
};

// ---------------------------------------------------------------
//  PointLight
// ---------------------------------------------------------------
class PointLight {
public:
    void setPosition(float x, float y, float z);
    void setPosition(const glm::vec3& pos);
    const glm::vec3& getPosition() const { return position_; }

private:
    glm::vec3 position_ = {2.0f, 2.0f, -2.0f};
};

// ---------------------------------------------------------------
//  MeshObject  —  a textured 3-D object with Blinn-Phong lighting.
// ---------------------------------------------------------------
class MeshObject {
public:
    MeshObject(RendererImpl*                renderer_impl,
               const std::vector<Vertex>&   vertices,
               const std::vector<uint32_t>& indices,
               const std::string&           texture_path);

    MeshObject(RendererImpl*      renderer_impl,
               const std::string& mesh_path,
               const std::string& texture_path);

    ~MeshObject();
    
    // --- Transform ---
    void setPosition(float x, float y, float z);
    void setPosition(const glm::vec3& pos);

    void setRotation(float x, float y, float z);   // degrees
    void setRotation(const glm::vec3& degrees);

    void setScale(float uniform_scale);
    void setScale(float x, float y, float z);
    void setScale(const glm::vec3& scale);

    // --- Accessors (read-only) ---
    const glm::vec3& getPosition() const { return position_; }
    const glm::vec3& getRotation() const { return rotation_; }
    const glm::vec3& getScale()    const { return scale_; }
    const glm::vec3 getForward() const;
    const glm::vec3 getRight() const;

    // Internal — do not call from user code
    MeshObjectImpl* impl() const { return impl_.get(); }

private:
    friend class Renderer;
    // --- Initialize ---
    void loadFromGLTF(const std::string& mesh_path,
                      const std::string& texture_path);

    void loadFromVertices(const std::vector<Vertex>&   vertices,
                          const std::vector<uint32_t>& indices,
                          const std::string&           texture_path);

    glm::vec3 position_     = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation_     = {0.0f, 0.0f, 0.0f};  // degrees
    glm::vec3 scale_        = {1.0f, 1.0f, 1.0f};
        
    std::unique_ptr<MeshObjectImpl> impl_;
};

// ---------------------------------------------------------------
//  CustomShaderQuad  —  a flat quad rendered with a user-supplied
//  compiled fragment shader (.spv).  Used for procedural overlays.
//  The shared vertex shader (vert.spv) is used automatically.
//
//  Usage:
//    auto& face = renderer.createCustomShaderQuad("face.spv");
//    face.setVertices(positions, uvs, indices);
//    face.setPosition(...);
// ---------------------------------------------------------------
class CustomShaderQuad {
public:
    explicit CustomShaderQuad(const std::string& frag_spv_path);
    // Supply baked quad geometry.
    // positions and uvs must have the same length.
    void setVertices(const std::vector<glm::vec3>& positions,
                     const std::vector<glm::vec2>& uvs,
                     const std::vector<uint32_t>&  indices);

    // --- Transform ---
    void setPosition(float x, float y, float z);
    void setPosition(const glm::vec3& pos);

    void setRotation(float x, float y, float z);
    void setRotation(const glm::vec3& degrees);

    void setScale(float uniform_scale);
    void setScale(float x, float y, float z);
    void setScale(const glm::vec3& scale);

    // --- Accessors ---
    const glm::vec3& getPosition() const { return position_; }
    const glm::vec3& getRotation() const { return rotation_; }
    const glm::vec3& getScale()    const { return scale_; }

    // Internal
    CustomShaderQuadImpl* impl() const { return impl_.get(); }

private:
    friend class Renderer;

    std::string frag_spv_path_;
    glm::vec3   position_ = {0.0f, 0.0f, 0.0f};
    glm::vec3   rotation_ = {0.0f, 0.0f, 0.0f};
    glm::vec3   scale_    = {1.0f, 1.0f, 1.0f};

    std::unique_ptr<CustomShaderQuadImpl> impl_;
};

class CustomShaderPoints3d {
public:
    explicit CustomShaderPoints3d(const std::string& frag_spv_path);

    void setPositions(const std::vector<glm::vec3>& positions);

    // Internal
    CustomShaderPoints3dImpl* impl() const { return impl_.get(); }

private:
    friend class Renderer;

    std::string            frag_spv_path_;
    std::vector<glm::vec3> positions_;

    std::unique_ptr<CustomShaderPoints3dImpl> impl_;
};

// ---------------------------------------------------------------
//  ImGuiLayer  —  exposes engine objects as ImGui widgets.
//  Obtain via Renderer::getGui().
//  All calls must be placed between beginFrame() and endFrame().
// ---------------------------------------------------------------
class ImGuiLayer {
public:
    // RGBA color picker that controls the renderer clear color
    void exposeClearColor(const std::string& label);

    // Position / Rotation / Scale sliders
    void exposeTransformation(MeshObject&       obj,  const std::string& label);
    void exposeTransformation(CustomShaderQuad& quad, const std::string& label);

    // Position-only sliders (scale/rotation irrelevant for lights)
    void exposeLight(PointLight& light, const std::string& label);

    void exposeCamera(const std::string& label);

    bool exposeButton(const std::string& label);  // returns true when clicked

    // Window management
    void beginWindow(const std::string& title);
    void endWindow();
    void separator();

    // Utility
    void text(const std::string& str);
    void showFPS();

    // Internal
    RendererImpl* renderer_impl_ = nullptr;
};

// ---------------------------------------------------------------
//  Renderer  —  top-level engine entry point.
//  One instance per application.
// ---------------------------------------------------------------
class Renderer {
public:
    // Width, height, and title are required at construction time
    explicit Renderer(uint32_t width, uint32_t height, const std::string& title);
    ~Renderer();

    // Non-copyable, non-movable
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&)                 = delete;
    Renderer& operator=(Renderer&&)      = delete;

    // ---- Configuration  (call BEFORE init()) ----
    void setClearColor(float r, float g, float b, float a = 1.0f);
    void setCameraPosition(float x, float y, float z);
    void setCameraTarget(float x, float y, float z);
    void enableLogging(bool enable);

    // ---- Initialisation  (call ONCE) ----
    void init(float guiSize);

    // ---- Scene objects  (call AFTER init()) ----
    MeshObject& createMeshObject(const std::vector<Vertex>&   vertices,
                              const std::vector<uint32_t>& indices,
                              const std::string&           texture_path);

    MeshObject& createMeshObject(const std::string& mesh_path,
                                const std::string& texture_path);

    CustomShaderQuad& createCustomShaderQuad(const std::string& frag_spv_path);

    PointLight&       createPointLight();

    // ---- ImGui ----
    ImGuiLayer& getGui();

    // ---- Event handling ----
    bool handleEvent(void* sdl_event);

    // ---- Frame loop ----
    bool beginFrame();
    void endFrame();

private:
    MeshObject&       createMeshObject();
    std::unique_ptr<RendererImpl>                  m_impl;
    std::vector<std::unique_ptr<MeshObject>>       m_mesh_objects;
    std::vector<std::unique_ptr<CustomShaderQuad>> m_quads;
    std::vector<std::unique_ptr<PointLight>>       m_lights;
    std::unique_ptr<ImGuiLayer>                    m_gui;

    uint32_t    m_window_width   = 800;
    uint32_t    m_window_height  = 600;
    std::string m_window_title   = "XZRenderer";
    bool        m_logging_enabled = false;

};

} // namespace XZRenderer