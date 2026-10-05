#pragma once
#include <functional>
#include <forge/types.hpp>
#include <unordered_map>
namespace forge {
struct Config;
struct Physics3D;
struct Particles;
struct Geometry;
struct TransformCache;
struct AnimatorState;
struct Entity {
    std::string id, name, kind = "sprite", model, texture, material, text, textKey;
    std::string parent;
    glm::mat4 worldMatrix{1};
    glm::vec3 worldRotation{0};
    Json materialData = Json::object();
    Json textParams = Json::object();
    glm::vec3 position{0}, rotation{0}, scale{1}, velocity{0}, collider{0};
    glm::vec3 angularVelocity{0}, force{0}, torque{0};
    Json rigidBody = Json::object();
    glm::vec4 color{1}, clip{0};
    glm::vec4 uv{0, 0, 1, 1};
    unsigned layer = 1;
    bool castsShadow = true;
    Json uniforms = Json::object();
    std::string animation;
    Json animatorSettings=Json::object(),morphWeights=Json::object();
    std::shared_ptr<AnimatorState> animator;
    double animationTime = 0;
    float animationSpeed = 1;
    bool animationLoop = true, animationPlaying = false;
    bool clipped = false;
    bool attached = false;
    bool visible = true, alive = true, dynamic = false, trigger = false, screen = false;
    float mass = 1, fontSize = 24;
    Json scripts = Json::array();
    Json data = Json::object();
    Json source = Json::object(); // Preserve shell/extension fields across runtime snapshots.
};
glm::mat4 composeTransform(const Entity &);
struct World {
    // Optional host preparation; independent Worlds need no Python runtime.
    std::function<void(Entity&)> prepareEntity;
    Config *config = nullptr;
    std::vector<std::shared_ptr<Entity>> entities;
    std::unordered_map<std::string, std::weak_ptr<Entity>> entityIndex;
    std::shared_ptr<Geometry> geometry;
    std::shared_ptr<TransformCache> transformCache;
    size_t transformAudits=0,transformComputations=0;
    size_t physicsCandidates = 0;
    bool assembling = false;
    Json scene;
    Json physicsSettings = Json::object();
    std::shared_ptr<Physics3D> physics3d;
    std::shared_ptr<Particles> particles;
    unsigned nextId = 0;
    glm::vec3 cameraPosition{0, 0, 5}, cameraTarget{0}, gravity{0, -9.81f, 0};
    bool is3d = false;
    bool physicsEnabled = true;
    Json renderSettings = Json::object();
    float fov = 60;
    int width = 1280, height = 720;
    glm::vec4 background{0.025f, 0.04f, 0.075f, 1};
    std::shared_ptr<Entity> spawn(Json data);
    void syncTransforms();
    void setLocalTransform(Entity &, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);
    void setPositions(const std::vector<std::pair<std::shared_ptr<Entity>,glm::vec3>>&);
    void loadEntities(const Json &);
    void clearEntities();
    void pruneIndex();
    void destroy(Entity &, bool children = true);
    void reparent(Entity &, const std::string &, bool keepWorld = false);
    void setWorldPosition(Entity &, glm::vec3);
    glm::vec3 worldPosition(const Entity &) const;
    std::vector<std::shared_ptr<Entity>> children(const Entity &, bool recursive = false);
    void configureSimulation(const Json &);
    std::shared_ptr<Entity> find(const std::string &id);
    void load(const std::string &scenePath);
    void loadDocument(const Json &);
    std::set<std::pair<std::string, std::string>> contacts;
    void physics(float dt);
    bool activeCollider(const Entity &) const;
    bool overlaps(const Entity &a, const Entity &b) const;
    std::shared_ptr<Entity> raycast(glm::vec3 origin, glm::vec3 direction, float distance, bool synchronize=true);
    Json moveCharacter(Entity &, glm::vec3 delta, float skin = .001f);
    Json serialize() const;
};
}
