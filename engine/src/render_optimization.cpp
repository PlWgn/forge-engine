#include <forge/render_optimization.hpp>
#include <forge/scene.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
namespace forge {
namespace {
bool finite(glm::dvec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
void include(RenderBounds& box, glm::dvec3 point) {
    if (!box.valid) {box.min = box.max = point; box.valid = true;}
    else {box.min = glm::min(box.min,point); box.max = glm::max(box.max,point);}
}
bool outside(const RenderBounds& box, const glm::mat4& matrix) {
    if (!box.valid) return false;
    auto points = renderBoundsCorners(box);
    unsigned common = 63;
    for (auto point : points) {
        auto clip = glm::dmat4(matrix)*glm::dvec4(point,1);
        if (!finite(glm::dvec3(clip)) || !std::isfinite(clip.w)) return false;
        // Keep touching/uncertain bounds. Canonical engine clip space is -w..w on every backend.
        double epsilon = 1e-6*std::max(1.0,std::abs(clip.w));
        unsigned mask = 0;
        for (int axis=0;axis<3;++axis) {
            if (clip[axis] < -clip.w-epsilon) mask |= 1u<<(axis*2);
            if (clip[axis] > clip.w+epsilon) mask |= 1u<<(axis*2+1);
        }
        common &= mask;
    }
    return common != 0;
}
struct Projection {
    std::vector<glm::dvec2> points;
    glm::dvec2 min{1},max{-1};
    double nearDepth=1,farDepth=-1;
    bool valid=false;
};
Projection project(const std::array<glm::dvec3,8>& points,const glm::mat4& matrix) {
    Projection result;
    for (auto point:points) {
        auto clip=glm::dmat4(matrix)*glm::dvec4(point,1);
        // Near-plane intersections/camera-containing boxes fail open, avoiding unsafe clipping/division.
        if (!finite(glm::dvec3(clip)) || !std::isfinite(clip.w) || clip.w<=1e-8 || clip.z<=-clip.w+1e-6)
            return {};
        auto ndc=glm::dvec3(clip)/clip.w;
        result.points.push_back(glm::dvec2(ndc));
        result.min=glm::min(result.min,glm::dvec2(ndc));result.max=glm::max(result.max,glm::dvec2(ndc));
        result.nearDepth=std::min(result.nearDepth,ndc.z);result.farDepth=std::max(result.farDepth,ndc.z);
    }
    result.valid=true;
    return result;
}
double cross(glm::dvec2 a,glm::dvec2 b,glm::dvec2 c) {
    auto x=b-a,y=c-a;return x.x*y.y-x.y*y.x;
}
std::vector<glm::dvec2> hull(std::vector<glm::dvec2> points) {
    std::sort(points.begin(),points.end(),[](auto a,auto b){return a.x==b.x?a.y<b.y:a.x<b.x;});
    points.erase(std::unique(points.begin(),points.end()),points.end());
    if(points.size()<3)return {};
    std::vector<glm::dvec2> result;
    for(auto point:points){while(result.size()>1 && cross(result[result.size()-2],result.back(),point)<=0)result.pop_back();result.push_back(point);}
    auto lower=result.size();
    for(auto it=points.rbegin()+1;it!=points.rend();++it){while(result.size()>lower && cross(result[result.size()-2],result.back(),*it)<=0)result.pop_back();result.push_back(*it);}
    result.pop_back();return result;
}
bool inside(const std::vector<glm::dvec2>& polygon,glm::dvec2 point) {
    for(size_t i=0;i<polygon.size();++i)
        if(cross(polygon[i],polygon[(i+1)%polygon.size()],point)<=1e-7)return false;
    return polygon.size()>=3;
}
// Each tile is either completely covered by a solid proxy, or remains unknown.
// Its depth is the proxy's farthest depth, never a sampled/optimistic near surface.
class OcclusionGrid {
    unsigned width,height;
    std::vector<double> depths;
    unsigned tile(double value,unsigned count) const {
        return unsigned(std::clamp((value+1)*.5*count,0.0,double(count-1)));
    }
public:
    explicit OcclusionGrid(const RenderOptimizationSettings& settings):width(settings.gridWidth),height(settings.gridHeight),
        depths(size_t(width)*height,std::numeric_limits<double>::infinity()) {}
    bool add(const Projection& projection) {
        if(!projection.valid || projection.farDepth>=1 || projection.max.x<=-1 || projection.min.x>=1 || projection.max.y<=-1 || projection.min.y>=1)return false;
        auto polygon=hull(projection.points);
        bool used=false;
        for(unsigned y=tile(projection.min.y,height);y<=tile(projection.max.y,height);++y)
            for(unsigned x=tile(projection.min.x,width);x<=tile(projection.max.x,width);++x){
                glm::dvec2 a(-1+2.0*x/width,-1+2.0*y/height),b(-1+2.0*(x+1)/width,-1+2.0*(y+1)/height);
                if(inside(polygon,a) && inside(polygon,b) && inside(polygon,{a.x,b.y}) && inside(polygon,{b.x,a.y})){
                    auto& depth=depths[size_t(y)*width+x];depth=std::min(depth,projection.farDepth+1e-6);used=true;
                }
            }
        return used;
    }
    bool hidden(const Projection& projection) const {
        if(!projection.valid || projection.min.x<-1 || projection.max.x>1 || projection.min.y<-1 || projection.max.y>1)return false;
        for(unsigned y=tile(projection.min.y,height);y<=tile(projection.max.y,height);++y)
            for(unsigned x=tile(projection.min.x,width);x<=tile(projection.max.x,width);++x)
                if(depths[size_t(y)*width+x]>=projection.nearDepth-1e-6)return false;
        return true;
    }
};
} // namespace
std::array<glm::dvec3,8> renderBoundsCorners(const RenderBounds& box,const glm::mat4& matrix) {
    std::array<glm::dvec3,8> result;
    for(unsigned i=0;i<8;++i){glm::dvec3 p;for(unsigned axis=0;axis<3;++axis)p[axis]=(i&(1u<<axis))?box.max[axis]:box.min[axis];result[i]=glm::dvec3(glm::dmat4(matrix)*glm::dvec4(p,1));}
    return result;
}
RenderBounds transformRenderBounds(const RenderBounds& box,const glm::mat4& matrix) {
    if(!box.valid)return {};
    RenderBounds result;
    for(auto p:renderBoundsCorners(box,matrix)){if(!finite(p))return {};include(result,p);}return result;
}
struct NativeRenderOptimizer::Impl {
    struct State {std::weak_ptr<Entity> entity;std::shared_ptr<const EntityOptimization> options;size_t lod;};
    struct Pass {std::unordered_map<const Entity*,State> states;bool used=false;};
    std::unordered_map<std::string,Pass> passes;
};
NativeRenderOptimizer::NativeRenderOptimizer():impl(std::make_unique<Impl>()) {}
NativeRenderOptimizer::~NativeRenderOptimizer()=default;
void NativeRenderOptimizer::beginFrame(){for(auto& pass:impl->passes)pass.second.used=false;}
void NativeRenderOptimizer::endFrame(){for(auto it=impl->passes.begin();it!=impl->passes.end();)if(!it->second.used)it=impl->passes.erase(it);else ++it;}
RenderOptimizationResult NativeRenderOptimizer::evaluate(const RenderOptimizationView& view,
    const std::vector<RenderOptimizationItem>& items,const RenderOptimizationSettings& settings) {
    if(settings.gridWidth<1 || settings.gridWidth>256 || settings.gridHeight<1 || settings.gridHeight>256 ||
        settings.maxOccluders<1 || settings.maxOccluders>1024)
        throw std::runtime_error("Render optimization grid/proxy budget outside supported range");
    RenderOptimizationResult result;result.decisions.resize(items.size());
    size_t frustum=0,distance=0,occlusion=0,lodCount=0,occluders=0,proxyTests=0,visible=0;
    auto& pass=impl->passes[view.id];pass.used=true;
    decltype(pass.states) next;
    for(size_t i=0;i<items.size();++i){
        auto& item=items[i];auto& decision=result.decisions[i];
        auto options=item.entity->optimization;
        if(!options)continue;
        auto p=glm::dvec3(item.entity->worldMatrix[3]);
        if(!view.is3d){p.z=0;}
        auto camera=view.position;if(!view.is3d)camera.z=0;
        double range=glm::length(p-camera);
        if(settings.enabled && options->culling && !view.shadow && settings.distance && options->maxDistance>0 && range>options->maxDistance){
            decision.visible=false;decision.reason=RenderCullReason::Distance;++distance;
        }else if(settings.enabled && options->culling && settings.frustum && outside(item.bounds,view.clipFromWorld)){
            decision.visible=false;decision.reason=RenderCullReason::Frustum;++frustum;
        }
        if(settings.enabled && settings.lod && options->lod && !options->levels.empty()){
            size_t level=0;
            auto old=pass.states.find(item.entity.get());
            bool remembered=old!=pass.states.end() && old->second.entity.lock()==item.entity && old->second.options==options;
            if(remembered)level=std::min(old->second.lod,options->levels.size());
            if(!remembered){while(level<options->levels.size() && range>=options->levels[level].distance)++level;}
            else {
                while(level<options->levels.size() && range>=options->levels[level].distance*(1+options->hysteresis))++level;
                while(level>0 && range<options->levels[level-1].distance*(1-options->hysteresis))--level;
            }
            // A skeleton/morph asset must retain its original palette and animation state.
            if(item.deforming && level>0 && !options->levels[level-1].model.empty())level=0;
            decision.lod=level;next.emplace(item.entity.get(),Impl::State{item.entity,options,level});
            if(level>0 && decision.visible)++lodCount;
        }
    }
    pass.states.swap(next);
    if(settings.enabled && settings.occlusion && view.is3d && !view.shadow){
        OcclusionGrid grid(settings);
        for(size_t i=0;i<items.size() && proxyTests<settings.maxOccluders;++i)
            if(result.decisions[i].visible && items[i].opaque && items[i].hasOccluder) {
                ++proxyTests;
                if(grid.add(project(items[i].occluder,view.clipFromWorld)))++occluders;
            }
        for(size_t i=0;i<items.size();++i){auto& item=items[i];auto& decision=result.decisions[i];
            if(decision.visible && item.bounds.valid && item.entity->optimization && item.entity->optimization->culling && grid.hidden(project(renderBoundsCorners(item.bounds),view.clipFromWorld))){
                decision.visible=false;decision.reason=RenderCullReason::Occlusion;++occlusion;
                if(decision.lod>0)--lodCount;
            }
        }
    }
    for(auto& decision:result.decisions)if(decision.visible)++visible;
    result.diagnostics={{"tested",items.size()},{"visible",visible},{"frustum_culled",frustum},{"distance_culled",distance},
        {"occlusion_culled",occlusion},{"lod_selected",lodCount},{"occluders",occluders},{"proxy_tests",proxyTests},
        {"grid_cells",settings.occlusion && view.is3d && !view.shadow?settings.gridWidth*settings.gridHeight:0}};
    return result;
}
} // namespace forge
