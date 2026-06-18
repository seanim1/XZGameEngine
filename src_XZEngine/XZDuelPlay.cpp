#include "XZSwordDuel.hpp"

#include <cstdlib>
#include <ctime>

namespace XZSwordDuel {

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────

Duel::Duel() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    buildCombos();
}

// ─────────────────────────────────────────────
//  Combo table construction
// ─────────────────────────────────────────────

void Duel::buildCombos() {
    // Combo 0 — balanced: equal windup and winddown
    combos_[0].sequence = {
        { AttackingState::WindingUp,     0.20f },
        { AttackingState::SendHitSignal, 0.00f },
        { AttackingState::WindingDown,   0.20f },
    };

    // Combo 1 — quick strike: fast windup, slow recovery
    combos_[1].sequence = {
        { AttackingState::WindingUp,     0.10f },
        { AttackingState::SendHitSignal, 0.00f },
        { AttackingState::WindingDown,   0.40f },
    };

    // Combo 2 — heavy: long windup, long winddown
    combos_[2].sequence = {
        { AttackingState::WindingUp,     0.40f },
        { AttackingState::SendHitSignal, 0.00f },
        { AttackingState::WindingDown,   0.35f },
    };
}

void Duel::pickCombo() {
    currentComboId_  = std::rand() % static_cast<int>(combos_.size());
    currentPhaseIdx_ = 0;
    enemyPhaseTimer_ = 0.0f;

    enemyState_    = EnemyState::Attacking;
    enemyAttState_ = combos_[currentComboId_].sequence[0].phase;
}

// ─────────────────────────────────────────────
//  Input setters
// ─────────────────────────────────────────────

void Duel::setPlayerAction(PlayerAction action) { currentAction_ = action; }
void Duel::setPlayerPosition(glm::vec2 pos)    { playerPos_ = pos; }
void Duel::setEnemyPosition(glm::vec2 pos)     { enemyPos_  = pos; }

// ─────────────────────────────────────────────
//  Main update
// ─────────────────────────────────────────────

void Duel::update(float deltaTime) {
    // Reset single-frame pulse flags
    parried_       = false;
    blocked_       = false;
    playerDamaged_ = false;
    enemyDamaged_  = false;

    updatePlayer(deltaTime);
    updateEnemy(deltaTime);
}

// ─────────────────────────────────────────────
//  Player state machine
// ─────────────────────────────────────────────

void Duel::updatePlayer(float deltaTime) {
    switch (playerState_) {

    // ── Idle ──────────────────────────────────
    case PlayerState::Idle:
        playerAttState_.reset();
        playerBlockState_.reset();

        if (currentAction_ == PlayerAction::Attack) {
            playerState_      = PlayerState::Attacking;
            playerAttState_   = AttackingState::WindingUp;
            playerPhaseTimer_ = kPlayerWindUp;
        }
        else if (currentAction_ == PlayerAction::Block) {
            playerState_      = PlayerState::Blocking;
            playerBlockState_ = BlockingState::Parrying;
            playerPhaseTimer_ = kParryWindow;
        }
        break;

    // ── Attacking ─────────────────────────────
    case PlayerState::Attacking:
        // Counter window: attack press during CounterAvailable fires the counter
        if (parriedState_.has_value() &&
            *parriedState_ == ParriedState::CounterAvailable &&
            currentAction_ == PlayerAction::Attack)
        {
            parriedState_  = ParriedState::SendCounteringSignal;
            enemyDamaged_  = true;
            playerState_   = PlayerState::Idle;
            playerAttState_.reset();
            break;
        }

        playerPhaseTimer_ -= deltaTime;

        if (*playerAttState_ == AttackingState::WindingUp) {
            if (playerPhaseTimer_ <= 0.0f) {
                playerAttState_   = AttackingState::SendHitSignal;
                playerPhaseTimer_ = kPlayerWindDown;
            }
        }
        else if (*playerAttState_ == AttackingState::SendHitSignal) {
            // Single-frame pulse: advance to WindingDown immediately
            playerAttState_ = AttackingState::WindingDown;
        }
        else if (*playerAttState_ == AttackingState::WindingDown) {
            if (playerPhaseTimer_ <= 0.0f) {
                playerState_    = PlayerState::Idle;
                playerAttState_.reset();
                parriedState_.reset();
            }
        }
        break;

    // ── Blocking ──────────────────────────────
    case PlayerState::Blocking:
        if (playerBlockState_ == BlockingState::Parrying) {
            playerPhaseTimer_ -= deltaTime;
            if (playerPhaseTimer_ <= 0.0f)
                playerBlockState_ = BlockingState::Blocking;
        }

        if (currentAction_ != PlayerAction::Block && !parriedState_.has_value()) {
            playerState_ = PlayerState::Idle;
            playerBlockState_.reset();
        }
        break;
    }

    // Clear SendCounteringSignal after its single-frame pulse
    if (parriedState_.has_value() &&
        *parriedState_ == ParriedState::SendCounteringSignal)
    {
        parriedState_.reset();
    }
}

