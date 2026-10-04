#include <forge/engine.hpp>
#include <forge/animation.hpp>
#include <algorithm>
namespace forge {
std::vector<double> Model::morphPose(size_t partIndex,const std::string& name,double seconds,bool loop)const{
    const auto& part=parts.at(partIndex);std::vector<double> result;
    for(auto& target:part.morphs)result.push_back(target.weight);
    if(name.empty() || result.empty())return result;
    auto found=std::find_if(clips.begin(),clips.end(),[&](auto& clip){return clip.name==name;});
    if(found==clips.end())throw std::runtime_error("Unknown morph animation: "+name);
    double length=found->duration/found->ticks;
    if(!std::isfinite(seconds))throw std::runtime_error("Morph animation time must be finite");
    double time=length>0?(loop?std::fmod(std::max(0.0,seconds),length):std::clamp(seconds,0.0,length))*found->ticks:0;
    for(auto& channel:found->morphs)if(channel.node==part.node && !channel.keys.empty()){
        auto upper=std::upper_bound(channel.keys.begin(),channel.keys.end(),time,[](double t,const auto& key){return t<key.first;});
        auto first=upper==channel.keys.begin()?upper:upper-1;
        auto last=upper==channel.keys.end()?first:upper;
        double factor=last->first>first->first?(time-first->first)/(last->first-first->first):0;
        for(size_t i=0;i<result.size();++i)result[i]=first->second.at(i)+(last->second.at(i)-first->second.at(i))*factor;
    }
    return result;
}
void validateMorphWeights(const Model& model,const Json& weights){
    if(!weights.is_object())throw std::runtime_error("Morph weights must be an object keyed by target name");
    std::set<std::string> names;for(auto& part:model.parts)for(auto& target:part.morphs)names.insert(target.name);
    for(auto it=weights.begin();it!=weights.end();++it){
        if(!names.count(it.key()))throw std::runtime_error("Unknown morph target: "+it.key());
        float value=finiteNumber(it.value(),"morph weight");if(value < -10 || value > 10)throw std::runtime_error("Morph weights must be -10..10");
    }
}
std::vector<ModelVertex> morphVertices(const Model::Part& part,const std::vector<double>& weights){
    if(weights.size()!=part.morphs.size())throw std::runtime_error("Morph weights/targets mismatch");
    for(double weight:weights)if(!std::isfinite(weight) || std::abs(weight)>10)throw std::runtime_error("Morph weight outside -10..10");
    auto vertices=part.vertices;
    for(size_t i=0;i<vertices.size();++i){glm::dvec3 position(vertices[i].p),normal(vertices[i].n);
        for(size_t j=0;j<weights.size();++j){
            position+=glm::dvec3(part.morphs[j].positions.at(i))*weights[j];normal+=glm::dvec3(part.morphs[j].normals.at(i))*weights[j];
        }
        for(int axis=0;axis<3;++axis)vertices[i].p[axis]=checkedFloat(position[axis],"morph vertex");
        double length=glm::length(normal);if(length>1e-8)vertices[i].n=glm::vec3(normal/length);
    }
    return vertices;
}
}
