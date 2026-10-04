#include <forge/particles.hpp>
#include <pybind11/stl.h>
#include <algorithm>
#include <cmath>
#include <atomic>
namespace forge {
namespace {
float number(const Json &value, const std::string &field, double low, double high) {
    float n = finiteNumber(value, "particles." + field);
    if (n < low || n > high) throw std::runtime_error("particles." + field + " outside allowed range");
    return n;
}
glm::vec3 vector(const Json &j) {
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}
glm::vec4 color(const Json &j) {
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}
void list(const Json &j, const std::string &field, size_t count, double low, double high) {
    if (!j.is_array() || j.size() != count) throw std::runtime_error("particles." + field + " has invalid vector size");
    for (const auto &v : j) number(v, field, low, high);
}
unsigned integer(const Json &j, const std::string &field, unsigned low, unsigned high) {
    if (!j.is_number_integer() || j.get<double>() < low || j.get<double>() > high)
        throw std::runtime_error("particles." + field + " must be a bounded integer");
    return j.get<unsigned>();
}
float uniform(ParticleEmitter &emitter) {
    return float(double(emitter.random()) / 4294967296.0);
}
float between(ParticleEmitter &emitter, float low, float high) {
    return low + (high - low) * uniform(emitter);
}
glm::vec3 origin(World &world, ParticleEmitter &emitter) {
    auto position = vector(emitter.settings["position"]);
    auto follow = emitter.settings.value("follow", "");
    if (!follow.empty()) {
        if (auto entity = world.find(follow)) position += entity->position;
        else return emitter.origin; // Existing particles keep the last attachment position.
    }
    emitter.origin = position;
    return position;
}
Runtime &runtime() {
    if (!active) throw std::runtime_error("Particle runtime is not active");
    return *active;
}
Json pythonInfo(const ParticleEmitter &emitter) {
    return {{"id", emitter.id}, {"settings", emitter.settings}, {"alive", emitter.live.size()},
            {"enabled", emitter.settings["enabled"]}, {"dropped", emitter.dropped}, {"elapsed", emitter.elapsed}};
}
py::object python(const Json &j) {
    return py::module_::import("json").attr("loads")(j.dump());
}
} // namespace
Json validateEmitter(const Config &config, Json j) {
    if (!j.is_object()) throw std::runtime_error("Particle emitter must be an object");
    for (auto field : {"name", "texture", "follow", "space", "shape", "blend"})
        if (j.contains(field) && !j[field].is_string()) throw std::runtime_error(std::string("particles.") + field + " must be a string");
    for (auto field : {"enabled", "loop", "screen"})
        if (j.contains(field) && !j[field].is_boolean()) throw std::runtime_error(std::string("particles.") + field + " must be boolean");
    Json defaults = {{"name", ""}, {"position", {0, 0, 0}}, {"velocity", {0, 1, 0}},
                     {"velocity_random", {0, 0, 0}}, {"gravity", {0, 0, 0}}, {"extent", {0, 0, 0}},
                     {"color_start", {1, 1, 1, 1}}, {"color_end", {1, 1, 1, 0}}, {"uv", {0, 0, 1, 1}},
                     {"lifetime", {1, 1}}, {"size", {0.1, 0.1}}, {"size_random", {1, 1}},
                     {"rotation", {0, 0}}, {"angular_speed", 0}, {"rate", 30}, {"burst", 0},
                     {"drag", 0}, {"duration", 0}, {"radius", 0}, {"max_particles", 1000},
                     {"seed", 1}, {"layer", 1}, {"enabled", true}, {"loop", false}, {"screen", false},
                     {"texture", ""}, {"follow", ""}, {"space", "world"}, {"shape", "point"}, {"blend", "alpha"}};
    defaults.merge_patch(j);
    j = std::move(defaults);
    for (auto field : {"position", "velocity", "gravity"}) list(j[field], field, 3, -1e6, 1e6);
    for (auto field : {"velocity_random", "extent"}) list(j[field], field, 3, 0, 1e4);
    for (auto field : {"color_start", "color_end", "uv"}) list(j[field], field, 4, 0, 1);
    if (j["uv"][0].get<double>() + j["uv"][2].get<double>() > 1.000001 ||
        j["uv"][1].get<double>() + j["uv"][3].get<double>() > 1.000001) throw std::runtime_error("particles.uv escapes texture");
    list(j["lifetime"], "lifetime", 2, .001, 3600);
    list(j["size"], "size", 2, 0, 10000);
    list(j["size_random"], "size_random", 2, 0, 10);
    list(j["rotation"], "rotation", 2, -36000, 36000);
    for (auto field : {"lifetime", "size_random", "rotation"})
        if (j[field][0] > j[field][1]) throw std::runtime_error(std::string("particles.") + field + " range is reversed");
    number(j["rate"], "rate", 0, 100000);
    number(j["duration"], "duration", 0, 86400);
    number(j["drag"], "drag", 0, 100);
    number(j["radius"], "radius", 0, 10000);
    number(j["angular_speed"], "angular_speed", -36000, 36000);
    integer(j["max_particles"], "max_particles", 1, unsigned(Particles::maximum));
    integer(j["burst"], "burst", 0, unsigned(Particles::maximum));
    integer(j["seed"], "seed", 0, 4294967295u);
    integer(j["layer"], "layer", 0, 4294967295u);
    if (j["space"] != "world" && j["space"] != "local") throw std::runtime_error("particles.space must be world or local");
    if (j["shape"] != "point" && j["shape"] != "box" && j["shape"] != "sphere") throw std::runtime_error("particles.shape must be point, box or sphere");
    if (j["blend"] != "alpha" && j["blend"] != "additive") throw std::runtime_error("particles.blend must be alpha or additive");
    auto texture = j["texture"].get<std::string>();
    if (!texture.empty() && !fs::is_regular_file(config.asset("textures", texture))) throw std::runtime_error("Missing particle texture: " + texture);
    return j;
}
void validateEmitters(const Config &config, const Json &j) {
    if (!j.is_array() || j.size() > 256) throw std::runtime_error("emitters must be an array of at most 256 emitters");
    size_t capacity = 0;
    for (const auto &settings : j) capacity += validateEmitter(config, settings)["max_particles"].get<size_t>();
    if (capacity > Particles::maximum) throw std::runtime_error("Combined particle capacity exceeds 100000");
}
unsigned Particles::create(const Config &config, Json settings) {
    settings = validateEmitter(config, std::move(settings));
    size_t capacity = settings["max_particles"].get<size_t>();
    for (const auto &emitter : emitters) capacity += emitter.settings["max_particles"].get<size_t>();
    if (emitters.size() >= 256 || capacity > maximum) throw std::runtime_error("Particle scene capacity exceeded");
    static std::atomic<unsigned> next{0};
    ParticleEmitter emitter;
    emitter.id = ++next;
    emitter.settings = std::move(settings);
    emitter.origin = vector(emitter.settings["position"]);
    emitter.random.seed(emitter.settings["seed"].get<unsigned>());
    emitter.live.reserve(emitter.settings["max_particles"].get<size_t>());
    emitters.push_back(std::move(emitter));
    return emitters.back().id;
}
ParticleEmitter &Particles::find(unsigned id) {
    for (auto &emitter : emitters) if (emitter.id == id) return emitter;
    throw std::runtime_error("Particle emitter is no longer in the current scene");
}
size_t Particles::burst(World &world, unsigned id, unsigned count) {
    if (count > maximum) throw std::runtime_error("Particle burst exceeds 100000");
    auto &emitter = find(id);
    auto base = origin(world, emitter);
    size_t available = emitter.settings["max_particles"].get<size_t>() - emitter.live.size();
    size_t born = std::min(available, size_t(count));
    emitter.dropped += count - born;
    auto v = vector(emitter.settings["velocity"]), variation = vector(emitter.settings["velocity_random"]), extent = vector(emitter.settings["extent"]);
    for (size_t i = 0; i < born; ++i) {
        Particle p;
        if (emitter.settings["shape"] == "box")
            for (int axis = 0; axis < 3; ++axis) p.position[axis] = between(emitter, -extent[axis], extent[axis]);
        if (emitter.settings["shape"] == "sphere") {
            double z = between(emitter, -1, 1), angle = uniform(emitter) * 6.283185307179586;
            double radial = std::sqrt(std::max(0.0, 1 - z * z));
            double r = std::cbrt(uniform(emitter)) * emitter.settings["radius"].get<double>();
            p.position = glm::vec3(radial * std::cos(angle), z, radial * std::sin(angle)) * float(r);
        }
        if (emitter.settings["space"] == "world") p.position += base;
        for (int axis = 0; axis < 3; ++axis) p.velocity[axis] = v[axis] + between(emitter, -variation[axis], variation[axis]);
        p.lifetime = between(emitter, emitter.settings["lifetime"][0], emitter.settings["lifetime"][1]);
        p.sizeFactor = between(emitter, emitter.settings["size_random"][0], emitter.settings["size_random"][1]);
        p.angle = between(emitter, emitter.settings["rotation"][0], emitter.settings["rotation"][1]);
        emitter.live.push_back(p);
    }
    return born;
}
void Particles::update(World &world, float dt) {
    number(dt, "dt", 0, 1);
    for (auto &emitter : emitters) {
        auto acceleration = glm::dvec3(vector(emitter.settings["gravity"]));
        double decay = std::exp(-emitter.settings["drag"].get<double>() * dt);
        double angular = emitter.settings["angular_speed"].get<double>() * dt;
        origin(world, emitter);
        for (auto &p : emitter.live) {
            auto velocity = (glm::dvec3(p.velocity) + acceleration * double(dt)) * decay;
            auto position = glm::dvec3(p.position) + velocity * double(dt);
            p.velocity = {checkedFloat(velocity.x, "particle velocity"), checkedFloat(velocity.y, "particle velocity"), checkedFloat(velocity.z, "particle velocity")};
            p.position = {checkedFloat(position.x, "particle position"), checkedFloat(position.y, "particle position"), checkedFloat(position.z, "particle position")};
            p.angle = float(std::fmod(double(p.angle) + angular, 360));
            p.age += dt;
        }
        emitter.live.erase(std::remove_if(emitter.live.begin(), emitter.live.end(), [](const Particle &p) { return p.age >= p.lifetime; }), emitter.live.end());
        double emissionTime = dt, duration = emitter.settings["duration"].get<double>();
        if (duration > 0 && !emitter.settings["loop"].get<bool>()) emissionTime = std::max(0.0, std::min(double(dt), duration - emitter.elapsed));
        emitter.elapsed = duration > 0 && emitter.settings["loop"].get<bool>() ? std::fmod(emitter.elapsed + dt, duration) : std::min(86400.0, emitter.elapsed + dt);
        bool attached = emitter.settings["follow"].get<std::string>().empty() || bool(world.find(emitter.settings["follow"]));
        if (attached && emitter.settings["enabled"].get<bool>()) {
            emitter.carry += emissionTime * emitter.settings["rate"].get<double>();
            auto count = unsigned(std::floor(emitter.carry));
            emitter.carry -= count;
            burst(world, emitter.id, count);
        }
    }
}
void Particles::remove(unsigned id) {
    find(id);
    emitters.erase(std::remove_if(emitters.begin(), emitters.end(), [id](const auto &e) { return e.id == id; }), emitters.end());
}
std::vector<ParticleDraw> Particles::draw(const World &) const {
    std::vector<ParticleDraw> result;
    for (const auto &emitter : emitters) {
        auto start = color(emitter.settings["color_start"]), end = color(emitter.settings["color_end"]);
        glm::vec3 offset = emitter.settings["space"] == "local" ? emitter.origin : glm::vec3(0);
        auto uv = color(emitter.settings["uv"]);
        for (const auto &p : emitter.live) {
            float t = std::clamp(p.age / p.lifetime, 0.f, 1.f);
            float size = glm::mix(emitter.settings["size"][0].get<float>(), emitter.settings["size"][1].get<float>(), t) * p.sizeFactor;
            result.push_back({p.position + offset, glm::mix(start, end, t), uv, size, p.angle,
                              emitter.settings["texture"], emitter.settings["layer"],
                              emitter.settings["blend"] == "additive", emitter.settings["screen"].get<bool>()});
        }
    }
    return result;
}
Json Particles::serialize() const {
    Json result = Json::array();
    for (const auto &emitter : emitters) result.push_back(emitter.settings);
    return result;
}
Json Particles::stats() const {
    size_t count = 0, capacity = 0, dropped = 0;
    for (const auto &emitter : emitters) { count += emitter.live.size(); capacity += emitter.settings["max_particles"].get<size_t>(); dropped += emitter.dropped; }
    return {{"emitters", emitters.size()}, {"alive", count}, {"capacity", capacity}, {"dropped", dropped}, {"limit", maximum}};
}
Particles &particleSystem(World &world) {
    if (!world.particles) world.particles = std::make_shared<Particles>();
    return *world.particles;
}
void bindParticles(py::module_ &m) {
    m.def("particle_step", [](float dt) { particleSystem(runtime().world).update(runtime().world, dt); });
    m.def("particle_emitter", [](py::dict settings) {
        if (runtime().tearingDown) throw std::runtime_error("Cannot create particles during scene teardown");
        auto &world = runtime().world;
        auto &system = particleSystem(world);
        unsigned id = system.create(runtime().config, fromPython(settings));
        system.burst(world, id, system.find(id).settings["burst"].get<unsigned>());
        return id;
    });
    m.def("particle_info", [](unsigned id) { return python(pythonInfo(particleSystem(runtime().world).find(id))); });
    m.def("particle_emitters", []() {
        std::vector<unsigned> ids;
        for (const auto &e : particleSystem(runtime().world).emitters) ids.push_back(e.id);
        return ids;
    });
    m.def("particle_burst", [](unsigned id, unsigned count) { return particleSystem(runtime().world).burst(runtime().world, id, count); });
    m.def("particle_remove", [](unsigned id) { particleSystem(runtime().world).remove(id); });
    m.def("particle_enabled", [](unsigned id, bool enabled) { particleSystem(runtime().world).find(id).settings["enabled"] = enabled; });
    m.def("particle_position", [](unsigned id, std::array<float, 3> value) {
        auto &system = particleSystem(runtime().world);
        auto &emitter = system.find(id);
        auto next = emitter.settings; next["position"] = value;
        emitter.settings = validateEmitter(runtime().config, std::move(next));
        origin(runtime().world, emitter);
    });
    m.def("particle_stats", []() { return python(particleSystem(runtime().world).stats()); });
    m.def("particle_snapshot", [](unsigned id, unsigned limit) {
        if (limit > 100000) throw std::runtime_error("Particle snapshot limit exceeds 100000");
        Json result = Json::array();
        auto &emitter = particleSystem(runtime().world).find(id);
        for (const auto &p : emitter.live) {
            if (result.size() >= limit) break;
            float t=std::clamp(p.age/p.lifetime,0.f,1.f);
            auto tint=glm::mix(color(emitter.settings["color_start"]),color(emitter.settings["color_end"]),t);
            auto position=p.position+(emitter.settings["space"]=="local"?emitter.origin:glm::vec3(0));
            float size=glm::mix(emitter.settings["size"][0].get<float>(),emitter.settings["size"][1].get<float>(),t)*p.sizeFactor;
            result.push_back({{"position", {p.position.x, p.position.y, p.position.z}}, {"velocity", {p.velocity.x, p.velocity.y, p.velocity.z}},
                              {"world_position",{position.x,position.y,position.z}},{"color",{tint.r,tint.g,tint.b,tint.a}},{"size",size},
                              {"age", p.age}, {"lifetime", p.lifetime}, {"angle", p.angle}, {"size_factor", p.sizeFactor}});
        }
        return python(result);
    }, py::arg("id"), py::arg("limit") = 32);
}
} // namespace forge
