#include <forge/engine.hpp>
#include <forge/animation.hpp>
#include <pybind11/stl.h>
namespace forge {
namespace {
Runtime& runtime(){if(!active)throw std::runtime_error("Runtime inactive");return *active;}
void current(const Entity& e){if(runtime().world.find(e.id).get()!=&e)throw std::runtime_error("Animation entity is not in current scene");}
Json info(const Entity& e){
    if(!e.animator)return Json{{"configured",false},{"playing",e.animationPlaying},{"clip",e.animation},{"time",e.animationTime}};
    auto& a=*e.animator;Json layers=Json::array();bool finished=true;
    for(auto& layer:a.layers){auto data=layer.settings;data["time"]=layer.time;data["finished"]=layer.finished;layers.push_back(data);finished=finished && layer.finished;}
    return {{"configured",true},{"playing",a.playing},{"auto_update",a.automatic},{"layers",layers},{"finished",finished},{"fade_time",a.fadeTime},{"fade_duration",a.fadeDuration},{"queued_events",a.events.size()}};
}
}
void bindAnimation(py::module_& module){
    module.def("validate_animator",[](const Entity& e,py::dict settings){current(e);return pythonValue(validateAnimator(runtime(),e,fromPython(settings)));});
    module.def("configure_animator",[](Entity& e,py::dict settings){current(e);configureAnimator(runtime(),e,fromPython(settings));});
    module.def("transition_animator",[](Entity& e,py::dict settings,double seconds){current(e);transitionAnimator(runtime(),e,fromPython(settings),seconds);},py::arg("entity"),py::arg("settings"),py::arg("seconds")=.25);
    module.def("crossfade_animation",[](Entity& e,const std::string& clip,double seconds,float speed,bool loop){current(e);crossfadeAnimation(runtime(),e,clip,seconds,speed,loop);},py::arg("entity"),py::arg("clip"),py::arg("seconds")=.25,py::arg("speed")=1,py::arg("loop")=true);
    module.def("animator_info",[](const Entity& e){current(e);return pythonValue(info(e));});
    module.def("update_animation",[](Entity& e,double dt){current(e);updateAnimation(e,dt);});
    module.def("seek_animation",[](Entity& e,double time){
        current(e);if(!std::isfinite(time) || time<0 || time>1e6)throw std::runtime_error("Animation seek must be 0..1e6 seconds");
        if(!e.animator){e.animationTime=time;return;}
        auto settings=e.animatorSettings;for(auto& layer:settings["layers"])layer["time"]=time;
        settings["playing"]=e.animator->playing;settings["auto_update"]=e.animator->automatic;configureAnimator(runtime(),e,std::move(settings));
    });
    module.def("animation_events",[](Entity& e){current(e);Json events=Json::array();if(e.animator)events.swap(e.animator->events);return pythonValue(events);});
    module.def("entity_animation_pose",[](const Entity& e){
        current(e);auto model=runtime().assets.model(runtime().config.asset("models",e.model));auto pose=e.animator?e.animator->pose:model->pose(e.animation,e.animationTime,e.animationLoop);
        py::list result;for(auto& matrix:pose){py::list row;for(int r=0;r<4;++r)for(int c=0;c<4;++c)row.append(matrix[c][r]);result.append(row);}return result;
    });
    module.def("set_morph_weights",[](Entity& e,py::dict weights){current(e);auto next=fromPython(weights);auto model=runtime().assets.model(runtime().config.asset("models",e.model));validateMorphWeights(*model,next);e.morphWeights=std::move(next);});
    module.def("morph_weights",[](const Entity& e){current(e);return pythonValue(e.morphWeights);});
    module.def("morph_vertices",[](const std::string& file,size_t part,py::dict weights){
        auto model=runtime().assets.model(runtime().config.asset("models",file));auto data=fromPython(weights);validateMorphWeights(*model,data);
        const auto& mesh=model->parts.at(part);std::vector<double> values;for(auto& target:mesh.morphs)values.push_back(data.value(target.name,target.weight));
        auto vertices=morphVertices(mesh,values);py::list result;for(auto& v:vertices)result.append(py::make_tuple(v.p.x,v.p.y,v.p.z));return result;
    },py::arg("model"),py::arg("part")=0,py::arg("weights")=py::dict());
}
}
