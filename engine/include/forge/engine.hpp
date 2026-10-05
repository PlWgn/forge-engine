#pragma once
#include <array>
#include <filesystem>
#include <forge/numeric.hpp>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <forge/types.hpp>
#include <forge/scene.hpp>
#include <forge/image.hpp>
#include <map>
#include <memory>
#include <pybind11/embed.h>
#include <set>
#include <string>
#include <vector>
#include <unordered_set>
#include <forge/config.hpp>
#include <forge/assets.hpp>
#include <forge/logger.hpp>
#include <forge/localization.hpp>
#include <forge/renderer.hpp>
#include <forge/audio.hpp>
#include <forge/editor_session.hpp>
namespace forge {
namespace py = pybind11;
struct Model;
struct Physics3D;
struct Particles;
float finiteNumber(const Json &, const std::string &field);
Json fromPython(py::handle value);
py::object pythonValue(const Json& value);
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
class FileWatch;
struct Runtime {
    Config config, sceneConfig;
    World world;
    EditorSession editorSession;
    bool baseShell = true;
    std::string shellFile;
    py::object editorShell, editorClient;
    fs::path executable;
    std::vector<std::string> extensionFiles;
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
    std::unordered_set<unsigned> listenerIds;
    bool dispatchingFrame=false;
    std::unique_ptr<FileWatch> watcher;
    std::set<fs::path> changedFiles;
    bool changedFilesOverflow=false;
    double time = 0;
    float dt = 0;
    unsigned moduleIndex = 0;
    Runtime(Config config, bool development, bool noWindow);
    ~Runtime();
    void loadScene(const std::string &, const Localization *teardownLocalization = nullptr, const Json *document = nullptr);
    py::object loadModule(const fs::path &);
    void attach(std::shared_ptr<Entity>);
    void attachPending();
    void destroyDead();
    void reload();
    bool tryReload();
    void restartWatcher();
    std::unique_ptr<FileWatch> prepareWatcher();
    unsigned onFrame(py::object,bool persistent);
    void removeListener(unsigned);
    void dispatchFrame();
    void dispatchContacts(const std::vector<Script>&,const std::set<std::pair<std::string,std::string>>& previous);
    void refreshLocalizedEntities();
    void refreshPythonPaths();
    void start();
    int run(int frames);
    void tick(float& physicsAccumulator);
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
