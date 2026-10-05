#define GLM_ENABLE_EXPERIMENTAL
#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/prefab.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
namespace forge {
namespace {
glm::dmat4 local(const Entity& e){
    glm::dmat4 m=glm::translate(glm::dmat4(1),glm::dvec3(e.position));
    auto r=glm::radians(glm::dvec3(e.rotation));
    m*=glm::eulerAngleXYZ(r.x,r.y,r.z);
    return glm::scale(m,glm::dvec3(e.scale));
}
glm::dquat orientation(glm::vec3 rotation){auto r=glm::radians(glm::dvec3(rotation));return glm::quat_cast(glm::eulerAngleXYZ(r.x,r.y,r.z));}
glm::vec3 angles(glm::dquat q){double x,y,z;glm::extractEulerAngleXYZ(glm::mat4_cast(q),x,y,z);auto r=glm::degrees(glm::dvec3(x,y,z));return {checkedFloat(r.x,"world rotation"),checkedFloat(r.y,"world rotation"),checkedFloat(r.z,"world rotation")};}
}
struct TransformCache {
    struct Node {
        glm::vec3 position,rotation,scale;
        std::string parent;
        bool screen,dynamic;
        glm::dmat4 matrix;
        glm::dquat orientation;
    };
    std::unordered_map<const Entity*,Node> nodes;
    std::unordered_map<std::string,std::vector<Entity*>> children;
};
glm::mat4 composeTransform(const Entity& e){auto m=local(e);for(int a=0;a<4;++a)for(int b=0;b<4;++b)checkedFloat(m[a][b],"Entity transform");return glm::mat4(m);}
void World::loadEntities(const Json& data){
    if(!data.is_array())throw std::runtime_error("Scene.entities must be array");
    std::set<std::string> reserved;
    for(const auto &item:data){auto checked=validateEntity(*config,item);if(checked.contains("id"))reserved.insert(checked["id"].get<std::string>());}
    assembling=true;
    try {
        for(auto item:data){
            auto resolved=entityPrefab(*config,item);
            if(!resolved.contains("id")){
                std::string id;do{id="entity_"+std::to_string(nextId++);}while(reserved.count(id) || find(id));
                item["id"]=id;
            }
            spawn(std::move(item));
        }
        assembling=false;syncTransforms();
    }catch(...){assembling=false;throw;}
}
void World::clearEntities(){for(auto& e:entities)e->alive=false;entities.clear();entityIndex.clear();transformCache.reset();}
void World::pruneIndex(){for(auto i=entityIndex.begin();i!=entityIndex.end();) {auto e=i->second.lock();if(!e || !e->alive)i=entityIndex.erase(i);else ++i;}}
void World::syncTransforms(){
    // Explicit native synchronization audits public fields, but does no matrix work if unchanged.
    ++transformAudits;
    if(transformCache){
        size_t alive=0;bool unchanged=true;
        for(auto& entry:entities)if(entry->alive){
            ++alive;auto found=transformCache->nodes.find(entry.get());
            if(found==transformCache->nodes.end()){unchanged=false;break;}
            auto& n=found->second;auto& e=*entry;
            if(n.position!=e.position || n.rotation!=e.rotation || n.scale!=e.scale || n.parent!=e.parent || n.screen!=e.screen || n.dynamic!=e.dynamic){unchanged=false;break;}
        }
        if(unchanged && alive==transformCache->nodes.size())return;
    }

    // Iterative traversal: arbitrary declaration order; no C++ recursion for deep trees.
    struct Pose {glm::dmat4 matrix;glm::dquat rotation;};
    std::unordered_map<const Entity*,Pose> ready;
    for(auto& entry:entities)if(entry->alive && !ready.count(entry.get())){
        std::vector<Entity*> chain;std::set<const Entity*> visiting;auto* e=entry.get();
        while(e && !ready.count(e)){
            if(!visiting.insert(e).second)throw std::runtime_error("Hierarchy cycle at '"+e->id+"'");
            if(!e->parent.empty() && e->dynamic)throw std::runtime_error("Dynamic bodies must be hierarchy roots: "+e->id);
            chain.push_back(e);
            if(e->parent.empty())e=nullptr;
            else {auto parent=find(e->parent);if(!parent)throw std::runtime_error("Missing parent '"+e->parent+"' for "+e->id);
                if(parent->screen!=e->screen)throw std::runtime_error("Parent and child must share screen/world coordinates");
                e=parent.get();}
        }
        for(auto i=chain.rbegin();i!=chain.rend();++i){auto* child=*i;auto m=local(*child);auto q=orientation(child->rotation);
            if(!child->parent.empty()){auto& parent=ready.at(find(child->parent).get());m=parent.matrix*m;q=parent.rotation*q;}
            for(int a=0;a<4;++a)for(int b=0;b<4;++b)checkedFloat(m[a][b],"Hierarchy matrix for "+child->id);
            if(rigidPhysics(*this) && activeCollider(*child)){auto body=*child;body.position=glm::vec3(m[3]);body.rotation=angles(q);validatePhysicsEntity(body);}
            ready.emplace(child,Pose{m,q});
        }
    }
    auto cache=std::make_shared<TransformCache>();
    for(auto& [e,pose]:ready){
        cache->nodes.emplace(e,TransformCache::Node{e->position,e->rotation,e->scale,e->parent,e->screen,e->dynamic,pose.matrix,pose.rotation});
        if(!e->parent.empty())cache->children[e->parent].push_back(const_cast<Entity*>(e));
    }
    // Commit only when the complete graph and all products are valid.
    for(auto& [e,pose]:ready){auto* target=const_cast<Entity*>(e);target->worldMatrix=glm::mat4(pose.matrix);target->worldRotation=angles(pose.rotation);}
    transformComputations+=ready.size();transformCache=std::move(cache);
}
void World::setLocalTransform(Entity& e,glm::vec3 position,glm::vec3 rotation,glm::vec3 scale){
    // Validate only the affected subtree. Reuse double-precision parent poses.
    if(!e.alive){auto copy=e;copy.position=position;copy.rotation=rotation;copy.scale=scale;composeTransform(copy);e.position=position;e.rotation=rotation;e.scale=scale;return;}
    if(find(e.id).get()!=&e)throw std::runtime_error("Entity is not in current world");
    if(!transformCache)syncTransforms();
    struct Pending {Entity* entity;TransformCache::Node node;glm::vec3 worldRotation;};
    std::vector<Pending> pending;std::vector<Entity*> queue{&e};
    std::unordered_map<const Entity*,size_t> ready;
    for(size_t i=0;i<queue.size();++i){
        auto* child=queue[i];auto copy=*child;
        if(child==&e){copy.position=position;copy.rotation=rotation;copy.scale=scale;}
        if(!copy.parent.empty() && copy.dynamic)throw std::runtime_error("Dynamic bodies must be hierarchy roots");
        auto matrix=local(copy);auto q=orientation(copy.rotation);
        if(!copy.parent.empty()){
            auto parent=find(copy.parent);if(!parent)throw std::runtime_error("Missing hierarchy parent");
            if(parent->screen!=copy.screen)throw std::runtime_error("Parent and child must share screen/world coordinates");
            auto replacement=ready.find(parent.get());
            auto& pose=replacement==ready.end()?transformCache->nodes.at(parent.get()):pending[replacement->second].node;
            matrix=pose.matrix*matrix;q=pose.orientation*q;
        }
        for(int a=0;a<4;++a)for(int b=0;b<4;++b)checkedFloat(matrix[a][b],"Hierarchy matrix for "+copy.id);
        auto worldAngles=angles(q);
        if(rigidPhysics(*this) && activeCollider(copy)){auto body=copy;body.position=glm::vec3(matrix[3]);body.rotation=worldAngles;validatePhysicsEntity(body);}
        ready[child]=pending.size();
        pending.push_back({child,{copy.position,copy.rotation,copy.scale,copy.parent,copy.screen,copy.dynamic,matrix,q},worldAngles});
        auto children=transformCache->children.find(child->id);
        if(children!=transformCache->children.end())queue.insert(queue.end(),children->second.begin(),children->second.end());
    }
    for(auto& next:pending){auto& target=*next.entity;target.worldMatrix=glm::mat4(next.node.matrix);target.worldRotation=next.worldRotation;transformCache->nodes.at(next.entity)=std::move(next.node);}
    e.position=position;e.rotation=rotation;e.scale=scale;transformComputations+=pending.size();
}
void World::setPositions(const std::vector<std::pair<std::shared_ptr<Entity>,glm::vec3>>& updates){
    syncTransforms();std::set<const Entity*> seen;std::vector<glm::vec3> previous;
    for(auto& [e,position]:updates){
        if(!e || find(e->id)!=e || !seen.insert(e.get()).second)throw std::runtime_error("set_positions needs distinct current entities");
        for(int axis=0;axis<3;++axis)checkedFloat(position[axis],"position");
        previous.push_back(e->position);
    }
    for(auto& [e,position]:updates)e->position=position;
    try{syncTransforms();}catch(...){for(size_t i=0;i<updates.size();++i)updates[i].first->position=previous[i];syncTransforms();throw;}
}
glm::vec3 World::worldPosition(const Entity& e)const{return glm::vec3(e.worldMatrix[3]);}
void World::setWorldPosition(Entity& e,glm::vec3 value){
    if(!transformCache)syncTransforms();auto next=glm::dvec3(value);
    if(!e.parent.empty()){auto p=find(e.parent);auto m=glm::dmat4(p->worldMatrix);if(std::abs(glm::determinant(m))<1e-15)throw std::runtime_error("Parent transform is singular");next=glm::dvec3(glm::inverse(m)*glm::dvec4(next,1));}
    glm::vec3 checked{checkedFloat(next.x,"local position"),checkedFloat(next.y,"local position"),checkedFloat(next.z,"local position")};
    setLocalTransform(e,checked,e.rotation,e.scale);
}
void World::reparent(Entity& e,const std::string& id,bool keepWorld){
    if(find(e.id).get()!=&e)throw std::runtime_error("Entity is not in current world");
    syncTransforms();auto matrix=glm::dmat4(e.worldMatrix);auto oldParent=e.parent;auto pos=e.position,rot=e.rotation,scale=e.scale;
    if(!id.empty() && !find(id))throw std::runtime_error("Missing parent: "+id);
    e.parent=id;
    try{
        if(keepWorld){
            if(!id.empty()){auto parent=glm::dmat4(find(id)->worldMatrix);if(std::abs(glm::determinant(parent))<1e-15)throw std::runtime_error("Parent transform is singular");matrix=glm::inverse(parent)*matrix;}
            e.position=glm::vec3(matrix[3]);glm::dmat3 basis(matrix);glm::dvec3 lengths{glm::length(basis[0]),glm::length(basis[1]),glm::length(basis[2])};
            if(lengths.x<1e-12 || lengths.y<1e-12 || lengths.z<1e-12)throw std::runtime_error("Cannot reparent a singular transform with keep_world");
            if(glm::determinant(basis)<0)lengths.x=-lengths.x;
            for(int a=0;a<3;++a)basis[a]/=lengths[a];
            if(std::abs(glm::dot(basis[0],basis[1]))>1e-5 || std::abs(glm::dot(basis[0],basis[2]))>1e-5 || std::abs(glm::dot(basis[1],basis[2]))>1e-5)throw std::runtime_error("keep_world requires a TRS transform without shear");
            e.rotation=angles(glm::quat_cast(basis));e.scale=glm::vec3(lengths);
        }
        syncTransforms();
    }catch(...){e.parent=oldParent;e.position=pos;e.rotation=rot;e.scale=scale;syncTransforms();throw;}
}
std::vector<std::shared_ptr<Entity>> World::children(const Entity& e,bool recursive){
    std::vector<std::shared_ptr<Entity>> result;std::set<std::string> parents{e.id};
    do {size_t previous=parents.size();for(auto& child:entities)if(child->alive && !parents.count(child->id) && parents.count(child->parent)){result.push_back(child);if(recursive)parents.insert(child->id);}if(!recursive || previous==parents.size())break;}while(true);
    return result;
}
void World::destroy(Entity& e,bool cascade){
    if(!e.alive)return;
    auto descendants=children(e,cascade);
    if(cascade)for(auto& child:descendants)child->alive=false;
    else {
        struct Old {std::shared_ptr<Entity> child;std::string parent;glm::vec3 position,rotation,scale;};std::vector<Old> previous;
        for(auto& child:descendants)previous.push_back({child,child->parent,child->position,child->rotation,child->scale});
        try{for(auto& child:descendants)reparent(*child,"",true);}catch(...){for(auto& old:previous){old.child->parent=old.parent;old.child->position=old.position;old.child->rotation=old.rotation;old.child->scale=old.scale;}syncTransforms();throw;}
    }
    e.alive=false;transformCache.reset();
}
} // namespace forge
