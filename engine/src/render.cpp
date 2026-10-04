#include <chrono>
#include <forge/gl.hpp>
#include <forge/model.hpp>
#include <forge/particles.hpp>
#include <forge/shaders.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <tuple>
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_TRUETYPE_IMPLEMENTATION
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
using Vertex = ModelVertex;
struct Mesh {
    unsigned vao = 0, vbo = 0;
    int count = 0;
};
Mesh upload(const std::vector<Vertex> &vertices) {
    Mesh m;
    m.count = static_cast<int>(vertices.size());
    gl::GenVertexArrays(1, &m.vao);
    gl::GenBuffers(1, &m.vbo);
    gl::BindVertexArray(m.vao);
    gl::BindBuffer(gl::ARRAY_BUFFER, m.vbo);
    gl::BufferData(gl::ARRAY_BUFFER, sizeof(Vertex) * vertices.size(), vertices.data(), gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, p)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, uv)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 3, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, n)));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribIPointer(3, 4, 0x1404, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, bones)));
    gl::EnableVertexAttribArray(4);
    gl::VertexAttribPointer(4, 4, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, weights)));
    return m;
}
unsigned compile(unsigned kind, const std::string &source, const std::string &name) {
    auto shader = gl::CreateShader(kind);
    const char *s = source.c_str();
    gl::ShaderSource(shader, 1, &s, nullptr);
    gl::CompileShader(shader);
    int ok;
    gl::GetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192]{};
        gl::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        gl::DeleteShader(shader);
        throw std::runtime_error("Shader " + name + ":\n" + log);
    }
    return shader;
}
unsigned texture(const unsigned char *data, int width, int height) {
    unsigned id;
    gl::GenTextures(1, &id);
    gl::BindTexture(gl::TEXTURE_2D, id);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA, width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE, data);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    return id;
}
std::vector<Vertex> obj(const fs::path &file) {
    std::istringstream in(textFile(file));
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::vector<Vertex> out;
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::istringstream s(line);
        std::string type;
        s >> type;
        if (type == "v") {
            glm::vec3 v;
            if (!(s >> v.x >> v.y >> v.z))
                throw std::runtime_error("Invalid OBJ vertex: " + file.u8string());
            positions.push_back(v);
        } else if (type == "vn") {
            glm::vec3 v;
            if (!(s >> v.x >> v.y >> v.z))
                throw std::runtime_error("Invalid OBJ normal");
            normals.push_back(v);
        } else if (type == "vt") {
            glm::vec2 v;
            if (!(s >> v.x >> v.y))
                throw std::runtime_error("Invalid OBJ UV");
            v.y = 1 - v.y;
            uvs.push_back(v);
        } else if (type == "f") {
            std::vector<Vertex> face;
            std::string token;
            auto index = [](int i, size_t size) {
                int result = i > 0 ? i - 1 : static_cast<int>(size) + i;
                if (i == 0 || result < 0 || result >= static_cast<int>(size))
                    throw std::runtime_error("OBJ index out of range");
                return result;
            };
            while (s >> token) {
                if (token[0] == '#')
                    break;
                std::array<int, 3> ids{};
                size_t start = 0;
                int component = 0;
                while (start <= token.size() && component < 3) {
                    auto end = token.find('/', start);
                    auto part = token.substr(start, end == std::string::npos ? end : end - start);
                    if (!part.empty())
                        ids[component] = std::stoi(part);
                    ++component;
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                Vertex v;
                v.p = positions.at(index(ids[0], positions.size()));
                v.uv = ids[1] ? uvs.at(index(ids[1], uvs.size())) : glm::vec2(0);
                v.n = ids[2] ? normals.at(index(ids[2], normals.size())) : glm::vec3(0);
                face.push_back(v);
            }
            if (face.size() < 3)
                throw std::runtime_error("OBJ face needs 3 vertices at line " + std::to_string(lineNumber));
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                Vertex tri[] = {face[0], face[i], face[i + 1]};
                auto normal = glm::cross(tri[1].p - tri[0].p, tri[2].p - tri[0].p);
                normal = glm::length(normal) > 0 ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                for (auto &v : tri) {
                    if (glm::length(v.n) == 0)
                        v.n = normal;
                    out.push_back(v);
                }
            }
        }
    }
    if (out.empty())
        throw std::runtime_error("OBJ contains no faces: " + file.u8string());
    return out;
}
std::vector<int> unicode(const std::string &s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i++];
        int code = c, count = 0;
        if (c >= 0xf0) {
            code = c & 7;
            count = 3;
        } else if (c >= 0xe0) {
            code = c & 15;
            count = 2;
        } else if (c >= 0xc0) {
            code = c & 31;
            count = 1;
        }
        for (int k = 0; k < count && i < s.size(); ++k)
            code = (code << 6) | (static_cast<unsigned char>(s[i++]) & 63);
        out.push_back(code);
    }
    return out;
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
struct FontFace {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo font{};
    std::string name;
};
std::vector<std::shared_ptr<FontFace>> fonts(const Config &config) {
    static std::map<std::string, std::shared_ptr<FontFace>> cache;
    auto options = config.data.value("renderer", Json::object());
    Json names = Json::array({options.value("font", std::string("font.ttf"))});
    for (auto &name : options.value("fallback_fonts", Json::array()))
        names.push_back(name);
    std::vector<std::shared_ptr<FontFace>> result;
    for (auto &name : names) {
        auto path = config.asset("graphics", name);
        auto key =
            path.u8string() +
            std::to_string(static_cast<long long>(fs::last_write_time(path).time_since_epoch().count()));
        auto found = cache.find(key);
        if (found == cache.end()) {
            auto face = std::make_shared<FontFace>();
            auto data = textFile(path);
            face->bytes.assign(data.begin(), data.end());
            face->name = path.u8string();
            if (!stbtt_InitFont(&face->font, face->bytes.data(),
                                stbtt_GetFontOffsetForIndex(face->bytes.data(), 0)))
                throw std::runtime_error("Invalid TrueType font: " + path.u8string());
            if (cache.size() >= 32)
                cache.erase(cache.begin());
            found = cache.emplace(key, face).first;
        }
        result.push_back(found->second);
    }
    return result;
}
int fontFor(const std::vector<std::shared_ptr<FontFace>> &faces, int code) {
    for (size_t i = 0; i < faces.size(); ++i)
        if (stbtt_FindGlyphIndex(&faces[i]->font, code))
            return int(i);
    static std::set<std::pair<std::string, int>> warned;
    if (warned.insert({faces.front()->name, code}).second) {
        std::ostringstream number;
        number << std::hex << std::uppercase << code;
        logger.write("WARN", "Missing glyph U+" + number.str() + " in configured fonts");
    }
    return 0;
}
glm::mat4 modelMatrix(const Entity &e) {
    auto m = glm::translate(glm::mat4(1), e.position);
    m = glm::rotate(m, glm::radians(e.rotation.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(e.rotation.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(e.rotation.z), glm::vec3(0, 0, 1));
    return glm::scale(m, e.scale);
}
unsigned linkProgram(const std::string &vertex, const std::string &fragment, const std::string &name) {
    auto vs = compile(gl::VERTEX_SHADER, vertex, name + ".vertex");
    unsigned fs = 0, p = 0;
    try {
        fs = compile(gl::FRAGMENT_SHADER, fragment, name + ".fragment");
        p = gl::CreateProgram();
        gl::AttachShader(p, vs);
        gl::AttachShader(p, fs);
        gl::LinkProgram(p);
        int ok;
        gl::GetProgramiv(p, gl::LINK_STATUS, &ok);
        if (!ok) {
            char log[8192]{};
            gl::GetProgramInfoLog(p, sizeof(log), nullptr, log);
            throw std::runtime_error(name + " linking: " + log);
        }
    } catch (...) {
        gl::DeleteShader(vs);
        if (fs)
            gl::DeleteShader(fs);
        if (p)
            gl::DeleteProgram(p);
        throw;
    }
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    return p;
}
void uniforms(unsigned program, const Json &values) {
    for (auto it = values.begin(); it != values.end(); ++it) {
        auto location = gl::GetUniformLocation(program, it.key().c_str());
        auto value = it.value();
        if (value.is_boolean() || value.is_number_integer())
            gl::Uniform1i(location, value.is_boolean() ? int(value.get<bool>()) : value.get<int>());
        else if (value.is_array()) {
            float v[4]{};
            for (size_t i = 0; i < value.size(); ++i)
                v[i] = finiteNumber(value[i], it.key());
            switch (value.size()) {
            case 1:
                gl::Uniform1f(location, v[0]);
                break;
            case 2:
                gl::Uniform2fv(location, 1, v);
                break;
            case 3:
                gl::Uniform3fv(location, 1, v);
                break;
            case 4:
                gl::Uniform4fv(location, 1, v);
                break;
            default:
                throw std::runtime_error("Invalid shader uniform");
            }
        } else
            gl::Uniform1f(location, finiteNumber(value, it.key()));
    }
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
    size_t particleAllocated = 0;
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
    bool editorEnabled = false, editorContext = false, preview = false;
    std::string selected, status;
    std::vector<Json> undo, redo;
    char sceneFile[512] = "editor-scene.json";
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
        models.clear();
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
        models.swap(other.models);
        std::swap(batchAllocated, other.batchAllocated);
        std::swap(particleAllocated, other.particleAllocated);
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
        shadowProgram = linkProgram(vertexDefault, "#version 330 core\nvoid main(){}", "Shadow shader");
        auto particleShader = [&](const char* field, const char* file, const char* fallback) {
            auto path = config->asset("graphics", options.value(field, std::string(file)));
            return options.contains(field) || fs::is_regular_file(path) ? textFile(path) : std::string(fallback);
        };
        particleProgram = linkProgram(particleShader("particle_vertex_shader", "particle.vert", particleVertexDefault),
                                      particleShader("particle_fragment_shader", "particle.frag", particleFragmentDefault), "Particle shader");
        meshes["sprite"] = upload({{{-.5f, -.5f, 0}, {0, 0}, {0, 0, 1}},
                                   {{.5f, -.5f, 0}, {1, 0}, {0, 0, 1}},
                                   {{.5f, .5f, 0}, {1, 1}, {0, 0, 1}},
                                   {{-.5f, -.5f, 0}, {0, 0}, {0, 0, 1}},
                                   {{.5f, .5f, 0}, {1, 1}, {0, 0, 1}},
                                   {{-.5f, .5f, 0}, {0, 1}, {0, 0, 1}}});
        meshes["batch"] = upload({});
        meshes["particles"] = upload({});
        gl::BindVertexArray(meshes["particles"].vao);gl::DisableVertexAttribArray(3);gl::DisableVertexAttribArray(4);
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
    unsigned imagePath(const fs::path &path) {
        auto name = path.u8string();
        auto found = textures.find(name);
        if (found != textures.end()) {
            textureFrames[name] = frame;
            return found->second;
        }
        auto pixels = active->assets.image(path);
        reserve(pixels->pixels.size());
        auto id = texture(pixels->pixels.data(), pixels->width, pixels->height);
        textures[name] = id;
        textureBytes[name] = pixels->pixels.size();
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
        return imagePath(config->asset("textures", name));
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
        gl::Uniform4fv(gl::GetUniformLocation(p, "u_color"), 1, glm::value_ptr(color));
        gl::Uniform1i(gl::GetUniformLocation(p, "u_textured"), tex ? 1 : 0);
        gl::Uniform1f(gl::GetUniformLocation(p, "u_lit"), lit ? 1 : 0);
        gl::Uniform1f(gl::GetUniformLocation(p, "u_time"), active ? float(active->time) : 0);
        glm::vec2 resolution(active ? active->world.width : 1280, active ? active->world.height : 720);
        gl::Uniform2fv(gl::GetUniformLocation(p, "u_resolution"), 1, glm::value_ptr(resolution));
        if (!shadowPass && !overrideProgram) {
            lighting(p);
            uniforms(p, renderOptions.value("uniforms", Json::object()));
            uniforms(p, entityUniforms);
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
        int raster = glyphRasterSize(e.fontSize, std::max(std::abs(e.scale.x), std::abs(e.scale.y)), density);
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
    Model &prepareModel(const std::string &name) {
        auto file = config->asset("models", name);
        auto key = file.u8string();
        auto found = models.find(key);
        if (found == models.end()) {
            auto asset = active->assets.model(file);
            found = models.emplace(key, asset).first;
            for (size_t i = 0; i < asset->parts.size(); ++i) {
                reserve(asset->parts[i].vertices.size() * sizeof(Vertex));
                meshes["model:" + key + ":" + std::to_string(i)] = upload(asset->parts[i].vertices);
            }
        }
        return *found->second;
    }

    unsigned modelTexture(const Model::Part &part, const std::string &key, size_t index) {
        if (part.embedded) {
            auto name = "embedded:" + key + ":" + std::to_string(index);
            auto cached = textures.find(name);
            unsigned result;
            if (cached == textures.end()) {
                reserve(part.embedded->pixels.size());
                result = texture(part.embedded->pixels.data(), part.embedded->width, part.embedded->height);
                textures[name] = result;
                textureBytes[name] = part.embedded->pixels.size();
            } else
                result = cached->second;
            textureFrames[name] = frame;
            return result;
        }
        return part.texture.empty() ? 0 : imagePath(part.texture);
    }
    void model(const Entity &e, const glm::mat4 &view, bool lit) {
        auto key = config->asset("models", e.model).u8string();
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
            unsigned tex = 0;
            if (!shadowPass)
                tex = !e.texture.empty() ? image(e.texture) : modelTexture(part, key, i);
            draw(meshes.at("model:" + key + ":" + std::to_string(i)), transform, view, e.color * part.color,
                 tex, lit, &bones);
        }
    }
    void particles(World &world, const glm::mat4 &view, const glm::mat4 &ortho, unsigned layers, bool screen) {
        if (!world.particles || shadowPass) return;
        auto particles = world.particles->draw(world);
        particles.erase(std::remove_if(particles.begin(), particles.end(), [&](const ParticleDraw &p) {
            return p.screen != screen || !(p.layer & layers) || p.size <= 0 || p.color.a <= 0;
        }), particles.end());
        if (particles.empty()) return;
        const auto projection = screen ? ortho : view;
        auto depth = [&](const ParticleDraw &p) {
            auto clip = projection * glm::vec4(p.position, 1);
            return world.is3d && !screen && std::abs(clip.w) > 1e-8 ? -clip.z / clip.w : p.position.z;
        };
        std::stable_sort(particles.begin(), particles.end(), [&](const auto &a, const auto &b) {
            if (a.additive != b.additive) return !a.additive;
            if (a.additive && a.texture != b.texture) return a.texture < b.texture;
            return depth(a) < depth(b);
        });
        glm::vec3 right(1,0,0), up(0,1,0);
        if (world.is3d && !screen) {
            auto inverse = glm::inverse(projection);
            auto unproject = [&](float x, float y) { auto v = inverse * glm::vec4(x,y,0,1); return glm::vec3(v) / v.w; };
            auto center = unproject(0,0);
            right = glm::normalize(unproject(1,0)-center);
            up = glm::normalize(unproject(0,1)-center);
        }
        struct ParticleVertex { glm::vec3 position; glm::vec2 uv; glm::vec4 color; };
        std::vector<ParticleVertex> vertices;
        vertices.reserve(24576);
        auto &mesh = meshes.at("particles");
        unsigned currentTexture = 0; bool currentBlend = false, prepared = false;
        gl::Disable(gl::SCISSOR_TEST);
        if (world.is3d && !screen) gl::Enable(gl::DEPTH_TEST); else gl::Disable(gl::DEPTH_TEST);
        gl::DepthMask(0);
        auto flushParticles = [&]() {
            if (vertices.empty()) return;
            size_t bytes = vertices.size() * sizeof(ParticleVertex);
            if (bytes > particleAllocated) reserve(bytes-particleAllocated); else gpuBytes -= particleAllocated-bytes;
            particleAllocated = bytes;
            gl::UseProgram(particleProgram);
            gl::UniformMatrix4fv(gl::GetUniformLocation(particleProgram,"u_view"),1,0,glm::value_ptr(projection));
            gl::Uniform1i(gl::GetUniformLocation(particleProgram,"u_texture"),0);
            gl::ActiveTexture(gl::TEXTURE0);gl::BindTexture(gl::TEXTURE_2D,currentTexture?currentTexture:whiteTexture);
            gl::BlendFunc(gl::SRC_ALPHA,currentBlend?1:gl::ONE_MINUS_SRC_ALPHA);
            gl::BindVertexArray(mesh.vao);gl::BindBuffer(gl::ARRAY_BUFFER,mesh.vbo);
            gl::BufferData(gl::ARRAY_BUFFER,bytes,vertices.data(),gl::STREAM_DRAW);
            gl::VertexAttribPointer(0,3,gl::FLOAT,0,sizeof(ParticleVertex),reinterpret_cast<void*>(offsetof(ParticleVertex,position)));
            gl::VertexAttribPointer(1,2,gl::FLOAT,0,sizeof(ParticleVertex),reinterpret_cast<void*>(offsetof(ParticleVertex,uv)));
            gl::VertexAttribPointer(2,4,gl::FLOAT,0,sizeof(ParticleVertex),reinterpret_cast<void*>(offsetof(ParticleVertex,color)));
            gl::DrawArrays(gl::TRIANGLES,0,int(vertices.size()));
            ++drawCalls;++particleCalls;triangles+=unsigned(vertices.size()/3);particleQuads+=unsigned(vertices.size()/6);
            vertices.clear();
        };
        static const glm::vec2 corners[]={{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
        static const glm::vec2 uv[]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        try {
            for (const auto &p : particles) {
                auto texture = image(p.texture);
                if (prepared && (texture!=currentTexture || p.additive!=currentBlend || vertices.size()>=24576)) flushParticles();
                prepared=true;currentTexture=texture;currentBlend=p.additive;
                float angle=glm::radians(p.angle), c=std::cos(angle), s=std::sin(angle);
                for(int i=0;i<6;++i) {
                    auto corner=corners[i]*p.size;
                    float x=corner.x*c-corner.y*s, y=corner.x*s+corner.y*c;
                    vertices.push_back({p.position+right*x+up*y,{p.uv.x+uv[i].x*p.uv.z,p.uv.y+uv[i].y*p.uv.w},p.color});
                }
            }
            flushParticles();
        } catch (...) { gl::DepthMask(1);gl::BlendFunc(gl::SRC_ALPHA,gl::ONE_MINUS_SRC_ALPHA);throw; }
        gl::DepthMask(1);gl::BlendFunc(gl::SRC_ALPHA,gl::ONE_MINUS_SRC_ALPHA);
    }
    void scene(World &world, const glm::mat4 &view, int width, int height, int logicalWidth,
               int logicalHeight, bool includeUI, unsigned layers, const std::string &currentTarget = "") {
        auto ortho = glm::ortho(0.f, float(logicalWidth), float(logicalHeight), 0.f, -10000.f, 10000.f);
        auto entities = world.entities;
        bool stateSet = false, previousClip = false, previousDepth = false, particlesDrawn = false;
        glm::vec4 previousRect{0};
        std::stable_sort(entities.begin(), entities.end(), [&](auto &a, auto &b) {
            if (a->screen != b->screen)
                return !a->screen;
            return (a->screen || !world.is3d) && a->position.z < b->position.z;
        });
        for (auto &pointer : entities) {
            auto &e = *pointer;
            if (!e.alive || !e.visible || e.kind == "empty" || (!includeUI && e.screen) ||
                !(e.layer & layers) || (shadowPass && (e.screen || !e.castsShadow || e.kind == "text")) ||
                (!currentTarget.empty() && e.texture == "@target:" + currentTarget))
                continue;
            if (e.screen && !particlesDrawn && !shadowPass) {
                flush();particles(world,view,ortho,layers,false);particlesDrawn=true;stateSet=false;
            }
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
            auto tex = shadowPass ? 0 : image(e.texture);
            if (!shadowPass && e.kind == "sprite" && (!world.is3d || e.screen))
                quad(matrix, projection, e.color, tex, e.uv, e.texture.rfind("@target:", 0) == 0);
            else {
                flush();
                draw(meshes.at(e.kind), matrix, projection, e.color, tex, world.is3d && !e.screen);
            }
        }
        flush();
        if (!particlesDrawn) particles(world,view,ortho,layers,false);
        if (includeUI) particles(world,view,ortho,layers,true);
        entityUniforms = Json::object();
        drawUV = {0, 0, 1, 1};
        drawFlip = false;
        gl::Disable(gl::SCISSOR_TEST);
    }
    void edit(World &world);
};
std::array<float, 3> measureText(const Config &config, const std::string &value, float size) {
    if (!std::isfinite(size) || size <= 0)
        throw std::runtime_error("Text size must be positive and finite");
    auto faces = fonts(config);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&faces.front()->font, &ascent, &descent, &gap);
    float lineHeight = (ascent - descent + gap) * stbtt_ScaleForPixelHeight(&faces.front()->font, size),
          x = 0, width = 0;
    int previous = 0, lastFace = -1, lines = 1;
    for (auto code : unicode(value)) {
        if (code == '\n') {
            width = std::max(width, x);
            x = 0;
            previous = 0;
            lastFace = -1;
            ++lines;
            continue;
        }
        int face = fontFor(faces, code), advance, bearing;
        auto &font = faces[face]->font;
        float scale = stbtt_ScaleForPixelHeight(&font, size);
        stbtt_GetCodepointHMetrics(&font, code, &advance, &bearing);
        if (previous && lastFace == face)
            x += stbtt_GetCodepointKernAdvance(&font, previous, code) * scale;
        x += advance * scale;
        previous = code;
        lastFace = face;
    }
    return {std::max(width, x), lines * lineHeight, lineHeight};
}
void validateMedia(const Config &c) {
    const std::set<std::string> extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga",
                                              ".gif", ".psd", ".hdr",  ".pic", ".pnm"};
    for (auto &entry : fs::recursive_directory_iterator(c.paths.at("textures")))
        if (entry.is_regular_file() && extensions.count(entry.path().extension().u8string())) {
            int w, h, n;
            auto filename = entry.path().u8string();
            if (!stbi_info(filename.c_str(), &w, &h, &n))
                throw std::runtime_error("Invalid texture " + filename + ": " + stbi_failure_reason());
            auto *bytes = stbi_load(filename.c_str(), &w, &h, &n, 4);
            if (!bytes)
                throw std::runtime_error("Invalid texture " + filename + ": " + stbi_failure_reason());
            stbi_image_free(bytes);
        }
    for (auto &entry : fs::recursive_directory_iterator(c.paths.at("models")))
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().u8string();
            if (ext == ".obj")
                obj(entry.path());
            if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae") {
                auto model = loadModel(entry.path(), c.root);
                for (auto &part : model->parts) if (!part.texture.empty()) {
                    int width, height, channels;
                    auto image = stbi_load(part.texture.u8string().c_str(), &width, &height, &channels, 4);
                    if (!image) throw std::runtime_error("Invalid model texture: " + part.texture.u8string());
                    stbi_image_free(image);
                }
            }
        }
    fonts(c);
}
Renderer::Renderer() : impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
void Renderer::init(const Config &c, World &world) {
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
    if (active && active->editing)
        editor(true);
}
void Renderer::stage() {
    staged = std::make_unique<Impl>();
    staged->config = impl->config;
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
            int raster = glyphRasterSize(e.fontSize, std::max(std::abs(e.scale.x), std::abs(e.scale.y)),
                                         resources.density);
            for (auto code : unicode(e.text))
                if (code != '\n')
                    resources.glyph(fontFor(resources.fontFaces, code), code, raster);
            continue;
        }
        if (e.scale.x == 0 || e.scale.y == 0 || e.scale.z == 0)
            throw std::runtime_error("Drawable scale cannot be zero: " + e.id);
        resources.image(e.texture);
        if (e.kind == "mesh") {
            auto &asset = resources.prepareModel(e.model);
            if (!e.animation.empty())
                asset.pose(e.animation, 0, e.animationLoop);
            if (e.texture.empty()) {
                auto key = resources.config->asset("models", e.model).u8string();
                for (size_t i = 0; i < asset.parts.size(); ++i)
                    resources.modelTexture(asset.parts[i], key, i);
            }
        }
    }
}
void Renderer::render(World &world) {
    auto started = std::chrono::steady_clock::now();
    int w, h;
    glfwGetFramebufferSize(impl->window, &w, &h);
    glfwGetWindowSize(impl->window, &world.width, &world.height);
    if (w <= 0 || h <= 0 || world.width <= 0 || world.height <= 0)
        return;
    ++impl->frame;
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
bool Renderer::previewing() const { return impl->preview; }
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
void Renderer::Impl::edit(World &world) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    auto before = world.serialize();
    bool changed = false;
    auto restore = [&](const Json &data) {
        for (auto &e : world.entities)
            e->alive = false;
        world.entities.clear();
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
        for (auto &entity : data["entities"])
            world.spawn(entity);
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
        active->pendingScene = active->currentScene;
        undo.clear();
        redo.clear();
    }
    if (ImGui::Checkbox("Play preview", &preview)) {
        active->gamePaused = !preview;
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
        ImGui::Text("ID: %s", entity->id.c_str());
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
        if (entity->kind == "text") {
            char text[4096]{};
            std::snprintf(text, sizeof(text), "%s", entity->text.c_str());
            if (ImGui::InputTextMultiline("Text", text, sizeof(text))) {
                entity->text = text;
                changed = true;
            }
            changed |= ImGui::DragFloat("Font size", &entity->fontSize, .5f, 1, 1024);
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
            entity->alive = false;
            selected.clear();
            changed = true;
        }
        if (!entity->model.empty()) {
            auto info = active->assets.model(config->asset("models", entity->model))->info();
            for (auto &clip : info["animations"])
                if (ImGui::Button(clip["name"].get<std::string>().c_str())) {
                    entity->animation = clip["name"];
                    entity->animationTime = 0;
                    entity->animationPlaying = true;
                }
        }
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
                            active->pendingScene = relative;
                        else if (entity && std::string(group) == "textures") {
                            entity->texture = relative;
                            changed = true;
                        } else if (std::string(group) == "models") {
                            if (!entity)
                                entity = world.spawn(Json{{"kind", "mesh"},
                                                          {"model", relative},
                                                          {"name", item.path().stem().u8string()}});
                            else {
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
    auto assetStats = active->assets.stats();
    ImGui::Text("Draw calls: %u; batches: %u", drawCalls, batchCalls);
    ImGui::Text("GPU: %.1f MiB", double(gpuBytes) / (1024 * 1024));
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
        move *= active->dt * 5;
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
