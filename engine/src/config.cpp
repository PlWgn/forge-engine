#include <forge/engine.hpp>
#include <cmath>
#include <limits>
namespace forge {
float finiteNumber(const Json& value,const std::string& field) {
    if(!value.is_number())throw std::runtime_error(field+" must be a number");
    return checkedFloat(value.get<double>(),field);
}
Json readJson(const fs::path& file) {
    std::ifstream stream(file); if(!stream) throw std::runtime_error("File not found: " + file.u8string());
    try { return Json::parse(stream); } catch(const Json::exception& e) { throw std::runtime_error(file.u8string() + ": " + e.what()); }
}
Json fromPython(py::handle value) { return Json::parse(py::module_::import("json").attr("dumps")(value).cast<std::string>()); }
static bool inside(const fs::path& root, const fs::path& path) {
    auto rel = path.lexically_relative(root); return !rel.empty() && *rel.begin() != "..";
}
fs::path Config::resolve(const std::string& path) const {
    fs::path p = fs::u8path(path);
    if(p.is_absolute()) throw std::runtime_error("Project paths must be relative: " + path);
    auto resolved = fs::weakly_canonical(root / p);
    if(!inside(root, resolved) && resolved != root) throw std::runtime_error("Path escapes project root: " + path);
    return resolved;
}
fs::path Config::asset(const std::string& group, const std::string& name) const {
    if(name.empty()) return {};
    auto found = paths.find(group); if(found == paths.end()) throw std::runtime_error("Unknown path group: " + group);
    auto result = fs::weakly_canonical(found->second / fs::u8path(name));
    if(!inside(root,result)) throw std::runtime_error("Asset escapes project root: " + name);
    return result;
}
Config Config::load(const fs::path& filename) {
    Config c; c.file = fs::absolute(filename); c.root = fs::weakly_canonical(c.file.parent_path()); c.data = readJson(c.file);
    if(!c.data.is_object()) throw std::runtime_error("Settings must be a JSON object");
    if(c.data.value("schema_version", 0) != 1) throw std::runtime_error("Unsupported schema_version (expected 1)");
    if(!c.data.contains("project") || !c.data["project"].is_object() || c.data["project"].value("name", "").empty()) throw std::runtime_error("project.name is required");
    if(!c.data.contains("paths") || !c.data["paths"].is_object()) throw std::runtime_error("paths is required");
    for(auto it=c.data["paths"].begin();it!=c.data["paths"].end();++it) c.paths[it.key()] = c.resolve(it.value().get<std::string>());
    for(auto group : {"graphics","modules","scenes","scripts","textures","materials","models","objects","audio"})
        if(!c.paths.count(group)) throw std::runtime_error(std::string("Missing paths.") + group);
    c.entry(); return c;
}
std::string Config::entry() const {
    auto e = data.value("entry_scene", ""); if(e.empty()) throw std::runtime_error("entry_scene is required"); return e;
}
static void requireFile(const fs::path& p) { if(!fs::is_regular_file(p)) throw std::runtime_error("Missing file: " + p.u8string()); }
Json validateEntity(const Config& c, Json j) {
    if(!j.is_object()) throw std::runtime_error("Entity must be an object");
    if(j.contains("prefab")) { auto base = readJson(c.asset("objects",j["prefab"])); j.erase("prefab"); base.merge_patch(j); j=base; }
    if(!j.is_object())throw std::runtime_error("Prefab must resolve to an entity object");
    for(auto field:{"id","name","kind","model","texture","material","text","text_key"})
        if(j.contains(field) && !j[field].is_string())throw std::runtime_error(std::string(field)+" must be a string");
    for(auto field:{"dynamic","trigger","visible","screen"})
        if(j.contains(field) && !j[field].is_boolean())throw std::runtime_error(std::string(field)+" must be a boolean");
    for(auto field : {"position","rotation","scale","velocity","collider"}) if(j.contains(field)) {
        if(!j[field].is_array() || j[field].size()!=3) throw std::runtime_error(std::string(field)+" must contain 3 numbers");
        for(auto& v:j[field])finiteNumber(v,field);
    }
    if(j.contains("text_params") && !j["text_params"].is_object())throw std::runtime_error("text_params must be an object");
    for(auto field:{"color","clip"})if(j.contains(field) && !(std::string(field)=="clip" && j[field].is_null())) {
        if(!j[field].is_array() || j[field].size()!=4)throw std::runtime_error(std::string(field)+" must contain 4 numbers");
        for(auto& v:j[field])finiteNumber(v,field);
    }
    checkedMass(finiteNumber(j.value("mass",Json(1)),"mass"));
    auto size=finiteNumber(j.value("font_size",Json(24)),"font_size");if(size<=0)throw std::runtime_error("font_size must be positive");
    auto kind=j.value("kind", "sprite"); if(kind!="sprite" && kind!="cube" && kind!="mesh" && kind!="text" && kind!="empty") throw std::runtime_error("Unknown entity kind: " + kind);
    for(auto group : {"texture","model","material"}) if(j.contains(group) && !j[group].get<std::string>().empty()) {
        std::string folder = std::string(group)=="texture"?"textures":std::string(group)=="model"?"models":"materials";
        requireFile(c.asset(folder,j[group]));
    }
    if(kind=="mesh" && j.value("model", "").empty()) throw std::runtime_error("mesh needs model");
    if(j.contains("scripts")) { if(!j["scripts"].is_array()) throw std::runtime_error("scripts must be an array"); for(auto& s:j["scripts"]) requireFile(c.asset("scripts",s.is_string()?s.get<std::string>():s.at("file").get<std::string>())); }
    return j;
}
void Config::validate() const {
    for(auto& [key,p]:paths) if(!fs::is_directory(p)) throw std::runtime_error("Missing directory paths."+key+": "+p.u8string());
    requireFile(asset("scenes", entry()));
    auto window = data.value("window", Json::object());
    if(window.value("width",1280)<1 || window.value("height",720)<1) throw std::runtime_error("Window dimensions must be positive");
    for(auto& s:data.value("startup_scripts",Json::array())) requireFile(asset("scripts",s.get<std::string>()));
    for(auto& p:data.value("python_paths",Json::array())) if(!fs::is_directory(resolve(p.get<std::string>()))) throw std::runtime_error("Missing python_paths directory");
    auto graphics = data.value("renderer",Json::object());
    for(auto field:{"vertex_shader","fragment_shader","font"}) if(graphics.contains(field)) requireFile(asset("graphics",graphics[field]));
    if(data["project"].contains("icon")) requireFile(resolve(data["project"]["icon"]));
    // Validate every reusable object and every declarative scene, not only the first one.
    for(auto& item:fs::recursive_directory_iterator(paths.at("objects"))) if(item.path().extension()==".json") validateEntity(*this,readJson(item.path()));
    for(auto& item:fs::recursive_directory_iterator(paths.at("materials"))) if(item.path().extension()==".json") {
        auto m=readJson(item.path()); if(m.contains("texture")) requireFile(asset("textures",m["texture"]));
        if(m.contains("color")) {
            if(!m["color"].is_array() || m["color"].size()!=4)throw std::runtime_error(item.path().u8string()+": color must have 4 components");
            for(auto& v:m["color"])finiteNumber(v,item.path().u8string()+": color");
        }
    }
    for(auto& item:fs::recursive_directory_iterator(paths.at("scenes"))) if(item.path().extension()==".json") {
        auto scene=readJson(item.path()); auto mode=scene.value("mode","2d"); if(mode!="2d" && mode!="3d") throw std::runtime_error("Scene mode must be 2d or 3d");
        if(scene.contains("script")) requireFile(asset("scenes",scene["script"]));
        if(!scene.value("entities",Json::array()).is_array()) throw std::runtime_error("Scene.entities must be array");
        std::set<std::string> ids;
        for(auto& e:scene.value("entities",Json::array())) { validateEntity(*this,e); auto id=e.value("id",""); if(!id.empty() && !ids.insert(id).second) throw std::runtime_error("Duplicate entity id: "+id); }
    }
    validateMedia(*this);
    Localization validation;validation.load(*this);
}
}
