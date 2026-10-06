#include <algorithm>
#include <cmath>
#include <forge/network.hpp>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace forge {
namespace {
struct NetworkThread {
    std::thread::id owner = std::this_thread::get_id();
};
std::shared_ptr<void> networkThread() {
    static std::mutex mutex;
    static std::weak_ptr<NetworkThread> shared;
    std::lock_guard<std::mutex> lock(mutex);
    auto token = shared.lock();
    if (token && token->owner != std::this_thread::get_id())
        throw std::runtime_error("All network hosts must share one servicing thread");
    if (!token) {
        token = std::make_shared<NetworkThread>();
        shared = token;
    }
    return token;
}
std::unordered_map<std::string, NetworkFactory> &factories() {
    static std::unordered_map<std::string, NetworkFactory> result;
    return result;
}
size_t integer(const NetworkJson &j, const char *key, size_t fallback, size_t low, size_t high) {
    auto v = j.value(key, NetworkJson(fallback));
    if (!v.is_number_integer() || v.get<double>() < double(low) || v.get<double>() > double(high))
        throw std::invalid_argument(std::string("network.") + key + " is out of range");
    return v.get<size_t>();
}
void customSettings(const NetworkJson &value) {
    if (!value.is_object())
        throw std::invalid_argument("transport_settings must be an object");
    std::vector<std::pair<const NetworkJson *, unsigned>> pending{{&value, 0}};
    unsigned count = 0;
    while (!pending.empty()) {
        auto [node, depth] = pending.back();
        pending.pop_back();
        if (depth > 64 || ++count > 4096)
            throw std::invalid_argument("transport_settings exceeds depth/value budget");
        if (node->is_number_float() && !std::isfinite(node->get<double>()))
            throw std::invalid_argument("transport_settings numbers must be finite");
        if (node->is_structured())
            for (auto &child : *node)
                pending.push_back({&child, depth + 1});
    }
    if (value.dump().size() > 16384)
        throw std::invalid_argument("transport_settings exceeds 16 KiB");
}

} // namespace
bool NetworkOptions::operator==(const NetworkOptions &o) const {
    return backend == o.backend && bind == o.bind && transportSettings == o.transportSettings &&
           port == o.port && peers == o.peers && channels == o.channels &&
           timeoutMs == o.timeoutMs && pollBudget == o.pollBudget && maxMessage == o.maxMessage &&
           queueBytes == o.queueBytes && queueEvents == o.queueEvents &&
           incomingBandwidth == o.incomingBandwidth && outgoingBandwidth == o.outgoingBandwidth;
}
NetworkOptions networkOptions(const NetworkJson &j) {
    if (!j.is_object())
        throw std::invalid_argument("network host settings must be an object");
    NetworkOptions o;
    o.backend = j.value("backend", "lan");
    o.bind = j.value("bind", "");
    if (o.backend.empty() || o.backend.size() > 64 || o.bind.size() > 255 ||
        o.bind.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid network backend/bind address");
    o.transportSettings = j.value("transport_settings", NetworkJson::object());
    customSettings(o.transportSettings);
    o.port = uint16_t(integer(j, "port", 0, 0, 65535));
    o.peers = unsigned(integer(j, "peers", 32, 1, 1024));
    o.channels = unsigned(integer(j, "channels", 4, 1, 32));
    o.timeoutMs = unsigned(integer(j, "timeout_ms", 10000, 100, 120000));
    o.pollBudget = unsigned(integer(j, "poll_budget", 256, 1, 4096));
    o.maxMessage = integer(j, "max_message_bytes", 65536, 1, 524287);
    o.queueBytes = integer(j, "queue_bytes", 4 * 1024 * 1024, o.maxMessage, 64 * 1024 * 1024);
    o.queueEvents = integer(j, "queue_events", 1024, 8, 65536);
    o.incomingBandwidth = uint32_t(integer(j, "incoming_bandwidth", 0, 0, 1000000000));
    o.outgoingBandwidth = uint32_t(integer(j, "outgoing_bandwidth", 0, 0, 1000000000));
    return o;
}
void registerNetworkTransport(const std::string &name, NetworkFactory factory) {
    if (name.empty() || name.size() > 64 || name.find('\0') != std::string::npos || name == "lan" ||
        name == "sockets" || name == "steam" || name == "steam_ip" || !factory)
        throw std::invalid_argument("Custom transport needs a unique non-reserved name/factory");
    if (!factories().emplace(name, std::move(factory)).second)
        throw std::invalid_argument("Network transport already registered: " + name);
}
std::vector<std::string> networkBackends() {
    std::vector<std::string> names;
#if FORGE_WITH_NETWORKING
    names.push_back("lan");
#endif
#if FORGE_WITH_GNS
    names.push_back("sockets");
#endif
#if FORGE_WITH_STEAMWORKS
    names.push_back("steam");
    names.push_back("steam_ip");
#endif
    for (const auto &entry : factories())
        names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    return names;
}
NetworkHost::NetworkHost(NetworkOptions value)
    : options(std::move(value)), thread(std::this_thread::get_id()), affinity(networkThread()) {
    // Native callers receive the same checks as JSON/Python callers.
    networkOptions({{"backend", options.backend},
                    {"transport_settings", options.transportSettings},
                    {"bind", options.bind},
                    {"port", options.port},
                    {"peers", options.peers},
                    {"channels", options.channels},
                    {"timeout_ms", options.timeoutMs},
                    {"poll_budget", options.pollBudget},
                    {"max_message_bytes", options.maxMessage},
                    {"queue_bytes", options.queueBytes},
                    {"queue_events", options.queueEvents},
                    {"incoming_bandwidth", options.incomingBandwidth},
                    {"outgoing_bandwidth", options.outgoingBandwidth}});
#if FORGE_WITH_NETWORKING
    if (options.backend == "lan")
        transport = makeLanTransport(options);
#endif
#if FORGE_WITH_GNS
    if (options.backend == "sockets")
        transport = makeSocketsTransport(options);
#endif
#if FORGE_WITH_STEAMWORKS
    if (options.backend == "steam" || options.backend == "steam_ip")
        transport = makeSteamTransport(options);
#endif
    auto custom = factories().find(options.backend);
    if (custom != factories().end())
        transport = custom->second(options);
    if (!transport)
        throw std::runtime_error("Network backend unavailable: " + options.backend);
}
NetworkHost::~NetworkHost() = default;
void NetworkHost::check() const {
    if (thread != std::this_thread::get_id())
        throw std::runtime_error("Network host must use its owning thread");
    if (!transport)
        throw std::runtime_error("Network host is closed");
}
uint64_t NetworkHost::connect(const std::string &address, unsigned port) {
    check();
    if (address.empty() || address.size() > 255 || address.find('\0') != std::string::npos ||
        port > 65535)
        throw std::invalid_argument("Invalid network destination");
    return transport->connect(address, uint16_t(port));
}
bool NetworkHost::send(uint64_t peer, std::string_view data, unsigned channel, bool reliable) {
    check();
    if (channel >= options.channels || data.size() > options.maxMessage)
        throw std::invalid_argument("Network channel/message exceeds configured limit");
    bool accepted = transport->send(peer, data, channel, reliable);
    if (accepted)
        sent += data.size();
    return accepted;
}
void NetworkHost::disconnect(uint64_t peer, uint32_t reason) {
    check();
    transport->disconnect(peer, reason);
}
void NetworkHost::pump() {
    check();
    // Bound temporary batches as well as the retained event queue. A backend may
    // return max-sized payloads for every event in this pass.
    auto quantum =
        unsigned(std::min(size_t(options.pollBudget), options.queueBytes / options.maxMessage));
    for (auto &event : transport->service(quantum)) {
        if (event.type == "message" && (event.data.size() > options.maxMessage ||
                                        eventBytes + event.data.size() > options.queueBytes ||
                                        events.size() >= options.queueEvents)) {
            ++dropped;
            if (event.reliable) {
                ++overflows;
                transport->disconnect(event.peer, 1);
            }
            continue;
        }
        if (events.size() >= options.queueEvents) {
            ++overflows;
            continue;
        }
        eventBytes += event.data.size();
        received += event.data.size();
        events.push_back(std::move(event));
    }
}
std::vector<NetworkEvent> NetworkHost::poll(unsigned limit) {
    check();
    if (!limit || limit > 4096)
        throw std::invalid_argument("poll limit must be 1..4096");
    pump();
    std::vector<NetworkEvent> result;
    result.reserve(std::min(size_t(limit), events.size()));
    while (!events.empty() && result.size() < limit) {
        eventBytes -= events.front().data.size();
        result.push_back(std::move(events.front()));
        events.pop_front();
    }
    return result;
}
NetworkJson NetworkHost::stats() const {
    if (thread != std::this_thread::get_id())
        throw std::runtime_error("Network host must use its owning thread");
    auto j = transport ? transport->stats() : NetworkJson::object();
    j.update({{"backend", options.backend},
              {"closed", closed()},
              {"queued_events", events.size()},
              {"queued_bytes", eventBytes},
              {"sent_bytes", sent},
              {"received_bytes", received},
              {"dropped_messages", dropped},
              {"overflows", overflows}});
    return j;
}
void NetworkHost::close() {
    if (thread != std::this_thread::get_id())
        throw std::runtime_error("Network host must use its owning thread");
    transport.reset();
    events.clear();
    eventBytes = 0;
}
NetworkScope::~NetworkScope() {
    for (auto &weak : hosts)
        if (auto host = weak.lock()) {
            try {
                host->close();
            } catch (...) {
            }
        }
}
void NetworkService::checkThread() const {
    if (thread != std::this_thread::get_id())
        throw std::runtime_error("Network/Steam API must use the runtime owning thread");
}
std::shared_ptr<NetworkHost> NetworkService::create(const NetworkOptions &options,
                                                    const std::string &name,
                                                    const std::shared_ptr<NetworkScope> &scene,
                                                    bool application) {
    checkThread();
    if (name.size() > 128 || (application && name.empty()))
        throw std::invalid_argument("Persistent host requires a name (1..128 chars)");
    if (!application && !scene)
        throw std::invalid_argument("Scene scope required");
    if (application) {
        auto found = persistent.find(name);
        if (found != persistent.end() && !found->second->closed()) {
            if (!(found->second->options == options))
                throw std::runtime_error(
                    "Named network host settings differ; close it outside reload first");
            return found->second;
        }
    }
    hosts.erase(std::remove_if(hosts.begin(), hosts.end(),
                               [](auto &w) {
                                   if (w.use_count() == 1)
                                       w->close();
                                   return w->closed();
                               }),
                hosts.end());
    for (auto it = persistent.begin(); it != persistent.end();)
        if (it->second->closed()) {
            candidate.erase(it->first);
            it = persistent.erase(it);
        } else
            ++it;
    if (hosts.size() >= 1024)
        throw std::runtime_error("Runtime network host budget reached (1024)");
    auto host = std::make_shared<NetworkHost>(options);
    hosts.push_back(host);
    if (application) {
        persistent[name] = host;
        if (preparing)
            candidate.insert(name);
    } else {
        // Compact weak lifetime records in batches; closed-host churn must not
        // retain an unbounded list or scan that list on every creation.
        if (scene->hosts.size() >= 64)
            scene->hosts.erase(std::remove_if(scene->hosts.begin(), scene->hosts.end(),
                                              [](auto &weak) {
                                                  auto h = weak.lock();
                                                  return !h || h->closed();
                                              }),
                               scene->hosts.end());
        scene->hosts.push_back(host);
    }
    return host;
}
void NetworkService::checkpoint() {
    checkThread();
    candidate.clear();
    preparing = true;
}
void NetworkService::rollback() {
    checkThread();
    for (auto &name : candidate) {
        auto it = persistent.find(name);
        if (it != persistent.end()) {
            it->second->close();
            persistent.erase(it);
        }
    }
    candidate.clear();
    preparing = false;
}
void NetworkService::commit() {
    checkThread();
    candidate.clear();
    preparing = false;
}
void NetworkService::pump() {
    checkThread();
    for (auto &host : hosts) {
        if (host.use_count() == 1)
            host->close();
        if (!host->closed())
            host->pump();
    }
    hosts.erase(
        std::remove_if(hosts.begin(), hosts.end(), [](auto &host) { return host->closed(); }),
        hosts.end());
}
void NetworkService::close() {
    checkThread();
    for (auto &host : hosts)
        host->close();
    hosts.clear();
    persistent.clear();
    candidate.clear();
    preparing = false;
}
} // namespace forge
