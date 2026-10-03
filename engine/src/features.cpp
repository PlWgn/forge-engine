#include <algorithm>
#include <forge/model.hpp>
#include <pybind11/stl.h>
namespace forge {
namespace {
Runtime &runtime() {
    if (!active)
        throw std::runtime_error("Engine runtime is not active");
    return *active;
}
py::object python(const Json &value) {
    return py::module_::import("json").attr("loads")(value.dump());
}
glm::vec3 vector(const Json &value) {
    if (!value.is_array() || value.size() != 3)
        throw std::runtime_error("Expected three vector components");
    return {finiteNumber(value[0], "vector"), finiteNumber(value[1], "vector"),
            finiteNumber(value[2], "vector")};
}
void range(const Json &value, const std::string &field, double low, double high) {
    auto number = finiteNumber(value, field);
    if (number < low || number > high)
        throw std::runtime_error(field + " outside supported range");
}
void uniformValues(const Json &uniforms) {
    if (!uniforms.is_object())
        throw std::runtime_error("uniforms must be an object");
    for (auto it = uniforms.begin(); it != uniforms.end(); ++it) {
        if (it.key().empty() || it.key().size() > 128)
            throw std::runtime_error("Invalid uniform name");
        auto value = it.value();
        if (value.is_boolean())
            continue;
        if (value.is_number_integer())
            range(value, it.key(), -2147483648.0, 2147483647.0);
        if (value.is_array()) {
            if (value.empty() || value.size() > 4)
                throw std::runtime_error("Uniform vector needs 1..4 components");
            for (auto &n : value)
                finiteNumber(n, it.key());
        } else
            finiteNumber(value, it.key());
    }
}
void inputValues(const Json &frame) {
    for (auto field : {"keys", "pressed", "released"}) {
        auto values = frame.value(field, Json::array());
        if (!values.is_array() || values.size() > 512)
            throw std::runtime_error("Input keys need a bounded list");
        for (auto &key : values)
            if (!key.is_string() || key.get<std::string>().empty() || key.get<std::string>().size() > 32)
                throw std::runtime_error("Invalid input key");
    }
    for (auto field : {"position", "delta", "scroll"}) {
        auto values = frame.value(field, Json::array({0, 0}));
        if (!values.is_array() || values.size() != 2)
            throw std::runtime_error("Mouse input needs two components");
        for (auto &value : values)
            finiteNumber(value, field);
    }
    auto buttons = [](const Json &values, int maximum) {
        if (!values.is_array())
            throw std::runtime_error("Input buttons need a list");
        for (auto &button : values)
            if (!button.is_number_integer() || button.get<double>() < 0 || button.get<double>() > maximum)
                throw std::runtime_error("Invalid input button");
    };
    buttons(frame.value("buttons", Json::array()), 7);
    auto pads = frame.value("gamepads", Json::array());
    if (!pads.is_array() || pads.size() > 16)
        throw std::runtime_error("Invalid gamepads list");
    std::set<int> ids;
    for (auto &pad : pads) {
        auto id = pad.at("id");
        if (!id.is_number_integer() || id.get<double>() < 0 || id.get<double>() > 15 ||
            !ids.insert(id.get<int>()).second)
            throw std::runtime_error("Invalid gamepad id");
        buttons(pad.value("buttons", Json::array()), 14);
        auto axes = pad.value("axes", Json::array({0, 0, 0, 0, 0, 0}));
        if (!axes.is_array() || axes.size() != 6)
            throw std::runtime_error("Gamepad needs six axes");
        for (auto &axis : axes)
            range(axis, "gamepad axis", -1, 1);
    }
    if (!frame.value("events", Json::array()).is_array())
        throw std::runtime_error("Input events need a list");
    if (frame.contains("dt"))
        range(frame["dt"], "input dt", 0, 1);
}
} // namespace
Json validateRenderSettings(Json data) {
    if (!data.is_object())
        throw std::runtime_error("rendering must be an object");
    uniformValues(data.value("uniforms", Json::object()));
    auto targets = data.value("targets", Json::object());
    if (!targets.is_object())
        throw std::runtime_error("render targets must be an object");
    for (auto it = targets.begin(); it != targets.end(); ++it) {
        auto &view = it.value();
        if (it.key().empty() || !view.is_object())
            throw std::runtime_error("Invalid render target");
        for (auto field : {"width", "height"}) {
            auto value = view.value(field, Json(320));
            if (!value.is_number_integer())
                throw std::runtime_error("Target dimensions must be integers");
            range(value, field, 1, 4096);
        }
        auto mode = view.value("mode", "3d");
        if (mode != "2d" && mode != "3d")
            throw std::runtime_error("Target mode must be 2d or 3d");
        auto position = vector(view.value("position", Json::array({0, 0, 5}))),
             target = vector(view.value("target", Json::array({0, 0, 0})));
        if (mode == "3d" && glm::length(glm::dvec3(position) - glm::dvec3(target)) < 1e-6)
            throw std::runtime_error("Camera position and target must differ");
        range(view.value("fov", Json(60)), "fov", 1, 178);
        if (view.contains("include_ui") && !view["include_ui"].is_boolean())
            throw std::runtime_error("include_ui must be boolean");
        if (view.contains("layers") &&
            (!view["layers"].is_number_unsigned() && !view["layers"].is_number_integer()))
            throw std::runtime_error("layers must be integer");
        if (view.contains("layers"))
            range(view["layers"], "layers", 0, 4294967295.0);
    }
    auto post = data.value("postprocess", Json::object());
    if (!post.is_object())
        throw std::runtime_error("postprocess must be an object");
    for (auto field : {"grain", "scanlines", "vignette", "fade"})
        range(post.value(field, Json(0)), field, 0, 1);
    range(post.value("bloom", Json(0)), "bloom", 0, 4);
    range(post.value("aberration", Json(0)), "aberration", 0, 32);
    range(post.value("gamma", Json(1)), "gamma", .1, 8);
    uniformValues(post.value("uniforms", Json::object()));
    if (post.contains("enabled") && !post["enabled"].is_boolean())
        throw std::runtime_error("Postprocess enabled must be boolean");
    vector(data.value("ambient", Json::array({.15, .15, .15})));
    auto shadow = data.value("shadow_size", Json(1024));
    if (!shadow.is_number_integer())
        throw std::runtime_error("shadow_size must be integer");
    range(shadow, "shadow_size", 64, 4096);
    auto lights = data.value("lights", Json::array());
    if (!lights.is_array() || lights.size() > 16)
        throw std::runtime_error("lights needs an array of at most 16 lights");
    for (auto &light : lights) {
        if (!light.is_object())
            throw std::runtime_error("Invalid light");
        auto type = light.value("type", "point");
        if (type != "point" && type != "directional" && type != "spot")
            throw std::runtime_error("Light type must be point, directional or spot");
        vector(light.value("position", Json::array({0, 5, 0})));
        auto direction = vector(light.value("direction", Json::array({0, -1, 0})));
        if (glm::length(direction) < 1e-5f)
            throw std::runtime_error("Light direction cannot be zero");
        vector(light.value("color", Json::array({1, 1, 1})));
        range(light.value("intensity", Json(1)), "intensity", 0, 1000);
        range(light.value("range", Json(20)), "range", .01, 100000);
        range(light.value("cone", Json(30)), "cone", 1, 89);
        range(light.value("shadow_extent", Json(20)), "shadow_extent", .1, 10000);
        if (light.contains("shadows") && !light["shadows"].is_boolean())
            throw std::runtime_error("shadows must be boolean");
    }
    return data;
}
void bindFeatures(py::module_ &m) {
    m.def(
        "user_path",
        [](const std::string &kind, const std::string &file) {
            return userPath(runtime().config, kind, file).u8string();
        },
        py::arg("kind") = "data", py::arg("file") = "");
    m.def(
        "storage_path",
        [](const std::string &file) { return storagePath(runtime().config, file).u8string(); },
        py::arg("file") = ".");
    m.def("crash_report", [](const std::string &message) { return crashReport(message).u8string(); });
    m.def("asset_request", [](const std::string &group, const std::string &file) {
        if (runtime().tearingDown)
            throw std::runtime_error("Cannot request assets during scene teardown");
        return runtime().assets.request(group, file);
    });
    m.def("asset_info", [](unsigned id) { return python(runtime().assets.info(id)); });
    m.def("asset_bytes", [](unsigned id) {
        auto bytes = runtime().assets.bytes(id);
        return py::bytes(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    });
    m.def("asset_release", [](unsigned id) { runtime().assets.release(id); });
    m.def("set_asset_budget", [](size_t bytes) { runtime().assets.budget(bytes); });
    m.def("asset_stats", []() { return python(runtime().assets.stats()); });
    m.def("model_info", [](const std::string &file) {
        return python(runtime().assets.model(runtime().config.asset("models", file))->info());
    });
    m.def(
        "animation_pose",
        [](const std::string &file, const std::string &clip, double seconds, bool loop) {
            if (!std::isfinite(seconds) || seconds < 0)
                throw std::runtime_error("Animation time must be finite and nonnegative");
            auto model = runtime().assets.model(runtime().config.asset("models", file));
            auto pose = model->pose(clip, seconds, loop);
            Json result = Json::array();
            for (auto &matrix : pose) {
                Json values = Json::array();
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        values.push_back(matrix[c][r]);
                result.push_back(values);
            }
            return python(result);
        },
        py::arg("model"), py::arg("clip") = "", py::arg("seconds") = 0, py::arg("loop") = true);
    m.def(
        "play_animation",
        [](Entity &entity, const std::string &clip, float speed, bool loop) {
            finiteNumber(speed, "animation speed");
            auto model = runtime().assets.model(runtime().config.asset("models", entity.model));
            model->pose(clip, 0, loop);
            entity.animation = clip;
            entity.animationTime = 0;
            entity.animationSpeed = speed;
            entity.animationLoop = loop;
            entity.animationPlaying = true;
        },
        py::arg("entity"), py::arg("clip"), py::arg("speed") = 1, py::arg("loop") = true);
    m.def(
        "pause_animation", [](Entity &entity, bool paused) { entity.animationPlaying = !paused; },
        py::arg("entity"), py::arg("paused") = true);
    m.def("set_render_target", [](const std::string &name, py::dict options) {
        auto data = runtime().world.renderSettings;
        if (!data.contains("targets"))
            data["targets"] = Json::object();
        data["targets"][name] = fromPython(options);
        runtime().world.renderSettings = validateRenderSettings(std::move(data));
        return "@target:" + name;
    });
    m.def("remove_render_target", [](const std::string &name) {
        auto &data = runtime().world.renderSettings;
        if (data.contains("targets"))
            data["targets"].erase(name);
    });
    m.def("render_settings", []() { return python(runtime().world.renderSettings); });
    m.def("set_postprocess", [](py::dict options) {
        auto data = runtime().world.renderSettings;
        data["postprocess"] = fromPython(options);
        runtime().world.renderSettings = validateRenderSettings(std::move(data));
    });
    m.def("set_lights", [](py::list lights) {
        auto data = runtime().world.renderSettings;
        data["lights"] = fromPython(lights);
        runtime().world.renderSettings = validateRenderSettings(std::move(data));
    });
    m.def("set_shader_uniform", [](const std::string &name, py::object value) {
        auto data = runtime().world.renderSettings;
        if (!data.contains("uniforms"))
            data["uniforms"] = Json::object();
        data["uniforms"][name] = fromPython(value);
        runtime().world.renderSettings = validateRenderSettings(std::move(data));
    });
    m.def("renderer_stats",
          []() { return python(runtime().renderer ? runtime().renderer->diagnostics() : Json::object()); });
    m.def("profile", []() { return python(runtime().profile); });
    m.def("set_physics_enabled", [](bool enabled) {
        runtime().world.physicsEnabled = enabled;
        if (!enabled)
            runtime().world.contacts.clear();
    });
    m.def("physics_enabled", []() { return runtime().world.physicsEnabled; });
    m.def("gravity", []() {
        auto v = runtime().world.gravity;
        return std::array<float, 3>{v.x, v.y, v.z};
    });
    m.def(
        "move_character",
        [](Entity &entity, std::array<float, 3> delta, float skin) {
            return python(runtime().world.moveCharacter(entity, vector(Json(delta)), skin));
        },
        py::arg("entity"), py::arg("delta"), py::arg("skin") = .001f);
    m.def("input_snapshot", []() { return python(runtime().inputFrame); });
    m.def("inject_input", [](py::dict frame) {
        auto value = fromPython(frame);
        inputValues(value);
        if (value.contains("dt")) {
            auto next = finiteNumber(value["dt"], "input dt");
            runtime().time += next - runtime().dt;
            runtime().dt = next;
        }
        runtime().inputFrame = std::move(value);
    });
    m.def("input_events", []() { return python(runtime().inputFrame.value("events", Json::array())); });
    m.def("gamepads", []() { return python(runtime().inputFrame.value("gamepads", Json::array())); });
    m.def("set_window", [](py::dict options) {
        if (runtime().renderer && !runtime().tearingDown)
            runtime().renderer->windowOptions(fromPython(options));
    });
    m.def("window_settings", []() {
        return python(runtime().renderer ? runtime().renderer->windowOptions()
                                         : runtime().config.data.value("window", Json::object()));
    });
    m.def("reload_in_progress", []() { return runtime().reloading; });
    m.def("defer_persistence", [](py::object callback) {
        if (!PyCallable_Check(callback.ptr()))
            throw std::runtime_error("Persistence callback must be callable");
        if (runtime().tearingDown)
            return;
        if (runtime().reloading || runtime().initializing)
            runtime().persistence.push_back(callback);
        else
            callback();
    });
    m.def("scene_data", []() { return python(runtime().world.serialize()); });
    m.def("save_scene", [](const std::string &file) {
        auto path = runtime().config.asset("scenes", file);
        if (path.extension() != ".json")
            throw std::runtime_error("Editor scenes must be JSON");
        auto temp = path;
        temp += ".tmp";
        fs::create_directories(path.parent_path());
        {
            std::ofstream stream(temp);
            stream << runtime().world.serialize().dump(2);
            if (!stream)
                throw std::runtime_error("Cannot write scene");
        }
        std::error_code error;
        fs::rename(temp, path, error);
        if (error) {
            fs::remove(path);
            fs::rename(temp, path);
        }
    });
    m.def("editor_enabled", []() { return runtime().editing; });
    m.def("set_sound_options",
          [](unsigned id, py::dict options) { runtime().audio.options(id, fromPython(options)); });
    m.def("sound_info", [](unsigned id) { return python(runtime().audio.info(id)); });
    m.def("sound_position", [](unsigned id) {
        auto info = runtime().audio.info(id);
        return info.value("cursor", 0.0);
    });
    m.def("audio_stats", []() { return python(runtime().audio.diagnostics()); });
    m.def("configure_audio", [](py::dict options) { runtime().audio.configure(fromPython(options)); });
    m.def(
        "set_audio_listener",
        [](std::array<float, 3> position, std::array<float, 3> direction) {
            runtime().audio.listener(vector(Json(position)), vector(Json(direction)));
        },
        py::arg("position"), py::arg("direction") = std::array<float, 3>{0, 0, -1});
}
} // namespace forge
