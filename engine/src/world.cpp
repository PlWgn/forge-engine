#include <forge/engine.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
namespace forge {
static glm::vec3 vec3(const Json& j, glm::vec3 fallback) {
    if(j.is_null()) return fallback;
    if(!j.is_array() || j.size()!=3) throw std::runtime_error("Expected three vector components");
    return {finiteNumber(j[0],"vector"),finiteNumber(j[1],"vector"),finiteNumber(j[2],"vector")};
}
static glm::vec4 vec4(const Json& j, glm::vec4 fallback) {
    if(j.is_null()) return fallback;
    if(!j.is_array() || j.size()!=4) throw std::runtime_error("Expected RGBA components");
    return {finiteNumber(j[0],"color/clip"),finiteNumber(j[1],"color/clip"),finiteNumber(j[2],"color/clip"),finiteNumber(j[3],"color/clip")};
}
std::shared_ptr<Entity> World::spawn(Json j) {
    if(!config)throw std::runtime_error("World needs a project configuration before spawn");
    j=validateEntity(*config,std::move(j));
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
    if(j.contains("clip") && !j["clip"].is_null()){e->clip=vec4(j["clip"],glm::vec4(0));e->clipped=true;}
    e->screen=j.value("screen",false); e->mass=checkedMass(j.value("mass",1.0f));
    e->text=j.value("text",""); e->fontSize=j.value("font_size",24.0f); e->scripts=j.value("scripts",Json::array()); e->data=j.value("data",Json::object());
    e->textKey=j.value("text_key","");e->textParams=j.value("text_params",Json::object());if(!e->textParams.is_object())throw std::runtime_error("text_params must be an object");
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
bool World::activeCollider(const Entity& e) const {
    return e.alive && e.collider.x>0 && e.collider.y>0 && (!is3d || e.collider.z>0);
}
bool World::overlaps(const Entity& a,const Entity& b) const {
    if(!activeCollider(a) || !activeCollider(b)) return false;
    auto d=glm::abs(glm::dvec3(a.position)-glm::dvec3(b.position));
    auto extent=(glm::dvec3(a.collider)+glm::dvec3(b.collider))*0.5;
    return d.x<=extent.x+0.00001 && d.y<=extent.y+0.00001 && (!is3d || d.z<=extent.z+0.00001);
}
static glm::vec3 physicsVector(const glm::dvec3& value,const Entity& e,const std::string& field) {
    auto context="Physics entity '"+e.id+"' "+field;
    return {checkedFloat(value.x,context),checkedFloat(value.y,context),checkedFloat(value.z,context)};
}
void World::physics(float dt) {
    contacts.clear();
    // Fixed substeps reduce tunnelling; this is a translational AABB solver.
    double count=std::ceil(double(dt)/double(1.0f/120));
    if(!std::isfinite(count) || dt<0 || count>std::numeric_limits<int>::max())
        throw std::runtime_error("Invalid physics time step");
    int steps=std::max(1,static_cast<int>(count)); double h=double(dt)/steps;
    for(int step=0;step<steps;++step) {
        std::vector<Entity*> colliders;
        for(auto& e:entities) if(e->alive) {
            if(e->dynamic) {
                checkedMass(e->mass);
                auto velocity=physicsVector(glm::dvec3(e->velocity)+glm::dvec3(gravity)*h,*e,"velocity");
                auto position=physicsVector(glm::dvec3(e->position)+glm::dvec3(velocity)*h,*e,"position");
                e->velocity=velocity; e->position=position;
            }
            if(activeCollider(*e))colliders.push_back(e.get());
        }
        for(size_t i=0;i<colliders.size();++i) for(size_t k=i+1;k<colliders.size();++k) {
            auto& a=*colliders[i]; auto& b=*colliders[k]; if(!overlaps(a,b)) continue;
            contacts.insert(std::minmax(a.id,b.id));
            if(a.trigger || b.trigger || (!a.dynamic && !b.dynamic)) continue;
            auto difference=glm::dvec3(a.position)-glm::dvec3(b.position);
            auto penetration=glm::max((glm::dvec3(a.collider)+glm::dvec3(b.collider))*0.5-glm::abs(difference),glm::dvec3(0));
            int axis=penetration.x<penetration.y?0:1; if(is3d && penetration.z<penetration[axis]) axis=2;
            double normal=difference[axis]>=0?1.0:-1.0;
            double invA=a.dynamic?1.0/double(a.mass):0, invB=b.dynamic?1.0/double(b.mass):0, sum=invA+invB;
            double weightA=invA/sum, weightB=invB/sum;
            auto positionA=checkedFloat(double(a.position[axis])+normal*penetration[axis]*weightA,"Physics entity '"+a.id+"' position");
            auto positionB=checkedFloat(double(b.position[axis])-normal*penetration[axis]*weightB,"Physics entity '"+b.id+"' position");
            double relative=(double(a.velocity[axis])-double(b.velocity[axis]))*normal;
            auto velocityA=a.velocity[axis], velocityB=b.velocity[axis];
            if(relative<0) {
                velocityA=checkedFloat(double(velocityA)-normal*relative*weightA,"Physics entity '"+a.id+"' velocity");
                velocityB=checkedFloat(double(velocityB)+normal*relative*weightB,"Physics entity '"+b.id+"' velocity");
            }
            // Commit a collision only after every result has passed validation.
            a.position[axis]=positionA; b.position[axis]=positionB;
            a.velocity[axis]=velocityA; b.velocity[axis]=velocityB;
        }
    }
}
std::shared_ptr<Entity> World::raycast(glm::vec3 origin,glm::vec3 direction,float distance) {
    auto ray=glm::dvec3(direction);auto length=glm::length(ray);
    if(length<0.00001 || distance<0) return {};
    ray/=length; std::shared_ptr<Entity> hit; double nearest=distance;
    for(auto& e:entities) if(activeCollider(*e)) {
        auto half=glm::dvec3(e->collider)*0.5; double low=0,high=nearest;
        for(int axis=0;axis<(is3d?3:2);++axis) {
            double mn=e->position[axis]-half[axis], mx=e->position[axis]+half[axis];
            if(std::abs(ray[axis])<1e-8) { if(origin[axis]<mn || origin[axis]>mx) { high=-1; break; } }
            else { double a=(mn-origin[axis])/ray[axis],b=(mx-origin[axis])/ray[axis]; if(a>b) std::swap(a,b); low=std::max(low,a); high=std::min(high,b); }
        }
        if(high>=low && low<=nearest) { nearest=low; hit=e; }
    }
    return hit;
}
}
