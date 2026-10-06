#include <forge/engine.hpp>
#include <forge/network.hpp>
#include <forge/steam.hpp>
#include <pybind11/stl.h>
namespace forge {
namespace {
Runtime &runtime() {
    if (!active)
        throw std::runtime_error("Network API requires an active runtime");
    active->network->checkThread();
    return *active;
}
void effect() {
    auto &r = runtime();
    if (r.reloading || r.authoringTransaction || r.tearingDown)
        throw std::runtime_error("Network/Steam effects are unavailable during reload, authoring "
                                 "preparation or scene teardown");
}
} // namespace
FORGE_MODULE(network_api) {
    py::class_<NetworkHost, std::shared_ptr<NetworkHost>>(module, "_NetworkHost")
        .def(
            "connect",
            [](NetworkHost &h, const std::string &address, unsigned port) {
                effect();
                return h.connect(address, port);
            },
            py::arg("address"), py::arg("port") = 0)
        .def(
            "send",
            [](NetworkHost &h, uint64_t peer, py::bytes value, unsigned channel, bool reliable) {
                effect();
                char *bytes = nullptr;
                Py_ssize_t size = 0;
                if (PyBytes_AsStringAndSize(value.ptr(), &bytes, &size))
                    throw py::error_already_set();
                return h.send(peer, std::string_view(bytes, size_t(size)), channel, reliable);
            },
            py::arg("peer"), py::arg("data"), py::arg("channel") = 0, py::arg("reliable") = true)
        .def(
            "disconnect",
            [](NetworkHost &h, uint64_t peer, uint32_t reason) {
                effect();
                h.disconnect(peer, reason);
            },
            py::arg("peer"), py::arg("reason") = 0)
        .def(
            "poll",
            [](NetworkHost &h, unsigned limit) {
                effect();
                py::list events;
                for (auto &e : h.poll(limit)) {
                    py::dict value;
                    value["type"] = e.type;
                    value["peer"] = e.peer;
                    value["address"] = e.address;
                    value["identity"] = e.identity;
                    value["channel"] = e.channel;
                    value["reliable"] = e.reliable;
                    value["reason"] = e.reason;
                    value["error"] = e.error;
                    if (e.type == "message")
                        value["data"] = py::bytes(e.data);
                    events.append(std::move(value));
                }
                return events;
            },
            py::arg("limit") = 256)
        .def("stats", [](NetworkHost &h) { return pythonValue(h.stats()); })
        .def("close",
             [](NetworkHost &h) {
                 auto &r = runtime();
                 if (r.tearingDown)
                     return;
                 effect();
                 h.close();
             })
        .def_property_readonly("closed", [](NetworkHost &h) {
            runtime();
            return h.closed();
        });
    module.def("network_backends", &networkBackends);
    module.def(
        "network_host",
        [](py::dict settings, const std::string &name, const std::string &lifetime) {
            auto &r = runtime();
            if (r.tearingDown)
                throw std::runtime_error("Cannot create hosts during scene teardown");
            if (lifetime != "scene" && lifetime != "application")
                throw std::invalid_argument("Network lifetime must be scene or application");
            if (!r.world.networkScope)
                r.world.networkScope = std::make_shared<NetworkScope>();
            return r.network->create(networkOptions(fromPython(settings)), name,
                                     r.world.networkScope, lifetime == "application");
        },
        py::arg("settings") = py::dict(), py::arg("name") = "", py::arg("lifetime") = "scene");
    module.def("steam_info", []() {
        auto &r = runtime();
        return pythonValue(
            r.steam ? r.steam->info()
                    : Json{{"compiled", steamCompiled()},
                           {"enabled", false},
                           {"app_id",
                            r.config.data.value("steam", Json::object()).value("app_id", 480)}});
    });
    module.def(
        "steam_poll",
        [](unsigned limit) {
            effect();
            auto &r = runtime();
            if (!r.steam)
                throw std::runtime_error("Steam service is not started");
            return pythonValue(r.steam->poll(limit));
        },
        py::arg("limit") = 256);
    module.def(
        "steam_call",
        [](const std::string &operation, py::dict args) {
            effect();
            auto &r = runtime();
            if (!r.steam)
                throw std::runtime_error("Steam service is not started");
            return pythonValue(r.steam->call(operation, fromPython(args)));
        },
        py::arg("operation"), py::arg("args") = py::dict());
}
} // namespace forge
