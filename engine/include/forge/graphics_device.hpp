#pragma once
#include <forge/types.hpp>
#include <functional>
struct GLFWwindow;
namespace forge {
class Direct3D11;
class Metal;
// Adaptable device boundary. Scene/resources issue the existing small GL-shaped
// command vocabulary; the chosen backend owns its implementation and
// presentation.
class GraphicsDevice {
  public:
    static std::string select(const Json &renderer);
    static std::vector<std::string> backends();
    GraphicsDevice(GLFWwindow *, const Json &renderer);
    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice &) = delete;
    GraphicsDevice &operator=(const GraphicsDevice &) = delete;
    const std::string &backend() const { return backend_; }
    void beginFrame(int width, int height);
    void renderFrame(const std::function<void()> &draw);
    void present(bool vsync);
    Json diagnostics() const;
    void initializeEditor();
    void shutdownEditor();
    void editorNewFrame();
    void editorDraw();
    unsigned hlslProgram(const std::string &vertex, const std::string &fragment,
                         const std::string &label);
    unsigned mslProgram(const std::string &, const std::string &, const std::string &,
                        const std::string &, const std::string &);
    static GraphicsDevice &current();

  private:
    GLFWwindow *window_;
    std::string backend_;
#if FORGE_WITH_DIRECT3D11
    std::unique_ptr<Direct3D11> direct3d_;
#endif
#if FORGE_WITH_METAL
    std::unique_ptr<Metal> metal_;
#endif
    bool editor_ = false;
    int vsync_ = -1;
};
} // namespace forge
