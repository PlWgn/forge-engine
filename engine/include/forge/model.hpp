#pragma once
#include <forge/engine.hpp>
#include <glm/gtc/quaternion.hpp>
namespace forge {
struct ModelVertex {
    glm::vec3 p{0};
    glm::vec2 uv{0};
    glm::vec3 n{0, 1, 0};
    glm::ivec4 bones{0};
    glm::vec4 weights{0};
};
struct Model {
    struct Node {
        std::string name;
        int parent = -1;
        glm::mat4 transform{1};
    };
    struct Bone {
        int node;
        glm::mat4 offset;
    };
    struct Part {
        std::vector<ModelVertex> vertices;
        std::vector<Bone> bones;
        int node = 0;
        glm::vec4 color{1};
        fs::path texture;
        std::shared_ptr<ImageData> embedded;
    };
    struct Channel {
        int node;
        std::vector<std::pair<double, glm::vec3>> positions, scales;
        std::vector<std::pair<double, glm::quat>> rotations;
    };
    struct Clip {
        std::string name;
        double duration = 0, ticks = 25;
        std::vector<Channel> channels;
    };
    std::vector<Node> nodes;
    std::vector<Part> parts;
    std::vector<Clip> clips;
    glm::mat4 rootInverse{1};
    size_t memoryBytes = 0;
    std::vector<glm::mat4> pose(const std::string &, double seconds, bool loop) const;
    Json info() const;
};
std::shared_ptr<Model> loadModel(const fs::path &file, const fs::path &projectRoot);
} // namespace forge
