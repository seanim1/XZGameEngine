#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_main_impl.h>

#include "XZRenderer.hpp"
#include "XZDuelAnim.hpp"

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
    XZRenderer::MeshObject* enemySword = nullptr;
    XZRenderer::MeshObject* playerSword = nullptr;
    XZRenderer::CustomShaderQuad* playerFace  = nullptr;
    XZRenderer::PointLight*       light = nullptr;

    XZDuelAnim::SwordAnim sword_anim{{0.0f, 90.0f, 270.0f}, {0.3f, 0.3f, 0.3f}};
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
    gui.exposeTransformation(*app.enemySword, "Sword");
    gui.separator();
    gui.exposeLight(*app.light,          "Light");
    gui.separator();
    gui.exposeClearColor("Background");
    if (gui.exposeButton("Enemy Attack")) {
        app.sword_anim.setState(XZDuelAnim::EnemySwordState::Attacking);
    }
    if (gui.exposeButton("Player Block")) {
        app.sword_anim.setState(XZDuelAnim::PlayerSwordState::Blocking);
    }
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
    app->enemyMesh = &app->renderer.createMeshObject(
        std::vector<XZRenderer::Vertex>(tetra_vertices, tetra_vertices + 12),
        std::vector<uint32_t>(tetra_indices, tetra_indices + 12),
        ASSET_OUTPUT_DIR "uv_checker.png"
    );
    app->enemyMesh->setPosition(0.0f, 0.0f, 2.0f);

    app->playerMesh->setPosition(0.0f, 0.0f, -2.0f);
    app->playerMesh->setRotation(0.0f, 180.0f, 0.0f);
    app->enemySword = &app->renderer.createMeshObject(
        ASSET_OUTPUT_DIR "sword_0.glb",
        ASSET_OUTPUT_DIR "sword_0.jpg"
    );
    app->enemySword->setScale(0.3f, 0.3f, 0.3f);
    app->playerSword = &app->renderer.createMeshObject(
        ASSET_OUTPUT_DIR "sword_0.glb",
        ASSET_OUTPUT_DIR "sword_0.jpg"
    );
    app->playerSword->setScale(0.3f, 0.3f, 0.3f);

    // --- Face overlay quad ---
    app->playerFace = &app->renderer.createCustomShaderQuad(SHADER_OUTPUT_DIR "face.spv");
    app->playerFace->setPosition(-0.4f, 0.4f, -1.0f);
    app->playerFace->setRotation(0.0f, 0.0f, 180.0f);
    app->playerFace->setScale(3.0f);

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

    static float last_time = 0.0f;
    float current_time = (float)SDL_GetTicks() / 1000.0f;
    float delta_time   = current_time - last_time;
    last_time          = current_time;

    XZDuelAnim::Transformation sword_transform;

    sword_transform = app->sword_anim.getEnemySwordTransformation(app->enemyMesh->getPosition(), app->enemyMesh->getRotation(), delta_time);
    app->enemySword->setPosition(sword_transform.position);
    app->enemySword->setRotation(sword_transform.rotation);
    app->enemySword->setScale(sword_transform.scale);
        std::cout << "Position: " << (sword_transform.scale).y << std::endl;

    sword_transform = app->sword_anim.getPlayerSwordTransformation(app->playerMesh->getPosition(), app->playerMesh->getRotation(), delta_time);
    app->playerSword->setPosition(sword_transform.position);
    app->playerSword->setRotation(sword_transform.rotation);
    app->playerSword->setScale(sword_transform.scale);
        std::cout << "Position: " << (sword_transform.scale).y << std::endl;

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