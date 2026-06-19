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
struct CustomShaderPoints3dImpl;   // NEW

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
    void loadFromGLTF(const std::string& mesh_path,
                      const std::string& texture_path);
    void loadFromVertices(const std::vector<Vertex>&   vertices,
                          const std::vector<uint32_t>& indices,
                          const std::string&           texture_path);

    glm::vec3 position_ = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation_ = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale_    = {1.0f, 1.0f, 1.0f};

    std::unique_ptr<MeshObjectImpl> impl_;
};

// ---------------------------------------------------------------
//  CustomShaderQuad
// ---------------------------------------------------------------
class CustomShaderQuad {
public:
    explicit CustomShaderQuad(const std::string& frag_spv_path);

    void setVertices(const std::vector<glm::vec3>& positions,
                     const std::vector<glm::vec2>& uvs,
                     const std::vector<uint32_t>&  indices);

    void setPosition(float x, float y, float z);
    void setPosition(const glm::vec3& pos);
    void setRotation(float x, float y, float z);
    void setRotation(const glm::vec3& degrees);
    void setScale(float uniform_scale);
    void setScale(float x, float y, float z);
    void setScale(const glm::vec3& scale);

    const glm::vec3& getPosition() const { return position_; }
    const glm::vec3& getRotation() const { return rotation_; }
    const glm::vec3& getScale()    const { return scale_; }

    CustomShaderQuadImpl* impl() const { return impl_.get(); }

private:
    friend class Renderer;

    std::string frag_spv_path_;
    glm::vec3   position_ = {0.0f, 0.0f, 0.0f};
    glm::vec3   rotation_ = {0.0f, 0.0f, 0.0f};
    glm::vec3   scale_    = {1.0f, 1.0f, 1.0f};

    std::unique_ptr<CustomShaderQuadImpl> impl_;
};

// ---------------------------------------------------------------
//  CustomShaderPoints3d  —  a batch of world-space points rendered
//  with a user-supplied fragment shader (.spv).
//  Useful for particles, sparks, debug visualizations etc.
//
//  Usage:
//    auto& sparks = renderer.createCustomShaderPoints3d("spark.frag.spv", 500);
//    sparks.setPositions(positions);   // call every frame with alive positions
//    sparks.setVisible(true/false);    // toggle rendering
// ---------------------------------------------------------------
class CustomShaderPoints3d {
public:
    explicit CustomShaderPoints3d(const std::string& frag_spv_path);

    // Upload new positions — call every frame with alive particle positions only.
    // Passing an empty vector hides all points.
    void setPositions(const std::vector<glm::vec3>& positions);

    // Toggle rendering without clearing positions
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const        { return visible_; }

    // Internal
    CustomShaderPoints3dImpl* impl() const { return impl_.get(); }

private:
    friend class Renderer;

    std::string            frag_spv_path_;
    std::vector<glm::vec3> positions_;
    bool                   visible_ = true;

    std::unique_ptr<CustomShaderPoints3dImpl> impl_;
};

// ---------------------------------------------------------------
//  ImGuiLayer
// ---------------------------------------------------------------
class ImGuiLayer {
public:
    void exposeClearColor(const std::string& label);
    void exposeTransformation(MeshObject&       obj,  const std::string& label);
    void exposeTransformation(CustomShaderQuad& quad, const std::string& label);
    void exposeLight(PointLight& light, const std::string& label);
    void exposeCamera(const std::string& label);
    bool exposeButton(const std::string& label);
    void beginWindow(const std::string& title);
    void endWindow();
    void separator();
    void text(const std::string& str);
    void showFPS();

    // Internal
    RendererImpl* renderer_impl_ = nullptr;
};

// ---------------------------------------------------------------
//  Renderer
// ---------------------------------------------------------------
class Renderer {
public:
    explicit Renderer(uint32_t width, uint32_t height, const std::string& title);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&)                 = delete;
    Renderer& operator=(Renderer&&)      = delete;

    // ---- Configuration (call BEFORE init()) ----
    void setClearColor(float r, float g, float b, float a = 1.0f);
    void setCameraPosition(glm::vec3 pos_);
    void setCameraTarget(glm::vec3 target_);
    void enableLogging(bool enable);

    // ---- Initialisation (call ONCE) ----
    void init(float guiSize);

    // ---- Scene objects (call AFTER init()) ----
    MeshObject& createMeshObject(const std::vector<Vertex>&   vertices,
                                 const std::vector<uint32_t>& indices,
                                 const std::string&           texture_path);

    MeshObject& createMeshObject(const std::string& mesh_path,
                                 const std::string& texture_path);

    CustomShaderQuad& createCustomShaderQuad(const std::string& frag_spv_path);

    // max_points: pre-allocates the GPU buffer at this capacity.
    // setPositions() must never exceed this number.
    CustomShaderPoints3d& createCustomShaderPoints3d(const std::string& frag_spv_path,
                                                      uint32_t           max_points = 500);

    PointLight& createPointLight();

    // ---- ImGui ----
    ImGuiLayer& getGui();

    // ---- Event handling ----
    bool handleEvent(void* sdl_event);

    // ---- Frame loop ----
    bool beginFrame();
    void submitFrame();

private:
    MeshObject& createMeshObject();

    std::unique_ptr<RendererImpl>                       m_impl;
    std::vector<std::unique_ptr<MeshObject>>            m_mesh_objects;
    std::vector<std::unique_ptr<CustomShaderQuad>>      m_quads;
    std::vector<std::unique_ptr<CustomShaderPoints3d>>  m_points_clusters;  // NEW
    std::vector<std::unique_ptr<PointLight>>            m_lights;
    std::unique_ptr<ImGuiLayer>                         m_gui;

    uint32_t    m_window_width    = 800;
    uint32_t    m_window_height   = 600;
    std::string m_window_title    = "XZRenderer";
    bool        m_logging_enabled = false;
};

} // namespace XZRenderer