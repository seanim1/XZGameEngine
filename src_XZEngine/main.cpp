#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_main_impl.h>

#include "XZRenderer.hpp"
#include "XZDuelAnim.hpp"
#include "XZDuelPlay.hpp"
#include "XZParticleSystem.hpp"
#include "XZCamera.hpp"
#include "XZAudio.hpp"
#include <iostream>

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

struct App {
    XZRenderer::Renderer renderer;

    XZRenderer::MeshObject*           playerMesh  = nullptr;
    XZRenderer::MeshObject*           enemyMesh   = nullptr;
    XZRenderer::MeshObject*           enemySword  = nullptr;
    XZRenderer::MeshObject*           playerSword = nullptr;
    XZRenderer::CustomShaderQuad*     enemyFace   = nullptr;
    XZRenderer::PointLight*           light       = nullptr;
    XZRenderer::CustomShaderPoints3d* sparks      = nullptr;

    XZDuelAnim::SwordAnim            sword_anim{{0.0f, 90.0f, 270.0f}, {0.3f, 0.3f, 0.3f}};
    XZDuelPlay::DuelPlay             duel_play;
    XZParticleSystem::ParticleSystem particle_system;
    XZCamera::Camera                 camera{{-2.6f, 3.8f, -8.0f}, {0.0f, 0.0f, 0.0f}};

    XZAudio::Sound parry_sound;

    float post_effect_timer    = 0.0f;
    float post_effect_duration = 0.2f;
    float time_scale           = 1.0f;
    float slow_mo_timer        = 0.0f;
    float slow_mo_duration     = 0.5f;
    float slow_mo_scale        = 0.2f;

    App() : renderer(720, 360, "XZRenderer") {}
};

