// Python API extracted from runtime.cpp; this code retains its licensed core status.
#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/material.hpp>
#include <pybind11/stl.h>
#include <algorithm>
namespace forge {
static Runtime& rt(){if(!active)throw std::runtime_error("Engine runtime is not active");return *active;}
static std::array<float,3> tuple(glm::vec3 v){return {v.x,v.y,v.z};}
static glm::vec3 vector(std::array<float,3> v,const std::string& field="vector"){return {finiteNumber(v[0],field),finiteNumber(v[1],field),finiteNumber(v[2],field)};}
static glm::vec4 color(std::array<float,4> v,const std::string& field="color"){return {finiteNumber(v[0],field),finiteNumber(v[1],field),finiteNumber(v[2],field),finiteNumber(v[3],field)};}
static float positive(float value,const std::string& field){finiteNumber(value,field);if(value<=0)throw std::runtime_error(field+" must be positive");return value;}
static void transformMutation(Entity& e,const Entity& next){rt().world.setLocalTransform(e,next.position,next.rotation,next.scale);}
static void physicsMutation(const Entity& e){if(rigidPhysics(rt().world) && rt().world.activeCollider(e)){auto body=e;if(!e.parent.empty()){auto parent=rt().world.find(e.parent);if(parent)body.position=glm::vec3((glm::dmat4(parent->worldMatrix)*glm::dmat4(composeTransform(e)))[3]);}validatePhysicsEntity(body);}}
static py::object toPython(const Json& j){return pythonValue(j);}
PYBIND11_EMBEDDED_MODULE(forge,m) {
    m.attr("__version__")=FORGE_VERSION;
    m.attr("api_version")=1;
    m.def("capabilities",[](){
        Json features=Json::array({"prefabs","animation_layers","animation_events","retargeting","morph_targets","property_clips","bone_clips","background_watch","bullet","particles","pbr","project_documents","editor_sessions","custom_shells"});
        if(FORGE_WITH_EDITOR)features.push_back("animation_editor");
        return pythonValue(Json{{"api_version",1},{"project_api_version",1},{"base_editor",bool(FORGE_WITH_EDITOR)},{"features",features},{"python_runtimes_per_process",1}});
    });
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
            e.localizedRevision=uint64_t(-1);
        })
        .def_property("text_key",[](Entity& e){return e.textKey;},[](Entity& e,const std::string& key){e.textKey=key;e.localizedRevision=uint64_t(-1);})
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
    m.def("set_positions",[](const std::vector<std::pair<std::shared_ptr<Entity>,std::array<float,3>>>& values){
        std::vector<std::pair<std::shared_ptr<Entity>,glm::vec3>> updates;updates.reserve(values.size());
        for(auto& [entity,position]:values)updates.push_back({entity,vector(position,"position")});
        rt().world.setPositions(updates);
    });
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
    m.def("set_gravity",[](std::array<float,3> v){auto next=vector(v);if(rigidPhysics(rt().world))for(int axis=0;axis<3;++axis)if(std::abs(double(next[axis]))>1e6)throw std::runtime_error("physics gravity outside allowed range");rt().world.gravity=next;});
    m.def("set_mode",[](const std::string& mode){if(mode!="2d" && mode!="3d")throw std::runtime_error("Mode must be 2d or 3d");if(mode=="2d" && rigidPhysics(rt().world))throw std::runtime_error("Bullet backend requires a 3D scene; configure legacy physics first");rt().world.is3d=mode=="3d";});
    m.def("set_background",[](std::array<float,4> v){rt().world.background=color(v,"background");});
    m.def("raycast",[](std::array<float,3> origin,std::array<float,3> direction,float distance){return rt().world.raycast(vector(origin),vector(direction),finiteNumber(distance,"raycast distance"));},py::arg("origin"),py::arg("direction"),py::arg("distance")=1000);
    m.def("raycast_many",[](const std::vector<std::tuple<std::array<float,3>,std::array<float,3>,float>>& rays){
        struct Ray {glm::vec3 origin,direction;float distance;};std::vector<Ray> checked;checked.reserve(rays.size());
        for(auto& [origin,direction,distance]:rays)checked.push_back({vector(origin,"ray origin"),vector(direction,"ray direction"),finiteNumber(distance,"ray distance")});
        auto& world=rt().world;
        if(rigidPhysics(world))physics3D(world).sync(world);else world.syncTransforms();
        std::vector<std::shared_ptr<Entity>> result;result.reserve(checked.size());
        for(auto& ray:checked)result.push_back(world.raycast(ray.origin,ray.direction,ray.distance,false));
        return result;
    },py::arg("rays"));
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
    m.def("on_frame",[](py::object callback,bool persistent){return rt().onFrame(callback,persistent);},py::arg("callback"),py::arg("persistent")=false);
    m.def("remove_listener",[](unsigned id){rt().removeListener(id);});
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
}
