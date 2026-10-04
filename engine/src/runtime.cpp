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
static Runtime& rt(){if(!active)throw std::runtime_error("Engine runtime is not active");return *active;}
static std::array<float,3> tuple(glm::vec3 v){return {v.x,v.y,v.z};}
static glm::vec3 vector(std::array<float,3> v,const std::string& field="vector"){return {finiteNumber(v[0],field),finiteNumber(v[1],field),finiteNumber(v[2],field)};}
static glm::vec4 color(std::array<float,4> v,const std::string& field="color"){return {finiteNumber(v[0],field),finiteNumber(v[1],field),finiteNumber(v[2],field),finiteNumber(v[3],field)};}
static float positive(float value,const std::string& field){finiteNumber(value,field);if(value<=0)throw std::runtime_error(field+" must be positive");return value;}
static void transformMutation(Entity& e,const Entity& next){auto old=e;e.position=next.position;e.rotation=next.rotation;e.scale=next.scale;try{rt().world.syncTransforms();}catch(...){e=std::move(old);rt().world.syncTransforms();throw;}}
static void physicsMutation(const Entity& e){if(rigidPhysics(rt().world) && rt().world.activeCollider(e)){auto body=e;if(!e.parent.empty()){auto parent=rt().world.find(e.parent);if(parent)body.position=glm::vec3((glm::dmat4(parent->worldMatrix)*glm::dmat4(composeTransform(e)))[3]);}validatePhysicsEntity(body);}}
static py::object toPython(const Json& j){return py::module_::import("json").attr("loads")(j.dump());}
PYBIND11_EMBEDDED_MODULE(forge,m) {
    m.attr("__version__")=FORGE_VERSION;
    m.doc()="Forge native engine modules. Coordinates: 2D pixels; 3D world units.";
    py::class_<LocalizedText>(m,"LocalizedText")
        .def(py::init([](const std::string& key,py::dict params){return LocalizedText{key,fromPython(params)};}),py::arg("key"),py::arg("params")=py::dict())
        .def_readwrite("key",&LocalizedText::key)
        .def_property("params",[](LocalizedText& value){return toPython(value.params);},[](LocalizedText& value,py::dict params){value.params=fromPython(params);})
        .def("__str__",[](LocalizedText& value){return rt().localization.translate(value.key,value.params);});
    m.def("message",[](const std::string& key,py::kwargs params){return LocalizedText{key,fromPython(params)};},py::arg("key"));
    m.def("tr",[](const std::string& key,py::kwargs params){return rt().localization.translate(key,fromPython(params));},py::arg("key"));
    m.def("language",[](){return rt().localization.language;});
    m.def("available_languages",[](){return toPython(rt().localization.languages());});
    m.def("localization_revision",[](){return rt().localization.revision;});
    m.def("has_translation",[](const std::string& key,const std::string& language,bool fallback){return rt().localization.has(key,language,fallback);},py::arg("key"),py::arg("language")="",py::arg("fallback")=true);
    m.def("set_language",[](const std::string& language,bool persist){auto previous=rt().localization;try{rt().localization.select(language,persist);rt().refreshLocalizedEntities();}catch(...){rt().localization=std::move(previous);throw;}},py::arg("language"),py::arg("persist")=true);
    py::class_<Entity,std::shared_ptr<Entity>>(m,"Entity")
        .def_readonly("id",&Entity::id).def_readwrite("name",&Entity::name)
        .def_property("text",[](Entity& e){return e.textKey.empty()?e.text:rt().localization.translate(e.textKey,e.textParams);},[](Entity& e,py::object value){
            if(py::isinstance<LocalizedText>(value)){auto message=value.cast<LocalizedText>();auto text=rt().localization.translate(message.key,message.params);e.textKey=message.key;e.textParams=message.params;e.text=text;}
            else{e.text=py::str(value);e.textKey.clear();e.textParams=Json::object();}
        })
        .def_readwrite("text_key",&Entity::textKey)
        .def_property("text_params",[](Entity& e){return toPython(e.textParams);},[](Entity& e,py::dict params){e.textParams=fromPython(params);})
        .def("set_localized_text",[](Entity& e,const std::string& key,py::dict params){auto data=fromPython(params);auto text=rt().localization.translate(key,data);e.textKey=key;e.textParams=data;e.text=text;},py::arg("key"),py::arg("params")=py::dict())
        .def_property("font_size",[](Entity& e){return e.fontSize;},[](Entity& e,float value){e.fontSize=positive(value,"font_size");})
        .def_readwrite("visible",&Entity::visible).def_readonly("alive",&Entity::alive)
        .def_property("dynamic",[](Entity& e){return e.dynamic;},[](Entity& e,bool value){if(value && !e.parent.empty())throw std::runtime_error("Dynamic bodies must be hierarchy roots");auto next=e;next.dynamic=value;physicsMutation(next);e.dynamic=value;}).def_readwrite("trigger",&Entity::trigger)
        .def_readwrite("texture",&Entity::texture).def_readwrite("model",&Entity::model)
        .def_property("material_properties",[](Entity& e){return toPython(e.materialData);},[](Entity& e,py::dict properties){e.materialData=validateMaterial(rt().config,fromPython(properties));})
        .def_property("uv",[](Entity& e){return std::array<float,4>{e.uv.x,e.uv.y,e.uv.z,e.uv.w};},[](Entity& e,std::array<float,4> v){validateEntity(rt().config,Json{{"uv",v}});e.uv=color(v,"uv");})
        .def_readwrite("layer",&Entity::layer).def_readwrite("casts_shadow",&Entity::castsShadow)
        .def_property("uniforms",[](Entity& e){return toPython(e.uniforms);},[](Entity& e,py::dict values){auto data=fromPython(values);validateRenderSettings(Json{{"uniforms",data}});e.uniforms=std::move(data);})
        .def_property("animation_time",[](Entity& e){return e.animationTime;},[](Entity& e,float value){e.animationTime=finiteNumber(value,"animation time");})
        .def_property_readonly("animation",[](Entity& e){return e.animation;})
        .def_property_readonly("animation_playing",[](Entity& e){return e.animationPlaying;})
        .def_property("mass",[](Entity& e){return e.mass;},[](Entity& e,float v){auto next=e;next.mass=checkedMass(v);physicsMutation(next);e.mass=next.mass;})
        .def_property("position",[](Entity& e){return tuple(e.position);},[](Entity& e,std::array<float,3> v){auto next=e;next.position=vector(v,"position");physicsMutation(next);transformMutation(e,next);})
        .def_property("rotation",[](Entity& e){return tuple(e.rotation);},[](Entity& e,std::array<float,3> v){auto next=e;next.rotation=vector(v,"rotation");physicsMutation(next);transformMutation(e,next);})
        .def_property("scale",[](Entity& e){return tuple(e.scale);},[](Entity& e,std::array<float,3> v){auto next=e;next.scale=vector(v,"scale");transformMutation(e,next);})
        .def_property("velocity",[](Entity& e){return tuple(e.velocity);},[](Entity& e,std::array<float,3> v){auto next=e;next.velocity=vector(v,"velocity");physicsMutation(next);e.velocity=next.velocity;})
        .def_property("angular_velocity",[](Entity& e){return tuple(e.angularVelocity);},[](Entity& e,std::array<float,3> v){auto next=e;next.angularVelocity=vector(v,"angular_velocity");if(rigidPhysics(rt().world) && rt().world.activeCollider(next))validatePhysicsEntity(next);e.angularVelocity=next.angularVelocity;})
        .def_property("collider",[](Entity& e){return tuple(e.collider);},[](Entity& e,std::array<float,3> v){auto next=e;next.collider=vector(v,"collider");physicsMutation(next);e.collider=next.collider;})
        .def_property_readonly("parent",[](Entity& e){return rt().world.find(e.parent);})
        .def_property("local_position",[](Entity& e){return tuple(e.position);},[](Entity& e,std::array<float,3> v){auto next=e;next.position=vector(v,"local_position");physicsMutation(next);transformMutation(e,next);})
        .def_property("local_rotation",[](Entity& e){return tuple(e.rotation);},[](Entity& e,std::array<float,3> v){auto next=e;next.rotation=vector(v,"local_rotation");physicsMutation(next);transformMutation(e,next);})
        .def_property("local_scale",[](Entity& e){return tuple(e.scale);},[](Entity& e,std::array<float,3> v){auto next=e;next.scale=vector(v,"local_scale");transformMutation(e,next);})
        .def_property("world_position",[](Entity& e){rt().world.syncTransforms();return tuple(rt().world.worldPosition(e));},[](Entity& e,std::array<float,3> v){rt().world.setWorldPosition(e,vector(v,"world_position"));})
        .def_property_readonly("world_rotation",[](Entity& e){rt().world.syncTransforms();return tuple(e.worldRotation);})
        .def_property_readonly("world_matrix",[](Entity& e){rt().world.syncTransforms();py::list rows;for(int r=0;r<4;++r){py::list row;for(int c=0;c<4;++c)row.append(e.worldMatrix[c][r]);rows.append(row);}return rows;})
        .def("set_parent",[](Entity& e,py::object parent,bool keep){std::string id;if(!parent.is_none())id=py::isinstance<py::str>(parent)?parent.cast<std::string>():parent.cast<Entity&>().id;rt().world.reparent(e,id,keep);},py::arg("parent")=py::none(),py::arg("keep_world")=false)
        .def("children",[](Entity& e,bool recursive){return rt().world.children(e,recursive);},py::arg("recursive")=false)
        .def_property("color",[](Entity& e){return std::array<float,4>{e.color.r,e.color.g,e.color.b,e.color.a};},[](Entity& e,std::array<float,4> v){e.color=color(v);})
        .def_property("data",[](Entity& e){return toPython(e.data);},[](Entity& e,py::object v){e.data=fromPython(v);})
        .def_property("clip",[](Entity& e)->py::object{if(!e.clipped)return py::none();return py::cast(std::array<float,4>{e.clip.x,e.clip.y,e.clip.z,e.clip.w});},[](Entity& e,py::object v){if(v.is_none()){e.clipped=false;return;}auto next=color(v.cast<std::array<float,4>>(),"clip");e.clip=next;e.clipped=true;})
        .def("destroy",[](Entity& e,bool children){rt().world.destroy(e,children);},py::arg("children")=true)
        .def("move",[](Entity& e,float x,float y,float z){auto next=e;next.position=vector(tuple(e.position+vector({x,y,z},"move")),"position");physicsMutation(next);transformMutation(e,next);},py::arg("x"),py::arg("y"),py::arg("z")=0)
        .def("impulse",[](Entity& e,float x,float y,float z){auto next=e;next.velocity=vector(tuple(e.velocity+vector({x,y,z},"impulse")/e.mass),"velocity");physicsMutation(next);e.velocity=next.velocity;},py::arg("x"),py::arg("y"),py::arg("z")=0);
    m.def("spawn",[](py::dict d){auto e=rt().world.spawn(fromPython(d));try{if(!e->textKey.empty())e->text=rt().localization.translate(e->textKey,e->textParams);}catch(...){e->alive=false;throw;}return e;});
    m.def("find",[](const std::string& id){return rt().world.find(id);});
    m.def("entities",[](){std::vector<std::shared_ptr<Entity>> result;for(auto& e:rt().world.entities)if(e->alive)result.push_back(e);return result;});
    m.def("change_scene",[](const std::string& path){rt().config.asset("scenes",path);rt().pendingScene=path;});
    m.def("quit",[](){rt().running=false;});
    m.def("dt",[](){return rt().dt;});m.def("time",[](){return rt().time;});
    m.def("is_dev",[](){return rt().dev;});m.def("is_headless",[](){return rt().headless;});
    m.def("key_down",[](std::string k){for(auto& c:k)c=char(std::toupper(static_cast<unsigned char>(c)));auto keys=rt().inputFrame.value("keys",Json::array());return std::find(keys.begin(),keys.end(),k)!=keys.end();});
    m.def("key_pressed",[](std::string k){for(auto& c:k)c=char(std::toupper(static_cast<unsigned char>(c)));auto keys=rt().inputFrame.value("pressed",Json::array());return std::find(keys.begin(),keys.end(),k)!=keys.end();});
    m.def("mouse_down",[](int b){if(b<0 || b>7)throw std::runtime_error("Mouse button must be 0..7");auto buttons=rt().inputFrame.value("buttons",Json::array());return std::find(buttons.begin(),buttons.end(),b)!=buttons.end();},py::arg("button")=0);
    m.def("mouse_position",[](){return rt().inputFrame.value("position",std::array<float,2>{0,0});});
    m.def("mouse_scroll",[](){return rt().inputFrame.value("scroll",std::array<float,2>{0,0});});
    m.def("mouse_delta",[](){return rt().inputFrame.value("delta",std::array<float,2>{0,0});});
    m.def("capture_mouse",[](bool e){if(rt().renderer && !rt().tearingDown)rt().renderer->capture(e);});
    m.def("window_size",[](){return std::array<int,2>{rt().world.width,rt().world.height};});
    m.def("set_camera",[](std::array<float,3> p,std::array<float,3> target){auto position=vector(p,"camera.position"),destination=vector(target,"camera.target");rt().world.cameraPosition=position;rt().world.cameraTarget=destination;},py::arg("position"),py::arg("target")=std::array<float,3>{0,0,0});
    m.def("camera_position",[](){return tuple(rt().world.cameraPosition);});
    m.def("set_gravity",[](std::array<float,3> v){rt().world.gravity=vector(v);});
    m.def("set_mode",[](const std::string& mode){if(mode!="2d" && mode!="3d")throw std::runtime_error("Mode must be 2d or 3d");rt().world.is3d=mode=="3d";});
    m.def("set_background",[](std::array<float,4> v){rt().world.background=color(v,"background");});
    m.def("raycast",[](std::array<float,3> origin,std::array<float,3> direction,float distance){return rt().world.raycast(vector(origin),vector(direction),finiteNumber(distance,"raycast distance"));},py::arg("origin"),py::arg("direction"),py::arg("distance")=1000);
    m.def("overlaps",[](const Entity& a,const Entity& b){return rt().world.overlaps(a,b);});
    m.def("play_sound",[](const std::string& path,bool loop,float volume,const std::string& channel,float fade,py::dict options){return rt().audio.play(rt().config.asset("audio",path),loop,volume,channel,fade,fromPython(options));},py::arg("file"),py::arg("loop")=false,py::arg("volume")=1,py::arg("channel")="sfx",py::arg("fade")=0,py::arg("options")=py::dict());
    m.def("stop_sound",[](unsigned id,float fade){rt().audio.stop(id,"",fade);},py::arg("id"),py::arg("fade")=0);
    m.def("stop_channel",[](const std::string& channel,float fade){rt().audio.stop(0,channel,fade);},py::arg("channel"),py::arg("fade")=0);
    m.def("channel_sounds",[](const std::string& channel){return rt().audio.ids(channel);});
    m.def("channel_volume",[](const std::string& channel){return rt().audio.volume(channel);});
    m.def("set_channel_volume",[](const std::string& channel,float value){rt().audio.volume(channel,value);});
    m.def("fade_sound",[](unsigned id,float volume,float seconds,bool stop){rt().audio.fade(id,volume,seconds,stop);},py::arg("id"),py::arg("volume"),py::arg("seconds"),py::arg("stop_after")=false);
    m.def("sound_volume",[](unsigned id){return rt().audio.gain(id);});
    m.def("set_sound_volume",[](unsigned id,float value){rt().audio.fade(id,value,0);});
    m.def("sound_playing",[](unsigned id){return rt().audio.playing(id);});
    m.def("pause_sound",[](unsigned id,bool paused){rt().audio.pause(id,paused);},py::arg("id"),py::arg("paused")=true);
    m.def("measure_text",[](const std::string& text,float size){return measureText(rt().config,text,size);},py::arg("text"),py::arg("size")=24);
    m.def("set_paused",[](bool value){rt().gamePaused=value;});m.def("is_paused",[](){return rt().gamePaused;});
    m.def("on_frame",[](py::object callback,bool persistent){if(!PyCallable_Check(callback.ptr()))throw std::runtime_error("Frame listener must be callable");unsigned id=++rt().listenerIndex;rt().listeners.push_back({id,callback,persistent});return id;},py::arg("callback"),py::arg("persistent")=false);
    m.def("remove_listener",[](unsigned id){auto& list=rt().listeners;list.erase(std::remove_if(list.begin(),list.end(),[&](auto& l){return l.id==id;}),list.end());});
    m.def("user_screenshot",[](const std::string& file){if(rt().tearingDown)return;if(rt().renderer)rt().renderer->screenshot(userPath(rt().config,"captures",file));else logger.write("WARN","Screenshot requires a window");});
    m.def("screenshot",[](const std::string& file){if(rt().tearingDown)return;if(rt().renderer)rt().renderer->screenshot(rt().config.resolve(file));else logger.write("WARN","Screenshot requires a window");});
    m.def("stop_sounds",[](){rt().audio.stop();});
    m.def("log",[](py::object value,const std::string& level){if(level!="INFO" && level!="WARN" && level!="ERROR" && level!="DEBUG")throw std::runtime_error("Unknown log level");logger.write(level,py::str(value));},py::arg("message"),py::arg("level")="INFO");
    m.def("open_log",[](){logger.open();});
    m.def("asset_path",[](const std::string& group,const std::string& file){return rt().config.asset(group,file).u8string();});
    m.def("project_path",[](const std::string& file){return rt().config.resolve(file).u8string();},py::arg("file")=".");
    m.def("settings",[](){return toPython(rt().config.data);});
    m.def("save",[](const std::string& name,py::object value){rt().save(name,value);});
    m.def("load",[](const std::string& name,py::object fallback){return rt().load(name,fallback);},py::arg("name"),py::arg("default")=py::none());
    bindFeatures(m);
    for(auto init:nativeModules())init(m);
}
Runtime::Runtime(Config c,bool development,bool noWindow):config(std::move(c)),sceneConfig(config),assets(config),dev(development),headless(noWindow){if(active)throw std::runtime_error("Only one Python runtime can be active in this process");world.config=&config;audio.silent=headless;localization.load(config,"",true);active=this;}
Runtime::~Runtime(){listeners.clear();scripts.clear();startup.clear();if(active==this)active=nullptr;}
py::object Runtime::loadModule(const fs::path& file) {
    if(!fs::is_regular_file(file))throw std::runtime_error("Script not found: "+file.u8string());
    auto types=py::module_::import("types");auto module=types.attr("ModuleType")("_forge_script_"+std::to_string(++moduleIndex));
    module.attr("__file__")=file.u8string();auto globals=module.attr("__dict__");
    std::ifstream stream(file,std::ios::binary);std::string source{std::istreambuf_iterator<char>(stream),{}};
    auto code=py::module_::import("builtins").attr("compile")(py::bytes(source),file.u8string(),"exec");
    py::module_::import("builtins").attr("exec")(code,globals);return module;
}
void Runtime::attach(std::shared_ptr<Entity> e) {
    if(!e->alive || e->attached)return;
    e->attached=true;
    auto descriptors=e->scripts;
    for(auto& item:descriptors) {
        if(!e->alive)break;
        std::string file=item.is_string()?item.get<std::string>():item.at("file").get<std::string>();
        auto module=loadModule(config.asset("scripts",file));auto properties=toPython(item.is_string()?Json::object():item.value("properties",Json::object()));
        auto instance=py::hasattr(module,"Behavior")?module.attr("Behavior")(e,properties):module;
        scripts.push_back({module,instance,e});
        if(py::hasattr(instance,"on_start"))instance.attr("on_start")();
    }
}
void Runtime::attachPending(){
    // Each wave owns its pointers; callbacks may freely reallocate world.entities.
    unsigned initializedCount=0;
    for(;;){std::vector<std::shared_ptr<Entity>> pending;for(auto& e:world.entities)if(e->alive && !e->attached)pending.push_back(e);
        if(pending.empty())break;
        for(auto e:pending){if(!e->scripts.empty() && ++initializedCount>8192)throw std::runtime_error("Lifecycle spawn limit exceeded (8192 scripted entities per attachment pass)");attach(e);}
    }
}
void Runtime::destroyDead(){
    std::exception_ptr failure;unsigned count=0;
    for(;;){std::vector<Script> dead,keep;for(auto& s:scripts)(s.entity && !s.entity->alive?dead:keep).push_back(s);scripts=std::move(keep);if(dead.empty())break;
        for(auto& s:dead){if(++count>8192)throw std::runtime_error("Lifecycle destroy limit exceeded");try{if(py::hasattr(s.instance,"on_destroy"))s.instance.attr("on_destroy")();}catch(...){if(!failure)failure=std::current_exception();}}
    }
    world.entities.erase(std::remove_if(world.entities.begin(),world.entities.end(),[](auto& e){return !e->alive;}),world.entities.end());
    world.pruneIndex();
    if(failure)std::rethrow_exception(failure);
}
void Runtime::loadScene(const std::string& name,const Localization* teardownLocalization) {
    auto previousPersistence=persistence;
    auto assetCheckpoint=assets.checkpoint();auto previousInput=inputFrame;auto previousTime=time;auto previousDt=dt;
    // Keep a usable world until the replacement scene and its Python scripts initialize.
    World replacement;replacement.config=&config;replacement.width=world.width;replacement.height=world.height;replacement.load(name);
    auto oldWorld=std::move(world);auto oldScripts=std::move(scripts);auto oldListeners=listeners;
    auto oldLocalization=localization;
    auto oldPending=pendingScene;bool oldRunning=running,oldPaused=gamePaused;
    world=std::move(replacement);scripts.clear();pendingScene.clear();gamePaused=false;
    listeners.erase(std::remove_if(listeners.begin(),listeners.end(),[](auto& l){return !l.persistent;}),listeners.end());
    if(renderer)renderer->checkpointInput();
    audio.begin();initializing=true;
    bool ownRendererStage=renderer && !reloading;
    try {
        if(ownRendererStage)renderer->stage();
        if(reloading)audio.configure(config.data.value("audio_settings",Json::object()));
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
                    world.physicsEnabled=j.value("physics_enabled",true);world.renderSettings=config.data.value("rendering",Json::object());world.renderSettings.merge_patch(j.value("rendering",Json::object()));world.renderSettings=validateRenderSettings(std::move(world.renderSettings));
                    world.gravity=world.is3d?glm::vec3(0,-9.81f,0):glm::vec3(0,980,0);
                    if(j.contains("gravity"))world.gravity=vector(j["gravity"].get<std::array<float,3>>());
                    if(j.contains("background"))world.background=color(j["background"].get<std::array<float,4>>(),"background");
                    auto camera=j.value("camera",Json::object());if(camera.contains("position"))world.cameraPosition=vector(camera["position"].get<std::array<float,3>>());if(camera.contains("target"))world.cameraTarget=vector(camera["target"].get<std::array<float,3>>());world.fov=camera.value("fov",60.0f);if(world.fov<=0 || world.fov>=179)throw std::runtime_error("Camera fov must be between 0 and 179");
                    world.loadEntities(j.value("entities",Json::array()));
                    world.configureSimulation(j);
                }
            }
        } else if(world.scene.contains("script"))sceneModule=loadModule(config.asset("scenes",world.scene["script"]));
        refreshLocalizedEntities();
        if(sceneModule && !sceneModule.is_none()) {scripts.push_back({sceneModule,sceneModule,{}});if(py::hasattr(sceneModule,"on_start"))sceneModule.attr("on_start")();}
        attachPending();destroyDead();world.syncTransforms();for(auto& e:world.entities)if(e->alive && proceduralName(e->model) && !geometry(world).entries.count(e->model))throw std::runtime_error("Missing procedural mesh: "+e->model);refreshLocalizedEntities();if(rigidPhysics(world))physics3D(world).sync(world);if(renderer)renderer->validateWorld(world);
    } catch(...) {
        if(ownRendererStage)renderer->discard();
        assets.rollback(assetCheckpoint);inputFrame=std::move(previousInput);time=previousTime;dt=previousDt;
        persistence=std::move(previousPersistence);initializing=false;localization=std::move(oldLocalization);audio.rollback();if(renderer)renderer->rollbackInput();for(auto& e:world.entities)e->alive=false;scripts.clear();world=std::move(oldWorld);scripts=std::move(oldScripts);
        listeners=std::move(oldListeners);pendingScene=oldPending;running=oldRunning;gamePaused=oldPaused;throw;
    }
    if(ownRendererStage)renderer->commit();
    initializing=false;audio.commit();
    // Old callbacks see their own world. Their mutations cannot affect the new scene.
    auto readyWorld=std::move(world);auto readyScripts=std::move(scripts);auto readyListeners=listeners;auto readyPending=pendingScene;bool readyRunning=running,readyPaused=gamePaused;
    auto readyLocalization=localization;localization=teardownLocalization?*teardownLocalization:std::move(oldLocalization);
    auto readyConfig=config;config=sceneConfig;sceneConfig=readyConfig;
    world=std::move(oldWorld);listeners=std::move(oldListeners);scripts.clear();tearingDown=true;audio.begin();
    for(auto& script:oldScripts)if(py::hasattr(script.instance,"on_destroy"))try{script.instance.attr("on_destroy")();}catch(const std::exception& ex){logger.error(ex.what());}
    for(auto& e:world.entities)e->alive=false;
    audio.rollback();localization=std::move(readyLocalization);tearingDown=false;config=std::move(readyConfig);world=std::move(readyWorld);scripts=std::move(readyScripts);listeners=std::move(readyListeners);pendingScene=readyPending;running=readyRunning;gamePaused=readyPaused;
    currentScene=name;logger.write("INFO","Scene loaded: "+name);
    if(!reloading){auto tasks=std::move(persistence);persistence.clear();for(auto& task:tasks)try{task();}catch(const std::exception& e){logger.error(e.what());}}
}
std::map<fs::path,fs::file_time_type> Runtime::snapshot() const {
    std::map<fs::path,fs::file_time_type> result;result[config.file]=fs::last_write_time(config.file);
    for(auto& [group,path]:config.paths)if(fs::exists(path))for(auto& entry:fs::recursive_directory_iterator(path))if(entry.is_regular_file() && entry.path().extension()!=".pyc" && entry.path().u8string().find("__pycache__")==std::string::npos)result[entry.path()]=entry.last_write_time();
    return result;
}
bool Runtime::changed(){auto now=snapshot();if(now==watched)return false;watched=std::move(now);return true;}
static std::vector<std::string> searchPaths(const Config& config) {
    std::vector<std::string> paths;
    for(auto group:{"modules","scripts","scenes"})paths.push_back(config.paths.at(group).u8string());
    for(auto& path:config.data.value("python_paths",Json::array()))paths.push_back(config.resolve(path.get<std::string>()).u8string());
    std::vector<std::string> result;
    for(auto it=paths.rbegin();it!=paths.rend();++it)if(std::find(result.begin(),result.end(),*it)==result.end())result.push_back(*it);
    return result;
}
void Runtime::refreshPythonPaths() {
    auto next=searchPaths(config);auto sys=py::module_::import("sys");py::list replacement;
    for(auto& path:next)replacement.append(path);
    for(auto item:sys.attr("path")) {
        if(py::isinstance<py::str>(item)){auto path=item.cast<std::string>();
            if(std::find(pythonPaths.begin(),pythonPaths.end(),path)!=pythonPaths.end() || std::find(next.begin(),next.end(),path)!=next.end())continue;
        }
        replacement.append(item);
    }
    sys.attr("path")=replacement;pythonPaths=std::move(next);
}
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
void Runtime::reload(){
    reloading=true;auto previousPersistence=persistence;auto previousBudget=assets.stats()["budget_bytes"].get<size_t>();
    auto previousLocalization=localization;auto previousConfig=config;auto previousPythonPaths=pythonPaths;auto sys=py::module_::import("sys");auto modules=sys.attr("modules").cast<py::dict>();
    auto previousModules=modules.attr("copy")().cast<py::dict>();auto previousPath=sys.attr("path").attr("copy")();
    try{
        auto updated=Config::load(config.file);if(!entryOverride.empty())updated.data["entry_scene"]=entryOverride;updated.validate(false);auto nextPaths=searchPaths(updated);
        std::vector<fs::path> invalidatedRoots;
        for(auto group:{"modules","scripts","scenes"})invalidatedRoots.push_back(config.paths.at(group));
        // Keep installed extension packages cached while their configured path remains.
        for(auto& path:pythonPaths)if(std::find(nextPaths.begin(),nextPaths.end(),path)==nextPaths.end())invalidatedRoots.push_back(fs::u8path(path));
        auto obsolete=[&](const std::string& input){auto file=fs::weakly_canonical(fs::u8path(input));for(auto& root:invalidatedRoots){auto relative=file.lexically_relative(root);if(!relative.empty() && *relative.begin()!="..")return true;}return false;};
        py::module_::import("importlib").attr("invalidate_caches")();py::list remove;
        for(auto item:modules){bool discard=false;
            if(py::hasattr(item.second,"__file__") && !item.second.attr("__file__").is_none())discard=obsolete(item.second.attr("__file__").cast<std::string>());
            if(!discard && py::hasattr(item.second,"__path__"))for(auto path:item.second.attr("__path__"))if(obsolete(py::cast<std::string>(path))){discard=true;break;}
            if(discard)remove.append(item.first);
        }
        for(auto item:remove)modules.attr("pop")(item,py::none());
        auto previousEntry=config.entry();config=std::move(updated);world.config=&config;assets.budget(config.data.value("asset_budget_bytes",size_t(256*1024*1024)));
        refreshPythonPaths();
        localization.load(config,previousLocalization.language);
        if(renderer)renderer->stage();
        loadScene(previousEntry==config.entry()?currentScene:config.entry(),&previousLocalization);
        if(renderer)renderer->commit();if(editing)gamePaused=renderer?!renderer->previewing():true;
    }catch(...){assets.budget(previousBudget);reloading=false;persistence=std::move(previousPersistence);if(renderer)renderer->discard();localization=std::move(previousLocalization);config=std::move(previousConfig);pythonPaths=std::move(previousPythonPaths);world.config=&config;sys.attr("path")=previousPath;modules.attr("clear")();modules.attr("update")(previousModules);throw;}
    reloading=false;auto tasks=std::move(persistence);persistence.clear();for(auto& task:tasks)try{task();}catch(const std::exception& e){logger.error(e.what());}
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
    if(reloading){auto snapshot=fromPython(value);persistence.push_back(py::cpp_function([this,name,snapshot](){save(name,toPython(snapshot));}));return;}
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
