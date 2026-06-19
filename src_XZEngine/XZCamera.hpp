#pragma once
#include <glm/glm.hpp>
#include <cstdlib>

namespace XZCamera {

class Camera {
public:
    Camera(glm::vec3 position, glm::vec3 target)
        : m_base_position(position)
        , m_base_target(target)
    {}

    void setBasePosition(const glm::vec3& pos)    { m_base_position = pos; }
    void setBaseTarget(const glm::vec3& target)   { m_base_target   = target; }

    void triggerShake(float duration = 0.3f, float intensity = 0.15f) {
        m_shake_duration  = duration;
        m_shake_intensity = intensity;
        m_shake_timer     = 0.0f;
    }

    void update(float delta_time) {
        if (m_shake_timer < m_shake_duration)
            m_shake_timer += delta_time;
    }

    glm::vec3 getPosition() const {
        return m_base_position + computeShakeOffset();
    }

    glm::vec3 getTarget() const { return m_base_target; }

    bool isShaking() const { return m_shake_timer < m_shake_duration; }

private:
    glm::vec3 m_base_position;
    glm::vec3 m_base_target;

    float m_shake_duration  = 0.0f;
    float m_shake_intensity = 0.0f;
    float m_shake_timer     = 0.0f;

    glm::vec3 computeShakeOffset() const {
        if (m_shake_timer >= m_shake_duration) return {0.0f, 0.0f, 0.0f};
        float t = 1.0f - (m_shake_timer / m_shake_duration);  // fades out
        return glm::vec3(
            ((float)rand() / RAND_MAX * 2.0f - 1.0f) * m_shake_intensity * t,
            ((float)rand() / RAND_MAX * 2.0f - 1.0f) * m_shake_intensity * t,
            0.0f
        );
    }
};

} // namespace XZCamera