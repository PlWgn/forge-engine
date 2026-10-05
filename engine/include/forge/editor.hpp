#pragma once
#include <forge/types.hpp>
#include <forge/gl.hpp>
#include <array>
namespace forge {
struct World;struct Runtime;
struct Editor {
    bool preview=false;
    bool animationPlaying=false,animationOpen=true;
    float animationFade=.25f,boneTime=0;
    std::string boneNode;
    glm::vec3 bonePosition{0},boneRotation{0},boneScale{1};
    std::string animationEntity,animationStatus;
    Json animationDraft;
    std::array<char,32768> animationJson{};
    char animationPrefab[512]="animation-prefab.json";
    std::string status,sceneIdentity;
    char extensionName[256]="",extensionArguments[4096]="{}";
    char sceneFile[512]="editor-scene.json";
    void draw(World &,Runtime &,const std::array<bool,GLFW_KEY_LAST+1> &,const std::array<bool,8> &,glm::vec2,const Json &);
};
}
