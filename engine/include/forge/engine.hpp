#pragma once
#include <array>
#include <filesystem>
#include <forge/numeric.hpp>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <json.hpp>
#include <map>
#include <memory>
#include <pybind11/embed.h>
#include <set>
#include <string>
#include <vector>
namespace forge {
namespace fs = std::filesystem;
namespace py = pybind11;
using Json = nlohmann::json;
struct Model;
struct ImageData {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};
float finiteNumber(const Json &, const std::string &field);
struct Config {
    fs::path root, file;
    Json data;
    std::map<std::string, fs::path> paths;
    static Config load(const fs::path &file);
    fs::path resolve(const std::string &path) const;
    fs::path asset(const std::string &group, const std::string &name) const;
    std::string entry() const;
    void validate(bool media = true) const;
};
Json validateEntity(const Config &, Json);
Json readJson(const fs::path &file);
void validateMedia(const Config &config);
fs::path userPath(const Config &, const std::string &kind, const std::string &file = "");
fs::path storagePath(const Config &, const std::string &relative);
void installCrashHandler(const fs::path &directory);
fs::path crashReport(const std::string &message);
Json validateRenderSettings(Json);
void bindFeatures(py::module_ &);
struct Assets {
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit Assets(Config &);
    ~Assets();
    unsigned request(const std::string &group, const std::string &name);
    Json info(unsigned);
    std::shared_ptr<ImageData> image(const fs::path &);
    std::shared_ptr<Model> model(const fs::path &);
    std::vector<unsigned char> bytes(unsigned);
    void release(unsigned);
    void budget(size_t);
    Json stats();
    unsigned checkpoint();
    void rollback(unsigned);
};
Json fromPython(py::handle value);
struct Logger {
    fs::path path;
    std::ofstream file;
    bool autoOpen = true, opened = false;
    void start(const fs::path &root, bool open);
    void write(const std::string &level, const std::string &message);
    void error(const std::string &message);
    void open();
};
extern Logger logger;
struct Localization {
    struct Catalog {
        std::string name, pluralLanguage;
        std::map<std::string, Json> messages;
    };
    std::map<std::string, Catalog> catalogs;
    std::string language, defaultLanguage, fallbackLanguage, pendingPreference;
    fs::path preferencePath;
    bool saveSelection = true, warnMissing = true;
    unsigned revision = 0;
    std::set<std::pair<std::string, std::string>> warned;
    void load(const Config &, const std::string &previous = "", bool readPreference = false);
    void select(const std::string &, bool persist = true);
    std::string translate(const std::string &, const Json &params = Json::object());
    bool has(const std::string &, const std::string &locale = "", bool fallback = true) const;
    Json languages() const;
    void flush();
};
struct LocalizedText {
    std::string key;
    Json params = Json::object();
};
struct Entity {
    std::string id, name, kind = "sprite", model, texture, material, text, textKey;
    Json textParams = Json::object();
    glm::vec3 position{0}, rotation{0}, scale{1}, velocity{0}, collider{0};
    glm::vec4 color{1}, clip{0};
    glm::vec4 uv{0, 0, 1, 1};
    unsigned layer = 1;
    bool castsShadow = true;
    Json uniforms = Json::object();
    std::string animation;
    double animationTime = 0;
    float animationSpeed = 1;
    bool animationLoop = true, animationPlaying = false;
    bool clipped = false;
    bool attached = false;
    bool visible = true, alive = true, dynamic = false, trigger = false, screen = false;
    float mass = 1, fontSize = 24;
    Json scripts = Json::array();
    Json data = Json::object();
};
struct World {
    Config *config = nullptr;
    std::vector<std::shared_ptr<Entity>> entities;
    Json scene;
    unsigned nextId = 0;
    glm::vec3 cameraPosition{0, 0, 5}, cameraTarget{0}, gravity{0, -9.81f, 0};
    bool is3d = false;
    bool physicsEnabled = true;
    Json renderSettings = Json::object();
    float fov = 60;
    int width = 1280, height = 720;
    glm::vec4 background{0.025f, 0.04f, 0.075f, 1};
    std::shared_ptr<Entity> spawn(Json data);
    std::shared_ptr<Entity> find(const std::string &id);
    void load(const std::string &scenePath);
    std::set<std::pair<std::string, std::string>> contacts;
    void physics(float dt);
    bool activeCollider(const Entity &) const;
    bool overlaps(const Entity &a, const Entity &b) const;
    std::shared_ptr<Entity> raycast(glm::vec3 origin, glm::vec3 direction, float distance);
    Json moveCharacter(Entity &, glm::vec3 delta, float skin = .001f);
    Json serialize() const;
};
struct Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl, staged;
    Renderer();
    ~Renderer();
    void init(const Config &, World &);
    void render(World &);
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
};
struct Audio {
    struct Impl;
    std::unique_ptr<Impl> impl;
    Audio();
    ~Audio();
    bool silent = false;
    unsigned play(const fs::path &, bool loop, float volume, const std::string &channel = "sfx",
                  float fade = 0, const Json &options = Json::object());
    void stop(unsigned id = 0, const std::string &channel = "", float fade = 0);
    void volume(const std::string &channel, float value);
    float volume(const std::string &channel) const;
    void fade(unsigned id, float target, float seconds, bool stopAfter = false);
    bool playing(unsigned id) const;
    float gain(unsigned id) const;
    std::vector<unsigned> ids(const std::string &) const;
    void pause(unsigned id, bool paused);
    void update(float dt);
    void begin();
    void commit();
    void rollback();
    void configure(const Json &);
    void options(unsigned, const Json &);
    Json info(unsigned) const;
    Json diagnostics() const;
    void listener(glm::vec3 position, glm::vec3 direction);
};
std::array<float, 3> measureText(const Config &, const std::string &, float size);
struct FrameListener {
    unsigned id;
    py::object callback;
    bool persistent;
};
struct Script {
    py::object module, instance;
    std::shared_ptr<Entity> entity;
};
struct Runtime {
    Config config, sceneConfig;
    World world;
    std::unique_ptr<Renderer> renderer;
    Audio audio;
    Localization localization;
    Assets assets;
    Json inputFrame = Json::object(), profile = Json::object();
    std::vector<py::object> persistence;
    std::string entryOverride;
    bool reloading = false, editing = false;
    std::vector<Script> scripts;
    std::vector<py::object> startup;
    std::map<fs::path, fs::file_time_type> watched;
    std::vector<std::string> pythonPaths;
    std::string currentScene, pendingScene;
    bool running = true, dev = false, headless = false, gamePaused = false, initializing = false,
         tearingDown = false;
    unsigned listenerIndex = 0;
    std::vector<FrameListener> listeners;
    double time = 0;
    float dt = 0;
    unsigned moduleIndex = 0;
    Runtime(Config config, bool development, bool noWindow);
    ~Runtime();
    void loadScene(const std::string &, const Localization *teardownLocalization = nullptr);
    py::object loadModule(const fs::path &);
    void attach(std::shared_ptr<Entity>);
    void attachPending();
    void destroyDead();
    void reload();
    void refreshLocalizedEntities();
    void refreshPythonPaths();
    void start();
    int run(int frames);
    bool shutdown();
    bool changed();
    std::map<fs::path, fs::file_time_type> snapshot() const;
    void save(const std::string &name, py::object value);
    py::object load(const std::string &name, py::object fallback);
};
extern Runtime *active;
using ModuleInit = void (*)(py::module_ &);
std::vector<ModuleInit> &nativeModules();
struct ModuleRegistration {
    explicit ModuleRegistration(ModuleInit init) {
        nativeModules().push_back(init);
    }
};
} // namespace forge
#define FORGE_MODULE(name)                                                                                   \
    static void name(pybind11::module_ &);                                                                   \
    static forge::ModuleRegistration name##_registration(name);                                              \
    static void name(pybind11::module_ &module)
