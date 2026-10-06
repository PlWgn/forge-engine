#pragma once
#include <string>
#include <map>
namespace forge {
struct TranslatedShaders {
    std::string vertex, fragment;
    // Generated identifier -> original public uniform name. HLSL keywords differ
    // from GLSL; compiler renaming must not change Python uniform lookup.
    std::map<std::string, std::string> vertexUniforms{}, fragmentUniforms{};
};
// Editable GLSL remains the portable source. Link stages before assigning locations.
// Generated HLSL uses TEXCOORD<location> vertex attributes and entry point main.
TranslatedShaders translateGlsl(const std::string& vertex, const std::string& fragment,
                               const std::string& label);
}
