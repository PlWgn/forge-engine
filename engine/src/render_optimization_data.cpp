#include <forge/render_optimization.hpp>
#include <forge/config.hpp>
#include <forge/model.hpp>
#include <forge/geometry.hpp>
#include <cmath>
namespace forge {
namespace {
void boolean(const Json& value, const char* field, bool& result) {
    if (!value.contains(field)) return;
    if (!value[field].is_boolean()) throw std::runtime_error(std::string("optimization.")+field+" must be boolean");
    result = value[field].get<bool>();
}
double number(const Json& value, const char* field, double fallback, double low, double high) {
    if (!value.contains(field)) return fallback;
    auto& entry = value[field];
    if (!entry.is_number()) throw std::runtime_error(std::string("optimization.")+field+" must be numeric");
    double result = entry.get<double>();
    if (!std::isfinite(result) || result < low || result > high)
        throw std::runtime_error(std::string("optimization.")+field+" outside supported range");
    return result;
}
RenderBounds bounds(const Json& value) {
    if (!value.is_object()) throw std::runtime_error("optimization bounds must be an object with min/max");
    RenderBounds result;
    for (auto field : {"min", "max"}) {
        if (!value.contains(field) || !value[field].is_array() || value[field].size() != 3)
            throw std::runtime_error("optimization bounds need three min/max components");
        for (int axis = 0; axis < 3; ++axis) {
            auto component = finiteNumber(value[field][axis], "optimization bounds");
            (std::string(field)=="min" ? result.min : result.max)[axis] = component;
        }
    }
    if (glm::any(glm::greaterThan(result.min,result.max))) throw std::runtime_error("optimization bounds min exceeds max");
    result.valid = true;
    return result;
}
bool finite(glm::dvec3 value) {return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);}
void include(RenderBounds& box,glm::dvec3 point) {
    if(!box.valid){box.min=box.max=point;box.valid=true;}
    else {box.min=glm::min(box.min,point);box.max=glm::max(box.max,point);}
}
} // namespace
std::shared_ptr<const EntityOptimization> entityOptimization(const Config& config,const Json& definition,const std::string& kind) {
    if(!definition.is_object())throw std::runtime_error("Entity optimization must be an object");
    if(definition.empty()) {
        static const std::shared_ptr<const EntityOptimization> defaults=std::make_shared<EntityOptimization>();
        return defaults; // Immutable defaults avoid per-entity allocations in existing projects.
    }
    auto result=std::make_shared<EntityOptimization>();result->definition=definition;
    boolean(definition,"culling",result->culling);boolean(definition,"lod",result->lod);
    result->maxDistance=number(definition,"max_distance",0,0,1e30);
    result->hysteresis=number(definition,"hysteresis",.1,0,.49);
    if(definition.contains("bounds"))result->bounds=bounds(definition["bounds"]);
    if(definition.contains("occluder")) {
        result->occluder=bounds(definition["occluder"]);
        if(glm::any(glm::greaterThanEqual(result->occluder.min,result->occluder.max)))throw std::runtime_error("Occluder box must have positive volume");
    }
    auto levels=definition.value("levels",Json::array());
    if(!levels.is_array() || levels.size()>16)throw std::runtime_error("optimization.levels needs at most 16 LOD levels");
    double previous=0;
    for(auto& entry:levels){
        if(!entry.is_object() || !entry.contains("distance"))throw std::runtime_error("LOD level needs distance");
        RenderLod level;level.distance=number(entry,"distance",0,0,1e30);
        if(level.distance<=previous)throw std::runtime_error("optimization LOD distances must be positive and strictly increasing");
        previous=level.distance;
        for(auto field:{"model","texture"})if(entry.contains(field)){
            if(!entry[field].is_string() || entry[field].get<std::string>().empty())throw std::runtime_error("LOD model/texture must be a nonempty string");
            auto name=entry[field].get<std::string>();
            if(std::string(field)=="model") {
                if(kind!="mesh")throw std::runtime_error("Model LOD requires a mesh entity");
                level.model=name;
            }else level.texture=name;
            bool procedural=std::string(field)=="model" && proceduralName(name);
            bool target=std::string(field)=="texture" && name.rfind("@target:",0)==0;
            if(!procedural && !target && !fs::is_regular_file(config.asset(std::string(field)=="model"?"models":"textures",name)))
                throw std::runtime_error("Missing LOD asset: "+name);
        }
        if(level.model.empty() && level.texture.empty())throw std::runtime_error("LOD level needs model or texture");
        result->levels.push_back(std::move(level));
    }
    return result;
}
RenderOptimizationSettings renderOptimizationSettings(const Json& definition) {
    if(!definition.is_object())throw std::runtime_error("rendering.optimization must be an object");
    RenderOptimizationSettings result;
    boolean(definition,"enabled",result.enabled);boolean(definition,"frustum",result.frustum);
    boolean(definition,"distance",result.distance);boolean(definition,"lod",result.lod);boolean(definition,"occlusion",result.occlusion);
    for(auto field:{"grid_width","grid_height","max_occluders"})if(definition.contains(field)) {
        if(!definition[field].is_number_integer())throw std::runtime_error(std::string("optimization.")+field+" must be an integer");
        auto value=number(definition,field,0,1,std::string(field)=="max_occluders"?1024:256);
        if(std::string(field)=="grid_width")result.gridWidth=unsigned(value);
        else if(std::string(field)=="grid_height")result.gridHeight=unsigned(value);
        else result.maxOccluders=unsigned(value);
    }
    return result;
}
RenderBounds staticModelBounds(const Model& model) {
    if(!model.clips.empty())return {};
    for(auto& part:model.parts)if(!part.bones.empty() || !part.morphs.empty())return {};
    auto pose=model.pose("",0,false);RenderBounds result;
    for(auto& part:model.parts){
        auto matrix=glm::dmat4(model.rootInverse)*glm::dmat4(pose.at(part.node));
        for(auto& vertex:part.vertices){auto p=glm::dvec3(matrix*glm::dvec4(vertex.p,1));if(!finite(p))return {};include(result,p);}
    }
    return result;
}
} // namespace forge
