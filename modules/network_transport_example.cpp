// Optional: add this file to native_modules, compile, then Host(backend="example_echo").
#include <forge/engine.hpp>
#include <forge/network.hpp>
namespace {
class Echo final : public forge::NetworkTransport {
    forge::NetworkOptions options;
    std::string label;
    std::deque<forge::NetworkEvent> events;
    size_t bytes = 0;
    bool connected = false;
    uint64_t currentPeer = 0, nextPeer = 1;

  public:
    explicit Echo(const forge::NetworkOptions &o)
        : options(o), label(o.transportSettings.value("label", "Example")) {
        if (label.size() > 128)
            throw std::invalid_argument("Echo label exceeds 128 bytes");
    }
    uint64_t connect(const std::string &, uint16_t) override {
        if (connected)
            throw std::runtime_error("Echo already connected");
        connected = true;
        currentPeer = nextPeer++;
        forge::NetworkEvent e;
        e.type = "connected";
        e.peer = currentPeer;
        events.push_back(std::move(e));
        return currentPeer;
    }
    bool send(uint64_t peer, std::string_view data, unsigned channel, bool reliable) override {
        if (peer != currentPeer || !connected)
            throw std::runtime_error("Echo peer unavailable");
        if (events.size() >= options.queueEvents || bytes + data.size() > options.queueBytes)
            return false;
        forge::NetworkEvent e;
        e.type = "message";
        e.peer = currentPeer;
        e.data.assign(data);
        e.channel = channel;
        e.reliable = reliable;
        bytes += data.size();
        events.push_back(std::move(e));
        return true;
    }
    void disconnect(uint64_t peer, uint32_t reason) override {
        if (peer != currentPeer || !connected)
            return;
        connected = false;
        events.clear();
        bytes = 0;
        forge::NetworkEvent e;
        e.type = "disconnected";
        e.peer = currentPeer;
        e.reason = reason;
        events.push_back(std::move(e));
    }
    std::vector<forge::NetworkEvent> service(unsigned budget) override {
        std::vector<forge::NetworkEvent> result;
        while (!events.empty() && result.size() < budget) {
            bytes -= events.front().data.size();
            result.push_back(std::move(events.front()));
            events.pop_front();
        }
        return result;
    }
    forge::NetworkJson stats() const override {
        return {{"label", label},
                {"port", 0},
                {"peers",
                 forge::NetworkJson::array({{{"peer", currentPeer}, {"connected", connected}}})}};
    }
};
} // namespace
FORGE_MODULE(example_network_transport) {
    forge::registerNetworkTransport(
        "example_echo", [](const forge::NetworkOptions &o) { return std::make_unique<Echo>(o); });
}
