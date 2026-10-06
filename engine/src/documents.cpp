#include <forge/documents.hpp>
#include <forge/scene.hpp>
#include <forge/material.hpp>
#include <forge/prefab.hpp>
#include <forge/logger.hpp>
#include <forge/graphics_device.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace forge {
DocumentConflict::DocumentConflict(Json value)
    : std::runtime_error("Document changed in both editors: " + value.dump()), paths(std::move(value)) {}
namespace {
using Value = std::optional<Json>;
std::string escape(const std::string &s) {
    std::string result;
    for (char c : s) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1,c);
    return result;
}
bool keyed(const Json &array) {
    if (!array.is_array()) return false;
    std::set<std::string> ids;
    for (const auto &item : array)
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
            item["id"].get<std::string>().empty() || !ids.insert(item["id"]).second) return false;
    return true;
}
Value field(const Value &object, const std::string &key) {
    return object && object->contains(key) ? Value(object->at(key)) : std::nullopt;
}
Value merge(const Value &base, const Value &local, const Value &disk,
            const std::string &path, Json &conflicts) {
    if (local == base) return disk;
    if (disk == base || disk == local) return local;
    if (base && local && disk && base->is_object() && local->is_object() && disk->is_object()) {
        Json result = Json::object(); std::set<std::string> keys;
        for (const auto *v : {&base,&local,&disk})
            for (auto it = (*v)->begin(); it != (*v)->end(); ++it) keys.insert(it.key());
        for (const auto &key : keys) {
            auto value = merge(field(base,key),field(local,key),field(disk,key),path+"/"+escape(key),conflicts);
            if (value) result[key] = *value;
        }
        return result;
    }
    // Entity/emitter records merge by stable IDs; vector/keyframe arrays stay atomic.
    if (base && local && disk && (path == "/entities" || path == "/emitters") &&
        keyed(*base) && keyed(*local) && keyed(*disk)) {
        auto index = [](const Json &items) {
            Json result = Json::object(); for (const auto &item : items) result[item["id"].get<std::string>()] = item; return result;
        };
        Value b=index(*base), l=index(*local), d=index(*disk);
        auto records=merge(b,l,d,path,conflicts).value();
        auto order=[](const Json &items,const Json &common) {
            Json ids=Json::array();for(const auto &item:items) if(common.contains(item["id"].get<std::string>())) ids.push_back(item["id"]);return ids;
        };
        // Detect incompatible reorderings of surviving base records.
        Json common=Json::object();for(const auto &item:*base) if(records.contains(item["id"].get<std::string>())) common[item["id"].get<std::string>()]=true;
        auto bo=order(*base,common),lo=order(*local,common),di=order(*disk,common);
        if(lo!=bo && di!=bo && lo!=di) conflicts.push_back(path+"/@order");
        const Json &primary=lo!=bo?*local:*disk;
        std::vector<std::string> ids;
        std::set<std::string> added;
        for(const auto &item:primary) {
            auto id=item["id"].get<std::string>();
            if(records.contains(id) && added.insert(id).second)ids.push_back(id);
        }
        // Preserve insertion anchors from the other writer, not just its records.
        for(const auto *items:{&*local,&*disk}) {
            std::vector<std::string> order;
            for(const auto &item:*items) {
                auto id=item["id"].get<std::string>();
                if(records.contains(id))order.push_back(id);
            }
            for(size_t i=0;i<order.size();++i) {
                if(!added.insert(order[i]).second)continue;
                size_t next=ids.size();std::optional<size_t> previous;
                for(size_t j=i+1;j<order.size();++j) {
                    auto anchor=std::find(ids.begin(),ids.end(),order[j]);
                    if(anchor!=ids.end()){next=size_t(anchor-ids.begin());break;}
                }
                for(size_t j=i;j>0;--j) {
                    auto anchor=std::find(ids.begin(),ids.end(),order[j-1]);
                    if(anchor!=ids.end()){previous=size_t(anchor-ids.begin());break;}
                }
                if(previous && *previous>=next)conflicts.push_back(path+"/@order");
                ids.insert(ids.begin()+next,order[i]);
            }
        }
        Json result=Json::array();
        for(const auto &id:ids)result.push_back(records[id]);
        return result;
    }
    conflicts.push_back(path.empty()?"/":path);
    return local;
}
std::string revision(const Json &value) {
    // Opaque content revision, not a cryptographic integrity/security checksum.
    uint64_t hash=14695981039346656037ull;
    for(unsigned char c:value.dump()){hash^=c;hash*=1099511628211ull;}
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
struct Lock {
    fs::path path;
    explicit Lock(const fs::path &file):path(file.parent_path()/fs::u8path("."+file.filename().u8string()+".forge-lock")) {
        std::error_code error;
        if(!fs::create_directory(path,error))throw std::runtime_error("Document is locked (remove stale lock only after stopping its writer): "+path.u8string());
    }
    ~Lock(){std::error_code error;fs::remove(path,error);}
};
void validateDocument(const Config &config,const std::string &group,const Json &data) {
    if(!data.is_object())throw std::runtime_error("Project document must be a JSON object");
    if(group=="scenes") {
        World world;world.config=const_cast<Config*>(&config);world.loadDocument(data);
        if(data.contains("script") && !fs::is_regular_file(config.asset("scenes",data["script"])))throw std::runtime_error("Missing scene script");
    } else if(group=="materials")validateMaterial(config,data);
    else if(group=="objects" && !data.contains("entities") && !data.contains("extends"))validateEntity(config,data);
}
}
Json mergeDocuments(const Json &base,const Json &local,const Json &disk) {
    Json conflicts=Json::array();auto result=merge(base,local,disk,"",conflicts);
    if(!conflicts.empty())throw DocumentConflict(std::move(conflicts));return result.value();
}
Json documentSnapshot(const fs::path &path) {
    if(!fs::exists(path))return {{"exists",false},{"revision",nullptr},{"data",nullptr}};
    auto data=readJson(path);return {{"exists",true},{"revision",revision(data)},{"data",std::move(data)}};
}
Json commitDocument(const fs::path &path,const Json &base,const Json &local,
                    const std::function<void(const Json &)> &validate) {
    fs::create_directories(path.parent_path());Lock lock(path);
    auto disk=documentSnapshot(path);Json next;
    if(base.is_null()) {
        if(disk["exists"].get<bool>())throw DocumentConflict(Json::array({"/"}));next=local;
    } else {
        if(!disk["exists"].get<bool>())throw DocumentConflict(Json::array({"/"}));
        next=mergeDocuments(base,local,disk["data"]);
    }
    if(validate)validate(next);
    if(disk["exists"].get<bool>() && next==disk["data"])return disk; // Do not reformat an unchanged file.
    auto temp=path.parent_path()/fs::u8path("."+path.filename().u8string()+".forge-tmp");
    if(fs::exists(temp) || fs::is_symlink(fs::symlink_status(temp)))throw std::runtime_error("Temporary document path already exists: "+temp.u8string());
    try {
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot write document: "+temp.u8string());out<<next.dump(2)<<'\n';out.flush();if(!out)throw std::runtime_error("Document write failed");}
        // Optimistic check also catches manual writers that do not use our lock.
        if(documentSnapshot(path)!=disk)throw DocumentConflict(Json::array({"/"}));
#ifdef _WIN32
        if(!MoveFileExW(temp.wstring().c_str(),path.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace document");
#else
        fs::rename(temp,path);
#endif
    }catch(...){std::error_code error;fs::remove(temp,error);throw;}
    return {{"exists",true},{"revision",revision(next)},{"data",std::move(next)}};
}
Json projectRequest(const fs::path &settings,const Json &request) {
    if(!request.is_object() || (request.contains("api_version") && (!request["api_version"].is_number_integer() || request["api_version"]!=1)))throw std::runtime_error("Expected project API version 1 request object");
    auto op=request.at("op").get<std::string>();
    if(op=="capabilities")return {{"api_version",1},{"engine_version",FORGE_VERSION},{"base_shell",bool(FORGE_WITH_EDITOR)},
        {"graphics_backends",GraphicsDevice::backends()},
        {"operations",{"capabilities","inspect","list","read","commit","patch","apply_patch","merge","check"}}};
    if(op=="apply_patch")return request.at("data").patch(request.at("patch"));
    if(op=="merge")return mergeDocuments(request.at("base"),request.at("local"),request.at("disk"));
    auto config=Config::load(settings);
    if(op=="inspect")return {{"schema_version",1},{"config",config.data},{"settings",config.file.lexically_relative(config.root).generic_u8string()}};
    auto group=request.value("group",std::string());
    if(op=="list") {
        Json result=Json::array();
        const auto folder=group.empty()?config.root:config.paths.at(group);
        if(!fs::is_directory(folder))return result;
        for(const auto &entry:fs::recursive_directory_iterator(folder))if(entry.is_regular_file()) {
            auto path=fs::weakly_canonical(entry.path()); // Resolve symlinks through the same public boundary.
            config.resolve(path.lexically_relative(config.root).generic_u8string());
            result.push_back(entry.path().lexically_relative(folder).generic_u8string());
        }
        std::sort(result.begin(),result.end());return result;
    }
    auto name=request.at("file").get<std::string>();
    auto path=group.empty()?config.resolve(name):config.asset(group,name);
    if(path.extension()!=".json")throw std::runtime_error("Document operations require a .json file; Python source stays a manually editable file");
    if(op=="read")return documentSnapshot(path);
    std::string role;
    size_t specificity=0;
    for(const auto &[key,folder]:config.paths) {
        auto relative=path.lexically_relative(folder);
        if(!relative.empty() && *relative.begin()!=".." && folder.generic_u8string().size()>=specificity) {
            role=key;specificity=folder.generic_u8string().size();
        }
    }
    auto validate=[&](const Json &data){
        if(path==fs::weakly_canonical(config.file)) {
            auto candidate=config;candidate.data=data;
            if(!data.is_object() || data.value("schema_version",0)!=1 || !data.contains("project") || !data["project"].is_object() || data["project"].value("name","").empty() || !data.contains("paths") || !data["paths"].is_object() || data.value("entry_scene","").empty())throw std::runtime_error("Invalid project configuration");
            candidate.paths.clear();for(auto it=data["paths"].begin();it!=data["paths"].end();++it)candidate.paths[it.key()]=candidate.resolve(it.value().get<std::string>());
            candidate.validate(false);
        }else if(role=="objects") {
            auto name=path.lexically_relative(config.paths.at("objects")).generic_u8string();
            prefabCandidate(config,name,&data);
        }else validateDocument(config,role,data);
    };
    if(op=="check"){validate(request.at("data"));return {{"valid",true}};}
    if(op=="commit" || op=="patch") {
        auto base=request.at("base"),local=request.at("data");
        if(op=="patch")local=base.patch(local);
        return commitDocument(path,base,local,validate);
    }
    throw std::runtime_error("Unknown project operation: "+op);
}
Json projectResponse(const fs::path &settings,const Json &request) {
    Json response={{"api_version",1},{"id",request.is_object()?request.value("id",Json()):Json()}};
    try{response["result"]=projectRequest(settings,request);response["ok"]=true;}
    catch(const DocumentConflict &e){response["ok"]=false;response["error"]={{"code","conflict"},{"message",e.what()},{"paths",e.paths}};}
    catch(const std::exception &e){response["ok"]=false;response["error"]={{"code","invalid_request"},{"message",e.what()}};}
    return response;
}
int projectProtocol(const fs::path &settings,const fs::path &requestFile,bool serve) {
    auto respond=[&](const std::string &line) {
        Json response={{"api_version",1}};
        try {
            if(line.size()>16*1024*1024)throw std::runtime_error("Request exceeds 16 MiB");
            response=projectResponse(settings,Json::parse(line));
        }catch(const DocumentConflict &e){response["ok"]=false;response["error"]={{"code","conflict"},{"message",e.what()},{"paths",e.paths}};}
        catch(const std::exception &e){response["ok"]=false;response["error"]={{"code","invalid_request"},{"message",e.what()}};}
        if(!response["ok"].get<bool>())logger.error(response["error"]["message"].get<std::string>());
        std::cout<<response.dump()<<std::endl;return response["ok"].get<bool>();
    };
    if(!requestFile.empty()) {
        if(serve)throw std::runtime_error("--request and --serve cannot be combined");
        std::ifstream input(requestFile,std::ios::binary);if(!input)throw std::runtime_error("Cannot read request");
        std::string line{std::istreambuf_iterator<char>(input),{}};return respond(line)?0:1;
    }
    if(serve){std::string line;while(std::getline(std::cin,line))respond(line);return 0;}
    std::string line{std::istreambuf_iterator<char>(std::cin),{}};return respond(line)?0:1;
}
}