static void update_gui(XZRenderer::ImGuiLayer& gui, App& app)
{
    gui.beginWindow("Scene");
    gui.text("Press T to toggle GUI");
    gui.exposeCamera("Camera");
    gui.separator();
    gui.exposeTransformation(*app.playerMesh, "Tetrahedron");
    gui.separator();
    gui.exposeTransformation(*app.enemyFace,  "Face");
    gui.separator();
    gui.exposeTransformation(*app.enemySword, "Sword");
    gui.separator();
    gui.exposeLight(*app.light, "Light");
    gui.separator();
    gui.exposeClearColor("Background");
    if (gui.exposeButton("Enemy Attack"))
        app.duel_play.setEnemyState(XZDuelPlay::EnemyState::AttackPrep);
    gui.showFPS();
    gui.endWindow();
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    App* app = new App();
    *appstate = app;

    app->renderer.init(2.0f);

    app->playerMesh = &app->renderer.createMeshObject(
        std::vector<XZRenderer::Vertex>(tetra_vertices, tetra_vertices + 12),
        std::vector<uint32_t>(tetra_indices, tetra_indices + 12),
        ASSET_OUTPUT_DIR "uv_checker.png"
    );
    app->playerMesh->setPosition(0.0f, 0.0f, -2.0f);
    app->playerMesh->setRotation(0.0f, 180.0f, 0.0f);

    app->enemyMesh = &app->renderer.createMeshObject(
        std::vector<XZRenderer::Vertex>(tetra_vertices, tetra_vertices + 12),
        std::vector<uint32_t>(tetra_indices, tetra_indices + 12),
        ASSET_OUTPUT_DIR "uv_checker.png"
    );
    app->enemyMesh->setPosition(0.0f, 0.0f, 2.0f);

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

    app->enemyFace = &app->renderer.createCustomShaderQuad(SHADER_OUTPUT_DIR "face.spv");
    app->enemyFace->setPosition(app->enemyMesh->getPosition() + 1.0f * app->enemyMesh->getForward());
    app->enemyFace->setRotation(0.0f, 0.0f, 180.0f);
    app->enemyFace->setScale(3.0f);

    app->light = &app->renderer.createPointLight();
    app->light->setPosition(2.0f, 2.0f, -2.0f);

    app->sparks = &app->renderer.createCustomShaderPoints3d(
        SHADER_OUTPUT_DIR "sparkParticles.spv", 500);
    app->sparks->setVisible(false);

    app->renderer.setCameraPosition(app->camera.getPosition());
    app->renderer.setCameraTarget(app->camera.getTarget());

    app->parry_sound.init();
    app->parry_sound.setPCM(XZAudio::generate_parry_clash());

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

SDL_AppResult SDL_AppIterate(void* appstate)
{
    App* app = static_cast<App*>(appstate);
    XZRenderer::Renderer& r = app->renderer;

    // Delta time — must come before frame cap
    static float last_time = (float)SDL_GetTicks() / 1000.0f;
    float current_time = (float)SDL_GetTicks() / 1000.0f;
    float delta_time   = current_time - last_time;
    last_time          = current_time;
    delta_time         = std::min(delta_time, 1.0f / 60.0f);

    // Cap to 60fps
    static float last_frame_time = 0.0f;
    if (current_time - last_frame_time < 1.0f / 60.0f)
        return SDL_APP_CONTINUE;
    last_frame_time = current_time;

    // Slow motion
    if (app->slow_mo_timer > 0.0f) {
        app->slow_mo_timer -= delta_time;
        app->time_scale = app->slow_mo_scale;
    } else {
        app->time_scale = 1.0f;
    }
    float scaled_delta = delta_time * app->time_scale;

    // Camera
    app->camera.update(scaled_delta);
    r.setCameraPosition(app->camera.getPosition());
    r.setCameraTarget(app->camera.getTarget());

    // Enemy AI
    static float enemy_attack_timer = 0.0f;
    constexpr float enemy_attack_interval = 2.0f;
    enemy_attack_timer += scaled_delta;
    if (enemy_attack_timer >= enemy_attack_interval) {
        enemy_attack_timer = 0.0f;
        app->duel_play.setEnemyState(XZDuelPlay::EnemyState::AttackPrep);
    }

    // Gameplay
    app->duel_play.update(scaled_delta);

    switch (app->duel_play.getPlayerState()) {
    case XZDuelPlay::PlayerState::Parry:
        app->sword_anim.setState(XZDuelAnim::PlayerSwordState::Parrying);
        break;
    case XZDuelPlay::PlayerState::CounterAvailable:
    case XZDuelPlay::PlayerState::Block:
        app->sword_anim.setState(XZDuelAnim::PlayerSwordState::Blocking);
        break;
    case XZDuelPlay::PlayerState::Idle:
        app->sword_anim.setState(XZDuelAnim::PlayerSwordState::Idle);
        break;
    default:
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
    default:
        break;
    }

    // Particles
    app->particle_system.update(scaled_delta);

    if (app->duel_play.checkParry()) {
        app->duel_play.onParry();
        app->parry_sound.play();
        app->particle_system.emitSparks(app->enemySword->getPosition());
        app->sparks->setVisible(true);
        app->camera.triggerShake(app->post_effect_duration, 0.05f);
        app->renderer.setChromaticAberration(true, 0.008f);
        app->renderer.setRadialBlur(true, 0.215f, 10);
        app->post_effect_timer = 0.0f;
        app->slow_mo_timer     = app->slow_mo_duration;
    }

    std::vector<glm::vec3> particle_positions;
    for (const XZParticleSystem::Particle& p : app->particle_system.getParticles())
        if (p.alive) particle_positions.push_back(p.position);
    app->sparks->setPositions(particle_positions);

    // Post effects — unscaled timer
    app->post_effect_timer += delta_time;
    if (app->post_effect_timer >= app->post_effect_duration) {
        app->renderer.setChromaticAberration(false);
        app->renderer.setRadialBlur(false);
    }
    if (app->particle_system.aliveCount() == 0)
        app->sparks->setVisible(false);

    // Sword transforms
    XZDuelAnim::Transformation sword_transform;

    sword_transform = app->sword_anim.getEnemySwordTransformation(
        app->enemyMesh->getPosition(), app->enemyMesh->getRotation(), scaled_delta);
    app->enemySword->setPosition(sword_transform.position);
    app->enemySword->setRotation(sword_transform.rotation);
    app->enemySword->setScale(sword_transform.scale);

    sword_transform = app->sword_anim.getPlayerSwordTransformation(
        app->playerMesh->getPosition(), app->playerMesh->getRotation(), scaled_delta);
    app->playerSword->setPosition(sword_transform.position);
    app->playerSword->setRotation(sword_transform.rotation);
    app->playerSword->setScale(sword_transform.scale);

    if (!r.beginFrame()) return SDL_APP_SUCCESS;
    update_gui(r.getGui(), *app);
    r.submitFrame();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    App* app = static_cast<App*>(appstate);
    delete app;
}