#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/particles.hpp>
#include <forge/material.hpp>
#include <forge/geometry.hpp>
#include <forge/file_watch.hpp>
#include <forge/animation.hpp>
#include <pybind11/stl.h>
#include <chrono>
#include <thread>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
namespace forge {
Runtime* active=nullptr;
std::vector<ModuleInit>& nativeModules(){static std::vector<ModuleInit> list;return list;}
Runtime::Runtime(Config c,bool development,bool noWindow):config(std::move(c)),sceneConfig(config),assets(config),dev(development),headless(noWindow){if(active)throw std::runtime_error("Only one Python runtime can be active in this process");world.config=&config;world.prepareEntity=[this](Entity& entity){prepareAnimation(*this,entity);if(!entity.textKey.empty())entity.text=localization.translate(entity.textKey,entity.textParams);};audio.silent=headless;localization.load(config,"",true);active=this;}
Runtime::~Runtime(){listeners.clear();scripts.clear();startup.clear();if(active==this)active=nullptr;}
void Runtime::start() {
    auto sys=py::module_::import("sys");sys.attr("dont_write_bytecode")=true;
    refreshPythonPaths();
    // print() and stderr join the same timestamped log as the native engine.
    py::exec(R"(
import sys, forge
class _ForgeStream:
    def __init__(self, level): self.level, self.buffer = level, ''
    def write(self, text):
        self.buffer += text
        while '\n' in self.buffer:
            line, self.buffer = self.buffer.split('\n', 1)
            forge.log(line, self.level)
        return len(text)
    def flush(self):
        if self.buffer: forge.log(self.buffer, self.level); self.buffer = ''
    def isatty(self): return False
    encoding = 'utf-8'
sys.stdout, sys.stderr = _ForgeStream('INFO'), _ForgeStream('ERROR')
)");
    auto win=config.data.value("window",Json::object());world.width=win.value("width",1280);world.height=win.value("height",720);
    if(!headless){renderer=std::make_unique<Renderer>();renderer->init(config,world,*this);}
    for(auto& item:config.data.value("startup_scripts",Json::array())){auto module=loadModule(config.asset("scripts",item.get<std::string>()));startup.push_back(module);if(py::hasattr(module,"on_start"))module.attr("on_start")();}
    if(dev)restartWatcher();
    loadScene(config.entry());
    if(editing)gamePaused=true;
    if(editing) {
        auto editorOptions=config.data.value("editor",Json::object());
        if(!editorOptions.is_object())throw std::runtime_error("editor must be an object");
        for(const auto &path:editorOptions.value("extensions",Json::array()))extensionFiles.push_back(config.resolve(path.get<std::string>()).u8string());
        auto sdk=fs::path(FORGE_SOURCE_DIR)/"sdk";
        if(fs::is_regular_file(sdk/"forge_editor/__init__.py")) {
            auto sys=py::module_::import("sys");sys.attr("path").attr("insert")(0,sdk.u8string());
            try{editorClient=py::module_::import("forge_editor").attr("RuntimeClient")(executable.u8string(),config.file.u8string());}
            catch(...){sys.attr("path").attr("remove")(sdk.u8string());throw;}
            sys.attr("path").attr("remove")(sdk.u8string());
            for(const auto &file:extensionFiles)editorClient.attr("extension")(file);
        }else if(!extensionFiles.empty())throw std::runtime_error("Editor extensions require the development SDK directory");
    }
    if(editing && !shellFile.empty()) {
        auto file=config.resolve(shellFile);
        if(file.extension()!=".py")throw std::runtime_error("Viewport shell must be a project .py file");
        gamePaused=true;
        editorShell=loadModule(file);
        if(!py::hasattr(editorShell,"API_VERSION") || editorShell.attr("API_VERSION").cast<int>()!=1)throw std::runtime_error("Viewport shell must declare API_VERSION = 1");
        if(py::hasattr(editorShell,"on_start"))editorShell.attr("on_start")();
    }
}
bool Runtime::shutdown(){watcher.reset();bool failed=false;tearingDown=true;
    if(editorShell && !editorShell.is_none()) {
        if(py::hasattr(editorShell,"on_destroy"))try{editorShell.attr("on_destroy")();}catch(const std::exception& e){logger.error(e.what());failed=true;}
        editorShell=py::object();
    }
    editorClient=py::object();
    auto ending=std::move(scripts);scripts.clear();auto endingStartup=std::move(startup);startup.clear();
    for(auto& script:ending)if(py::hasattr(script.instance,"on_destroy"))try{script.instance.attr("on_destroy")();}catch(const std::exception& e){logger.error(e.what());failed=true;}
    for(auto& module:endingStartup)if(py::hasattr(module,"on_destroy"))try{module.attr("on_destroy")();}catch(const std::exception& e){logger.error(e.what());failed=true;}
    for(auto& e:world.entities)e->alive=false;
    for(auto name:{"stdout","stderr"})try{py::module_::import("sys").attr(name).attr("flush")();}catch(...){}
    try{localization.flush();}catch(const std::exception& e){logger.error(e.what());failed=true;}
    listeners.clear();scripts.clear();startup.clear();audio.stop();renderer.reset();tearingDown=false;return !failed;
}
void Runtime::refreshLocalizedEntities(){
    std::vector<std::pair<std::shared_ptr<Entity>,std::string>> updates;
    for(auto& e:world.entities)if(e->alive && !e->textKey.empty() &&
        (e->localizedRevision!=localization.revision || e->localizedKey!=e->textKey || e->localizedParams!=e->textParams))
        updates.push_back({e,localization.translate(e->textKey,e->textParams)});
    // Commit derived values only after all translations have succeeded.
    for(auto& [entity,text]:updates){entity->text=std::move(text);entity->localizedKey=entity->textKey;entity->localizedParams=entity->textParams;entity->localizedRevision=localization.revision;}
    profile["localization_translations"]=updates.size();
}
int Runtime::run(int frames) {
    auto previous=std::chrono::steady_clock::now();
    float physicsAccumulator=0;
    bool paused=false,pausedRenderFailed=false,pausedShellFailed=false;
    int result=0;
    try {
        audio.configure(config.data.value("audio_settings",Json::object()));
        assets.budget(config.data.value("asset_budget_bytes",size_t(256*1024*1024)));
        start();
        for(int frame=0;running && (frames<0 || frame<frames);++frame){
            auto now=std::chrono::steady_clock::now();
            dt=headless?1.0f/60:std::min(.1f,std::chrono::duration<float>(now-previous).count());
            previous=now;time+=dt;
            if(renderer){
                renderer->poll();
                if(renderer->closing())break;
            }
            inputFrame=renderer?renderer->input():Json::object();
            inputFrame["dt"]=dt;inputFrame["time"]=time;inputFrame["frame"]=frame;
            if(dev && changed()){
                paused=!tryReload();
                if(!paused){pausedRenderFailed=false;pausedShellFailed=false;}
            }
            if(paused){
                if(editing && editorShell && !editorShell.is_none() && !pausedShellFailed && py::hasattr(editorShell,"on_update")){
                    try{editorShell.attr("on_update")(dt);}
                    catch(const std::exception& error){logger.error(error.what());pausedShellFailed=true;}
                }
                if(editing && !pendingScene.empty()){
                    auto name=pendingScene;pendingScene.clear();
                    try{loadScene(name);gamePaused=true;physicsAccumulator=0;paused=false;pausedRenderFailed=false;pausedShellFailed=false;}
                    catch(const std::exception& error){logger.error(error.what());}
                }
                if(renderer && !pausedRenderFailed){
                    try {renderer->render(world);}
                    catch(const std::exception& error){logger.error(error.what());pausedRenderFailed=true;}
                }
                if(headless)std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            try {
                if(!pendingScene.empty()){
                    auto name=pendingScene;pendingScene.clear();
                    loadScene(name);
                    if(editing)gamePaused=renderer?!renderer->previewing():true;
                    physicsAccumulator=0;
                }
                tick(physicsAccumulator);
            }catch(const std::exception& error){
                logger.error(error.what());
                if(!dev){result=1;break;}
                paused=true;
            }
            if(headless && dev)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(const std::exception& error){logger.error(error.what());result=1;}
    if(!shutdown())result=1;
    logger.write("INFO",result==0?"Runtime stopped":"Runtime stopped with errors");
    return result;
}
static fs::path savePath(const Runtime& r,const std::string& name) {
    if(name.empty() || fs::u8path(name).filename()!=fs::u8path(name) || name=="." || name=="..")throw std::runtime_error("Save name must be a filename");
    auto base=r.config.data.value("save_directory","saves");auto folder=storagePath(r.config,base);fs::create_directories(folder);return storagePath(r.config,(fs::u8path(base)/fs::u8path(name+".json")).generic_u8string());
}
void Runtime::save(const std::string& name,py::object value) {
    if(tearingDown)return;
    if(reloading || authoringTransaction){auto snapshot=fromPython(value);persistence.push_back(py::cpp_function([this,name,snapshot](){save(name,pythonValue(snapshot));}));return;}
    auto file=savePath(*this,name),temp=file;temp += ".tmp";auto data=fromPython(value);
    if(fs::exists(temp) || fs::is_symlink(fs::symlink_status(temp)))throw std::runtime_error("Temporary save path already exists: "+temp.u8string());
    try {
        {std::ofstream out(temp);if(!out)throw std::runtime_error("Cannot write save: "+temp.u8string());out<<data.dump(2);out.flush();if(!out)throw std::runtime_error("Save write failed");}
#ifdef _WIN32
        if(!MoveFileExW(temp.wstring().c_str(),file.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace save file");
#else
        fs::rename(temp,file);
#endif
    }catch(...){std::error_code error;fs::remove(temp,error);throw;}
}
py::object Runtime::load(const std::string& name,py::object fallback){auto file=savePath(*this,name);return fs::exists(file)?pythonValue(readJson(file)):fallback;}
}
