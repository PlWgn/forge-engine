#pragma once
#include <forge/types.hpp>
#include <forge/gl.hpp>
#include <algorithm>
#include <array>
namespace forge {
struct World;struct Runtime;
// Independent builtin workspace; custom shells do not depend on this UI.
struct Editor {
    struct Layout { float left,right,bottom,x,y,width,height; };
    float leftFraction=.19f,rightFraction=.27f,bottomFraction=.30f;
    Layout layout(float width,float height) const {
        float left=width*std::clamp(leftFraction,.12f,.28f);
        float right=width*std::clamp(rightFraction,.20f,.36f);
        float bottom=std::max(80.f,(height-74.f)*std::clamp(bottomFraction,.18f,.48f));
        return {left,right,bottom,left+4,74,std::max(1.f,width-left-right-8),std::max(1.f,height-74-bottom-28)};
    }
    Layout viewport(float width,float height,bool threeD) const {
        auto area=layout(width,height);
        if(!threeD && width>0 && height>0) {
            float scale=std::min(area.width/width,area.height/height);
            float w=width*scale,h=height*scale;
            area.x+=(area.width-w)*.5f;area.y+=(area.height-h)*.5f;area.width=w;area.height=h;
        }
        return area;
    }
    bool preview=false,grid=true,settings=false,styled=false;
    bool animationPlaying=false,animationOpen=false;
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
    char hierarchySearch[256]="",assetSearch[256]="",consoleSearch[256]="";
    struct Asset { std::string group,name; };
    std::vector<Asset> assetEntries;
    std::string assetGroup="scenes",assetSelection,assetError,assetRoot;
    bool assetsReady=false,consoleFollow=true,consoleErrors=false;
    double consoleRefresh=0;
    std::string consoleText,jsonEntity;
    std::array<char,32768> entityJson{};
    void refreshAssets(Runtime &);
    void projectPanel(World &,Runtime &);
    void sceneOverlay(World &,Runtime &,const Layout &);
    void focus(World &,const std::string &);
    void draw(World &,Runtime &,const std::array<bool,GLFW_KEY_LAST+1> &,const std::array<bool,8> &,glm::vec2,const Json &);
};
}
