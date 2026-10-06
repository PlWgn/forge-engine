#include <forge/config.hpp>
#include <forge/graphics_settings.hpp>
#include <algorithm>
namespace forge {
void validateGraphicsConfiguration(const Config& config) {
    auto options = config.data.value("renderer", Json::object());
    if (!options.is_object()) throw std::runtime_error("renderer must be an object");
    auto backend = options.value("backend", Json("opengl"));
    if (!backend.is_string() || (backend != "opengl" && backend != "direct3d11" && backend != "auto"))
        throw std::runtime_error("renderer.backend must be opengl, direct3d11, or auto");
    auto directx = options.value("direct3d11", Json::object());
    if (!directx.is_object()) throw std::runtime_error("renderer.direct3d11 must be an object");
    auto driver = directx.value("driver", Json("auto"));
    if (!driver.is_string() || (driver != "auto" && driver != "hardware" && driver != "warp"))
        throw std::runtime_error("renderer.direct3d11.driver must be auto, hardware, or warp");
    if (directx.contains("debug") && !directx.at("debug").is_boolean())
        throw std::runtime_error("renderer.direct3d11.debug must be boolean");
    auto shaders = directx.value("shaders", Json::object());
    if (!shaders.is_object()) throw std::runtime_error("renderer.direct3d11.shaders must be an object");
    for (auto name : {"scene", "post", "shadow", "particles", "particles_instanced"}) {
        if (!shaders.contains(name)) continue;
        const auto& pair = shaders.at(name);
        if (!pair.is_object()) throw std::runtime_error(std::string("HLSL shader pair must be an object: ") + name);
        for (auto stage : {"vertex", "fragment"}) {
            if (!pair.contains(stage) || !pair.at(stage).is_string() || pair.at(stage).get<std::string>().empty())
                throw std::runtime_error(std::string("HLSL shader pair requires vertex and fragment paths: ") + name);
            auto path = config.asset("graphics", pair.at(stage).get<std::string>());
            if (!fs::is_regular_file(path)) throw std::runtime_error("Missing HLSL shader: " + path.u8string());
        }
    }
    for (auto field : {"shadow_vertex_shader", "shadow_fragment_shader"}) {
        if (!options.contains(field)) continue;
        if (!options.at(field).is_string() || options.at(field).get<std::string>().empty())
            throw std::runtime_error(std::string("renderer.") + field + " must be a shader path");
        auto path = config.asset("graphics", options.at(field).get<std::string>());
        if (!fs::is_regular_file(path)) throw std::runtime_error("Missing shadow shader: " + path.u8string());
    }
}
}
