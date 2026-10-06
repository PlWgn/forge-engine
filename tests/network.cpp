#include <chrono>
#include <forge/network.hpp>
#include <forge/steam.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace forge;
void require(bool yes, const char *message) {
    if (!yes)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid operation accepted");
}
struct QueueState {
    std::vector<NetworkEvent> packets;
    bool disconnected = false;
    size_t created = 0;
};
class QueueTransport final : public NetworkTransport {
    std::shared_ptr<QueueState> state;

  public:
    explicit QueueTransport(std::shared_ptr<QueueState> s) : state(std::move(s)) {
        ++state->created;
    }
    uint64_t connect(const std::string &, uint16_t) override {
        return 1;
    }
    bool send(uint64_t, std::string_view, unsigned, bool) override {
        return false;
    }
    void disconnect(uint64_t, uint32_t) override {
        state->disconnected = true;
    }
    std::vector<NetworkEvent> service(unsigned budget) override {
        std::vector<NetworkEvent> out;
        while (!state->packets.empty() && out.size() < budget) {
            out.push_back(std::move(state->packets.back()));
            state->packets.pop_back();
        }
        return out;
    }
    NetworkJson stats() const override {
        return NetworkJson::object();
    }
};
void bounds() {
    auto custom = networkOptions({{"transport_settings", {{"label", "custom"}}}});
    require(custom.transportSettings["label"] == "custom", "Custom factory options lost");
    auto changed = custom;
    changed.transportSettings["label"] = "other";
    require(!(changed == custom), "Custom options ignored for session adoption");
    auto state = std::make_shared<QueueState>();
    registerNetworkTransport("test_queue", [state](const NetworkOptions &) {
        return std::make_unique<QueueTransport>(state);
    });
    rejects([&] {
        registerNetworkTransport(
            "steam_ip", [](const NetworkOptions &) { return std::unique_ptr<NetworkTransport>(); });
    });
    rejects([&] {
        registerNetworkTransport("test_queue", [](const NetworkOptions &) {
            return std::unique_ptr<NetworkTransport>();
        });
    });
    for (bool reliable : {false, true}) {
        state->disconnected = false;
        auto o = networkOptions({{"backend", "test_queue"},
                                 {"queue_events", 8},
                                 {"max_message_bytes", 8},
                                 {"queue_bytes", 16}});
        NetworkHost host(o);
        std::thread worker([&] {
            rejects([&] { NetworkHost other(o); });
            rejects([&] { host.poll(); });
        });
        worker.join();
        for (unsigned i = 0; i < 20; ++i) {
            NetworkEvent e;
            e.type = "message";
            e.peer = 1;
            e.data = "12345678";
            e.reliable = reliable;
            state->packets.push_back(e);
        }
        for (unsigned pass = 0; pass < 20; ++pass)
            host.pump();
        auto stats = host.stats();
        require(stats["queued_bytes"] == 16 && stats["queued_events"] == 2 &&
                    stats["dropped_messages"] == 18,
                "Receive budget exceeded or drop accounting failed");
        require(state->disconnected == reliable,
                "Reliable overflow did not disconnect or unreliable overflow disconnected");
        require(!host.send(1, "retry"), "Transport backpressure not exposed");
        require(host.poll().size() == 2 && host.stats()["queued_bytes"] == 0,
                "Draining queue retained bytes");
    }
    NetworkService service;
    auto options = networkOptions({{"backend", "test_queue"}});
    auto created = state->created;
    rejects([&] { service.create(options, "", nullptr, false); });
    require(state->created == created, "Rejected host creation opened a transport");
    auto scope = std::make_shared<NetworkScope>();
    for (unsigned i = 0; i < 2048; ++i) {
        auto h = service.create(options, "", scope, false);
        h->close();
    }
    require(scope->hosts.size() <= 64, "Closed hosts accumulated scene lifetime records");
    auto ordinary = service.create(options, "ordinary", scope, true);
    service.rollback();
    require(!ordinary->closed(), "Rollback closed a session created outside preparation");
    service.checkpoint();
    for (unsigned i = 0; i < 128; ++i) {
        auto h = service.create(options, "retry", scope, true);
        h->close();
    }
    auto final = service.create(options, "retry", scope, true);
    service.rollback();
    require(final->closed() && !ordinary->closed(),
            "Recreated candidate ownership changed committed session");
    service.close();
}
void transport(const std::string &backend) {
    auto o =
        networkOptions({{"backend", backend}, {"bind", "127.0.0.1"}, {"max_message_bytes", 65536}});
    NetworkHost server(o);
    auto port = server.stats().at("port").get<unsigned>();
    require(port > 0, "Ephemeral listen port missing");
    o.bind = "";
    NetworkHost client(o);
    auto peer = client.connect("127.0.0.1", port);
    uint64_t remote = 0;
    auto until = [&](auto done) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (!done()) {
            require(std::chrono::steady_clock::now() < deadline, "Network exchange timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    bool connected = false;
    until([&]() {
        for (auto &e : server.poll())
            if (e.type == "connected")
                remote = e.peer;
        for (auto &e : client.poll())
            if (e.type == "connected")
                connected = true;
        return connected && remote;
    });
    rejects([&] { client.send(peer, "x", 32); });
    rejects([&] { client.send(peer, std::string(65537, 'x')); });
    rejects([&] { client.connect("not-an-ip", port); });
    std::vector<std::string> payloads = {std::string("binary\0text", 11), "",
                                         std::string(65536, 'Z')};
    for (auto &bytes : payloads)
        require(client.send(peer, bytes, 2, true), "Reliable send refused unexpectedly");
    unsigned received = 0;
    until([&]() {
        client.pump();
        for (auto &e : server.poll())
            if (e.type == "message") {
                require(e.reliable && e.channel == 2, "Message flags/channel changed");
                require(received < payloads.size() && e.data == payloads[received],
                        "Ordered binary message corrupted");
                ++received;
            }
        return received == payloads.size();
    });
    require(server.send(remote, "unreliable", 1, false), "Unreliable send refused");
    bool got = false;
    until([&]() {
        server.pump();
        for (auto &e : client.poll())
            if (e.type == "message") {
                require(e.data == "unreliable" && !e.reliable && e.channel == 1,
                        "Unreliable message corrupted");
                got = true;
            }
        return got;
    });
    client.disconnect(peer, 0);
    bool disconnected = false;
    until([&]() {
        client.pump();
        for (auto &e : server.poll())
            if (e.type == "disconnected")
                disconnected = true;
        return disconnected;
    });
    rejects([&] { server.send(remote, "stale"); });
    client.close();
    rejects([&] { client.poll(); });
    require(client.stats()["closed"], "Closed status missing");
    NetworkService service;
    auto scope = std::make_shared<NetworkScope>();
    o.bind = "127.0.0.1";
    auto scoped = service.create(o, "", scope, false);
    std::weak_ptr<NetworkHost> abandoned;
    {
        auto temporary = service.create(o, "", scope, false);
        abandoned = temporary;
        std::thread worker([value = std::move(temporary)]() mutable { value.reset(); });
        worker.join();
    }
    require(!abandoned.expired(), "Worker released transport outside runtime servicing");
    service.pump();
    require(abandoned.expired(), "Unused host was not released by runtime servicing");
    scope.reset();
    require(scoped->closed(), "Scene host leaked");
    scope = std::make_shared<NetworkScope>();
    service.checkpoint();
    auto named = service.create(o, "session", scope, true);
    service.commit();
    require(service.create(o, "session", scope, true) == named, "Named session not adopted");
    auto different = o;
    different.channels = 2;
    rejects([&] { service.create(different, "session", scope, true); });
    service.checkpoint();
    auto rejected = service.create(o, "candidate", scope, true);
    service.rollback();
    require(rejected->closed() && !named->closed(), "Rejected candidate changed live session");
    service.close();
    require(named->closed(), "Application shutdown leaked host");
    std::cout << backend << " real LAN exchange and lifetime checks passed\n";
}
void lanPendingBudget() {
    auto o = networkOptions({{"bind", "127.0.0.1"},
                             {"max_message_bytes", 64},
                             {"queue_bytes", 1024},
                             {"queue_events", 8}});
    NetworkHost server(o);
    o.bind = "";
    NetworkHost client(o);
    auto peer = client.connect("127.0.0.1", server.stats()["port"].get<unsigned>());
    auto until = [&](auto ready) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ready()) {
            require(std::chrono::steady_clock::now() < deadline, "LAN pending queue did not drain");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    bool connected = false, accepted = false;
    until([&] {
        for (auto &event : server.poll())
            if (event.type == "connected")
                accepted = true;
        for (auto &event : client.poll())
            if (event.type == "connected")
                connected = true;
        return connected && accepted;
    });
    for (unsigned i = 0; i < 8; ++i)
        require(client.send(peer, ""), "Empty packet refused below packet budget");
    require(!client.send(peer, ""), "Zero-byte packets bypassed outgoing message budget");
    until([&] {
        server.poll();
        client.poll();
        return client.stats()["peers"][0]["pending_messages"] == 0;
    });
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        require(client.send(peer, std::string(64, 'x')), "Drained packets retained queue capacity");
        until([&] {
            server.poll();
            client.poll();
            return client.stats()["peers"][0]["pending_messages"] == 0;
        });
        require(client.stats()["peers"][0]["pending_bytes"] == 0,
                "Acknowledged payload remained pending");
    }
    std::cout << "LAN pending byte/message budgets passed\n";
}
int main() {
    try {
        for (auto j : {NetworkJson{{"transport_settings", false}},
                       NetworkJson{{"transport_settings",
                                    {{"value", std::numeric_limits<double>::infinity()}}}},
                       NetworkJson{{"transport_settings", {{"large", std::string(16385, 'x')}}}},
                       NetworkJson{{"channels", true}}, NetworkJson{{"port", -1}},
                       NetworkJson{{"queue_bytes", 1}}, NetworkJson{{"poll_budget", 0}}})
            rejects([&] { networkOptions(j); });
        for (auto j : {NetworkJson{{"app_id", false}}, NetworkJson{{"app_id", 0}},
                       NetworkJson{{"enabled", 1}}})
            rejects([&] { validateSteamSettings(j); });
        SteamClient disabled({{"enabled", false}, {"app_id", 480}});
        require(!disabled.info()["enabled"].get<bool>(), "Steam initialized implicitly");
        bounds();
        for (auto &backend : networkBackends())
            if (backend == "lan" || backend == "sockets") {
                transport(backend);
                if (backend == "lan")
                    lanPendingBudget();
            }
        std::cout << "Network checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
