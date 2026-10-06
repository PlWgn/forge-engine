#pragma once
#include <forge/types.hpp>
#include <forge/image.hpp>
#include <glm/gtc/quaternion.hpp>
namespace forge {
struct ModelVertex {
    glm::vec3 p{0};
    glm::vec2 uv{0};
    glm::vec3 n{0, 1, 0};
    glm::vec4 color{1};
    glm::ivec4 bones{0};
    glm::vec4 weights{0};
};
struct Model {
    struct Dependency {
        fs::path path, resolved;
        fs::file_time_type modified;
        uintmax_t size;
    };
    struct Node {
        std::string name;
        int parent = -1;
        glm::mat4 transform{1};
    };
    struct Bone {
        int node;
        glm::mat4 offset;
    };
    struct MorphTarget {std::string name;double weight=0;std::vector<glm::vec3> positions,normals;};
    struct Part {
        std::vector<ModelVertex> vertices;
        std::vector<Bone> bones;
        int node = 0;
        std::vector<MorphTarget> morphs;
        glm::vec4 color{1};
        Json material = Json::object();
        std::map<std::string,fs::path> maps;
        std::map<std::string,std::shared_ptr<ImageData>> embeddedMaps;
        fs::path texture;
        std::shared_ptr<ImageData> embedded;
    };
    struct Channel {
        int node;
        std::vector<std::pair<double, glm::vec3>> positions, scales;
        std::vector<std::pair<double, glm::quat>> rotations;
    };
    struct MorphChannel {int node;std::vector<std::pair<double,std::vector<double>>> keys;};
    struct Clip {
        std::string name;
        double duration = 0, ticks = 25;
        std::vector<Channel> channels;
        std::vector<MorphChannel> morphs;
    };
    std::vector<Node> nodes;
    std::vector<Part> parts;
    std::vector<Clip> clips;
    std::vector<Dependency> dependencies;
    glm::mat4 rootInverse{1};
    size_t memoryBytes = 0;
    std::vector<glm::mat4> pose(const std::string &, double seconds, bool loop) const;
    std::vector<glm::mat4> localPose(const std::string&,double,bool) const;
    std::vector<double> morphPose(size_t part,const std::string&,double,bool) const;
    Json info() const;
    bool dependenciesCurrent() const;
};
std::shared_ptr<Model> loadModel(const fs::path &file, const fs::path &projectRoot);
} // namespace forge
