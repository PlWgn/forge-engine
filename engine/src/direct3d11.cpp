// Optional, adaptable Direct3D 11 device. This implements Forge's graphics command
// vocabulary, not a general OpenGL driver. CPU resources stay backend independent.
#include <forge/direct3d11.hpp>
#include <forge/gl.hpp>
#include <forge/shader_compiler.hpp>
#include <forge/logger.hpp>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#if FORGE_WITH_EDITOR
#include <imgui.h>
#include <imgui_impl_dx11.h>
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>
namespace forge {
namespace {
using Microsoft::WRL::ComPtr;
using U = gl::U;
using I = gl::I;
void check(HRESULT result, const std::string& operation) {
    if (FAILED(result)) {
        std::ostringstream text;
        text << operation << " failed (HRESULT 0x" << std::hex << unsigned(result) << ')';
        throw std::runtime_error(text.str());
    }
}
UINT narrow(size_t value, const char* name) {
    if (value > std::numeric_limits<UINT>::max()) throw std::runtime_error(std::string(name) + " is too large");
    return static_cast<UINT>(value);
}
void logText(const std::string& text, I capacity, I* written, char* output) {
    I count = std::min<I>(std::max(0, capacity - 1), static_cast<I>(text.size()));
    if (output && capacity > 0) { std::memcpy(output, text.data(), count); output[count] = 0; }
    if (written) *written = count;
}
struct Buffer {
    ComPtr<ID3D11Buffer> gpu;
    std::vector<unsigned char> bytes;
    bool dirty = false;
};
struct Attribute {
    U buffer = 0, type = gl::FLOAT, divisor = 0;
    UINT offset = 0, stride = 0;
    I size = 4;
    bool enabled = false, normalized = false;
};
struct VertexArray {
    std::array<Attribute, 16> attributes;
    unsigned long long revision = 0;
};
struct Texture {
    ComPtr<ID3D11Texture2D> gpu;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11SamplerState> sampler;
    int width = 0, height = 0;
    U minFilter = gl::LINEAR, magFilter = gl::LINEAR, wrapS = gl::CLAMP_TO_EDGE, wrapT = gl::CLAMP_TO_EDGE;
    bool depth = false, mipmaps = false;
};
struct Framebuffer { U color = 0, depth = 0; };
struct Shader { U kind = 0; std::string source, error; bool compiled = false; };
struct Variable {
    std::string name;
    UINT offset = 0, elements = 1, stride = 0, rows = 1, columns = 1;
    D3D_SHADER_VARIABLE_CLASS category = D3D_SVC_SCALAR;
    D3D_SHADER_VARIABLE_TYPE type = D3D_SVT_FLOAT;
};
struct Constants {
    UINT slot = 0;
    ComPtr<ID3D11Buffer> gpu;
    std::vector<unsigned char> bytes;
    std::vector<Variable> variables;
    bool dirty = true;
};
struct Resource { std::string name; UINT slot = 0; bool sampler = false; };
struct Stage {
    ComPtr<ID3DBlob> code;
    std::vector<Constants> constants;
    std::vector<Resource> resources;
};
struct Program {
    Stage vertex, pixel;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11ShaderReflection> reflection;
    std::vector<U> shaders;
    std::vector<std::string> locations;
    std::unordered_map<std::string, I> locationIndex;
    std::unordered_map<std::string, I> textureUnits;
    std::map<std::pair<U, unsigned long long>, ComPtr<ID3D11InputLayout>> layouts;
    std::string error;
    bool linked = false, native = false;
};
}
struct Direct3D11::Impl {
    static Impl* current;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<IDXGISwapChain> swapchain;
    ComPtr<ID3D11Texture2D> back;
    ComPtr<ID3D11RenderTargetView> backView;
    Texture screen, screenDepth;
    ComPtr<ID3D11VertexShader> presentVertex;
    ComPtr<ID3D11PixelShader> presentPixel;
    ComPtr<ID3D11SamplerState> presentSampler;
    ComPtr<ID3D11RasterizerState> presentRaster;
    ComPtr<ID3D11Buffer> zeroFloat, zeroInt;
    std::map<U, Buffer> buffers;
    std::map<U, VertexArray> arrays;
    std::map<U, Texture> textures;
    std::map<U, Framebuffer> framebuffers;
    std::map<U, Shader> shaders;
    std::map<U, Program> programs;
    std::map<unsigned, ComPtr<ID3D11DepthStencilState>> depthStates;
    std::map<unsigned, ComPtr<ID3D11BlendState>> blendStates;
    std::map<bool, ComPtr<ID3D11RasterizerState>> rasterStates;
    std::array<U, 16> boundTextures{};
    U next = 1, buffer = 0, array = 0, framebuffer = 0, program = 0, textureUnit = 0;
    U blendSource = gl::SRC_ALPHA, blendDestination = gl::ONE_MINUS_SRC_ALPHA;
    bool depthTest = false, depthWrite = true, blend = false, scissorTest = false;
    bool debug = false, composited = false;
    int width = 0, height = 0, unpack = 4, pack = 4;
    D3D11_VIEWPORT viewport{};
    D3D11_RECT scissor{};
    float clearColor[4] = {0, 0, 0, 0};
    std::string driver, adapter;
    unsigned long long translatedPrograms = 0, nativePrograms = 0;
    U id() {
        if (next == std::numeric_limits<U>::max()) throw std::runtime_error("Graphics handle space exhausted");
        return next++;
    }
    static Impl& get() {
        if (!current) throw std::runtime_error("No Direct3D 11 device is active");
        return *current;
    }
    ComPtr<ID3DBlob> compile(const std::string& source, const std::string& name, const char* profile, bool rowMajor = false) {
        ComPtr<ID3DBlob> code, errors;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
        if (debug) flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
        if (rowMajor) flags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
        auto result = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, nullptr, "main", profile,
                                 flags, 0, code.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result)) {
            std::string message = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "No compiler diagnostics";
            throw std::runtime_error("Shader " + name + ":\n" + message);
        }
        return code;
    }
    void reflect(Stage& stage, const std::map<std::string, std::string>& names) {
        auto publicName = [&](const std::string& name) { auto found = names.find(name); return found == names.end() ? name : found->second; };
        ComPtr<ID3D11ShaderReflection> reflection;
        check(D3DReflect(stage.code->GetBufferPointer(), stage.code->GetBufferSize(), IID_ID3D11ShaderReflection,
                         reinterpret_cast<void**>(reflection.GetAddressOf())), "D3DReflect");
        D3D11_SHADER_DESC desc{};
        check(reflection->GetDesc(&desc), "Shader reflection");
        for (UINT i = 0; i < desc.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC binding{};
            check(reflection->GetResourceBindingDesc(i, &binding), "Resource reflection");
            if (binding.BindCount != 1) throw std::runtime_error("Shader resource arrays are not supported by Forge's texture API");
            if (binding.Type == D3D_SIT_CBUFFER) {
                auto cb = reflection->GetConstantBufferByName(binding.Name);
                D3D11_SHADER_BUFFER_DESC description{};
                check(cb->GetDesc(&description), "Constant buffer reflection");
                if (description.Size > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16)
                    throw std::runtime_error("Shader constant buffer exceeds Direct3D 11 limit");
                Constants constants;
                constants.slot = binding.BindPoint;
                constants.bytes.resize((description.Size + 15) & ~UINT(15));
                D3D11_BUFFER_DESC bufferDesc{};
                bufferDesc.ByteWidth = narrow(constants.bytes.size(), "Constant buffer");
                bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
                bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                check(device->CreateBuffer(&bufferDesc, nullptr, constants.gpu.GetAddressOf()), "Create constant buffer");
                for (UINT v = 0; v < description.Variables; ++v) {
                    auto var = cb->GetVariableByIndex(v);
                    D3D11_SHADER_VARIABLE_DESC value{};
                    D3D11_SHADER_TYPE_DESC type{};
                    check(var->GetDesc(&value), "Uniform reflection");
                    check(var->GetType()->GetDesc(&type), "Uniform type reflection");
                    if (type.Class == D3D_SVC_STRUCT || type.Class == D3D_SVC_OBJECT)
                        throw std::runtime_error(std::string("Unsupported shader uniform structure: ") + value.Name);
                    Variable field;
                    field.name = publicName(value.Name);
                    field.offset = value.StartOffset;
                    field.elements = std::max(1u, type.Elements);
                    field.rows = type.Rows; field.columns = type.Columns;
                    field.category = type.Class; field.type = type.Type;
                    field.stride = type.Class == D3D_SVC_MATRIX_ROWS ? 16 * type.Rows :
                                   type.Class == D3D_SVC_MATRIX_COLUMNS ? 16 * type.Columns : 16;
                    constants.variables.push_back(std::move(field));
                }
                stage.constants.push_back(std::move(constants));
            } else if (binding.Type == D3D_SIT_TEXTURE || binding.Type == D3D_SIT_SAMPLER) {
                if (binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D && binding.Type == D3D_SIT_TEXTURE)
                    throw std::runtime_error("Forge texture API supports sampler2D / Texture2D only");
                stage.resources.push_back({binding.Type == D3D_SIT_SAMPLER ? binding.Name : publicName(binding.Name),
                                           binding.BindPoint, binding.Type == D3D_SIT_SAMPLER});
            } else throw std::runtime_error("Shader resource type is outside Forge's draw API");
        }
    }
    void buildProgram(Program& value, const std::string& vertex, const std::string& fragment,
                      const std::string& label, bool native) {
        auto translated = native ? TranslatedShaders{vertex, fragment} : translateGlsl(vertex, fragment, label);
        value.native = native;
        value.vertex.code = compile(translated.vertex, label + ".vertex", "vs_5_0", !native);
        value.pixel.code = compile(translated.fragment, label + ".fragment", "ps_5_0", !native);
        check(device->CreateVertexShader(value.vertex.code->GetBufferPointer(), value.vertex.code->GetBufferSize(), nullptr,
                                         value.vs.GetAddressOf()), "Create vertex shader");
        check(device->CreatePixelShader(value.pixel.code->GetBufferPointer(), value.pixel.code->GetBufferSize(), nullptr,
                                        value.ps.GetAddressOf()), "Create pixel shader");
        check(D3DReflect(value.vertex.code->GetBufferPointer(), value.vertex.code->GetBufferSize(), IID_ID3D11ShaderReflection,
                         reinterpret_cast<void**>(value.reflection.GetAddressOf())), "Vertex input reflection");
        reflect(value.vertex, translated.vertexUniforms); reflect(value.pixel, translated.fragmentUniforms);
        value.linked = true;
        if (native) ++nativePrograms; else ++translatedPrograms;
    }
    void allocateTexture(Texture& target, int w, int h, bool depth, bool mipmaps, const void* pixels) {
        if (w < 1 || h < 1 || w > 16384 || h > 16384) throw std::runtime_error("Invalid Direct3D texture dimensions");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.ArraySize = 1;
        desc.MipLevels = mipmaps ? 0 : 1;
        desc.Format = depth ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET);
        if (mipmaps) desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        Texture candidate;
        candidate.width = w; candidate.height = h; candidate.depth = depth; candidate.mipmaps = mipmaps;
        candidate.minFilter = target.minFilter; candidate.magFilter = target.magFilter;
        candidate.wrapS = target.wrapS; candidate.wrapT = target.wrapT;
        check(device->CreateTexture2D(&desc, nullptr, candidate.gpu.GetAddressOf()), "Create texture");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = depth ? DXGI_FORMAT_R32_FLOAT : desc.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = mipmaps ? UINT(-1) : 1;
        check(device->CreateShaderResourceView(candidate.gpu.Get(), &view, candidate.srv.GetAddressOf()), "Create texture view");
        if (depth) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
            dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            check(device->CreateDepthStencilView(candidate.gpu.Get(), &dv, candidate.dsv.GetAddressOf()), "Create depth view");
        } else check(device->CreateRenderTargetView(candidate.gpu.Get(), nullptr, candidate.rtv.GetAddressOf()), "Create color view");
        if (pixels) context->UpdateSubresource(candidate.gpu.Get(), 0, nullptr, pixels, UINT(w) * 4, 0);
        target = std::move(candidate);
    }
    Texture& boundTexture() {
        auto it = textures.find(boundTextures.at(textureUnit));
        if (it == textures.end()) throw std::runtime_error("No texture is bound");
        return it->second;
    }
    std::pair<Texture*, Texture*> target() {
        if (!framebuffer) return {&screen, &screenDepth};
        auto& fbo = framebuffers.at(framebuffer);
        auto color = textures.find(fbo.color), depth = textures.find(fbo.depth);
        return {color == textures.end() ? nullptr : &color->second, depth == textures.end() ? nullptr : &depth->second};
    }
    void bindTarget() {
        auto pair = target();
        auto rtv = pair.first ? pair.first->rtv.Get() : nullptr;
        auto dsv = pair.second ? pair.second->dsv.Get() : nullptr;
        if (!rtv && !dsv) throw std::runtime_error("Incomplete Direct3D render target");
        context->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);
    }
    void unbindTextures() {
        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> empty{};
        context->VSSetShaderResources(0, UINT(empty.size()), empty.data());
        context->PSSetShaderResources(0, UINT(empty.size()), empty.data());
    }
    void sampler(Texture& texture) {
        if (texture.sampler) return;
        D3D11_SAMPLER_DESC desc{};
        bool min = texture.minFilter == gl::LINEAR || texture.minFilter == 0x2701 || texture.minFilter == 0x2703;
        bool mag = texture.magFilter == gl::LINEAR;
        bool mip = texture.minFilter == 0x2702 || texture.minFilter == 0x2703;
        desc.Filter = D3D11_ENCODE_BASIC_FILTER(min ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                                               mag ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                                               mip ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                                               false);
        auto wrap = [](U mode) { return mode == 0x2901 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP; };
        desc.AddressU = wrap(texture.wrapS); desc.AddressV = wrap(texture.wrapT); desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = texture.minFilter >= 0x2700 && texture.minFilter <= 0x2703 ? D3D11_FLOAT32_MAX : 0;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        check(device->CreateSamplerState(&desc, texture.sampler.GetAddressOf()), "Create texture sampler");
    }
    static std::pair<std::string, UINT> uniformName(const std::string& name) {
        auto bracket = name.find('[');
        if (bracket == std::string::npos) return {name, 0};
        if (name.back() != ']' || bracket + 2 > name.size()) return {name, 0};
        auto index = name.substr(bracket + 1, name.size() - bracket - 2);
        if (index.empty() || index.find_first_not_of("0123456789") != std::string::npos) return {name, 0};
        auto number = std::stoull(index);
        return {name.substr(0, bracket), narrow(number, "Uniform array index")};
    }
    I location(U handle, const std::string& name) {
        auto& p = programs.at(handle);
        auto found = p.locationIndex.find(name);
        if (found != p.locationIndex.end()) return found->second;
        auto parsed = uniformName(name);
        bool exists = false;
        for (auto stage : {&p.vertex, &p.pixel}) {
            for (auto& cb : stage->constants)
                for (auto& var : cb.variables)
                    if (var.name == parsed.first && parsed.second < var.elements) exists = true;
            for (auto& resource : stage->resources)
                if (!resource.sampler && resource.name == name) exists = true;
        }
        if (!exists) return -1;
        I loc = static_cast<I>(p.locations.size());
        p.locations.push_back(name); p.locationIndex[name] = loc;
        return loc;
    }
    void uniform(I location, I count, UINT components, const void* data, bool integer, bool matrix = false) {
        if (location < 0) return;
        auto& p = programs.at(program);
        if (size_t(location) >= p.locations.size() || count < 1 || !data) throw std::runtime_error("Invalid uniform update");
        auto name = p.locations[size_t(location)];
        auto parsed = uniformName(name);
        if (integer && !matrix && count == 1 && components == 1) {
            for (auto stage : {&p.vertex, &p.pixel})
                for (auto& resource : stage->resources)
                    if (!resource.sampler && resource.name == name) {
                        int unit = *static_cast<const int*>(data);
                        if (unit < 0 || unit >= int(boundTextures.size())) throw std::runtime_error("Texture unit must be 0..15");
                        p.textureUnits[name] = unit;
                    }
        }
        for (auto stage : {&p.vertex, &p.pixel}) for (auto& cb : stage->constants) for (auto& var : cb.variables) {
            if (var.name != parsed.first || parsed.second >= var.elements) continue;
            if (UINT(count) > var.elements - parsed.second) throw std::runtime_error("Uniform array update exceeds declared length: " + name);
            if (matrix ? var.rows != 4 || var.columns != 4 : var.rows * var.columns != components)
                throw std::runtime_error("Uniform shape mismatch: " + name);
            bool expectedInteger = var.type == D3D_SVT_INT || var.type == D3D_SVT_UINT || var.type == D3D_SVT_BOOL;
            if (integer != expectedInteger) throw std::runtime_error("Uniform scalar type mismatch: " + name);
            for (UINT i = 0; i < UINT(count); ++i) {
                size_t offset = var.offset + (parsed.second + i) * var.stride;
                size_t bytes = size_t(components) * 4;
                if (offset > cb.bytes.size() || bytes > cb.bytes.size() - offset) throw std::runtime_error("Uniform buffer range overflow");
                auto input = static_cast<const unsigned char*>(data) + size_t(i) * bytes;
                if (matrix && p.native && var.category == D3D_SVC_MATRIX_ROWS) {
                    float values[16]; std::memcpy(values, input, sizeof(values));
                    for (UINT row = 0; row < 4; ++row) for (UINT column = 0; column < 4; ++column)
                        std::memcpy(cb.bytes.data() + offset + (row * 4 + column) * 4, &values[column * 4 + row], 4);
                } else std::memcpy(cb.bytes.data() + offset, input, bytes);
            }
            cb.dirty = true;
        }
    }
    void stageResources(Program& p, Stage& stage, bool vertex) {
        for (auto& cb : stage.constants) {
            if (cb.dirty) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                check(context->Map(cb.gpu.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map constant buffer");
                std::memcpy(mapped.pData, cb.bytes.data(), cb.bytes.size()); context->Unmap(cb.gpu.Get(), 0);
                cb.dirty = false;
            }
            auto buffer = cb.gpu.Get();
            if (vertex) context->VSSetConstantBuffers(cb.slot, 1, &buffer); else context->PSSetConstantBuffers(cb.slot, 1, &buffer);
        }
        auto outputs = target();
        for (auto& resource : stage.resources) {
            std::string name = resource.name;
            if (resource.sampler) {
                const std::string suffix = "_sampler";
                if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                    name.resize(name.size() - suffix.size());
                if (!name.empty() && name[0] == '_' && !p.textureUnits.count(name)) name.erase(0, 1);
                auto texture = std::find_if(stage.resources.begin(), stage.resources.end(),
                    [&](const Resource& item) { return !item.sampler && item.name == name; });
                if (texture == stage.resources.end()) {
                    // Native HLSL may use arbitrary sampler names with matching
                    // tN/sN bindings rather than the generated name convention.
                    texture = std::find_if(stage.resources.begin(), stage.resources.end(),
                        [&](const Resource& item) { return !item.sampler && item.slot == resource.slot; });
                    if (texture != stage.resources.end()) name = texture->name;
                }
            }
            auto unit = p.textureUnits.count(name) ? p.textureUnits.at(name) : 0;
            auto found = textures.find(boundTextures.at(size_t(unit)));
            Texture* image = found == textures.end() ? nullptr : &found->second;
            if (image && (image == outputs.first || image == outputs.second))
                throw std::runtime_error("A shader cannot sample its active render target");
            if (resource.sampler) {
                if (image) sampler(*image);
                auto samplerState = image ? image->sampler.Get() : nullptr;
                if (vertex) context->VSSetSamplers(resource.slot, 1, &samplerState); else context->PSSetSamplers(resource.slot, 1, &samplerState);
            } else {
                auto srv = image ? image->srv.Get() : nullptr;
                if (vertex) context->VSSetShaderResources(resource.slot, 1, &srv); else context->PSSetShaderResources(resource.slot, 1, &srv);
            }
        }
    }
    static DXGI_FORMAT attributeFormat(const Attribute& a) {
        if (a.size < 1 || a.size > 4) throw std::runtime_error("Vertex attribute must have 1..4 components");
        const DXGI_FORMAT floats[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
        const DXGI_FORMAT ints[] = {DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32A32_SINT};
        const DXGI_FORMAT uints[] = {DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32A32_UINT};
        if (a.type == gl::FLOAT) return floats[a.size - 1];
        if (a.type == 0x1404) return ints[a.size - 1];
        if (a.type == gl::UNSIGNED_INT) return uints[a.size - 1];
        throw std::runtime_error("Direct3D vertex attributes require float32/int32/uint32");
    }
    void upload(Buffer& b) {
        if (!b.dirty || !b.gpu) return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(b.gpu.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map vertex buffer");
        std::memcpy(mapped.pData, b.bytes.data(), b.bytes.size()); context->Unmap(b.gpu.Get(), 0); b.dirty = false;
    }
    void vertexLayout(Program& p) {
        auto& vao = arrays.at(array);
        D3D11_SHADER_DESC shaderDesc{};
        check(p.reflection->GetDesc(&shaderDesc), "Vertex signature reflection");
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        std::array<ID3D11Buffer*, 16> vertexBuffers{};
        std::array<UINT, 16> strides{}, offsets{};
        for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
            D3D11_SIGNATURE_PARAMETER_DESC input{};
            check(p.reflection->GetInputParameterDesc(i, &input), "Vertex attribute reflection");
            if (input.SystemValueType != D3D_NAME_UNDEFINED) continue;
            if (std::string(input.SemanticName) != "TEXCOORD" || input.SemanticIndex >= vao.attributes.size())
                throw std::runtime_error("Forge vertex attributes use TEXCOORD0..15 semantics");
            auto slot = input.SemanticIndex;
            auto attr = vao.attributes[slot];
            if (!attr.enabled) {
                attr.size = 4; attr.stride = 0; attr.offset = 0;
                attr.type = input.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? 0x1404 :
                            input.ComponentType == D3D_REGISTER_COMPONENT_UINT32 ? gl::UNSIGNED_INT : gl::FLOAT;
                vertexBuffers[slot] = attr.type == gl::FLOAT ? zeroFloat.Get() : zeroInt.Get();
            } else {
                auto& b = buffers.at(attr.buffer); upload(b);
                vertexBuffers[slot] = b.gpu.Get();
                if (!b.gpu) throw std::runtime_error("Drawing with an empty vertex buffer");
            }
            strides[slot] = attr.stride;
            D3D11_INPUT_ELEMENT_DESC element{};
            element.SemanticName = input.SemanticName; element.SemanticIndex = slot;
            element.Format = attributeFormat(attr); element.InputSlot = slot; element.AlignedByteOffset = attr.offset;
            element.InputSlotClass = attr.divisor ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
            element.InstanceDataStepRate = attr.divisor;
            elements.push_back(element);
        }
        auto key = std::make_pair(array, vao.revision);
        auto& layout = p.layouts[key];
        if (!layout && !elements.empty())
            check(device->CreateInputLayout(elements.data(), narrow(elements.size(), "Input layout"),
                                            p.vertex.code->GetBufferPointer(), p.vertex.code->GetBufferSize(),
                                            layout.GetAddressOf()), "Create vertex layout");
        context->IASetInputLayout(layout.Get());
        context->IASetVertexBuffers(0, UINT(vertexBuffers.size()), vertexBuffers.data(), strides.data(), offsets.data());
    }
    void states() {
        unsigned depthKey = unsigned(depthTest) | (unsigned(depthWrite) << 1);
        auto& ds = depthStates[depthKey];
        if (!ds) {
            D3D11_DEPTH_STENCIL_DESC desc{};
            desc.DepthEnable = depthTest; desc.DepthWriteMask = depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_LESS;
            check(device->CreateDepthStencilState(&desc, ds.GetAddressOf()), "Create depth state");
        }
        context->OMSetDepthStencilState(ds.Get(), 0);
        auto blendValue = [](U factor) {
            if (factor == gl::SRC_ALPHA) return D3D11_BLEND_SRC_ALPHA;
            if (factor == gl::ONE_MINUS_SRC_ALPHA) return D3D11_BLEND_INV_SRC_ALPHA;
            if (factor == 1) return D3D11_BLEND_ONE;
            if (factor == 0) return D3D11_BLEND_ZERO;
            throw std::runtime_error("Unsupported blend factor");
        };
        unsigned blendKey = unsigned(blend) | (unsigned(blendValue(blendSource)) << 1) | (unsigned(blendValue(blendDestination)) << 6);
        auto& bs = blendStates[blendKey];
        if (!bs) {
            D3D11_BLEND_DESC desc{};
            auto& target = desc.RenderTarget[0];
            target.BlendEnable = blend; target.SrcBlend = blendValue(blendSource); target.DestBlend = blendValue(blendDestination);
            target.SrcBlendAlpha = target.SrcBlend; target.DestBlendAlpha = target.DestBlend;
            target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            check(device->CreateBlendState(&desc, bs.GetAddressOf()), "Create blend state");
        }
        context->OMSetBlendState(bs.Get(), nullptr, ~UINT(0));
        auto& rs = rasterStates[scissorTest];
        if (!rs) {
            D3D11_RASTERIZER_DESC desc{};
            desc.FillMode = D3D11_FILL_SOLID; desc.CullMode = D3D11_CULL_NONE; desc.FrontCounterClockwise = TRUE;
            desc.DepthClipEnable = TRUE; desc.ScissorEnable = scissorTest;
            check(device->CreateRasterizerState(&desc, rs.GetAddressOf()), "Create rasterizer state");
        }
        context->RSSetState(rs.Get()); context->RSSetViewports(1, &viewport); context->RSSetScissorRects(1, &scissor);
    }
    void draw(I first, I count, I instances) {
        if (first < 0 || count < 0 || instances < 0) throw std::runtime_error("Invalid draw range");
        if (!count || !instances) return;
        auto& p = programs.at(program);
        if (!p.linked) throw std::runtime_error("Shader program is not linked");
        bindTarget(); states(); vertexLayout(p);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(p.vs.Get(), nullptr, 0); context->PSSetShader(p.ps.Get(), nullptr, 0);
        stageResources(p, p.vertex, true); stageResources(p, p.pixel, false);
        context->DrawInstanced(UINT(count), UINT(instances), UINT(first), 0);
    }
    void compose() {
        if (composited) return;
        unbindTextures();
        context->OMSetRenderTargets(1, backView.GetAddressOf(), nullptr);
        context->OMSetDepthStencilState(nullptr, 0);
        context->OMSetBlendState(nullptr, nullptr, ~UINT(0));
        context->RSSetState(presentRaster.Get());
        D3D11_VIEWPORT view{0, 0, float(width), float(height), 0, 1};
        context->RSSetViewports(1, &view);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(presentVertex.Get(), nullptr, 0);
        context->PSSetShader(presentPixel.Get(), nullptr, 0);
        auto source = screen.srv.Get(); auto sampling = presentSampler.Get();
        context->PSSetShaderResources(0, 1, &source); context->PSSetSamplers(0, 1, &sampling);
        context->Draw(3, 0);
        unbindTextures(); composited = true;
    }
    void install();
};
Direct3D11::Impl* Direct3D11::Impl::current = nullptr;
namespace commands {
using Device = Direct3D11::Impl;
void FORGE_GL_CALL Viewport(I x, I y, I w, I h) {
    if (w < 0 || h < 0) throw std::runtime_error("Invalid viewport dimensions");
    Device::get().viewport = {float(x), float(y), float(w), float(h), 0, 1};
}
void FORGE_GL_CALL Scissor(I x, I y, I w, I h) {
    if (w < 0 || h < 0 || int64_t(x) + w > LONG_MAX || int64_t(y) + h > LONG_MAX)
        throw std::runtime_error("Invalid scissor rectangle");
    Device::get().scissor = {x, y, x + w, y + h};
}
void FORGE_GL_CALL ClearColor(float r, float g, float b, float a) {
    auto& d = Device::get(); d.clearColor[0] = r; d.clearColor[1] = g; d.clearColor[2] = b; d.clearColor[3] = a;
}
void FORGE_GL_CALL Clear(U mask) {
    auto& d = Device::get(); d.bindTarget(); auto pair = d.target();
    if (d.scissorTest) {
        if (!d.context1) throw std::runtime_error("Scissored clears require the Windows Direct3D 11.1 runtime");
        auto view = pair.first ? pair.first : pair.second;
        D3D11_RECT rect{std::max<LONG>(0, d.scissor.left), std::max<LONG>(0, d.scissor.top),
                       std::min<LONG>(view->width, d.scissor.right), std::min<LONG>(view->height, d.scissor.bottom)};
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        if ((mask & gl::COLOR) && pair.first) d.context1->ClearView(pair.first->rtv.Get(), d.clearColor, &rect, 1);
        const float depth[4] = {1, 0, 0, 0};
        if ((mask & gl::DEPTH) && d.depthWrite && pair.second) d.context1->ClearView(pair.second->dsv.Get(), depth, &rect, 1);
    } else {
        if ((mask & gl::COLOR) && pair.first) d.context->ClearRenderTargetView(pair.first->rtv.Get(), d.clearColor);
        if ((mask & gl::DEPTH) && d.depthWrite && pair.second) d.context->ClearDepthStencilView(pair.second->dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    }
}
void enabled(U flag, bool value) {
    auto& d = Device::get();
    if (flag == gl::DEPTH_TEST) d.depthTest = value;
    else if (flag == gl::BLEND) d.blend = value;
    else if (flag == gl::SCISSOR_TEST) d.scissorTest = value;
    else throw std::runtime_error("Unsupported Forge graphics capability");
}
void FORGE_GL_CALL Enable(U flag) { enabled(flag, true); }
void FORGE_GL_CALL Disable(U flag) { enabled(flag, false); }
void FORGE_GL_CALL DepthMask(gl::B flag) { Device::get().depthWrite = flag != 0; }
void FORGE_GL_CALL BlendFunc(U source, U destination) { auto& d = Device::get(); d.blendSource = source; d.blendDestination = destination; }
void FORGE_GL_CALL GenBuffers(I count, U* names) {
    auto& d = Device::get(); for (I i = 0; i < count; ++i) { names[i] = d.id(); d.buffers.emplace(names[i], Buffer{}); }
}
void FORGE_GL_CALL BindBuffer(U target, U name) {
    if (target != gl::ARRAY_BUFFER) throw std::runtime_error("Forge supports vertex buffers only");
    auto& d = Device::get(); if (name && !d.buffers.count(name)) throw std::runtime_error("Unknown vertex buffer"); d.buffer = name;
}
void FORGE_GL_CALL BufferData(U target, gl::S size, const void* data, U) {
    if (target != gl::ARRAY_BUFFER || size < 0) throw std::runtime_error("Invalid vertex buffer request");
    auto& d = Device::get(); auto& buffer = d.buffers.at(d.buffer);
    Buffer candidate; candidate.bytes.resize(size_t(size));
    if (data && size) std::memcpy(candidate.bytes.data(), data, size_t(size));
    if (size) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = narrow(size_t(size), "Vertex buffer"); desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = candidate.bytes.data();
        check(d.device->CreateBuffer(&desc, &initial, candidate.gpu.GetAddressOf()), "Create vertex buffer");
    }
    buffer = std::move(candidate);
}
void FORGE_GL_CALL BufferSubData(U target, gl::S offset, gl::S size, const void* data) {
    if (target != gl::ARRAY_BUFFER || offset < 0 || size < 0 || (size && !data)) throw std::runtime_error("Invalid vertex buffer update");
    auto& d = Device::get(); auto& buffer = d.buffers.at(d.buffer);
    if (size_t(offset) > buffer.bytes.size() || size_t(size) > buffer.bytes.size() - size_t(offset)) throw std::runtime_error("Vertex buffer update exceeds capacity");
    if (size) std::memcpy(buffer.bytes.data() + size_t(offset), data, size_t(size)); buffer.dirty = true;
}
void FORGE_GL_CALL DeleteBuffers(I count, const U* names) {
    auto& d = Device::get(); for (I i = 0; i < count; ++i) { d.buffers.erase(names[i]); if (d.buffer == names[i]) d.buffer = 0; }
}
void FORGE_GL_CALL GenVertexArrays(I count, U* names) {
    auto& d = Device::get(); for (I i = 0; i < count; ++i) { names[i] = d.id(); d.arrays.emplace(names[i], VertexArray{}); }
}
void FORGE_GL_CALL BindVertexArray(U name) {
    auto& d = Device::get(); if (name && !d.arrays.count(name)) throw std::runtime_error("Unknown vertex array"); d.array = name;
}
void FORGE_GL_CALL DeleteVertexArrays(I count, const U* names) {
    auto& d = Device::get(); for (I i = 0; i < count; ++i) {
        d.arrays.erase(names[i]); if (d.array == names[i]) d.array = 0;
        for (auto& entry : d.programs) {
            auto& layouts = entry.second.layouts;
            for (auto it = layouts.begin(); it != layouts.end();) if (it->first.first == names[i]) it = layouts.erase(it); else ++it;
        }
    }
}
void attribute(U index, I size, U type, bool normalized, I stride, const void* offset) {
    auto& d = Device::get(); auto& vao = d.arrays.at(d.array); auto& a = vao.attributes.at(index);
    if (stride < 0 || size < 1 || size > 4) throw std::runtime_error("Invalid vertex attribute");
    a.buffer = d.buffer; a.size = size; a.type = type; a.normalized = normalized;
    a.offset = narrow(reinterpret_cast<uintptr_t>(offset), "Vertex attribute offset"); a.stride = stride ? UINT(stride) : UINT(size * 4);
    Device::attributeFormat(a); ++vao.revision;
}
void FORGE_GL_CALL EnableVertexAttribArray(U index) { auto& d = Device::get(); auto& v = d.arrays.at(d.array); v.attributes.at(index).enabled = true; ++v.revision; }
void FORGE_GL_CALL DisableVertexAttribArray(U index) { auto& d = Device::get(); auto& v = d.arrays.at(d.array); v.attributes.at(index).enabled = false; ++v.revision; }
void FORGE_GL_CALL VertexAttribPointer(U index, I size, U type, gl::B normalized, I stride, const void* offset) { attribute(index, size, type, normalized != 0, stride, offset); }
void FORGE_GL_CALL VertexAttribIPointer(U index, I size, U type, I stride, const void* offset) { attribute(index, size, type, false, stride, offset); }
void FORGE_GL_CALL VertexAttribDivisor(U index, U divisor) { auto& d = Device::get(); auto& v = d.arrays.at(d.array); v.attributes.at(index).divisor = divisor; ++v.revision; }
U FORGE_GL_CALL CreateShader(U kind) {
    auto& d = Device::get(); auto name = d.id(); Shader shader; shader.kind = kind; d.shaders.emplace(name, std::move(shader)); return name;
}
void FORGE_GL_CALL ShaderSource(U name, I count, const char* const* sources, const I* lengths) {
    auto& shader = Device::get().shaders.at(name); shader.source.clear();
    for (I i = 0; i < count; ++i) {
        if (!sources[i]) throw std::runtime_error("Null shader source");
        if (lengths && lengths[i] >= 0) shader.source.append(sources[i], size_t(lengths[i])); else shader.source += sources[i];
    }
    shader.compiled = false;
}
void FORGE_GL_CALL CompileShader(U name) {
    auto& shader = Device::get().shaders.at(name);
    // The complete program is compiled/linked together so user varyings keep the
    // same locations. Diagnostics from both compilers are returned at link time.
    shader.compiled = !shader.source.empty(); shader.error = shader.compiled ? "" : "Empty shader source";
}
void FORGE_GL_CALL GetShaderiv(U name, U property, I* result) {
    if (property != gl::COMPILE_STATUS) throw std::runtime_error("Unsupported shader query"); *result = Device::get().shaders.at(name).compiled;
}
void FORGE_GL_CALL GetShaderInfoLog(U name, I size, I* written, char* text) { logText(Device::get().shaders.at(name).error, size, written, text); }
void FORGE_GL_CALL DeleteShader(U name) { Device::get().shaders.erase(name); }
U FORGE_GL_CALL CreateProgram() { auto& d = Device::get(); auto name = d.id(); d.programs.emplace(name, Program{}); return name; }
void FORGE_GL_CALL AttachShader(U program, U shader) { Device::get().programs.at(program).shaders.push_back(shader); }
void FORGE_GL_CALL LinkProgram(U name) {
    auto& d = Device::get(); auto& p = d.programs.at(name);
    try {
        std::string vertex, fragment;
        for (auto shader : p.shaders) {
            auto& s = d.shaders.at(shader);
            if (!s.compiled) throw std::runtime_error(s.error);
            if (s.kind == gl::VERTEX_SHADER) vertex = s.source;
            else if (s.kind == gl::FRAGMENT_SHADER) fragment = s.source;
            else throw std::runtime_error("Forge supports vertex and fragment shaders");
        }
        if (vertex.empty() || fragment.empty()) throw std::runtime_error("A shader program requires both stages");
        d.buildProgram(p, vertex, fragment, "GLSL program", false);
    } catch (const std::exception& error) { p.linked = false; p.error = error.what(); }
}
void FORGE_GL_CALL GetProgramiv(U name, U property, I* result) {
    if (property != gl::LINK_STATUS) throw std::runtime_error("Unsupported program query"); *result = Device::get().programs.at(name).linked;
}
void FORGE_GL_CALL GetProgramInfoLog(U name, I size, I* written, char* text) { logText(Device::get().programs.at(name).error, size, written, text); }
void FORGE_GL_CALL DeleteProgram(U name) { auto& d = Device::get(); d.programs.erase(name); if (d.program == name) d.program = 0; }
void FORGE_GL_CALL UseProgram(U name) { auto& d = Device::get(); if (name && !d.programs.at(name).linked) throw std::runtime_error("Unlinked shader program"); d.program = name; }
I FORGE_GL_CALL GetUniformLocation(U program, const char* name) { return Device::get().location(program, name); }
void FORGE_GL_CALL Uniform1i(I location, I value) { Device::get().uniform(location, 1, 1, &value, true); }
void FORGE_GL_CALL Uniform1f(I location, float value) { Device::get().uniform(location, 1, 1, &value, false); }
void FORGE_GL_CALL Uniform2fv(I location, I count, const float* values) { Device::get().uniform(location, count, 2, values, false); }
void FORGE_GL_CALL Uniform3fv(I location, I count, const float* values) { Device::get().uniform(location, count, 3, values, false); }
void FORGE_GL_CALL Uniform4fv(I location, I count, const float* values) { Device::get().uniform(location, count, 4, values, false); }
void FORGE_GL_CALL UniformMatrix4fv(I location, I count, gl::B transpose, const float* values) {
    if (transpose) throw std::runtime_error("Forge matrices use column-major input"); Device::get().uniform(location, count, 16, values, false, true);
}
void FORGE_GL_CALL DrawArrays(U mode, I first, I count) { if (mode != gl::TRIANGLES) throw std::runtime_error("Forge draws triangles"); Device::get().draw(first, count, 1); }
void FORGE_GL_CALL DrawArraysInstanced(U mode, I first, I count, I instances) { if (mode != gl::TRIANGLES) throw std::runtime_error("Forge draws triangles"); Device::get().draw(first, count, instances); }
void FORGE_GL_CALL GenTextures(I count, U* names) { auto& d = Device::get(); for (I i = 0; i < count; ++i) { names[i] = d.id(); d.textures.emplace(names[i], Texture{}); } }
void FORGE_GL_CALL BindTexture(U target, U name) {
    if (target != gl::TEXTURE_2D) throw std::runtime_error("Forge uses 2D textures"); auto& d = Device::get();
    if (name && !d.textures.count(name)) throw std::runtime_error("Unknown texture"); d.boundTextures.at(d.textureUnit) = name;
}
void FORGE_GL_CALL ActiveTexture(U unit) {
    if (unit < gl::TEXTURE0 || unit >= gl::TEXTURE0 + 16) throw std::runtime_error("Texture unit must be 0..15"); Device::get().textureUnit = unit - gl::TEXTURE0;
}
std::vector<unsigned char> pixels(const void* input, I width, I height, U format, I alignment) {
    if (format != gl::RGBA && format != gl::RGB) throw std::runtime_error("Texture input must be RGB/RGBA8");
    size_t components = format == gl::RGBA ? 4 : 3;
    size_t stride = (size_t(width) * components + size_t(alignment) - 1) & ~(size_t(alignment) - 1);
    std::vector<unsigned char> output(size_t(width) * size_t(height) * 4);
    if (input) for (I y = 0; y < height; ++y) for (I x = 0; x < width; ++x) {
        auto source = static_cast<const unsigned char*>(input) + size_t(y) * stride + size_t(x) * components;
        auto dest = output.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
        std::memcpy(dest, source, components); if (components == 3) dest[3] = 255;
    }
    return output;
}
void FORGE_GL_CALL TexImage2D(U target, I level, I, I width, I height, I border, U format, U type, const void* input) {
    auto& d = Device::get();
    if (target != gl::TEXTURE_2D || level != 0 || border || width < 1 || height < 1 || width > 16384 || height > 16384)
        throw std::runtime_error("Invalid texture allocation");
    bool depth = format == gl::DEPTH_COMPONENT;
    if (depth) {
        if (input || type != gl::UNSIGNED_INT) throw std::runtime_error("Depth textures are render-target-only");
        d.allocateTexture(d.boundTexture(), width, height, true, false, nullptr);
    } else {
        if (type != gl::UNSIGNED_BYTE) throw std::runtime_error("Texture input must be uint8");
        auto bytes = pixels(input, width, height, format, d.unpack);
        d.allocateTexture(d.boundTexture(), width, height, false, false, input ? bytes.data() : nullptr);
    }
}
void FORGE_GL_CALL TexSubImage2D(U target, I level, I x, I y, I width, I height, U format, U type, const void* input) {
    auto& d = Device::get(); auto& image = d.boundTexture();
    if (target != gl::TEXTURE_2D || level || type != gl::UNSIGNED_BYTE || image.depth || !input || x < 0 || y < 0 || width < 0 || height < 0 || int64_t(x) + width > image.width || int64_t(y) + height > image.height)
        throw std::runtime_error("Invalid texture update");
    auto bytes = pixels(input, width, height, format, d.unpack);
    D3D11_BOX box{UINT(x), UINT(y), 0, UINT(x + width), UINT(y + height), 1};
    if (width && height) d.context->UpdateSubresource(image.gpu.Get(), 0, &box, bytes.data(), UINT(width) * 4, 0);
}
void FORGE_GL_CALL TexParameteri(U target, U field, I value) {
    if (target != gl::TEXTURE_2D) throw std::runtime_error("Forge uses 2D textures"); auto& image = Device::get().boundTexture();
    if (field == gl::TEXTURE_MIN_FILTER) image.minFilter = U(value);
    else if (field == gl::TEXTURE_MAG_FILTER) image.magFilter = U(value);
    else if (field == gl::TEXTURE_WRAP_S) image.wrapS = U(value);
    else if (field == gl::TEXTURE_WRAP_T) image.wrapT = U(value);
    else throw std::runtime_error("Unsupported texture parameter"); image.sampler.Reset();
}
void FORGE_GL_CALL GenerateMipmap(U target) {
    if (target != gl::TEXTURE_2D) throw std::runtime_error("Forge uses 2D textures"); auto& d = Device::get(); auto& image = d.boundTexture();
    if (image.depth) throw std::runtime_error("Depth mipmaps are not supported");
    if (!image.mipmaps) {
        Texture candidate;
        candidate.minFilter = image.minFilter; candidate.magFilter = image.magFilter; candidate.wrapS = image.wrapS; candidate.wrapT = image.wrapT;
        d.allocateTexture(candidate, image.width, image.height, false, true, nullptr);
        d.context->CopySubresourceRegion(candidate.gpu.Get(), 0, 0, 0, 0, image.gpu.Get(), 0, nullptr);
        image = std::move(candidate);
    }
    d.context->GenerateMips(image.srv.Get());
}
void FORGE_GL_CALL DeleteTextures(I count, const U* names) {
    auto& d = Device::get(); d.unbindTextures(); for (I i = 0; i < count; ++i) {
        d.textures.erase(names[i]); for (auto& bound : d.boundTextures) if (bound == names[i]) bound = 0;
    }
}
void FORGE_GL_CALL PixelStorei(U field, I value) {
    if (value != 1 && value != 2 && value != 4 && value != 8) throw std::runtime_error("Invalid pixel row alignment"); auto& d = Device::get();
    if (field == gl::PACK_ALIGNMENT) d.pack = value; else if (field == gl::UNPACK_ALIGNMENT) d.unpack = value; else throw std::runtime_error("Unsupported pixel storage option");
}
void FORGE_GL_CALL GetIntegerv(U field, I* value) {
    if (field != gl::MAX_TEXTURE_SIZE) throw std::runtime_error("Unsupported device query"); *value = 16384;
}
void FORGE_GL_CALL GenFramebuffers(I count, U* names) { auto& d = Device::get(); for (I i = 0; i < count; ++i) { names[i] = d.id(); d.framebuffers.emplace(names[i], Framebuffer{}); } }
void FORGE_GL_CALL BindFramebuffer(U target, U name) {
    if (target != gl::FRAMEBUFFER) throw std::runtime_error("Unsupported framebuffer target"); auto& d = Device::get();
    if (name && !d.framebuffers.count(name)) throw std::runtime_error("Unknown framebuffer"); d.unbindTextures(); d.framebuffer = name;
}
void FORGE_GL_CALL FramebufferTexture2D(U target, U attachment, U textureTarget, U name, I level) {
    if (target != gl::FRAMEBUFFER || textureTarget != gl::TEXTURE_2D || level) throw std::runtime_error("Invalid framebuffer attachment");
    auto& d = Device::get(); auto& fbo = d.framebuffers.at(d.framebuffer);
    if (attachment == gl::COLOR_ATTACHMENT0) fbo.color = name; else if (attachment == gl::DEPTH_ATTACHMENT) fbo.depth = name; else throw std::runtime_error("Unsupported framebuffer attachment");
}
U FORGE_GL_CALL CheckFramebufferStatus(U target) {
    if (target != gl::FRAMEBUFFER) throw std::runtime_error("Unsupported framebuffer target"); auto pair = Device::get().target();
    return pair.first && pair.second && pair.first->rtv && pair.second->dsv && pair.first->width == pair.second->width && pair.first->height == pair.second->height ? gl::FRAMEBUFFER_COMPLETE : 0x8CD6;
}
void FORGE_GL_CALL DeleteFramebuffers(I count, const U* names) {
    auto& d = Device::get(); for (I i = 0; i < count; ++i) { d.framebuffers.erase(names[i]); if (d.framebuffer == names[i]) d.framebuffer = 0; }
}
void FORGE_GL_CALL ReadPixels(I x, I y, I width, I height, U format, U type, void* output) {
    auto& d = Device::get(); auto target = d.target().first;
    bool flip = !d.framebuffer && d.composited;
    auto source = flip ? d.back.Get() : target ? target->gpu.Get() : nullptr;
    if (!source || type != gl::UNSIGNED_BYTE || (format != gl::RGB && format != gl::RGBA) || !output || x < 0 || y < 0 || width < 0 || height < 0)
        throw std::runtime_error("Invalid screenshot/readback request");
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    if (uint64_t(x) + width > desc.Width || uint64_t(y) + height > desc.Height) throw std::runtime_error("Screenshot rectangle exceeds target");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0; desc.MipLevels = 1;
    ComPtr<ID3D11Texture2D> staging;
    check(d.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()), "Create readback buffer");
    d.context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(d.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read screenshot");
    size_t channels = format == gl::RGBA ? 4 : 3;
    size_t stride = (size_t(width) * channels + size_t(d.pack) - 1) & ~(size_t(d.pack) - 1);
    for (I row = 0; row < height; ++row) {
        UINT sourceRow = flip ? desc.Height - 1 - UINT(y + row) : UINT(y + row);
        auto input = static_cast<const unsigned char*>(mapped.pData) + size_t(sourceRow) * mapped.RowPitch + size_t(x) * 4;
        auto dest = static_cast<unsigned char*>(output) + size_t(row) * stride;
        for (I col = 0; col < width; ++col) std::memcpy(dest + size_t(col) * channels, input + size_t(col) * 4, channels);
    }
    d.context->Unmap(staging.Get(), 0);
}
} // namespace commands
void Direct3D11::Impl::install() {
#define BIND(name) gl::name = &commands::name;
    BIND(Viewport) BIND(Scissor) BIND(ClearColor) BIND(Clear) BIND(Enable) BIND(Disable) BIND(BlendFunc) BIND(DepthMask)
    BIND(GenBuffers) BIND(BindBuffer) BIND(BufferData) BIND(BufferSubData) BIND(DeleteBuffers)
    BIND(GenVertexArrays) BIND(BindVertexArray) BIND(DeleteVertexArrays) BIND(EnableVertexAttribArray) BIND(DisableVertexAttribArray)
    BIND(VertexAttribPointer) BIND(VertexAttribIPointer) BIND(VertexAttribDivisor)
    BIND(CreateShader) BIND(ShaderSource) BIND(CompileShader) BIND(GetShaderiv) BIND(GetShaderInfoLog) BIND(DeleteShader)
    BIND(CreateProgram) BIND(AttachShader) BIND(LinkProgram) BIND(GetProgramiv) BIND(GetProgramInfoLog) BIND(DeleteProgram)
    BIND(UseProgram) BIND(GetUniformLocation) BIND(Uniform1i) BIND(Uniform1f) BIND(Uniform2fv) BIND(Uniform3fv) BIND(Uniform4fv) BIND(UniformMatrix4fv)
    BIND(DrawArrays) BIND(DrawArraysInstanced) BIND(GenTextures) BIND(BindTexture) BIND(ActiveTexture)
    BIND(TexImage2D) BIND(TexSubImage2D) BIND(TexParameteri) BIND(GenerateMipmap) BIND(DeleteTextures) BIND(PixelStorei)
    BIND(GenFramebuffers) BIND(BindFramebuffer) BIND(FramebufferTexture2D) BIND(CheckFramebufferStatus) BIND(DeleteFramebuffers)
    BIND(ReadPixels) BIND(GetIntegerv)
#undef BIND
}
Direct3D11::Direct3D11(GLFWwindow* window, const Json& options) : impl_(std::make_unique<Impl>()) {
    auto& d = *impl_;
    if (Impl::current) throw std::runtime_error("Only one Direct3D device is supported");
    d.debug = options.value("debug", false);
    auto requested = options.value("driver", std::string("auto"));
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = glfwGetWin32Window(window);
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    UINT flags = d.debug ? D3D11_CREATE_DEVICE_DEBUG : 0;
    auto create = [&](D3D_DRIVER_TYPE driver) {
        return D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, flags, &level, 1, D3D11_SDK_VERSION,
                                            &desc, d.swapchain.ReleaseAndGetAddressOf(), d.device.ReleaseAndGetAddressOf(),
                                            nullptr, d.context.ReleaseAndGetAddressOf());
    };
    HRESULT result;
    if (requested == "warp") { result = create(D3D_DRIVER_TYPE_WARP); d.driver = "warp"; }
    else if (requested == "hardware" || requested == "auto") {
        result = create(D3D_DRIVER_TYPE_HARDWARE); d.driver = "hardware";
        if (FAILED(result) && requested == "auto") {
            result = create(D3D_DRIVER_TYPE_WARP); d.driver = "warp";
            if (SUCCEEDED(result)) logger.write("WARN", "Direct3D hardware unavailable; using the WARP software driver");
        }
    } else throw std::runtime_error("renderer.direct3d11.driver must be auto, hardware, or warp");
    check(result, "Create Direct3D 11 feature-level 11_0 device (install Graphics Tools for debug=true)");
    d.context.As(&d.context1);
    ComPtr<IDXGIDevice> dxgi;
    check(d.device.As(&dxgi), "Query DXGI device");
    ComPtr<IDXGIAdapter> adapter;
    check(dxgi->GetAdapter(adapter.GetAddressOf()), "Query graphics adapter");
    DXGI_ADAPTER_DESC adapterDesc{};
    check(adapter->GetDesc(&adapterDesc), "Query adapter description");
    int size = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
    if (size > 0) {
        std::vector<char> utf8(static_cast<size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, utf8.data(), size, nullptr, nullptr);
        d.adapter = utf8.data();
    }
    ComPtr<IDXGIFactory> factory;
    check(adapter->GetParent(IID_IDXGIFactory, reinterpret_cast<void**>(factory.GetAddressOf())), "Query DXGI factory");
    check(factory->MakeWindowAssociation(desc.OutputWindow, DXGI_MWA_NO_ALT_ENTER), "Set window association");
    const std::string vertex = R"(
struct Output {float4 position:SV_Position;float2 uv:TEXCOORD0;};
Output main(uint id:SV_VertexID){
    float2 uv=float2((id<<1)&2,id&2); Output result;
    result.position=float4(uv.x*2-1,1-uv.y*2,0,1);
    result.uv=float2(uv.x,1-uv.y);return result;
})";
    const std::string fragment = "Texture2D image:register(t0);SamplerState sampling:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{return image.Sample(sampling,uv);}";
    auto vs = d.compile(vertex, "Presentation.vertex", "vs_5_0");
    auto ps = d.compile(fragment, "Presentation.fragment", "ps_5_0");
    check(d.device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, d.presentVertex.GetAddressOf()), "Create presentation vertex shader");
    check(d.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, d.presentPixel.GetAddressOf()), "Create presentation pixel shader");
    D3D11_SAMPLER_DESC sample{};
    sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sample.ComparisonFunc = D3D11_COMPARISON_NEVER;
    check(d.device->CreateSamplerState(&sample, d.presentSampler.GetAddressOf()), "Create presentation sampler");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    check(d.device->CreateRasterizerState(&raster, d.presentRaster.GetAddressOf()), "Create presentation rasterizer");
    const float floats[4] = {0, 0, 0, 1}; const int ints[4] = {0, 0, 0, 1};
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 16; buffer.Usage = D3D11_USAGE_IMMUTABLE; buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = floats;
    check(d.device->CreateBuffer(&buffer, &initial, d.zeroFloat.GetAddressOf()), "Create default float attribute");
    initial.pSysMem = ints;
    check(d.device->CreateBuffer(&buffer, &initial, d.zeroInt.GetAddressOf()), "Create default integer attribute");
    int width, height; glfwGetFramebufferSize(window, &width, &height);
    resize(std::max(1, width), std::max(1, height));
    Impl::current = &d; d.install();
    logger.write("INFO", "Graphics backend: Direct3D 11 / " + d.driver + " / " + d.adapter);
}
Direct3D11::~Direct3D11() {
    if (impl_->context) { impl_->context->ClearState(); impl_->context->Flush(); }
    if (Impl::current == impl_.get()) Impl::current = nullptr;
}
void Direct3D11::resize(int width, int height) {
    auto& d = *impl_;
    d.composited = false;
    if (width == d.width && height == d.height) return;
    if (width < 1 || height < 1 || width > 16384 || height > 16384) throw std::runtime_error("Invalid Direct3D framebuffer size");
    d.context->ClearState();
    d.backView.Reset(); d.back.Reset();
    check(d.swapchain->ResizeBuffers(0, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, 0), "Resize swapchain");
    check(d.swapchain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(d.back.GetAddressOf())), "Get swapchain buffer");
    check(d.device->CreateRenderTargetView(d.back.Get(), nullptr, d.backView.GetAddressOf()), "Create swapchain view");
    d.allocateTexture(d.screen, width, height, false, false, nullptr);
    d.allocateTexture(d.screenDepth, width, height, true, false, nullptr);
    d.width = width; d.height = height;
}
void Direct3D11::present(bool vsync) {
    auto& d = *impl_; d.compose();
    auto result = d.swapchain->Present(vsync ? 1 : 0, 0);
    if (FAILED(result)) {
        check(d.device->GetDeviceRemovedReason(), "Direct3D device removed");
        check(result, "Present Direct3D frame");
    }
}
Json Direct3D11::diagnostics() const {
    auto& d = *impl_;
    return {{"backend", "direct3d11"}, {"driver", d.driver}, {"adapter", d.adapter}, {"feature_level", "11_0"},
            {"shader_language", "glsl+hlsl"}, {"translated_programs", d.translatedPrograms}, {"native_programs", d.nativePrograms},
            {"presentation_bytes", size_t(d.width) * size_t(d.height) * 8}, {"debug", d.debug}};
}
unsigned Direct3D11::hlslProgram(const std::string& vertex, const std::string& fragment, const std::string& label) {
    auto& d = *impl_; Program program;
    d.buildProgram(program, vertex, fragment, label, true);
    auto name = d.id(); d.programs.emplace(name, std::move(program)); return name;
}
void Direct3D11::initializeEditor() {
#if FORGE_WITH_EDITOR
    if (!ImGui_ImplDX11_Init(impl_->device.Get(), impl_->context.Get())) throw std::runtime_error("Cannot initialize Direct3D editor");
#endif
}
void Direct3D11::shutdownEditor() {
#if FORGE_WITH_EDITOR
    ImGui_ImplDX11_Shutdown();
#endif
}
void Direct3D11::editorNewFrame() {
#if FORGE_WITH_EDITOR
    ImGui_ImplDX11_NewFrame();
#endif
}
void Direct3D11::editorDraw() {
#if FORGE_WITH_EDITOR
    // ImGui uses top-left pixels. Composite the GL-oriented scene first; then draw
    // the unchanged upstream DX11 backend into the actual top-left swapchain.
    impl_->compose();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
}
} // namespace forge
