#pragma once
#include <forge/types.hpp>
namespace forge {
struct World;struct Runtime;
struct EditorSession {
    fs::path file;
    Json diskBase, runtimeBase;
    std::vector<Json> undo, redo;
    std::string selected;
    void opened(const fs::path &,const World &);
    void record(const Json &before,const Json &after);
    void apply(Runtime &,const Json &);
    bool step(Runtime &,bool forward);
    Json save(Runtime &,const std::string &);
    Json command(Runtime &,const Json &);
};
}
