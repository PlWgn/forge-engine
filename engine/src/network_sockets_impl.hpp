// Shared adapter logic, compiled against either Valve's open API or the user's SDK.
#include <charconv>
#include <forge/network.hpp>
#include <stdexcept>
#if FORGE_SOCKETS_STEAM
#undef STEAMNETWORKINGSOCKETS_STATIC_LINK
#include <forge/steam.hpp>
#include <steam/steam_api.h>
#else
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>
#endif
namespace forge {
namespace {
#if !FORGE_SOCKETS_STEAM
struct SocketsLifetime {
    SocketsLifetime() {
        SteamNetworkingErrMsg error{};
        if (!GameNetworkingSockets_Init(nullptr, error))
            throw std::runtime_error(error);
    }
    ~SocketsLifetime() {
        GameNetworkingSockets_Kill();
    }
};
#endif
class SocketsTransport final : public NetworkTransport {
    NetworkOptions options;
    ISteamNetworkingSockets *api = nullptr;
    HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;
#if !FORGE_SOCKETS_STEAM
    std::shared_ptr<SocketsLifetime> lifetime;
#endif
    bool callbackFailed = false;
    std::string callbackError;
    uint64_t notificationDrops = 0;
    uint64_t nextPeer = 1;
    std::unordered_map<uint64_t, HSteamNetConnection> peers;
    std::unordered_map<HSteamNetConnection, uint64_t> ids;
    std::deque<NetworkEvent> notifications;
    static std::unordered_map<HSteamListenSocket, SocketsTransport *> &listeners() {
        static std::unordered_map<HSteamListenSocket, SocketsTransport *> map;
        return map;
    }
    static std::unordered_map<HSteamNetConnection, SocketsTransport *> &connections() {
        static std::unordered_map<HSteamNetConnection, SocketsTransport *> map;
        return map;
    }
    uint64_t add(HSteamNetConnection connection) {
        auto found = ids.find(connection);
        if (found != ids.end())
            return found->second;
        if (peers.size() >= options.peers) {
            api->CloseConnection(connection, 1, "Peer capacity", false);
            return 0;
        }
        if (!api->SetConnectionPollGroup(connection, group)) {
            api->CloseConnection(connection, 1, "Cannot set poll group", false);
            throw std::runtime_error("Cannot assign socket poll group");
        }
        auto id = nextPeer++;
        peers[id] = connection;
        ids[connection] = id;
        connections()[connection] = this;
        return id;
    }
    static void status(SteamNetConnectionStatusChangedCallback_t *change) noexcept {
        try {
            statusChecked(change);
        } catch (const std::exception &error) {
            auto it = connections().find(change->m_hConn);
            if (it != connections().end()) {
                it->second->callbackFailed = true;
                try {
                    it->second->callbackError = error.what();
                } catch (...) {
                }
            } else {
                auto listener = listeners().find(change->m_info.m_hListenSocket);
                if (listener != listeners().end())
                    listener->second->callbackFailed = true;
            }
        } catch (...) {
            auto it = connections().find(change->m_hConn);
            if (it != connections().end())
                it->second->callbackFailed = true;
        }
    }
    static void statusChecked(SteamNetConnectionStatusChangedCallback_t *change) {
        auto found = connections().find(change->m_hConn);
        SocketsTransport *self = found == connections().end() ? nullptr : found->second;
        if (!self) {
            auto it = listeners().find(change->m_info.m_hListenSocket);
            if (it != listeners().end())
                self = it->second;
        }
        if (!self)
            return;
        if (!self->ids.count(change->m_hConn) &&
            change->m_info.m_eState != k_ESteamNetworkingConnectionState_Connecting)
            return;
        auto id = self->add(change->m_hConn);
        if (!id)
            return;
        NetworkEvent e;
        e.peer = id;
        char address[SteamNetworkingIPAddr::k_cchMaxString]{};
        change->m_info.m_addrRemote.ToString(address, sizeof(address), true);
        e.address = address;
        char identity[SteamNetworkingIdentity::k_cchMaxString]{};
        change->m_info.m_identityRemote.ToString(identity, sizeof(identity));
        e.identity = identity;
        switch (change->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (change->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid &&
                self->api->AcceptConnection(change->m_hConn) != k_EResultOK) {
                self->api->CloseConnection(change->m_hConn, 1, "Cannot accept connection", false);
                self->peers.erase(id);
                self->ids.erase(change->m_hConn);
                connections().erase(change->m_hConn);
                e.type = "error";
                e.error = "Cannot accept incoming socket";
            } else
                return;
            break;
        case k_ESteamNetworkingConnectionState_Connected:
            e.type = "connected";
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            e.type = "disconnected";
            e.reason = uint32_t(change->m_info.m_eEndReason);
            e.error = change->m_info.m_szEndDebug;
            self->api->CloseConnection(change->m_hConn, 0, nullptr, false);
            self->peers.erase(id);
            self->ids.erase(change->m_hConn);
            connections().erase(change->m_hConn);
            break;
        default:
            return;
        }
        // Callback queues are bounded independently from application messages.
        if (self->notifications.size() < self->options.queueEvents)
            self->notifications.push_back(std::move(e));
        else
            ++self->notificationDrops;
    }
    SteamNetworkingIPAddr address(const std::string &text, uint16_t port) const {
        SteamNetworkingIPAddr a;
        a.Clear();
        if (!text.empty() && !a.ParseString(text.c_str()))
            throw std::invalid_argument("Sockets needs a numeric IPv4/IPv6 address");
        a.m_port = port;
        return a;
    }
    std::vector<SteamNetworkingConfigValue_t> settings() const {
        std::vector<SteamNetworkingConfigValue_t> values(4);
        values[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                         reinterpret_cast<void *>(status));
        values[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, int32_t(options.queueBytes));
        values[2].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, int32_t(options.timeoutMs));
        values[3].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, int32_t(options.timeoutMs));
#if !FORGE_SOCKETS_STEAM
        values.emplace_back();
        values.back().SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
#endif
        return values;
    }

