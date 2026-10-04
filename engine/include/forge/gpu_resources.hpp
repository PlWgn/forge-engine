#pragma once
#include <forge/model.hpp>
namespace forge {
using Vertex = ModelVertex;
struct Mesh {unsigned vao=0,vbo=0;int count=0;};
Mesh upload(const std::vector<Vertex>&);
unsigned texture(const unsigned char*,int,int);
unsigned linkProgram(const std::string&,const std::string&,const std::string&);
void uniforms(unsigned,const Json&);
}
