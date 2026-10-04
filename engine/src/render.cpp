#include <forge/engine.hpp>
#include <chrono>
#include <forge/gl.hpp>
#include <forge/model.hpp>
#include <forge/particles.hpp>
#include <forge/particle_render.hpp>
#include <forge/gpu_resources.hpp>
#include <forge/text.hpp>
#include <unordered_map>
#include <forge/editor.hpp>
#include <forge/geometry.hpp>
#include <forge/material.hpp>
#include <forge/shaders.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <tuple>
#define STBI_WINDOWS_UTF8
#include <stb_image.h>
#include <array>
#include <cmath>
#include <sstream>
#include <stb_truetype.h>
namespace forge {
namespace {
std::string textFile(const fs::path &p) {
    std::ifstream s(p, std::ios::binary);
    if (!s)
        throw std::runtime_error("Cannot read: " + p.u8string());
    return {std::istreambuf_iterator<char>(s), {}};
}
int keyCode(std::string name) {
    for (auto &c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (name.size() == 1)
        return name[0];
    static const std::map<std::string, int> keys = {
        {"PAGEUP", GLFW_KEY_PAGE_UP},      {"PAGEDOWN", GLFW_KEY_PAGE_DOWN},
        {"SPACE", GLFW_KEY_SPACE},         {"ESCAPE", GLFW_KEY_ESCAPE},
        {"ENTER", GLFW_KEY_ENTER},         {"TAB", GLFW_KEY_TAB},
        {"BACKSPACE", GLFW_KEY_BACKSPACE}, {"LEFT", GLFW_KEY_LEFT},
        {"RIGHT", GLFW_KEY_RIGHT},         {"UP", GLFW_KEY_UP},
        {"DOWN", GLFW_KEY_DOWN},           {"SHIFT", GLFW_KEY_LEFT_SHIFT},
        {"CTRL", GLFW_KEY_LEFT_CONTROL},   {"ALT", GLFW_KEY_LEFT_ALT}};
    if (name.size() >= 2 && name[0] == 'F') {
        int n = std::stoi(name.substr(1));
        if (n >= 1 && n <= 25)
            return GLFW_KEY_F1 + n - 1;
    }
    auto it = keys.find(name);
    if (it == keys.end())
        throw std::runtime_error("Unknown key: " + name);
    return it->second;
}
} // namespace
namespace {
glm::mat4 modelMatrix(const Entity &e) {return e.worldMatrix;}
double textWorldScale(const Entity &e) {
    return std::max(glm::length(glm::dvec3(e.worldMatrix[0])),
                    glm::length(glm::dvec3(e.worldMatrix[1])));
}
} // namespace
struct Renderer::Impl {
    GLFWwindow *window = nullptr;
    const Config *config = nullptr;
    fs::path screenshotPath, previousScreenshot;
    bool captured = false, previousCaptured = false, firstMouse = true, vsync = true;
    std::array<bool, GLFW_KEY_LAST + 1> keys{}, previous{};
    std::array<bool, 8> buttons{}, previousButtons{};
    glm::vec2 mousePosition{0}, mouseDelta{0}, mouseScroll{0};
    std::vector<unsigned> characters;
    unsigned program = 0, postProgram = 0, shadowProgram = 0, particleProgram = 0, whiteTexture = 0;
    std::unique_ptr<ParticleRenderer> particleRenderer;
    bool particleInstanced=false;
    std::vector<ParticleDraw> particleSnapshot;
    std::unordered_map<std::string,unsigned> frameImages;
    std::unordered_map<std::string,fs::path> resolvedImages;
    unsigned imageResolutions=0;
    size_t particleUpload=0;
    double particleSortMs=0;

