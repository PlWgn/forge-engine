#include <forge/render_optimization.hpp>
#include <forge/scene.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
using namespace forge;
static void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
static RenderOptimizationItem box(const char* name,glm::vec3 position,glm::vec3 size) {
    RenderOptimizationItem item;item.entity=std::make_shared<Entity>();item.entity->id=name;
    item.entity->optimization=std::make_shared<EntityOptimization>();
    item.entity->worldMatrix=glm::scale(glm::translate(glm::mat4(1),position),size);
    item.bounds=transformRenderBounds({glm::dvec3(-.5),glm::dvec3(.5),true},item.entity->worldMatrix);
    item.opaque=true;return item;
}
int main() {
    try {
        NativeRenderOptimizer policy;RenderOptimizationSettings settings;settings.enabled=true;
        RenderOptimizationView view;view.clipFromWorld=glm::perspective(glm::radians(60.f),1.f,.1f,100.f);
        auto visible=box("visible",{0,0,-5},{1,1,1}),outside=box("outside",{100,0,-5},{1,1,1});
        auto near=box("near",{0,0,-.1f},{1,1,1}),behind=box("behind",{0,0,5},{1,1,1});
        policy.beginFrame();auto result=policy.evaluate(view,{visible,outside,near,behind},settings);policy.endFrame();
        require(result.decisions[0].visible && !result.decisions[1].visible && result.decisions[2].visible && !result.decisions[3].visible,"Frustum/near-plane classification failed");
        auto reflected=glm::scale(glm::rotate(glm::mat4(1),.7f,glm::vec3(0,0,1)),glm::vec3(-2,3,1));
        auto transformed=transformRenderBounds({glm::dvec3(-.5),glm::dvec3(.5),true},reflected);
        for(auto corner:renderBoundsCorners({glm::dvec3(-.5),glm::dvec3(.5),true},reflected))
            require(glm::all(glm::greaterThanEqual(corner,transformed.min)) && glm::all(glm::lessThanEqual(corner,transformed.max)),"Rotated/reflected bound lost geometry");
        auto options=std::make_shared<EntityOptimization>();options->maxDistance=4;visible.entity->optimization=options;
        result=policy.evaluate(view,{visible},settings);require(result.decisions[0].reason==RenderCullReason::Distance,"Distance culling failed");
        view.shadow=true;result=policy.evaluate(view,{visible},settings);require(result.decisions[0].visible,"Distance culling removed a shadow caster");view.shadow=false;
        options->maxDistance=0;options->levels={{10,"low",""},{20,"lowest",""}};
        auto at=[&](float distance){visible.entity->worldMatrix=glm::translate(glm::mat4(1),glm::vec3(0,0,-distance));return policy.evaluate(view,{visible},settings).decisions[0].lod;};
        require(at(10)==1 && at(9.5f)==1 && at(8.9f)==0 && at(10.5f)==0 && at(11.1f)==1 && at(22.1f)==2 && at(5)==0,"LOD hysteresis or multi-level jumps failed");
        view.id="other";require(at(10.5f)==1,"Cameras shared LOD hysteresis");view.id="main";
        visible.deforming=true;require(at(30)==0,"Model LOD replaced a skeleton");visible.deforming=false;
        options->levels.clear();options->culling=false;result=policy.evaluate(view,{outside},settings);
        // Use the disabled options on the out-of-frustum object too.
        outside.entity->optimization=options;require(policy.evaluate(view,{outside},settings).decisions[0].visible,"Per-entity culling opt-out ignored");
        settings.occlusion=true;
        auto wall=box("wall",{0,0,-4},{3,5,.4f});wall.hasOccluder=true;
        wall.occluder=renderBoundsCorners({glm::dvec3(-.5),glm::dvec3(.5),true},wall.entity->worldMatrix);
        auto hidden=box("hidden",{0,0,-8},{.5f,.5f,.5f}),edge=box("edge",{4,0,-8},{.5f,.5f,.5f});
        result=policy.evaluate(view,{hidden,wall,edge,near},settings);
        require(!result.decisions[0].visible && result.decisions[0].reason==RenderCullReason::Occlusion,"Solid proxy did not occlude a fully hidden object");
        require(result.decisions[1].visible && result.decisions[2].visible && result.decisions[3].visible,"Occlusion removed proxy/self/edge/near-plane geometry");
        wall.opaque=false;require(policy.evaluate(view,{hidden,wall},settings).decisions[0].visible,"Transparent proxy occluded geometry");wall.opaque=true;
        auto narrow=box("narrow",{0,0,-4},{.001f,5,.4f});narrow.hasOccluder=true;narrow.occluder=renderBoundsCorners({glm::dvec3(-.5),glm::dvec3(.5),true},narrow.entity->worldMatrix);
        require(policy.evaluate(view,{hidden,narrow},settings).decisions[0].visible,"Partial tile coverage was treated as solid");
        view.shadow=true;require(policy.evaluate(view,{hidden,wall},settings).decisions[0].visible,"Color-view occlusion affected shadow pass");view.shadow=false;
        require(policy.evaluate(view,{hidden},settings).decisions[0].visible,"Occlusion retained an absent/moved proxy");
        settings.enabled=false;require(policy.evaluate(view,{outside},settings).decisions[0].visible,"Master disable ignored");
        settings.gridWidth=0;
        bool rejected=false;
        try {policy.evaluate(view,{visible},settings);}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"Unchecked native grid exceeded bounded allocation contract");
        std::cout<<"Native render optimization regressions passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
