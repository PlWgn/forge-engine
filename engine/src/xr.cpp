#include <forge/xr.hpp>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace forge {
XrSettings xrSettings(const Json &data) {
    if (!data.is_object()) throw std::runtime_error("xr must be an object");
    XrSettings result;
    result.definition = data;
    if (data.contains("enabled")) {
        if (!data["enabled"].is_boolean()) throw std::runtime_error("xr.enabled must be boolean");
        result.enabled = data["enabled"];
    }
    if (data.contains("reference_space")) {
        if (!data["reference_space"].is_string()) throw std::runtime_error("xr.reference_space must be local or stage");
        result.space = data["reference_space"];
    }
    if (result.space != "local" && result.space != "stage") throw std::runtime_error("xr.reference_space must be local or stage");
    auto number = [&](const char *key, double fallback, double minimum, double maximum) {
        if (!data.contains(key)) return fallback;
        const auto &value = data[key];
        if (!value.is_number() || value.is_boolean()) throw std::runtime_error(std::string("xr.") + key + " must be a number");
        double n = value.get<double>();
        if (!std::isfinite(n) || n < minimum || n > maximum) throw std::runtime_error(std::string("xr.") + key + " is out of range");
        return n;
    };
    if (data.contains("eye_budget_bytes")) {
        auto value = data["eye_budget_bytes"];
        if (!value.is_number_integer() || value < 1024*1024 || value > 1024*1024*1024)
            throw std::runtime_error("xr.eye_budget_bytes must be an integer in 1MiB..1GiB");
        result.eyeBudget = value.get<size_t>();
    }
    result.nearZ = number("near", .05, .001, 1000);
    result.farZ = number("far", 1000, .002, 100000);
    result.metersPerUnit = number("meters_per_unit", 1, .0001, 10000);
    if (result.farZ <= result.nearZ) throw std::runtime_error("xr.far must be greater than xr.near");
    return result;
}
bool xrCompiled() { return FORGE_WITH_OPENXR != 0; }
glm::dmat4 xrProjection(double left, double right, double down, double up, double nearZ, double farZ) {
    for (double value : {left, right, down, up, nearZ, farZ})
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite XR projection");
    constexpr double halfPi = 1.5707963267948966;
    if (left <= -halfPi || right >= halfPi || down <= -halfPi || up >= halfPi ||
        left >= right || down >= up || nearZ <= 0 || farZ <= nearZ)
        throw std::runtime_error("Invalid XR projection");
    double l = std::tan(left), r = std::tan(right), d = std::tan(down), u = std::tan(up);
    glm::dmat4 m(0);
    m[0][0] = 2 / (r - l); m[1][1] = 2 / (u - d);
    m[2][0] = (r + l) / (r - l); m[2][1] = (u + d) / (u - d);
    m[2][2] = -(farZ + nearZ) / (farZ - nearZ); m[2][3] = -1;
    m[3][2] = -2 * farZ * nearZ / (farZ - nearZ);
    for (int c=0;c<4;++c) for(int r=0;r<4;++r)
        if (!std::isfinite(m[c][r]) || std::abs(m[c][r]) > std::numeric_limits<float>::max())
            throw std::runtime_error("XR projection exceeds finite float range");
    return m;
}
#if !FORGE_WITH_OPENXR
struct XrSession::Impl {};
XrSession::XrSession(const XrSettings &, const std::string &) { throw std::runtime_error("OpenXR was not compiled; use compile --openxr on Windows or the Android target"); }
XrSession::~XrSession() = default;
void XrSession::poll() {}
void XrSession::render(const std::function<void(const XrEye &)> &) {}
void XrSession::cancelFrame() noexcept {}
bool XrSession::closing() const { return false; }
Json XrSession::state() const { return {{"enabled", false}}; }
void XrSession::origin(glm::dvec3, double) { throw std::runtime_error("XR is disabled"); }
bool XrSession::haptic(const std::string &, double, double, double) { throw std::runtime_error("XR is disabled"); }
#endif
} // namespace forge
