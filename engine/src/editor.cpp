#include <forge/engine.hpp>
#include <forge/editor.hpp>
#include <forge/model.hpp>
#include <forge/geometry.hpp>
#include <forge/material.hpp>
#include <forge/physics.hpp>
#include <forge/animation.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
namespace forge {
void Editor::draw(World &world, Runtime &runtime, const std::array<bool, GLFW_KEY_LAST+1> &keys, const std::array<bool,8> &buttons, glm::vec2 mouseDelta, const Json &diagnostics) {
    const Config* config=&runtime.config;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    auto before = world.serialize();
    bool changed = false;
    auto restore = [&](const Json &data) {
        world.clearEntities();
        world.contacts.clear();
        world.physicsSettings=Json::object();world.physics3d.reset();world.particles.reset();
        world.scene = data;
        world.is3d = data.value("mode", "2d") == "3d";
        world.physicsEnabled = data.value("physics_enabled", true);
        auto bg = data.value("background", Json::array({0, 0, 0, 1}));
        world.background = {bg[0].get<float>(), bg[1].get<float>(), bg[2].get<float>(), bg[3].get<float>()};
        auto camera = data.value("camera", Json::object()),
             position = camera.value("position", Json::array({0, 0, 5})),
             target = camera.value("target", Json::array({0, 0, 0}));
        world.cameraPosition = {position[0].get<float>(), position[1].get<float>(), position[2].get<float>()};
        world.cameraTarget = {target[0].get<float>(), target[1].get<float>(), target[2].get<float>()};
        world.fov = camera.value("fov", 60.f);
        world.renderSettings = validateRenderSettings(data.value("rendering", Json::object()));
        auto v = data.value("gravity", Json::array({0, -9.81, 0}));
        world.gravity = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
        world.loadEntities(data["entities"]);
        world.configureSimulation(data);
    };
    auto save = [&]() {
        try {
            py::module_::import("forge").attr("save_scene")(std::string(sceneFile));
            status = "Saved " + std::string(sceneFile);
        } catch (const std::exception &error) {
            status = error.what();
        }
    };
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290, float(world.height)), ImGuiCond_FirstUseEver);
    ImGui::Begin("Forge Scene Editor");
    ImGui::TextUnformatted("Scene / hierarchy");
    ImGui::InputText("Save as", sceneFile, sizeof(sceneFile));
    if (ImGui::Button("Save JSON"))
        save();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        runtime.pendingScene = runtime.currentScene;
        undo.clear();
        redo.clear();
    }
    if (ImGui::Checkbox("Play preview", &preview)) {
        runtime.gamePaused = !preview;
        world.contacts.clear();
    }
    if (ImGui::Button("Undo") && !preview && !undo.empty()) {
        redo.push_back(before);
        auto previous = undo.back();
        undo.pop_back();
        restore(previous);
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo") && !preview && !redo.empty()) {
        undo.push_back(before);
        auto next = redo.back();
        redo.pop_back();
        restore(next);
    }
    if (ImGui::Button("Add sprite")) {
        auto e = world.spawn(Json{{"kind", "sprite"},
                                  {"name", "Sprite"},
                                  {"position", {float(world.width) / 2, float(world.height) / 2, 0}},
                                  {"scale", {100, 100, 1}}});
        selected = e->id;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add cube")) {
        auto e = world.spawn(Json{{"kind", "cube"}, {"name", "Cube"}});
        selected = e->id;
        changed = true;
    }
    if (ImGui::Button("Add text")) {
        auto e = world.spawn(Json{{"kind", "text"},
                                  {"name", "Text"},
                                  {"text", "New text"},
                                  {"screen", true},
                                  {"position", {100, 100, 0}}});
        selected = e->id;
        changed = true;
    }
    ImGui::Separator();
    for (auto &e : world.entities)
        if (e->alive) {
            auto label = e->name + "##" + e->id;
            if (ImGui::Selectable(label.c_str(), selected == e->id))
                selected = e->id;
        }
    ImGui::Separator();
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::TextWrapped("Middle mouse: pan. WASD: move camera. Ctrl+S: save. Preview enables game scripts.");
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(float(world.width) - 340, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, float(world.height)), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    auto entity = world.find(selected);
    if (entity) {
        auto original = *entity;
        ImGui::Text("ID: %s", entity->id.c_str());
        auto parent = world.find(entity->parent);
        if (ImGui::BeginCombo("Parent", parent ? parent->name.c_str() : "World root")) {
            auto attach = [&](const std::string &id) {
                try { world.reparent(*entity, id, true); changed = true; }
                catch (const std::exception &error) { status = error.what(); }
            };
            if (ImGui::Selectable("World root", !parent)) attach("");
            for (auto &candidate : world.entities)
                if (candidate->alive && candidate != entity && candidate->screen == entity->screen)
                    if (ImGui::Selectable((candidate->name + "##parent:" + candidate->id).c_str(), parent == candidate))
                        attach(candidate->id);
            ImGui::EndCombo();
        }
        auto worldPosition = world.worldPosition(*entity);
        ImGui::Text("World: %.2f, %.2f, %.2f", worldPosition.x, worldPosition.y, worldPosition.z);
        char name[512]{};
        std::snprintf(name, sizeof(name), "%s", entity->name.c_str());
        if (ImGui::InputText("Name", name, sizeof(name))) {
            entity->name = name;
            changed = true;
        }
        changed |= ImGui::DragFloat3("Position", glm::value_ptr(entity->position), world.is3d ? .05f : 1.f);
        changed |= ImGui::DragFloat3("Rotation", glm::value_ptr(entity->rotation), .5f);
        changed |= ImGui::DragFloat3("Scale", glm::value_ptr(entity->scale), world.is3d ? .02f : 1.f, .001f,
                                     100000.f);
        changed |= ImGui::ColorEdit4("Color", glm::value_ptr(entity->color));
        changed |= ImGui::DragFloat3("Collider", glm::value_ptr(entity->collider), .05f, 0, 100000);
        changed |= ImGui::Checkbox("Visible", &entity->visible);
        changed |= ImGui::Checkbox("Screen UI", &entity->screen);
        changed |= ImGui::Checkbox("Dynamic", &entity->dynamic);
        changed |= ImGui::Checkbox("Trigger", &entity->trigger);
        changed |= ImGui::Checkbox("Cast shadow", &entity->castsShadow);
        bool pbr = entity->materialData.value("shading", "legacy") == "pbr";
        if (ImGui::Checkbox("PBR", &pbr)) {
            entity->materialData["shading"] = pbr ? "pbr" : "legacy";
            changed = true;
        }
        if (pbr) {
            float metallic = entity->materialData.value("metallic", 0.f);
            float roughness = entity->materialData.value("roughness", 1.f);
            if (ImGui::SliderFloat("Metallic", &metallic, 0, 1)) { entity->materialData["metallic"] = metallic; changed = true; }
            if (ImGui::SliderFloat("Roughness", &roughness, 0, 1)) { entity->materialData["roughness"] = roughness; changed = true; }
        }
        if (entity->kind == "text") {
            char text[4096]{};
            std::snprintf(text, sizeof(text), "%s", entity->text.c_str());
            if (ImGui::InputTextMultiline("Text", text, sizeof(text))) {
                entity->text = text;
                changed = true;
            }
            changed |= ImGui::DragFloat("Font size", &entity->fontSize, .5f, 1, 1024);
        }
        try {
            if (!entity->materialData.empty()) entity->materialData = validateMaterial(*config, entity->materialData);
            world.syncTransforms();
            if (rigidPhysics(world) && world.activeCollider(*entity)) {
                auto body = *entity; body.position = world.worldPosition(*entity); body.rotation = entity->worldRotation;
                validatePhysicsEntity(body);
            }
        } catch (const std::exception &error) {
            *entity = std::move(original); world.syncTransforms(); changed = false; status = error.what();
        }
        if (ImGui::Button("Duplicate")) {
            auto data = world.serialize();
            auto copy = *std::find_if(data["entities"].begin(), data["entities"].end(),
                                      [&](auto &e) { return e["id"] == entity->id; });
            copy.erase("id");
            copy["name"] = entity->name + " copy";
            auto added = world.spawn(copy);
            selected = added->id;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            world.destroy(*entity);
            selected.clear();
            changed = true;
        }
        if (!entity->model.empty() && !proceduralName(entity->model))
            changed |= animationEditor(*entity,runtime,*this);
    }
    ImGui::Separator();
    bool threeD = world.is3d;
    if (ImGui::Checkbox("3D world", &threeD)) {
        world.is3d = threeD;
        changed = true;
    }
    changed |= ImGui::ColorEdit4("Background", glm::value_ptr(world.background));
    changed |= ImGui::Checkbox("Physics enabled", &world.physicsEnabled);
    changed |= ImGui::DragFloat3("Gravity", glm::value_ptr(world.gravity), .1f);
    changed |= ImGui::DragFloat3("Camera position", glm::value_ptr(world.cameraPosition), .05f);
    changed |= ImGui::DragFloat3("Camera target", glm::value_ptr(world.cameraTarget), .05f);
    if (ImGui::Button("Add sunlight")) {
        world.renderSettings["lights"] = Json::array({{{"type", "directional"},
                                                       {"direction", {-.5, -1, -.5}},
                                                       {"color", {1, 1, 1}},
                                                       {"intensity", 1},
                                                       {"shadows", true}}});
        changed = true;
    }
    if (world.renderSettings.contains("lights"))
        for (size_t i = 0; i < world.renderSettings["lights"].size(); ++i) {
            auto &light = world.renderSettings["lights"][i];
            ImGui::PushID(int(i));
            float intensity = light.value("intensity", 1.f);
            if (ImGui::SliderFloat("Intensity", &intensity, 0, 10)) {
                light["intensity"] = intensity;
                changed = true;
            }
            ImGui::PopID();
        }
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Assets")) {
        for (auto group : {"textures", "models", "scenes"}) {
            if (!ImGui::TreeNode(group))
                continue;
            for (auto &item : fs::recursive_directory_iterator(config->paths.at(group))) {
                if (item.is_regular_file()) {
                    auto extension = item.path().extension().u8string();
                    if (std::string(group) == "textures" && extension != ".png" && extension != ".jpg" &&
                        extension != ".jpeg" && extension != ".tga" && extension != ".bmp")
                        continue;
                    if (std::string(group) == "models" && extension != ".obj" && extension != ".gltf" &&
                        extension != ".glb" && extension != ".fbx" && extension != ".dae")
                        continue;
                    if (std::string(group) == "scenes" && extension != ".json" && extension != ".py")
                        continue;
                    auto relative =
                        item.path().lexically_relative(config->paths.at(group)).generic_u8string();
                    if (ImGui::Selectable(relative.c_str())) {
                        if (std::string(group) == "scenes" &&
                            (item.path().extension() == ".json" || item.path().extension() == ".py"))
                            runtime.pendingScene = relative;
                        else if (entity && std::string(group) == "textures") {
                            entity->texture = relative;
                            changed = true;
                        } else if (std::string(group) == "models") {
                            if (!entity)
                                entity = world.spawn(Json{{"kind", "mesh"},
                                                          {"model", relative},
                                                          {"name", item.path().stem().u8string()}});
                            else {
                                entity->animator.reset();entity->animatorSettings=Json::object();entity->morphWeights=Json::object();entity->animation.clear();
                                entity->model = relative;
                                entity->kind = "mesh";
                            }
                            selected = entity->id;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::TreePop();
        }
    }
    auto assetStats = runtime.assets.stats();
    ImGui::Text("Draw calls: %u; batches: %u", diagnostics.value("draw_calls",0u), diagnostics.value("batches",0u));
    ImGui::Text("GPU: %.1f MiB", diagnostics.value("gpu_bytes",0.0) / (1024 * 1024));
    ImGui::Text("Assets: %.1f MiB", assetStats["resident_bytes"].get<double>() / (1024 * 1024));
    ImGui::End();
    auto &io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard && !preview) {
        if (keys[GLFW_KEY_S] && (keys[GLFW_KEY_LEFT_CONTROL] || keys[GLFW_KEY_LEFT_SUPER]))
            save();
        glm::vec3 forward = world.cameraTarget - world.cameraPosition;
        if (glm::length(forward) < .0001f)
            forward = {0, 0, -1};
        forward = glm::normalize(forward);
        auto right = glm::cross(forward, glm::vec3(0, 1, 0));
        right = glm::length(right) > 1e-6f ? glm::normalize(right) : glm::vec3(1, 0, 0);
        auto move = glm::vec3(0);
        if (keys[GLFW_KEY_W])
            move += forward;
        if (keys[GLFW_KEY_S])
            move -= forward;
        if (keys[GLFW_KEY_D])
            move += right;
        if (keys[GLFW_KEY_A])
            move -= right;
        move *= runtime.dt * 5;
        world.cameraPosition += move;
        world.cameraTarget += move;
    }
    if (!io.WantCaptureMouse && buttons[GLFW_MOUSE_BUTTON_MIDDLE]) {
        auto move = glm::vec3(-mouseDelta.x, mouseDelta.y, 0) * (world.is3d ? .01f : 1.f);
        world.cameraPosition += move;
        world.cameraTarget += move;
    }
    if (changed && !preview) {
        undo.push_back(std::move(before));
        if (undo.size() > 100)
            undo.erase(undo.begin());
        redo.clear();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
} // namespace forge
