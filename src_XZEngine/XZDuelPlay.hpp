#pragma once
#include <iostream>

namespace XZDuelPlay {

enum class PlayerState {
    Idle,
    Damaged,
    Parry,
    CounterAvailable,
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
        if (m_player_state != PlayerState::CounterAvailable)
            m_player_state = PlayerState::Idle;
    }

    void update(float delta_time) {
        // CounterAvailable window
        if (m_player_state == PlayerState::CounterAvailable) {
            m_counter_timer += delta_time;
            if (m_counter_timer >= m_counter_window)
                m_player_state = m_mouse_held ? PlayerState::Block : PlayerState::Idle;
        }

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
        m_player_state  = PlayerState::CounterAvailable;
        m_counter_timer = 0.0f;
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

    float m_counter_timer  = 0.0f;
    float m_counter_window = 0.3f;
};

} // namespace XZDuelPlay