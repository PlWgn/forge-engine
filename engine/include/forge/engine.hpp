#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <json.hpp>
#include <glm/glm.hpp>
#include <pybind11/embed.h>
namespace forge {
namespace fs = std::filesystem;
namespace py = pybind11;
using Json = nlohmann::json;
struct Config {
    fs::path root, file;
    Json data;
    std::map<std::string, fs::path> paths;
    static Config load(const fs::path& file);
    fs::path resolve(const std::string& path) const;
    fs::path asset(const std::string& group, const std::string& name) const;
    std::string entry() const;
    void validate() const;
};
Json readJson(const fs::path& file);
void validateMedia(const Config& config);
Json fromPython(py::handle value);
struct Logger {
    fs::path path;
    std::ofstream file;
    bool autoOpen = true, opened = false;
    void start(const fs::path& root, bool open);
    void write(const std::string& level, const std::string& message);
    void error(const std::string& message);
    void open();
};
extern Logger logger;
struct Entity {
    std::string id, name, kind = "sprite", model, texture, material, text;
    glm::vec3 position{0}, rotation{0}, scale{1}, velocity{0}, collider{0};
    glm::vec4 color{1};
    bool visible = true, alive = true, dynamic = false, trigger = false, screen = false;
    float mass = 1, fontSize = 24;
    Json scripts = Json::array();
    Json data = Json::object();
};
struct World {
    Config* config = nullptr;
    std::vector<std::shared_ptr<Entity>> entities;
    Json scene;
    unsigned nextId = 0;
    glm::vec3 cameraPosition{0,0,5}, cameraTarget{0}, gravity{0,-9.81f,0};
    bool is3d = false;
    float fov = 60;
    int width = 1280, height = 720;
    glm::vec4 background{0.025f,0.04f,0.075f,1};
    std::shared_ptr<Entity> spawn(Json data);
    std::shared_ptr<Entity> find(const std::string& id);
    void load(const std::string& scenePath);
    std::set<std::pair<std::string,std::string>> contacts;
    void physics(float dt);
    bool overlaps(const Entity& a, const Entity& b) const;
    std::shared_ptr<Entity> raycast(glm::vec3 origin, glm::vec3 direction, float distance);
};
struct Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl;
    Renderer(); ~Renderer();
    void init(const Config&, World&);
    void render(World&);
    void invalidate();
    void poll();
    bool closing() const;
    bool key(const std::string&) const;
    bool pressed(const std::string&) const;
    bool mouse(int button) const;
    glm::vec2 cursor() const;
    glm::vec2 delta() const;
    void capture(bool);
    void quit();
    void screenshot(const fs::path& path);
};
struct Audio {
    struct Impl; std::unique_ptr<Impl> impl;
    Audio(); ~Audio();
    void play(const fs::path&, bool loop, float volume);
    void stop();
};
struct Script { py::object module, instance; std::shared_ptr<Entity> entity; };
struct Runtime {
    Config config;
    World world;
    std::unique_ptr<Renderer> renderer;
    Audio audio;
    std::vector<Script> scripts;
    std::vector<py::object> startup;
    std::map<fs::path, fs::file_time_type> watched;
    std::string currentScene, pendingScene;
    bool running = true, dev = false, headless = false;
    double time = 0;
    float dt = 0;
    unsigned moduleIndex = 0;
    Runtime(Config config, bool development, bool noWindow);
    ~Runtime();
    void loadScene(const std::string&);
    py::object loadModule(const fs::path&);
    void attach(std::shared_ptr<Entity>);
    void start();
    int run(int frames);
    bool shutdown();
    bool changed();
    std::map<fs::path, fs::file_time_type> snapshot() const;
    void save(const std::string& name, py::object value);
    py::object load(const std::string& name, py::object fallback);
};
extern Runtime* active;
using ModuleInit = void(*)(py::module_&);
std::vector<ModuleInit>& nativeModules();
struct ModuleRegistration { explicit ModuleRegistration(ModuleInit init) { nativeModules().push_back(init); } };
}
#define FORGE_MODULE(name) static void name(pybind11::module_&); static forge::ModuleRegistration name##_registration(name); static void name(pybind11::module_& module)
