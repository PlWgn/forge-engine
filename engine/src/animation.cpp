#define GLM_ENABLE_EXPERIMENTAL
#include <forge/engine.hpp>
#include <forge/animation.hpp>
#include <forge/prefab.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
namespace forge {
namespace {
struct TRS {glm::vec3 position,scale;glm::quat rotation;};
TRS decompose(const glm::mat4& matrix){
    TRS value;glm::vec3 skew;glm::vec4 perspective;
    if(!glm::decompose(matrix,value.scale,value.rotation,value.position,skew,perspective))throw std::runtime_error("Animation needs decomposable TRS nodes");
    value.rotation=glm::normalize(value.rotation);return value;
}
glm::mat4 compose(const TRS& value){
    auto matrix=glm::translate(glm::mat4(1),value.position)*glm::mat4_cast(value.rotation)*glm::scale(glm::mat4(1),value.scale);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)checkedFloat(matrix[c][r],"animation pose");
    return matrix;
}
const Model::Clip& clip(const Model& model,const std::string& name){
    auto found=std::find_if(model.clips.begin(),model.clips.end(),[&](auto& value){return value.name==name;});
    if(found==model.clips.end())throw std::runtime_error("Unknown animation: "+name);
    return *found;
}
double duration(const AnimatorState::Layer& layer){return layer.length;}
int node(const Model& model,const std::string& name){
    auto found=std::find_if(model.nodes.begin(),model.nodes.end(),[&](auto& value){return value.name==name;});
    if(found==model.nodes.end())throw std::runtime_error("Unknown skeleton node: "+name);
    return int(found-model.nodes.begin());
}
AnimatorState::Layer makeLayer(const Config& config,Assets& assets,const Model& target,const std::string& file,Json settings){
    if(!settings.is_object())throw std::runtime_error("Animation layer must be an object");
    auto name=settings.value("model",file);auto source=assets.model(config.asset("models",name));
    bool authored=settings.contains("duration");
    double length;
    if(authored){length=finiteNumber(settings["duration"],"authored duration");if(length<1e-6 || length>1e6)throw std::runtime_error("Authored duration must be 1e-6..1e6 seconds");settings["clip"]=settings.value("clip","Authored");}
    else {auto& animation=clip(*source,settings.at("clip").get<std::string>());length=animation.duration/animation.ticks;}
    for(auto field:{"weight","speed","time","translation_scale"}){
        double value=finiteNumber(settings.value(field,Json(std::string(field)=="time"?0:1)),std::string("animation ")+field);
        if((std::string(field)=="weight" && (value<0 || value>1)) || (std::string(field)=="time" && value<0) || std::abs(value)>1e6)throw std::runtime_error(std::string("Animation ")+field+" outside range");
    }
    for(auto field:{"loop"})if(settings.contains(field) && !settings[field].is_boolean())throw std::runtime_error("Animation loop must be boolean");
    auto markers=settings.value("events",Json::array());
    if(!markers.is_array() || markers.size()>1024)throw std::runtime_error("Animation events must be an array of at most 1024 markers");
    for(auto& event:markers){
        if(!event.is_object() || event.value("name","").empty())throw std::runtime_error("Animation event needs name/time");
        double time=finiteNumber(event.at("time"),"animation event time");
        if(time<0 || time>length)throw std::runtime_error("Animation event time outside clip");
    }
    auto mask=settings.value("mask",Json::array());
    if(!mask.is_array())throw std::runtime_error("Animation mask must be a node-name array");
    for(auto& value:mask)node(target,value.get<std::string>());
    AnimatorState::Layer result;result.source=source;result.length=length;result.authored=authored;result.time=settings.value("time",0.0);result.settings=std::move(settings);
    auto tracks=result.settings.value("tracks",Json::array());
    if(!tracks.is_array() || tracks.size()>128)throw std::runtime_error("Bone tracks need an array of at most 128 nodes");
    std::set<int> tracked;size_t totalKeys=0;
    for(auto& track:tracks){
        Model::Channel channel;channel.node=node(*source,track.at("node").get<std::string>());
        if(!tracked.insert(channel.node).second)throw std::runtime_error("Duplicate bone track node");
        for(auto field:{"position","rotation","scale"})if(track.contains(field)){
            auto& keys=track[field];if(!keys.is_array() || keys.empty() || keys.size()>8192)throw std::runtime_error("Bone track needs 1..8192 keys");
            totalKeys+=keys.size();if(totalKeys>65536)throw std::runtime_error("Bone track key budget exceeded");
            double previous=-1;
            for(auto& key:keys){
                double time=finiteNumber(key.at("time"),"bone key time");
                if(time<=previous || time<0 || time>length)throw std::runtime_error("Bone key times must increase within duration");
                previous=time;auto& components=key.at("value");
                if(!components.is_array() || components.size()!=3)throw std::runtime_error("Bone key value needs three components");
                glm::vec3 value;for(int axis=0;axis<3;++axis)value[axis]=finiteNumber(components[axis],"bone key");
                if(std::string(field)=="position")channel.positions.push_back({time,value});
                else if(std::string(field)=="scale")channel.scales.push_back({time,value});
                else channel.rotations.push_back({time,glm::quat(glm::radians(value))});
            }
        }
        result.tracks.push_back(std::move(channel));
    }
    if(name!=file){
        auto mapping=result.settings.value("mapping",Json::object());
        if(!mapping.is_object())throw std::runtime_error("Retarget mapping must map source names to target names");
        std::set<int> targets;
        if(mapping.empty())for(auto& value:source->nodes){
            auto found=std::find_if(target.nodes.begin(),target.nodes.end(),[&](auto& n){return n.name==value.name;});
            if(found!=target.nodes.end())mapping[value.name]=value.name;
        }
        for(auto it=mapping.begin();it!=mapping.end();++it){
            int a=node(*source,it.key()),b=node(target,it.value().get<std::string>());
            if(!targets.insert(b).second)throw std::runtime_error("Retarget target mapped twice");
            auto base=decompose(source->nodes[a].transform);
            for(int axis=0;axis<3;++axis)if(std::abs(base.scale[axis])<1e-8)throw std::runtime_error("Retarget source rest scale is singular");
            result.mapping.push_back({a,b});
        }
        if(result.mapping.empty())throw std::runtime_error("Retarget models need an explicit mapping or matching node names");
    }
    return result;
}
template<class T> T sampleKeys(const std::vector<std::pair<double,T>>& keys,double time,T fallback){
    if(keys.empty())return fallback;
    auto upper=std::upper_bound(keys.begin(),keys.end(),time,[](double t,auto& key){return t<key.first;});
    if(upper==keys.begin())return upper->second;
    if(upper==keys.end())return keys.back().second;
    auto first=upper-1;float factor=float((time-first->first)/(upper->first-first->first));
    if constexpr(std::is_same_v<T,glm::quat>)return glm::normalize(glm::slerp(first->second,upper->second,factor));
    else return glm::mix(first->second,upper->second,factor);
}
std::vector<glm::mat4> sampleLayer(const Model& target,const AnimatorState::Layer& layer){
    double time=layer.length>0?(layer.settings.value("loop",true)?std::fmod(layer.time,layer.length):std::clamp(layer.time,0.0,layer.length)):0;
    if(time<0)time+=layer.length;
    auto local=layer.source->localPose(layer.authored?"":layer.settings.at("clip").get<std::string>(),time,false);
    for(auto& track:layer.tracks){
        auto value=decompose(local.at(track.node));
        value.position=sampleKeys(track.positions,time,value.position);value.scale=sampleKeys(track.scales,time,value.scale);value.rotation=sampleKeys(track.rotations,time,value.rotation);
        local[track.node]=compose(value);
    }
    if(layer.mapping.empty())return local;
    std::vector<glm::mat4> result;for(auto& value:target.nodes)result.push_back(value.transform);
    float translationScale=layer.settings.value("translation_scale",1.f);
    for(auto [from,to]:layer.mapping){
        auto sourceRest=decompose(layer.source->nodes[from].transform),pose=decompose(local.at(from)),targetRest=decompose(target.nodes[to].transform);
        TRS mapped{targetRest.position+(pose.position-sourceRest.position)*translationScale,
                   targetRest.scale*(pose.scale/sourceRest.scale),
                   targetRest.rotation*glm::inverse(sourceRest.rotation)*pose.rotation};
        result[to]=compose(mapped);
    }
    return result;
}
std::vector<glm::mat4> sample(const Model& target,const std::vector<AnimatorState::Layer>& layers){
    std::vector<std::vector<glm::mat4>> sampled;for(auto& layer:layers)sampled.push_back(sampleLayer(target,layer));
    std::vector<glm::mat4> result;
    for(size_t i=0;i<target.nodes.size();++i){
        auto rest=decompose(target.nodes[i].transform);double total=0;
        std::vector<double> weights;
        for(auto& layer:layers){
            auto mask=layer.settings.value("mask",Json::array());bool enabled=mask.empty() || std::find(mask.begin(),mask.end(),target.nodes[i].name)!=mask.end();
            double weight=enabled?layer.settings.value("weight",1.0):0;weights.push_back(weight);total+=weight;
        }
        double restWeight=std::max(0.0,1-total),normalizer=std::max(1.0,total);
        TRS value{rest.position*float(restWeight),rest.scale*float(restWeight),rest.rotation*float(restWeight)};
        for(size_t j=0;j<layers.size();++j)if(weights[j]>0){
            auto pose=decompose(sampled[j][i]);if(glm::dot(pose.rotation,rest.rotation)<0)pose.rotation=-pose.rotation;
            value.position+=pose.position*float(weights[j]);value.scale+=pose.scale*float(weights[j]);value.rotation+=pose.rotation*float(weights[j]);
        }
        value.position/=float(normalizer);value.scale/=float(normalizer);
        if(glm::length(value.rotation)<1e-8)value.rotation=rest.rotation;else value.rotation=glm::normalize(value.rotation);
        result.push_back(compose(value));
    }
    return result;
}
void advance(AnimatorState::Layer& layer,double dt,Json& events){
    bool loop=layer.settings.value("loop",true);
    double old=layer.time,next=old+dt*layer.settings.value("speed",1.0);
    if(!loop)next=std::max(0.0,next);
    if(!std::isfinite(next))throw std::runtime_error("Animation clock overflow");
    double length=duration(layer);
    auto markers=layer.settings.value("events",Json::array());
    struct Event {double occurrence;Json value;};std::vector<Event> emitted;
    if(next!=old)for(auto marker:markers){
        double time=marker.at("time"),low=std::min(old,next),high=std::max(old,next);
        double first=0,last=0;
        if(loop && length>0){first=std::ceil((low-time)/length);last=std::floor((high-time)/length);}
        if(last-first>1024)throw std::runtime_error("Animation event budget exceeded; seek instead of advancing large intervals");
        if(!std::isfinite(first) || !std::isfinite(last))throw std::runtime_error("Animation event cycle overflow");
        int count=last>=first?int(last-first)+1:0;
        for(int index=0;index<count;++index){
            double stamp=time+(loop?(first+index)*length:0);
            bool crossed=next>old?(stamp>old && stamp<=next):(stamp<old && stamp>=next);
            if(!crossed)continue;
            if(emitted.size()+events.size()>=4096)throw std::runtime_error("Animation event queue full; consume animation_events");
            marker["type"]="marker";marker["clip"]=layer.settings.at("clip");emitted.push_back({stamp,marker});
        }
    }
    std::stable_sort(emitted.begin(),emitted.end(),[&](auto& a,auto& b){return next>old?a.occurrence<b.occurrence:a.occurrence>b.occurrence;});
    for(auto& event:emitted)events.push_back(event.value);
    bool finished=!loop && (layer.settings.value("speed",1.0)>=0?next>=length:next<=0);
    if(finished && !layer.finished)events.push_back({{"type","finished"},{"clip",layer.settings.at("clip")}});
    layer.finished=finished;layer.time=next;
}
std::vector<std::vector<double>> sampleMorphs(const Model& model,const std::vector<AnimatorState::Layer>& layers){
    std::vector<std::vector<double>> result;
    for(size_t part=0;part<model.parts.size();++part){
        std::vector<double> weights(model.parts[part].morphs.size(),0);double sum=0;
        for(auto& layer:layers)if(layer.source.get()==&model && !layer.authored){
            double time=layer.length>0?(layer.settings.value("loop",true)?std::fmod(layer.time,layer.length):std::clamp(layer.time,0.0,layer.length)):0;
            if(time<0)time+=layer.length;
            auto values=model.morphPose(part,layer.settings.at("clip"),time,false);
            double factor=layer.settings.value("weight",1.0);sum+=factor;
            for(size_t i=0;i<weights.size();++i)weights[i]+=values[i]*factor;
        }
        for(size_t i=0;i<weights.size();++i)weights[i]=(weights[i]+model.parts[part].morphs[i].weight*std::max(0.0,1-sum))/std::max(1.0,sum);
        result.push_back(std::move(weights));
    }
    return result;
}
void evaluate(AnimatorState& state){
    auto local=sample(*state.target,state.layers);
    auto morphs=sampleMorphs(*state.target,state.layers);
    if(!state.from.empty() || !state.fromLocal.empty()){
        auto previous=state.fromLocal.empty()?sample(*state.target,state.from):state.fromLocal;
        auto previousMorphs=state.fromLocal.empty()?sampleMorphs(*state.target,state.from):state.fromMorphs;
        float t=state.fadeDuration>0?float(std::clamp(state.fadeTime/state.fadeDuration,0.0,1.0)):1;
        for(size_t i=0;i<local.size();++i){
            auto a=decompose(previous[i]),b=decompose(local[i]);
            local[i]=compose({glm::mix(a.position,b.position,t),glm::mix(a.scale,b.scale,t),glm::slerp(a.rotation,b.rotation,t)});
        }
        for(size_t i=0;i<morphs.size();++i)for(size_t j=0;j<morphs[i].size();++j)morphs[i][j]=previousMorphs[i][j]*(1-t)+morphs[i][j]*t;
    }
    state.local=local;state.morphs=std::move(morphs);state.pose.resize(local.size());
    for(size_t i=0;i<local.size();++i){
        int parent=state.target->nodes[i].parent;state.pose[i]=parent<0?local[i]:state.pose[parent]*local[i];
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)checkedFloat(state.pose[i][c][r],"animation global pose");
    }
}
}
static std::shared_ptr<AnimatorState> prepareAnimator(const Config& config,Assets& assets,const Entity& entity,const Json& options){
    if(!options.is_object() || entity.model.empty() || entity.model.rfind("@mesh:",0)==0)throw std::runtime_error("Animator needs an imported model and object settings");
    auto candidate=std::make_shared<AnimatorState>();
    candidate->model=entity.model;candidate->target=assets.model(config.asset("models",entity.model));
    auto layers=options.value("layers",Json::array());
    if(!layers.is_array() || layers.empty() || layers.size()>16)throw std::runtime_error("Animator needs 1..16 layers");
    for(auto& layer:layers)candidate->layers.push_back(makeLayer(config,assets,*candidate->target,entity.model,layer));
    for(auto field:{"playing","auto_update"})if(options.contains(field) && !options[field].is_boolean())throw std::runtime_error("Animator playback options must be boolean");
    candidate->playing=options.value("playing",true);candidate->automatic=options.value("auto_update",true);
    evaluate(*candidate);return candidate;
}
Json validateAnimator(Runtime& runtime,const Entity& entity,Json options){
    prepareAnimator(runtime.config,runtime.assets,entity,options);return options;
}
void configureAnimator(Runtime& runtime,Entity& entity,Json options){
    auto candidate=prepareAnimator(runtime.config,runtime.assets,entity,options);
    entity.animator=std::move(candidate);entity.animatorSettings=std::move(options);
}
void validateAnimationAssets(const Config& config){
    Config owned=config;Assets assets(owned);
    auto check=[&](Json data){
        data=validateEntity(config,std::move(data));Entity entity;
        entity.model=data.value("model","");auto options=data.value("animator",Json::object()),weights=data.value("morph_weights",Json::object());
        auto name=data.value("animation","");
        if(options.empty() && weights.empty() && name.empty())return;
        if(entity.model.empty() || entity.model.rfind("@mesh:",0)==0)throw std::runtime_error("Animation settings need imported model");
        auto model=assets.model(config.asset("models",entity.model));
        validateMorphWeights(*model,weights);if(!name.empty())model->pose(name,0,true);
        if(!options.empty())prepareAnimator(config,assets,entity,options);
    };
    for(auto& entry:fs::recursive_directory_iterator(config.paths.at("objects")))if(entry.is_regular_file() && entry.path().extension()==".json"){
        auto file=entry.path().lexically_relative(config.paths.at("objects")).generic_u8string();
        auto document=prefabDocument(config,file);
        for(auto& data:document["entities"])check(data);
    }
    for(auto& entry:fs::recursive_directory_iterator(config.paths.at("scenes")))if(entry.is_regular_file() && entry.path().extension()==".json")
        for(auto& data:readJson(config.asset("scenes",entry.path().lexically_relative(config.paths.at("scenes")).generic_u8string())).value("entities",Json::array()))check(data);
}
void transitionAnimator(Runtime& runtime,Entity& entity,Json options,double seconds){
    if(!std::isfinite(seconds) || seconds<0 || seconds>60)throw std::runtime_error("Animation fade must be 0..60 seconds");
    std::vector<AnimatorState::Layer> previous;
    if(entity.animator)previous=entity.animator->layers;
    else if(!entity.animation.empty()){
        auto model=runtime.assets.model(runtime.config.asset("models",entity.model));
        previous.push_back(makeLayer(runtime.config,runtime.assets,*model,entity.model,Json{{"clip",entity.animation},{"time",entity.animationTime},{"loop",entity.animationLoop},{"speed",entity.animationSpeed}}));
    }
    if(entity.animator && !options.contains("auto_update"))options["auto_update"]=entity.animator->automatic;
    Entity candidate=entity;
    configureAnimator(runtime,candidate,std::move(options));
    if(seconds>0 && !previous.empty()){
        auto& next=*candidate.animator;
        if(entity.animator && (!entity.animator->from.empty() || !entity.animator->fromLocal.empty())){
            next.fromLocal=entity.animator->local;next.fromMorphs=entity.animator->morphs;
        }else next.from=std::move(previous);
        next.fadeDuration=seconds;evaluate(next);
    }
    entity.animator=std::move(candidate.animator);entity.animatorSettings=std::move(candidate.animatorSettings);
}
void crossfadeAnimation(Runtime& runtime,Entity& entity,const std::string& name,double seconds,float speed,bool loop){
    transitionAnimator(runtime,entity,Json{{"layers",Json::array({Json{{"clip",name},{"speed",speed},{"loop",loop}}})}},seconds);
}
void updateAnimation(Entity& entity,double dt){
    if(!std::isfinite(dt) || dt<0 || dt>10)throw std::runtime_error("Animation dt must be 0..10 seconds");
    if(!entity.animator)return;
    if(entity.model!=entity.animator->model)throw std::runtime_error("Animator model changed; configure the animator again");
    if(!entity.animator->playing)return;
    auto next=*entity.animator;
    for(auto& layer:next.layers)advance(layer,dt,next.events);
    for(auto& layer:next.from)advance(layer,dt,next.events);
    if(next.events.size()>4096)throw std::runtime_error("Animation event queue full; consume animation_events");
    next.fadeTime+=dt;if(next.fadeTime>=next.fadeDuration){next.from.clear();next.fromLocal.clear();next.fromMorphs.clear();}evaluate(next);
    *entity.animator=std::move(next);
}
void prepareAnimation(Runtime& runtime,Entity& entity){
    if(entity.animatorSettings.empty() && entity.morphWeights.empty())return;
    if(entity.model.empty() || entity.model.rfind("@mesh:",0)==0)throw std::runtime_error("Animation settings need an imported model");
    auto model=runtime.assets.model(runtime.config.asset("models",entity.model));
    validateMorphWeights(*model,entity.morphWeights);
    if(!entity.animatorSettings.empty())configureAnimator(runtime,entity,entity.animatorSettings);
}
void updateAnimations(Runtime& runtime,double dt){
    for(auto& entity:runtime.world.entities)if(entity->alive){
        if(entity->animator){if(entity->animator->automatic)updateAnimation(*entity,dt);}
        else if(entity->animationPlaying){double time=std::max(0.0,entity->animationTime+dt*entity->animationSpeed);if(!std::isfinite(time))throw std::runtime_error("Animation clock overflow");entity->animationTime=time;}
    }
}
}
