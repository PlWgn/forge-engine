#include <forge/engine.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <forge/physics.hpp>
#include <btBulletDynamicsCommon.h>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/quaternion.hpp>
#include <pybind11/stl.h>
#include <algorithm>
#include <cmath>
namespace forge {
namespace {
float range(const Json &j, const std::string &field, double low, double high) {
    auto value = finiteNumber(j, field);
    if (value < float(low) || value > float(high)) throw std::runtime_error(field + " outside allowed range");
    return value;
}
unsigned integer(const Json &j, const std::string &field, unsigned low, unsigned high) {
    if (!j.is_number_integer() || j.get<double>() < low || j.get<double>() > high)
        throw std::runtime_error(field + " must be a bounded integer");
    return j.get<unsigned>();
}
btVector3 vector(glm::vec3 v) { return {v.x, v.y, v.z}; }
glm::vec3 vector(const btVector3 &v, const std::string &field, double limit = 1e6) {
    return {range(v.x(), field, -limit, limit), range(v.y(), field, -limit, limit), range(v.z(), field, -limit, limit)};
}
glm::vec3 vector(const Json &j, const std::string &field, double limit = 1e6) {
    if (!j.is_array() || j.size() != 3) throw std::runtime_error(field + " needs three components");
    return {range(j[0], field, -limit, limit), range(j[1], field, -limit, limit), range(j[2], field, -limit, limit)};
}
Entity bodyPose(const World& world,const Entity& e){auto pose=e;pose.position=world.worldPosition(e);pose.rotation=e.worldRotation;return pose;}
btTransform transform(const Entity &e) {
    auto r = glm::radians(glm::dvec3(e.rotation));
    auto q = glm::angleAxis(r.x, glm::dvec3(1, 0, 0)) * glm::angleAxis(r.y, glm::dvec3(0, 1, 0)) * glm::angleAxis(r.z, glm::dvec3(0, 0, 1));
    return btTransform(btQuaternion(q.x, q.y, q.z, q.w), vector(e.position));
}
glm::vec3 rotation(const btTransform &t) {
    auto q = t.getRotation();
    auto matrix = glm::mat4_cast(glm::dquat(q.w(), q.x(), q.y(), q.z()));
    double x, y, z;
    glm::extractEulerAngleXYZ(matrix, x, y, z);
    auto angles=glm::degrees(glm::dvec3(x,y,z));
    return {checkedFloat(angles.x,"physics rotation"),checkedFloat(angles.y,"physics rotation"),checkedFloat(angles.z,"physics rotation")};
}
std::unique_ptr<btCollisionShape> shape(const Entity &e) {
    auto type = e.rigidBody.value("shape", "box");
    double radius = std::min(double(e.collider.x), double(e.collider.z)) * .5;
    if (type == "sphere") return std::make_unique<btSphereShape>(std::min(radius, double(e.collider.y) * .5));
    if (type == "capsule") return std::make_unique<btCapsuleShape>(radius, std::max(0.0, double(e.collider.y) - radius * 2));
    auto result = std::make_unique<btBoxShape>(vector(e.collider) * .5);
    result->setMargin(std::min(.01, double(std::min({e.collider.x, e.collider.y, e.collider.z})) * .05));
    return result;
}
bool masks(const Entity &a, const Entity &b) {
    auto groupA = a.rigidBody.value("group", 1u), groupB = b.rigidBody.value("group", 1u);
    return (groupA & b.rigidBody.value("mask", 65535u)) && (groupB & a.rigidBody.value("mask", 65535u));
}
struct Touch : btCollisionWorld::ContactResultCallback {
    bool touching = false;
    btScalar addSingleResult(btManifoldPoint &p, const btCollisionObjectWrapper *, int, int,
                             const btCollisionObjectWrapper *, int, int) override {
        if (p.getDistance() <= 0.00001) touching = true;
        return 0;
    }
};
Runtime &runtime() {
    if (!active) throw std::runtime_error("Physics runtime is not active");
    return *active;
}
void current(Entity &e) {
    auto entity = runtime().world.find(e.id);
    if (!entity || entity.get() != &e) throw std::runtime_error("Physics entity is not in the current scene");
}
py::object python(const Json &j) { return pythonValue(j); }
} // namespace
Json validatePhysics(Json settings) {
    if (!settings.is_object()) throw std::runtime_error("physics must be an object");
    auto backend = settings.value("backend", "legacy");
    if (backend != "legacy" && backend != "bullet") throw std::runtime_error("physics.backend must be legacy or bullet");
    integer(settings.value("iterations", Json(20)), "physics.iterations", 1, 100);
    integer(settings.value("max_bodies", Json(10000)), "physics.max_bodies", 1, 100000);
    settings["backend"] = backend;
    return settings;
}
Json validateRigidBody(Json settings) {
    if (!settings.is_object()) throw std::runtime_error("rigid_body must be an object");
    auto type = settings.value("shape", "box");
    if (type != "box" && type != "sphere" && type != "capsule") throw std::runtime_error("rigid_body.shape must be box, sphere or capsule");
    for (auto field : {"kinematic", "sleep", "ccd"})
        if (settings.contains(field) && !settings[field].is_boolean()) throw std::runtime_error(std::string("rigid_body.") + field + " must be boolean");
    for (auto field : {"friction", "rolling_friction", "spinning_friction"})
        range(settings.value(field, Json(std::string(field) == "friction" ? .5 : 0)), std::string("rigid_body.") + field, 0, 10);
    for (auto field : {"restitution", "linear_damping", "angular_damping"})
        range(settings.value(field, Json(0)), std::string("rigid_body.") + field, 0, 1);
    range(settings.value("max_slope", Json(45)), "rigid_body.max_slope", 0, 89);
    integer(settings.value("group", Json(1)), "rigid_body.group", 1, 65535);
    integer(settings.value("mask", Json(65535)), "rigid_body.mask", 0, 65535);
    return settings;
}
void validatePhysicsEntity(const Entity &e) {
    auto context = "Physics entity '" + e.id + "' ";
    validateRigidBody(e.rigidBody);
    if (e.dynamic && e.rigidBody.value("kinematic", false)) throw std::runtime_error(context + "cannot be dynamic and kinematic");
    range(e.mass, context + "mass", 1e-6, 1e6);
    for (int i = 0; i < 3; ++i) {
        range(e.position[i], context + "position", -1e6, 1e6);
        range(e.velocity[i], context + "velocity", -1e6, 1e6);
        range(e.rotation[i], context + "rotation", -1e7, 1e7);
        range(e.angularVelocity[i], context + "angular_velocity", -1e4, 1e4);
        range(e.force[i], context + "force", -1e6, 1e6);
        range(e.torque[i], context + "torque", -1e6, 1e6);
        range(e.collider[i], context + "collider", .0001, 10000);
    }
    if (e.rigidBody.value("shape", "box") == "capsule" && e.collider.y < std::min(e.collider.x, e.collider.z))
        throw std::runtime_error(context + "capsule height must be at least its diameter");
}
bool rigidPhysics(const World &world) { return world.is3d && world.physicsSettings.value("backend", "legacy") == "bullet"; }
struct Physics3D::Impl {
    struct Body {
        std::shared_ptr<Entity> entity;
        std::unique_ptr<btCollisionShape> shape;
        std::unique_ptr<btRigidBody> body;
        glm::vec3 collider;
        float mass=1;
        bool dynamic=false,trigger=false;
        Json settings;
        bool sameShape(const Entity& e) const {return collider==e.collider && mass==e.mass && dynamic==e.dynamic && trigger==e.trigger && settings==e.rigidBody;}

