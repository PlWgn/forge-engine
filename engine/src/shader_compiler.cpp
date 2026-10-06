#include <SPIRV/GlslangToSpv.h>
#include <forge/shader_compiler.hpp>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <mutex>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#include <stdexcept>
#include <vector>
namespace forge {
namespace {
struct CompilerProcess {
    CompilerProcess() {
        if (!glslang::InitializeProcess())
            throw std::runtime_error("Cannot initialize GLSL compiler");
    }
    ~CompilerProcess() { glslang::FinalizeProcess(); }
    std::mutex mutex;
};
CompilerProcess &compilerProcess() {
    static CompilerProcess process;
    return process;
}
void parse(glslang::TShader &shader, const std::string &source, const std::string &label,
           bool metal = false) {
    const char *text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, shader.getStage(), glslang::EShClientOpenGL, 330);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    auto messages = EShMessages(EShMsgSpvRules);
    if (metal) {
        shader.setEnvInput(glslang::EShSourceGlsl, shader.getStage(), glslang::EShClientVulkan,
                           100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
        shader.setEnvInputVulkanRulesRelaxed();
        shader.setGlobalUniformBlockName("ForgeUniforms");
        shader.setGlobalUniformBinding(0);
        shader.setGlobalUniformSet(0);
        messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    }
    if (!shader.parse(GetDefaultResources(), 330, false, messages))
        throw std::runtime_error(label + ":\n" + shader.getInfoLog() + shader.getInfoDebugLog());
}
struct Translation {
    std::string source;
    std::map<std::string, std::string> names;
    std::map<std::string, unsigned> components;
};
Translation hlsl(glslang::TIntermediate &intermediate, std::map<std::string, unsigned> &varyings,
                 bool vertex) {
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
    // glslang's default mapper may assign different locations when declarations
    // have a different order in the two stages. Link by the validated GLSL names.
    if (vertex) {
        for (const auto &output : resources.stage_outputs)
            varyings[compiler.get_name(output.id)] =
                compiler.get_decoration(output.id, spv::DecorationLocation);
    } else {
        for (const auto &input : resources.stage_inputs) {
            auto found = varyings.find(compiler.get_name(input.id));
            if (found != varyings.end())
                compiler.set_decoration(input.id, spv::DecorationLocation, found->second);
        }
    }
    if (resources.sampled_images.size() > 16)
        throw std::runtime_error(
            "Direct3D 11 supports at most 16 sampler2D resources per shader stage");
    unsigned binding = 0;
    for (const auto &resource : resources.sampled_images) {
        compiler.set_decoration(resource.id, spv::DecorationBinding, binding++);
        compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 0);
    }
    auto common = compiler.get_common_options();
    common.vertex.fixup_clipspace = true; // Existing GL projections use -w..w depth.
    common.vertex.flip_vert_y = true;     // Preserve GL framebuffer/texture row orientation.
    compiler.set_common_options(common);
    auto target = compiler.get_hlsl_options();
    target.shader_model = 50;
    compiler.set_hlsl_options(target);
    Translation result;
    result.source = compiler.compile();
    for (const auto &item : originalNames)
        result.names[compiler.get_name(item.first)] = item.second;
    return result;
}
Translation msl(glslang::TIntermediate &intermediate, std::map<std::string, unsigned> &varyings,
                bool vertex) {
    std::vector<unsigned> spirv;
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    glslang::GlslangToSpv(intermediate, spirv, &options);
    spirv_cross::CompilerMSL compiler(std::move(spirv));
    auto resources = compiler.get_shader_resources();
    // glslang's default mapper may assign different locations when declarations
    // have a different order in the two stages. Link by the validated GLSL names.
    if (vertex) {
        for (const auto &output : resources.stage_outputs)
            varyings[compiler.get_name(output.id)] =
                compiler.get_decoration(output.id, spv::DecorationLocation);
    } else {
        for (const auto &input : resources.stage_inputs) {
            auto found = varyings.find(compiler.get_name(input.id));
            if (found != varyings.end())
                compiler.set_decoration(input.id, spv::DecorationLocation, found->second);
        }
    }
    if (!resources.storage_buffers.empty() || !resources.storage_images.empty() ||
        !resources.push_constant_buffers.empty() || !resources.separate_images.empty() ||
        !resources.separate_samplers.empty())
        throw std::runtime_error("Resource is outside Forge's Metal draw API");
    if (resources.sampled_images.size() > 16)
        throw std::runtime_error("Metal draw API supports 16 sampler2D resources per stage");
    std::map<unsigned, std::string> names;
    std::map<std::pair<unsigned, unsigned>, std::string> members;
    std::map<std::string, unsigned> components;
    unsigned slot = 0;
    for (const auto &resource : resources.sampled_images) {
        auto &type = compiler.get_type(resource.type_id);
        if (type.image.dim != spv::Dim2D || type.image.arrayed || !type.array.empty())
            throw std::runtime_error("Metal draw API requires individual sampler2D resources");
        names[resource.id] = compiler.get_name(resource.id);
        compiler.set_decoration(resource.id, spv::DecorationBinding, slot++);
        compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 1);
    }
    for (const auto &resource : resources.uniform_buffers) {
        if (resources.uniform_buffers.size() != 1 || resource.name != "ForgeUniforms")
            throw std::runtime_error(
                "Custom GLSL uniform blocks are outside Forge's Metal uniform API");
        const auto &type = compiler.get_type(resource.base_type_id);
        for (unsigned i = 0; i < type.member_types.size(); ++i) {
            auto name = compiler.get_member_name(resource.base_type_id, i);
            const auto &member = compiler.get_type(type.member_types[i]);
            if (member.basetype == spirv_cross::SPIRType::Struct || member.array.size() > 1 ||
                (member.columns != 1 && (member.columns != 4 || member.vecsize != 4)))
                throw std::runtime_error("Metal uniform API supports vectors/mat4 and "
                                         "one-dimensional arrays");
            members[{resource.base_type_id, i}] = name;
            components[name] = member.vecsize * member.columns;
        }
        spirv_cross::MSLResourceBinding binding;
        binding.stage = compiler.get_execution_model();
        binding.desc_set = 0;
        binding.binding = 0;
        binding.msl_buffer = 0;
        compiler.add_msl_resource_binding(binding);
    }
    auto common = compiler.get_common_options();
    common.vertex.fixup_clipspace = true;
    common.vertex.flip_vert_y = true;
    compiler.set_common_options(common);
    auto target = compiler.get_msl_options();
    target.platform = spirv_cross::CompilerMSL::Options::macOS;
    target.set_msl_version(2, 0);
    compiler.set_msl_options(target);
    Translation result;
    result.source = compiler.compile();
    for (const auto &item : names)
        result.names[compiler.get_name(item.first)] = item.second;
    for (const auto &item : members)
        result.names[compiler.get_member_name(item.first.first, item.first.second)] = item.second;
    result.components = std::move(components);
    return result;
}
} // namespace
TranslatedShaders translateGlsl(const std::string &vertex, const std::string &fragment,
                                const std::string &label) {
    auto &process = compilerProcess();
    std::lock_guard<std::mutex> lock(process.mutex);
    glslang::TShader vs(EShLangVertex), ps(EShLangFragment);
    parse(vs, vertex, label + ".vertex");
    parse(ps, fragment, label + ".fragment");
    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&ps);
    if (!program.link(EShMessages(EShMsgSpvRules)) || !program.mapIO())
        throw std::runtime_error(label + " linking:\n" + program.getInfoLog() +
                                 program.getInfoDebugLog());
    try {
        std::map<std::string, unsigned> varyings;
        auto vertex = hlsl(*program.getIntermediate(EShLangVertex), varyings, true);
        auto fragment = hlsl(*program.getIntermediate(EShLangFragment), varyings, false);
        return {std::move(vertex.source), std::move(fragment.source), std::move(vertex.names),
                std::move(fragment.names)};
    } catch (const std::exception &error) {
        throw std::runtime_error(label + " GLSL to HLSL:\n" + error.what());
    }
}
TranslatedShaders translateGlslToMsl(const std::string &vertex, const std::string &fragment,
                                     const std::string &label) {
    auto &process = compilerProcess();
    std::lock_guard<std::mutex> lock(process.mutex);
    glslang::TShader vs(EShLangVertex), ps(EShLangFragment);
    parse(vs, vertex, label + ".vertex", true);
    parse(ps, fragment, label + ".fragment", true);
    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&ps);
    if (!program.link(EShMessages(EShMsgSpvRules | EShMsgVulkanRules)) || !program.mapIO())
        throw std::runtime_error(label + " linking:\n" + program.getInfoLog() +
                                 program.getInfoDebugLog());
    try {
        std::map<std::string, unsigned> varyings;
        auto v = msl(*program.getIntermediate(EShLangVertex), varyings, true),
             f = msl(*program.getIntermediate(EShLangFragment), varyings, false);
        return {std::move(v.source), std::move(f.source),     std::move(v.names),
                std::move(f.names),  std::move(v.components), std::move(f.components)};
    } catch (const std::exception &error) {
        throw std::runtime_error(label + " GLSL to MSL:\n" + error.what());
    }
}

} // namespace forge
