#pragma once
#include <forge/engine.hpp>
#include <random>
namespace forge {
Json validateEmitter(const Config &, Json);
void validateEmitters(const Config &, const Json &);
struct Particle {
    glm::vec3 position{0}, velocity{0};
    float age = 0, lifetime = 1, angle = 0, sizeFactor = 1;
};
struct ParticleEmitter {
    unsigned id = 0;
    Json settings;
    std::mt19937 random;
    std::vector<Particle> live;
    glm::vec3 origin{0};
    double carry = 0, elapsed = 0;
    size_t dropped = 0;
};
struct ParticleDraw {
    glm::vec3 position;
    glm::vec4 color, uv;
    float size, angle;
    std::string texture;
    unsigned layer;
    bool additive, screen;
};
struct Particles {
    std::vector<ParticleEmitter> emitters;
    unsigned create(const Config &, Json);
    ParticleEmitter &find(unsigned);
    void update(World &, float);
    size_t burst(World &, unsigned, unsigned);
    void remove(unsigned);
    std::vector<ParticleDraw> draw(const World &) const;
    Json serialize() const;
    Json stats() const;
    static constexpr size_t maximum = 100000;
};
Particles &particleSystem(World &);
void bindParticles(py::module_ &);
} // namespace forge