// ─────────────────────────────────────────────
//  Enemy state machine
// ─────────────────────────────────────────────

void Duel::updateEnemy(float deltaTime) {
    switch (enemyState_) {

    case EnemyState::Idle:
        enemyAttState_.reset();
        enemyIdleTimer_ += deltaTime;
        if (enemyIdleTimer_ >= kEnemyIdleDuration) {
            enemyIdleTimer_ = 0.0f;
            pickCombo();
        }
        break;

    case EnemyState::Attacking:
        advanceEnemyCombo(deltaTime);
        break;
    }
}

void Duel::advanceEnemyCombo(float deltaTime) {
    const AttackCombo& combo = combos_[currentComboId_];
    const int          total = static_cast<int>(combo.sequence.size());

    if (currentPhaseIdx_ >= total) {
        enemyState_    = EnemyState::Idle;
        enemyAttState_.reset();
        return;
    }

    const AttackPhase& phase = combo.sequence[currentPhaseIdx_];
    enemyAttState_           = phase.phase;

    if (phase.phase == AttackingState::SendHitSignal) {
        // Single-frame pulse: resolve hit and immediately advance
        resolveHit();
        ++currentPhaseIdx_;
        enemyPhaseTimer_ = 0.0f;
        return;
    }

    enemyPhaseTimer_ += deltaTime;
    if (enemyPhaseTimer_ >= phase.duration) {
        enemyPhaseTimer_ = 0.0f;
        ++currentPhaseIdx_;
    }
}

// ─────────────────────────────────────────────
//  Hit resolution
// ─────────────────────────────────────────────

void Duel::resolveHit() {
    if (playerState_ == PlayerState::Blocking) {
        if (playerBlockState_ == BlockingState::Parrying) {
            parried_          = true;
            parriedState_     = ParriedState::CounterAvailable;
            playerState_      = PlayerState::Attacking;
            playerAttState_   = AttackingState::WindingDown;
            playerPhaseTimer_ = 0.0f;
        } else {
            blocked_ = true;
        }
    } else {
        playerDamaged_ = true;
    }
}

// ─────────────────────────────────────────────
//  run() — self-contained chrono tick
// ─────────────────────────────────────────────

void Duel::run() {
    auto now = std::chrono::steady_clock::now();

    if (!runTimerInitialized_) {
        lastTime_            = now;
        runTimerInitialized_ = true;
        return;
    }

    float deltaTime = std::chrono::duration<float>(now - lastTime_).count();
    lastTime_       = now;
    update(deltaTime);
}

// ─────────────────────────────────────────────
//  Getters
// ─────────────────────────────────────────────

glm::vec2   Duel::getPlayerPosition() const { return playerPos_;    }
glm::vec2   Duel::getEnemyPosition()  const { return enemyPos_;     }
PlayerState Duel::getPlayerState()    const { return playerState_;  }
EnemyState  Duel::getEnemyState()     const { return enemyState_;   }

std::optional<AttackingState> Duel::getPlayerAttackingState() const { return playerAttState_;   }
std::optional<BlockingState>  Duel::getPlayerBlockingState()  const { return playerBlockState_; }
std::optional<ParriedState>   Duel::getParriedState()         const { return parriedState_;     }
std::optional<AttackingState> Duel::getEnemyAttackingState()  const { return enemyAttState_;    }

bool Duel::isParried()       const { return parried_;       }
bool Duel::isBlocked()       const { return blocked_;       }
bool Duel::isPlayerDamaged() const { return playerDamaged_; }
bool Duel::isEnemyDamaged()  const { return enemyDamaged_;  }

} // namespace XZSwordDuel

// ─────────────────────────────────────────────
//  Attack duration queries
// ─────────────────────────────────────────────

float Duel::getPlayerAttackDuration() const {
    return kPlayerWindUp + kPlayerWindDown;
}

float Duel::getEnemyAttackDuration() const {
    const auto it = combos_.find(currentComboId_);
    if (it == combos_.end()) return 0.0f;

    float windUp   = 0.0f;
    float windDown = 0.0f;

    for (const AttackPhase& phase : it->second.sequence) {
        if (phase.phase == AttackingState::WindingUp)   windUp   = phase.duration;
        if (phase.phase == AttackingState::WindingDown) windDown = phase.duration;
    }

    return windUp + windDown;
}