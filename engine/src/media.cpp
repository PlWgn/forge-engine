#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <forge/engine.hpp>
#include <forge/model.hpp>
#include <forge/text.hpp>
#include <sstream>
namespace forge {
namespace {
using Vertex=ModelVertex;
std::string textFile(const fs::path &p) {
    std::ifstream s(p, std::ios::binary);
    if (!s)
        throw std::runtime_error("Cannot read: " + p.u8string());
    return {std::istreambuf_iterator<char>(s), {}};
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
}
void validateMedia(const Config &c) {
    const std::set<std::string> extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga",
                                              ".gif", ".psd", ".hdr",  ".pic", ".pnm"};
    for (auto &entry : fs::recursive_directory_iterator(c.paths.at("textures")))
        if (entry.is_regular_file() && extensions.count(entry.path().extension().u8string())) {
            int w, h, n;
            auto filename = c.asset("textures",entry.path().lexically_relative(c.paths.at("textures")).generic_u8string()).u8string();
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
            auto path=c.asset("models",entry.path().lexically_relative(c.paths.at("models")).generic_u8string());
            if (ext == ".obj")
                obj(path);
            if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae") {
                auto model = loadModel(path, c.root);
                std::set<fs::path> maps;
                for (auto &part : model->parts) {
                    if (!part.texture.empty()) maps.insert(part.texture);
                    for (auto &map : part.maps) maps.insert(map.second);
                }
                for (auto &path : maps) {
                    int width, height, channels;
                    auto image = stbi_load(path.u8string().c_str(), &width, &height, &channels, 4);
                    if (!image) throw std::runtime_error("Invalid model texture: " + path.u8string());
                    stbi_image_free(image);
                }
            }
        }
    fonts(c);
}
}
