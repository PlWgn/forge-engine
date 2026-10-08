#include <enet/enet.h>
#include <forge/network.hpp>
#include <algorithm>
#include <iterator>
#include <stdexcept>
namespace forge {
namespace {
struct EnetLifetime {
    EnetLifetime() {
        if (enet_initialize())
            throw std::runtime_error("ENet initialization failed");
    }
    ~EnetLifetime() {
        enet_deinitialize();
    }
};
class LanTransport final : public NetworkTransport {
    struct Pending {
        size_t bytes = 0, messages = 0;
    };
    struct PacketOwner {
        std::shared_ptr<Pending> pending;
        size_t bytes;
    };
    static void released(ENetPacket *packet) noexcept {
        auto owner = static_cast<PacketOwner *>(packet->userData);
        owner->pending->bytes -= owner->bytes;
        --owner->pending->messages;
        delete owner;
    }
    std::shared_ptr<EnetLifetime> lifetime;
    ENetHost *host = nullptr;
    NetworkOptions options;
    uint64_t nextPeer = 1;
    std::unordered_map<uint64_t, ENetPeer *> peers;
    std::unordered_map<ENetPeer *, uint64_t> ids;
    std::unordered_map<uint64_t, std::shared_ptr<Pending>> pending;
    // Local closures that ENet completes without a service() event.
    std::vector<NetworkEvent> closed;
    static std::string text(const ENetAddress &address) {
        char ip[64]{};
        enet_address_get_host_ip(&address, ip, sizeof(ip));
        return std::string(ip) + ":" + std::to_string(address.port);
    }
    void forget(uint64_t id, ENetPeer *peer) {
        peers.erase(id);
        pending.erase(id);
        ids.erase(peer);
    }
    uint64_t identify(ENetPeer *peer) {
        auto it = ids.find(peer);
        if (it != ids.end())
            return it->second;
        auto id = nextPeer++;
        ids[peer] = id;
        peers[id] = peer;
        pending[id] = std::make_shared<Pending>();
        enet_peer_timeout(peer, 32, options.timeoutMs / 2, options.timeoutMs);
        return id;
    }
    static ENetAddress address(const std::string &text, uint16_t port) {
        ENetAddress a{};
        a.port = port;
        // Numeric IPv4 only: address resolution never blocks the game loop on DNS.
        if (text != "0.0.0.0" && enet_address_set_host_ip(&a, text.c_str()))
            throw std::invalid_argument("LAN needs a numeric IPv4 address");
        return a;
    }

