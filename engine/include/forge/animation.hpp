#pragma once
#include <forge/scene.hpp>
#include <forge/model.hpp>
namespace pybind11 {class module_;}
namespace forge {
struct Runtime;struct Editor;
struct AnimatorState {
    struct Layer {
        Json settings;
        std::shared_ptr<Model> source;
        std::vector<std::pair<int,int>> mapping;
        std::vector<Model::Channel> tracks;
        double length=0,time=0;
        bool authored=false;
        bool finished=false;
    };
    std::shared_ptr<Model> target;
    std::string model;
    std::vector<Layer> layers,from;
    std::vector<glm::mat4> pose,local,fromLocal;
    std::vector<std::vector<double>> fromMorphs;
    std::vector<std::vector<double>> morphs;
    Json events=Json::array();
    double fadeDuration=0,fadeTime=0;
    bool playing=true,automatic=true;
};
Json validateAnimator(Runtime&,const Entity&,Json);
void configureAnimator(Runtime&,Entity&,Json);
void crossfadeAnimation(Runtime&,Entity&,const std::string&,double,float,bool);
void updateAnimation(Entity&,double);
void updateAnimations(Runtime&,double);
void prepareAnimation(Runtime&,Entity&);
void validateAnimationAssets(const Config&);
void validateMorphWeights(const Model&,const Json&);
std::vector<ModelVertex> morphVertices(const Model::Part&,const std::vector<double>&);
void bindAnimation(pybind11::module_&);
bool animationEditor(Entity&,Runtime&,Editor&);
void transitionAnimator(Runtime&,Entity&,Json,double);
}
