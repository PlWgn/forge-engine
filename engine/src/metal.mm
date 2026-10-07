// Optional adaptable Metal backend for Forge's small graphics command
// vocabulary.
#include <forge/gl.hpp>
#include <forge/logger.hpp>
#include <forge/metal.hpp>
#include <forge/shader_compiler.hpp>
#define GLFW_EXPOSE_NATIVE_COCOA
#import <Cocoa/Cocoa.h>
#include <GLFW/glfw3native.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#if FORGE_WITH_EDITOR
#include <imgui.h>
#include <imgui_impl_metal.h>
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <limits>
#include <tuple>
#include <unordered_map>
namespace forge {
namespace {
using U = gl::U;
using I = gl::I;
NSString *ns(const std::string &value) {
    auto result = [[NSString alloc] initWithBytes:value.data()
                                           length:value.size()
                                         encoding:NSUTF8StringEncoding];
    if (!result)
        throw std::runtime_error("Metal shader/configuration contains invalid UTF-8");
    return result;
}
std::string message(NSError *error) {
    return error ? std::string(error.localizedDescription.UTF8String) : "No Metal diagnostics";
}
void require(bool condition, const std::string &operation) {
    if (!condition)
        throw std::runtime_error(operation);
}
void logText(const std::string &text, I capacity, I *written, char *output) {
    size_t count = std::min(text.size(), size_t(std::max(0, capacity - 1)));
    if (output && capacity > 0) {
        std::memcpy(output, text.data(), count);
        output[count] = 0;
    }
    if (written)
        *written = I(count);
}
struct Buffer {
    id<MTLBuffer> gpu = nil;
    std::vector<unsigned char> bytes;
    bool dirty = true;
};
struct Attribute {
    U buffer = 0, type = gl::FLOAT, divisor = 0;
    size_t offset = 0, stride = 0;
    I size = 4;
    bool enabled = false;
};
struct VertexArray {
    std::array<Attribute, 16> attributes;
};
struct Texture {
    id<MTLTexture> gpu = nil;
    id<MTLSamplerState> sampler = nil;
    int width = 0, height = 0;
    bool depth = false, mipmaps = false;
    U minFilter = gl::LINEAR, magFilter = gl::LINEAR, wrapS = gl::CLAMP_TO_EDGE,
      wrapT = gl::CLAMP_TO_EDGE;
};
struct Framebuffer {
    U color = 0, depth = 0;
};
struct Shader {
    U kind = 0;
    std::string source, error;
    bool compiled = false;
};
struct Variable {
    std::string name;
    size_t offset = 0, elements = 1, stride = 0, components = 1;
    bool integer = false, boolean = false, matrix = false;
};
struct Constants {
    size_t slot = 0;
    std::vector<unsigned char> bytes;
    std::vector<Variable> variables;
};
struct Resource {
    std::string name;
    size_t slot = 0;
    bool sampler = false;
};
struct Stage {
    id<MTLFunction> function = nil;
    std::vector<Constants> constants;
    std::vector<Resource> resources;
};
struct Program {
    Stage vertex, fragment;
    std::vector<U> shaders;
    std::vector<std::string> locations;
    std::unordered_map<std::string, I> locationIndex, textureUnits;
    // Only the effective vertex descriptor participates, never VAO/buffer
    // identity or mesh revisions. Programs own their shader-specific variants.
    using Layout = std::array<std::array<size_t, 5>, 16>;
    using Key = std::tuple<Layout, bool, U, U, bool>;
    struct Pipeline {
        id<MTLRenderPipelineState> state = nil;
        uint64_t lastUse = 0;
    };
    std::map<Key, Pipeline> pipelines;
    uint64_t pipelineClock = 0;
    std::string error;
    bool linked = false;
};
MTLVertexFormat format(U type, int size) {
    require(size >= 1 && size <= 4, "Vertex attribute requires 1..4 components");
    const MTLVertexFormat floats[] = {MTLVertexFormatFloat, MTLVertexFormatFloat2,
                                      MTLVertexFormatFloat3, MTLVertexFormatFloat4};
    const MTLVertexFormat ints[] = {MTLVertexFormatInt, MTLVertexFormatInt2, MTLVertexFormatInt3,
                                    MTLVertexFormatInt4};
    const MTLVertexFormat uints[] = {MTLVertexFormatUInt, MTLVertexFormatUInt2,
                                     MTLVertexFormatUInt3, MTLVertexFormatUInt4};
    if (type == gl::FLOAT)
        return floats[size - 1];
    if (type == 0x1404)
        return ints[size - 1];
    if (type == gl::UNSIGNED_INT)
        return uints[size - 1];
    throw std::runtime_error("Metal vertex attributes require float32/int32/uint32");
}
void dataShape(MTLDataType type, size_t &components, bool &integer, bool &boolean, bool &matrix) {
    integer = false;
    boolean = false;
    matrix = false;
    if (type >= MTLDataTypeFloat && type <= MTLDataTypeFloat4) {
        components = 1 + type - MTLDataTypeFloat;
        return;
    }
    if (type >= MTLDataTypeInt && type <= MTLDataTypeInt4) {
        components = 1 + type - MTLDataTypeInt;
        integer = true;
        return;
    }
    if (type >= MTLDataTypeUInt && type <= MTLDataTypeUInt4) {
        components = 1 + type - MTLDataTypeUInt;
        integer = true;
        return;
    }
    if (type >= MTLDataTypeBool && type <= MTLDataTypeBool4) {
        components = 1 + type - MTLDataTypeBool;
        integer = true;
        boolean = true;
        return;
    }
    if (type == MTLDataTypeFloat4x4) {
        components = 16;
        matrix = true;
        return;
    }
    throw std::runtime_error("Metal uniforms support float/int/uint/bool vectors "
                             "and float4x4, without nested structures");
}
MTLBlendFactor blendFactor(U value) {
    if (value == gl::SRC_ALPHA)
        return MTLBlendFactorSourceAlpha;
    if (value == gl::ONE_MINUS_SRC_ALPHA)
        return MTLBlendFactorOneMinusSourceAlpha;
    if (value == 1)
        return MTLBlendFactorOne;
    if (value == 0)
        return MTLBlendFactorZero;
    throw std::runtime_error("Unsupported Metal blend factor");
}
} // namespace
struct Metal::Impl {
    static Impl *current;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLCommandBuffer> command = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    CAMetalLayer *layer = nil;
    NSView *view = nil;
    CALayer *previousLayer = nil;
    bool previousWantsLayer = false;
    Texture screen, screenDepth;
    id<MTLTexture> composed = nil;
    id<MTLRenderPipelineState> presentPipeline = nil;
    id<MTLSamplerState> presentSampler = nil;
    std::array<id<MTLBuffer>, 2> zero{};
    std::map<U, Buffer> buffers;
    std::map<U, VertexArray> arrays;
    std::map<U, Texture> textures;
    std::map<U, Framebuffer> framebuffers;
    std::map<U, Shader> shaders;
    std::map<U, Program> programs;
    std::map<unsigned, id<MTLDepthStencilState>> depthStates;
    id<MTLFunction> clearVertex = nil, clearFragment = nil;
    std::map<unsigned, id<MTLRenderPipelineState>> clearPipelines;
    std::map<bool, id<MTLDepthStencilState>> clearDepthStates;
    struct Arena {
        id<MTLBuffer> gpu = nil;
        size_t used = 0;
    };
    std::vector<Arena> arena;
    std::vector<Arena> spareArena;
    struct Submission {
        id<MTLCommandBuffer> command = nil;
        std::vector<Arena> arena;
    };
    std::deque<Submission> pending;
    static constexpr size_t maxInflight = 3;
    size_t peakInflight = 0;
    size_t uniformBudget = 64 * 1024 * 1024, uniformAllocated = 0, uniformUsed = 0;
    std::array<U, 16> boundTextures{};
    U next = 1, buffer = 0, array = 0, framebuffer = 0, program = 0, textureUnit = 0;
    U blendSource = gl::SRC_ALPHA, blendDestination = gl::ONE_MINUS_SRC_ALPHA;
    bool depthTest = false, depthWrite = true, blend = false, scissorTest = false,
         composited = false;
    int width = 0, height = 0, pack = 4, unpack = 4;
    MTLViewport viewport{0, 0, 1, 1, 0, 1};
    std::array<int64_t, 4> scissor{0, 0, 0, 0};
    float clearColor[4] = {0, 0, 0, 0};
    uint64_t translatedPrograms = 0, nativePrograms = 0;
    uint64_t pipelineBuilds = 0, submissions = 0, gpuWaits = 0, synchronousFlushes = 0;
    U handle() {
        require(next < std::numeric_limits<U>::max(), "Metal handle space exhausted");
        return next++;
    }
    static Impl &get() {
        require(current != nullptr, "No Metal device is active");
        return *current;
    }
    void endEncoder() {
        if (encoder) {
            [encoder endEncoding];
            encoder = nil;
        }
    }
    void startCommand() {
        if (!command) {
            reap();
            if (pending.size() >= maxInflight) {
                wait(pending.front().command);
                reap();
            }
            command = [queue commandBuffer];
            require(command != nil, "Cannot allocate Metal command buffer");
        }
    }
    void wait(id<MTLCommandBuffer> buffer) {
        if (buffer.status != MTLCommandBufferStatusCompleted &&
            buffer.status != MTLCommandBufferStatusError) {
            ++gpuWaits;
            [buffer waitUntilCompleted];
        }
    }
    void reap() {
        while (!pending.empty()) {
            auto &front = pending.front();
            auto status = front.command.status;
            if (status != MTLCommandBufferStatusCompleted && status != MTLCommandBufferStatusError)
                break;
            // Reserve before moving: allocation failure must leave the owned
            // snapshots attached to their completed submission for cleanup.
            spareArena.reserve(spareArena.size() + front.arena.size());
            for (auto &chunk : front.arena) {
                chunk.used = 0;
                spareArena.push_back(std::move(chunk));
            }
            auto error = front.command.error;
            pending.pop_front();
            if (error)
                throw std::runtime_error("Metal command buffer: " + message(error));
        }
    }
    void submit() {
        endEncoder();
        if (!command)
            return;
        pending.emplace_back();
        pending.back().command = command;
        pending.back().arena = std::move(arena);
        [command commit];
        ++submissions;
        command = nil;
        uniformUsed = 0;
        peakInflight = std::max(peakInflight, pending.size());
    }
    void flush() {
        if (!command && pending.empty())
            return;
        ++synchronousFlushes;
        submit();
        // Queue order means the last completion covers all preceding work.
        if (!pending.empty())
            wait(pending.back().command);
        reap();
    }
    std::pair<id<MTLBuffer>, size_t> snapshot(const std::vector<unsigned char> &bytes) {
        size_t length = (bytes.size() + 255) & ~size_t(255);
        require(length <= uniformBudget && uniformUsed <= uniformBudget - length,
                "Metal uniform snapshot budget exceeded; increase "
                "renderer.metal.uniform_budget_bytes or reduce draws");
        for (auto &chunk : arena)
            if (length <= chunk.gpu.length - chunk.used) {
                size_t offset = chunk.used;
                chunk.used += length;
                uniformUsed += length;
                std::memcpy(static_cast<unsigned char *>(chunk.gpu.contents) + offset, bytes.data(),
                            bytes.size());
                return {chunk.gpu, offset};
            }
        for (auto it = spareArena.begin(); it != spareArena.end(); ++it)
            if (length <= it->gpu.length) {
                arena.push_back(std::move(*it));
                spareArena.erase(it);
                return snapshot(bytes);
            }
        // The budget covers every retained chunk, including pending commands.
        // Reclaim idle chunks first; wait only under capacity pressure rather
        // than overwriting snapshots that the GPU may still be reading.
        if (length > uniformBudget - uniformAllocated) {
            for (const auto &chunk : spareArena)
                uniformAllocated -= chunk.gpu.length;
            spareArena.clear();
            for (auto it = arena.begin(); it != arena.end();)
                if (!it->used) {
                    uniformAllocated -= it->gpu.length;
                    it = arena.erase(it);
                } else
                    ++it;
            if (length > uniformBudget - uniformAllocated && !pending.empty()) {
                wait(pending.front().command);
                reap();
                return snapshot(bytes);
            }
        }
        size_t capacity =
            std::min(uniformBudget - uniformAllocated, std::max(size_t(1024 * 1024), length));
        require(capacity >= length, "Metal uniform arena allocation exceeds budget");
        Arena chunk;
        chunk.gpu = [device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
        require(chunk.gpu != nil, "Cannot allocate Metal uniform arena");
        arena.push_back(chunk);
        uniformAllocated += capacity;
        return snapshot(bytes);
    }
    std::pair<Texture *, Texture *> target() {
        if (!framebuffer)
            return {&screen, &screenDepth};
        auto &f = framebuffers.at(framebuffer);
        auto c = textures.find(f.color), d = textures.find(f.depth);
        return {c == textures.end() ? nullptr : &c->second,
                d == textures.end() ? nullptr : &d->second};
    }
    MTLRenderPassDescriptor *pass(MTLLoadAction colorLoad = MTLLoadActionLoad,
                                  MTLLoadAction depthLoad = MTLLoadActionLoad) {
        auto targets = target();
        require(targets.first || targets.second, "Incomplete Metal target");
        auto result = [MTLRenderPassDescriptor renderPassDescriptor];
        if (targets.first) {
            auto c = result.colorAttachments[0];
            c.texture = targets.first->gpu;
            c.loadAction = colorLoad;
            c.storeAction = MTLStoreActionStore;
            c.clearColor =
                MTLClearColorMake(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        }
        if (targets.second) {
            result.depthAttachment.texture = targets.second->gpu;
            result.depthAttachment.loadAction = depthLoad;
            result.depthAttachment.storeAction = MTLStoreActionStore;
            result.depthAttachment.clearDepth = 1;
        }
        return result;
    }
    void renderEncoder() {
        if (encoder)
            return;
        startCommand();
        encoder = [command renderCommandEncoderWithDescriptor:pass()];
        require(encoder != nil, "Cannot create Metal render encoder");
    }
    void allocateTexture(Texture &image, int w, int h, bool depth, bool mipmaps,
                         const void *pixels) {
        @autoreleasepool {
            require(w > 0 && h > 0 && w <= 16384 && h <= 16384, "Invalid Metal texture dimensions");
            auto desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:depth ? MTLPixelFormatDepth32Float
                                                         : MTLPixelFormatRGBA8Unorm
                                             width:w
                                            height:h
                                         mipmapped:mipmaps];
            desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            desc.storageMode = depth                     ? MTLStorageModePrivate
                               : device.hasUnifiedMemory ? MTLStorageModeShared
                                                         : MTLStorageModeManaged;
            auto texture = [device newTextureWithDescriptor:desc];
            require(texture != nil, "Cannot allocate Metal texture");
            if (pixels)
                [texture replaceRegion:MTLRegionMake2D(0, 0, w, h)
                           mipmapLevel:0
                             withBytes:pixels
                           bytesPerRow:size_t(w) * 4];
            image.gpu = texture;
            image.width = w;
            image.height = h;
            image.depth = depth;
            image.mipmaps = mipmaps;
            image.sampler = nil;
        }
    }
    Texture &boundTexture() {
        auto found = textures.find(boundTextures.at(textureUnit));
        require(found != textures.end(), "No Metal texture is bound");
        return found->second;
    }
    void sampler(Texture &image) {
        if (image.sampler)
            return;
        auto desc = [MTLSamplerDescriptor new];
        desc.minFilter = (image.minFilter == gl::LINEAR || image.minFilter == 0x2701 ||
                          image.minFilter == 0x2703)
                             ? MTLSamplerMinMagFilterLinear
                             : MTLSamplerMinMagFilterNearest;
        desc.magFilter = image.magFilter == gl::LINEAR ? MTLSamplerMinMagFilterLinear
                                                       : MTLSamplerMinMagFilterNearest;
        desc.mipFilter = image.minFilter >= 0x2700 && image.minFilter <= 0x2703
                             ? (image.minFilter >= 0x2702 ? MTLSamplerMipFilterLinear
                                                          : MTLSamplerMipFilterNearest)
                             : MTLSamplerMipFilterNotMipmapped;
        desc.sAddressMode =
            image.wrapS == 0x2901 ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        desc.tAddressMode =
            image.wrapT == 0x2901 ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        image.sampler = [device newSamplerStateWithDescriptor:desc];
        require(image.sampler != nil, "Cannot allocate Metal sampler");
    }
    id<MTLFunction> compile(const std::string &source, const std::string &entry,
                            const std::string &label) {
        @autoreleasepool {
            auto options = [MTLCompileOptions new];
            options.languageVersion = MTLLanguageVersion2_0;
            options.fastMathEnabled = NO;
            NSError *error = nil;
            auto library = [device newLibraryWithSource:ns(source) options:options error:&error];
            if (!library)
                throw std::runtime_error("Shader " + label + ":\n" + message(error));
            auto function = [library newFunctionWithName:ns(entry)];
            require(function != nil, "Shader " + label + ": entry point not found: " + entry);
            return function;
        }
    }
    MTLVertexDescriptor *placeholder(Stage &stage) {
        auto result = [MTLVertexDescriptor vertexDescriptor];
        for (MTLAttribute *input in stage.function.stageInputAttributes) {
            if (!input.active)
                continue;
            require(input.attributeIndex < 16, "Metal attributes must be 0..15");
            size_t n = 0;
            bool integer = false, boolean = false, matrix = false;
            dataShape(input.attributeType, n, integer, boolean, matrix);
            require(!matrix && !boolean, "Unsupported Metal vertex attribute type");
            auto a = result.attributes[input.attributeIndex];
            a.format = format(integer ? (input.attributeType >= MTLDataTypeUInt &&
                                                 input.attributeType <= MTLDataTypeUInt4
                                             ? gl::UNSIGNED_INT
                                             : 0x1404)
                                      : gl::FLOAT,
                              int(n));
            a.bufferIndex = 15 + input.attributeIndex;
            result.layouts[a.bufferIndex].stride = 16;
        }
        return result;
    }
    MTLRenderPipelineDescriptor *pipelineDescriptor(Program &p, MTLVertexDescriptor *vertex,
                                                    bool enabledBlend, U source, U destination,
                                                    bool depth) {
        auto desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = p.vertex.function;
        desc.fragmentFunction = p.fragment.function;
        desc.vertexDescriptor = vertex;
        auto color = desc.colorAttachments[0];
        color.pixelFormat = MTLPixelFormatRGBA8Unorm;
        color.blendingEnabled = enabledBlend;
        color.sourceRGBBlendFactor = color.sourceAlphaBlendFactor = blendFactor(source);
        color.destinationRGBBlendFactor = color.destinationAlphaBlendFactor =
            blendFactor(destination);
        color.rgbBlendOperation = color.alphaBlendOperation = MTLBlendOperationAdd;
        desc.depthAttachmentPixelFormat =
            depth ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;
        return desc;
    }
    void reflect(Stage &stage, NSArray<MTLArgument *> *arguments,
                 const std::map<std::string, std::string> &aliases,
                 const std::map<std::string, unsigned> &shapes) {
        auto name = [&](NSString *value) {
            std::string text = value.UTF8String;
            auto it = aliases.find(text);
            return it == aliases.end() ? text : it->second;
        };
        for (MTLArgument *arg in arguments) {
            if (!arg.active)
                continue;
            if (arg.type == MTLArgumentTypeBuffer) {
                if (arg.index >= 15) {
                    require(stage.function.functionType == MTLFunctionTypeVertex,
                            "Metal uniform buffers must use slots 0..14");
                    require([arg.name hasPrefix:@"vertexBuffer."],
                            "Reserved Metal vertex slots cannot contain uniform values");
                    continue; // Vertex fetch buffers are reserved at 15..30.
                }
                require(arg.bufferDataSize > 0 && arg.bufferDataSize <= 65536,
                        "Metal uniform buffer exceeds 64 KiB draw API limit");
                Constants cb;
                cb.slot = arg.index;
                cb.bytes.resize(arg.bufferDataSize);
                auto add = [&](NSString *field, size_t offset, MTLDataType type,
                               MTLArrayType *array) {
                    Variable var;
                    var.name = name(field);
                    var.offset = offset;
                    if (type == MTLDataTypeArray) {
                        require(array != nil, "Missing Metal array reflection");
                        var.elements = array.arrayLength;
                        var.stride = array.stride;
                        type = array.elementType;
                    }
                    dataShape(type, var.components, var.integer, var.boolean, var.matrix);
                    auto shape = shapes.find(var.name);
                    if (shape != shapes.end())
                        var.components = shape->second;
                    if (!var.stride)
                        var.stride = var.components * (var.boolean ? 1 : 4);
                    require(var.elements > 0 &&
                                var.offset + var.components * (var.boolean ? 1 : 4) <=
                                    cb.bytes.size(),
                            "Invalid Metal uniform layout");
                    cb.variables.push_back(std::move(var));
                };
                if (arg.bufferDataType == MTLDataTypeStruct) {
                    for (MTLStructMember *member in arg.bufferStructType.members)
                        add(member.name, member.offset, member.dataType, member.arrayType);
                } else
                    add(arg.name, 0, arg.bufferDataType, nullptr);
                stage.constants.push_back(std::move(cb));
            } else if (arg.type == MTLArgumentTypeTexture || arg.type == MTLArgumentTypeSampler) {
                require(arg.arrayLength == 1 && arg.index < 16,
                        "Metal draw API supports individual resources at slots 0..15");
                if (arg.type == MTLArgumentTypeTexture)
                    require(arg.textureType == MTLTextureType2D,
                            "Metal draw API supports Texture2D only");
                stage.resources.push_back({arg.type == MTLArgumentTypeSampler
                                               ? std::string(arg.name.UTF8String)
                                               : name(arg.name),
                                           arg.index, arg.type == MTLArgumentTypeSampler});
            } else
                throw std::runtime_error("Metal argument is outside Forge's draw API");
        }
    }
    void buildProgram(Program &output, const std::string &vertex, const std::string &fragment,
                      const std::string &label, bool native, const std::string &ve = "main0",
                      const std::string &fe = "main0") {
        @autoreleasepool {
            Program p;
            p.shaders = output.shaders;
            auto translated = native ? TranslatedShaders{vertex, fragment}
                                     : translateGlslToMsl(vertex, fragment, label);
            p.vertex.function = compile(translated.vertex, ve, label + ".vertex");
            p.fragment.function = compile(translated.fragment, fe, label + ".fragment");
            require(p.vertex.function.functionType == MTLFunctionTypeVertex &&
                        p.fragment.function.functionType == MTLFunctionTypeFragment,
                    "Metal override requires vertex/fragment entry points");
            NSError *error = nil;
            MTLRenderPipelineReflection *reflection = nil;
            auto probe = [device
                newRenderPipelineStateWithDescriptor:pipelineDescriptor(p, placeholder(p.vertex),
                                                                        false, gl::SRC_ALPHA,
                                                                        gl::ONE_MINUS_SRC_ALPHA,
                                                                        true)
                                             options:MTLPipelineOptionArgumentInfo |
                                                     MTLPipelineOptionBufferTypeInfo
                                          reflection:&reflection
                                               error:&error];
            if (!probe)
                throw std::runtime_error("Shader " + label + " linking:\n" + message(error));
            reflect(p.vertex, reflection.vertexArguments, translated.vertexUniforms,
                    translated.vertexComponents);
            reflect(p.fragment, reflection.fragmentArguments, translated.fragmentUniforms,
                    translated.fragmentComponents);
            p.linked = true;
            output = std::move(p);
            if (native)
                ++nativePrograms;
            else
                ++translatedPrograms;
        }
    }
    static std::pair<std::string, size_t> uniformName(const std::string &text) {
        auto pos = text.find('[');
        if (pos == std::string::npos || text.back() != ']')
            return {text, 0};
        auto index = text.substr(pos + 1, text.size() - pos - 2);
        if (index.empty() || index.find_first_not_of("0123456789") != std::string::npos)
            return {text, 0};
        return {text.substr(0, pos), std::stoull(index)};
    }
    I location(U handle, const std::string &name) {
        auto &p = programs.at(handle);
        auto it = p.locationIndex.find(name);
        if (it != p.locationIndex.end())
            return it->second;
        auto parsed = uniformName(name);
        bool exists = false;
        for (auto stage : {&p.vertex, &p.fragment}) {
            for (auto &cb : stage->constants)
                for (auto &var : cb.variables)
                    if (var.name == parsed.first && parsed.second < var.elements)
                        exists = true;
            for (auto &r : stage->resources)
                if (!r.sampler && r.name == name)
                    exists = true;
        }
        if (!exists)
            return -1;
        I result = I(p.locations.size());
        p.locations.push_back(name);
        p.locationIndex[name] = result;
        return result;
    }
    void uniform(I location, I count, size_t components, const void *data, bool integer,
                 bool matrix = false) {
        if (location < 0)
            return;
        auto &p = programs.at(program);
        require(size_t(location) < p.locations.size() && count > 0 && data,
                "Invalid Metal uniform update");
        auto name = p.locations[location];
        auto parsed = uniformName(name);
        for (auto stage : {&p.vertex, &p.fragment}) {
            if (integer && count == 1 && components == 1)
                for (auto &r : stage->resources)
                    if (!r.sampler && r.name == name) {
                        int unit = *static_cast<const int *>(data);
                        require(unit >= 0 && unit < 16, "Texture unit must be 0..15");
                        p.textureUnits[name] = unit;
                    }
            for (auto &cb : stage->constants)
                for (auto &var : cb.variables) {
                    if (var.name != parsed.first || parsed.second >= var.elements)
                        continue;
                    require(size_t(count) <= var.elements - parsed.second &&
                                var.components == components && var.integer == integer &&
                                var.matrix == matrix,
                            "Metal uniform type/shape mismatch: " + name);
                    for (size_t i = 0; i < size_t(count); ++i) {
                        size_t offset = var.offset + (parsed.second + i) * var.stride,
                               bytes = components * (var.boolean ? 1 : 4);
                        require(offset <= cb.bytes.size() && bytes <= cb.bytes.size() - offset,
                                "Metal uniform range overflow");
                        if (var.boolean)
                            for (size_t c = 0; c < components; ++c)
                                cb.bytes[offset + c] =
                                    static_cast<const int *>(data)[i * components + c] != 0;
                        else
                            std::memcpy(cb.bytes.data() + offset,
                                        static_cast<const unsigned char *>(data) +
                                            i * components * 4,
                                        bytes);
                    }
                }
        }
    }
    void stageResources(Program &p, Stage &stage, bool vertex) {
        for (auto &cb : stage.constants) {
            auto copy = snapshot(cb.bytes);
            if (vertex)
                [encoder setVertexBuffer:copy.first offset:copy.second atIndex:cb.slot];
            else
                [encoder setFragmentBuffer:copy.first offset:copy.second atIndex:cb.slot];
        }
        auto output = target();
        for (auto &r : stage.resources) {
            auto name = r.name;
            if (r.sampler) {
                const std::string suffix = "_sampler";
                if (name.size() > suffix.size() &&
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                    name.resize(name.size() - suffix.size());
                if (!name.empty() && name.front() == '_' && !p.textureUnits.count(name))
                    name.erase(0, 1);
                auto texture = std::find_if(stage.resources.begin(), stage.resources.end(),
                                            [&](const Resource &resource) {
                                                return !resource.sampler && resource.name == name;
                                            });
                if (texture == stage.resources.end()) {
                    texture = std::find_if(stage.resources.begin(), stage.resources.end(),
                                           [&](const Resource &resource) {
                                               return !resource.sampler && resource.slot == r.slot;
                                           });
                    if (texture != stage.resources.end())
                        name = texture->name;
                }
            }
            auto unit = p.textureUnits.count(name) ? p.textureUnits.at(name) : 0;
            auto found = textures.find(boundTextures.at(unit));
            Texture *texture = found == textures.end() ? nullptr : &found->second;
            require(!texture || (texture != output.first && texture != output.second),
                    "A shader cannot sample its active Metal render target");
            if (r.sampler) {
                if (texture)
                    sampler(*texture);
                if (vertex)
                    [encoder setVertexSamplerState:texture ? texture->sampler : nil atIndex:r.slot];
                else
                    [encoder setFragmentSamplerState:texture ? texture->sampler : nil
                                             atIndex:r.slot];
            } else {
                if (vertex)
                    [encoder setVertexTexture:texture ? texture->gpu : nil atIndex:r.slot];
                else
                    [encoder setFragmentTexture:texture ? texture->gpu : nil atIndex:r.slot];
            }
        }
    }
    MTLVertexDescriptor *vertexDescriptor(Program &p, VertexArray &vao, bool bind) {
        auto result = [MTLVertexDescriptor vertexDescriptor];
        for (MTLAttribute *input in p.vertex.function.stageInputAttributes) {
            if (!input.active)
                continue;
            auto index = input.attributeIndex;
            require(index < 16, "Metal attributes must be 0..15");
            auto attr = vao.attributes[index];
            id<MTLBuffer> gpu = nil;
            if (!attr.enabled) {
                size_t n = 0;
                bool integer = false, boolean = false, matrix = false;
                dataShape(input.attributeType, n, integer, boolean, matrix);
                attr.size = 4;
                attr.type = integer ? (input.attributeType >= MTLDataTypeUInt &&
                                               input.attributeType <= MTLDataTypeUInt4
                                           ? gl::UNSIGNED_INT
                                           : 0x1404)
                                    : gl::FLOAT;
                attr.stride = 16;
                attr.offset = 0;
                gpu = zero[integer ? 1 : 0];
            } else if (bind) {
                auto &b = buffers.at(attr.buffer);
                if (b.dirty) {
                    b.gpu = [device newBufferWithBytes:b.bytes.data()
                                                length:b.bytes.size()
                                               options:MTLResourceStorageModeShared];
                    require(b.gpu != nil, "Cannot upload Metal vertex buffer");
                    b.dirty = false;
                }
                gpu = b.gpu;
            }
            auto a = result.attributes[index];
            a.format = format(attr.type, attr.size);
            a.offset = attr.offset;
            a.bufferIndex = 15 + index;
            auto layout = result.layouts[a.bufferIndex];
            layout.stride = attr.stride;
            layout.stepFunction = !attr.enabled  ? MTLVertexStepFunctionConstant
                                  : attr.divisor ? MTLVertexStepFunctionPerInstance
                                                 : MTLVertexStepFunctionPerVertex;
            layout.stepRate = !attr.enabled ? 0 : attr.divisor ? attr.divisor : 1;
            if (bind)
                [encoder setVertexBuffer:gpu offset:0 atIndex:a.bufferIndex];
        }
        return result;
    }
    Program::Layout vertexLayout(MTLVertexDescriptor *descriptor) {
        Program::Layout result{};
        for (size_t index = 0; index < result.size(); ++index) {
            auto attr = descriptor.attributes[index];
            if (attr.format == MTLVertexFormatInvalid)
                continue;
            auto layout = descriptor.layouts[attr.bufferIndex];
            result[index] = {size_t(attr.format), attr.offset, layout.stride,
                             size_t(layout.stepFunction), layout.stepRate};
        }
        return result;
    }
    void draw(I first, I count, I instances) {
        require(first >= 0 && count >= 0 && instances >= 0, "Invalid Metal draw range");
        if (!count || !instances)
            return;
        auto &p = programs.at(program);
        require(p.linked, "Unlinked Metal program");
        auto &vao = arrays.at(array);
        renderEncoder();
        auto targets = target();
        bool depth = targets.second != nullptr;
        auto vertex = vertexDescriptor(p, vao, true);
        auto key = Program::Key{vertexLayout(vertex), blend, blend ? blendSource : gl::SRC_ALPHA,
                                blend ? blendDestination : gl::ONE_MINUS_SRC_ALPHA, depth};
        auto found = p.pipelines.find(key);
        if (found == p.pipelines.end()) {
            NSError *error = nil;
            auto pipeline = [device
                newRenderPipelineStateWithDescriptor:pipelineDescriptor(p, vertex, blend,
                                                                        blendSource,
                                                                        blendDestination, depth)
                                               error:&error];
            if (!pipeline)
                throw std::runtime_error("Metal draw pipeline: " + message(error));
            ++pipelineBuilds;
            // Retain useful layouts after VAO deletion without permitting
            // unbounded growth from extensions that continually change layout.
            if (p.pipelines.size() >= 256) {
                auto oldest = std::min_element(p.pipelines.begin(), p.pipelines.end(),
                                               [](const auto &a, const auto &b) {
                                                   return a.second.lastUse < b.second.lastUse;
                                               });
                p.pipelines.erase(oldest);
            }
            found = p.pipelines.emplace(std::move(key), Program::Pipeline{pipeline, 0}).first;
        }
        found->second.lastUse = ++p.pipelineClock;
        [encoder setRenderPipelineState:found->second.state];
        unsigned depthKey = unsigned(depthTest) | (unsigned(depthWrite) << 1);
        auto &state = depthStates[depthKey];
        if (!state) {
            auto desc = [MTLDepthStencilDescriptor new];
            desc.depthCompareFunction =
                depthTest ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
            desc.depthWriteEnabled = depthTest && depthWrite;
            state = [device newDepthStencilStateWithDescriptor:desc];
            require(state != nil, "Cannot allocate Metal depth state");
        }
        [encoder setDepthStencilState:state];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setViewport:viewport];
        auto output = targets.first ? targets.first : targets.second;
        int64_t left = scissorTest ? std::clamp<int64_t>(scissor[0], 0, output->width) : 0,
                top = scissorTest ? std::clamp<int64_t>(scissor[1], 0, output->height) : 0;
        int64_t right = scissorTest ? std::clamp<int64_t>(scissor[0] + scissor[2], 0, output->width)
                                    : output->width,
                bottom = scissorTest
                             ? std::clamp<int64_t>(scissor[1] + scissor[3], 0, output->height)
                             : output->height;
        if (right <= left || bottom <= top)
            return;
        [encoder setScissorRect:MTLScissorRect{size_t(left), size_t(top), size_t(right - left),
                                               size_t(bottom - top)}];
        stageResources(p, p.vertex, true);
        stageResources(p, p.fragment, false);
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:first
                    vertexCount:count
                  instanceCount:instances];
    }
    void clear(U mask) {
        endEncoder();
        startCommand();
        if (!scissorTest) {
            encoder = [command
                renderCommandEncoderWithDescriptor:pass((mask & gl::COLOR) ? MTLLoadActionClear
                                                                           : MTLLoadActionLoad,
                                                        ((mask & gl::DEPTH) && depthWrite)
                                                            ? MTLLoadActionClear
                                                            : MTLLoadActionLoad)];
            require(encoder != nil, "Cannot clear Metal target");
            return;
        }
        auto targets = target();
        auto output = targets.first ? targets.first : targets.second;
        require(output != nullptr, "Incomplete Metal clear target");
        int64_t left = std::clamp<int64_t>(scissor[0], 0, output->width),
                top = std::clamp<int64_t>(scissor[1], 0, output->height);
        int64_t right = std::clamp<int64_t>(scissor[0] + scissor[2], 0, output->width),
                bottom = std::clamp<int64_t>(scissor[1] + scissor[3], 0, output->height);
        if (right <= left || bottom <= top)
            return;
        if (!clearVertex) {
            const std::string source = R"(#include <metal_stdlib>
using namespace metal;
vertex float4 clear_vertex(uint id [[vertex_id]]){float2 uv=float2((id<<1)&2,id&2);return float4(uv*2-1,1,1);}
fragment float4 clear_fragment(constant float4& color [[buffer(0)]]){return color;}
)";
            clearVertex = compile(source, "clear_vertex", "Clear.vertex");
            clearFragment = compile(source, "clear_fragment", "Clear.fragment");
        }
        bool color = (mask & gl::COLOR) && targets.first,
             depth = (mask & gl::DEPTH) && depthWrite && targets.second;
        unsigned key = unsigned(color) | (unsigned(targets.second != nullptr) << 1);
        auto &pipeline = clearPipelines[key];
        if (!pipeline) {
            auto desc = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction = clearVertex;
            desc.fragmentFunction = clearFragment;
            desc.colorAttachments[0].pixelFormat =
                targets.first ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatInvalid;
            desc.colorAttachments[0].writeMask =
                color ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
            desc.depthAttachmentPixelFormat =
                targets.second ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;
            NSError *error = nil;
            pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            require(pipeline != nil, "Metal clear pipeline: " + message(error));
        }
        auto &ds = clearDepthStates[depth];
        if (!ds) {
            auto desc = [MTLDepthStencilDescriptor new];
            desc.depthCompareFunction = MTLCompareFunctionAlways;
            desc.depthWriteEnabled = depth;
            ds = [device newDepthStencilStateWithDescriptor:desc];
            require(ds != nil, "Cannot create Metal clear depth state");
        }
        renderEncoder();
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:ds];
        [encoder setCullMode:MTLCullModeNone];
        [encoder
            setViewport:MTLViewport{0, 0, double(output->width), double(output->height), 0, 1}];
        [encoder setScissorRect:MTLScissorRect{size_t(left), size_t(top), size_t(right - left),
                                               size_t(bottom - top)}];
        [encoder setFragmentBytes:clearColor length:16 atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    void compose() {
        if (composited)
            return;
        endEncoder();
        startCommand();
        auto desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        if (!composed || composed.width != size_t(width) || composed.height != size_t(height))
            composed = [device newTextureWithDescriptor:desc];
        require(composed != nil, "Cannot allocate Metal presentation target");
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = composed;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        auto e = [command renderCommandEncoderWithDescriptor:pass];
        require(e != nil, "Cannot compose Metal frame");
        [e setRenderPipelineState:presentPipeline];
        [e setFragmentTexture:screen.gpu atIndex:0];
        [e setFragmentSamplerState:presentSampler atIndex:0];
        [e setCullMode:MTLCullModeNone];
        [e setViewport:MTLViewport{0, 0, double(width), double(height), 0, 1}];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [e endEncoding];
        composited = true;
    }
    void install();
};
Metal::Impl *Metal::Impl::current = nullptr;
namespace metalCommands {
using Device = Metal::Impl;
void Viewport(I x, I y, I w, I h) {
    require(w >= 0 && h >= 0, "Invalid Metal viewport");
    Device::get().viewport = {double(x), double(y), double(w), double(h), 0, 1};
}
void Scissor(I x, I y, I w, I h) {
    require(w >= 0 && h >= 0, "Invalid Metal scissor");
    Device::get().scissor = {x, y, w, h};
}
void ClearColor(float r, float g, float b, float a) {
    auto &d = Device::get();
    d.clearColor[0] = r;
    d.clearColor[1] = g;
    d.clearColor[2] = b;
    d.clearColor[3] = a;
}
void Clear(U mask) { Device::get().clear(mask); }

void enabled(U flag, bool value) {
    auto &d = Device::get();
    if (flag == gl::DEPTH_TEST)
        d.depthTest = value;
    else if (flag == gl::BLEND)
        d.blend = value;
    else if (flag == gl::SCISSOR_TEST)
        d.scissorTest = value;
    else
        throw std::runtime_error("Unsupported Metal capability");
}
void Enable(U value) { enabled(value, true); }
void Disable(U value) { enabled(value, false); }
void DepthMask(gl::B value) { Device::get().depthWrite = value != 0; }
void BlendFunc(U source, U destination) {
    blendFactor(source);
    blendFactor(destination);
    auto &d = Device::get();
    d.blendSource = source;
    d.blendDestination = destination;
}
void GenBuffers(I count, U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        names[i] = d.handle();
        d.buffers.emplace(names[i], Buffer{});
    }
}
void BindBuffer(U target, U name) {
    require(target == gl::ARRAY_BUFFER, "Metal supports vertex buffers only");
    auto &d = Device::get();
    require(!name || d.buffers.count(name), "Unknown Metal buffer");
    d.buffer = name;
}
void BufferData(U target, gl::S size, const void *data, U) {
    require(target == gl::ARRAY_BUFFER && size >= 0, "Invalid Metal buffer allocation");
    auto &b = Device::get().buffers.at(Device::get().buffer);
    Buffer next;
    next.bytes.resize(size_t(size));
    if (data && size)
        std::memcpy(next.bytes.data(), data, size_t(size));
    b = std::move(next);
}
void BufferSubData(U target, gl::S offset, gl::S size, const void *data) {
    require(target == gl::ARRAY_BUFFER && offset >= 0 && size >= 0 && (!size || data),
            "Invalid Metal buffer update");
    auto &b = Device::get().buffers.at(Device::get().buffer);
    require(size_t(offset) <= b.bytes.size() && size_t(size) <= b.bytes.size() - size_t(offset),
            "Metal buffer update exceeds capacity");
    if (size)
        std::memcpy(b.bytes.data() + offset, data, size_t(size));
    b.dirty = true;
}
void DeleteBuffers(I count, const U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        d.buffers.erase(names[i]);
        if (d.buffer == names[i])
            d.buffer = 0;
    }
}
void GenVertexArrays(I count, U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        names[i] = d.handle();
        d.arrays.emplace(names[i], VertexArray{});
    }
}
void BindVertexArray(U name) {
    auto &d = Device::get();
    require(!name || d.arrays.count(name), "Unknown Metal vertex array");
    d.array = name;
}
void DeleteVertexArrays(I count, const U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        d.arrays.erase(names[i]);
        if (d.array == names[i])
            d.array = 0;
    }
}
void attribute(U index, I size, U type, I stride, const void *offset) {
    format(type, size);
    require(stride >= 0, "Invalid Metal attribute stride");
    auto &d = Device::get();
    auto &v = d.arrays.at(d.array);
    auto &a = v.attributes.at(index);
    a.buffer = d.buffer;
    a.size = size;
    a.type = type;
    a.stride = stride ? size_t(stride) : size_t(size) * 4;
    a.offset = reinterpret_cast<uintptr_t>(offset);
}
void EnableVertexAttribArray(U index) {
    auto &d = Device::get();
    auto &v = d.arrays.at(d.array);
    v.attributes.at(index).enabled = true;
}
void DisableVertexAttribArray(U index) {
    auto &d = Device::get();
    auto &v = d.arrays.at(d.array);
    v.attributes.at(index).enabled = false;
}
void VertexAttribPointer(U index, I size, U type, gl::B normalized, I stride, const void *offset) {
    require(!normalized || type == gl::FLOAT,
            "Normalized integer Metal attributes need an extension");
    attribute(index, size, type, stride, offset);
}
void VertexAttribIPointer(U index, I size, U type, I stride, const void *offset) {
    attribute(index, size, type, stride, offset);
}
void VertexAttribDivisor(U index, U divisor) {
    auto &d = Device::get();
    auto &v = d.arrays.at(d.array);
    v.attributes.at(index).divisor = divisor;
}
U CreateShader(U kind) {
    auto &d = Device::get();
    auto name = d.handle();
    Shader s;
    s.kind = kind;
    d.shaders.emplace(name, s);
    return name;
}
void ShaderSource(U name, I count, const char *const *sources, const I *lengths) {
    auto &s = Device::get().shaders.at(name);
    s.source.clear();
    for (I i = 0; i < count; ++i) {
        require(sources[i] != nullptr, "Null Metal shader source");
        if (lengths && lengths[i] >= 0)
            s.source.append(sources[i], size_t(lengths[i]));
        else
            s.source += sources[i];
    }
    s.compiled = false;
}
void CompileShader(U name) {
    auto &s = Device::get().shaders.at(name);
    s.compiled = !s.source.empty();
    s.error = s.compiled ? "" : "Empty shader source";
}
void GetShaderiv(U name, U field, I *value) {
    require(field == gl::COMPILE_STATUS, "Unsupported shader query");
    *value = Device::get().shaders.at(name).compiled;
}
void GetShaderInfoLog(U name, I size, I *written, char *output) {
    logText(Device::get().shaders.at(name).error, size, written, output);
}
void DeleteShader(U name) { Device::get().shaders.erase(name); }
U CreateProgram() {
    auto &d = Device::get();
    auto name = d.handle();
    d.programs.emplace(name, Program{});
    return name;
}
void AttachShader(U name, U shader) { Device::get().programs.at(name).shaders.push_back(shader); }
void LinkProgram(U name) {
    auto &d = Device::get();
    auto &p = d.programs.at(name);
    try {
        std::string vertex, fragment;
        for (auto handle : p.shaders) {
            auto &shader = d.shaders.at(handle);
            require(shader.compiled, shader.error);
            if (shader.kind == gl::VERTEX_SHADER)
                vertex = shader.source;
            else if (shader.kind == gl::FRAGMENT_SHADER)
                fragment = shader.source;
            else
                throw std::runtime_error("Metal supports vertex/fragment stages");
        }
        require(!vertex.empty() && !fragment.empty(), "A program requires both shader stages");
        d.buildProgram(p, vertex, fragment, "GLSL program", false);
    } catch (const std::exception &error) {
        p.linked = false;
        p.error = error.what();
    }
}
void GetProgramiv(U name, U field, I *value) {
    require(field == gl::LINK_STATUS, "Unsupported Metal program query");
    *value = Device::get().programs.at(name).linked;
}
void GetProgramInfoLog(U name, I size, I *written, char *output) {
    logText(Device::get().programs.at(name).error, size, written, output);
}
void DeleteProgram(U name) {
    auto &d = Device::get();
    d.programs.erase(name);
    if (d.program == name)
        d.program = 0;
}
void UseProgram(U name) {
    auto &d = Device::get();
    require(!name || d.programs.at(name).linked, "Unlinked Metal program");
    d.program = name;
}
I GetUniformLocation(U program, const char *name) { return Device::get().location(program, name); }
void Uniform1i(I loc, I value) { Device::get().uniform(loc, 1, 1, &value, true); }
void Uniform1f(I loc, float value) { Device::get().uniform(loc, 1, 1, &value, false); }
void Uniform2fv(I loc, I count, const float *v) { Device::get().uniform(loc, count, 2, v, false); }
void Uniform3fv(I loc, I count, const float *v) { Device::get().uniform(loc, count, 3, v, false); }
void Uniform4fv(I loc, I count, const float *v) { Device::get().uniform(loc, count, 4, v, false); }
void UniformMatrix4fv(I loc, I count, gl::B transpose, const float *v) {
    require(!transpose, "Forge matrices use column-major input");
    Device::get().uniform(loc, count, 16, v, false, true);
}
void DrawArrays(U kind, I first, I count) {
    require(kind == gl::TRIANGLES, "Metal draws triangles");
    Device::get().draw(first, count, 1);
}
void DrawArraysInstanced(U kind, I first, I count, I instances) {
    require(kind == gl::TRIANGLES, "Metal draws triangles");
    Device::get().draw(first, count, instances);
}
void GenTextures(I count, U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        names[i] = d.handle();
        d.textures.emplace(names[i], Texture{});
    }
}
void ActiveTexture(U value) {
    require(value >= gl::TEXTURE0 && value < gl::TEXTURE0 + 16, "Metal texture unit must be 0..15");
    Device::get().textureUnit = value - gl::TEXTURE0;
}
void BindTexture(U target, U name) {
    auto &d = Device::get();
    require(target == gl::TEXTURE_2D && (!name || d.textures.count(name)), "Unknown Metal texture");
    d.boundTextures.at(d.textureUnit) = name;
}
std::vector<unsigned char> pixels(const void *input, I w, I h, U format, I alignment) {
    require(format == gl::RGB || format == gl::RGBA, "Metal texture input must be RGB/RGBA8");
    size_t channels = format == gl::RGBA ? 4 : 3,
           stride = (size_t(w) * channels + alignment - 1) & ~size_t(alignment - 1);
    std::vector<unsigned char> output(size_t(w) * size_t(h) * 4);
    if (input)
        for (I y = 0; y < h; ++y)
            for (I x = 0; x < w; ++x) {
                auto source = static_cast<const unsigned char *>(input) + size_t(y) * stride +
                              size_t(x) * channels;
                auto dest = output.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                std::memcpy(dest, source, channels);
                if (channels == 3)
                    dest[3] = 255;
            }
    return output;
}
void TexImage2D(U target, I level, I, I w, I h, I border, U format, U type, const void *input) {
    require(target == gl::TEXTURE_2D && !level && !border && w > 0 && h > 0 && w <= 16384 &&
                h <= 16384,
            "Invalid Metal texture allocation");
    auto &d = Device::get();
    // Replacing storage creates a new version. Encoded/submitted commands
    // retain the previous texture; no CPU mutation of that version occurs.
    d.endEncoder();
    bool depth = format == gl::DEPTH_COMPONENT;
    if (depth) {
        require(!input && type == gl::UNSIGNED_INT, "Depth textures are render targets only");
        d.allocateTexture(d.boundTexture(), w, h, true, false, nullptr);
    } else {
        require(type == gl::UNSIGNED_BYTE, "Metal color textures require uint8");
        auto data = pixels(input, w, h, format, d.unpack);
        d.allocateTexture(d.boundTexture(), w, h, false, false, input ? data.data() : nullptr);
    }
}
void TexSubImage2D(U target, I level, I x, I y, I w, I h, U format, U type, const void *input) {
    auto &d = Device::get();
    auto &texture = d.boundTexture();
    require(target == gl::TEXTURE_2D && !level && !texture.depth && type == gl::UNSIGNED_BYTE &&
                input && x >= 0 && y >= 0 && w >= 0 && h >= 0 && int64_t(x) + w <= texture.width &&
                int64_t(y) + h <= texture.height,
            "Invalid Metal texture update");
    if (!w || !h)
        return;
    auto data = pixels(input, w, h, format, d.unpack);
    size_t stride = (size_t(w) * 4 + 255) & ~size_t(255);
    auto staging = [d.device newBufferWithLength:stride * size_t(h)
                                         options:MTLResourceStorageModeShared];
    require(staging != nil, "Cannot allocate Metal texture upload buffer");
    for (I row = 0; row < h; ++row)
        std::memcpy(static_cast<unsigned char *>(staging.contents) + size_t(row) * stride,
                    data.data() + size_t(row) * size_t(w) * 4, size_t(w) * 4);
    d.endEncoder();
    d.startCommand();
    auto blit = [d.command blitCommandEncoder];
    require(blit != nil, "Cannot create Metal texture upload encoder");
    [blit copyFromBuffer:staging
               sourceOffset:0
          sourceBytesPerRow:stride
        sourceBytesPerImage:stride * size_t(h)
                 sourceSize:MTLSizeMake(w, h, 1)
                  toTexture:texture.gpu
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake(x, y, 0)];
    [blit endEncoding];
}
void TexParameteri(U target, U field, I value) {
    require(target == gl::TEXTURE_2D, "Metal uses Texture2D");
    auto &t = Device::get().boundTexture();
    if (field == gl::TEXTURE_MIN_FILTER)
        t.minFilter = value;
    else if (field == gl::TEXTURE_MAG_FILTER)
        t.magFilter = value;
    else if (field == gl::TEXTURE_WRAP_S)
        t.wrapS = value;
    else if (field == gl::TEXTURE_WRAP_T)
        t.wrapT = value;
    else
        throw std::runtime_error("Unsupported Metal texture parameter");
    t.sampler = nil;
}
void GenerateMipmap(U target) {
    require(target == gl::TEXTURE_2D, "Metal uses Texture2D");
    auto &d = Device::get();
    auto &image = d.boundTexture();
    require(!image.depth, "Depth mipmaps are unsupported");
    d.endEncoder();
    d.startCommand();
    if (!image.mipmaps) {
        Texture replacement;
        replacement.minFilter = image.minFilter;
        replacement.magFilter = image.magFilter;
        replacement.wrapS = image.wrapS;
        replacement.wrapT = image.wrapT;
        d.allocateTexture(replacement, image.width, image.height, false, true, nullptr);
        auto blit = [d.command blitCommandEncoder];
        [blit copyFromTexture:image.gpu
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(image.width, image.height, 1)
                    toTexture:replacement.gpu
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        image = std::move(replacement);
    }
    auto blit = [d.command blitCommandEncoder];
    [blit generateMipmapsForTexture:image.gpu];
    [blit endEncoding];
}
void DeleteTextures(I count, const U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        d.textures.erase(names[i]);
        for (auto &bound : d.boundTextures)
            if (bound == names[i])
                bound = 0;
    }
}
void PixelStorei(U field, I value) {
    require(value == 1 || value == 2 || value == 4 || value == 8, "Invalid pixel alignment");
    auto &d = Device::get();
    if (field == gl::PACK_ALIGNMENT)
        d.pack = value;
    else if (field == gl::UNPACK_ALIGNMENT)
        d.unpack = value;
    else
        throw std::runtime_error("Unsupported Metal pixel storage option");
}
void GetIntegerv(U field, I *value) {
    require(field == gl::MAX_TEXTURE_SIZE, "Unsupported Metal device query");
    *value = 16384;
}
void GenFramebuffers(I count, U *names) {
    auto &d = Device::get();
    for (I i = 0; i < count; ++i) {
        names[i] = d.handle();
        d.framebuffers.emplace(names[i], Framebuffer{});
    }
}
void BindFramebuffer(U target, U name) {
    auto &d = Device::get();
    require(target == gl::FRAMEBUFFER && (!name || d.framebuffers.count(name)),
            "Unknown Metal framebuffer");
    d.endEncoder();
    d.framebuffer = name;
}
void FramebufferTexture2D(U target, U attachment, U textureTarget, U name, I level) {
    require(target == gl::FRAMEBUFFER && textureTarget == gl::TEXTURE_2D && !level,
            "Invalid Metal framebuffer attachment");
    auto &d = Device::get();
    auto &f = d.framebuffers.at(d.framebuffer);
    if (attachment == gl::COLOR_ATTACHMENT0)
        f.color = name;
    else if (attachment == gl::DEPTH_ATTACHMENT)
        f.depth = name;
    else
        throw std::runtime_error("Unsupported Metal framebuffer attachment");
}
U CheckFramebufferStatus(U target) {
    require(target == gl::FRAMEBUFFER, "Unsupported framebuffer target");
    auto pair = Device::get().target();
    return pair.first && pair.second && pair.first->gpu && pair.second->gpu && !pair.first->depth &&
                   pair.second->depth && pair.first->width == pair.second->width &&
                   pair.first->height == pair.second->height
               ? gl::FRAMEBUFFER_COMPLETE
               : 0x8CD6;
}
void DeleteFramebuffers(I count, const U *names) {
    auto &d = Device::get();
    d.endEncoder();
    for (I i = 0; i < count; ++i) {
        d.framebuffers.erase(names[i]);
        if (d.framebuffer == names[i])
            d.framebuffer = 0;
    }
}
void ReadPixels(I x, I y, I w, I h, U format, U type, void *output) {
    auto &d = Device::get();
    auto target = d.target().first;
    bool flip = !d.framebuffer && d.composited;
    id<MTLTexture> source = flip ? d.composed : target ? target->gpu : nil;
    require(source && type == gl::UNSIGNED_BYTE && (format == gl::RGBA || format == gl::RGB) &&
                output && x >= 0 && y >= 0 && w >= 0 && h >= 0 && uint64_t(x) + w <= source.width &&
                uint64_t(y) + h <= source.height,
            "Invalid Metal screenshot/readback request");
    if (!w || !h)
        return;
    d.endEncoder();
    d.startCommand();
    size_t stride = (source.width * 4 + 255) & ~size_t(255);
    auto buffer = [d.device newBufferWithLength:stride * source.height
                                        options:MTLResourceStorageModeShared];
    require(buffer != nil, "Cannot allocate Metal readback buffer");
    auto blit = [d.command blitCommandEncoder];
    [blit copyFromTexture:source
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(source.width, source.height, 1)
                        toBuffer:buffer
               destinationOffset:0
          destinationBytesPerRow:stride
        destinationBytesPerImage:stride * source.height];
    [blit endEncoding];
    d.flush();
    size_t channels = format == gl::RGBA ? 4 : 3,
           outStride = (size_t(w) * channels + d.pack - 1) & ~size_t(d.pack - 1);
    for (I row = 0; row < h; ++row) {
        size_t sourceRow = flip ? source.height - 1 - size_t(y + row) : size_t(y + row);
        auto input = static_cast<const unsigned char *>(buffer.contents) + sourceRow * stride +
                     size_t(x) * 4;
        auto dest = static_cast<unsigned char *>(output) + size_t(row) * outStride;
        for (I col = 0; col < w; ++col) {
            auto pixel = input + size_t(col) * 4;
            auto out = dest + size_t(col) * channels;
            out[0] = pixel[flip ? 2 : 0];
            out[1] = pixel[1];
            out[2] = pixel[flip ? 0 : 2];
            if (channels == 4)
                out[3] = pixel[3];
        }
    }
}
} // namespace metalCommands
void Metal::Impl::install() {
#define BIND(name) gl::name = &metalCommands::name
    BIND(Viewport);
    BIND(Scissor);
    BIND(ClearColor);
    BIND(Clear);
    BIND(Enable);
    BIND(Disable);
    BIND(DepthMask);
    BIND(BlendFunc);
    BIND(GenBuffers);
    BIND(BindBuffer);
    BIND(BufferData);
    BIND(BufferSubData);
    BIND(DeleteBuffers);
    BIND(GenVertexArrays);
    BIND(BindVertexArray);
    BIND(DeleteVertexArrays);
    BIND(EnableVertexAttribArray);
    BIND(DisableVertexAttribArray);
    BIND(VertexAttribPointer);
    BIND(VertexAttribIPointer);
    BIND(VertexAttribDivisor);
    BIND(CreateShader);
    BIND(ShaderSource);
    BIND(CompileShader);
    BIND(GetShaderiv);
    BIND(GetShaderInfoLog);
    BIND(DeleteShader);
    BIND(CreateProgram);
    BIND(AttachShader);
    BIND(LinkProgram);
    BIND(GetProgramiv);
    BIND(GetProgramInfoLog);
    BIND(DeleteProgram);
    BIND(UseProgram);
    BIND(GetUniformLocation);
    BIND(Uniform1i);
    BIND(Uniform1f);
    BIND(Uniform2fv);
    BIND(Uniform3fv);
    BIND(Uniform4fv);
    BIND(UniformMatrix4fv);
    BIND(DrawArrays);
    BIND(DrawArraysInstanced);
    BIND(GenTextures);
    BIND(ActiveTexture);
    BIND(BindTexture);
    BIND(TexImage2D);
    BIND(TexSubImage2D);
    BIND(TexParameteri);
    BIND(GenerateMipmap);
    BIND(DeleteTextures);
    BIND(PixelStorei);
    BIND(GetIntegerv);
    BIND(GenFramebuffers);
    BIND(BindFramebuffer);
    BIND(FramebufferTexture2D);
    BIND(CheckFramebufferStatus);
    BIND(DeleteFramebuffers);
    BIND(ReadPixels);
#undef BIND
}

