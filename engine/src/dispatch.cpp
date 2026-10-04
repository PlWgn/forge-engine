// Callback/contact dispatch extracted from runtime.cpp; licensed core origin.
#include <forge/engine.hpp>
#include <algorithm>
#include <limits>
#include <unordered_map>
namespace forge {
unsigned Runtime::onFrame(py::object callback,bool persistent){
    if(!PyCallable_Check(callback.ptr()))throw std::runtime_error("Frame listener must be callable");
    if(listenerIndex==std::numeric_limits<unsigned>::max())throw std::runtime_error("Frame listener IDs exhausted");
    unsigned id=++listenerIndex;
    listeners.push_back({id,std::move(callback),persistent});listenerIds.insert(id);
    return id;
}
void Runtime::removeListener(unsigned id){
    listenerIds.erase(id);
    // Defer vector compaction while a frame snapshot is executing.
    if(!dispatchingFrame)listeners.erase(std::remove_if(listeners.begin(),listeners.end(),[&](auto& entry){return entry.id==id;}),listeners.end());
}
void Runtime::dispatchFrame(){
    listenerIds.clear();
    for(auto& entry:listeners)listenerIds.insert(entry.id);
    auto snapshot=listeners;
    dispatchingFrame=true;
    size_t checks=0,calls=0;
    auto finish=[&]{
        dispatchingFrame=false;
        listeners.erase(std::remove_if(listeners.begin(),listeners.end(),[&](auto& entry){return !listenerIds.count(entry.id);}),listeners.end());
        profile["listener_checks"]=checks;profile["listener_calls"]=calls;
    };
    try{
        for(auto& entry:snapshot){
            ++checks;
            if(listenerIds.count(entry.id)){++calls;entry.callback(dt);}
        }
    }catch(...){finish();throw;}
    finish();
}
void Runtime::dispatchContacts(const std::vector<Script>& snapshot,const std::set<std::pair<std::string,std::string>>& previous){
    struct Changes {std::vector<std::string> enter,exit;};
    std::unordered_map<std::string,Changes> changes;
    for(auto& pair:world.contacts)if(!previous.count(pair)){
        changes[pair.first].enter.push_back(pair.second);changes[pair.second].enter.push_back(pair.first);
    }
    for(auto& pair:previous)if(!world.contacts.count(pair)){
        changes[pair.first].exit.push_back(pair.second);changes[pair.second].exit.push_back(pair.first);
    }
    const auto scanned=world.contacts.size()+previous.size();
    size_t callbacks=0;
    // Preserve script order and each entity's enter-before-exit order.
    for(auto& script:snapshot){
        if(!script.entity || !script.entity->alive)continue;
        auto found=changes.find(script.entity->id);
        if(found==changes.end())continue;
        for(auto kind:{std::pair<const char*,const std::vector<std::string>*>{"on_collision",&found->second.enter},
                       {"on_collision_exit",&found->second.exit}}){
            if(kind.second->empty() || !py::hasattr(script.instance,kind.first))continue;
            auto callback=script.instance.attr(kind.first);
            for(auto& other:*kind.second){
                if(!script.entity->alive)break;
                ++callbacks;callback(world.find(other));
            }
        }
    }
    profile["contact_pairs_scanned"]=scanned;
    profile["contact_callbacks"]=callbacks;
}
}
