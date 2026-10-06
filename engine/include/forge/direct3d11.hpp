#pragma once
#include <forge/types.hpp>
struct GLFWwindow;
namespace forge {
// Independent Windows implementation; no D3D types leak into engine contracts.
class Direct3D11 {
public:
    struct Impl;
    Direct3D11(GLFWwindow*, const Json& options);
    ~Direct3D11();
    void resize(int width, int height);
    void present(bool vsync);
    Json diagnostics() const;
    void initializeEditor();
    void shutdownEditor();
    void editorNewFrame();
    void editorDraw();
    unsigned hlslProgram(const std::string&, const std::string&, const std::string&);
private:
    std::unique_ptr<Impl> impl_;
};
}
