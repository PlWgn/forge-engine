#include <forge/editor_session.hpp>
#include <forge/engine.hpp>
#include <forge/documents.hpp>
#include <optional>
namespace forge {
namespace {
using Value=std::optional<Json>;
Value member(const Value &v,const std::string &key){return v && v->is_object() && v->contains(key)?Value(v->at(key)):std::nullopt;}
// Project only authored deltas onto the original file, retaining absent defaults,
// prefab references, and properties not understood by the runtime.
Value project(const Value &base,const Value &local,const Value &source,const std::string &path) {
    if(base==local)return source;
    if(!local)return std::nullopt;
    if(base && base->is_object() && local->is_object()) {
        Json result=source && source->is_object()?*source:Json::object();std::set<std::string> keys;
        for(auto it=base->begin();it!=base->end();++it)keys.insert(it.key());
        for(auto it=local->begin();it!=local->end();++it)keys.insert(it.key());
        for(const auto &key:keys) {
            auto value=project(member(base,key),member(local,key),member(source,key),path+"/"+key);
            if(value)result[key]=*value;else result.erase(key);
        }
        return result;
    }
    if(path=="/entities" && base && base->is_array() && local->is_array() && source && source->is_array()) {
        Json originals=Json::object(),baselines=Json::object();
        for(size_t i=0;i<base->size();++i)baselines[base->at(i).at("id").get<std::string>()]=base->at(i);
        for(size_t i=0;i<source->size();++i) {
            const auto &item=source->at(i);
            auto id=item.value("id",i<base->size()?base->at(i).at("id").get<std::string>():std::string());
            if(!id.empty())originals[id]=item;
        }
        Json result=Json::array();
        for(const auto &item:*local) {
            auto id=item.at("id").get<std::string>();
            auto value=project(member(baselines,id),item,member(originals,id),path+"/"+id);
            if(value)result.push_back(*value);
        }
        return result;
    }
    return local;
}
}
void EditorSession::opened(const fs::path &path,const World &world) {
    file=path;diskBase=path.extension()==".json"?world.scene:Json();runtimeBase=world.serialize();
    undo.clear();redo.clear();
}
void EditorSession::record(const Json &before,const Json &after) {
    if(before==after)return;undo.push_back(before);if(undo.size()>100)undo.erase(undo.begin());redo.clear();
}
void EditorSession::apply(Runtime &runtime,const Json &data) {
    if(!runtime.editing || !runtime.gamePaused || runtime.initializing || runtime.reloading || runtime.tearingDown)
        throw std::runtime_error("Editor mutations require a paused, initialized edit session");
    runtime.loadScene(runtime.currentScene,nullptr,&data);runtime.gamePaused=true;
}
bool EditorSession::step(Runtime &runtime,bool forward) {
    auto &from=forward?redo:undo;auto &to=forward?undo:redo;
    if(from.empty())return false;
    auto before=runtime.world.serialize(),candidate=from.back();
    apply(runtime,candidate);from.pop_back();to.push_back(std::move(before));return true;
}
Json EditorSession::save(Runtime &runtime,const std::string &name) {
    if(runtime.reloading)throw std::runtime_error("Document authoring is unavailable during hot reload; save after commit");
    auto path=runtime.config.asset("scenes",name);
    if(path.extension()!=".json")throw std::runtime_error("Editor scenes must be JSON");
    auto snapshot=runtime.world.serialize();
    Json local=snapshot;
    bool same=!file.empty() && fs::weakly_canonical(path)==fs::weakly_canonical(file) && !diskBase.is_null();
    if(same)local=project(runtimeBase,snapshot,diskBase,"").value();
    auto result=commitDocument(path,same?diskBase:Json(),local,[&](const Json &data){World candidate;candidate.config=&runtime.config;candidate.geometry=runtime.world.geometry;candidate.loadDocument(data);});
    if(same || (runtime.editing && !runtime.initializing)) {
        file=path;diskBase=result["data"];runtimeBase=std::move(snapshot);
        if(runtime.editing && !runtime.initializing)runtime.currentScene=name;
    }
    return result;
}
Json EditorSession::command(Runtime &runtime,const Json &request) {
    if(!runtime.editing)throw std::runtime_error("Editor API requires edit mode (builtin shell is optional)");
    auto op=request.at("op").get<std::string>();
    if(op=="commands")return runtime.editorClient?fromPython(py::module_::import("builtins").attr("list")(runtime.editorClient.attr("commands"))):Json::array();
    if(op=="command") {
        if(!runtime.editorClient)throw std::runtime_error("Editor SDK is unavailable");
        auto name=request.at("name").get<std::string>();auto args=pythonValue(request.value("arguments",Json::object())).cast<py::dict>();
        return fromPython(runtime.editorClient.attr("command")(name,**args));
    }
    if(op=="snapshot")return {{"scene",runtime.world.serialize()},{"selected",selected},{"preview",!runtime.gamePaused},{"undo",undo.size()},{"redo",redo.size()}};
    if(op=="select") {auto id=request.value("id",std::string());if(!id.empty() && !runtime.world.find(id))throw std::runtime_error("Unknown selection");selected=id;return true;}
    if(op=="preview") {runtime.gamePaused=!request.at("enabled").get<bool>();runtime.world.contacts.clear();return true;}
    if(op=="save")return save(runtime,request.at("file").get<std::string>());
    if(op=="load") {
        auto name=request.at("file").get<std::string>();auto path=runtime.config.asset("scenes",name);
        if((path.extension()!=".json" && path.extension()!=".py") || !fs::is_regular_file(path))throw std::runtime_error("Editor load needs an existing JSON/Python scene");
        runtime.pendingScene=std::move(name);return true;
    }
    if(op=="undo" || op=="redo")return step(runtime,op=="redo");
    if(op=="apply" || op=="patch") {
        auto before=runtime.world.serialize();auto candidate=op=="patch"?before.patch(request.at("patch")):request.at("scene");
        apply(runtime,candidate);record(before,runtime.world.serialize());return true;
    }
    throw std::runtime_error("Unknown editor operation: "+op);
}
}
