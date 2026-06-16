#pragma once

#include <unordered_map>
#include <vector>
#include <optional>
#include <chrono>

#include <glm/glm.hpp>

namespace XZSwordDuel {

// ─────────────────────────────────────────────
//  Top-level states  (consumed by animation / renderer)
// ─────────────────────────────────────────────

enum class PlayerState {
    Idle,
    Attacking,
    Blocking
};

enum class EnemyState {
    Idle,
    Attacking
};

// ─────────────────────────────────────────────
//  Sub-states  (consumed by hitbox / combat logic)
// ─────────────────────────────────────────────

enum class AttackingState {
    WindingUp,
    SendHitSignal,   // single-frame pulse
    WindingDown
};

enum class BlockingState {
    Parrying,        // instant on block press; active briefly
    Blocking         // held guard after parry window closes
};

enum class ParriedState {
    CounterAvailable,        // window is open; waiting for player attack press
    SendCounteringSignal     // single-frame pulse; triggers isEnemyDamaged
};

// ─────────────────────────────────────────────
//  Player input  (set each frame by SDL layer)
// ─────────────────────────────────────────────

enum class PlayerAction {
    Idle,
    Attack,
    Block
};

// ─────────────────────────────────────────────
//  Enemy combo table types
// ─────────────────────────────────────────────

struct AttackPhase {
    AttackingState phase;
    float          duration;  // seconds; ignored / 0 for SendHitSignal
};

struct AttackCombo {
    std::vector<AttackPhase> sequence;
};

// ─────────────────────────────────────────────
//  Duel
// ─────────────────────────────────────────────

class Duel {
public:
    Duel();

    // ── Input (call from SDL_AppIterate before update) ──
    void setPlayerAction(PlayerAction action);
    void setPlayerPosition(glm::vec2 pos);
    void setEnemyPosition(glm::vec2 pos);

    // ── Tick (call once per SDL_AppIterate frame) ──
    void update(float deltaTime);

    // ── Simulation entry point (wraps internal chrono timer) ──
    void run();

    // ── Queries for renderer / animation ──
    glm::vec2   getPlayerPosition() const;
    glm::vec2   getEnemyPosition()  const;
    PlayerState getPlayerState()    const;
    EnemyState  getEnemyState()     const;

    // Sub-state accessors (valid after update())
    std::optional<AttackingState> getPlayerAttackingState() const;
    std::optional<BlockingState>  getPlayerBlockingState()  const;
    std::optional<ParriedState>   getParriedState()         const;
    std::optional<AttackingState> getEnemyAttackingState()  const;

    // ── Attack duration queries ───────────────────────────────────────────────
    // Returns WindingUp + WindingDown summed for the active combo.
    // Pass the result directly into XZSwordAnim::SwordAnim::playSwordAttack().
    // Player value is constant (kPlayerWindUp + kPlayerWindDown).
    // Enemy value varies per combo — always reflects the currently running combo.
    float getPlayerAttackDuration() const;
    float getEnemyAttackDuration()  const;

    // ── Pulse flags (true for exactly one frame; reset at start of update) ──
    bool isParried()       const;   // enemy hit during player Parrying
    bool isBlocked()       const;   // enemy hit during player Blocking
    bool isPlayerDamaged() const;   // enemy hit during player Idle/Attacking
    bool isEnemyDamaged()  const;   // player SendCounteringSignal fired

private:
    // ── Positions ──
    glm::vec2 playerPos_ { 0.0f, 0.0f };
    glm::vec2 enemyPos_  { 0.0f, 0.0f };

    // ── Player state machine ──
    PlayerState                   playerState_      = PlayerState::Idle;
    std::optional<AttackingState> playerAttState_;
    std::optional<BlockingState>  playerBlockState_;
    std::optional<ParriedState>   parriedState_;
    float                         playerPhaseTimer_ = 0.0f;

    // Player attack timing constants
    static constexpr float kPlayerWindUp   = 0.25f;  // seconds
    static constexpr float kPlayerWindDown = 0.25f;
    static constexpr float kParryWindow    = 0.15f;

    // ── Enemy state machine ──
    EnemyState                    enemyState_      = EnemyState::Idle;
    std::optional<AttackingState> enemyAttState_;
    float                         enemyPhaseTimer_ = 0.0f;
    float                         enemyIdleTimer_  = 0.0f;
    static constexpr float        kEnemyIdleDuration = 1.0f;

    // Active combo
    int currentComboId_  = 0;
    int currentPhaseIdx_ = 0;

    // ── Combo table ──
    std::unordered_map<int, AttackCombo> combos_;
    void buildCombos();
    void pickCombo();
    void advanceEnemyCombo(float deltaTime);

    // ── Pulse flags ──
    bool parried_       = false;
    bool blocked_       = false;
    bool playerDamaged_ = false;
    bool enemyDamaged_  = false;

    // ── Internal helpers ──
    void updatePlayer(float deltaTime);
    void updateEnemy(float deltaTime);
    void resolveHit();

    // ── Internal chrono timer (for run()) ──
    std::chrono::steady_clock::time_point lastTime_;
    bool runTimerInitialized_ = false;

    // ── Cached action for current frame ──
    PlayerAction currentAction_ = PlayerAction::Idle;
};

} // namespace XZSwordDuel