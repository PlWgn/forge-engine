#include <forge/engine.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
namespace forge {
static glm::vec3 vec3(const Json& j, glm::vec3 fallback) {
    if(j.is_null()) return fallback;
    if(!j.is_array() || j.size()!=3) throw std::runtime_error("Expected three vector components");
    return {j.at(0).get<float>(),j.at(1).get<float>(),j.at(2).get<float>()};
}
static glm::vec4 vec4(const Json& j, glm::vec4 fallback) {
    if(j.is_null()) return fallback;
    if(!j.is_array() || j.size()!=4) throw std::runtime_error("Expected RGBA components");
    return {j[0].get<float>(),j[1].get<float>(),j[2].get<float>(),j[3].get<float>()};
}
std::shared_ptr<Entity> World::spawn(Json j) {
    if(j.contains("prefab")) { auto base=readJson(config->asset("objects",j["prefab"])); j.erase("prefab"); base.merge_patch(j); j=base; }
    auto e=std::make_shared<Entity>();
    if(j.contains("id"))e->id=j["id"].get<std::string>();else {do {e->id="entity_"+std::to_string(nextId++);}while(find(e->id));}
    if(find(e->id)) throw std::runtime_error("Duplicate entity id: "+e->id);
    e->name=j.value("name",e->id); e->kind=j.value("kind","sprite");
    if(e->kind!="sprite" && e->kind!="cube" && e->kind!="mesh" && e->kind!="text" && e->kind!="empty") throw std::runtime_error("Unknown kind: "+e->kind);
    e->model=j.value("model",""); e->texture=j.value("texture",""); e->material=j.value("material","");
    if(!e->material.empty()) { auto m=readJson(config->asset("materials",e->material)); e->color=vec4(m.value("color",Json()),e->color); if(e->texture.empty()) e->texture=m.value("texture",""); }
    e->position=vec3(j.value("position",Json()),e->position); e->rotation=vec3(j.value("rotation",Json()),e->rotation);
    e->scale=vec3(j.value("scale",Json()),e->scale); e->velocity=vec3(j.value("velocity",Json()),e->velocity);
    e->collider=vec3(j.value("collider",Json()),e->collider); e->color=vec4(j.value("color",Json()),e->color);
    e->dynamic=j.value("dynamic",false); e->trigger=j.value("trigger",false); e->visible=j.value("visible",true);
    e->screen=j.value("screen",false); e->mass=j.value("mass",1.0f); if(e->mass<=0) throw std::runtime_error("mass must be positive");
    e->text=j.value("text",""); e->fontSize=j.value("font_size",24.0f); e->scripts=j.value("scripts",Json::array()); e->data=j.value("data",Json::object());
    entities.push_back(e); return e;
}
std::shared_ptr<Entity> World::find(const std::string& id) {
    for(auto& e:entities) if(e->alive && e->id==id) return e; return {};
}
void World::load(const std::string& scenePath) {
    for(auto& e:entities) e->alive=false;
    entities.clear(); contacts.clear();
    auto file=config->asset("scenes",scenePath);
    if(file.extension()==".json") scene=readJson(file);
    else if(file.extension()==".py") scene=Json::object();
    else throw std::runtime_error("Scene must be .json or .py: "+file.u8string());
    is3d=scene.value("mode","2d")=="3d";
    gravity=vec3(scene.value("gravity",Json()),is3d?glm::vec3(0,-9.81f,0):glm::vec3(0,980,0));
    background=vec4(scene.value("background",Json()),glm::vec4(0.025f,0.04f,0.075f,1));
    auto camera=scene.value("camera",Json::object());
    cameraPosition=vec3(camera.value("position",Json()),glm::vec3(0,0,5)); cameraTarget=vec3(camera.value("target",Json()),glm::vec3(0)); fov=camera.value("fov",60.0f);
    if(fov<=0 || fov>=179) throw std::runtime_error("Camera fov must be between 0 and 179");
    for(auto& j:scene.value("entities",Json::array())) spawn(j);
}
bool World::overlaps(const Entity& a,const Entity& b) const {
    if(a.collider.x<=0 || a.collider.y<=0 || b.collider.x<=0 || b.collider.y<=0) return false;
    auto d=glm::abs(a.position-b.position), extent=(a.collider+b.collider)*0.5f;
    return d.x<=extent.x+0.00001f && d.y<=extent.y+0.00001f && (!is3d || (a.collider.z>0 && b.collider.z>0 && d.z<=extent.z+0.00001f));
}
void World::physics(float dt) {
    contacts.clear();
    // Fixed substeps reduce tunnelling; this is a translational AABB solver.
    int steps=std::max(1,static_cast<int>(std::ceil(dt/(1.0f/120)))); float h=dt/steps;
    for(int step=0;step<steps;++step) {
        for(auto& e:entities) if(e->alive && e->dynamic) { e->velocity+=gravity*h; e->position+=e->velocity*h; }
        for(size_t i=0;i<entities.size();++i) for(size_t k=i+1;k<entities.size();++k) {
            auto& a=*entities[i]; auto& b=*entities[k]; if(!a.alive || !b.alive || !overlaps(a,b)) continue;
            contacts.insert(std::minmax(a.id,b.id));
            if(a.trigger || b.trigger || (!a.dynamic && !b.dynamic)) continue;
            auto difference=a.position-b.position; auto penetration=glm::max((a.collider+b.collider)*0.5f-glm::abs(difference),glm::vec3(0));
            int axis=penetration.x<penetration.y?0:1; if(is3d && penetration.z<penetration[axis]) axis=2;
            float normal=difference[axis]>=0?1.0f:-1.0f;
            float invA=a.dynamic?1/a.mass:0, invB=b.dynamic?1/b.mass:0, sum=invA+invB;
            a.position[axis]+=normal*penetration[axis]*invA/sum; b.position[axis]-=normal*penetration[axis]*invB/sum;
            float relative=(a.velocity[axis]-b.velocity[axis])*normal;
            if(relative<0) { float impulse=-relative/sum; a.velocity[axis]+=normal*impulse*invA; b.velocity[axis]-=normal*impulse*invB; }
        }
    }
}
std::shared_ptr<Entity> World::raycast(glm::vec3 origin,glm::vec3 direction,float distance) {
    if(glm::length(direction)<0.00001f || distance<0) return {};
    direction=glm::normalize(direction); std::shared_ptr<Entity> hit; float nearest=distance;
    for(auto& e:entities) if(e->alive && e->collider.x>0 && e->collider.y>0) {
        auto half=e->collider*0.5f; float low=0,high=nearest;
        for(int axis=0;axis<(is3d?3:2);++axis) {
            float mn=e->position[axis]-half[axis], mx=e->position[axis]+half[axis];
            if(std::abs(direction[axis])<1e-8f) { if(origin[axis]<mn || origin[axis]>mx) { high=-1; break; } }
            else { float a=(mn-origin[axis])/direction[axis],b=(mx-origin[axis])/direction[axis]; if(a>b) std::swap(a,b); low=std::max(low,a); high=std::min(high,b); }
        }
        if(high>=low && low<=nearest) { nearest=low; hit=e; }
    }
    return hit;
}
}
