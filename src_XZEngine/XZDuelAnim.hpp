#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

glm::mat4 getRotationMatrix(const glm::vec3& rotation) {
    glm::mat4 rotMatrix = glm::mat4(1.0f);
    rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    return rotMatrix;
}

const glm::vec3 getRight(const glm::vec3& rotation) {
    return glm::normalize(glm::vec3(getRotationMatrix(rotation)[0]));
}

const glm::vec3 getUp(const glm::vec3& rotation) {
    return glm::normalize(glm::vec3(getRotationMatrix(rotation)[1]));
}

const glm::vec3 getForward(const glm::vec3& rotation) {
    return -glm::normalize(glm::vec3(getRotationMatrix(rotation)[2]));
}

namespace XZDuelAnim {

enum class EnemySwordState {
    Idle,
    Attacking
};

enum class EnemyBodyState {
    Idle,
    Damaged
};

enum class PlayerSwordState {
    Idle,
    Blocking
};

enum class PlayerBodyState {
    Idle,
    Damaged
};

struct Transformation {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
};

class SwordAnim {
public:
    SwordAnim(glm::vec3 default_rotation = {90.0f, 0.0f, 0.0f},
              glm::vec3 default_scale    = {0.3f, 0.3f, 0.3f})
    {
        m_enemy_sword_trans.position = {0.0f, 0.0f, 0.0f};
        m_enemy_sword_trans.rotation = default_rotation;
        m_enemy_sword_trans.scale    = default_scale;
        m_player_sword_trans.position = {0.0f, 0.0f, 0.0f};
        m_player_sword_trans.rotation = default_rotation;
        m_player_sword_trans.scale    = default_scale;

    }
    template<typename StateType>
    void setState(StateType state) {
        if constexpr (std::is_same_v<StateType, EnemySwordState>)
            m_enemy_sword_state = state;
        else if constexpr (std::is_same_v<StateType, EnemyBodyState>)
            m_enemy_body_state = state;
        else if constexpr (std::is_same_v<StateType, PlayerSwordState>)
            m_player_sword_state = state;
        else if constexpr (std::is_same_v<StateType, PlayerBodyState>)
            m_player_body_state = state;
    }

    template<typename StateType>
    StateType getState() const {
        if constexpr (std::is_same_v<StateType, EnemySwordState>)
            return m_enemy_sword_state;
        else if constexpr (std::is_same_v<StateType, EnemyBodyState>)
            return m_enemy_body_state;
        else if constexpr (std::is_same_v<StateType, PlayerSwordState>)
            return m_player_sword_state;
        else if constexpr (std::is_same_v<StateType, PlayerBodyState>)
            return m_player_body_state;
    }

    Transformation getEnemySwordTransformation(glm::vec3 centerPosition, glm::vec3 centerRotation, float deltaTime) {
        m_enemy_sword_trans.position = centerPosition;
        m_enemy_sword_trans.rotation = centerRotation;
        if (m_enemy_sword_state == EnemySwordState::Attacking) {
            static float EnemySwordAttackDuration = 1.0f;
            constexpr float animFreq = 8.0f;
            glm::vec3 pos_offset = { glm::sin(animFreq * m_enemyAnimPlaytimeSec), 0.0f, glm::cos(animFreq * m_enemyAnimPlaytimeSec)};
            m_enemy_sword_trans.position = centerPosition + 2.0f * pos_offset;

            float angleRadians = std::atan2(pos_offset.x, pos_offset.z);
            float angleDegrees = glm::degrees(angleRadians);
            m_enemy_sword_trans.rotation = centerRotation + glm::vec3(0.0f, angleDegrees + 90.f, 0.0f);
        } else {
            m_enemy_sword_trans.position = centerPosition + 2.0f * getRight(centerRotation);
            m_enemy_sword_trans.position.y = 0.1f * glm::cos(7.0f * m_enemyAnimPlaytimeSec);
            m_enemy_sword_trans.rotation = {0.0f, 90.0f, 270.0f};
        }
        m_enemyAnimPlaytimeSec += deltaTime;
        return m_enemy_sword_trans;
    }
    Transformation getPlayerSwordTransformation(glm::vec3 centerPosition, glm::vec3 centerRotation, float deltaTime) {
        m_player_sword_trans.position = centerPosition;
        m_player_sword_trans.rotation = centerRotation;
        if (m_player_sword_state == PlayerSwordState::Blocking) {
            m_player_sword_trans.position = centerPosition + 1.0f * getForward(centerRotation) + 0.5f * getRight(centerRotation);
            m_player_sword_trans.rotation = centerRotation + glm::vec3(90.0f, 60.f, 0.0f);
        } else {
            m_player_sword_trans.position = centerPosition + 2.0f * getRight(centerRotation);
            m_player_sword_trans.position.y = 0.1f * glm::cos(7.0f * m_playerAnimPlaytimeSec);
            m_player_sword_trans.rotation = {0.0f, 90.0f, 270.0f};
        }
        m_playerAnimPlaytimeSec += deltaTime;
        return m_player_sword_trans;
    }

private:
    EnemySwordState  m_enemy_sword_state  = EnemySwordState::Idle;
    EnemyBodyState   m_enemy_body_state   = EnemyBodyState::Idle;
    PlayerSwordState m_player_sword_state = PlayerSwordState::Idle;
    PlayerBodyState  m_player_body_state  = PlayerBodyState::Idle;
    Transformation m_enemy_sword_trans;
    Transformation m_player_sword_trans;
    float m_enemyAnimPlaytimeSec = 0.0f;
    float m_playerAnimPlaytimeSec = 0.0f;
};

} // namespace XZDuelAnim