#pragma once
#include <forge/types.hpp>
#include <forge/gl.hpp>
#include <array>
namespace forge {
struct World;struct Runtime;
struct Editor {
    bool preview=false;
    std::string selected,status;
    std::vector<Json> undo,redo;
    char sceneFile[512]="editor-scene.json";
    void draw(World &,Runtime &,const std::array<bool,GLFW_KEY_LAST+1> &,const std::array<bool,8> &,glm::vec2,const Json &);
};
}
