#include <forge/graphics_device.hpp>
#include <forge/direct3d11.hpp>
#include <forge/gl.hpp>
#if FORGE_WITH_EDITOR
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif
namespace forge {
namespace { GraphicsDevice* device = nullptr; }
std::vector<std::string> GraphicsDevice::backends() {
#if FORGE_WITH_DIRECT3D11
    return {"opengl", "direct3d11"};
#else
    return {"opengl"};
#endif
}
std::string GraphicsDevice::select(const Json& options) {
    auto name = options.value("backend", std::string("opengl"));
    if (name == "auto") {
#if FORGE_WITH_DIRECT3D11
        name = "direct3d11";
#else
        name = "opengl";
#endif
    }
    if (name != "opengl" && name != "direct3d11")
        throw std::runtime_error("renderer.backend must be opengl, direct3d11, or auto");
#if !FORGE_WITH_DIRECT3D11
    if (name == "direct3d11")
        throw std::runtime_error("Direct3D 11 is unavailable in this build/platform; select opengl or auto");
#endif
    return name;
}
GraphicsDevice::GraphicsDevice(GLFWwindow* window, const Json& options)
    : window_(window), backend_(select(options)) {
    if (device) throw std::runtime_error("Only one graphics device is supported per process");
#if FORGE_WITH_DIRECT3D11
    if (backend_ == "direct3d11")
        direct3d_ = std::make_unique<Direct3D11>(window, options.value("direct3d11", Json::object()));
    else
#endif
    { glfwMakeContextCurrent(window_); gl::load(); }
    device = this;
}
GraphicsDevice::~GraphicsDevice() {
    shutdownEditor();
#if FORGE_WITH_DIRECT3D11
    direct3d_.reset();
#endif
    if (device == this) device = nullptr;
}
GraphicsDevice& GraphicsDevice::current() {
    if (!device) throw std::runtime_error("No graphics device is active");
    return *device;
}
void GraphicsDevice::beginFrame(int width, int height) {
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) direct3d_->resize(width, height);
#else
    (void)width; (void)height;
#endif
}
void GraphicsDevice::present(bool vsync) {
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) { direct3d_->present(vsync); return; }
#endif
    if (vsync_ != int(vsync)) { glfwSwapInterval(vsync ? 1 : 0); vsync_ = int(vsync); }
    glfwSwapBuffers(window_);
}
Json GraphicsDevice::diagnostics() const {
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) return direct3d_->diagnostics();
#endif
    return {{"backend", backend_}, {"shader_language", "glsl"}};
}
unsigned GraphicsDevice::hlslProgram(const std::string& vertex, const std::string& fragment,
                                    const std::string& label) {
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) return direct3d_->hlslProgram(vertex, fragment, label);
#else
    (void)vertex; (void)fragment; (void)label;
#endif
    throw std::runtime_error("Native HLSL programs require Direct3D 11");
}
void GraphicsDevice::initializeEditor() {
#if FORGE_WITH_EDITOR
    if (editor_) return;
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) {
        if (!ImGui_ImplGlfw_InitForOther(window_, true)) throw std::runtime_error("Cannot initialize GLFW editor input");
        try { direct3d_->initializeEditor(); } catch (...) { ImGui_ImplGlfw_Shutdown(); throw; }
    } else
#endif
    {
        if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) throw std::runtime_error("Cannot initialize GLFW editor input");
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
            ImGui_ImplGlfw_Shutdown(); throw std::runtime_error("Cannot initialize OpenGL editor");
        }
    }
    editor_ = true;
#else
    throw std::runtime_error("Builtin editor shell is not compiled");
#endif
}
void GraphicsDevice::shutdownEditor() {
#if FORGE_WITH_EDITOR
    if (!editor_) return;
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) direct3d_->shutdownEditor(); else
#endif
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    editor_ = false;
#endif
}
void GraphicsDevice::editorNewFrame() {
#if FORGE_WITH_EDITOR
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) direct3d_->editorNewFrame(); else
#endif
    ImGui_ImplOpenGL3_NewFrame();
#endif
}
void GraphicsDevice::editorDraw() {
#if FORGE_WITH_EDITOR
#if FORGE_WITH_DIRECT3D11
    if (direct3d_) direct3d_->editorDraw(); else
#endif
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}
}
