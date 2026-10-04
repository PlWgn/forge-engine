#include <forge/engine.hpp>
#include <forge/geometry.hpp>
#include <pybind11/stl.h>
namespace forge {
namespace {
std::string key(std::string name){if(name.rfind("@mesh:",0)==0)name=name.substr(6);if(name.empty() || name.size()>128 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-. ")!=std::string::npos)throw std::runtime_error("Mesh name must contain 1..128 ASCII letters/digits, spaces, -_.");return "@mesh:"+name;}
void vector(const Json& j,size_t n,const std::string& field){if(!j.is_array() || j.size()!=n)throw std::runtime_error("mesh."+field+" has invalid vector size");for(auto& v:j)finiteNumber(v,"mesh."+field);}
Runtime& rt(){if(!active)throw std::runtime_error("Geometry runtime is not active");return *active;}
py::object python(const Json& j){return pythonValue(j);}
}
bool proceduralName(const std::string& name){return name.rfind("@mesh:",0)==0;}
Geometry& geometry(World& world){if(!world.geometry){world.geometry=std::make_shared<Geometry>();if(world.config)world.geometry->limit=world.config->data.value("geometry_budget_bytes",size_t(64*1024*1024));}return *world.geometry;}
std::string Geometry::set(const std::string& name,const Json& data){
    auto id=key(name);if(!data.is_object() || !data.contains("positions"))throw std::runtime_error("Mesh needs positions");auto& positions=data["positions"];
    if(!positions.is_array() || positions.empty() || positions.size()>2000000)throw std::runtime_error("Mesh needs 1..2M positions");
    auto count=positions.size();for(auto& p:positions)vector(p,3,"positions");
    for(auto field:{"normals","uvs","colors"})if(data.contains(field)){
        auto& values=data[field];if(!values.is_array() || values.size()!=count)throw std::runtime_error(std::string("mesh.")+field+" count must match positions");
        for(auto& v:values){vector(v,std::string(field)=="uvs"?2:std::string(field)=="colors"?4:3,field);if(std::string(field)=="colors")for(auto& c:v)if(c<0 || c>1)throw std::runtime_error("Mesh colors must be 0..1");}
    }
    std::vector<size_t> indices;
    if(data.contains("indices")){auto& list=data["indices"];if(!list.is_array() || list.empty() || list.size()%3 || list.size()>2000000)throw std::runtime_error("Mesh indices need 3..2M triangle indices");for(auto& i:list){if(!i.is_number_integer() || i.get<double>()<0 || i.get<double>()>=count)throw std::runtime_error("Mesh index out of range");indices.push_back(i.get<size_t>());}}
    else {if(count%3)throw std::runtime_error("Mesh positions without indices must form triangles");for(size_t i=0;i<count;++i)indices.push_back(i);}
    auto found=entries.find(id);size_t previous=found==entries.end()?0:found->second.model->memoryBytes;size_t needed=indices.size()*sizeof(ModelVertex);
    if(needed>limit || bytes-previous>limit-needed)throw std::runtime_error("Geometry CPU budget exceeded");
    auto model=std::make_shared<Model>();model->nodes.push_back({"mesh",-1,glm::mat4(1)});Model::Part part;
    for(size_t index:indices){ModelVertex v;auto& p=positions[index];v.p={p[0],p[1],p[2]};if(data.contains("normals")){auto n=data["normals"][index];v.n={n[0],n[1],n[2]};}if(data.contains("uvs")){auto t=data["uvs"][index];v.uv={t[0],t[1]};}if(data.contains("colors")){auto c=data["colors"][index];v.color={c[0],c[1],c[2],c[3]};}part.vertices.push_back(v);}
    if(!data.contains("normals"))for(size_t i=0;i<part.vertices.size();i+=3){auto n=glm::cross(glm::dvec3(part.vertices[i+1].p)-glm::dvec3(part.vertices[i].p),glm::dvec3(part.vertices[i+2].p)-glm::dvec3(part.vertices[i].p));auto length=glm::length(n);glm::vec3 normal=length>1e-12?glm::vec3(n/length):glm::vec3(0,1,0);for(int k=0;k<3;++k)part.vertices[i+k].n=normal;}
    model->memoryBytes=part.vertices.size()*sizeof(ModelVertex);model->parts.push_back(std::move(part));
    if(model->memoryBytes>limit || bytes-previous>limit-model->memoryBytes)throw std::runtime_error("Geometry CPU budget exceeded");
    bytes=bytes-previous+model->memoryBytes;entries[id]={std::move(model),++version};return id;
}
void Geometry::remove(const std::string& name){auto found=entries.find(key(name));if(found==entries.end())return;bytes-=found->second.model->memoryBytes;entries.erase(found);++version;}
Json Geometry::info()const{return {{"meshes",entries.size()},{"resident_bytes",bytes},{"budget_bytes",limit},{"revision",version}};}
void bindGeometry(py::module_& m){
    m.def("set_mesh",[](const std::string& name,py::dict data){if(rt().tearingDown)throw std::runtime_error("Cannot create geometry during teardown");return geometry(rt().world).set(name,fromPython(data));});
    m.def("remove_mesh",[](const std::string& name){auto id=key(name);for(auto& e:rt().world.entities)if(e->alive && e->model==id)throw std::runtime_error("Mesh is still used by entity: "+e->id);geometry(rt().world).remove(name);});
    m.def("mesh_info",[](const std::string& name){auto& store=geometry(rt().world);auto found=store.entries.find(key(name));if(found==store.entries.end())throw std::runtime_error("Unknown procedural mesh: "+name);auto info=found->second.model->info();info["revision"]=found->second.revision;info["bytes"]=found->second.model->memoryBytes;return python(info);});
    m.def("set_geometry_budget",[](size_t bytes){auto& store=geometry(rt().world);if(bytes<1 || bytes>1024ull*1024*1024 || bytes<store.bytes)throw std::runtime_error("Geometry budget must be 1..1GiB and cover resident meshes");store.limit=bytes;});
    m.def("geometry_stats",[](){return python(geometry(rt().world).info());});
}
} // namespace forge