Metal::Metal(GLFWwindow *window, const Json &options) : impl_(std::make_unique<Impl>()) {
    @autoreleasepool {
        auto &d = *impl_;
        require(!Impl::current, "Only one Metal device is supported");
        d.uniformBudget = options.value("uniform_budget_bytes", size_t(64 * 1024 * 1024));
        d.device = MTLCreateSystemDefaultDevice();
        require(d.device != nil, "No Metal device is available; choose OpenGL");
        d.queue = [d.device newCommandQueue];
        require(d.queue != nil, "Cannot create Metal command queue");
        NSWindow *native = glfwGetCocoaWindow(window);
        d.view = native.contentView;
        d.previousLayer = d.view.layer;
        d.previousWantsLayer = d.view.wantsLayer;
        d.layer = [CAMetalLayer layer];
        d.layer.device = d.device;
        d.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.layer.framebufferOnly = NO;
        d.layer.opaque = YES;
        d.layer.contentsScale = native.backingScaleFactor;
        const std::string source = R"(#include <metal_stdlib>
using namespace metal;
struct Output {float4 position [[position]];float2 uv;};
vertex Output present_vertex(uint id [[vertex_id]]){float2 uv=float2((id<<1)&2,id&2);return {float4(uv.x*2-1,1-uv.y*2,0,1),float2(uv.x,1-uv.y)};}
fragment float4 present_fragment(Output input [[stage_in]],texture2d<float> image [[texture(0)]],sampler sampling [[sampler(0)]]){return image.sample(sampling,input.uv);}
)";
        auto descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = d.compile(source, "present_vertex", "Presentation.vertex");
        descriptor.fragmentFunction =
            d.compile(source, "present_fragment", "Presentation.fragment");
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        NSError *error = nil;
        d.presentPipeline = [d.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        require(d.presentPipeline != nil,
                "Cannot create Metal presentation pipeline: " + message(error));
        auto sample = [MTLSamplerDescriptor new];
        sample.minFilter = sample.magFilter = MTLSamplerMinMagFilterNearest;
        sample.sAddressMode = sample.tAddressMode = MTLSamplerAddressModeClampToEdge;
        d.presentSampler = [d.device newSamplerStateWithDescriptor:sample];
        const float floats[4] = {0, 0, 0, 1};
        const int ints[4] = {0, 0, 0, 1};
        d.zero[0] = [d.device newBufferWithBytes:floats
                                          length:16
                                         options:MTLResourceStorageModeShared];
        d.zero[1] = [d.device newBufferWithBytes:ints
                                          length:16
                                         options:MTLResourceStorageModeShared];
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        beginFrame(std::max(1, w), std::max(1, h));
        d.view.wantsLayer = YES;
        d.view.layer = d.layer;
        Impl::current = &d;
        d.install();
        logger.write("INFO", std::string("Graphics backend: Metal / ") + d.device.name.UTF8String);
    }
}
Metal::~Metal() {
    auto &d = *impl_;
    try {
        d.flush();
    } catch (const std::exception &error) {
        logger.write("ERROR", error.what());
    }
    if (d.view.layer == d.layer) {
        d.view.layer = d.previousLayer;
        d.view.wantsLayer = d.previousWantsLayer;
    }
    if (Impl::current == &d)
        Impl::current = nullptr;
}
void Metal::renderFrame(const std::function<void()> &draw) {
    // GLFW's pools cover its own calls only. Bound all transient Cocoa/Metal
    // objects to this frame, including exceptions and editor rendering.
    @autoreleasepool {
        draw();
    }
}
void Metal::beginFrame(int w, int h) {
    auto &d = *impl_;
    d.reap();
    d.composited = false;
    require(w > 0 && h > 0 && w <= 16384 && h <= 16384, "Invalid Metal framebuffer dimensions");
    if (w == d.width && h == d.height)
        return;
    d.flush();
    d.allocateTexture(d.screen, w, h, false, false, nullptr);
    d.allocateTexture(d.screenDepth, w, h, true, false, nullptr);
    d.width = w;
    d.height = h;
    d.layer.drawableSize = CGSizeMake(w, h);
}
void Metal::present(bool vsync) {
    @autoreleasepool {
        auto &d = *impl_;
        d.compose();
        d.layer.displaySyncEnabled = vsync;
        auto drawable = [d.layer nextDrawable];
        if (drawable) {
            d.endEncoder();
            d.startCommand();
            auto blit = [d.command blitCommandEncoder];
            [blit copyFromTexture:d.composed
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(d.width, d.height, 1)
                        toTexture:drawable.texture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [d.command presentDrawable:drawable];
        }
        d.submit();
    }
}
Json Metal::diagnostics() const {
    auto &d = *impl_;
    size_t pipelineEntries = 0;
    for (const auto &entry : d.programs)
        pipelineEntries += entry.second.pipelines.size();
    return {{"backend", "metal"},
            {"adapter", std::string(d.device.name.UTF8String)},
            {"unified_memory", bool(d.device.hasUnifiedMemory)},
            {"shader_language", "glsl+msl"},
            {"translated_programs", d.translatedPrograms},
            {"native_programs", d.nativePrograms},
            {"draw_pipeline_builds", d.pipelineBuilds},
            {"draw_pipeline_cache_entries", pipelineEntries},
            {"command_submissions", d.submissions},
            {"gpu_waits", d.gpuWaits},
            {"synchronous_flushes", d.synchronousFlushes},
            {"inflight_command_buffers", d.pending.size()},
            {"peak_inflight_command_buffers", d.peakInflight},
            {"max_inflight_command_buffers", Impl::maxInflight},
            {"presentation_bytes", size_t(d.width) * size_t(d.height) * 12},
            {"uniform_arena_bytes", d.uniformAllocated},
            {"uniform_budget_bytes", d.uniformBudget}};
}
void Metal::finish() {
    @autoreleasepool {
        impl_->flush();
    }
}
unsigned Metal::mslProgram(const std::string &v, const std::string &f, const std::string &ve,
                           const std::string &fe, const std::string &label) {
    auto &d = *impl_;
    Program p;
    d.buildProgram(p, v, f, label, true, ve, fe);
    auto name = d.handle();
    d.programs.emplace(name, std::move(p));
    return name;
}
void Metal::initializeEditor() {
#if FORGE_WITH_EDITOR
    require(ImGui_ImplMetal_Init(impl_->device), "Cannot initialize Metal editor");
#endif
}
void Metal::shutdownEditor() {
#if FORGE_WITH_EDITOR
    ImGui_ImplMetal_Shutdown();
#endif
}
void Metal::editorNewFrame() {
#if FORGE_WITH_EDITOR
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = impl_->composed;
    if (!pass.colorAttachments[0].texture) {
        auto desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:impl_->width
                                                              height:impl_->height
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget;
        pass.colorAttachments[0].texture = [impl_->device newTextureWithDescriptor:desc];
    }
    ImGui_ImplMetal_NewFrame(pass);
#endif
}
void Metal::editorDraw() {
#if FORGE_WITH_EDITOR
    auto &d = *impl_;
    d.compose();
    d.startCommand();
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = d.composed;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto encoder = [d.command renderCommandEncoderWithDescriptor:pass];
    require(encoder != nil, "Cannot draw Metal editor");
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), d.command, encoder);
    [encoder endEncoding];
#endif
}
} // namespace forge
