#include <forge/engine.hpp>
#include <forge/physics.hpp>
#include <forge/particles.hpp>
#include <forge/material.hpp>
#include <forge/geometry.hpp>
#include <forge/prefab.hpp>
#include <forge/animation.hpp>
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
    j=entityPrefab(c,std::move(j));
    if(!j.is_object())throw std::runtime_error("Prefab must resolve to an entity object");
    for(auto field:{"id","name","kind","model","texture","material","text","text_key","parent"})
        if(j.contains(field) && !j[field].is_string())throw std::runtime_error(std::string(field)+" must be a string");
    for(auto field:{"dynamic","trigger","visible","screen"})
        if(j.contains(field) && !j[field].is_boolean())throw std::runtime_error(std::string(field)+" must be a boolean");
    for(auto field : {"position","rotation","scale","velocity","collider","angular_velocity"}) if(j.contains(field)) {
        if(!j[field].is_array() || j[field].size()!=3) throw std::runtime_error(std::string(field)+" must contain 3 numbers");
        for(auto& v:j[field])finiteNumber(v,field);
    }
    if(j.contains("text_params") && !j["text_params"].is_object())throw std::runtime_error("text_params must be an object");
    for(auto field:{"color","clip","uv"})if(j.contains(field) && !(std::string(field)=="clip" && j[field].is_null())) {
        if(!j[field].is_array() || j[field].size()!=4)throw std::runtime_error(std::string(field)+" must contain 4 numbers");
        for(auto& v:j[field])finiteNumber(v,field);
    }
    checkedMass(finiteNumber(j.value("mass",Json(1)),"mass"));
    if(j.contains("material_properties"))validateMaterial(c,j["material_properties"]);
    validateRigidBody(j.value("rigid_body",Json::object()));
    if(j.contains("layer") && (!j["layer"].is_number_integer() || j["layer"].get<double>()<0 || j["layer"].get<double>()>4294967295.0))throw std::runtime_error("layer must be an unsigned 32-bit integer");
    for(auto field:{"animator","morph_weights"})if(j.contains(field) && !j[field].is_object())throw std::runtime_error(std::string(field)+" must be an object");
    for(auto field:{"casts_shadow","animation_loop"})if(j.contains(field) && !j[field].is_boolean())throw std::runtime_error(std::string(field)+" must be boolean");
    if(j.contains("animation") && !j["animation"].is_string())throw std::runtime_error("animation must be string");finiteNumber(j.value("animation_speed",Json(1)),"animation_speed");
    validateRenderSettings(Json{{"uniforms",j.value("uniforms",Json::object())}});
    if(j.contains("uv")){auto uv=j["uv"];for(auto& v:uv)if(v<0 || v>1)throw std::runtime_error("uv components must be in 0..1");if(uv[0].get<double>()+uv[2].get<double>()>1.000001 || uv[1].get<double>()+uv[3].get<double>()>1.000001)throw std::runtime_error("uv region escapes texture");}
    auto size=finiteNumber(j.value("font_size",Json(24)),"font_size");if(size<=0)throw std::runtime_error("font_size must be positive");
    auto kind=j.value("kind", "sprite"); if(kind!="sprite" && kind!="cube" && kind!="mesh" && kind!="text" && kind!="empty") throw std::runtime_error("Unknown entity kind: " + kind);
    for(auto group : {"texture","model","material"}) if(j.contains(group) && !j[group].get<std::string>().empty()) {
        std::string folder = std::string(group)=="texture"?"textures":std::string(group)=="model"?"models":"materials";
        if(!(std::string(group)=="model" && proceduralName(j[group])) && (std::string(group)!="texture" || j[group].get<std::string>().rfind("@target:",0)!=0))requireFile(c.asset(folder,j[group]));
    }
    if(kind=="mesh" && j.value("model", "").empty()) throw std::runtime_error("mesh needs model");
    if(j.contains("scripts")) { if(!j["scripts"].is_array()) throw std::runtime_error("scripts must be an array"); for(auto& s:j["scripts"]) requireFile(c.asset("scripts",s.is_string()?s.get<std::string>():s.at("file").get<std::string>())); }
    return j;
}
void Config::validate(bool media) const {
    auto geometryBudget=data.value("geometry_budget_bytes",Json(64*1024*1024));if(!geometryBudget.is_number_integer() || geometryBudget<1 || geometryBudget>1024*1024*1024)throw std::runtime_error("geometry_budget_bytes must be 1..1GiB");
    validatePhysics(data.value("physics",Json::object()));
    validateRenderSettings(data.value("rendering",Json::object()));
    storagePath(*this,data.value("save_directory","saves"));
    for(auto& [key,p]:paths) if(!fs::is_directory(p)) throw std::runtime_error("Missing directory paths."+key+": "+p.u8string());
    requireFile(asset("scenes", entry()));
    auto window = data.value("window", Json::object());
    if(window.value("width",1280)<1 || window.value("height",720)<1) throw std::runtime_error("Window dimensions must be positive");
    for(auto& s:data.value("startup_scripts",Json::array())) requireFile(asset("scripts",s.get<std::string>()));
    for(auto& p:data.value("python_paths",Json::array())) if(!fs::is_directory(resolve(p.get<std::string>()))) throw std::runtime_error("Missing python_paths directory");
    auto development=data.value("development",Json::object());
    if(!development.is_object())throw std::runtime_error("development must be an object");
    auto watchInterval=finiteNumber(development.value("watch_interval",Json(.3)),"development.watch_interval");
    if(watchInterval<.05 || watchInterval>10)throw std::runtime_error("development.watch_interval must be .05..10 seconds");
    auto graphics = data.value("renderer",Json::object());
    if(graphics.contains("texture_filter") && graphics["texture_filter"]!="nearest" && graphics["texture_filter"]!="linear")throw std::runtime_error("renderer.texture_filter must be nearest or linear");
    if(graphics.contains("particle_instancing") && !graphics["particle_instancing"].is_boolean())throw std::runtime_error("renderer.particle_instancing must be boolean");
    if(graphics.contains("particle_instance_shader"))requireFile(asset("graphics",graphics["particle_instance_shader"]));
    if(graphics.contains("mipmaps") && !graphics["mipmaps"].is_boolean())throw std::runtime_error("renderer.mipmaps must be boolean");
    for(auto field:{"vertex_shader","fragment_shader","font","particle_vertex_shader","particle_fragment_shader"}) if(graphics.contains(field)) requireFile(asset("graphics",graphics[field]));
    for(auto& font:graphics.value("fallback_fonts",Json::array()))requireFile(asset("graphics",font));
    if(graphics.contains("post_shader"))requireFile(asset("graphics",graphics["post_shader"]));
    if(data["project"].contains("icon")) requireFile(resolve(data["project"]["icon"]));
    // Validate every reusable object and every declarative scene, not only the first one.
    for(auto& item:fs::recursive_directory_iterator(paths.at("objects"))) if(item.path().extension()==".json"){auto object=readJson(item.path());if(object.contains("entities") || object.contains("extends"))prefabDocument(*this,item.path().lexically_relative(paths.at("objects")).generic_u8string());else validateEntity(*this,std::move(object));}
    for(auto& item:fs::recursive_directory_iterator(paths.at("materials"))) if(item.path().extension()==".json") {
        validateMaterial(*this,readJson(item.path()));
    }
    for(auto& item:fs::recursive_directory_iterator(paths.at("scenes"))) if(item.path().extension()==".json") {
        auto scene=readJson(item.path()); auto mode=scene.value("mode","2d"); if(mode!="2d" && mode!="3d") throw std::runtime_error("Scene mode must be 2d or 3d");
        validateRenderSettings(scene.value("rendering",Json::object()));if(scene.contains("physics_enabled") && !scene["physics_enabled"].is_boolean())throw std::runtime_error("physics_enabled must be boolean");
        if(scene.contains("script")) requireFile(asset("scenes",scene["script"]));
        if(!scene.value("entities",Json::array()).is_array()) throw std::runtime_error("Scene.entities must be array");
        std::set<std::string> ids;
        for(auto& e:scene.value("entities",Json::array())) { validateEntity(*this,e); auto id=e.value("id",""); if(!id.empty() && !ids.insert(id).second) throw std::runtime_error("Duplicate entity id: "+id); }
        World simulation;simulation.config=const_cast<Config*>(this);simulation.load(item.path().lexically_relative(paths.at("scenes")).generic_u8string());
    }
    if(media){validateMedia(*this);validateAnimationAssets(*this);}
    Localization validation;validation.load(*this);
}
}
