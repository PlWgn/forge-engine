#pragma once
#include <forge/particles.hpp>
#include <functional>
namespace forge {
struct ParticleRenderStats {
    unsigned calls=0,quads=0;
    size_t uploadBytes=0;
    double sortMs=0;
};
// Owns only particle GPU buffers. The renderer supplies textures, program and budget.
struct ParticleRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit ParticleRenderer(bool instanced);
    ~ParticleRenderer();
    ParticleRenderStats draw(const std::vector<ParticleDraw>&, const glm::mat4 &view,
                            bool is3d, bool screen, unsigned layers, unsigned program,
                            unsigned white, const std::function<unsigned(const std::string&)> &texture,
                            const std::function<void(size_t)> &reserve);
};
}
