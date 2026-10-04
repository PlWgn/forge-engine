#pragma once
#include <forge/model.hpp>
#include <unordered_map>
namespace pybind11 {class module_;}
namespace forge {
struct World;
struct Geometry {
    struct Entry {std::shared_ptr<Model> model;unsigned long long revision;};
    std::unordered_map<std::string,Entry> entries;
    size_t bytes=0,limit=64*1024*1024;
    unsigned long long version=0;
    std::string set(const std::string &,const Json &);
    void remove(const std::string &);
    Json info() const;
};
bool proceduralName(const std::string &);
Geometry &geometry(World &);
void bindGeometry(pybind11::module_ &);
} // namespace forge
