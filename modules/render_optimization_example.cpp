// Optional example: add this file to native_modules and recompile.
// Uses public contracts only; no renderer Impl or backend-specific access.
#include <forge/engine.hpp>
#include <forge/render_optimization.hpp>
namespace {
class ExampleVisibility final : public forge::RenderOptimizationPolicy {
    forge::NativeRenderOptimizer native;
public:
    void beginFrame() override {native.beginFrame();}
    void endFrame() override {native.endFrame();}
    forge::RenderOptimizationResult evaluate(const forge::RenderOptimizationView& view,
        const std::vector<forge::RenderOptimizationItem>& items,
        const forge::RenderOptimizationSettings& settings) override {
        auto result=native.evaluate(view,items,settings); // Optional: replace this entirely.
        size_t custom=0;
        for(size_t i=0;i<items.size();++i)
            if(items[i].entity->data.value("example_hide",false)) {
                if(result.decisions[i].visible)++custom;
                result.decisions[i].visible=false;
                result.decisions[i].reason=forge::RenderCullReason::Custom;
            }
        result.diagnostics["custom_culled"]=custom;
        return result;
    }
};
}
FORGE_MODULE(render_optimization_example) {
    module.def("use_example_render_policy",[](bool enabled) {
        if(!forge::active || !forge::active->renderer)
            throw std::runtime_error("Example render policy requires a graphical runtime");
        forge::active->renderer->setOptimizationPolicy(enabled?std::make_shared<ExampleVisibility>():nullptr);
    },pybind11::arg("enabled")=true);
}
