#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_main_impl.h>

#include "XZRenderer.hpp"
#include "XZDuelAnim.hpp"
#include "XZDuelPlay.hpp"
#include "XZParticleSystem.hpp"

#include <iostream>
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
    XZParticleSystem::ParticleSystem particle_system;

    XZDuelAnim::SwordAnim sword_anim{{0.0f, 90.0f, 270.0f}, {0.3f, 0.3f, 0.3f}};
    XZDuelPlay::DuelPlay  duel_play;

    App() : renderer(1920, 1080, "XZRenderer") {}
};

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

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_RIGHT)
        app->duel_play.onMouseDown();

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_RIGHT)
        app->duel_play.onMouseUp();

    return SDL_APP_CONTINUE;
}

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
        app.duel_play.setEnemyState(XZDuelPlay::EnemyState::AttackPrep);
    }
    gui.showFPS();
    gui.endWindow();
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    App* app = static_cast<App*>(appstate);
    XZRenderer::Renderer& r = app->renderer;

    // delta time
    static float last_time = 0.0f;
    float current_time = (float)SDL_GetTicks() / 1000.0f;
    float delta_time   = current_time - last_time;
    last_time          = current_time;

    // Enemy AI
    static float enemy_attack_timer = 0.0f;
    constexpr float enemy_attack_interval = 2.0f;
    enemy_attack_timer += delta_time;
    if (enemy_attack_timer >= enemy_attack_interval) {
        enemy_attack_timer = 0.0f;
        app->duel_play.setEnemyState(XZDuelPlay::EnemyState::AttackPrep);
    }

    // Gameplay
    app->duel_play.update(delta_time);  
    switch (app->duel_play.getPlayerState()) {
    case XZDuelPlay::PlayerState::Parry:
        app->sword_anim.setState(XZDuelAnim::PlayerSwordState::Parrying);
        break;
    case XZDuelPlay::PlayerState::Block:
        app->sword_anim.setState(XZDuelAnim::PlayerSwordState::Blocking);
        break;
    case XZDuelPlay::PlayerState::Idle:
        app->sword_anim.setState(XZDuelAnim::PlayerSwordState::Idle);
        break;
    }
    switch (app->duel_play.getEnemyState()) {
    case XZDuelPlay::EnemyState::AttackPrep:
        app->sword_anim.setState(XZDuelAnim::EnemySwordState::AttackPreparing);
        break;
    case XZDuelPlay::EnemyState::Attack:
        app->sword_anim.setState(XZDuelAnim::EnemySwordState::Attacking);
        break;
    case XZDuelPlay::EnemyState::Idle:
        app->sword_anim.setState(XZDuelAnim::EnemySwordState::Idle);
        break;
    }

    if (app->duel_play.checkParry()) {
        app->duel_play.onParry();
        app->particle_system.emitSparks(app->playerSword->getPosition());
        // PostProcess — not yet implemented
        // XZRenderer::PostProcess::radialBlur();
        // XZRenderer::PostProcess::screenShake();
        // XZRenderer::PostProcess::chromaticAberration();
        // XZRenderer::PostProcess::bloom();
    };

    XZDuelAnim::Transformation sword_transform;

    sword_transform = app->sword_anim.getEnemySwordTransformation(app->enemyMesh->getPosition(), app->enemyMesh->getRotation(), delta_time);
    app->enemySword->setPosition(sword_transform.position);
    app->enemySword->setRotation(sword_transform.rotation);
    app->enemySword->setScale(sword_transform.scale);

    sword_transform = app->sword_anim.getPlayerSwordTransformation(app->playerMesh->getPosition(), app->playerMesh->getRotation(), delta_time);
    app->playerSword->setPosition(sword_transform.position);
    app->playerSword->setRotation(sword_transform.rotation);
    app->playerSword->setScale(sword_transform.scale);

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