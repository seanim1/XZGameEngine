#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdlib>
#include <cmath>

namespace XZParticleSystem {

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float     lifetime;       // seconds remaining
    float     max_lifetime;   // total lifetime
    bool      alive = false;
};

struct SparkConfig {
    int   count          = 30;
    float speed          = 6.0f;   // initial speed
    float lifetime       = 1.4f;   // seconds
    float gravity        = -9.8f;  // downward acceleration
    float spread         = 6.0f;   // how wide the cone of sparks is
};

class ParticleSystem {
public:
    explicit ParticleSystem(int max_particles = 500) {
        m_particles.resize(max_particles);
    }

    void emitSparks(const glm::vec3& emit_position,
                    const SparkConfig& config = SparkConfig{})
    {
        int emitted = 0;
        for (Particle& particle : m_particles) {
            if (emitted >= config.count) break;
            if (particle.alive) continue;

            particle.alive        = true;
            particle.position     = emit_position;
            particle.lifetime     = config.lifetime;
            particle.max_lifetime = config.lifetime;

            // Random direction in a sphere
            float theta = ((float)rand() / RAND_MAX) * 2.0f * 3.14159f;
            float phi   = ((float)rand() / RAND_MAX) * 3.14159f;
            float speed = config.speed * (0.5f + 0.5f * ((float)rand() / RAND_MAX));

            glm::vec3 direction = {
                glm::sin(phi) * glm::cos(theta) * config.spread,
                glm::sin(phi) * glm::sin(theta),
                glm::cos(phi) * config.spread
            };
            particle.velocity = glm::normalize(direction) * speed;

            emitted++;
        }
    }

    void update(float delta_time) {
        for (Particle& particle : m_particles) {
            if (!particle.alive) continue;

            particle.lifetime -= delta_time;
            if (particle.lifetime <= 0.0f) {
                particle.alive = false;
                continue;
            }

            // Apply gravity
            particle.velocity.y += -9.8f * delta_time;

            // Move
            particle.position += particle.velocity * delta_time;
        }
    }

    const std::vector<Particle>& getParticles() const { return m_particles; }

    int aliveCount() const {
        int count = 0;
        for (const Particle& p : m_particles)
            if (p.alive) count++;
        return count;
    }

private:
    std::vector<Particle> m_particles;
};

} // namespace XZParticleSystem