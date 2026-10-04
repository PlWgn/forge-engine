#pragma once
#include <forge/scene.hpp>
namespace pybind11 {class module_;}
namespace forge {
Json validatePhysics(Json);
Json validateRigidBody(Json);
void validatePhysicsEntity(const Entity &);
bool rigidPhysics(const World &);
struct Physics3D {
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit Physics3D(World &);
    ~Physics3D();
    void sync(World &);
    void step(World &, float);
    bool overlaps(World &, const Entity &, const Entity &);
    Json raycast(World &, glm::vec3, glm::vec3, float, unsigned, const std::string &, bool, bool synchronize=true);
    Json move(World &, Entity &, glm::vec3, float);
    Json info(World &, const Entity &);
    void wake(World &, Entity &);
    Json stats() const;
};
Physics3D &physics3D(World &);
// One frame of forces; callbacks can queue the next frame without losing them.
struct PhysicsForces {
    struct Value {std::shared_ptr<Entity> entity;glm::vec3 force,torque;};
    World &world;
    std::vector<Value> frame;
    explicit PhysicsForces(World &);
    void step(float);
};
void bindPhysics(pybind11::module_ &);
} // namespace forge
