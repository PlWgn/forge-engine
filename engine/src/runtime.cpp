#include <forge/engine.hpp>
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
static Runtime& rt(){if(!active)throw std::runtime_error("Engine runtime is not active");return *active;}
static std::array<float,3> tuple(glm::vec3 v){return {v.x,v.y,v.z};}
static glm::vec3 vector(std::array<float,3> v){return {v[0],v[1],v[2]};}
static py::object toPython(const Json& j){return py::module_::import("json").attr("loads")(j.dump());}
PYBIND11_EMBEDDED_MODULE(forge,m) {
    m.doc()="Forge native engine modules. Coordinates: 2D pixels; 3D world units.";
    py::class_<Entity,std::shared_ptr<Entity>>(m,"Entity")
        .def_readonly("id",&Entity::id).def_readwrite("name",&Entity::name)
        .def_readwrite("text",&Entity::text).def_readwrite("font_size",&Entity::fontSize)
        .def_readwrite("visible",&Entity::visible).def_readonly("alive",&Entity::alive)
        .def_readwrite("dynamic",&Entity::dynamic).def_readwrite("trigger",&Entity::trigger)
        .def_readwrite("texture",&Entity::texture).def_readwrite("model",&Entity::model)
        .def_property("mass",[](Entity& e){return e.mass;},[](Entity& e,float v){if(v<=0)throw std::runtime_error("mass must be positive");e.mass=v;})
        .def_property("position",[](Entity& e){return tuple(e.position);},[](Entity& e,std::array<float,3> v){e.position=vector(v);})
        .def_property("rotation",[](Entity& e){return tuple(e.rotation);},[](Entity& e,std::array<float,3> v){e.rotation=vector(v);})
        .def_property("scale",[](Entity& e){return tuple(e.scale);},[](Entity& e,std::array<float,3> v){e.scale=vector(v);})
        .def_property("velocity",[](Entity& e){return tuple(e.velocity);},[](Entity& e,std::array<float,3> v){e.velocity=vector(v);})
        .def_property("collider",[](Entity& e){return tuple(e.collider);},[](Entity& e,std::array<float,3> v){e.collider=vector(v);})
        .def_property("color",[](Entity& e){return std::array<float,4>{e.color.r,e.color.g,e.color.b,e.color.a};},[](Entity& e,std::array<float,4> v){e.color={v[0],v[1],v[2],v[3]};})
        .def_property("data",[](Entity& e){return toPython(e.data);},[](Entity& e,py::object v){e.data=fromPython(v);})
        .def("destroy",[](Entity& e){e.alive=false;})
        .def("move",[](Entity& e,float x,float y,float z){e.position+=glm::vec3(x,y,z);},py::arg("x"),py::arg("y"),py::arg("z")=0)
        .def("impulse",[](Entity& e,float x,float y,float z){e.velocity+=glm::vec3(x,y,z)/e.mass;},py::arg("x"),py::arg("y"),py::arg("z")=0);
    m.def("spawn",[](py::dict d){return rt().world.spawn(fromPython(d));});
    m.def("find",[](const std::string& id){return rt().world.find(id);});
    m.def("entities",[](){std::vector<std::shared_ptr<Entity>> result;for(auto& e:rt().world.entities)if(e->alive)result.push_back(e);return result;});
    m.def("change_scene",[](const std::string& path){rt().config.asset("scenes",path);rt().pendingScene=path;});
    m.def("quit",[](){rt().running=false;});
    m.def("dt",[](){return rt().dt;});m.def("time",[](){return rt().time;});
    m.def("is_dev",[](){return rt().dev;});m.def("is_headless",[](){return rt().headless;});
    m.def("key_down",[](const std::string& k){return rt().renderer?rt().renderer->key(k):false;});
    m.def("key_pressed",[](const std::string& k){return rt().renderer?rt().renderer->pressed(k):false;});
    m.def("mouse_down",[](int b){return rt().renderer?rt().renderer->mouse(b):false;},py::arg("button")=0);
    m.def("mouse_position",[](){auto v=rt().renderer?rt().renderer->cursor():glm::vec2(0);return std::array<float,2>{v.x,v.y};});
    m.def("mouse_delta",[](){auto v=rt().renderer?rt().renderer->delta():glm::vec2(0);return std::array<float,2>{v.x,v.y};});
    m.def("capture_mouse",[](bool e){if(rt().renderer)rt().renderer->capture(e);});
    m.def("window_size",[](){return std::array<int,2>{rt().world.width,rt().world.height};});
    m.def("set_camera",[](std::array<float,3> p,std::array<float,3> target){rt().world.cameraPosition=vector(p);rt().world.cameraTarget=vector(target);},py::arg("position"),py::arg("target")=std::array<float,3>{0,0,0});
    m.def("camera_position",[](){return tuple(rt().world.cameraPosition);});
    m.def("set_gravity",[](std::array<float,3> v){rt().world.gravity=vector(v);});
    m.def("set_mode",[](const std::string& mode){if(mode!="2d" && mode!="3d")throw std::runtime_error("Mode must be 2d or 3d");rt().world.is3d=mode=="3d";});
    m.def("set_background",[](std::array<float,4> v){rt().world.background={v[0],v[1],v[2],v[3]};});
    m.def("raycast",[](std::array<float,3> origin,std::array<float,3> direction,float distance){return rt().world.raycast(vector(origin),vector(direction),distance);},py::arg("origin"),py::arg("direction"),py::arg("distance")=1000);
    m.def("overlaps",[](const Entity& a,const Entity& b){return rt().world.overlaps(a,b);});
    m.def("play_sound",[](const std::string& path,bool loop,float volume){if(!rt().headless)rt().audio.play(rt().config.asset("audio",path),loop,volume);},py::arg("file"),py::arg("loop")=false,py::arg("volume")=1);
    m.def("screenshot",[](const std::string& file){if(rt().renderer)rt().renderer->screenshot(rt().config.resolve(file));else logger.write("WARN","Screenshot requires a window");});
    m.def("stop_sounds",[](){rt().audio.stop();});
    m.def("log",[](py::object value,const std::string& level){if(level!="INFO" && level!="WARN" && level!="ERROR" && level!="DEBUG")throw std::runtime_error("Unknown log level");logger.write(level,py::str(value));},py::arg("message"),py::arg("level")="INFO");
    m.def("open_log",[](){logger.open();});
    m.def("asset_path",[](const std::string& group,const std::string& file){return rt().config.asset(group,file).u8string();});
    m.def("project_path",[](const std::string& file){return rt().config.resolve(file).u8string();},py::arg("file")=".");
    m.def("settings",[](){return toPython(rt().config.data);});
    m.def("save",[](const std::string& name,py::object value){rt().save(name,value);});
    m.def("load",[](const std::string& name,py::object fallback){return rt().load(name,fallback);},py::arg("name"),py::arg("default")=py::none());
    for(auto init:nativeModules())init(m);
}
Runtime::Runtime(Config c,bool development,bool noWindow):config(std::move(c)),dev(development),headless(noWindow){world.config=&config;active=this;}
Runtime::~Runtime(){scripts.clear();startup.clear();active=nullptr;}
py::object Runtime::loadModule(const fs::path& file) {
    if(!fs::is_regular_file(file))throw std::runtime_error("Script not found: "+file.u8string());
    auto types=py::module_::import("types");auto module=types.attr("ModuleType")("_forge_script_"+std::to_string(++moduleIndex));
    module.attr("__file__")=file.u8string();auto globals=module.attr("__dict__");
    std::ifstream stream(file,std::ios::binary);std::string source{std::istreambuf_iterator<char>(stream),{}};
    auto code=py::module_::import("builtins").attr("compile")(py::bytes(source),file.u8string(),"exec");
    py::module_::import("builtins").attr("exec")(code,globals);return module;
}
void Runtime::attach(std::shared_ptr<Entity> e) {
    for(auto& item:e->scripts) {
        std::string file=item.is_string()?item.get<std::string>():item.at("file").get<std::string>();
        auto module=loadModule(config.asset("scripts",file));auto properties=toPython(item.is_string()?Json::object():item.value("properties",Json::object()));
        auto instance=py::hasattr(module,"Behavior")?module.attr("Behavior")(e,properties):module;
        scripts.push_back({module,instance,e});
        if(py::hasattr(instance,"on_start"))instance.attr("on_start")();
    }
}
void Runtime::loadScene(const std::string& name) {
    // Keep a usable world until the replacement scene and its Python scripts initialize.
    World replacement;replacement.config=&config;replacement.width=world.width;replacement.height=world.height;replacement.load(name);
    auto oldWorld=std::move(world);auto oldScripts=std::move(scripts);world=std::move(replacement);scripts.clear();
    try {
        py::object sceneModule;
        auto sceneFile=config.asset("scenes",name);
        if(sceneFile.extension()==".py") {
            sceneModule=loadModule(sceneFile);
            if(py::hasattr(sceneModule,"build")) {
                auto result=sceneModule.attr("build")();
                if(!result.is_none()) {
                    auto j=fromPython(result);
                    if(!j.is_object())throw std::runtime_error("Scene build() must return dict or None");
                    world.scene=j;auto mode=j.value("mode","2d");if(mode!="2d" && mode!="3d")throw std::runtime_error("Scene mode must be 2d or 3d");world.is3d=mode=="3d";
                    world.gravity=world.is3d?glm::vec3(0,-9.81f,0):glm::vec3(0,980,0);
                    if(j.contains("gravity"))world.gravity=vector(j["gravity"].get<std::array<float,3>>());
                    if(j.contains("background")){auto v=j["background"].get<std::array<float,4>>();world.background={v[0],v[1],v[2],v[3]};}
                    auto camera=j.value("camera",Json::object());if(camera.contains("position"))world.cameraPosition=vector(camera["position"].get<std::array<float,3>>());if(camera.contains("target"))world.cameraTarget=vector(camera["target"].get<std::array<float,3>>());world.fov=camera.value("fov",60.0f);if(world.fov<=0 || world.fov>=179)throw std::runtime_error("Camera fov must be between 0 and 179");
                    for(auto& data:j.value("entities",Json::array()))world.spawn(data);
                }
            }
        } else if(world.scene.contains("script"))sceneModule=loadModule(config.asset("scenes",world.scene["script"]));
        if(sceneModule && !sceneModule.is_none()) {scripts.push_back({sceneModule,sceneModule,{}});if(py::hasattr(sceneModule,"on_start"))sceneModule.attr("on_start")();}
        auto initial=world.entities;for(auto& e:initial)attach(e);
    } catch(...) {for(auto& e:world.entities)e->alive=false;scripts.clear();world=std::move(oldWorld);scripts=std::move(oldScripts);throw;}
    for(auto& script:oldScripts)if(py::hasattr(script.instance,"on_destroy"))try{script.instance.attr("on_destroy")();}catch(const std::exception& ex){logger.error(ex.what());}
    for(auto& e:oldWorld.entities)e->alive=false;
    currentScene=name;logger.write("INFO","Scene loaded: "+name);
}
std::map<fs::path,fs::file_time_type> Runtime::snapshot() const {
    std::map<fs::path,fs::file_time_type> result;result[config.file]=fs::last_write_time(config.file);
    for(auto& [group,path]:config.paths)if(fs::exists(path))for(auto& entry:fs::recursive_directory_iterator(path))if(entry.is_regular_file() && entry.path().extension()!=".pyc" && entry.path().u8string().find("__pycache__")==std::string::npos)result[entry.path()]=entry.last_write_time();
    return result;
}
bool Runtime::changed(){auto now=snapshot();if(now==watched)return false;watched=std::move(now);return true;}
void Runtime::start() {
    auto sys=py::module_::import("sys");sys.attr("dont_write_bytecode")=true;
    for(auto group:{"modules","scripts","scenes"})sys.attr("path").attr("insert")(0,config.paths.at(group).u8string());
    for(auto& path:config.data.value("python_paths",Json::array()))sys.attr("path").attr("insert")(0,config.resolve(path.get<std::string>()).u8string());
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
    if(!headless){renderer=std::make_unique<Renderer>();renderer->init(config,world);}
    for(auto& item:config.data.value("startup_scripts",Json::array())){auto module=loadModule(config.asset("scripts",item.get<std::string>()));startup.push_back(module);if(py::hasattr(module,"on_start"))module.attr("on_start")();}
    loadScene(config.entry());watched=snapshot();
}
bool Runtime::shutdown(){bool failed=false;
    for(auto& script:scripts)if(py::hasattr(script.instance,"on_destroy"))try{script.instance.attr("on_destroy")();}catch(const std::exception& e){logger.error(e.what());failed=true;}
    for(auto& module:startup)if(py::hasattr(module,"on_destroy"))try{module.attr("on_destroy")();}catch(const std::exception& e){logger.error(e.what());failed=true;}
    for(auto name:{"stdout","stderr"})try{py::module_::import("sys").attr(name).attr("flush")();}catch(...){}
    scripts.clear();startup.clear();audio.stop();renderer.reset();return !failed;
}
int Runtime::run(int frames) {
    auto previous=std::chrono::steady_clock::now();double reloadClock=0;float physicsAccumulator=0;bool paused=false,pausedRenderFailed=false;int result=0;
    try {start();
        for(int frame=0;running && (frames<0 || frame<frames);++frame) {
            auto now=std::chrono::steady_clock::now();dt=headless?1.0f/60:std::min(.1f,std::chrono::duration<float>(now-previous).count());previous=now;time+=dt;reloadClock+=dt;
            if(renderer){renderer->poll();if(renderer->closing())break;}
            if(dev && reloadClock>=.3){reloadClock=0;if(changed()) {
                try {
                    auto importlib=py::module_::import("importlib");importlib.attr("invalidate_caches")();
                    // Imported Python helpers also reload; clear only modules under game code directories.
                    auto sys=py::module_::import("sys");auto modules=sys.attr("modules").cast<py::dict>();py::list remove;
                    for(auto item:modules)if(py::hasattr(item.second,"__file__") && !item.second.attr("__file__").is_none()) {
                        auto file=fs::weakly_canonical(fs::u8path(item.second.attr("__file__").cast<std::string>()));
                        for(auto group:{"modules","scripts","scenes"}) {auto relative=file.lexically_relative(config.paths.at(group));if(!relative.empty() && *relative.begin()!="..") {remove.append(item.first);break;}}
                    }
                    for(auto item:remove)modules.attr("pop")(item,py::none());
                    // Settings are reread; window settings take effect on the next launch.
                    auto previousEntry=config.entry();auto updated=Config::load(config.file);updated.validate();config=std::move(updated);world.config=&config;
                    for(auto group:{"modules","scripts","scenes"})sys.attr("path").attr("insert")(0,config.paths.at(group).u8string());
                    if(renderer)renderer->invalidate();
                    loadScene(previousEntry==config.entry()?currentScene:config.entry());
                    paused=false;pausedRenderFailed=false;logger.write("INFO","Hot reload complete");
                }catch(const std::exception& e){logger.error(e.what());paused=true;logger.write("WARN","Development paused. Edit a watched file to retry.");}
            }}
            if(paused){if(renderer && !pausedRenderFailed)try{renderer->render(world);}catch(const std::exception& e){logger.error(e.what());pausedRenderFailed=true;}if(headless)std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            try {
                if(!pendingScene.empty()){auto name=pendingScene;pendingScene.clear();loadScene(name);physicsAccumulator=0;}
                // New objects can be spawned by lifecycle callbacks without invalidating iteration.
                auto current=scripts;
                for(auto& module:startup)if(py::hasattr(module,"on_update"))module.attr("on_update")(dt);
                for(auto& script:current)if((!script.entity || script.entity->alive) && py::hasattr(script.instance,"on_update"))script.instance.attr("on_update")(dt);
                physicsAccumulator+=dt;
                while(physicsAccumulator>=1.0f/120){
                    auto old=world.contacts;world.physics(1.0f/120);physicsAccumulator-=1.0f/120;
                    for(auto& script:current)if(script.entity && script.entity->alive) {
                        auto id=script.entity->id;
                        for(auto& pair:world.contacts)if(!old.count(pair) && (pair.first==id || pair.second==id) && py::hasattr(script.instance,"on_collision"))script.instance.attr("on_collision")(world.find(pair.first==id?pair.second:pair.first));
                        for(auto& pair:old)if(!world.contacts.count(pair) && (pair.first==id || pair.second==id) && py::hasattr(script.instance,"on_collision_exit"))script.instance.attr("on_collision_exit")(world.find(pair.first==id?pair.second:pair.first));
                    }
                }
                for(auto it=scripts.begin();it!=scripts.end();)if(it->entity && !it->entity->alive){if(py::hasattr(it->instance,"on_destroy"))it->instance.attr("on_destroy")();it=scripts.erase(it);}else ++it;
                // Track all attached entities, including ones whose script list is empty.
                for(auto& e:world.entities)if(e->alive && !e->scripts.empty() && !e->data.value("_forge_attached",false)) {
                    bool exists=std::any_of(scripts.begin(),scripts.end(),[&](auto& s){return s.entity==e;});if(!exists)attach(e);e->data["_forge_attached"]=true;
                }
                world.entities.erase(std::remove_if(world.entities.begin(),world.entities.end(),[](auto& e){return !e->alive;}),world.entities.end());
                if(renderer)renderer->render(world);
            }catch(const std::exception& e){logger.error(e.what());if(!dev){result=1;break;}paused=true;}
            if(headless && dev)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(const std::exception& e){logger.error(e.what());result=1;}
    if(!shutdown())result=1;logger.write("INFO",result==0?"Runtime stopped":"Runtime stopped with errors");return result;
}
static fs::path savePath(const Runtime& r,const std::string& name) {
    if(name.empty() || fs::u8path(name).filename()!=fs::u8path(name) || name=="." || name=="..")throw std::runtime_error("Save name must be a filename");
    auto base=r.config.data.value("save_directory","saves");auto folder=r.config.resolve(base);fs::create_directories(folder);return folder/fs::u8path(name+".json");
}
void Runtime::save(const std::string& name,py::object value) {
    auto file=savePath(*this,name),temp=file;temp += ".tmp";auto data=fromPython(value);
    {std::ofstream out(temp);if(!out)throw std::runtime_error("Cannot write save: "+temp.u8string());out<<data.dump(2);out.flush();if(!out)throw std::runtime_error("Save write failed");}
#ifdef _WIN32
    if(!MoveFileExW(temp.wstring().c_str(),file.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace save file");
#else
    fs::rename(temp,file);
#endif
}
py::object Runtime::load(const std::string& name,py::object fallback){auto file=savePath(*this,name);return fs::exists(file)?toPython(readJson(file)):fallback;}
}
