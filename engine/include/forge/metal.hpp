#pragma once
#include <forge/types.hpp>
#include <functional>
struct GLFWwindow;
namespace forge {
// Adaptable macOS implementation. Objective-C/Metal types stay private.
class Metal {
  public:
    struct Impl;
    Metal(GLFWwindow *, const Json &options);
    ~Metal();
    void beginFrame(int width, int height);
    void renderFrame(const std::function<void()> &draw);
    void present(bool vsync);
    Json diagnostics() const;
    void initializeEditor();
    void shutdownEditor();
    void editorNewFrame();
    void editorDraw();
    unsigned mslProgram(const std::string &vertex, const std::string &fragment,
                        const std::string &vertexEntry, const std::string &fragmentEntry,
                        const std::string &label);

  private:
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
