#include <forge/shader_compiler.hpp>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace forge {
namespace {
struct CompilerProcess {
    CompilerProcess() {
        if (!glslang::InitializeProcess()) throw std::runtime_error("Cannot initialize GLSL compiler");
    }
    ~CompilerProcess() { glslang::FinalizeProcess(); }
    std::mutex mutex;
};
void parse(glslang::TShader& shader, const std::string& source, const std::string& label) {
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, shader.getStage(), glslang::EShClientOpenGL, 330);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    auto messages = EShMessages(EShMsgSpvRules);
    if (!shader.parse(GetDefaultResources(), 330, false, messages))
        throw std::runtime_error(label + ":\n" + shader.getInfoLog() + shader.getInfoDebugLog());
}
struct Translation { std::string source; std::map<std::string, std::string> names; };
Translation hlsl(glslang::TIntermediate& intermediate) {
    std::vector<unsigned> spirv;
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    glslang::GlslangToSpv(intermediate, spirv, &options);
    spirv_cross::CompilerHLSL compiler(std::move(spirv));
    std::map<unsigned, std::string> originalNames;
    for (auto id : compiler.get_active_interface_variables())
        if (compiler.get_storage_class(id) == spv::StorageClassUniformConstant)
            originalNames[id] = compiler.get_name(id);
    // OpenGL auto-mapping may place samplers after hundreds of scalar/matrix
    // uniform locations. HLSL sampler registers have a separate 16-slot space.
    auto resources = compiler.get_shader_resources();
    if (resources.sampled_images.size() > 16)
        throw std::runtime_error("Direct3D 11 supports at most 16 sampler2D resources per shader stage");
    unsigned binding = 0;
    for (const auto& resource : resources.sampled_images) {
        compiler.set_decoration(resource.id, spv::DecorationBinding, binding++);
        compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 0);
    }
    auto common = compiler.get_common_options();
    common.vertex.fixup_clipspace = true; // Existing GL projections use -w..w depth.
    common.vertex.flip_vert_y = true; // Preserve GL framebuffer/texture row orientation.
    compiler.set_common_options(common);
    auto target = compiler.get_hlsl_options();
    target.shader_model = 50;
    compiler.set_hlsl_options(target);
    Translation result;
    result.source = compiler.compile();
    for (const auto& item : originalNames) result.names[compiler.get_name(item.first)] = item.second;
    return result;
}
}
TranslatedShaders translateGlsl(const std::string& vertex, const std::string& fragment,
                               const std::string& label) {
    static CompilerProcess process;
    std::lock_guard<std::mutex> lock(process.mutex);
    glslang::TShader vs(EShLangVertex), ps(EShLangFragment);
    parse(vs, vertex, label + ".vertex");
    parse(ps, fragment, label + ".fragment");
    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&ps);
    if (!program.link(EShMessages(EShMsgSpvRules)) || !program.mapIO())
        throw std::runtime_error(label + " linking:\n" + program.getInfoLog() + program.getInfoDebugLog());
    try {
        auto vertex = hlsl(*program.getIntermediate(EShLangVertex));
        auto fragment = hlsl(*program.getIntermediate(EShLangFragment));
        return {std::move(vertex.source), std::move(fragment.source), std::move(vertex.names), std::move(fragment.names)};
    } catch (const std::exception& error) {
        throw std::runtime_error(label + " GLSL to HLSL:\n" + error.what());
    }
}
}
