#pragma once

namespace XZDuelPlay {

enum class PlayerState {
    Idle,
    Damaged,
    Parry,
    Block,
    Attack
};

enum class EnemyState {
    Idle,
    Damaged,
    AttackPrep,
    Attack
};

class DuelPlay {
public:
    DuelPlay() = default;

    void onMouseDown() {
        m_mouse_held       = true;
        m_mouse_hold_timer = 0.0f;
        m_player_state     = PlayerState::Parry;
    }

    void onMouseUp() {
        m_mouse_held   = false;
        m_player_state = PlayerState::Idle;
    }

    void update(float delta_time) {
        // Player parry window
        if (m_mouse_held) {
            m_mouse_hold_timer += delta_time;
            if (m_mouse_hold_timer >= m_parry_window &&
                m_player_state == PlayerState::Parry) {
                m_player_state = PlayerState::Block;
            }
        }
        // Enemy state machine
        switch (m_enemy_state) {
            case EnemyState::AttackPrep:
                m_enemy_attack_timer += delta_time;
                if (m_enemy_attack_timer >= m_enemy_attack_prep_dur) {
                    m_enemy_attack_timer = 0.0f;
                    m_enemy_state = EnemyState::Attack;
                }
                break;

            case EnemyState::Attack:
                m_enemy_attack_timer += delta_time;
                if (m_enemy_attack_timer >= m_enemy_attack_dur) {
                    m_enemy_attack_timer = 0.0f;
                    m_enemy_state = EnemyState::Idle;
                }
                break;

            default:
                break;
        }
    }
    bool checkParry() {
        if (m_enemy_state == EnemyState::Attack &&
            m_player_state == PlayerState::Parry) {
            onParry();
            return true;
        }
        return false;
    }

    void onParry() {
        std::cout << "Parried" << std::endl;
    }
    PlayerState getPlayerState() const { return m_player_state; }
    EnemyState  getEnemyState()  const { return m_enemy_state;  }
    void setPlayerState(PlayerState state) { m_player_state = state; }
    void setEnemyState(EnemyState state)   { m_enemy_state  = state; }

private:
    PlayerState m_player_state = PlayerState::Idle;
    EnemyState  m_enemy_state  = EnemyState::Idle;

    float m_enemy_attack_timer    = 0.0f;
    float m_enemy_attack_prep_dur = 0.3f;
    float m_enemy_attack_dur      = 0.2f;

    bool  m_mouse_held       = false;
    float m_mouse_hold_timer = 0.0f;
    float m_parry_window     = 0.2f;
};

} // namespace XZDuelPlay