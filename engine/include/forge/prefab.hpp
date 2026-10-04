#pragma once
#include <forge/scene.hpp>
namespace pybind11 {class module_;}
namespace forge {
struct Config;
Json prefabDocument(const Config&,const std::string&);
Json entityPrefab(const Config&,Json);
std::map<std::string,std::shared_ptr<Entity>> instantiatePrefab(World&,const std::string&,const std::string&,const Json&,const std::string&,glm::vec3);
void bindPrefabs(pybind11::module_&);
}