    unsigned particleCalls = 0, particleQuads = 0;
    std::map<std::string, Mesh> meshes;
    std::map<std::string, unsigned> textures;
    std::map<std::string, size_t> textureBytes;
    std::map<std::string, unsigned> textureFrames;
    std::vector<std::shared_ptr<FontFace>> fontFaces;
    struct Page {
        unsigned texture;
        int size, x = 2, y = 2, row = 0;
    };
    struct Glyph {
        unsigned page = 0;
        int w = 0, h = 0, x = 0, y = 0, advance = 0;
        glm::vec4 uv{0};
    };
    std::vector<Page> pages;
    std::map<std::tuple<int, int, int>, Glyph> glyphs;
    struct Target {
        unsigned color = 0, depth = 0, fbo = 0;
        int width = 0, height = 0;
    };
    std::map<std::string, Target> targets;
    std::map<std::string, std::shared_ptr<Model>> models;
    std::map<std::string,unsigned long long> modelRevisions;
    Json materialOptions=Json::object();
    const Model::Part* materialPart=nullptr;
    glm::vec3 cameraPosition{0,0,5};
    std::vector<Vertex> batch;
    unsigned batchTexture = 0;
    glm::vec4 batchColor{1};
    glm::mat4 batchView{1};
    glm::vec4 drawUV{0, 0, 1, 1};
    bool drawFlip = false;
    bool batching = true, shadowPass = false;
    float density = 1;
    unsigned frame = 0, drawCalls = 0, batchCalls = 0, triangles = 0;
    size_t batchAllocated = 0, gpuBytes = 0, gpuLimit = 128 * 1024 * 1024;
    Json renderOptions = Json::object(), entityUniforms = Json::object(), stats = Json::object(),
         previousWindow;
    glm::mat4 shadowMatrix{1};
    bool shadowEnabled = false;
    bool editorEnabled = false, editorContext = false;
    Editor editorState;
    Runtime* context = nullptr;
    ~Impl() {
        if (editorContext) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        clear();
        if (window) {
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    }
    void clear() {
        batch.clear();
        particleRenderer.reset();frameImages.clear();resolvedImages.clear();
        for (auto &[name, m] : meshes) {
            gl::DeleteBuffers(1, &m.vbo);
            gl::DeleteVertexArrays(1, &m.vao);
        }
        meshes.clear();
        for (auto &[name, t] : textures)
            gl::DeleteTextures(1, &t);
        textures.clear();
        textureBytes.clear();
        textureFrames.clear();
        for (auto &page : pages)
            gl::DeleteTextures(1, &page.texture);
        pages.clear();
        glyphs.clear();
        for (auto &[name, target] : targets)
            freeTarget(target);
        targets.clear();
        models.clear();modelRevisions.clear();
        fontFaces.clear();
        if (whiteTexture)
            gl::DeleteTextures(1, &whiteTexture);
        whiteTexture = 0;
        for (auto *p : {&program, &postProgram, &shadowProgram, &particleProgram})
            if (*p) {
                gl::DeleteProgram(*p);
                *p = 0;
            }
        gpuBytes = 0;
    }
    void swapResources(Impl &other) {
        for (auto pair : {std::pair<unsigned *, unsigned *>{&program, &other.program},
                          {&postProgram, &other.postProgram},
                          {&shadowProgram, &other.shadowProgram},
                          {&particleProgram, &other.particleProgram},
                          {&whiteTexture, &other.whiteTexture}})
            std::swap(*pair.first, *pair.second);
        meshes.swap(other.meshes);
        textures.swap(other.textures);
        textureBytes.swap(other.textureBytes);
        textureFrames.swap(other.textureFrames);
        fontFaces.swap(other.fontFaces);
        pages.swap(other.pages);
        glyphs.swap(other.glyphs);
        targets.swap(other.targets);
        models.swap(other.models);modelRevisions.swap(other.modelRevisions);
        std::swap(batchAllocated, other.batchAllocated);
        particleRenderer.swap(other.particleRenderer);
        std::swap(particleInstanced,other.particleInstanced);
        frameImages.clear();other.frameImages.clear();resolvedImages.clear();other.resolvedImages.clear();
        std::swap(gpuBytes, other.gpuBytes);
        std::swap(gpuLimit, other.gpuLimit);
        std::swap(batching, other.batching);
    }
    void reserve(size_t bytes) {
        for (auto it = textures.begin(); gpuBytes + bytes > gpuLimit && it != textures.end();) {
            auto name = it->first;
            if (textureFrames[name] >= frame || it->second == batchTexture) {
                ++it;
                continue;
            }
            gpuBytes -= textureBytes[name];
            gl::DeleteTextures(1, &it->second);
            textureBytes.erase(name);
            textureFrames.erase(name);
            it = textures.erase(it);
        }
        if (bytes > gpuLimit || gpuBytes > gpuLimit - bytes)
            throw std::runtime_error("GPU memory budget exceeded; reduce render targets/atlases or increase "
                                     "renderer.gpu_budget_bytes");
        gpuBytes += bytes;
    }
    void setup() {
        auto options = config->data.value("renderer", Json::object());
        batching = options.value("batching", true);
        gpuLimit = options.value("gpu_budget_bytes", size_t(128 * 1024 * 1024));
        auto vertex = options.contains("vertex_shader")
                          ? textFile(config->asset("graphics", options["vertex_shader"]))
                          : vertexDefault;
        auto fragment = options.contains("fragment_shader")
                            ? textFile(config->asset("graphics", options["fragment_shader"]))
                            : fragmentDefault;
        program = linkProgram(vertex, fragment, "Scene shader");
        postProgram = linkProgram(vertexDefault,
                                  options.contains("post_shader")
                                      ? textFile(config->asset("graphics", options["post_shader"]))
                                      : postDefault,
                                  "Post shader");
        shadowProgram = linkProgram(vertexDefault, R"(#version 330 core
in vec2 v_uv;
in vec4 v_vertex_color;
uniform sampler2D u_texture;
uniform vec4 u_color;
uniform int u_textured, u_alpha_mode;
uniform float u_alpha_cutoff;
void main(){
    float alpha=u_color.a*v_vertex_color.a;
    if(u_textured==1) alpha*=texture(u_texture,v_uv).a;
    if(u_alpha_mode==1 && alpha<u_alpha_cutoff) discard;
}
)", "Shadow shader");
        auto particleShader = [&](const char* field, const char* file, const char* fallback) {
            auto path = config->asset("graphics", options.value(field, std::string(file)));
            return options.contains(field) || fs::is_regular_file(path) ? textFile(path) : std::string(fallback);
        };
        auto particleVertex=particleShader("particle_vertex_shader","particle.vert",particleVertexDefault);
        auto newlineNormalized=[](const std::string& source){
            std::string result;result.reserve(source.size());
            for(size_t i=0;i<source.size();++i)if(source[i]!='\r' || i+1==source.size() || source[i+1]!='\n')result.push_back(source[i]);
            return result;
        };
        particleInstanced=options.value("particle_instancing",true) && newlineNormalized(particleVertex)==newlineNormalized(particleVertexDefault);
        if(particleInstanced)particleVertex=particleShader("particle_instance_shader","particle-instance.vert",particleInstanceDefault);
        particleProgram=linkProgram(particleVertex,particleShader("particle_fragment_shader","particle.frag",particleFragmentDefault),"Particle shader");
        particleRenderer=std::make_unique<ParticleRenderer>(particleInstanced);
        meshes["sprite"] = upload({{{-.5f, -.5f, 0}, {0, 0}, {0, 0, 1}},
                                   {{.5f, -.5f, 0}, {1, 0}, {0, 0, 1}},
                                   {{.5f, .5f, 0}, {1, 1}, {0, 0, 1}},
                                   {{-.5f, -.5f, 0}, {0, 0}, {0, 0, 1}},
                                   {{.5f, .5f, 0}, {1, 1}, {0, 0, 1}},
                                   {{-.5f, .5f, 0}, {0, 1}, {0, 0, 1}}});
        meshes["batch"] = upload({});
        std::vector<Vertex> cube;
        for (int axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                glm::vec3 n(0), u(0), v(0);
                n[axis] = float(sign);
                u[(axis + 1) % 3] = 1;
                v[(axis + 2) % 3] = float(sign);
                auto center = n * .5f;
                glm::vec3 p[] = {center - (u + v) * .5f, center + (u - v) * .5f, center + (u + v) * .5f,
                                 center + (-u + v) * .5f};
                glm::vec2 uv[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                for (int i : {0, 1, 2, 0, 2, 3})
                    cube.push_back({p[i], uv[i], n});
            }
        meshes["cube"] = upload(cube);
        reserve(42 * sizeof(Vertex) + 4);
        const unsigned char white[] = {255, 255, 255, 255};
        whiteTexture = texture(white, 1, 1);
        fontFaces = fonts(*config);
    }
    void freeTarget(Target &target) {
        if (target.fbo)
            gl::DeleteFramebuffers(1, &target.fbo);
        if (target.color)
            gl::DeleteTextures(1, &target.color);
        if (target.depth)
            gl::DeleteTextures(1, &target.depth);
        gpuBytes -= size_t(target.width) * target.height * 8;
        target = {};
    }
    Target &target(const std::string &name, int width, int height) {
        auto &result = targets[name];
        if (result.width == width && result.height == height)
            return result;
        if (result.fbo)
            freeTarget(result);
        reserve(size_t(width) * height * 8);
        result.width = width;
        result.height = height;
        result.color = texture(nullptr, width, height);
        gl::GenTextures(1, &result.depth);
        gl::BindTexture(gl::TEXTURE_2D, result.depth);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::DEPTH_COMPONENT24, width, height, 0, gl::DEPTH_COMPONENT,
                       gl::UNSIGNED_INT, nullptr);
        for (auto parameter : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER})
            gl::TexParameteri(gl::TEXTURE_2D, parameter, gl::LINEAR);
        for (auto parameter : {gl::TEXTURE_WRAP_S, gl::TEXTURE_WRAP_T})
            gl::TexParameteri(gl::TEXTURE_2D, parameter, gl::CLAMP_TO_EDGE);
        gl::GenFramebuffers(1, &result.fbo);
        gl::BindFramebuffer(gl::FRAMEBUFFER, result.fbo);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, result.color, 0);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::TEXTURE_2D, result.depth, 0);
        if (gl::CheckFramebufferStatus(gl::FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete render target: " + name);
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        return result;
    }
    size_t textureStorage(int w,int h)const{size_t bytes=size_t(w)*h*4;if(config->data.value("renderer",Json::object()).value("mipmaps",false))while(w>1 || h>1){w=std::max(1,w/2);h=std::max(1,h/2);bytes+=size_t(w)*h*4;}return bytes;}
    void filterTexture(){auto options=config->data.value("renderer",Json::object());bool nearest=options.value("texture_filter","linear")=="nearest";bool mip=options.value("mipmaps",false);gl::TexParameteri(gl::TEXTURE_2D,gl::TEXTURE_MAG_FILTER,nearest?0x2600:gl::LINEAR);gl::TexParameteri(gl::TEXTURE_2D,gl::TEXTURE_MIN_FILTER,mip?(nearest?0x2700:0x2703):(nearest?0x2600:gl::LINEAR));if(mip)gl::GenerateMipmap(gl::TEXTURE_2D);}
    unsigned imagePath(const fs::path &path) {
        auto name = path.u8string();
        auto found = textures.find(name);
        if (found != textures.end()) {
            textureFrames[name] = frame;
            return found->second;
        }
        auto pixels = context->assets.image(path);
        auto bytes=textureStorage(pixels->width,pixels->height);reserve(bytes);
        auto id = texture(pixels->pixels.data(), pixels->width, pixels->height);
        filterTexture();textures[name] = id;
        textureBytes[name] = bytes;
        textureFrames[name] = frame;
        return id;
    }
    unsigned image(const std::string &name) {
        if (name.empty())
            return 0;
        if (name.rfind("@target:", 0) == 0) {
            auto found = targets.find("camera:" + name.substr(8));
            if (found == targets.end())
                throw std::runtime_error("Unknown render target texture: " + name);
            return found->second.color;
        }
        auto cached=frameImages.find(name);
        if(cached!=frameImages.end())return cached->second;
        auto path=resolvedImages.find(name);
        if(path==resolvedImages.end()){
            ++imageResolutions;path=resolvedImages.emplace(name,config->asset("textures",name)).first;
        }
        auto id=imagePath(path->second);frameImages.emplace(name,id);return id;
    }
    void lighting(unsigned p) {
        auto lights = renderOptions.value("lights", Json::array());
        std::vector<glm::vec3> positions, directions, colors;
        std::vector<glm::vec4> parameters;
        for (auto &light : lights) {
            auto vector = [](const Json &v) {
                return glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
            };
            positions.push_back(vector(light.value("position", Json::array({0, 5, 0}))));
            directions.push_back(vector(light.value("direction", Json::array({0, -1, 0}))));
            colors.push_back(vector(light.value("color", Json::array({1, 1, 1}))));
            auto type = light.value("type", "point");
            parameters.emplace_back(type == "directional" ? 1
                                    : type == "spot"      ? 2
                                                          : 0,
                                    light.value("intensity", 1.f), light.value("range", 20.f),
                                    std::cos(glm::radians(light.value("cone", 30.f))));
        }
        gl::Uniform1i(gl::GetUniformLocation(p, "u_light_count"), int(lights.size()));
        glm::vec3 ambient(.15f);
        auto a = renderOptions.value("ambient", Json::array({.15, .15, .15}));
        ambient = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
        gl::Uniform3fv(gl::GetUniformLocation(p, "u_ambient"), 1, glm::value_ptr(ambient));
        if (!positions.empty()) {
            gl::Uniform3fv(gl::GetUniformLocation(p, "u_light_position"), int(positions.size()),
                           glm::value_ptr(positions[0]));
            gl::Uniform3fv(gl::GetUniformLocation(p, "u_light_direction"), int(directions.size()),
                           glm::value_ptr(directions[0]));
            gl::Uniform3fv(gl::GetUniformLocation(p, "u_light_color"), int(colors.size()),
                           glm::value_ptr(colors[0]));
            gl::Uniform4fv(gl::GetUniformLocation(p, "u_light_params"), int(parameters.size()),
                           glm::value_ptr(parameters[0]));
        }
        gl::Uniform1i(gl::GetUniformLocation(p, "u_shadow_enabled"), shadowEnabled ? 1 : 0);
        gl::UniformMatrix4fv(gl::GetUniformLocation(p, "u_shadow_matrix"), 1, 0,
                             glm::value_ptr(shadowMatrix));
        if (shadowEnabled) {
            gl::ActiveTexture(gl::TEXTURE0 + 1);
            gl::BindTexture(gl::TEXTURE_2D, targets.at("shadow").depth);
            gl::Uniform1i(gl::GetUniformLocation(p, "u_shadow_map"), 1);
        }
    }
    unsigned embeddedImage(const ImageData& pixels,const std::string& name){auto found=textures.find(name);if(found!=textures.end()){textureFrames[name]=frame;return found->second;}auto bytes=textureStorage(pixels.width,pixels.height);reserve(bytes);auto id=texture(pixels.pixels.data(),pixels.width,pixels.height);filterTexture();textures[name]=id;textureBytes[name]=bytes;textureFrames[name]=frame;return id;}
    unsigned materialImage(const char* field){
        auto name=materialOptions.value(field,"");if(!name.empty())return image(name);
        if(materialPart){auto embedded=materialPart->embeddedMaps.find(field);if(embedded!=materialPart->embeddedMaps.end())return embeddedImage(*embedded->second,"material:"+std::to_string(reinterpret_cast<uintptr_t>(materialPart))+":"+field);auto found=materialPart->maps.find(field);if(found!=materialPart->maps.end())return imagePath(found->second);}
        return 0;
    }
    glm::vec4 materialColor()const{auto c=materialOptions.value("base_color",Json::array({1,1,1,1}));return {c[0],c[1],c[2],c[3]};}
    void material(unsigned p){
        gl::Uniform1i(gl::GetUniformLocation(p,"u_pbr"),materialOptions.value("shading","legacy")=="pbr");
        auto alpha=materialOptions.value("alpha_mode","blend");gl::Uniform1i(gl::GetUniformLocation(p,"u_alpha_mode"),alpha=="opaque"?0:alpha=="mask"?1:2);
        for(auto field:{"metallic","roughness","normal_scale","occlusion_strength","alpha_cutoff"})gl::Uniform1f(gl::GetUniformLocation(p,(std::string("u_")+field).c_str()),materialOptions.value(field,std::string(field)=="metallic"?0.f:std::string(field)=="alpha_cutoff"?.5f:1.f));
        auto e=materialOptions.value("emissive",Json::array({0,0,0}));glm::vec3 emission{e[0],e[1],e[2]};gl::Uniform3fv(gl::GetUniformLocation(p,"u_emissive"),1,glm::value_ptr(emission));gl::Uniform3fv(gl::GetUniformLocation(p,"u_camera_position"),1,glm::value_ptr(cameraPosition));
        const char* fields[]={"normal_texture","metallic_roughness_texture","metallic_texture","roughness_texture","occlusion_texture","emissive_texture"};
        const char* uniforms[]={"u_normal_map","u_mr_map","u_metallic_map","u_roughness_map","u_occlusion_map","u_emissive_map"};
        for(int i=0;i<6;++i){auto tex=materialImage(fields[i]);gl::ActiveTexture(gl::TEXTURE0+2+i);gl::BindTexture(gl::TEXTURE_2D,tex?tex:whiteTexture);gl::Uniform1i(gl::GetUniformLocation(p,uniforms[i]),2+i);gl::Uniform1i(gl::GetUniformLocation(p,("u_maps["+std::to_string(i)+"]").c_str()),tex?1:0);}
    }
    void draw(const Mesh &mesh, const glm::mat4 &model, const glm::mat4 &view, const glm::vec4 &color,
              unsigned tex, bool lit, const std::vector<glm::mat4> *bones = nullptr,
              unsigned overrideProgram = 0) {
        auto p = overrideProgram ? overrideProgram : shadowPass ? shadowProgram : program;
        gl::UseProgram(p);
        auto mvp = view * model;
        gl::UniformMatrix4fv(gl::GetUniformLocation(p, "u_mvp"), 1, 0, glm::value_ptr(mvp));
        gl::UniformMatrix4fv(gl::GetUniformLocation(p, "u_model"), 1, 0, glm::value_ptr(model));
        gl::Uniform4fv(gl::GetUniformLocation(p, "u_uv_rect"), 1, glm::value_ptr(drawUV));
        gl::Uniform1i(gl::GetUniformLocation(p, "u_flip_y"), drawFlip ? 1 : 0);
        gl::Uniform1i(gl::GetUniformLocation(p, "u_skinned"), bones && !bones->empty() ? 1 : 0);
        if (bones && !bones->empty())
            gl::UniformMatrix4fv(gl::GetUniformLocation(p, "u_bones"), int(bones->size()), 0,
                                 glm::value_ptr(bones->front()));
        auto tint=color*materialColor();gl::Uniform4fv(gl::GetUniformLocation(p, "u_color"), 1, glm::value_ptr(tint));
        gl::Uniform1i(gl::GetUniformLocation(p, "u_textured"), tex ? 1 : 0);
        gl::Uniform1f(gl::GetUniformLocation(p, "u_lit"), lit && materialOptions.value("shading","legacy")!="unlit" ? 1 : 0);
        gl::Uniform1f(gl::GetUniformLocation(p, "u_time"), context ? float(context->time) : 0);
        glm::vec2 resolution(context ? context->world.width : 1280, context ? context->world.height : 720);
        gl::Uniform2fv(gl::GetUniformLocation(p, "u_resolution"), 1, glm::value_ptr(resolution));
        if (!shadowPass && !overrideProgram) {
            lighting(p);material(p);
            uniforms(p, renderOptions.value("uniforms", Json::object()));
            uniforms(p, entityUniforms);
        }
        if (shadowPass) {
            gl::Uniform1i(gl::GetUniformLocation(p,"u_alpha_mode"),materialOptions.value("alpha_mode","blend")=="mask"?1:0);
            gl::Uniform1f(gl::GetUniformLocation(p,"u_alpha_cutoff"),materialOptions.value("alpha_cutoff",.5f));
        }
        gl::ActiveTexture(gl::TEXTURE0);
        gl::BindTexture(gl::TEXTURE_2D, tex ? tex : whiteTexture);
        gl::Uniform1i(gl::GetUniformLocation(p, "u_texture"), 0);
        gl::BindVertexArray(mesh.vao);
        gl::DrawArrays(gl::TRIANGLES, 0, mesh.count);
        ++drawCalls;
        triangles += unsigned(mesh.count / 3);
    }
    void flush() {
        if (batch.empty())
            return;
        auto &mesh = meshes.at("batch");
        size_t bytes = sizeof(Vertex) * batch.size();
        if (bytes > batchAllocated)
            reserve(bytes - batchAllocated);
        else
            gpuBytes -= batchAllocated - bytes;
        batchAllocated = bytes;
        mesh.count = int(batch.size());
        gl::BindBuffer(gl::ARRAY_BUFFER, mesh.vbo);
        gl::BufferData(gl::ARRAY_BUFFER, sizeof(Vertex) * batch.size(), batch.data(), gl::STREAM_DRAW);
        auto savedUV = drawUV;
        auto savedFlip = drawFlip;
        drawUV = {0, 0, 1, 1};
        drawFlip = false;
        draw(mesh, glm::mat4(1), batchView, batchColor, batchTexture, false);
        drawUV = savedUV;
        drawFlip = savedFlip;
        ++batchCalls;
        batch.clear();
        batchTexture = 0;
    }
    void quad(const glm::mat4 &model, const glm::mat4 &view, const glm::vec4 &color, unsigned tex,
              glm::vec4 uv, bool flip = false) {
        if (!batch.empty() &&
            (tex != batchTexture || color != batchColor || view != batchView || batch.size() > 65520))
            flush();
        if (batch.empty()) {
            batchView = view;
            batchColor = color;
            batchTexture = tex;
        }
        static const glm::vec3 positions[] = {{-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0},
                                              {-.5f, -.5f, 0}, {.5f, .5f, 0},  {-.5f, .5f, 0}};
        static const glm::vec2 coords[] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 6; ++i) {
            Vertex v;
            v.p = glm::vec3(model * glm::vec4(positions[i], 1));
            v.uv = {uv.x + coords[i].x * uv.z, uv.y + coords[i].y * uv.w};
            if (flip)
                v.uv.y = 1 - v.uv.y;
            v.n = {0, 0, 1};
            batch.push_back(v);
        }
        if (!batching || !entityUniforms.empty())
            flush();
    }
    Glyph &glyph(int face, int code, int raster) {
        auto key = std::make_tuple(face, raster, code);
        auto found = glyphs.find(key);
        if (found != glyphs.end())
            return found->second;
        Glyph glyph;
        int bearing;
        auto &font = fontFaces[face]->font;
        stbtt_GetCodepointHMetrics(&font, code, &glyph.advance, &bearing);
        float scale = stbtt_ScaleForPixelHeight(&font, float(raster));
        auto pixels = stbtt_GetCodepointBitmap(&font, 0, scale, code, &glyph.w, &glyph.h, &glyph.x, &glyph.y);
        if (pixels && glyph.w > 0 && glyph.h > 0) {
            Page *page = nullptr;
            unsigned index = 0;
            for (unsigned i = 0; i < pages.size(); ++i) {
                auto &candidate = pages[i];
                if (glyph.w + 4 > candidate.size || glyph.h + 4 > candidate.size)
                    continue;
                if (candidate.x + glyph.w + 2 > candidate.size) {
                    candidate.x = 2;
                    candidate.y += candidate.row + 2;
                    candidate.row = 0;
                }
                if (candidate.y + glyph.h + 2 <= candidate.size) {
                    page = &candidate;
                    index = i;
                    break;
                }
            }
            if (!page) {
                flush();
                int size = 2048;
                while (size < glyph.w + 4 || size < glyph.h + 4)
                    size *= 2;
                int maximum;
                gl::GetIntegerv(gl::MAX_TEXTURE_SIZE, &maximum);
                if (size > maximum) {
                    stbtt_FreeBitmap(pixels, nullptr);
                    throw std::runtime_error("Glyph atlas exceeds GPU texture limit");
                }
                reserve(size_t(size) * size * 4);
                std::vector<unsigned char> zero(size_t(size) * size * 4, 0);
                pages.push_back({texture(zero.data(), size, size), size});
                index = unsigned(pages.size() - 1);
                page = &pages.back();
            }
            std::vector<unsigned char> rgba(size_t(glyph.w) * glyph.h * 4, 255);
            for (size_t i = 0; i < size_t(glyph.w) * glyph.h; ++i)
                rgba[i * 4 + 3] = pixels[i];
            gl::BindTexture(gl::TEXTURE_2D, page->texture);
            gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
            gl::TexSubImage2D(gl::TEXTURE_2D, 0, page->x, page->y, glyph.w, glyph.h, gl::RGBA,
                              gl::UNSIGNED_BYTE, rgba.data());
            glyph.page = index;
            glyph.uv = {float(page->x) / page->size, float(page->y) / page->size, float(glyph.w) / page->size,
                        float(glyph.h) / page->size};
            page->x += glyph.w + 2;
            page->row = std::max(page->row, glyph.h);
        }
        stbtt_FreeBitmap(pixels, nullptr);
        return glyphs.emplace(key, glyph).first->second;
    }
    void text(const Entity &e, const glm::mat4 &projection) {
        if (e.fontSize <= 0)
            return;
        int raster = glyphRasterSize(e.fontSize, textWorldScale(e), density);
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&fontFaces.front()->font, &ascent, &descent, &gap);
        float primaryScale = stbtt_ScaleForPixelHeight(&fontFaces.front()->font, float(raster));
        float ratio = e.fontSize / raster, x = 0, y = ascent * primaryScale * ratio;
        int previousCode = 0, previousFace = -1;
        auto origin = modelMatrix(e);
        for (auto code : unicode(e.text)) {
            if (code == '\n') {
                x = 0;
                y += (ascent - descent + gap) * primaryScale * ratio;
                previousCode = 0;
                previousFace = -1;
                continue;
            }
            int face = fontFor(fontFaces, code);
            auto &font = fontFaces[face]->font;
            float scale = stbtt_ScaleForPixelHeight(&font, float(raster));
            if (previousCode && face == previousFace)
                x += stbtt_GetCodepointKernAdvance(&font, previousCode, code) * scale * ratio;
            auto &g = glyph(face, code, raster);
            if (g.w && g.h) {
                auto model = glm::translate(
                    origin, glm::vec3(x + (g.x + g.w * .5f) * ratio, y + (g.y + g.h * .5f) * ratio, 0));
                model = glm::scale(model, glm::vec3(g.w * ratio, g.h * ratio, 1));
                quad(model, projection, e.color, pages.at(g.page).texture, g.uv);
            }
            x += g.advance * scale * ratio;
            previousCode = code;
            previousFace = face;
        }
    }
    std::string modelKey(const std::string& name){return proceduralName(name)?name:config->asset("models",name).u8string();}
    void dropModel(const std::string& key){auto found=models.find(key);if(found==models.end())return;for(size_t i=0;i<found->second->parts.size();++i){auto id="model:"+key+":"+std::to_string(i);auto mesh=meshes.find(id);if(mesh!=meshes.end()){gl::DeleteBuffers(1,&mesh->second.vbo);gl::DeleteVertexArrays(1,&mesh->second.vao);gpuBytes-=found->second->parts[i].vertices.size()*sizeof(Vertex);meshes.erase(mesh);}}models.erase(found);modelRevisions.erase(key);}
    Model &prepareModel(const std::string &name) {
        auto key=modelKey(name);std::shared_ptr<Model> source;unsigned long long revision=0;
        if(proceduralName(name)){auto& store=geometry(context->world);auto found=store.entries.find(name);if(found==store.entries.end())throw std::runtime_error("Missing procedural mesh: "+name);source=found->second.model;revision=found->second.revision;}
        auto found=models.find(key);
        if(found!=models.end() && (modelRevisions[key]!=revision || (source && found->second!=source))){
            // Reserve the replacement before releasing the working GPU mesh.
            size_t needed=0,previous=0;for(auto& part:source->parts)needed+=part.vertices.size()*sizeof(Vertex);for(auto& part:found->second->parts)previous+=part.vertices.size()*sizeof(Vertex);
            if(needed>gpuLimit || gpuBytes-previous>gpuLimit-needed)throw std::runtime_error("Geometry GPU budget exceeded");dropModel(key);found=models.end();
        }
        if(found==models.end()){
            if(!source)source=context->assets.model(config->asset("models",name));
            size_t bytes=0;for(auto& part:source->parts)bytes+=part.vertices.size()*sizeof(Vertex);reserve(bytes);
            for(size_t i=0;i<source->parts.size();++i)meshes["model:"+key+":"+std::to_string(i)]=upload(source->parts[i].vertices);
            found=models.emplace(key,source).first;modelRevisions[key]=revision;
        }
        return *found->second;
    }

