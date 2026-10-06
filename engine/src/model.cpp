#include <forge/engine.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <forge/model.hpp>
#include <assimp/GltfMaterial.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <cstring>
#include <glm/gtx/matrix_decompose.hpp>
namespace forge {
namespace {
struct Stream : Assimp::IOStream {
    std::vector<char> data;
    size_t position = 0;
    explicit Stream(const fs::path &path) {
        std::ifstream s(path, std::ios::binary);
        if (!s)
            throw std::runtime_error("Cannot read model dependency");
        data.assign(std::istreambuf_iterator<char>(s), {});
    }
    size_t Read(void *out, size_t size, size_t count) override {
        if (!size)
            return 0;
        count = std::min(count, (data.size() - position) / size);
        std::memcpy(out, data.data() + position, count * size);
        position += count * size;
        return count;
    }
    size_t Write(const void *, size_t, size_t) override {
        return 0;
    }
    aiReturn Seek(size_t offset, aiOrigin origin) override {
        size_t next = origin == aiOrigin_SET   ? offset
                      : origin == aiOrigin_CUR ? position + offset
                                               : data.size() - offset;
        if (next > data.size())
            return aiReturn_FAILURE;
        position = next;
        return aiReturn_SUCCESS;
    }
    size_t Tell() const override {
        return position;
    }
    size_t FileSize() const override {
        return data.size();
    }
    void Flush() override {}
};
struct ProjectIO : Assimp::IOSystem {
    fs::path root, folder;
    std::map<fs::path, Model::Dependency> dependencies;
    ProjectIO(fs::path r, fs::path f) : root(std::move(r)), folder(std::move(f)) {}
    fs::path path(const char *value) const {
        auto p = fs::u8path(value);
        p = fs::weakly_canonical(p.is_absolute() ? p : folder / p);
        auto rel = p.lexically_relative(root);
        if (rel.empty() || *rel.begin() == "..")
            throw std::runtime_error("Model dependency escapes project: " + p.u8string());
        return p;
    }
    bool Exists(const char *value) const override {
        try {
            return fs::is_regular_file(path(value));
        } catch (...) {
            return false;
        }
    }
    char getOsSeparator() const override {
        return '/';
    }
    Assimp::IOStream *Open(const char *value, const char *mode) override {
        if (std::strchr(mode, 'w'))
            return nullptr;
        auto requested=fs::u8path(value);
        requested=(requested.is_absolute()?requested:folder/requested).lexically_normal();
        auto resolved=path(value);
        Model::Dependency dependency{requested,resolved,fs::last_write_time(requested),fs::file_size(requested)};
        auto stream=std::make_unique<Stream>(resolved);
        dependencies[requested]=std::move(dependency);
        return stream.release();
    }
    void Close(Assimp::IOStream *stream) override {
        delete stream;
    }
};
glm::mat4 matrix(const aiMatrix4x4 &m) {
    return glm::transpose(glm::make_mat4(&m.a1));
}
glm::vec3 vec(const aiVector3D &v) {
    return {v.x, v.y, v.z};
}
template <class T> T sample(const std::vector<std::pair<double, T>> &keys, double time, const T &fallback) {
    if (keys.empty())
        return fallback;
    if (time <= keys.front().first)
        return keys.front().second;
    auto it =
        std::upper_bound(keys.begin(), keys.end(), time, [](double t, const auto &k) { return t < k.first; });
    if (it == keys.end())
        return keys.back().second;
    auto prev = it - 1;
    float factor = float((time - prev->first) / (it->first - prev->first));
    if constexpr (std::is_same_v<T, glm::quat>)
        return glm::normalize(glm::slerp(prev->second, it->second, factor));
    else
        return glm::mix(prev->second, it->second, factor);
}
} // namespace
std::shared_ptr<Model> loadModel(const fs::path &file, const fs::path &projectRoot) {
    Assimp::Importer importer;
    auto io=new ProjectIO(projectRoot, file.parent_path());
    importer.SetIOHandler(io);
    auto scene =
        importer.ReadFile(file.u8string(), aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                               aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights |
                                               aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode || !scene->mNumMeshes)
        throw std::runtime_error("Model " + file.u8string() + ": " + importer.GetErrorString());
    auto result = std::make_shared<Model>();
    for(const auto& [path,dependency]:io->dependencies)result->dependencies.push_back(dependency);
    std::map<std::string, int> names;
    std::map<const aiNode *, int> indices;
    std::function<void(aiNode *, int)> nodes = [&](aiNode *node, int parent) {
        int index = int(result->nodes.size());
        indices[node] = index;
        names[node->mName.C_Str()] = index;
        result->nodes.push_back({node->mName.C_Str(), parent, matrix(node->mTransformation)});
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            nodes(node->mChildren[i], index);
    };
    nodes(scene->mRootNode, -1);
    result->rootInverse = glm::inverse(result->nodes.front().transform);
    std::function<void(aiNode *)> geometry = [&](aiNode *node) {
        for (unsigned mi = 0; mi < node->mNumMeshes; ++mi) {
            auto mesh = scene->mMeshes[node->mMeshes[mi]];
            Model::Part part;
            part.node = indices.at(node);
            if (mesh->mNumBones > 128 || mesh->mNumVertices > 2000000)
                throw std::runtime_error("Model exceeds 128 bones per mesh or 2M vertices");
            std::vector<ModelVertex> vertices(mesh->mNumVertices);
            for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
                auto &v = vertices[i];
                v.p = vec(mesh->mVertices[i]);
                if(mesh->HasVertexColors(0)){auto c=mesh->mColors[0][i];v.color={c.r,c.g,c.b,c.a};}
                if (mesh->HasNormals())
                    v.n = vec(mesh->mNormals[i]);
                if (mesh->HasTextureCoords(0))
                    v.uv = {mesh->mTextureCoords[0][i].x, 1 - mesh->mTextureCoords[0][i].y};
                for (int a = 0; a < 3; ++a) {
                    checkedFloat(v.p[a], "model vertex");
                    checkedFloat(v.n[a], "model normal");
                }
                checkedFloat(v.uv.x, "model UV");
                checkedFloat(v.uv.y, "model UV");
                for (int a = 0; a < 4; ++a)
                    if (checkedFloat(v.color[a], "model vertex color") < 0 || v.color[a] > 1)
                        throw std::runtime_error("Model vertex color must be 0..1");
            }
            for (unsigned bi = 0; bi < mesh->mNumBones; ++bi) {
                auto bone = mesh->mBones[bi];
                auto found = names.find(bone->mName.C_Str());
                if (found == names.end())
                    throw std::runtime_error("Missing skeleton node");
                part.bones.push_back({found->second, matrix(bone->mOffsetMatrix)});
                for (unsigned wi = 0; wi < bone->mNumWeights; ++wi) {
                    auto weight = bone->mWeights[wi];
                    auto &v = vertices.at(weight.mVertexId);
                    for (int k = 0; k < 4; ++k)
                        if (v.weights[k] == 0) {
                            v.bones[k] = int(bi);
                            v.weights[k] = weight.mWeight;
                            break;
                        }
                }
            }
            for (unsigned fi = 0; fi < mesh->mNumFaces; ++fi) {
                auto &face = mesh->mFaces[fi];
                if (face.mNumIndices != 3)
                    continue;
                for (unsigned k = 0; k < 3; ++k) {
                    auto v = vertices.at(face.mIndices[k]);
                    float sum = v.weights.x + v.weights.y + v.weights.z + v.weights.w;
                    if (sum > 0)
                        v.weights /= sum;
                    part.vertices.push_back(v);
                }
            }
            if(mesh->mNumAnimMeshes>32)throw std::runtime_error("Model exceeds 32 morph targets per part");
            for(unsigned target=0;target<mesh->mNumAnimMeshes;++target){
                auto* source=mesh->mAnimMeshes[target];
                if(source->mNumVertices!=mesh->mNumVertices)throw std::runtime_error("Morph topology mismatch");
                Model::MorphTarget morph;
                morph.name=source->mName.length?source->mName.C_Str():"morph_"+std::to_string(target);
                morph.weight=checkedFloat(source->mWeight,"morph default weight");
                if(std::abs(morph.weight)>10)throw std::runtime_error("Morph default outside -10..10");
                for(auto& previous:part.morphs)if(previous.name==morph.name)throw std::runtime_error("Duplicate morph target name");
                for(unsigned fi=0;fi<mesh->mNumFaces;++fi){auto& face=mesh->mFaces[fi];if(face.mNumIndices!=3)continue;
                    for(unsigned k=0;k<3;++k){auto index=face.mIndices[k];
                        auto position=source->mVertices?vec(source->mVertices[index])-vertices[index].p:glm::vec3(0);
                        auto normal=source->mNormals?vec(source->mNormals[index])-vertices[index].n:glm::vec3(0);
                        for(int axis=0;axis<3;++axis){checkedFloat(position[axis],"morph position");checkedFloat(normal[axis],"morph normal");}
                        morph.positions.push_back(position);morph.normals.push_back(normal);
                    }
                }
                result->memoryBytes+=(morph.positions.size()+morph.normals.size())*sizeof(glm::vec3);
                part.morphs.push_back(std::move(morph));
            }
            auto material = scene->mMaterials[mesh->mMaterialIndex];
            aiColor4D color;
            if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
                part.color = {color.r, color.g, color.b, color.a};
            float metallic=0,roughness=1;
            bool pbr=material->Get(AI_MATKEY_METALLIC_FACTOR,metallic)==AI_SUCCESS;
            material->Get(AI_MATKEY_ROUGHNESS_FACTOR,roughness);
            if(pbr){
                part.material={{"shading","pbr"},{"metallic",metallic},{"roughness",roughness},{"alpha_mode","opaque"}};
                if(aiGetMaterialColor(material,AI_MATKEY_BASE_COLOR,&color)==AI_SUCCESS)part.color={color.r,color.g,color.b,color.a};
                aiString alpha;if(material->Get(AI_MATKEY_GLTF_ALPHAMODE,alpha)==AI_SUCCESS){auto name=std::string(alpha.C_Str());part.material["alpha_mode"]=name=="BLEND"?"blend":name=="MASK"?"mask":"opaque";}
                float cutoff=.5;material->Get(AI_MATKEY_GLTF_ALPHACUTOFF,cutoff);part.material["alpha_cutoff"]=cutoff;
                if(aiGetMaterialColor(material,AI_MATKEY_COLOR_EMISSIVE,&color)==AI_SUCCESS)part.material["emissive"]={color.r,color.g,color.b};
            }
            for (int a = 0; a < 4; ++a)
                if ((checkedFloat(part.color[a], "model material color") < 0 || part.color[a] > 1) && pbr)
                    throw std::runtime_error("Model material color must be 0..1");
            if (pbr) {
                for (auto field : {"metallic", "roughness", "alpha_cutoff"}) {
                    float value = finiteNumber(part.material[field], std::string("model ") + field);
                    if (value < 0 || value > 1)
                        throw std::runtime_error(std::string("Model ") + field + " must be 0..1");
                }
                if (part.material.contains("emissive"))
                    for (auto &component : part.material["emissive"]) {
                        float value = finiteNumber(component, "model emissive");
                        if (value < 0 || value > 1000)
                            throw std::runtime_error("Model emissive must be 0..1000");
                    }
            }
            auto channel=[&](aiTextureType type,const std::string& field){
                aiString texture;material->GetTexture(type,0,&texture);
            if (!texture.length)return;
            std::shared_ptr<ImageData> imageData;fs::path imagePath;
            {
                auto raw = std::string(texture.C_Str());
                auto embedded = scene->GetEmbeddedTexture(raw.c_str());
                if (embedded) {
                    imageData = std::make_shared<ImageData>();
                    auto &image = *imageData;
                    if (!embedded->mHeight) {
                        int channels;
                        auto pixels = stbi_load_from_memory(
                            reinterpret_cast<const unsigned char *>(embedded->pcData), int(embedded->mWidth),
                            &image.width, &image.height, &channels, 4);
                        if (!pixels)
                            throw std::runtime_error("Cannot decode embedded model texture");
                        image.pixels.assign(pixels, pixels + size_t(image.width) * image.height * 4);
                        stbi_image_free(pixels);
                    } else {
                        image.width = int(embedded->mWidth);
                        image.height = int(embedded->mHeight);
                        for (size_t i = 0; i < size_t(image.width) * image.height; ++i) {
                            auto c = embedded->pcData[i];
                            image.pixels.insert(image.pixels.end(), {c.r, c.g, c.b, c.a});
                        }
                    }
                    result->memoryBytes += image.pixels.size();
                } else {
                    std::replace(raw.begin(), raw.end(), '\\', '/');
                    ProjectIO io(projectRoot, file.parent_path());
                    imagePath = io.path(raw.c_str());
                    if (!fs::is_regular_file(imagePath))
                        throw std::runtime_error("Model texture missing: " + imagePath.u8string());
                }
            }

                if(imageData)part.embeddedMaps[field]=imageData;
                if(!imagePath.empty())part.maps[field]=imagePath;
                if(field=="albedo_texture"){part.embedded=imageData;part.texture=imagePath;}
            };
            channel(aiTextureType_BASE_COLOR,"albedo_texture");
            if(part.texture.empty() && !part.embedded)channel(aiTextureType_DIFFUSE,"albedo_texture");
            channel(aiTextureType_NORMALS,"normal_texture");
            channel(aiTextureType_AMBIENT_OCCLUSION,"occlusion_texture");
            channel(aiTextureType_EMISSIVE,"emissive_texture");
            // glTF packed metallic/roughness: Assimp exposes the same image for both types.
            channel(aiTextureType_METALNESS,"metallic_texture");channel(aiTextureType_DIFFUSE_ROUGHNESS,"roughness_texture");
            bool packed=(part.maps.count("metallic_texture") && part.maps.count("roughness_texture") && part.maps["metallic_texture"]==part.maps["roughness_texture"]);
            if(part.embeddedMaps.count("metallic_texture") && part.embeddedMaps.count("roughness_texture")){
                aiString m,r;material->GetTexture(aiTextureType_METALNESS,0,&m);material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS,0,&r);packed=std::string(m.C_Str())==r.C_Str();
            }
            if(packed){if(part.maps.count("metallic_texture"))part.maps["metallic_roughness_texture"]=part.maps["metallic_texture"];if(part.embeddedMaps.count("metallic_texture"))part.embeddedMaps["metallic_roughness_texture"]=part.embeddedMaps["metallic_texture"];for(auto field:{"metallic_texture","roughness_texture"}){part.maps.erase(field);part.embeddedMaps.erase(field);}}
            result->memoryBytes +=
                part.vertices.size() * sizeof(ModelVertex) + part.bones.size() * sizeof(Model::Bone);
            result->parts.push_back(std::move(part));
        }
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            geometry(node->mChildren[i]);
    };
    geometry(scene->mRootNode);
    for (unsigned ci = 0; ci < scene->mNumAnimations; ++ci) {
        auto animation = scene->mAnimations[ci];
        Model::Clip clip;
        clip.name = animation->mName.length ? animation->mName.C_Str() : "animation_" + std::to_string(ci);
        clip.duration = animation->mDuration;
        clip.ticks = animation->mTicksPerSecond > 0 ? animation->mTicksPerSecond : 25;
        if(!std::isfinite(clip.duration) || clip.duration<0 || !std::isfinite(clip.ticks) || clip.ticks<=0 || !std::isfinite(clip.duration/clip.ticks))throw std::runtime_error("Invalid animation duration/ticks");
        for(auto& previous:result->clips)if(previous.name==clip.name)throw std::runtime_error("Duplicate animation clip name");
        for (unsigned k = 0; k < animation->mNumChannels; ++k) {
            auto source = animation->mChannels[k];
            auto found = names.find(source->mNodeName.C_Str());
            if (found == names.end())
                continue;
            Model::Channel channel;
            channel.node = found->second;
            for (unsigned i = 0; i < source->mNumPositionKeys; ++i)
                channel.positions.push_back(
                    {source->mPositionKeys[i].mTime, vec(source->mPositionKeys[i].mValue)});
            for (unsigned i = 0; i < source->mNumScalingKeys; ++i)
                channel.scales.push_back(
                    {source->mScalingKeys[i].mTime, vec(source->mScalingKeys[i].mValue)});
            for (unsigned i = 0; i < source->mNumRotationKeys; ++i) {
                auto q = source->mRotationKeys[i].mValue;
                channel.rotations.push_back({source->mRotationKeys[i].mTime, {q.w, q.x, q.y, q.z}});
            }
            auto validateKeys=[](const auto& keys){
                double previous=-1;
                for(auto& key:keys){
                    if(!std::isfinite(key.first) || key.first<0 || key.first<=previous)throw std::runtime_error("Animation key times must strictly increase");
                    previous=key.first;for(int axis=0;axis<int(key.second.length());++axis)checkedFloat(key.second[axis],"animation key");
                }
            };
            validateKeys(channel.positions);validateKeys(channel.scales);validateKeys(channel.rotations);
            for(auto& key:channel.rotations)if(glm::length(key.second)<1e-8)throw std::runtime_error("Animation quaternion cannot be zero");
            clip.channels.push_back(std::move(channel));
        }
        for(unsigned k=0;k<animation->mNumMorphMeshChannels;++k){
            auto* source=animation->mMorphMeshChannels[k];auto found=names.find(source->mName.C_Str());
            if(found==names.end())continue;
            Model::MorphChannel channel;channel.node=found->second;
            size_t count=0;for(auto& part:result->parts)if(part.node==channel.node)count=std::max(count,part.morphs.size());
            for(unsigned i=0;i<source->mNumKeys;++i){auto& key=source->mKeys[i];std::vector<double> weights(count,0);
                for(unsigned j=0;j<key.mNumValuesAndWeights;++j){if(key.mValues[j]>=count)throw std::runtime_error("Morph animation index outside target range");weights[key.mValues[j]]=checkedFloat(key.mWeights[j],"morph animation weight");}
                if(!std::isfinite(key.mTime) || key.mTime<0 || (!channel.keys.empty() && key.mTime<=channel.keys.back().first))throw std::runtime_error("Morph key times must strictly increase");
                for(double value:weights)if(std::abs(value)>10)throw std::runtime_error("Morph animation weight outside -10..10");
                channel.keys.push_back({key.mTime,std::move(weights)});result->memoryBytes+=count*sizeof(double);
            }
            clip.morphs.push_back(std::move(channel));
        }
        result->clips.push_back(std::move(clip));
    }
    result->memoryBytes += result->nodes.size() * sizeof(Model::Node);
    for(const auto& dependency:result->dependencies)
        result->memoryBytes += sizeof(dependency) +
            (dependency.path.native().size()+dependency.resolved.native().size())*sizeof(fs::path::value_type);
    for (auto &clip : result->clips)
        for (auto &channel : clip.channels)
            result->memoryBytes += sizeof(channel) + channel.positions.size() * sizeof(channel.positions[0]) +
                                   channel.scales.size() * sizeof(channel.scales[0]) +
                                   channel.rotations.size() * sizeof(channel.rotations[0]);
    return result;
}
std::vector<glm::mat4> Model::localPose(const std::string &name, double seconds, bool loop) const {
    std::vector<glm::mat4> local;
    for (auto &node : nodes)
        local.push_back(node.transform);
    if (!name.empty()) {
        auto clip = std::find_if(clips.begin(), clips.end(), [&](auto &c) { return c.name == name; });
        if (clip == clips.end())
            throw std::runtime_error("Unknown animation: " + name);
        if (!std::isfinite(seconds))
            throw std::runtime_error("Animation time must be finite");
        double duration = clip->duration / clip->ticks;
        double t = duration > 0 ? (loop ? std::fmod(std::max(0.0, seconds), duration)
                                        : std::clamp(seconds, 0.0, duration)) *
                                      clip->ticks
                                : 0;
        for (auto &channel : clip->channels) {
            glm::vec3 scale, position, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(local[channel.node], scale, rotation, position, skew, perspective);
            local[channel.node] = glm::translate(glm::mat4(1), sample(channel.positions, t, position)) *
                                  glm::mat4_cast(sample(channel.rotations, t, rotation)) *
                                  glm::scale(glm::mat4(1), sample(channel.scales, t, scale));
        }
    }
    return local;
}
std::vector<glm::mat4> Model::pose(const std::string& name,double seconds,bool loop) const {
    auto local=localPose(name,seconds,loop);
    std::vector<glm::mat4> globals(local.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        globals[i] = nodes[i].parent < 0 ? local[i] : globals[nodes[i].parent] * local[i];
    return globals;
}
Json Model::info() const {
    Json animations = Json::array();
    for (auto &clip : clips)
        animations.push_back({{"name", clip.name}, {"duration", clip.duration / clip.ticks}});
    Json morphs=Json::array(),skeleton=Json::array();
    for(auto& n:nodes)skeleton.push_back(Json{{"name",n.name},{"parent",n.parent}});
    for(size_t i=0;i<parts.size();++i)for(auto& morph:parts[i].morphs)morphs.push_back(Json{{"part",i},{"name",morph.name},{"weight",morph.weight}});
    Json textures = Json::array();
    size_t vertices = 0, bones = 0;
    for (auto &part : parts) {
        vertices += part.vertices.size();
        bones += part.bones.size();
        if (!part.texture.empty())
            textures.push_back(part.texture.u8string());
    }
    return {{"parts", parts.size()},    {"vertices", vertices}, {"bones", bones},
            {"animations", animations}, {"bytes", memoryBytes}, {"textures", textures},{"morph_targets",morphs},{"skeleton",skeleton}};
}
bool Model::dependenciesCurrent() const {
    try {
        for(const auto& dependency:dependencies)
            if(fs::weakly_canonical(dependency.path)!=dependency.resolved ||
               fs::last_write_time(dependency.path)!=dependency.modified || fs::file_size(dependency.path)!=dependency.size)
                return false;
        return true;
    }catch(const fs::filesystem_error&){return false;}
}
} // namespace forge
