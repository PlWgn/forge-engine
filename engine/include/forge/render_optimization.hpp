#pragma once
// Adaptable rendering policy: no window, GPU, Python or game-specific dependencies.
#include <forge/types.hpp>
#include <array>
namespace forge {
struct Config;
struct Entity;
struct Model;
struct RenderBounds {
    glm::dvec3 min{0}, max{0};
    bool valid = false;
};
struct RenderLod {
    double distance = 0;
    std::string model, texture;
};
struct EntityOptimization {
    Json definition = Json::object();
    bool culling = true, lod = true;
    double maxDistance = 0, hysteresis = .1;
    RenderBounds bounds, occluder;
    std::vector<RenderLod> levels;
};
struct RenderOptimizationSettings {
    bool enabled = false, frustum = true, distance = true, lod = true, occlusion = false;
    unsigned gridWidth = 64, gridHeight = 36, maxOccluders = 128;
};
std::shared_ptr<const EntityOptimization> entityOptimization(const Config&, const Json&, const std::string& kind);
RenderOptimizationSettings renderOptimizationSettings(const Json&);
RenderBounds staticModelBounds(const Model&);
RenderBounds transformRenderBounds(const RenderBounds&, const glm::mat4&);
std::array<glm::dvec3,8> renderBoundsCorners(const RenderBounds&, const glm::mat4& = glm::mat4(1));

struct RenderOptimizationView {
    std::string id = "main";
    glm::mat4 clipFromWorld{1};
    glm::dvec3 position{0};
    bool is3d = true, shadow = false;
};
struct RenderOptimizationItem {
    std::shared_ptr<Entity> entity;
    RenderBounds bounds; // Conservative world AABB enclosing all available static LODs.
    std::array<glm::dvec3,8> occluder{}; // Explicit solid local box transformed into world space.
    bool opaque = false, hasOccluder = false, deforming = false;
};
enum class RenderCullReason { None, Frustum, Distance, Occlusion, Custom };
struct RenderOptimizationDecision {
    bool visible = true;
    size_t lod = 0; // 0: original representation; 1..N: authored levels.
    RenderCullReason reason = RenderCullReason::None;
};
struct RenderOptimizationResult {
    std::vector<RenderOptimizationDecision> decisions; // Same order/size as input.
    Json diagnostics = Json::object();
};
class RenderOptimizationPolicy {
public:
    virtual ~RenderOptimizationPolicy() = default;
    virtual void beginFrame() {}
    virtual void endFrame() {}
    virtual RenderOptimizationResult evaluate(const RenderOptimizationView&,
        const std::vector<RenderOptimizationItem>&, const RenderOptimizationSettings&) = 0;
};
class NativeRenderOptimizer : public RenderOptimizationPolicy {
public:
    NativeRenderOptimizer();
    ~NativeRenderOptimizer() override;
    void beginFrame() override;
    void endFrame() override;
    RenderOptimizationResult evaluate(const RenderOptimizationView&,
        const std::vector<RenderOptimizationItem>&, const RenderOptimizationSettings&) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace forge
