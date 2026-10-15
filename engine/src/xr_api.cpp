// Independent OpenXR Python facade. Owns no interpreter, renderer, or listeners.
#include <forge/engine.hpp>
#include <forge/xr.hpp>
namespace forge {
namespace {
Renderer &renderer() {
    if (!active) throw std::runtime_error("Engine runtime is not active");
    auto &r = *active;
    if (r.initializing || r.reloading || r.authoringTransaction || r.tearingDown)
        throw std::runtime_error("XR side effects are unavailable during candidate/teardown transactions");
    if (!r.renderer) throw std::runtime_error("XR requires a graphical runtime");
    return *r.renderer;
}
double scalar(const Json &value) {
    if (!value.is_number()) throw std::runtime_error("XR parameters must be numbers, excluding booleans");
    return value.get<double>();
}
}
void bindXr(py::module_ &module) {
    module.def("xr_state", []() { return pythonValue(active && active->renderer ? active->renderer->xrState() : Json{{"enabled", false}}); });
    module.def("xr_set_origin", [](py::object position, py::object yaw) {
        auto p = fromPython(position);
        if (!p.is_array() || p.size() != 3) throw std::runtime_error("XR position must have exactly three numbers");
        glm::dvec3 next(scalar(p[0]), scalar(p[1]), scalar(p[2]));
        auto angle = scalar(fromPython(yaw));
        renderer().xrOrigin(next, angle);
    }, py::arg("position"), py::arg("yaw") = 0.0);
    module.def("xr_haptic", [](const std::string &hand, py::object amplitude, py::object duration, py::object frequency) {
        double a = scalar(fromPython(amplitude)), d = scalar(fromPython(duration)), f = scalar(fromPython(frequency));
        return renderer().xrHaptic(hand, a, d, f);
    }, py::arg("hand"), py::arg("amplitude") = .5, py::arg("duration") = .1, py::arg("frequency") = 0.0);
}
} // namespace forge