    unsigned modelTexture(const Model::Part &part, const std::string &key, size_t index) {
        if (part.embedded) {
            auto name = "embedded:" + key + ":" + std::to_string(index);
            auto cached = textures.find(name);
            unsigned result;
            if (cached == textures.end()) {
                reserve(textureStorage(part.embedded->width,part.embedded->height));
                result = texture(part.embedded->pixels.data(), part.embedded->width, part.embedded->height);
                filterTexture();textures[name] = result;
                textureBytes[name] = textureStorage(part.embedded->width,part.embedded->height);
            } else
                result = cached->second;
            textureFrames[name] = frame;
            return result;
        }
        return part.texture.empty() ? 0 : imagePath(part.texture);
    }
    void model(const Entity &e, const glm::mat4 &view, bool lit) {
        auto key = modelKey(e.model);
        auto &asset = prepareModel(e.model);
        auto pose = asset.pose(e.animation, e.animationTime, e.animationLoop);
        auto base = modelMatrix(e);
        for (size_t i = 0; i < asset.parts.size(); ++i) {
            auto &part = asset.parts[i];
            auto transform = base;
            std::vector<glm::mat4> bones;
            if (part.bones.empty())
                transform *= asset.rootInverse * pose[part.node];
            else
                for (auto &bone : part.bones)
                    bones.push_back(asset.rootInverse * pose[bone.node] * bone.offset);
            auto saved=materialOptions;materialPart=&part;if(e.materialData.empty())materialOptions=part.material;
            unsigned tex = 0;
            if (!shadowPass || materialOptions.value("alpha_mode","blend")=="mask")
                tex = !e.texture.empty() ? image(e.texture) : !materialOptions.value("albedo_texture","").empty()?image(materialOptions["albedo_texture"]):modelTexture(part, key, i);
            draw(meshes.at("model:" + key + ":" + std::to_string(i)), transform, view, e.color * part.color,
                 tex, lit, &bones);materialOptions=std::move(saved);materialPart=nullptr;
        }
    }
    void particles(World &world,const glm::mat4 &view,const glm::mat4 &ortho,unsigned layers,bool screen){
        if(!world.particles || shadowPass)return;
        auto result=particleRenderer->draw(particleSnapshot,screen?ortho:view,world.is3d,screen,layers,
            particleProgram,whiteTexture,[&](const std::string& name){return image(name);},[&](size_t bytes){reserve(bytes);});
        particleCalls+=result.calls;drawCalls+=result.calls;particleQuads+=result.quads;
        triangles+=result.quads*2;particleUpload+=result.uploadBytes;particleSortMs+=result.sortMs;
    }
    void scene(World &world, const glm::mat4 &view, int width, int height, int logicalWidth,
               int logicalHeight, bool includeUI, unsigned layers, const std::string &currentTarget = "") {
        auto ortho = glm::ortho(0.f, float(logicalWidth), float(logicalHeight), 0.f, -10000.f, 10000.f);
        auto entities = world.entities;
        bool stateSet = false, previousClip = false, previousDepth = false, particlesDrawn = false;
        glm::vec4 previousRect{0};
        auto blended=[&](const Entity& e){if(!e.alive || !e.visible || !world.is3d || e.screen)return false;if(e.materialData.contains("alpha_mode"))return e.materialData["alpha_mode"]=="blend";if(e.color.a<.999f)return true;if(e.kind=="mesh"){auto& model=prepareModel(e.model);for(auto& p:model.parts)if(p.material.value("alpha_mode","opaque")=="blend")return true;}return false;};
        std::stable_sort(entities.begin(), entities.end(), [&](auto &a, auto &b) {
            if (a->screen != b->screen)
                return !a->screen;
            if(!a->screen && world.is3d){bool aa=blended(*a),bb=blended(*b);if(aa!=bb)return !aa;if(aa){auto p=view*glm::vec4(world.worldPosition(*a),1),q=view*glm::vec4(world.worldPosition(*b),1);return p.z/std::max(std::abs(p.w),1e-8f)>q.z/std::max(std::abs(q.w),1e-8f);}}
            return (a->screen || !world.is3d) && a->position.z < b->position.z;
        });
        for (auto &pointer : entities) {
            auto &e = *pointer;
            if (!e.alive || !e.visible || e.kind == "empty" || (!includeUI && e.screen) ||
                !(e.layer & layers) || (shadowPass && (e.screen || !e.castsShadow || e.kind == "text")) ||
                (shadowPass && blended(e)) ||
                (!currentTarget.empty() && e.texture == "@target:" + currentTarget))
                continue;
            if (e.screen && !particlesDrawn && !shadowPass) {
                flush();particles(world,view,ortho,layers,false);particlesDrawn=true;stateSet=false;
            }
            bool blend=!shadowPass && blended(e);
            if(materialOptions!=e.materialData){flush();materialOptions=e.materialData;}
            gl::DepthMask(blend?0:1);
            // Scissor/depth/uniform changes terminate an adjacent compatible batch.
            bool clip = e.clipped && e.screen, depth = shadowPass || (world.is3d && !e.screen);
            if (!stateSet || clip != previousClip || depth != previousDepth ||
                (clip && e.clip != previousRect) || entityUniforms != e.uniforms) {
                flush();
                if (clip) {
                    auto c = e.clip;
                    auto bounded = [](double value, int maximum) {
                        return int(std::clamp(value, 0.0, double(maximum)));
                    };
                    int left = bounded(double(c.x) * width / logicalWidth, width),
                        right = bounded((double(c.x) + c.z) * width / logicalWidth, width),
                        top = bounded(double(c.y) * height / logicalHeight, height),
                        bottom = bounded((double(c.y) + c.w) * height / logicalHeight, height);
                    gl::Enable(gl::SCISSOR_TEST);
                    gl::Scissor(left, height - bottom, std::max(0, right - left), std::max(0, bottom - top));
                } else
                    gl::Disable(gl::SCISSOR_TEST);
                if (depth)
                    gl::Enable(gl::DEPTH_TEST);
                else
                    gl::Disable(gl::DEPTH_TEST);
                entityUniforms = e.uniforms;
                previousClip = clip;
                previousDepth = depth;
                previousRect = e.clip;
                stateSet = true;
            }
            drawUV = e.uv;
            drawFlip = e.texture.rfind("@target:", 0) == 0;
            auto projection = e.screen ? ortho : view;
            if (e.kind == "text") {
                text(e, projection);
                continue;
            }
            if (e.scale.x == 0 || e.scale.y == 0 || e.scale.z == 0)
                throw std::runtime_error("Drawable scale cannot be zero: " + e.id);
            if (e.kind == "mesh") {
                flush();
                model(e, projection, world.is3d && !e.screen);
                continue;
            }
            auto matrix = modelMatrix(e);
            auto tex = shadowPass && materialOptions.value("alpha_mode","blend")!="mask" ? 0 : image(!e.texture.empty()?e.texture:materialOptions.value("albedo_texture",""));
            if (!shadowPass && e.materialData.empty() && e.kind == "sprite" && (!world.is3d || e.screen))
                quad(matrix, projection, e.color, tex, e.uv, e.texture.rfind("@target:", 0) == 0);
            else {
                flush();
                draw(meshes.at(e.kind), matrix, projection, e.color, tex, world.is3d && !e.screen);
            }
        }
        flush();
        if (!particlesDrawn) particles(world,view,ortho,layers,false);
        if (includeUI) particles(world,view,ortho,layers,true);
        gl::DepthMask(1);materialOptions=Json::object();materialPart=nullptr;
        entityUniforms = Json::object();
        drawUV = {0, 0, 1, 1};
        drawFlip = false;
        gl::Disable(gl::SCISSOR_TEST);
    }
    void edit(World &world){editorState.draw(world,*context,keys,buttons,mouseDelta,stats);}
};
Renderer::Renderer() : impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
void Renderer::init(const Config &c, World &world){if(!active)throw std::runtime_error("Renderer needs explicit runtime context");init(c,world,*active);}
void Renderer::init(const Config &c, World &world, Runtime &context) {
    impl->context=&context;
    impl->config = &c;
    glfwSetErrorCallback([](int code, const char *message) {
        logger.write("ERROR", "GLFW " + std::to_string(code) + ": " + message);
    });
    if (!glfwInit())
        throw std::runtime_error("GLFW initialization failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    auto options = c.data.value("window", Json::object());
    world.width = options.value("width", 1280);
    world.height = options.value("height", 720);
    glfwWindowHint(GLFW_RESIZABLE, options.value("resizable", true));
    impl->window =
        glfwCreateWindow(world.width, world.height, c.data["project"]["name"].get<std::string>().c_str(),
                         options.value("fullscreen", false) ? glfwGetPrimaryMonitor() : nullptr, nullptr);
    if (!impl->window) {
        glfwTerminate();
        throw std::runtime_error("Cannot create OpenGL 3.3 window");
    }
    glfwSetWindowUserPointer(impl->window, impl.get());
    glfwSetScrollCallback(impl->window, [](GLFWwindow *window, double x, double y) {
        static_cast<Impl *>(glfwGetWindowUserPointer(window))->mouseScroll += glm::vec2(x, y);
    });
    glfwSetCharCallback(impl->window, [](GLFWwindow *window, unsigned code) {
        static_cast<Impl *>(glfwGetWindowUserPointer(window))->characters.push_back(code);
    });
    glfwMakeContextCurrent(impl->window);
    gl::load();
    impl->vsync = options.value("vsync", true);
    glfwSwapInterval(impl->vsync ? 1 : 0);
#ifndef __APPLE__
    if (c.data["project"].contains("icon")) {
        auto iconPath = c.resolve(c.data["project"]["icon"]).u8string();
        GLFWimage icon{};
        int n;
        icon.pixels = stbi_load(iconPath.c_str(), &icon.width, &icon.height, &n, 4);
        if (!icon.pixels)
            throw std::runtime_error("Cannot decode window icon");
        glfwSetWindowIcon(impl->window, 1, &icon);
        stbi_image_free(icon.pixels);
    }
#endif
    impl->setup();
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    if (impl->context && impl->context->editing)
        editor(true);
}
void Renderer::stage() {
    staged = std::make_unique<Impl>();
    staged->config = impl->config;
    staged->context = impl->context;
    try {
        staged->setup();
    } catch (...) {
        staged.reset();
        throw;
    }
}
void Renderer::commit() {
    if (staged) {
        impl->swapResources(*staged);
        staged.reset();
    }
}
void Renderer::discard() { staged.reset(); }
void Renderer::invalidate() {
    stage();
    commit();
}
void Renderer::checkpointInput() {
    impl->previousWindow = windowOptions();
    impl->previousCaptured = impl->captured;
    impl->previousScreenshot = impl->screenshotPath;
}
void Renderer::rollbackInput() {
    if (!impl->previousWindow.is_null())
        windowOptions(impl->previousWindow);
    capture(impl->previousCaptured);
    impl->screenshotPath = impl->previousScreenshot;
}
void Renderer::validateWorld(const World &world) {
    auto &resources = staged ? *staged : *impl;
    int framebufferWidth, framebufferHeight, width, height;
    glfwGetFramebufferSize(impl->window, &framebufferWidth, &framebufferHeight);
    glfwGetWindowSize(impl->window, &width, &height);
    resources.density = std::max(float(framebufferWidth) / std::max(1, width),
                                 float(framebufferHeight) / std::max(1, height));
    auto targets = world.renderSettings.value("targets", Json::object());
    for (auto it = targets.begin(); it != targets.end(); ++it)
        resources.target("camera:" + it.key(), it.value().value("width", 320),
                         it.value().value("height", 240));
    auto post = world.renderSettings.value("postprocess", Json::object());
    if (!post.empty() && post.value("enabled", true) && framebufferWidth > 0 && framebufferHeight > 0)
        resources.target("main", framebufferWidth, framebufferHeight);
    auto lights = world.renderSettings.value("lights", Json::array());
    if (world.is3d && !lights.empty() && lights[0].value("type", "point") == "directional" &&
        lights[0].value("shadows", false)) {
        int size = world.renderSettings.value("shadow_size", 1024);
        resources.target("shadow", size, size);
    }
    if (world.particles) for (const auto &emitter : world.particles->emitters) resources.image(emitter.settings["texture"]);
    for (auto &pointer : world.entities) {
        auto &e = *pointer;
        if (!e.alive || e.kind == "empty")
            continue;
        if (e.kind == "text") {
            if (!std::isfinite(e.fontSize) || e.fontSize <= 0)
                throw std::runtime_error("Text size must be positive: " + e.id);
            int raster = glyphRasterSize(e.fontSize, textWorldScale(e),
                                         resources.density);
            for (auto code : unicode(e.text))
                if (code != '\n')
                    resources.glyph(fontFor(resources.fontFaces, code), code, raster);
            continue;
        }
        if (e.scale.x == 0 || e.scale.y == 0 || e.scale.z == 0)
            throw std::runtime_error("Drawable scale cannot be zero: " + e.id);
        resources.image(e.texture);for(auto& file:materialTextures(e.materialData))resources.image(file);
        if (e.kind == "mesh") {
            auto &asset = resources.prepareModel(e.model);
            for(auto& part:asset.parts){for(auto& [field,file]:part.maps)resources.imagePath(file);for(auto& [field,pixels]:part.embeddedMaps)resources.embeddedImage(*pixels,"material:"+std::to_string(reinterpret_cast<uintptr_t>(&part))+":"+field);}
            if (!e.animation.empty())
                asset.pose(e.animation, 0, e.animationLoop);
            if (e.texture.empty()) {
                auto key = resources.modelKey(e.model);
                for (size_t i = 0; i < asset.parts.size(); ++i)
                    resources.modelTexture(asset.parts[i], key, i);
            }
        }
    }
}
void Renderer::render(World &world) {
    world.syncTransforms();
    std::vector<std::string> obsolete;for(auto& [key,model]:impl->models)if(proceduralName(key) && !geometry(world).entries.count(key))obsolete.push_back(key);for(auto& key:obsolete)impl->dropModel(key);
    auto started = std::chrono::steady_clock::now();
    int w, h;
    glfwGetFramebufferSize(impl->window, &w, &h);
    glfwGetWindowSize(impl->window, &world.width, &world.height);
    if (w <= 0 || h <= 0 || world.width <= 0 || world.height <= 0)
        return;
    ++impl->frame;
    impl->frameImages.clear();impl->imageResolutions=0;impl->particleUpload=0;impl->particleSortMs=0;
    impl->particleSnapshot=world.particles?world.particles->draw(world):std::vector<ParticleDraw>{};
    impl->drawCalls = impl->batchCalls = impl->triangles = impl->particleCalls = impl->particleQuads = 0;
    impl->density = std::max(float(w) / world.width, float(h) / world.height);
    impl->renderOptions = world.renderSettings;
    auto targets = world.renderSettings.value("targets", Json::object());
    auto lights = world.renderSettings.value("lights", Json::array());
    std::set<std::string> wanted = {"shadow", "main"};
    for (auto it = targets.begin(); it != targets.end(); ++it) {
        wanted.insert("camera:" + it.key());
        impl->target("camera:" + it.key(), it.value().value("width", 320), it.value().value("height", 240));
    }
    for (auto it = impl->targets.begin(); it != impl->targets.end();)
        if (!wanted.count(it->first)) {
            impl->freeTarget(it->second);
            it = impl->targets.erase(it);
        } else
            ++it;
    impl->shadowEnabled = world.is3d && !lights.empty() &&
                          lights[0].value("type", "point") == "directional" &&
                          lights[0].value("shadows", false);
    if (impl->shadowEnabled) {
        auto &light = lights[0];
        int size = world.renderSettings.value("shadow_size", 1024);
        if (size < 64 || size > 4096)
            throw std::runtime_error("shadow_size must be 64..4096");
        auto &target = impl->target("shadow", size, size);
        auto v = light.value("direction", Json::array({0, -1, 0}));
        glm::vec3 direction(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
        direction = glm::normalize(direction);
        float range = light.value("shadow_extent", 20.f);
        if (!std::isfinite(range) || range < .1f || range > 10000)
            throw std::runtime_error("shadow_extent must be .1..10000");
        auto center = world.cameraTarget;
        auto up = std::abs(direction.y) > .99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        impl->shadowMatrix = glm::ortho(-range, range, -range, range, .01f, range * 4) *
                             glm::lookAt(center - direction * range * 2.f, center, up);
        gl::BindFramebuffer(gl::FRAMEBUFFER, target.fbo);
        gl::Viewport(0, 0, size, size);
        gl::Disable(gl::SCISSOR_TEST);
        gl::ClearColor(1, 1, 1, 1);
        gl::Clear(gl::COLOR | gl::DEPTH);
        impl->shadowPass = true;
        impl->scene(world, impl->shadowMatrix, size, size, size, size, false, ~0u);
        impl->shadowPass = false;
    }
    auto background = world.background;
    for (auto it = targets.begin(); it != targets.end(); ++it) {
        auto &options = it.value();
        auto &target = impl->targets.at("camera:" + it.key());
        int width = target.width, height = target.height;
        gl::BindFramebuffer(gl::FRAMEBUFFER, target.fbo);
        gl::Viewport(0, 0, width, height);
        gl::ClearColor(background.r, background.g, background.b, background.a);
        gl::Clear(gl::COLOR | gl::DEPTH);
        auto p = options.value("position", Json::array({0, 0, 5})),
             t = options.value("target", Json::array({0, 0, 0}));
        glm::vec3 position(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()),
            destination(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());
        auto up = glm::length(glm::cross(destination - position, glm::vec3(0, 1, 0))) < 1e-6f
                      ? glm::vec3(0, 0, 1)
                      : glm::vec3(0, 1, 0);
        auto view = options.value("mode", "3d") == "3d"
                        ? glm::perspective(glm::radians(options.value("fov", 60.f)), float(width) / height,
                                           .05f, 10000.f) *
                              glm::lookAt(position, destination, up)
                        : glm::ortho(0.f, float(width), float(height), 0.f, -10000.f, 10000.f) *
                              glm::translate(glm::mat4(1), -position);
        bool originalMode = world.is3d;
        world.is3d = options.value("mode", "3d") == "3d";
        impl->cameraPosition = position;
        try {
            impl->scene(world, view, width, height, width, height, options.value("include_ui", false),
                        options.value("layers", ~0u), it.key());
        } catch (...) {
            world.is3d = originalMode;
            throw;
        }
        world.is3d = originalMode;
    }
    auto post = world.renderSettings.value("postprocess", Json::object());
    bool processing = !post.empty() && post.value("enabled", true);
    unsigned mainTexture = 0, mainFbo = 0;
    if (processing) {
        auto &main = impl->target("main", w, h);
        mainTexture = main.color;
        mainFbo = main.fbo;
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, mainFbo);
    gl::Viewport(0, 0, w, h);
    gl::Disable(gl::SCISSOR_TEST);
    gl::ClearColor(background.r, background.g, background.b, background.a);
    gl::Clear(gl::COLOR | gl::DEPTH);
    auto ortho = glm::ortho(0.f, float(world.width), float(world.height), 0.f, -10000.f, 10000.f);
    auto up = glm::length(glm::cross(world.cameraTarget - world.cameraPosition, glm::vec3(0, 1, 0))) < 1e-6f
                  ? glm::vec3(0, 0, 1)
                  : glm::vec3(0, 1, 0);
    auto view = world.is3d ? glm::perspective(glm::radians(world.fov), float(w) / h, .05f, 10000.f) *
                                 glm::lookAt(world.cameraPosition, world.cameraTarget, up)
                           : ortho * glm::translate(glm::mat4(1), glm::vec3(-world.cameraPosition.x,
                                                                            -world.cameraPosition.y, 0));
    impl->cameraPosition = world.cameraPosition;
    impl->scene(world, view, w, h, world.width, world.height, true, ~0u);
    if (processing) {
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        gl::Viewport(0, 0, w, h);
        gl::Disable(gl::DEPTH_TEST);
        gl::UseProgram(impl->postProgram);
        for (auto field : {"grain", "bloom", "aberration", "scanlines", "vignette", "fade", "gamma"})
            gl::Uniform1f(gl::GetUniformLocation(impl->postProgram, (std::string("u_") + field).c_str()),
                          post.value(field, std::string(field) == "gamma" ? 1.f : 0.f));
        glm::vec2 resolution(w, h);
        gl::Uniform2fv(gl::GetUniformLocation(impl->postProgram, "u_resolution"), 1,
                       glm::value_ptr(resolution));
        uniforms(impl->postProgram, post.value("uniforms", Json::object()));
        // Framebuffer textures have bottom-left origin. The full-screen quad reverses Y.
        auto matrix = glm::translate(glm::mat4(1), glm::vec3(world.width * .5f, world.height * .5f, 0));
        matrix = glm::scale(matrix, glm::vec3(world.width, -world.height, 1));
        impl->draw(impl->meshes.at("sprite"), matrix, ortho, glm::vec4(1), mainTexture, false, nullptr,
                   impl->postProgram);
    }
    if (impl->editorEnabled)
        impl->edit(world);
    if (!impl->screenshotPath.empty()) {
        std::vector<unsigned char> pixels(size_t(w) * h * 3);
        gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, w, h, gl::RGB, gl::UNSIGNED_BYTE, pixels.data());
        auto path = impl->screenshotPath;
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot save screenshot: " + path.u8string());
        file << "P6\n" << w << " " << h << "\n255\n";
        for (int y = h - 1; y >= 0; --y)
            file.write(reinterpret_cast<char *>(pixels.data() + size_t(y) * w * 3), w * 3);
        if (!file)
            throw std::runtime_error("Screenshot write failed");
        impl->screenshotPath.clear();
        logger.write("INFO", "Screenshot saved: " + path.u8string());
    }
    impl->stats = {
        {"draw_calls", impl->drawCalls},
        {"particle_draw_calls",impl->particleCalls},
        {"particle_quads",impl->particleQuads},
        {"particle_instancing",impl->particleInstanced},
        {"particle_upload_bytes",impl->particleUpload},
        {"particle_sort_ms",impl->particleSortMs},
        {"texture_path_resolutions",impl->imageResolutions},
        {"batches", impl->batchCalls},
        {"triangles", impl->triangles},
        {"gpu_bytes", impl->gpuBytes},
        {"gpu_budget_bytes", impl->gpuLimit},
        {"glyphs", impl->glyphs.size()},
        {"glyph_pages", impl->pages.size()},
        {"render_targets", impl->targets.size()},
        {"render_ms",
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()}};
    glfwSwapBuffers(impl->window);
}
void Renderer::poll() {
    impl->previous = impl->keys;
    impl->previousButtons = impl->buttons;
    impl->mouseScroll = {0, 0};
    impl->characters.clear();
    glfwPollEvents();
    for (int i = GLFW_KEY_SPACE; i <= GLFW_KEY_LAST; ++i)
        impl->keys[i] = glfwGetKey(impl->window, i) == GLFW_PRESS;
    for (int i = 0; i < 8; ++i)
        impl->buttons[i] = glfwGetMouseButton(impl->window, i) == GLFW_PRESS;
    double x, y;
    glfwGetCursorPos(impl->window, &x, &y);
    auto position = glm::vec2(x, y);
    impl->mouseDelta = impl->firstMouse ? glm::vec2(0) : position - impl->mousePosition;
    impl->firstMouse = false;
    impl->mousePosition = position;
}
bool Renderer::closing() const { return glfwWindowShouldClose(impl->window); }
bool Renderer::key(const std::string &s) const { return impl->keys.at(keyCode(s)); }
bool Renderer::pressed(const std::string &s) const {
    auto k = keyCode(s);
    return impl->keys[k] && !impl->previous[k];
}
bool Renderer::mouse(int button) const {
    if (button < 0 || button > 7)
        throw std::runtime_error("Mouse button must be 0..7");
    return impl->buttons[button];
}
glm::vec2 Renderer::cursor() const { return impl->mousePosition; }
glm::vec2 Renderer::delta() const { return impl->mouseDelta; }
glm::vec2 Renderer::scroll() const { return impl->mouseScroll; }
void Renderer::capture(bool enabled) {
    if (impl->captured == enabled)
        return;
    impl->captured = enabled;
    glfwSetInputMode(impl->window, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    impl->firstMouse = true;
}
void Renderer::quit() { glfwSetWindowShouldClose(impl->window, 1); }
void Renderer::screenshot(const fs::path &path) { impl->screenshotPath = path; }
Json Renderer::diagnostics() const { return impl->stats; }
Json Renderer::input() const {
    Json keys = Json::array(), pressed = Json::array(), released = Json::array(), buttons = Json::array(),
         events = Json::array(), pads = Json::array();
    std::map<int, std::string> names = {{GLFW_KEY_ESCAPE, "ESCAPE"},
                                        {GLFW_KEY_ENTER, "ENTER"},
                                        {GLFW_KEY_TAB, "TAB"},
                                        {GLFW_KEY_BACKSPACE, "BACKSPACE"},
                                        {GLFW_KEY_INSERT, "INSERT"},
                                        {GLFW_KEY_DELETE, "DELETE"},
                                        {GLFW_KEY_PAGE_UP, "PAGEUP"},
                                        {GLFW_KEY_PAGE_DOWN, "PAGEDOWN"},
                                        {GLFW_KEY_HOME, "HOME"},
                                        {GLFW_KEY_END, "END"},
                                        {GLFW_KEY_LEFT, "LEFT"},
                                        {GLFW_KEY_RIGHT, "RIGHT"},
                                        {GLFW_KEY_UP, "UP"},
                                        {GLFW_KEY_DOWN, "DOWN"},
                                        {GLFW_KEY_LEFT_SHIFT, "SHIFT"},
                                        {GLFW_KEY_LEFT_CONTROL, "CTRL"},
                                        {GLFW_KEY_LEFT_ALT, "ALT"},
                                        {GLFW_KEY_LEFT_SUPER, "SUPER"},
                                        {GLFW_KEY_SPACE, "SPACE"}};
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k) {
        std::string name;
        if (names.count(k))
            name = names.at(k);
        else if (k >= GLFW_KEY_F1 && k <= GLFW_KEY_F25)
            name = "F" + std::to_string(k - GLFW_KEY_F1 + 1);
        else if (k >= 32 && k <= 96)
            name = std::string(1, char(k));
        else
            continue;
        bool down = impl->keys[k], before = impl->previous[k];
        if (k == GLFW_KEY_LEFT_SHIFT) {
            down |= impl->keys[GLFW_KEY_RIGHT_SHIFT];
            before |= impl->previous[GLFW_KEY_RIGHT_SHIFT];
        }
        if (k == GLFW_KEY_LEFT_CONTROL) {
            down |= impl->keys[GLFW_KEY_RIGHT_CONTROL];
            before |= impl->previous[GLFW_KEY_RIGHT_CONTROL];
        }
        if (k == GLFW_KEY_LEFT_ALT) {
            down |= impl->keys[GLFW_KEY_RIGHT_ALT];
            before |= impl->previous[GLFW_KEY_RIGHT_ALT];
        }
        if (down)
            keys.push_back(name);
        if (down && !before) {
            pressed.push_back(name);
            events.push_back({{"type", "key"}, {"key", name}, {"action", "press"}});
        }
        if (!down && before) {
            released.push_back(name);
            events.push_back({{"type", "key"}, {"key", name}, {"action", "release"}});
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (impl->buttons[i])
            buttons.push_back(i);
        if (impl->buttons[i] != impl->previousButtons[i])
            events.push_back({{"type", "mouse_button"},
                              {"button", i},
                              {"action", impl->buttons[i] ? "press" : "release"}});
    }
    for (auto code : impl->characters)
        events.push_back({{"type", "text"}, {"codepoint", code}});
    if (impl->mouseScroll != glm::vec2(0))
        events.push_back({{"type", "scroll"}, {"value", {impl->mouseScroll.x, impl->mouseScroll.y}}});
    for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id) {
        GLFWgamepadstate state{};
        if (glfwGetGamepadState(id, &state)) {
            Json values = Json::array(), axes = Json::array();
            for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b)
                if (state.buttons[b] == GLFW_PRESS)
                    values.push_back(b);
            for (float value : state.axes)
                axes.push_back(value);
            pads.push_back(
                {{"id", id}, {"name", glfwGetGamepadName(id)}, {"buttons", values}, {"axes", axes}});
        }
    }
    return {{"keys", keys},
            {"pressed", pressed},
            {"released", released},
            {"buttons", buttons},
            {"position", {impl->mousePosition.x, impl->mousePosition.y}},
            {"delta", {impl->mouseDelta.x, impl->mouseDelta.y}},
            {"scroll", {impl->mouseScroll.x, impl->mouseScroll.y}},
            {"events", events},
            {"gamepads", pads}};
}
Json Renderer::windowOptions() const {
    int w, h;
    glfwGetWindowSize(impl->window, &w, &h);
    return {{"width", w},
            {"height", h},
            {"fullscreen", glfwGetWindowMonitor(impl->window) != nullptr},
            {"vsync", impl->vsync}};
}
void Renderer::windowOptions(const Json &options) {
    auto current = windowOptions();
    current.merge_patch(options);
    int width = current.value("width", 1280), height = current.value("height", 720);
    if (width < 320 || height < 240 || width > 16384 || height > 16384)
        throw std::runtime_error("Window dimensions must be 320..16384 by 240..16384");
    bool fullscreen = current.value("fullscreen", false);
    if (fullscreen) {
        auto monitor = glfwGetPrimaryMonitor();
        auto mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(impl->window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else
        glfwSetWindowMonitor(impl->window, nullptr, 100, 100, width, height, GLFW_DONT_CARE);
    impl->vsync = current.value("vsync", true);
    glfwSwapInterval(impl->vsync ? 1 : 0);
}
bool Renderer::previewing() const { return impl->editorState.preview; }
void Renderer::editor(bool enabled) {
    impl->editorEnabled = enabled;
    if (!enabled)
        return;
    if (!impl->editorContext) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.Fonts->AddFontFromFileTTF(impl->fontFaces.front()->name.c_str(), 18, nullptr,
                                     io.Fonts->GetGlyphRangesCyrillic());
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(impl->window, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");
        impl->editorContext = true;
    }
}
} // namespace forge
