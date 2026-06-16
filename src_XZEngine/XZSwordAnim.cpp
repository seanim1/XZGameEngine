#include "XZSwordAnim.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace XZSwordAnim {

// ─────────────────────────────────────────────
//  Clip switching
// ─────────────────────────────────────────────

void SwordAnim::switchClip(SwordAnimClip clip) {
    if (activeClip_ == clip) return;

    activeClip_ = clip;
    t_          = 0.0f;
    time_       = 0.0f;
    done_       = false;
}

// ─────────────────────────────────────────────
//  Public play functions
// ─────────────────────────────────────────────

SwordTransform SwordAnim::playSwordAttack(float deltaTime, float totalDuration) {
    switchClip(SwordAnimClip::Attack);

    if (!done_) {
        t_ += deltaTime / totalDuration;
        if (t_ >= 1.0f) {
            t_    = 1.0f;
            done_ = true;
        }
    }

    return computeAttack(smoothstep(t_));
}

SwordTransform SwordAnim::playSwordBlock(float deltaTime) {
    switchClip(SwordAnimClip::Block);
    time_ += deltaTime;
    return computeBlock(time_);
}

SwordTransform SwordAnim::playSwordIdle(float deltaTime) {
    switchClip(SwordAnimClip::Idle);
    time_ += deltaTime;
    return computeIdle(time_);
}

// ─────────────────────────────────────────────
//  State queries
// ─────────────────────────────────────────────

bool          SwordAnim::isAttackDone()   const { return done_;       }
SwordAnimClip SwordAnim::currentClip()    const { return activeClip_; }
float         SwordAnim::attackProgress() const {
    return (activeClip_ == SwordAnimClip::Attack) ? t_ : 0.0f;
}

// ─────────────────────────────────────────────
//  Attack arc  (t in [0, 1], smoothstepped before entry)
//
//  t [0.0, 0.5]  windup:  sword rises (+Y) and pulls back (-Z)
//  t [0.5, 1.0]  release: slashes forward (+Z), sweeps right (+X), dips (-Y)
//
//  Rotation mirrors the arc so the blade always faces the sweep direction.
// ─────────────────────────────────────────────

SwordTransform SwordAnim::computeAttack(float t) {
    SwordTransform out;

    if (t <= 0.5f) {
        const float p = t / 0.5f;  // [0, 1] within windup

        out.pos = glm::vec3(
             0.00f,
             glm::mix(0.00f,  0.35f, p),   // rise
             glm::mix(0.00f, -0.20f, p)    // pull back
        );

        out.rot = glm::vec3(
             glm::mix(  0.0f,  45.0f, p),  // pitch up
             glm::mix(  0.0f, -30.0f, p),  // yaw left (coiling)
             glm::mix(  0.0f, -15.0f, p)   // counter-roll
        );
    } else {
        const float p   = (t - 0.5f) / 0.5f;           // [0, 1] within release
        const float arc = glm::sin(p * glm::pi<float>()); // bell curve: 0→1→0

        out.pos = glm::vec3(
             glm::mix(0.00f,  0.45f, p),               // rightward sweep
             glm::mix(0.35f, -0.15f, p),               // dip down
             glm::mix(-0.20f, 0.30f, p) + arc * 0.10f  // forward thrust + belly
        );

        out.rot = glm::vec3(
             glm::mix( 45.0f, -30.0f, p),  // pitch down hard on slash
             glm::mix(-30.0f,  60.0f, p),  // yaw sweeps right
             glm::mix(-15.0f,  25.0f, p)   // roll follows the arc
        );
    }

    out.scale = 1.0f;
    return out;
}

// ─────────────────────────────────────────────
//  Block pose
//
//  Sword held diagonally up-forward with a slow
//  micro-sway on yaw and roll to feel alive.
// ─────────────────────────────────────────────

SwordTransform SwordAnim::computeBlock(float time) const {
    SwordTransform out;

    const float sway = glm::sin(time * blockSwaySpeed);

    out.pos = glm::vec3(0.10f, 0.40f, 0.15f);

    out.rot = glm::vec3(
         35.0f,                              // fixed upward pitch
         sway * blockSwayAmount,             // gentle left-right yaw
        -45.0f + sway * blockSwayAmount * 0.5f  // roll follows sway
    );

    out.scale = 1.0f;
    return out;
}

// ─────────────────────────────────────────────
//  Idle pose
//
//  Sword rests low at the side with a sinusoidal
//  vertical bob and gentle rotational sway.
// ─────────────────────────────────────────────

SwordTransform SwordAnim::computeIdle(float time) const {
    SwordTransform out;

    const float bob  = glm::sin(time * idleBobSpeed);
    const float sway = glm::sin(time * idleBobSpeed * 0.6f);

    out.pos = glm::vec3(
         0.20f,
        -0.10f + bob * idleBobAmount,
         0.05f
    );

    out.rot = glm::vec3(
        -10.0f + sway * 3.0f,
         15.0f + bob  * 2.0f,
        -20.0f + sway * 2.5f
    );

    out.scale = 1.0f;
    return out;
}

} // namespace XZSwordAnim