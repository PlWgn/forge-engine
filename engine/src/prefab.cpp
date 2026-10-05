#include <forge/engine.hpp>
#include <forge/prefab.hpp>
#include <pybind11/stl.h>
#include <algorithm>
namespace forge {
namespace {
Json document(const Config& config,const std::string& name,std::set<fs::path>& ancestors,const fs::path &overridePath={},const Json *candidate=nullptr){
    auto path=config.asset("objects",name);
    if(ancestors.size()>=64 || !ancestors.insert(path).second)throw std::runtime_error("Prefab inheritance cycle/depth: "+name);
    auto result=candidate && path==overridePath?*candidate:readJson(path);
    if(!result.is_object())throw std::runtime_error("Prefab must be an object: "+name);
    if(result.contains("extends")){
        auto parent=document(config,result.at("extends").get<std::string>(),ancestors,overridePath,candidate);
        result.erase("extends");parent.merge_patch(result);result=std::move(parent);
    }
    ancestors.erase(path);return result;
}
Json entity(const Config& config,Json value,std::set<fs::path>& ancestors,const fs::path &overridePath={},const Json *candidate=nullptr){
    if(!value.is_object())throw std::runtime_error("Entity must be an object");
    if(value.contains("prefab")){
        auto name=value.at("prefab").get<std::string>();auto path=config.asset("objects",name);
        if(ancestors.size()>=64 || !ancestors.insert(path).second)throw std::runtime_error("Entity prefab cycle/depth: "+name);
        std::set<fs::path> inheritance;auto base=document(config,name,inheritance,overridePath,candidate);
        if(base.contains("entities"))throw std::runtime_error("Use instantiate_prefab for a prefab hierarchy: "+name);
        base=entity(config,std::move(base),ancestors,overridePath,candidate);value.erase("prefab");base.merge_patch(value);value=std::move(base);
        ancestors.erase(path);
    }
    return value;
}
}
Json entityPrefab(const Config& config,Json value){std::set<fs::path> ancestors;return entity(config,std::move(value),ancestors);}
Json prefabCandidate(const Config& config,const std::string& name,const Json* candidate){
    std::set<fs::path> ancestors;auto result=document(config,name,ancestors,config.asset("objects",name),candidate);
    if(!result.contains("entities")){
        std::set<fs::path> chain;result=entity(config,std::move(result),chain,config.asset("objects",name),candidate);result["id"]=result.value("id","root");
        result=Json{{"entities",Json::array({result})}};
    }
    auto& list=result["entities"];
    if(!list.is_array() || list.empty() || list.size()>8192)throw std::runtime_error("Prefab needs 1..8192 entities");
    std::map<std::string,std::string> parents;
    for(auto& item:list){
        std::set<fs::path> chain;item=entity(config,item,chain,config.asset("objects",name),candidate);
        item=validateEntity(config,item);auto id=item.value("id","");
        if(id.empty() || !parents.emplace(id,item.value("parent","")).second)throw std::runtime_error("Prefab needs distinct nonempty local IDs");
    }
    for(auto& [id,parent]:parents)if(!parent.empty() && !parents.count(parent))throw std::runtime_error("Prefab parent missing: "+parent);
    size_t roots=0;std::map<std::string,int> state;
    for(auto& [id,parent]:parents){
        if(parent.empty()){++roots;continue;}
        if(!parents.count(parent))throw std::runtime_error("Prefab parent missing: "+parent);
        std::vector<std::string> chain;auto current=id;
        while(!current.empty() && state[current]!=2){
            if(state[current]==1)throw std::runtime_error("Prefab hierarchy cycle: "+current);
            state[current]=1;chain.push_back(current);current=parents.at(current);
        }
        for(auto& node:chain)state[node]=2;
    }
    if(roots!=1)throw std::runtime_error("Prefab hierarchy needs exactly one root");
    return result;
}
Json prefabDocument(const Config& config,const std::string& name){return prefabCandidate(config,name,nullptr);}
std::map<std::string,std::shared_ptr<Entity>> instantiatePrefab(World& world,const std::string& file,const std::string& requested,const Json& overrides,const std::string& parent,glm::vec3 offset){
    if(!world.config)throw std::runtime_error("Prefab needs project configuration");
    if(!parent.empty() && !world.find(parent))throw std::runtime_error("Prefab attachment parent missing");
    for(int axis=0;axis<3;++axis)checkedFloat(offset[axis],"prefab position");
    if(!overrides.is_object())throw std::runtime_error("Prefab overrides must be an object keyed by local ID");
    auto source=prefabDocument(*world.config,file);auto list=source["entities"];
    std::set<std::string> localIds;for(auto& e:list)localIds.insert(e.at("id"));
    for(auto it=overrides.begin();it!=overrides.end();++it){
        if(!localIds.count(it.key()) || !it.value().is_object() || it.value().contains("id") || it.value().contains("parent"))throw std::runtime_error("Invalid prefab override: "+it.key());
    }
    auto prefix=requested;
    if(prefix.empty()){
        do{prefix="prefab_"+std::to_string(world.nextId++)+"_";}while(std::any_of(localIds.begin(),localIds.end(),[&](auto& id){return bool(world.find(prefix+id));}));
    }
    std::vector<std::string> ids;
    for(auto& item:list){
        auto local=item.at("id").get<std::string>();ids.push_back(local);
        if(overrides.contains(local))item.merge_patch(overrides[local]);
        item["id"]=prefix+local;auto baseParent=item.value("parent","");
        item["parent"]=baseParent.empty()?parent:prefix+baseParent;
        if(baseParent.empty()){
            auto p=item.value("position",Json::array({0,0,0}));
            for(int axis=0;axis<3;++axis)p[axis]=checkedFloat(double(finiteNumber(p[axis],"prefab position"))+offset[axis],"prefab position");
            item["position"]=p;
        }
        item=validateEntity(*world.config,std::move(item));
        if(world.find(item.at("id")))throw std::runtime_error("Prefab entity ID already exists");
    }
    size_t size=world.entities.size();bool assembling=world.assembling;
    std::map<std::string,std::shared_ptr<Entity>> result;
    world.syncTransforms();world.assembling=true;
    try{
        for(size_t i=0;i<list.size();++i)result[ids[i]]=world.spawn(list[i]);
        world.assembling=assembling;world.syncTransforms();
    }catch(...){
        for(size_t i=size;i<world.entities.size();++i){world.entities[i]->alive=false;world.entityIndex.erase(world.entities[i]->id);}
        world.entities.resize(size);world.transformCache.reset();world.assembling=assembling;world.syncTransforms();throw;
    }
    return result;
}
void bindPrefabs(py::module_& module){
    module.def("load_prefab",[](const std::string& file){if(!active)throw std::runtime_error("Runtime inactive");return pythonValue(prefabDocument(active->config,file));});
    module.def("instantiate_prefab",[](const std::string& file,const std::string& prefix,py::dict overrides,py::object parent,std::array<float,3> position){
        if(!active || active->tearingDown)throw std::runtime_error("Cannot instantiate prefab in this runtime state");
        std::string id;
        if(!parent.is_none()){
            if(py::isinstance<py::str>(parent))id=parent.cast<std::string>();
            else {auto& entity=parent.cast<Entity&>();if(active->world.find(entity.id).get()!=&entity)throw std::runtime_error("Prefab parent is not in current scene");id=entity.id;}
        }
        auto result=instantiatePrefab(active->world,file,prefix,fromPython(overrides),id,{position[0],position[1],position[2]});
        return result;
    },py::arg("file"),py::arg("prefix")="",py::arg("overrides")=py::dict(),py::arg("parent")=py::none(),py::arg("position")=std::array<float,3>{0,0,0});
}
}
