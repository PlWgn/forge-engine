#pragma once
// Optional, adaptable OpenXR module. No runtime, renderer or game ownership.
#include <forge/types.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <memory>
namespace pybind11 { class module_; }
namespace forge {
void bindXr(pybind11::module_ &);
struct XrSettings {
    bool enabled = false;
    std::string space = "local";
    size_t eyeBudget = 256 * 1024 * 1024;
    double nearZ = .05, farZ = 1000, metersPerUnit = 1;
    Json definition = Json::object();
};
XrSettings xrSettings(const Json &);
bool xrCompiled();
// OpenGL clip convention, right handed, -Z forward; FOV angles in radians.
glm::dmat4 xrProjection(double left, double right, double down, double up, double nearZ, double farZ);
struct XrEye {
    glm::mat4 clipFromWorld{1};
    glm::vec3 position{0};
    unsigned framebuffer = 0;
    int width = 0, height = 0;
};
class XrSession {
  public:
    explicit XrSession(const XrSettings &, const std::string &applicationName);
    ~XrSession();
    XrSession(const XrSession &) = delete;
    XrSession &operator=(const XrSession &) = delete;
    void poll(); // poll lifecycle, wait/begin frame and sample predicted views/actions once
    void render(const std::function<void(const XrEye &)> &);
    void cancelFrame() noexcept; // submit no layers after failure, never retain acquired images
    bool closing() const;
    Json state() const; // detached snapshot, tracking validity must be checked by callers
    void origin(glm::dvec3 position, double yawDegrees);
    bool haptic(const std::string &hand, double amplitude, double duration, double frequency);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace forge
