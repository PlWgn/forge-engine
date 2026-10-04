#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/particles.hpp>
#include <forge/material.hpp>
#include <forge/geometry.hpp>
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
Runtime::Runtime(Config c,bool development,bool noWindow):config(std::move(c)),sceneConfig(config),assets(config),dev(development),headless(noWindow){if(active)throw std::runtime_error("Only one Python runtime can be active in this process");world.config=&config;audio.silent=headless;localization.load(config,"",true);active=this;}
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
    loadScene(config.entry());watched=snapshot();
}
bool Runtime::shutdown(){bool failed=false;tearingDown=true;
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
    for(auto& e:world.entities)if(e->alive && !e->textKey.empty())updates.push_back({e,localization.translate(e->textKey,e->textParams)});
    for(auto& [entity,text]:updates)entity->text=std::move(text);
}
int Runtime::run(int frames) {
    auto previous=std::chrono::steady_clock::now();double reloadClock=0;float physicsAccumulator=0;bool paused=false,pausedRenderFailed=false;int result=0;
    try {audio.configure(config.data.value("audio_settings",Json::object()));assets.budget(config.data.value("asset_budget_bytes",size_t(256*1024*1024)));start();if(editing)gamePaused=true;
        for(int frame=0;running && (frames<0 || frame<frames);++frame) {
            auto now=std::chrono::steady_clock::now();dt=headless?1.0f/60:std::min(.1f,std::chrono::duration<float>(now-previous).count());previous=now;time+=dt;reloadClock+=dt;
            if(renderer){renderer->poll();if(renderer->closing())break;}
            inputFrame=renderer?renderer->input():Json::object();inputFrame["dt"]=dt;inputFrame["time"]=time;inputFrame["frame"]=frame;
            if(dev && reloadClock>=.3){reloadClock=0;if(changed()) {
                try {
                    reload();
                    paused=false;pausedRenderFailed=false;logger.write("INFO","Hot reload complete");
                }catch(const std::exception& e){logger.error(e.what());paused=true;logger.write("WARN","Development paused. Edit a watched file to retry.");
                    auto previousScripts=scripts;for(auto& script:previousScripts)if(py::hasattr(script.instance,"on_reload_failed"))try{script.instance.attr("on_reload_failed")(std::string(e.what()));}catch(const std::exception& failure){logger.error(failure.what());}
                    auto previousStartup=startup;for(auto& module:previousStartup)if(py::hasattr(module,"on_reload_failed"))try{module.attr("on_reload_failed")(std::string(e.what()));}catch(const std::exception& failure){logger.error(failure.what());}
                }
            }}
            if(paused){if(renderer && !pausedRenderFailed)try{renderer->render(world);}catch(const std::exception& e){logger.error(e.what());pausedRenderFailed=true;}if(headless)std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            try {
                if(!pendingScene.empty()){auto name=pendingScene;pendingScene.clear();loadScene(name);if(editing)gamePaused=renderer?!renderer->previewing():true;physicsAccumulator=0;}
                auto frameStart=std::chrono::steady_clock::now();auto elapsed=[&](auto before){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();};
                auto callbacks=listeners;
                if(!editing || !gamePaused)for(auto& listener:callbacks)if(std::any_of(listeners.begin(),listeners.end(),[&](auto& l){return l.id==listener.id;}))listener.callback(dt);
                profile["callbacks_ms"]=elapsed(frameStart);auto audioStart=std::chrono::steady_clock::now();
                audio.update(dt);
                profile["audio_ms"]=elapsed(audioStart);auto scriptsStart=std::chrono::steady_clock::now();
                destroyDead();attachPending();destroyDead();
                if(!gamePaused){
                // New objects can be spawned by lifecycle callbacks without invalidating iteration.
                auto current=scripts;
                for(auto& module:startup)if(py::hasattr(module,"on_update"))module.attr("on_update")(dt);
                for(auto& script:current)if((!script.entity || script.entity->alive) && py::hasattr(script.instance,"on_update"))script.instance.attr("on_update")(dt);
                destroyDead();attachPending();destroyDead();
                current=scripts;
                profile["scripts_ms"]=elapsed(scriptsStart);auto physicsStart=std::chrono::steady_clock::now();
                physicsAccumulator+=dt;
                if(!world.physicsEnabled)physicsAccumulator=0;
                std::unique_ptr<PhysicsForces> forceFrame;
                if(world.physicsEnabled && physicsAccumulator>=1.0f/120)forceFrame=std::make_unique<PhysicsForces>(world);
                while(world.physicsEnabled && physicsAccumulator>=1.0f/120){
                    auto old=world.contacts;forceFrame->step(1.0f/120);physicsAccumulator-=1.0f/120;
                    for(auto& script:current)if(script.entity && script.entity->alive) {
                        auto id=script.entity->id;
                        for(auto& pair:world.contacts)if(!old.count(pair) && (pair.first==id || pair.second==id) && py::hasattr(script.instance,"on_collision"))script.instance.attr("on_collision")(world.find(pair.first==id?pair.second:pair.first));
                        for(auto& pair:old)if(!world.contacts.count(pair) && (pair.first==id || pair.second==id) && py::hasattr(script.instance,"on_collision_exit"))script.instance.attr("on_collision_exit")(world.find(pair.first==id?pair.second:pair.first));
                    }
                }
                profile["physics_ms"]=elapsed(physicsStart);
                }
                auto particleStart=std::chrono::steady_clock::now();
                if(!gamePaused && world.particles)world.particles->update(world,dt);
                profile["particles_ms"]=elapsed(particleStart);
                destroyDead();attachPending();destroyDead();
                if(!gamePaused)for(auto& entity:world.entities)if(entity->alive && entity->animationPlaying)entity->animationTime=std::max(0.0,entity->animationTime+double(dt)*entity->animationSpeed);
                refreshLocalizedEntities();localization.flush();
                if(renderer)renderer->render(world);
                profile["frame_ms"]=elapsed(frameStart);profile["entities"]=world.entities.size();profile["assets"]=assets.stats();if(world.particles)profile["particles"]=world.particles->stats();if(world.physics3d)profile["physics"]=world.physics3d->stats();if(renderer)profile["renderer"]=renderer->diagnostics();
            }catch(const std::exception& e){logger.error(e.what());if(!dev){result=1;break;}paused=true;}
            if(headless && dev)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(const std::exception& e){logger.error(e.what());result=1;}
    if(!shutdown())result=1;logger.write("INFO",result==0?"Runtime stopped":"Runtime stopped with errors");return result;
}
static fs::path savePath(const Runtime& r,const std::string& name) {
    if(name.empty() || fs::u8path(name).filename()!=fs::u8path(name) || name=="." || name=="..")throw std::runtime_error("Save name must be a filename");
    auto base=r.config.data.value("save_directory","saves");auto folder=storagePath(r.config,base);fs::create_directories(folder);return folder/fs::u8path(name+".json");
}
void Runtime::save(const std::string& name,py::object value) {
    if(tearingDown)return;
    if(reloading){auto snapshot=fromPython(value);persistence.push_back(py::cpp_function([this,name,snapshot](){save(name,pythonValue(snapshot));}));return;}
    auto file=savePath(*this,name),temp=file;temp += ".tmp";auto data=fromPython(value);
    {std::ofstream out(temp);if(!out)throw std::runtime_error("Cannot write save: "+temp.u8string());out<<data.dump(2);out.flush();if(!out)throw std::runtime_error("Save write failed");}
#ifdef _WIN32
    if(!MoveFileExW(temp.wstring().c_str(),file.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace save file");
#else
    fs::rename(temp,file);
#endif
}
py::object Runtime::load(const std::string& name,py::object fallback){auto file=savePath(*this,name);return fs::exists(file)?pythonValue(readJson(file)):fallback;}
}
