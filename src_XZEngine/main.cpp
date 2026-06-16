#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_main_impl.h>

#include "XZRenderer.hpp"

// Tetrahedron vertex data — flat-shaded normals baked in
static const XZRenderer::Vertex tetra_vertices[] = {
    {{ 0.0f,  1.0f,  0.0f},    {0.5f, 0.0f}, {-0.8321f,  0.2773f,  0.4804f}},
    {{ 0.0f, -1.0f,  1.1547f}, {0.0f, 1.0f}, {-0.8321f,  0.2773f,  0.4804f}},
    {{-1.0f, -1.0f, -0.5774f}, {1.0f, 1.0f}, {-0.8321f,  0.2773f,  0.4804f}},
    {{ 0.0f,  1.0f,  0.0f},    {0.5f, 0.0f}, { 0.0000f,  0.2774f, -0.9608f}},
    {{-1.0f, -1.0f, -0.5774f}, {0.0f, 1.0f}, { 0.0000f,  0.2774f, -0.9608f}},
    {{ 1.0f, -1.0f, -0.5774f}, {1.0f, 1.0f}, { 0.0000f,  0.2774f, -0.9608f}},
    {{ 0.0f,  1.0f,  0.0f},    {0.5f, 0.0f}, { 0.8321f,  0.2773f,  0.4804f}},
    {{ 1.0f, -1.0f, -0.5774f}, {0.0f, 1.0f}, { 0.8321f,  0.2773f,  0.4804f}},
    {{ 0.0f, -1.0f,  1.1547f}, {1.0f, 1.0f}, { 0.8321f,  0.2773f,  0.4804f}},
    {{ 0.0f, -1.0f,  1.1547f}, {0.0f, 0.0f}, { 0.0000f, -1.0000f,  0.0000f}},
    {{ 1.0f, -1.0f, -0.5774f}, {1.0f, 0.0f}, { 0.0000f, -1.0000f,  0.0000f}},
    {{-1.0f, -1.0f, -0.5774f}, {0.5f, 1.0f}, { 0.0000f, -1.0000f,  0.0000f}},
};
static const uint32_t tetra_indices[] = { 0,1,2, 3,4,5, 6,7,8, 9,10,11 };

// ============================================================
//  App state — engine objects
// ============================================================
struct App {
    XZRenderer::Renderer renderer;

    XZRenderer::MeshObject* playerMesh  = nullptr;
    XZRenderer::MeshObject* enemyMesh   = nullptr;
    XZRenderer::MeshObject* playerSword = nullptr;
    XZRenderer::CustomShaderQuad* playerFace  = nullptr;
    XZRenderer::PointLight*       light = nullptr;
    
    App() : renderer(1920, 1080, "XZRenderer") {}
};

static void update_gui(XZRenderer::ImGuiLayer& gui, App& app)
{
    gui.beginWindow("Scene");
    gui.text("Press T to toggle GUI");
    gui.exposeCamera("Camera");
    gui.separator();
    gui.exposeTransformation(*app.playerMesh, "Tetrahedron");
    gui.separator();
    gui.exposeTransformation(*app.playerFace,  "Face");
    gui.separator();
    gui.exposeTransformation(*app.playerSword, "Sword");
    gui.separator();
    gui.exposeLight(*app.light,          "Light");
    gui.separator();
    gui.exposeClearColor("Background");
    gui.showFPS();
    gui.endWindow();
}

// ============================================================
//  SDL3 app callbacks
// ============================================================
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    App* app = new App();
    *appstate  = app;

    app->renderer.init(2.0f);

    // --- Tetrahedron ---
    app->playerMesh = &app->renderer.createMeshObject(
        std::vector<XZRenderer::Vertex>(tetra_vertices, tetra_vertices + 12),
        std::vector<uint32_t>(tetra_indices, tetra_indices + 12),
        ASSET_OUTPUT_DIR "uv_checker.png"
    );
    app->playerMesh->setPosition(0.0f, 0.0f, -2.0f);
    app->playerMesh->setRotation(0.0f, 180.0f, 0.0f);

    app->playerSword = &app->renderer.createMeshObject(
        ASSET_OUTPUT_DIR "sword_0.glb",
        ASSET_OUTPUT_DIR "sword_0.jpg"
    );
    app->playerSword->setPosition(app->playerMesh->getPosition());
    app->playerSword->setRotation(90.0f, 0.0f, 0.0f);
    app->playerSword->setScale(0.3f);

    // --- Face overlay quad ---
    app->playerFace = &app->renderer.createCustomShaderQuad(SHADER_OUTPUT_DIR "face.spv");
    app->playerFace->setPosition(-0.4f, 0.4f, -1.0f);
    app->playerFace->setRotation(0.0f, 0.0f, 180.0f);
    app->playerFace->setScale(3.0f);

    app->enemyMesh = &app->renderer.createMeshObject(
    std::vector<XZRenderer::Vertex>(tetra_vertices, tetra_vertices + 12),
    std::vector<uint32_t>(tetra_indices, tetra_indices + 12),
    ASSET_OUTPUT_DIR "uv_checker.png"
    );
    app->enemyMesh->setPosition(0.0f, 0.0f, 2.0f);

    // --- Light ---
    app->light = &app->renderer.createPointLight();
    app->light->setPosition(2.0f, 2.0f, -2.0f);

    app->renderer.setCameraPosition(-2.6f, 3.8f, -8.0f);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    App* app = static_cast<App*>(appstate);
    if (app->renderer.handleEvent(event)) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    App* app = static_cast<App*>(appstate);
    XZRenderer::Renderer& r = app->renderer;

    app->playerSword->setRotation(app->playerMesh->getRotation() + glm::vec3(0.0f, 180.0f, 0.0f));
    app->playerSword->setPosition(app->playerMesh->getPosition() + app->playerMesh->getRight() * 2.0f);

    if (!r.beginFrame()) return SDL_APP_SUCCESS;

    update_gui(r.getGui(), *app);

    r.endFrame();
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    App* app = static_cast<App*>(appstate);
    delete app;
}