  public:
    explicit SocketsTransport(const NetworkOptions &o) : options(o) {
        if (!o.transportSettings.empty())
            throw std::invalid_argument("Valve transport has no transport_settings fields");
#if FORGE_SOCKETS_STEAM
        if (!steamAvailable())
            throw std::runtime_error("Steam must be initialized before opening Steam sockets");
#else
        static std::weak_ptr<SocketsLifetime> shared;
        lifetime = shared.lock();
        if (!lifetime) {
            lifetime = std::make_shared<SocketsLifetime>();
            shared = lifetime;
        }
#endif
#if FORGE_SOCKETS_STEAM
        api = SteamNetworkingSockets_SteamAPI();
#else
        api = SteamNetworkingSockets_Lib();
#endif
        if (!api)
            throw std::runtime_error("Steam sockets interface unavailable");
        group = api->CreatePollGroup();
        if (group == k_HSteamNetPollGroup_Invalid)
            throw std::runtime_error("Cannot create socket poll group");
        try {
            if (!options.bind.empty()) {
                auto opts = settings();
#if FORGE_SOCKETS_STEAM
                if (options.backend == "steam")
                    listener =
                        api->CreateListenSocketP2P(options.port, int(opts.size()), opts.data());
                else
#endif
                {
                    auto bindAddress = address(options.bind, options.port);
                    for (unsigned attempt = 0; attempt < 4; ++attempt) {
                        if (!options.port)
                            bindAddress.m_port = networkEphemeralPort(options.bind);
                        listener =
                            api->CreateListenSocketIP(bindAddress, int(opts.size()), opts.data());
                        if (listener != k_HSteamListenSocket_Invalid || options.port)
                            break;
                    }
                }
                if (listener == k_HSteamListenSocket_Invalid)
                    throw std::runtime_error("Cannot bind socket listener");
                listeners()[listener] = this;
            }
        } catch (...) {
            if (listener != k_HSteamListenSocket_Invalid)
                api->CloseListenSocket(listener);
            api->DestroyPollGroup(group);
            throw;
        }
    }
    ~SocketsTransport() override {
        for (auto &[id, connection] : peers) {
            (void)id;
            connections().erase(connection);
            api->CloseConnection(connection, 0, "Host closed", false);
        }
        if (listener != k_HSteamListenSocket_Invalid) {
            listeners().erase(listener);
            api->CloseListenSocket(listener);
        }
        if (group != k_HSteamNetPollGroup_Invalid)
            api->DestroyPollGroup(group);
    }
    uint64_t connect(const std::string &text, uint16_t port) override {
        if (peers.size() >= options.peers)
            throw std::runtime_error("Socket peer limit reached");
        auto opts = settings();
        HSteamNetConnection connection;
#if FORGE_SOCKETS_STEAM
        if (options.backend == "steam") {
            uint64_t id = 0;
            auto result = std::from_chars(text.data(), text.data() + text.size(), id);
            if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !id)
                throw std::invalid_argument("Steam destination must be a Steam ID decimal string");
            SteamNetworkingIdentity identity;
            identity.Clear();
            identity.SetSteamID64(id);
            connection = api->ConnectP2P(identity, port, int(opts.size()), opts.data());
        } else
#endif
        {
            if (!port)
                throw std::invalid_argument("Socket IP connect port must be 1..65535");
            connection =
                api->ConnectByIPAddress(address(text, port), int(opts.size()), opts.data());
        }
        if (connection == k_HSteamNetConnection_Invalid)
            throw std::runtime_error("Cannot begin socket connection");
        return add(connection);
    }
    bool send(uint64_t id, std::string_view bytes, unsigned channel, bool reliable) override {
        auto it = peers.find(id);
        if (it == peers.end())
            throw std::runtime_error("Socket peer does not exist");
        // Channel prefix is part of Forge's socket wire contract, never JSON/pickle.
        std::string packet;
        packet.reserve(bytes.size() + 1);
        packet.push_back(char(channel));
        packet.append(bytes);
        auto result = api->SendMessageToConnection(
            it->second, packet.data(), uint32_t(packet.size()),
            reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable,
            nullptr);
        if (result == k_EResultLimitExceeded)
            return false;
        if (result != k_EResultOK)
            throw std::runtime_error("Socket send failed: " + std::to_string(int(result)));
        return true;
    }
    void disconnect(uint64_t id, uint32_t reason) override {
        auto it = peers.find(id);
        if (it == peers.end())
            return;
        auto connection = it->second;
        api->CloseConnection(connection, int(1000 + reason % 1000), "Application disconnect",
                             false);
        peers.erase(it);
        ids.erase(connection);
        connections().erase(connection);
        if (notifications.size() < options.queueEvents) {
            NetworkEvent e;
            e.type = "disconnected";
            e.peer = id;
            e.reason = reason;
            notifications.push_back(std::move(e));
        }
    }
    std::vector<NetworkEvent> service(unsigned budget) override {
        api->RunCallbacks();
        if (callbackFailed) {
            callbackFailed = false;
            throw std::runtime_error("Socket callback failed while managing a connection: " +
                                     callbackError);
        }
        std::vector<NetworkEvent> result;
        while (!notifications.empty() && result.size() < budget) {
            result.push_back(std::move(notifications.front()));
            notifications.pop_front();
        }
        unsigned processed = unsigned(result.size());
        while (processed < budget) {
            SteamNetworkingMessage_t *messages[32]{};
            int count = api->ReceiveMessagesOnPollGroup(group, messages,
                                                        int(std::min(32u, budget - processed)));
            if (count < 0)
                throw std::runtime_error("Socket receive failed");
            if (!count)
                break;
            struct Release {
                SteamNetworkingMessage_t **items;
                int count;
                ~Release() {
                    for (int i = 0; i < count; ++i)
                        if (items[i])
                            items[i]->Release();
                }
            } release{messages, count};
            processed += unsigned(count); // Invalid packets consume budget as well.
            for (int i = 0; i < count; ++i) {
                auto message = messages[i];
                auto id = ids.find(message->m_conn);
                if (id == ids.end())
                    continue;
                NetworkEvent e;
                e.type = "message";
                e.peer = id->second;
                e.reliable = (message->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
                if (message->m_cbSize < 1 || size_t(message->m_cbSize - 1) > options.maxMessage) {
                    disconnect(e.peer, 1);
                    continue;
                }
                auto bytes = static_cast<const char *>(message->m_pData);
                e.channel = uint8_t(bytes[0]);
                if (e.channel >= options.channels) {
                    disconnect(e.peer, 1);
                    continue;
                }
                e.data.assign(bytes + 1, size_t(message->m_cbSize - 1));
                result.push_back(std::move(e));
            }
        }
        return result;
    }
    NetworkJson stats() const override {
        NetworkJson list = NetworkJson::array();
        for (auto &[id, c] : peers) {
            SteamNetConnectionRealTimeStatus_t s{};
            api->GetConnectionRealTimeStatus(c, &s, 0, nullptr);
            list.push_back(
                {{"peer", id},
                 {"connected", s.m_eState == k_ESteamNetworkingConnectionState_Connected},
                 {"rtt_ms", s.m_nPing},
                 {"pending_bytes", s.m_cbPendingReliable + s.m_cbPendingUnreliable}});
        }
        SteamNetworkingIPAddr a;
        a.Clear();
        if (listener != k_HSteamListenSocket_Invalid)
            api->GetListenSocketAddress(listener, &a);
        return {{"notification_drops", notificationDrops},
                {"peers", list},
                {"port", options.backend == "steam" ? options.port : a.m_port}};
    }
};
} // namespace
#if FORGE_SOCKETS_STEAM
std::unique_ptr<NetworkTransport> makeSteamTransport(const NetworkOptions &o) {
    return std::make_unique<SocketsTransport>(o);
}
#else
std::unique_ptr<NetworkTransport> makeSocketsTransport(const NetworkOptions &o) {
    return std::make_unique<SocketsTransport>(o);
}
#endif
} // namespace forge
