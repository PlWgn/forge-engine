#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <json.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace forge {
using NetworkJson = nlohmann::json;
struct NetworkOptions {
    std::string backend = "lan", bind;
    NetworkJson transportSettings = NetworkJson::object();
    uint16_t port = 0;
    unsigned peers = 32, channels = 4, timeoutMs = 10000, pollBudget = 256;
    size_t maxMessage = 65536, queueBytes = 4 * 1024 * 1024, queueEvents = 1024;
    uint32_t incomingBandwidth = 0, outgoingBandwidth = 0;
    bool operator==(const NetworkOptions &other) const;
};
NetworkOptions networkOptions(const NetworkJson &);
struct NetworkEvent {
    std::string type, address, identity, error, data;
    uint64_t peer = 0;
    unsigned channel = 0;
    bool reliable = false;
    uint32_t reason = 0;
};
// Adaptable transport boundary: bytes/messages only, no game entities or serialization.
class NetworkTransport {
  public:
    virtual ~NetworkTransport() = default;
    virtual uint64_t connect(const std::string &address, uint16_t port) = 0;
    virtual bool send(uint64_t peer, std::string_view data, unsigned channel, bool reliable) = 0;
    virtual void disconnect(uint64_t peer, uint32_t reason) = 0;
    virtual std::vector<NetworkEvent> service(unsigned budget) = 0;
    virtual NetworkJson stats() const = 0;
};
using NetworkFactory = std::function<std::unique_ptr<NetworkTransport>(const NetworkOptions &)>;
void registerNetworkTransport(
    const std::string &name,
    NetworkFactory); // Register before host creation, on its owning thread.
std::vector<std::string> networkBackends();
class NetworkHost {
  public:
    explicit NetworkHost(NetworkOptions);
    ~NetworkHost();
    uint64_t connect(const std::string &, unsigned port);
    bool send(uint64_t, std::string_view, unsigned channel = 0, bool reliable = true);
    void disconnect(uint64_t, uint32_t reason = 0);
    void pump();
    std::vector<NetworkEvent> poll(unsigned limit = 256);
    NetworkJson stats() const;
    void close();
    bool closed() const {
        return !transport;
    }
    const NetworkOptions options;

  private:
    std::thread::id thread;
    std::shared_ptr<void> affinity;
    std::unique_ptr<NetworkTransport> transport;
    std::deque<NetworkEvent> events;
    size_t eventBytes = 0;
    uint64_t received = 0, sent = 0, dropped = 0, overflows = 0;
    void check() const;
};
struct NetworkScope {
    std::vector<std::weak_ptr<NetworkHost>> hosts;
    ~NetworkScope();
};
// Runtime ownership, independent from transport implementations.
class NetworkService {
  public:
    void checkThread() const;
    std::shared_ptr<NetworkHost> create(const NetworkOptions &, const std::string &name,
                                        const std::shared_ptr<NetworkScope> &scene,
                                        bool persistent);
    void checkpoint();
    void rollback();
    void commit();
    void pump();
    void close();

  private:
    const std::thread::id thread = std::this_thread::get_id();
    std::unordered_map<std::string, std::shared_ptr<NetworkHost>> persistent;
    // Keep transports alive until servicing can release them on this thread.
    std::vector<std::shared_ptr<NetworkHost>> hosts;
    std::unordered_set<std::string> candidate;
    bool preparing = false;
};
uint16_t networkEphemeralPort(const std::string &bind);
std::unique_ptr<NetworkTransport> makeLanTransport(const NetworkOptions &);
std::unique_ptr<NetworkTransport> makeSocketsTransport(const NetworkOptions &);
std::unique_ptr<NetworkTransport> makeSteamTransport(const NetworkOptions &);
} // namespace forge
