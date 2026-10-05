// Lifecycle and scene transactions extracted from runtime.cpp (licensed core origin).
#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/particles.hpp>
#include <forge/geometry.hpp>
#include <forge/animation.hpp>
#include <pybind11/stl.h>
#include <algorithm>
namespace forge {
static glm::vec3 vector(std::array<float,3> v){return {finiteNumber(v[0],"vector"),finiteNumber(v[1],"vector"),finiteNumber(v[2],"vector")};}
static glm::vec4 color(std::array<float,4> v,const std::string& field){return {finiteNumber(v[0],field),finiteNumber(v[1],field),finiteNumber(v[2],field),finiteNumber(v[3],field)};}
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
        auto module=loadModule(config.asset("scripts",file));auto properties=pythonValue(item.is_string()?Json::object():item.value("properties",Json::object()));
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
void Runtime::loadScene(const std::string& name,const Localization* teardownLocalization,const Json* document) {
    auto previousPersistence=persistence;
    auto assetCheckpoint=assets.checkpoint();auto previousInput=inputFrame;auto previousTime=time;auto previousDt=dt;
    // Keep a usable world until the replacement scene and its Python scripts initialize.
    World replacement;replacement.config=&config;replacement.prepareEntity=world.prepareEntity;replacement.width=world.width;replacement.height=world.height;if(document){if(world.geometry)replacement.geometry=std::make_shared<Geometry>(*world.geometry);replacement.loadDocument(*document);}else replacement.load(name);
    auto oldSelection=editorSession.selected;if(!document)editorSession.selected.clear();
    auto oldWorld=std::move(world);auto oldScripts=std::move(scripts);auto oldListeners=listeners;auto oldListenerIds=listenerIds;
    auto oldLocalization=localization;
    auto oldPending=pendingScene;bool oldRunning=running,oldPaused=gamePaused;
    world=std::move(replacement);scripts.clear();pendingScene.clear();gamePaused=false;
    listeners.erase(std::remove_if(listeners.begin(),listeners.end(),[&](auto& l){return !l.persistent || !listenerIds.count(l.id);}),listeners.end());
    listenerIds.clear();for(const auto &listener:listeners)listenerIds.insert(listener.id);
    if(renderer)renderer->checkpointInput();
    audio.begin();initializing=true;auto previousAuthoring=authoringTransaction;authoringTransaction=document!=nullptr;
    bool ownRendererStage=renderer && !reloading;
    try {
        if(ownRendererStage)renderer->stage();
        if(reloading)audio.configure(config.data.value("audio_settings",Json::object()));
        py::object sceneModule;
        auto sceneFile=config.asset("scenes",name);
        if(sceneFile.extension()==".py" && !document) {
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
        editorSession.selected=oldSelection;
        if(ownRendererStage)renderer->discard();
        assets.rollback(assetCheckpoint);inputFrame=std::move(previousInput);time=previousTime;dt=previousDt;
        persistence=std::move(previousPersistence);initializing=false;authoringTransaction=previousAuthoring;localization=std::move(oldLocalization);audio.rollback();if(renderer)renderer->rollbackInput();for(auto& e:world.entities)e->alive=false;scripts.clear();world=std::move(oldWorld);scripts=std::move(oldScripts);
        listeners=std::move(oldListeners);listenerIds=std::move(oldListenerIds);pendingScene=oldPending;running=oldRunning;gamePaused=oldPaused;throw;
    }
    if(ownRendererStage)renderer->commit();
    initializing=false;authoringTransaction=previousAuthoring;audio.commit();
    // Old callbacks see their own world. Their mutations cannot affect the new scene.
    listeners.erase(std::remove_if(listeners.begin(),listeners.end(),[&](auto& l){return !listenerIds.count(l.id);}),listeners.end());
    auto readyListenerIds=listenerIds;
    auto readyWorld=std::move(world);auto readyScripts=std::move(scripts);auto readyListeners=listeners;auto readyPending=pendingScene;bool readyRunning=running,readyPaused=gamePaused;
    auto readyLocalization=localization;localization=teardownLocalization?*teardownLocalization:std::move(oldLocalization);
    auto readyConfig=config;config=sceneConfig;sceneConfig=readyConfig;
    world=std::move(oldWorld);listeners=std::move(oldListeners);listenerIds=std::move(oldListenerIds);scripts.clear();tearingDown=true;audio.begin();
    for(auto& script:oldScripts)if(py::hasattr(script.instance,"on_destroy"))try{script.instance.attr("on_destroy")();}catch(const std::exception& ex){logger.error(ex.what());}
    for(auto& e:world.entities)e->alive=false;
    audio.rollback();localization=std::move(readyLocalization);tearingDown=false;config=std::move(readyConfig);world=std::move(readyWorld);scripts=std::move(readyScripts);listeners=std::move(readyListeners);pendingScene=readyPending;running=readyRunning;gamePaused=readyPaused;
    listenerIds=std::move(readyListenerIds);
    currentScene=name;
    if(!world.find(editorSession.selected))editorSession.selected.clear();
    if(!document)editorSession.opened(config.asset("scenes",name),world);
    logger.write("INFO","Scene loaded: "+name);
    if(!reloading){auto tasks=std::move(persistence);persistence.clear();for(auto& task:tasks)try{task();}catch(const std::exception& e){logger.error(e.what());}}
}
}