  public:
    explicit LanTransport(const NetworkOptions &o) : options(o) {
        if (!o.transportSettings.empty())
            throw std::invalid_argument("LAN transport has no transport_settings fields");
        static std::weak_ptr<EnetLifetime> shared;
        lifetime = shared.lock();
        if (!lifetime) {
            lifetime = std::make_shared<EnetLifetime>();
            shared = lifetime;
        }
        auto a = address(o.bind.empty() ? "0.0.0.0" : o.bind, o.port);
        host = enet_host_create(o.bind.empty() ? nullptr : &a, o.peers, o.channels,
                                o.incomingBandwidth, o.outgoingBandwidth);
        if (!host)
            throw std::runtime_error("Cannot create/bind LAN host");
        host->maximumPacketSize = o.maxMessage;
        host->maximumWaitingData = o.queueBytes;
    }
    ~LanTransport() override {
        if (host)
            enet_host_destroy(host);
    }
    uint64_t connect(const std::string &ip, uint16_t port) override {
        if (!port)
            throw std::invalid_argument("LAN connect port must be 1..65535");
        auto a = address(ip, port);
        auto peer = enet_host_connect(host, &a, options.channels, 0);
        if (!peer)
            throw std::runtime_error("LAN peer limit reached");
        return identify(peer);
    }
    bool send(uint64_t id, std::string_view bytes, unsigned channel, bool reliable) override {
        auto it = peers.find(id);
        if (it == peers.end() || it->second->state != ENET_PEER_STATE_CONNECTED)
            throw std::runtime_error("LAN peer is not connected");
        auto queue = pending.at(id);
        if (bytes.size() > options.queueBytes - queue->bytes ||
            queue->messages >= options.queueEvents)
            return false;
        auto owner = std::make_unique<PacketOwner>(PacketOwner{queue, bytes.size()});
        auto packet = enet_packet_create(bytes.data(), bytes.size(),
                                         reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        if (!packet)
            throw std::runtime_error("Cannot allocate LAN packet");
        queue->bytes += bytes.size();
        ++queue->messages;
        packet->userData = owner.release();
        packet->freeCallback = released;
        if (enet_peer_send(it->second, uint8_t(channel), packet)) {
            enet_packet_destroy(packet);
            return false;
        }
        return true;
    }
    void disconnect(uint64_t id, uint32_t reason) override {
        auto it = peers.find(id);
        if (it == peers.end())
            return;
        auto *peer = it->second;
        auto address = text(peer->address);
        enet_peer_disconnect(peer, reason);
        // A peer that was still connecting is reset at once and never reports
        // ENET_EVENT_TYPE_DISCONNECT. Release its number now; otherwise a later
        // connection reusing this ENet slot would inherit the retired peer ID.
        if (peer->state == ENET_PEER_STATE_DISCONNECTED) {
            forget(id, peer);
            NetworkEvent e;
            e.type = "disconnected";
            e.peer = id;
            e.address = std::move(address);
            e.reason = reason;
            closed.push_back(std::move(e));
        }
    }
    std::vector<NetworkEvent> service(unsigned budget) override {
        std::vector<NetworkEvent> out;
        unsigned local = unsigned(std::min<size_t>(budget, closed.size()));
        out.assign(std::make_move_iterator(closed.begin()),
                   std::make_move_iterator(closed.begin() + local));
        closed.erase(closed.begin(), closed.begin() + local);
        ENetEvent event{};
        for (unsigned i = local; i < budget; ++i) {
            int result = enet_host_service(host, &event, 0);
            if (result < 0)
                throw std::runtime_error("LAN service failed");
            if (!result)
                break;
            std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> received(
                event.type == ENET_EVENT_TYPE_RECEIVE ? event.packet : nullptr,
                enet_packet_destroy);
            NetworkEvent e;
            e.peer = identify(event.peer);
            e.address = text(event.peer->address);
            if (event.type == ENET_EVENT_TYPE_CONNECT)
                e.type = "connected";
            else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                e.type = "disconnected";
                e.reason = event.data;
                forget(e.peer, event.peer);
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                e.type = "message";
                e.channel = event.channelID;
                e.reliable = (event.packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0;
                if (event.packet->dataLength)
                    e.data.assign(reinterpret_cast<char *>(event.packet->data),
                                  event.packet->dataLength);
            } else
                continue;
            out.push_back(std::move(e));
        }
        // Combine small sends in the frame rather than flushing once per Python send.
        enet_host_flush(host);
        return out;
    }
    NetworkJson stats() const override {
        NetworkJson list = NetworkJson::array();
        for (auto &[id, p] : peers)
            list.push_back({{"peer", id},
                            {"connected", p->state == ENET_PEER_STATE_CONNECTED},
                            {"rtt_ms", p->roundTripTime},
                            {"packet_loss", p->packetLoss},
                            {"pending_bytes", pending.at(id)->bytes},
                            {"pending_messages", pending.at(id)->messages}});
        return {{"port", host->address.port},
                {"peers", list},
                {"wire_sent_bytes", host->totalSentData},
                {"wire_received_bytes", host->totalReceivedData}};
    }
};
} // namespace
std::unique_ptr<NetworkTransport> makeLanTransport(const NetworkOptions &o) {
    return std::make_unique<LanTransport>(o);
}
} // namespace forge
