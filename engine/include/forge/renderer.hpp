#pragma once
// Contract extracted from engine.hpp; licensed core origin.
#include <forge/scene.hpp>
namespace forge {struct Config;}
namespace forge {
struct Runtime;
class RenderOptimizationPolicy;
struct Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl, staged;
    Renderer();
    ~Renderer();
    void init(const Config &, World &);
    void init(const Config &, World &, Runtime &);
    void render(World &);
    void finish();
    void setOptimizationPolicy(std::shared_ptr<RenderOptimizationPolicy>); // nullptr restores native policy
    void validateWorld(const World &);
    void invalidate();
    void stage();
    void commit();
    void discard();
    void checkpointInput();
    void rollbackInput();
    void poll();
    bool closing() const;
    bool key(const std::string &) const;
    bool pressed(const std::string &) const;
    bool mouse(int button) const;
    glm::vec2 cursor() const;
    glm::vec2 scroll() const;
    glm::vec2 delta() const;
    void capture(bool);
    void quit();
    void screenshot(const fs::path &path);
    Json input() const;
    Json diagnostics() const;
    void windowOptions(const Json &);
    Json windowOptions() const;
    void editor(bool enabled);
    bool previewing() const;
    void select(const std::string&);
};
}
