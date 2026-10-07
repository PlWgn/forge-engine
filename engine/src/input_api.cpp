// Adaptable native input bindings; lifecycle is owned by the caller/scene listener.
#include <forge/engine.hpp>
#include <forge/input_api.hpp>
#include <forge/input_manager.hpp>
namespace forge {
namespace {
Runtime &runtime() {
    if (!active)
        throw std::runtime_error("Engine runtime is not active");
    return *active;
}
double scalar(py::handle value) {
    if (PyBool_Check(value.ptr()) || (!PyFloat_Check(value.ptr()) && !PyLong_Check(value.ptr())))
        throw std::invalid_argument("Input parameter must be a number");
    return py::cast<double>(value);
}
} // namespace
void bindInputManager(py::module_ &module) {
    py::class_<InputManager>(module, "InputManager")
        .def(py::init([](py::object actions, py::object deadzone) {
                 return InputManager(actions.is_none() ? Json::object() : fromPython(actions),
                                     scalar(deadzone));
             }),
             py::arg("actions") = py::none(), py::arg("deadzone") = py::float_(.15))
        .def(
            "define",
            [](InputManager &m, const std::string &action, py::object bindings,
               const std::string &context) { m.define(action, fromPython(bindings), context); },
            py::arg("action"), py::arg("bindings"), py::arg("context") = "default")
        .def(
            "bind",
            [](InputManager &m, const std::string &action, py::object bindings,
               const std::string &context, const std::string &conflict) {
                m.bind(action, fromPython(bindings), context, conflict);
            },
            py::arg("action"), py::arg("bindings"), py::arg("context") = "default",
            py::arg("conflict") = "share")
        .def(
            "conflicts",
            [](const InputManager &m, const std::string &action, py::object bindings,
               const std::string &context) {
                return pythonValue(m.conflicts(action, fromPython(bindings), context));
            },
            py::arg("action"), py::arg("bindings"), py::arg("context") = "default")
        .def("reset", &InputManager::reset, py::arg("action") = "", py::arg("context") = "")
        .def("push_context", &InputManager::pushContext)
        .def("pop_context", &InputManager::popContext)
        .def_property_readonly("context", &InputManager::context)
        .def_property_readonly("bindings",
                               [](const InputManager &m) { return pythonValue(m.bindings()); })
        .def_property_readonly("defaults",
                               [](const InputManager &m) { return pythonValue(m.defaults()); })
        .def("export_profile", [](const InputManager &m) { return pythonValue(m.exportProfile()); })
        .def("import_profile",
             [](InputManager &m, py::object profile) { m.importProfile(fromPython(profile)); })
        .def(
            "update",
            [](InputManager &m, py::object dt) {
                auto &r = runtime();
                m.update(InputFrame::parse(r.inputFrame), dt.is_none() ? r.dt : scalar(dt));
            },
            py::arg("dt") = py::none())
        .def(
            "update_snapshot",
            [](InputManager &m, py::object frame, py::object dt) {
                m.update(InputFrame::parse(fromPython(frame)), scalar(dt));
            },
            py::arg("frame"), py::arg("dt") = py::float_(0.0))
        .def("value", &InputManager::value)
        .def("raw_value", &InputManager::rawValue)
        .def("down", &InputManager::down)
        .def("pressed", &InputManager::pressed)
        .def("released", &InputManager::released)
        .def("held", &InputManager::held)
        .def(
            "repeat",
            [](const InputManager &m, const std::string &action, py::object delay,
               py::object interval) { return m.repeat(action, scalar(delay), scalar(interval)); },
            py::arg("action"), py::arg("delay") = py::float_(.4),
            py::arg("interval") = py::float_(.08))
        .def(
            "begin_rebind",
            [](InputManager &m, const std::string &action, py::kwargs options) {
                auto baseline = InputFrame::parse(runtime().inputFrame);
                m.beginRebind(action, fromPython(options), &baseline);
            },
            py::arg("action"))
        .def("cancel_rebind", &InputManager::cancelRebind)
        .def_property_readonly("capturing", &InputManager::capturing)
        .def("capture_state", [](const InputManager &m) { return pythonValue(m.captureState()); });
}
} // namespace forge
