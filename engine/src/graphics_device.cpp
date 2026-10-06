#include <forge/direct3d11.hpp>
#include <forge/gl.hpp>
#include <forge/graphics_device.hpp>
#include <forge/metal.hpp>
#if FORGE_WITH_EDITOR
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif
namespace forge {
namespace {
GraphicsDevice *device = nullptr;
}
std::vector<std::string> GraphicsDevice::backends() {
#if FORGE_WITH_DIRECT3D11
    return {"opengl", "direct3d11"};
#elif FORGE_WITH_METAL
    return {"opengl", "metal"};
#else
    return {"opengl"};
#endif
}
std::string GraphicsDevice::select(const Json &options) {
    auto name = options.value("backend", std::string("opengl"));
    if (name == "auto") {
#if FORGE_WITH_DIRECT3D11
        name = "direct3d11";
#elif FORGE_WITH_METAL
        name = "metal";
#else
        name = "opengl";
#endif
    }
    if (name != "opengl" && name != "direct3d11" && name != "metal")
        throw std::runtime_error("renderer.backend must be opengl, direct3d11, metal, or auto");
#if !FORGE_WITH_DIRECT3D11
    if (name == "direct3d11")
        throw std::runtime_error("Direct3D 11 is unavailable in this "
                                 "build/platform; select opengl or auto");
#endif
#if !FORGE_WITH_METAL
    if (name == "metal")
        throw std::runtime_error(
            "Metal is unavailable in this build/platform; select opengl or auto");
#endif
    return name;
}
GraphicsDevice::GraphicsDevice(GLFWwindow *window, const Json &options)
    : window_(window), backend_(select(options)) {
    if (device)
        throw std::runtime_error("Only one graphics device is supported per process");
    if (backend_ == "opengl") {
        glfwMakeContextCurrent(window_);
        gl::load();
    }
#if FORGE_WITH_DIRECT3D11
    if (backend_ == "direct3d11")
        direct3d_ =
            std::make_unique<Direct3D11>(window, options.value("direct3d11", Json::object()));
#endif
#if FORGE_WITH_METAL
    if (backend_ == "metal")
        metal_ = std::make_unique<Metal>(window, options.value("metal", Json::object()));
#endif
    device = this;
}
GraphicsDevice::~GraphicsDevice() {
    shutdownEditor();
#if FORGE_WITH_DIRECT3D11
    direct3d_.reset();
#endif
#if FORGE_WITH_METAL
    metal_.reset();
#endif
    if (device == this)
        device = nullptr;
}
GraphicsDevice &GraphicsDevice::current() {
    if (!device)
        throw std::runtime_error("No graphics device is active");
    return *device;
}
void GraphicsDevice::renderFrame(const std::function<void()> &draw) {
#if FORGE_WITH_METAL
    if (metal_) {
        metal_->renderFrame(draw);
        return;
    }
#endif
    draw();
}
void GraphicsDevice::beginFrame(int width, int height) {
#if FORGE_WITH_DIRECT3D11
    if (direct3d_)
        direct3d_->resize(width, height);
#else
    (void)width;
    (void)height;
#endif
#if FORGE_WITH_METAL
    if (metal_)
        metal_->beginFrame(width, height);
#endif
}
void GraphicsDevice::present(bool vsync) {
#if FORGE_WITH_METAL
    if (metal_) {
        metal_->present(vsync);
        return;
    }
#endif
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) {
        direct3d_->present(vsync);
        return;
    }
#endif
    if (vsync_ != int(vsync)) {
        glfwSwapInterval(vsync ? 1 : 0);
        vsync_ = int(vsync);
    }
    glfwSwapBuffers(window_);
}
Json GraphicsDevice::diagnostics() const {
#if FORGE_WITH_METAL
    if (metal_)
        return metal_->diagnostics();
#endif
#if FORGE_WITH_DIRECT3D11
    if (direct3d_)
        return direct3d_->diagnostics();
#endif
    return {{"backend", backend_}, {"shader_language", "glsl"}};
}
unsigned GraphicsDevice::hlslProgram(const std::string &vertex, const std::string &fragment,
                                     const std::string &label) {
#if FORGE_WITH_DIRECT3D11
    if (direct3d_)
        return direct3d_->hlslProgram(vertex, fragment, label);
#else
    (void)vertex;
    (void)fragment;
    (void)label;
#endif
    throw std::runtime_error("Native HLSL programs require Direct3D 11");
}
unsigned GraphicsDevice::mslProgram(const std::string &vertex, const std::string &fragment,
                                    const std::string &ve, const std::string &fe,
                                    const std::string &label) {
#if FORGE_WITH_METAL
    if (metal_)
        return metal_->mslProgram(vertex, fragment, ve, fe, label);
#else
    (void)vertex;
    (void)fragment;
    (void)ve;
    (void)fe;
    (void)label;
#endif
    throw std::runtime_error("Native MSL programs require Metal");
}
void GraphicsDevice::initializeEditor() {
#if FORGE_WITH_EDITOR
    if (editor_)
        return;
    bool input = backend_ == "opengl" ? ImGui_ImplGlfw_InitForOpenGL(window_, true)
                                      : ImGui_ImplGlfw_InitForOther(window_, true);
    if (!input)
        throw std::runtime_error("Cannot initialize GLFW editor input");
    try {
#if FORGE_WITH_DIRECT3D11
        if (direct3d_)
            direct3d_->initializeEditor();
#endif
#if FORGE_WITH_METAL
        if (metal_)
            metal_->initializeEditor();
#endif
        if (backend_ == "opengl" && !ImGui_ImplOpenGL3_Init("#version 330 core"))
            throw std::runtime_error("Cannot initialize OpenGL editor");
    } catch (...) {
        ImGui_ImplGlfw_Shutdown();
        throw;
    }
    editor_ = true;
#else
    throw std::runtime_error("Builtin editor shell is not compiled");
#endif
}
void GraphicsDevice::shutdownEditor() {
#if FORGE_WITH_EDITOR
    if (!editor_)
        return;
#if FORGE_WITH_DIRECT3D11
    if (direct3d_)
        direct3d_->shutdownEditor();
#endif
#if FORGE_WITH_METAL
    if (metal_)
        metal_->shutdownEditor();
#endif
    if (backend_ == "opengl")
        ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    editor_ = false;
#endif
}
void GraphicsDevice::editorNewFrame() {
#if FORGE_WITH_EDITOR
#if FORGE_WITH_DIRECT3D11
    if (direct3d_)
        direct3d_->editorNewFrame();
#endif
#if FORGE_WITH_METAL
    if (metal_)
        metal_->editorNewFrame();
#endif
    if (backend_ == "opengl")
        ImGui_ImplOpenGL3_NewFrame();
#endif
}
void GraphicsDevice::editorDraw() {
#if FORGE_WITH_EDITOR
#if FORGE_WITH_DIRECT3D11
    if (direct3d_)
        direct3d_->editorDraw();
#endif
#if FORGE_WITH_METAL
    if (metal_)
        metal_->editorDraw();
#endif
    if (backend_ == "opengl")
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}
} // namespace forge
