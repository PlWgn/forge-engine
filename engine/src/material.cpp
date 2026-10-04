#include <forge/engine.hpp>
#include <forge/material.hpp>
namespace forge {
std::vector<std::string> materialTextures(const Json& data){
    std::vector<std::string> result;
    for(auto key:{"texture","albedo_texture","normal_texture","metallic_texture","roughness_texture","metallic_roughness_texture","occlusion_texture","emissive_texture"})
        if(data.contains(key) && !data.at(key).get<std::string>().empty())result.push_back(data.at(key));
    return result;
}
Json validateMaterial(const Config& c,Json j){
    if(!j.is_object())throw std::runtime_error("Material must be an object");
    auto mode=j.value("shading","legacy");if(mode!="legacy" && mode!="pbr" && mode!="unlit")throw std::runtime_error("material.shading must be legacy, pbr or unlit");
    auto alpha=j.value("alpha_mode",mode=="legacy"?"blend":"opaque");if(alpha!="opaque" && alpha!="mask" && alpha!="blend")throw std::runtime_error("material.alpha_mode must be opaque, mask or blend");
    j["shading"]=mode;j["alpha_mode"]=alpha;
    for(auto key:{"metallic","roughness","occlusion_strength","alpha_cutoff"}){
        float v=finiteNumber(j.value(key,Json(std::string(key)=="metallic"?0:std::string(key)=="alpha_cutoff"?.5:1)),std::string("material.")+key);
        if(v<0 || v>1)throw std::runtime_error(std::string("material.")+key+" must be 0..1");j[key]=v;
    }
    float normal=finiteNumber(j.value("normal_scale",Json(1)),"material.normal_scale");if(normal<0 || normal>10)throw std::runtime_error("material.normal_scale must be 0..10");j["normal_scale"]=normal;
    for(auto key:{"base_color","color","emissive"}){
        auto value=j.value(key,std::string(key)=="emissive"?Json::array({0,0,0}):Json::array({1,1,1,1}));
        size_t count=std::string(key)=="emissive"?3:4;if(!value.is_array() || value.size()!=count)throw std::runtime_error(std::string("material.")+key+" has invalid vector size");
        for(auto& v:value){auto n=finiteNumber(v,std::string("material.")+key);if(std::string(key)!="color" && (n<0 || n>(std::string(key)=="emissive"?1000:1)))throw std::runtime_error(std::string("material.")+key+" outside range");}
        j[key]=value;
    }
    if(j.contains("double_sided") && !j["double_sided"].is_boolean())throw std::runtime_error("material.double_sided must be boolean");
    for(auto key:{"texture","albedo_texture","normal_texture","metallic_texture","roughness_texture","metallic_roughness_texture","occlusion_texture","emissive_texture"}){
        if(!j.contains(key))continue;if(!j[key].is_string())throw std::runtime_error(std::string("material.")+key+" must be a string");
        auto name=j[key].get<std::string>();if(!name.empty() && !fs::is_regular_file(c.asset("textures",name)))throw std::runtime_error("Missing material texture: "+name);
    }
    return j;
}
} // namespace forge