        glm::vec3 position, rotation, velocity, angular, force, torque;
    };
    btDefaultCollisionConfiguration configuration;
    btCollisionDispatcher dispatcher{&configuration};
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world{&dispatcher, &broadphase, &solver, &configuration};
    std::map<std::string, Body> bodies;
    size_t syncAudits=0,bodySynchronizations=0;
    ~Impl() { for (auto &[id, record] : bodies) world.removeRigidBody(record.body.get()); }
};
Physics3D::Physics3D(World &) : impl(std::make_unique<Impl>()) {}
Physics3D::~Physics3D() = default;
Physics3D &physics3D(World &world) {
    if (!rigidPhysics(world)) throw std::runtime_error("Enable physics.backend = bullet in a 3D scene");
    if (!world.physics3d) world.physics3d = std::make_shared<Physics3D>(world);
    return *world.physics3d;
}
void Physics3D::sync(World &world) {
    world.syncTransforms();
    ++impl->syncAudits;
    size_t count = 0;
    // Validate the entire input before changing Bullet objects or broadphase membership.
    for (auto &e : world.entities) if (world.activeCollider(*e)) {
        auto found=impl->bodies.find(e->id);
        if(found==impl->bodies.end() || found->second.entity!=e || !found->second.sameShape(*e) ||
           found->second.position!=world.worldPosition(*e) || found->second.rotation!=e->worldRotation ||
           found->second.velocity!=e->velocity || found->second.angular!=e->angularVelocity ||
           found->second.force!=e->force || found->second.torque!=e->torque)
            validatePhysicsEntity(bodyPose(world,*e));
        ++count;
    }
    if (count > world.physicsSettings.value("max_bodies", 10000u)) throw std::runtime_error("physics.max_bodies exceeded");
    for(int axis=0;axis<3;++axis)range(world.gravity[axis],"physics gravity",-1e6,1e6);
    auto gravity = vector(world.gravity);
    if (gravity != impl->world.getGravity())
        for (auto &[id, record] : impl->bodies) if (record.entity->dynamic) record.body->activate(true);
    impl->world.setGravity(gravity);
    impl->world.getSolverInfo().m_numIterations = world.physicsSettings.value("iterations", 20);
    for (auto it = impl->bodies.begin(); it != impl->bodies.end();) {
        auto entity = world.find(it->first);
        if (!entity || entity != it->second.entity || !world.activeCollider(*entity) || !it->second.sameShape(*entity)) {
            impl->world.removeRigidBody(it->second.body.get());
            it = impl->bodies.erase(it);
        } else ++it;
    }
    for (auto &entity : world.entities) if (world.activeCollider(*entity)) {
        auto &e = *entity;
        auto it = impl->bodies.find(e.id);
        bool created = it == impl->bodies.end();
        if(!created && it->second.position==world.worldPosition(e) && it->second.rotation==e.worldRotation &&
           it->second.velocity==e.velocity && it->second.angular==e.angularVelocity){
            it->second.force=e.force;it->second.torque=e.torque;continue;
        }
        ++impl->bodySynchronizations;
        if (created) {
            Impl::Body record;
            record.entity=entity;record.collider=e.collider;record.mass=e.mass;record.dynamic=e.dynamic;record.trigger=e.trigger;record.settings=e.rigidBody;record.shape=shape(e);
            btVector3 inertia(0, 0, 0);
            btScalar mass = e.dynamic ? e.mass : 0;
            if (mass) record.shape->calculateLocalInertia(mass, inertia);
            btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr, record.shape.get(), inertia);
            info.m_friction = e.rigidBody.value("friction", .5);
            info.m_restitution = e.rigidBody.value("restitution", 0.0);
            info.m_linearDamping = e.rigidBody.value("linear_damping", 0.0);
            info.m_angularDamping = e.rigidBody.value("angular_damping", 0.0);
            record.body = std::make_unique<btRigidBody>(info);
            record.body->setUserPointer(&e);
            record.body->setRollingFriction(e.rigidBody.value("rolling_friction", 0.0));
            record.body->setSpinningFriction(e.rigidBody.value("spinning_friction", 0.0));
            if (e.trigger) record.body->setCollisionFlags(record.body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
            if (e.rigidBody.value("kinematic", false)) record.body->setCollisionFlags((record.body->getCollisionFlags() & ~btCollisionObject::CF_STATIC_OBJECT) | btCollisionObject::CF_KINEMATIC_OBJECT);
            if (!e.rigidBody.value("sleep", true) || e.rigidBody.value("kinematic", false)) record.body->setActivationState(DISABLE_DEACTIVATION);
            if (e.dynamic && e.rigidBody.value("ccd", true)) {
                double radius = std::min({e.collider.x, e.collider.y, e.collider.z}) * .25;
                record.body->setCcdMotionThreshold(radius);
                record.body->setCcdSweptSphereRadius(radius);
            }
            // Initialize transforms before insertion, so the broadphase sees the right bounds.
            record.body->setWorldTransform(transform(bodyPose(world,e)));
            impl->world.addRigidBody(record.body.get(), int(e.rigidBody.value("group", 1u)), int(e.rigidBody.value("mask", 65535u)));
            it = impl->bodies.emplace(e.id, std::move(record)).first;
        }
        auto &record = it->second;
        if (created || world.worldPosition(e) != record.position || e.worldRotation != record.rotation) {
            auto next = transform(bodyPose(world,e));
            record.body->setWorldTransform(next);
            if(created || !e.rigidBody.value("kinematic",false))record.body->setInterpolationWorldTransform(next);
            record.body->activate(true); impl->world.updateSingleAabb(record.body.get());
        }
        if (created || e.velocity != record.velocity) { record.body->setLinearVelocity(vector(e.velocity)); record.body->activate(true); }
        if (created || e.angularVelocity != record.angular) { record.body->setAngularVelocity(vector(e.angularVelocity)); record.body->activate(true); }
        record.position = world.worldPosition(e); record.rotation = e.worldRotation; record.velocity = e.velocity; record.angular = e.angularVelocity;record.force=e.force;record.torque=e.torque;
    }
}
void Physics3D::step(World &world, float dt) {
    range(dt, "physics dt", 0, 1);
    sync(world);
    for (int axis = 0; axis < 3; ++axis) range(world.gravity[axis], "physics gravity", -1e6, 1e6);
    struct Next { Entity *e; glm::vec3 position, rotation, velocity, angular; };
    int steps = std::max(1, int(std::ceil(double(dt) / double(1.f / 120))));
    double h = double(dt) / steps;
    for (int i = 0; i < steps; ++i) {
        for (auto &[id, record] : impl->bodies) if (h>0 && record.entity->dynamic) {
            auto &e = *record.entity;
            if (glm::length(glm::dvec3(e.force)) > 0 || glm::length(glm::dvec3(e.torque)) > 0) record.body->activate(true);
            record.body->applyCentralForce(vector(e.force)); record.body->applyTorque(vector(e.torque));
        }
        if (h > 0) impl->world.stepSimulation(h, 0, h);
    }
    if(dt==0)impl->world.performDiscreteCollisionDetection();
    std::vector<Next> pending;
    for (auto &[id, record] : impl->bodies) if (record.entity->dynamic) {
        auto &e = *record.entity;
        auto context = "Physics entity '" + id + "' ";
        pending.push_back({&e, vector(record.body->getWorldTransform().getOrigin(), context + "position"), rotation(record.body->getWorldTransform()),
                           vector(record.body->getLinearVelocity(), context + "velocity"), vector(record.body->getAngularVelocity(), context + "angular_velocity", 1e4)});
    }
    // Objects without a collider still retain the established free-fall behavior.
    for (auto &pointer : world.entities) if (pointer->alive && pointer->dynamic && !world.activeCollider(*pointer)) {
        auto &e = *pointer;
        auto velocity = glm::dvec3(e.velocity) + (glm::dvec3(world.gravity) + glm::dvec3(e.force) / double(checkedMass(e.mass))) * double(dt);
        auto position = glm::dvec3(e.position) + velocity * double(dt);
        pending.push_back({&e, vector(Json::array({position.x, position.y, position.z}), "physics position"), e.rotation,
                           vector(Json::array({velocity.x, velocity.y, velocity.z}), "physics velocity"), e.angularVelocity});
    }
    std::set<std::pair<std::string, std::string>> contacts;
    for (int i = 0; i < impl->dispatcher.getNumManifolds(); ++i) {
        auto *manifold = impl->dispatcher.getManifoldByIndexInternal(i);
        auto *a = static_cast<Entity *>(manifold->getBody0()->getUserPointer()), *b = static_cast<Entity *>(manifold->getBody1()->getUserPointer());
        if (!a || !b) continue;
        for (int k = 0; k < manifold->getNumContacts(); ++k) if (manifold->getContactPoint(k).getDistance() <= .0001) { contacts.insert(std::minmax(a->id, b->id)); break; }
    }
    // Static/kinematic trigger pairs can be skipped by Bullet's dynamics dispatcher.
    for (auto &[id, a] : impl->bodies) if (a.entity->trigger) {
        struct TriggerContacts : btCollisionWorld::ContactResultCallback {
            const Entity* self;std::set<std::pair<std::string,std::string>>& contacts;
            TriggerContacts(const Entity* e,std::set<std::pair<std::string,std::string>>& c):self(e),contacts(c){
                m_collisionFilterGroup=int(e->rigidBody.value("group",1u));m_collisionFilterMask=int(e->rigidBody.value("mask",65535u));
            }
            bool needsCollision(btBroadphaseProxy* proxy)const override {
                auto* object=static_cast<btCollisionObject*>(proxy->m_clientObject);auto* e=static_cast<Entity*>(object->getUserPointer());
                return e && e!=self && masks(*self,*e);
            }
            btScalar addSingleResult(btManifoldPoint& point,const btCollisionObjectWrapper* a,int,int,const btCollisionObjectWrapper* b,int,int)override {
                auto* first=static_cast<Entity*>(a->getCollisionObject()->getUserPointer());auto* second=static_cast<Entity*>(b->getCollisionObject()->getUserPointer());
                if(first && second && point.getDistance()<=.00001)contacts.insert(std::minmax(first->id,second->id));
                return 0;
            }
        } callback(a.entity.get(),contacts);
        impl->world.contactTest(a.body.get(),callback);
    }
    for (const auto &next : pending) {
        next.e->position = next.position; next.e->rotation = next.rotation; next.e->velocity = next.velocity; next.e->angularVelocity = next.angular;
        auto it = impl->bodies.find(next.e->id);
        if (it != impl->bodies.end()) { it->second.position = next.position; it->second.rotation = next.rotation; it->second.velocity = next.velocity; it->second.angular = next.angular; }
    }
    world.contacts = std::move(contacts);
}
bool Physics3D::overlaps(World &world, const Entity &a, const Entity &b) {
    if (&a == &b || !world.activeCollider(a) || !world.activeCollider(b) || !masks(a, b)) return false;
    sync(world);
    auto ia = impl->bodies.find(a.id), ib = impl->bodies.find(b.id);
    if (ia == impl->bodies.end() || ib == impl->bodies.end() || ia->second.entity.get() != &a || ib->second.entity.get() != &b) return false;
    Touch callback; impl->world.contactPairTest(ia->second.body.get(), ib->second.body.get(), callback);
    return callback.touching;
}
Json Physics3D::raycast(World &world, glm::vec3 origin, glm::vec3 direction, float distance, unsigned mask, const std::string &ignore, bool triggers, bool synchronize) {
    if(synchronize)sync(world);
    auto length = glm::length(glm::dvec3(direction));
    if (length < 1e-8 || distance <= 0) return nullptr;
    if (mask > 65535) throw std::runtime_error("Ray mask must be 0..65535");
    btVector3 from = vector(origin), to = from + vector(direction) * (distance / length);
    struct Ray : btCollisionWorld::ClosestRayResultCallback {
        std::string ignore; bool triggers;
        Ray(btVector3 from, btVector3 to, std::string id, bool include) : ClosestRayResultCallback(from, to), ignore(std::move(id)), triggers(include) {}
        bool needsCollision(btBroadphaseProxy *proxy) const override {
            auto *body = static_cast<btCollisionObject *>(proxy->m_clientObject);
            auto *e = static_cast<Entity *>(body->getUserPointer());
            return e && e->id != ignore && (triggers || !e->trigger) && ClosestRayResultCallback::needsCollision(proxy);
        }
    } callback(from, to, ignore, triggers);
    callback.m_collisionFilterGroup = 65535; callback.m_collisionFilterMask = int(mask);
    impl->world.rayTest(from, to, callback);
    if (!callback.hasHit()) return nullptr;
    auto *e = static_cast<Entity *>(callback.m_collisionObject->getUserPointer());
    auto p = vector(callback.m_hitPointWorld, "ray hit", 1e9), n = vector(callback.m_hitNormalWorld, "ray normal");
    return {{"entity", e->id}, {"position", {p.x, p.y, p.z}}, {"normal", {n.x, n.y, n.z}},
            {"distance", callback.m_closestHitFraction * distance}, {"fraction", callback.m_closestHitFraction}};
}
Json Physics3D::move(World &world, Entity &e, glm::vec3 delta, float skin) {
    range(skin, "character skin", 0, 1);
    sync(world);
    auto found = impl->bodies.find(e.id);
    if (found == impl->bodies.end() || found->second.entity.get() != &e) throw std::runtime_error("Character needs an active collider in current scene");
    if (e.dynamic) throw std::runtime_error("Character movement needs a static or kinematic body");
    auto *convex = static_cast<btConvexShape *>(found->second.shape.get());
    btVector3 position = vector(world.worldPosition(e)), remaining = vector(delta);
    Json hits = Json::array(); bool grounded = false;
    // Bounded recovery lets a resized/spawned controller leave shallow initial penetration.
    btCollisionObject probe;probe.setCollisionShape(convex);
    for(int iteration=0;iteration<8;++iteration){
        auto pose=transform(bodyPose(world,e));pose.setOrigin(position);probe.setWorldTransform(pose);
        struct Recovery : btCollisionWorld::ContactResultCallback {
            const Entity* self;const btCollisionObject* probe;btVector3 normal{0,0,0};double depth=0;
            Recovery(const Entity* e,const btCollisionObject* p):self(e),probe(p){}
            bool needsCollision(btBroadphaseProxy* proxy)const override {
                auto* object=static_cast<btCollisionObject*>(proxy->m_clientObject);auto* other=static_cast<Entity*>(object->getUserPointer());
                return other && other!=self && !other->trigger && masks(*self,*other);
            }
            btScalar addSingleResult(btManifoldPoint& point,const btCollisionObjectWrapper* a,int,int,const btCollisionObjectWrapper*,int,int)override {
                if(point.getDistance() < -depth){depth=-point.getDistance();normal=point.m_normalWorldOnB*(a->getCollisionObject()==probe?1:-1);}
                return 0;
            }
        } callback(&e,&probe);
        impl->world.contactTest(&probe,callback);
        if(callback.depth<1e-6)break;
        position+=callback.normal*(callback.depth+skin);
    }
    for (int iteration = 0; iteration < 6 && remaining.length2() > 1e-14; ++iteration) {
        auto from = transform(bodyPose(world,e)), to = from; from.setOrigin(position); to.setOrigin(position + remaining);
        struct Sweep : btCollisionWorld::ClosestConvexResultCallback {
            const Entity *self; btVector3 delta;
            Sweep(btVector3 from, btVector3 to, const Entity *e) : ClosestConvexResultCallback(from, to), self(e), delta(to - from) {
                m_collisionFilterGroup=int(e->rigidBody.value("group",1u));
                m_collisionFilterMask=int(e->rigidBody.value("mask",65535u));
            }
            bool needsCollision(btBroadphaseProxy *proxy) const override {
                auto *body = static_cast<btCollisionObject *>(proxy->m_clientObject);
                auto *e = static_cast<Entity *>(body->getUserPointer());
                return e && e != self && !e->trigger && masks(*self, *e) && ClosestConvexResultCallback::needsCollision(proxy);
            }
            btScalar addSingleResult(btCollisionWorld::LocalConvexResult &result, bool normalInWorld) override {
                btVector3 normal = normalInWorld ? result.m_hitNormalLocal : result.m_hitCollisionObject->getWorldTransform().getBasis() * result.m_hitNormalLocal;
                if (normal.dot(delta) >= -1e-9) return 1;
                return ClosestConvexResultCallback::addSingleResult(result, normalInWorld);
            }
        } callback(position, position + remaining, &e);
        callback.m_collisionFilterGroup = int(e.rigidBody.value("group", 1u)); callback.m_collisionFilterMask = int(e.rigidBody.value("mask", 65535u));
        impl->world.convexSweepTest(convex, from, to, callback);
        if (!callback.hasHit()) { position += remaining; break; }
        auto normal = callback.m_hitNormalWorld.normalized();
        double fraction = std::max(0.0, double(callback.m_closestHitFraction) - skin / double(remaining.length()));
        position += remaining * fraction;
        auto *other = static_cast<Entity *>(callback.m_hitCollisionObject->getUserPointer());
        auto n = vector(normal, "character normal"); hits.push_back({{"entity", other->id}, {"normal", {n.x, n.y, n.z}}});
        double up=world.gravity.y>0?-1:1;
        if (normal.y()*up >= std::cos(glm::radians(e.rigidBody.value("max_slope", 45.0))) && delta.y*up <= 0) grounded = true;
        remaining *= 1 - callback.m_closestHitFraction;
        double into = remaining.dot(normal); if (into < 0) remaining -= normal * into;
    }
    auto next = vector(position, "character position");
    world.setWorldPosition(e,next);
    sync(world);
    return {{"position", {next.x, next.y, next.z}}, {"grounded", grounded}, {"hits", hits}};
}
Json Physics3D::info(World &world, const Entity &e) {
    sync(world);
    auto it = impl->bodies.find(e.id);
    if (it == impl->bodies.end() || it->second.entity.get() != &e) throw std::runtime_error("No active rigid body");
    return {{"sleeping", !it->second.body->isActive()}, {"shape", e.rigidBody.value("shape", "box")}, {"dynamic", e.dynamic}, {"kinematic", e.rigidBody.value("kinematic", false)}};
}
void Physics3D::wake(World &world, Entity &e) { sync(world); impl->bodies.at(e.id).body->activate(true); }
Json Physics3D::stats() const {
    size_t sleeping = 0;
    for (auto &[id, record] : impl->bodies) if (record.entity->dynamic && !record.body->isActive()) ++sleeping;
    return {{"backend", "bullet"}, {"bodies", impl->bodies.size()}, {"sleeping", sleeping}, {"precision", "double"}, {"version", "3.25"},{"sync_audits",impl->syncAudits},{"body_synchronizations",impl->bodySynchronizations}};
}
void bindPhysics(py::module_ &m) {
    m.def("configure_physics", [](py::dict settings) {
        auto next = validatePhysics(fromPython(settings));
        if (next["backend"] == "bullet" && !runtime().world.is3d) throw std::runtime_error("Bullet backend requires a 3D scene");
        auto &world = runtime().world;
        if (next["backend"] == "bullet") {
            size_t count=0;for(auto& e:world.entities)if(world.activeCollider(*e)){validatePhysicsEntity(*e);++count;}
            if(count>next.value("max_bodies",10000u))throw std::runtime_error("physics.max_bodies exceeded");
        }
        world.physicsSettings = std::move(next); world.physics3d.reset();
    });
    m.def("physics_settings", []() { return python(runtime().world.physicsSettings); });
    m.def("physics_stats", []() {
        auto &world = runtime().world;
        if (!rigidPhysics(world)) return python(Json{{"backend", "legacy"}});
        auto &backend = physics3D(world); backend.sync(world); return python(backend.stats());
    });
    m.def("rigid_body_info", [](Entity &e) { current(e); return python(physics3D(runtime().world).info(runtime().world, e)); });
    m.def("set_rigid_body", [](Entity &e, py::dict settings) {
        current(e); auto next = validateRigidBody(fromPython(settings)); auto previous = e.rigidBody;
        e.rigidBody = std::move(next);
        try { if (rigidPhysics(runtime().world) && runtime().world.activeCollider(e)) validatePhysicsEntity(e); }
        catch (...) { e.rigidBody = std::move(previous); throw; }
    });
    m.def("rigid_body_settings", [](Entity &e) { current(e); return python(e.rigidBody); });
    m.def("apply_force", [](Entity &e, std::array<float, 3> value, py::object point) {
        current(e); auto force = vector(Json(value), "force"); auto torque = glm::dvec3(e.torque);
        if (!point.is_none()) torque += glm::cross(glm::dvec3(vector(fromPython(point), "force point")) - glm::dvec3(e.position), glm::dvec3(force));
        auto total = glm::dvec3(e.force) + glm::dvec3(force);
        auto nextForce = vector(Json::array({total.x, total.y, total.z}), "force"), nextTorque = vector(Json::array({torque.x, torque.y, torque.z}), "torque");
        e.force = nextForce; e.torque = nextTorque;
    }, py::arg("entity"), py::arg("force"), py::arg("point") = py::none());
    m.def("apply_torque", [](Entity &e, std::array<float, 3> value) {
        current(e); auto torque = glm::dvec3(e.torque) + glm::dvec3(vector(Json(value), "torque"));
        e.torque = vector(Json::array({torque.x, torque.y, torque.z}), "torque");
    });
    m.def("apply_impulse", [](Entity &e, std::array<float, 3> value, py::object point) {
        current(e); if (!e.dynamic) throw std::runtime_error("Impulse requires a dynamic body");
        auto impulse = vector(Json(value), "impulse");
        auto &backend = physics3D(runtime().world); backend.sync(runtime().world);
        auto &body = backend.impl->bodies.at(e.id).body;
        auto velocity = body->getLinearVelocity(), angular = body->getAngularVelocity();
        btVector3 relative(0, 0, 0);
        if (!point.is_none()) relative = vector(vector(fromPython(point), "impulse point") - e.position);
        body->applyImpulse(vector(impulse), relative);
        try {
            auto next = vector(body->getLinearVelocity(), "impulse velocity"), nextAngular = vector(body->getAngularVelocity(), "impulse angular_velocity", 1e4);
            e.velocity = next; e.angularVelocity = nextAngular; body->activate(true);
        } catch (...) { body->setLinearVelocity(velocity); body->setAngularVelocity(angular); throw; }
    }, py::arg("entity"), py::arg("impulse"), py::arg("point") = py::none());
    m.def("wake_body", [](Entity &e) { current(e); physics3D(runtime().world).wake(runtime().world, e); });
    m.def("raycast_hit", [](std::array<float, 3> origin, std::array<float, 3> direction, float distance, unsigned mask, const std::string &ignore, bool triggers) {
        auto &world = runtime().world;
        return python(physics3D(world).raycast(world, vector(Json(origin), "ray origin"), vector(Json(direction), "ray direction"), range(distance, "ray distance", 0, 1e6), mask, ignore, triggers));
    }, py::arg("origin"), py::arg("direction"), py::arg("distance") = 1000, py::arg("mask") = 65535, py::arg("ignore") = "", py::arg("include_triggers") = true);
    m.def("physics_step", [](float dt) {
        range(dt, "physics dt", 0, 1); auto &world = runtime().world; world.physics(dt);
        if(dt>0)for (auto &e : world.entities) { e->force = glm::vec3(0); e->torque = glm::vec3(0); }
    });
}
PhysicsForces::PhysicsForces(World& value):world(value){
    for(auto& e:world.entities){frame.push_back({e,e->force,e->torque});e->force=glm::vec3(0);e->torque=glm::vec3(0);}
}
void PhysicsForces::step(float dt){
    std::vector<Value> pending;
    for(auto& e:world.entities){pending.push_back({e,e->force,e->torque});e->force=glm::vec3(0);e->torque=glm::vec3(0);}
    for(auto& value:frame)if(value.entity->alive){value.entity->force=value.force;value.entity->torque=value.torque;}
    auto restore=[&](){for(auto& value:pending){value.entity->force=value.force;value.entity->torque=value.torque;}};
    try{world.physics(dt);}catch(...){restore();for(auto& value:frame){value.entity->force+=value.force;value.entity->torque+=value.torque;}throw;}
    restore();
}
} // namespace forge
