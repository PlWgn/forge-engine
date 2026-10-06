#include <forge/shader_compiler.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <regex>
#ifdef _WIN32
#define NOMINMAX
#include <d3dcompiler.h>
#endif
void verify(const forge::TranslatedShaders& shaders, const std::string& label, bool native = false) {
    std::regex binding("register\\(s([0-9]+)\\)");
    for (const auto* source : {&shaders.vertex, &shaders.fragment})
        for (std::sregex_iterator it(source->begin(), source->end(), binding), end; it != end; ++it)
            if (std::stoi((*it)[1]) >= 16) throw std::runtime_error(label + ": HLSL sampler register exceeds 15");
#ifdef _WIN32
    unsigned flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (!native) flags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
    for (auto stage : {std::make_pair(&shaders.vertex, "vs_5_0"), std::make_pair(&shaders.fragment, "ps_5_0")}) {
        ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr;
        auto result = D3DCompile(stage.first->data(), stage.first->size(), label.c_str(), nullptr, nullptr,
                                 "main", stage.second, flags, 0, &code, &errors);
        std::string message = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "";
        if (code) code->Release(); if (errors) errors->Release();
        if (FAILED(result)) throw std::runtime_error(label + " D3DCompile:\n" + message);
    }
#else
    (void)native;
#endif
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Missing fixture " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected graphics directory");
        std::filesystem::path graphics(argv[1]);
        auto vertex = read(graphics / "default.vert");
        for (auto fragment : {"default.frag", "post.frag"}) {
            auto result = forge::translateGlsl(vertex, read(graphics / fragment), fragment);
            verify(result, fragment);
            if (result.vertex.find("SV_Position") == std::string::npos ||
                result.fragment.find("SV_Target") == std::string::npos)
                throw std::runtime_error("Missing HLSL stage semantics");
        }
        for (auto shader : {"particle.vert", "particle-instance.vert"})
            verify(forge::translateGlsl(read(graphics / shader), read(graphics / "particle.frag"), shader), shader);
        verify({read(graphics / "direct3d11/unlit.vert.hlsl"), read(graphics / "direct3d11/unlit.frag.hlsl")}, "Native unlit example", true);
        verify(forge::translateGlsl(vertex, "#version 330 core\nvoid main(){}", "Depth-only shadow"), "Depth-only shadow");
        auto custom = forge::translateGlsl("#version 330 core\nlayout(location=0) in vec3 p; out vec2 uv; uniform mat4 u_mvp; void main(){uv=p.xy;gl_Position=u_mvp*vec4(p,1);}",
            "#version 330 core\nin vec2 uv;out vec4 c;uniform vec4 custom_tint;void main(){c=custom_tint+vec4(uv,0,0);}", "Custom shader");
        if (custom.fragment.find("custom_tint") == std::string::npos) throw std::runtime_error("Custom uniform lost");
        verify(custom, "Custom shader");
        auto keyword = forge::translateGlsl("#version 330 core\nvoid main(){gl_Position=vec4(0);}",
            "#version 330 core\nuniform vec4 register;out vec4 c;void main(){c=register;}", "HLSL keyword uniform");
        bool mapped = false;
        for (const auto& item : keyword.fragmentUniforms) if (item.second == "register") mapped = true;
        if (!mapped) throw std::runtime_error("Compiler-renamed uniform lost its public name");
        verify(keyword, "HLSL keyword uniform");
        bool rejected = false;
        try { forge::translateGlsl(vertex, "#version 330 core\nINVALID", "Broken shader"); }
        catch (const std::exception& error) { rejected = std::string(error.what()).find("Broken shader.fragment") != std::string::npos; }
        if (!rejected) throw std::runtime_error("Missing shader diagnostics");
        rejected = false;
        try { forge::translateGlsl("#version 330 core\nout vec2 mismatch;void main(){mismatch=vec2(1);gl_Position=vec4(1);}",
            "#version 330 core\nin vec3 mismatch;out vec4 c;void main(){c=vec4(mismatch,1);}", "Stage mismatch"); }
        catch (const std::exception&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Stage mismatch accepted");
        std::cout << "Default/PBR/post/particle/custom GLSL translation and failure diagnostics passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
