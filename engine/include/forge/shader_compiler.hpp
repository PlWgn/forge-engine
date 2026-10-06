#pragma once
#include <map>
#include <string>
namespace forge {
struct TranslatedShaders {
    std::string vertex, fragment;
    // Generated identifier -> original public uniform name. HLSL keywords differ
    // from GLSL; compiler renaming must not change Python uniform lookup.
    std::map<std::string, std::string> vertexUniforms{}, fragmentUniforms{};
    // MSL may pad scalar arrays to int4/float4; retain their logical API shape.
    std::map<std::string, unsigned> vertexComponents{}, fragmentComponents{};
};
// Editable GLSL remains the portable source. Link stages before assigning
// locations. Generated HLSL uses TEXCOORD<location> vertex attributes and entry
// point main.
TranslatedShaders translateGlsl(const std::string &vertex, const std::string &fragment,
                                const std::string &label);
TranslatedShaders translateGlslToMsl(const std::string &vertex, const std::string &fragment,
                                     const std::string &label);
} // namespace forge
