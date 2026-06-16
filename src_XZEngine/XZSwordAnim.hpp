#pragma once

#include <glm/glm.hpp>

namespace XZSwordAnim {

// ─────────────────────────────────────────────
//  Per-frame output
// ─────────────────────────────────────────────

struct SwordTransform {
    glm::vec3 pos   { 0.0f };   // world-space offset of sword pivot
    glm::vec3 rot   { 0.0f };   // euler angles in degrees (pitch X, yaw Y, roll Z)
    float     scale { 1.0f };
};

// ─────────────────────────────────────────────
//  Active clip
// ─────────────────────────────────────────────

enum class SwordAnimClip {
    None,
    Attack,
    Block,
    Idle
};

// ─────────────────────────────────────────────
//  SwordAnim
//
//  One instance per combatant (player / enemy).
//  Call the desired play*() each frame; switching
//  clips mid-animation resets internal accumulators.
//
//  playSwordAttack takes totalDuration from XZSwordDuel::Duel::getPlayerAttackDuration()
//  or getEnemyAttackDuration() — the arc plays exactly once over that window.
//  playSwordBlock and playSwordIdle loop indefinitely until interrupted.
// ─────────────────────────────────────────────

class SwordAnim {
public:
    SwordAnim() = default;

    // ── Animation functions ───────────────────────────────────────────────────
    // Safe to call every frame. Switching to a different play*() resets state.

    // One-shot: plays a single attack arc over totalDuration seconds then holds.
    // totalDuration = XZSwordDuel::Duel::getPlayerAttackDuration() / getEnemyAttackDuration()
    SwordTransform playSwordAttack(float deltaTime, float totalDuration);

    // Looping: held guard pose with micro-sway. Loops until interrupted.
    SwordTransform playSwordBlock(float deltaTime);

    // Looping: resting idle bob. Loops until interrupted.
    SwordTransform playSwordIdle(float deltaTime);

    // ── State queries ─────────────────────────────────────────────────────────

    // True once the attack arc has fully completed (t >= 1.0)
    bool isAttackDone() const;

    // Which clip is currently active
    SwordAnimClip currentClip() const;

    // Normalized [0, 1] progress through the attack arc; 0 if not attacking
    float attackProgress() const;

    // ── Tuning parameters ─────────────────────────────────────────────────────
    // Public — tweak at runtime without recompiling.

    float idleBobSpeed    = 1.20f;  // Hz of idle vertical bob
    float idleBobAmount   = 0.04f;  // world units of vertical bob amplitude
    float blockSwaySpeed  = 0.80f;  // Hz of block micro-sway
    float blockSwayAmount = 1.50f;  // degrees of block rotation sway amplitude

private:
    SwordAnimClip activeClip_ = SwordAnimClip::None;

    float t_    = 0.0f;   // normalized [0, 1] attack progress
    bool  done_ = false;  // true when attack arc has completed

    float time_ = 0.0f;   // raw time accumulator for looping clips

    void switchClip(SwordAnimClip clip);

    static SwordTransform computeAttack(float t);
    SwordTransform        computeBlock(float time) const;
    SwordTransform        computeIdle(float time)  const;

    static float smoothstep(float t) { return glm::smoothstep(0.0f, 1.0f, t); }
};

} // namespace XZSwordAnim