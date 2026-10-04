#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/particles.hpp>
#include <forge/material.hpp>
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
    if(!e->material.empty()) { auto m=validateMaterial(*config,readJson(config->asset("materials",e->material)));e->materialData=m; e->color=vec4(m.value("color",Json()),e->color); if(e->texture.empty()) e->texture=m.value("texture",""); }
    e->position=vec3(j.value("position",Json()),e->position); e->rotation=vec3(j.value("rotation",Json()),e->rotation);
    e->scale=vec3(j.value("scale",Json()),e->scale); e->velocity=vec3(j.value("velocity",Json()),e->velocity);
    e->collider=vec3(j.value("collider",Json()),e->collider); e->color=vec4(j.value("color",Json()),e->color);
    e->uv=vec4(j.value("uv",Json()),e->uv);e->layer=j.value("layer",1u);e->castsShadow=j.value("casts_shadow",true);e->uniforms=j.value("uniforms",Json::object());
    e->animation=j.value("animation","");e->animationSpeed=j.value("animation_speed",1.f);e->animationPlaying=!e->animation.empty();e->animationLoop=j.value("animation_loop",true);
    e->dynamic=j.value("dynamic",false); e->trigger=j.value("trigger",false); e->visible=j.value("visible",true);
    if(j.contains("clip") && !j["clip"].is_null()){e->clip=vec4(j["clip"],glm::vec4(0));e->clipped=true;}
    e->screen=j.value("screen",false); e->mass=checkedMass(j.value("mass",1.0f));
    e->angularVelocity=vec3(j.value("angular_velocity",Json()),glm::vec3(0));e->rigidBody=validateRigidBody(j.value("rigid_body",Json::object()));
    e->text=j.value("text",""); e->fontSize=j.value("font_size",24.0f); e->scripts=j.value("scripts",Json::array()); e->data=j.value("data",Json::object());
    e->textKey=j.value("text_key","");e->textParams=j.value("text_params",Json::object());if(!e->textParams.is_object())throw std::runtime_error("text_params must be an object");
    if(rigidPhysics(*this) && activeCollider(*e))validatePhysicsEntity(*e);
    if(j.contains("material_properties"))e->materialData=validateMaterial(*config,j["material_properties"]);
    e->parent=j.value("parent","");
    e->worldMatrix=composeTransform(*e);e->worldRotation=e->rotation;
    transformCache.reset();
    if(!assembling && !e->parent.empty()){
        entities.push_back(e);entityIndex[e->id]=e;
        try{syncTransforms();}catch(...){entities.pop_back();entityIndex.erase(e->id);throw;}
    }else {entities.push_back(e);entityIndex[e->id]=e;}
    return e;
}
void World::configureSimulation(const Json& data) {
    syncTransforms();
    auto settings=config->data.value("physics",Json::object());settings.merge_patch(data.value("physics",Json::object()));
    settings=validatePhysics(std::move(settings));
    if(settings["backend"]=="bullet" && !is3d){
        if(data.value("physics",Json::object()).value("backend","legacy")=="bullet")throw std::runtime_error("Bullet backend requires a 3D scene");
        settings["backend"]="legacy";
    }
    auto definitions=data.value("emitters",Json::array());validateEmitters(*config,definitions);
    if(settings["backend"]=="bullet"){
        for(int axis=0;axis<3;++axis)if(!std::isfinite(gravity[axis]) || std::abs(double(gravity[axis]))>1e6)
            throw std::runtime_error("physics gravity outside allowed range");
        size_t count=0;for(auto& e:entities)if(activeCollider(*e)){auto body=*e;body.position=worldPosition(*e);body.rotation=e->worldRotation;validatePhysicsEntity(body);++count;}
        if(count>settings.value("max_bodies",10000u))throw std::runtime_error("physics.max_bodies exceeded");
    }
    auto next=std::make_shared<Particles>();
    for(auto& definition:definitions){auto id=next->create(*config,definition);next->burst(*this,id,next->find(id).settings["burst"].get<unsigned>());}
    physicsSettings=std::move(settings);physics3d.reset();particles=std::move(next);
}
std::shared_ptr<Entity> World::find(const std::string& id) {
    auto found=entityIndex.find(id);if(found==entityIndex.end())return {};auto e=found->second.lock();return e && e->alive?e:nullptr;
}
void World::load(const std::string& scenePath) {
    clearEntities(); contacts.clear();
    physicsSettings=Json::object();physics3d.reset();particles.reset();
    auto file=config->asset("scenes",scenePath);
    if(file.extension()==".json") scene=readJson(file);
    else if(file.extension()==".py") scene=Json::object();
    else throw std::runtime_error("Scene must be .json or .py: "+file.u8string());
    is3d=scene.value("mode","2d")=="3d";
    physicsEnabled=scene.value("physics_enabled",true);renderSettings=config->data.value("rendering",Json::object());renderSettings.merge_patch(scene.value("rendering",Json::object()));renderSettings=validateRenderSettings(std::move(renderSettings));
    gravity=vec3(scene.value("gravity",Json()),is3d?glm::vec3(0,-9.81f,0):glm::vec3(0,980,0));
    background=vec4(scene.value("background",Json()),glm::vec4(0.025f,0.04f,0.075f,1));
    auto camera=scene.value("camera",Json::object());
    cameraPosition=vec3(camera.value("position",Json()),glm::vec3(0,0,5)); cameraTarget=vec3(camera.value("target",Json()),glm::vec3(0)); fov=camera.value("fov",60.0f);
    if(fov<=0 || fov>=179) throw std::runtime_error("Camera fov must be between 0 and 179");
    loadEntities(scene.value("entities",Json::array()));
    configureSimulation(scene);
}
bool World::activeCollider(const Entity& e) const {
    return e.alive && e.collider.x>0 && e.collider.y>0 && (!is3d || e.collider.z>0);
}
bool World::overlaps(const Entity& a,const Entity& b) const {
    if(rigidPhysics(*this))return physics3D(const_cast<World&>(*this)).overlaps(const_cast<World&>(*this),a,b);
    const_cast<World*>(this)->syncTransforms();
    if(!activeCollider(a) || !activeCollider(b)) return false;
    auto d=glm::abs(glm::dvec3(worldPosition(a))-glm::dvec3(worldPosition(b)));
    auto extent=(glm::dvec3(a.collider)+glm::dvec3(b.collider))*0.5;
    return d.x<=extent.x+0.00001 && d.y<=extent.y+0.00001 && (!is3d || d.z<=extent.z+0.00001);
}
static glm::vec3 physicsVector(const glm::dvec3& value,const Entity& e,const std::string& field) {
    auto context="Physics entity '"+e.id+"' "+field;
    return {checkedFloat(value.x,context),checkedFloat(value.y,context),checkedFloat(value.z,context)};
}
void World::physics(float dt) {
    if(rigidPhysics(*this)){try{physics3D(*this).step(*this,dt);}catch(...){physics3d.reset();throw;}return;}
    contacts.clear();physicsCandidates=0;
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
                auto velocity=physicsVector(glm::dvec3(e->velocity)+(glm::dvec3(gravity)+glm::dvec3(e->force)/double(e->mass))*h,*e,"velocity");
                auto position=physicsVector(glm::dvec3(e->position)+glm::dvec3(velocity)*h,*e,"position");
                e->velocity=velocity; e->position=position;
            }
            if(activeCollider(*e))colliders.push_back(e.get());
        }
        syncTransforms();
        struct Bound {size_t index;double low,high;};std::vector<Bound> sweep;
        for(size_t i=0;i<colliders.size();++i){auto& e=*colliders[i];auto p=worldPosition(e);sweep.push_back({i,double(p.x)-e.collider.x*.5,double(p.x)+e.collider.x*.5});}
        std::stable_sort(sweep.begin(),sweep.end(),[](auto& a,auto& b){return a.low<b.low;});
        std::vector<std::pair<size_t,size_t>> pairs;
        for(size_t i=0;i<sweep.size();++i)for(size_t k=i+1;k<sweep.size() && sweep[k].low<=sweep[i].high+.00001;++k)pairs.push_back(std::minmax(sweep[i].index,sweep[k].index));
        std::sort(pairs.begin(),pairs.end());physicsCandidates+=pairs.size();
        for(auto [i,k]:pairs) {
            auto& a=*colliders[i];auto& b=*colliders[k];
            auto difference=glm::dvec3(worldPosition(a))-glm::dvec3(worldPosition(b));auto extent=(glm::dvec3(a.collider)+glm::dvec3(b.collider))*.5;
            auto distance=glm::abs(difference);if(distance.x>extent.x+.00001 || distance.y>extent.y+.00001 || (is3d && distance.z>extent.z+.00001))continue;
            contacts.insert(std::minmax(a.id,b.id));
            if(a.trigger || b.trigger || (!a.dynamic && !b.dynamic)) continue;
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
            if(a.dynamic)a.worldMatrix[3]=glm::vec4(a.position,1);if(b.dynamic)b.worldMatrix[3]=glm::vec4(b.position,1);
        }
        syncTransforms();
    }
}
std::shared_ptr<Entity> World::raycast(glm::vec3 origin,glm::vec3 direction,float distance,bool synchronize) {
    if(rigidPhysics(*this)){auto result=physics3D(*this).raycast(*this,origin,direction,distance,65535,"",true,synchronize);return result.is_null()?nullptr:find(result["entity"]);}
    if(synchronize)syncTransforms();
    auto ray=glm::dvec3(direction);auto length=glm::length(ray);
    if(length<0.00001 || distance<0) return {};
    ray/=length; std::shared_ptr<Entity> hit; double nearest=distance;
    for(auto& e:entities) if(activeCollider(*e)) {
        auto half=glm::dvec3(e->collider)*0.5; double low=0,high=nearest;
        for(int axis=0;axis<(is3d?3:2);++axis) {
            double mn=double(worldPosition(*e)[axis])-half[axis], mx=double(worldPosition(*e)[axis])+half[axis];
            if(std::abs(ray[axis])<1e-8) { if(origin[axis]<mn || origin[axis]>mx) { high=-1; break; } }
            else { double a=(mn-origin[axis])/ray[axis],b=(mx-origin[axis])/ray[axis]; if(a>b) std::swap(a,b); low=std::max(low,a); high=std::min(high,b); }
        }
        if(high>=low && low<=nearest) { nearest=low; hit=e; }
    }
    return hit;
}
Json World::moveCharacter(Entity& body,glm::vec3 delta,float skin){
    if(rigidPhysics(*this))return physics3D(*this).move(*this,body,delta,skin);
    if(!activeCollider(body))throw std::runtime_error("Character needs an active collider");
    if(!std::isfinite(skin) || skin<0 || skin>1)throw std::runtime_error("Character skin must be in 0..1");
    syncTransforms();
    auto position=glm::dvec3(worldPosition(body));auto half=glm::dvec3(body.collider)*.5;Json hits=Json::array();bool grounded=false;
    for(int axis: {0,2,1}){if(!is3d && axis==2)continue;double requested=checkedFloat(delta[axis],"character delta"),allowed=requested;Entity* hit=nullptr;
        for(auto& pointer:entities){auto& other=*pointer;if(&other==&body || other.trigger || !activeCollider(other))continue;auto otherHalf=glm::dvec3(other.collider)*.5;
            bool aligned=true;for(int k=0;k<(is3d?3:2);++k)if(k!=axis && std::abs(position[k]-double(worldPosition(other)[k]))>=half[k]+otherHalf[k]-1e-8)aligned=false;
            if(!aligned)continue;
            double low=double(worldPosition(other)[axis])-otherHalf[axis],high=double(worldPosition(other)[axis])+otherHalf[axis];
            if(requested>0 && position[axis]+half[axis]<=low+skin){double gap=std::max(0.0,low-position[axis]-half[axis]-skin);if(gap<allowed){allowed=gap;hit=&other;}}
            if(requested<0 && position[axis]-half[axis]>=high-skin){double gap=std::min(0.0,high-position[axis]+half[axis]+skin);if(gap>allowed){allowed=gap;hit=&other;}}
        }
        position[axis]+=allowed;
        if(hit){glm::vec3 normal(0);normal[axis]=requested>0?-1.f:1.f;hits.push_back({{"entity",hit->id},{"normal",{normal.x,normal.y,normal.z}}});if(axis==1 && (is3d?requested<0:requested>0))grounded=true;}
    }
    auto next=physicsVector(position,body,"character position");setWorldPosition(body,next);
    return {{"position",{next.x,next.y,next.z}},{"grounded",grounded},{"hits",hits}};
}
Json World::serialize()const{
    auto array3=[](glm::vec3 v){return Json::array({v.x,v.y,v.z});};auto array4=[](glm::vec4 v){return Json::array({v.x,v.y,v.z,v.w});};Json result=scene;
    result["mode"]=is3d?"3d":"2d";result["gravity"]=array3(gravity);result["background"]=array4(background);result["physics_enabled"]=physicsEnabled;result["rendering"]=renderSettings;
    result["physics"]=physicsSettings;result["emitters"]=particles?particles->serialize():Json::array();
    result["camera"]={{"position",array3(cameraPosition)},{"target",array3(cameraTarget)},{"fov",fov}};result["entities"]=Json::array();
    for(auto& pointer:entities){auto& e=*pointer;if(!e.alive)continue;Json data={{"id",e.id},{"name",e.name},{"kind",e.kind},{"position",array3(e.position)},{"rotation",array3(e.rotation)},{"scale",array3(e.scale)},{"velocity",array3(e.velocity)},{"collider",array3(e.collider)},{"color",array4(e.color)},{"uv",array4(e.uv)},{"layer",e.layer},{"casts_shadow",e.castsShadow},{"uniforms",e.uniforms},{"dynamic",e.dynamic},{"trigger",e.trigger},{"visible",e.visible},{"screen",e.screen},{"mass",e.mass},{"font_size",e.fontSize},{"scripts",e.scripts},{"data",e.data}};
        for(auto& field:std::vector<std::pair<std::string,std::string>>{{"model",e.model},{"texture",e.texture},{"material",e.material},{"text",e.text},{"text_key",e.textKey},{"animation",e.animation}})if(!field.second.empty())data[field.first]=field.second;
        if(!e.textKey.empty())data["text_params"]=e.textParams;if(e.clipped)data["clip"]=array4(e.clip);if(!e.animation.empty()){data["animation_speed"]=e.animationSpeed;data["animation_loop"]=e.animationLoop;}result["entities"].push_back(std::move(data));
        if(!e.parent.empty())result["entities"].back()["parent"]=e.parent;
        if(!e.materialData.empty())result["entities"].back()["material_properties"]=e.materialData;
        result["entities"].back()["angular_velocity"]=array3(e.angularVelocity);result["entities"].back()["rigid_body"]=e.rigidBody;
    }return result;
}
}
