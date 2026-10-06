#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <forge/steam.hpp>
#include <stdexcept>
#if FORGE_WITH_STEAMWORKS
#undef STEAMNETWORKINGSOCKETS_STATIC_LINK
#include <steam/steam_api.h>
#endif
namespace forge {
void validateSteamSettings(const NetworkJson &j) {
    if (!j.is_object())
        throw std::invalid_argument("steam must be an object");
    for (auto key : {"enabled", "relay"})
        if (j.contains(key) && !j[key].is_boolean())
            throw std::invalid_argument(std::string("steam.") + key + " must be boolean");
    auto id = j.value("app_id", NetworkJson(480));
    if (!id.is_number_integer() || id.get<double>() < 1 || id.get<double>() > 4294967295.0)
        throw std::invalid_argument("steam.app_id must be an integer in 1..4294967295");
}
bool steamCompiled() {
    return bool(FORGE_WITH_STEAMWORKS);
}
namespace {
bool initialized = false;
}
bool steamAvailable() {
    return initialized;
}
struct SteamClient::Impl {
    uint32_t appId = 480;
    bool enabled = false;
    std::deque<NetworkJson> events;
    uint64_t dropped = 0;
#if FORGE_WITH_STEAMWORKS
    std::unordered_map<SteamAPICall_t, std::string> calls;
    std::string oldApp, oldGame;
    bool hadApp = false, hadGame = false;
    void event(NetworkJson value) {
        if (events.size() < 1024)
            events.push_back(std::move(value));
        else
            ++dropped;
    }
    void pending(SteamAPICall_t handle, const char *name) {
        if (handle == k_uAPICallInvalid)
            throw std::runtime_error("Steam rejected asynchronous request");
        calls.emplace(handle, name);
    }
#endif
};
#if FORGE_WITH_STEAMWORKS
namespace {
// New SDKs preload stats and may omit the deprecated request method. Older
// interfaces still need its asynchronous callback. Keep both source contracts.
template <class Stats, class Ready>
auto beginStats(Stats *stats, Ready ready, int) -> decltype(stats->RequestCurrentStats(), void()) {
    if (!stats->RequestCurrentStats())
        ready(false);
}
template <class Stats, class Ready> void beginStats(Stats *, Ready ready, long) {
    ready(true); // Modern SDK: client-preloaded state, including cached offline stats.
}
void environment(const char *key, const char *value) {
#ifdef _WIN32
    if (_putenv_s(key, value ? value : ""))
        throw std::runtime_error("Cannot set Steam environment");
#else
    if (value ? setenv(key, value, 1) : unsetenv(key))
        throw std::runtime_error("Cannot set Steam environment");
#endif
}
std::string text(const NetworkJson &j, const char *key, size_t maximum) {
    auto value = j.at(key).get<std::string>();
    if (value.size() > maximum || value.find('\0') != std::string::npos)
        throw std::invalid_argument(std::string("Invalid Steam ") + key);
    return value;
}
uint64_t identity(const NetworkJson &j, const char *key) {
    auto value = text(j, key, 20);
    uint64_t id = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), id);
    if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || !id)
        throw std::invalid_argument("Steam ID must be a decimal string");
    return id;
}
int integer(const NetworkJson &j, const char *key, int low, int high) {
    auto value = j.at(key);
    if (!value.is_number_integer() || value.get<double>() < low || value.get<double>() > high)
        throw std::invalid_argument(std::string("Invalid Steam ") + key);
    return value.get<int>();
}
} // namespace
#endif
SteamClient::SteamClient(const NetworkJson &settings) : impl(std::make_unique<Impl>()) {
    validateSteamSettings(settings);
    impl->appId = settings.value("app_id", uint32_t(480));
    if (!settings.value("enabled", false))
        return;
#if FORGE_WITH_STEAMWORKS
    if (initialized)
        throw std::runtime_error("Only one Steam client can be active");
    if (auto v = std::getenv("SteamAppId")) {
        impl->hadApp = true;
        impl->oldApp = v;
    }
    if (auto v = std::getenv("SteamGameId")) {
        impl->hadGame = true;
        impl->oldGame = v;
    }
    auto restore = [&]() {
        environment("SteamAppId", impl->hadApp ? impl->oldApp.c_str() : nullptr);
        environment("SteamGameId", impl->hadGame ? impl->oldGame.c_str() : nullptr);
    };
    try {
        auto id = std::to_string(impl->appId);
        environment("SteamAppId", id.c_str());
        environment("SteamGameId", id.c_str());
        if (!SteamAPI_Init())
            throw std::runtime_error(
                "Steam initialization failed: start Steam, check App ID and account ownership");
        if (SteamUtils()->GetAppID() != impl->appId) {
            SteamAPI_Shutdown();
            throw std::runtime_error("Steam initialized with a different App ID");
        }
    } catch (...) {
        restore();
        throw;
    }
    impl->enabled = initialized = true;
    try {
        SteamAPI_ManualDispatch_Init();
        if (settings.value("relay", false))
            SteamNetworkingUtils_SteamAPI()->InitRelayNetworkAccess();
        beginStats(
            SteamUserStats(), [&](bool ok) { impl->event({{"type", "stats_ready"}, {"ok", ok}}); },
            0);
    } catch (...) {
        SteamAPI_Shutdown();
        impl->enabled = initialized = false;
        restore();
        throw;
    }
#else
    throw std::runtime_error("Steam is enabled but this runtime has no Steamworks SDK support; "
                             "compile with --steamworks-sdk or set steam.enabled=false");
#endif
}
SteamClient::~SteamClient() {
#if FORGE_WITH_STEAMWORKS
    if (impl->enabled) {
        SteamAPI_Shutdown();
        initialized = false;
        try {
            environment("SteamAppId", impl->hadApp ? impl->oldApp.c_str() : nullptr);
            environment("SteamGameId", impl->hadGame ? impl->oldGame.c_str() : nullptr);
        } catch (...) {
        }
    }
#endif
}
NetworkJson SteamClient::info() const {
    NetworkJson result = {{"compiled", steamCompiled()},
                          {"enabled", impl->enabled},
                          {"app_id", impl->appId},
                          {"dropped_events", impl->dropped}};
#if FORGE_WITH_STEAMWORKS
    if (impl->enabled) {
        result["steam_id"] = std::to_string(SteamUser()->GetSteamID().ConvertToUint64());
        result["name"] = SteamFriends()->GetPersonaName();
        result["logged_on"] = SteamUser()->BLoggedOn();
        result["overlay_enabled"] = SteamUtils()->IsOverlayEnabled();
    }
#endif
    return result;
}
void SteamClient::pump() {
#if FORGE_WITH_STEAMWORKS
    if (!impl->enabled)
        return;
    auto pipe = SteamAPI_GetHSteamPipe();
    SteamAPI_ManualDispatch_RunFrame(pipe);
    CallbackMsg_t callback{};
    for (unsigned i = 0; i < 256 && SteamAPI_ManualDispatch_GetNextCallback(pipe, &callback); ++i) {
        // Always release SDK memory, including conversion/allocation failures.
        struct Release {
            HSteamPipe pipe;
            ~Release() {
                SteamAPI_ManualDispatch_FreeLastCallback(pipe);
            }
        } release{pipe};
        if (callback.m_iCallback == SteamAPICallCompleted_t::k_iCallback &&
            callback.m_cubParam == sizeof(SteamAPICallCompleted_t)) {
            auto c = *reinterpret_cast<SteamAPICallCompleted_t *>(callback.m_pubParam);
            auto found = impl->calls.find(c.m_hAsyncCall);
            if (found == impl->calls.end())
                continue;
            auto operation = found->second;
            impl->calls.erase(found);
            bool failed = false;
            NetworkJson e = {
                {"type", operation}, {"request", std::to_string(c.m_hAsyncCall)}, {"ok", false}};
            if (c.m_iCallback == LobbyCreated_t::k_iCallback) {
                LobbyCreated_t v{};
                if (SteamAPI_ManualDispatch_GetAPICallResult(pipe, c.m_hAsyncCall, &v, sizeof(v),
                                                             v.k_iCallback, &failed) &&
                    !failed) {
                    e["ok"] = v.m_eResult == k_EResultOK;
                    e["result"] = int(v.m_eResult);
                    e["lobby"] = std::to_string(v.m_ulSteamIDLobby);
                }
            } else if (c.m_iCallback == LobbyEnter_t::k_iCallback) {
                LobbyEnter_t v{};
                if (SteamAPI_ManualDispatch_GetAPICallResult(pipe, c.m_hAsyncCall, &v, sizeof(v),
                                                             v.k_iCallback, &failed) &&
                    !failed) {
                    e["ok"] = v.m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess;
                    e["result"] = v.m_EChatRoomEnterResponse;
                    e["lobby"] = std::to_string(v.m_ulSteamIDLobby);
                }
            } else if (c.m_iCallback == LobbyMatchList_t::k_iCallback) {
                LobbyMatchList_t v{};
                if (SteamAPI_ManualDispatch_GetAPICallResult(pipe, c.m_hAsyncCall, &v, sizeof(v),
                                                             v.k_iCallback, &failed) &&
                    !failed) {
                    e["ok"] = true;
                    e["lobbies"] = NetworkJson::array();
                    for (uint32_t n = 0; n < std::min(v.m_nLobbiesMatching, uint32_t(1000)); ++n)
                        e["lobbies"].push_back(std::to_string(
                            SteamMatchmaking()->GetLobbyByIndex(int(n)).ConvertToUint64()));
                }
            }
            impl->event(std::move(e));
        } else if (callback.m_iCallback == UserStatsReceived_t::k_iCallback &&
                   callback.m_cubParam == sizeof(UserStatsReceived_t)) {
            auto v = *reinterpret_cast<UserStatsReceived_t *>(callback.m_pubParam);
            if (v.m_nGameID != impl->appId || v.m_steamIDUser != SteamUser()->GetSteamID())
                continue;
            impl->event({{"type", "stats_ready"},
                         {"ok", v.m_eResult == k_EResultOK},
                         {"result", int(v.m_eResult)}});
        } else if (callback.m_iCallback == UserStatsStored_t::k_iCallback &&
                   callback.m_cubParam == sizeof(UserStatsStored_t)) {
            auto v = *reinterpret_cast<UserStatsStored_t *>(callback.m_pubParam);
            if (v.m_nGameID != impl->appId)
                continue;
            impl->event({{"type", "stats_stored"},
                         {"ok", v.m_eResult == k_EResultOK},
                         {"result", int(v.m_eResult)}});
        } else if (callback.m_iCallback == GameOverlayActivated_t::k_iCallback &&
                   callback.m_cubParam == sizeof(GameOverlayActivated_t)) {
            auto v = *reinterpret_cast<GameOverlayActivated_t *>(callback.m_pubParam);
            impl->event({{"type", "overlay"}, {"active", bool(v.m_bActive)}});
        }
    }
#endif
}
NetworkJson SteamClient::poll(unsigned limit) {
    if (!limit || limit > 4096)
        throw std::invalid_argument("Steam poll limit must be 1..4096");
    pump();
    auto result = NetworkJson::array();
    while (!impl->events.empty() && result.size() < limit) {
        result.push_back(std::move(impl->events.front()));
        impl->events.pop_front();
    }
    return result;
}
NetworkJson SteamClient::call(const std::string &operation, const NetworkJson &j) {
    if (!impl->enabled)
        throw std::runtime_error("Steam is disabled or unavailable");
    if (!j.is_object())
        throw std::invalid_argument("Steam request must be an object");
#if FORGE_WITH_STEAMWORKS
    if (operation == "friends") {
        auto result = NetworkJson::array();
        int count = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
        for (int i = 0; i < std::min(count, 10000); ++i) {
            auto id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
            result.push_back({{"steam_id", std::to_string(id.ConvertToUint64())},
                              {"name", SteamFriends()->GetFriendPersonaName(id)},
                              {"state", int(SteamFriends()->GetFriendPersonaState(id))}});
        }
        return result;
    }
    if (operation == "overlay") {
        auto dialog = text(j, "dialog", 32);
        if (dialog != "friends" && dialog != "community" && dialog != "players" &&
            dialog != "settings" && dialog != "officialgamegroup" && dialog != "stats" &&
            dialog != "achievements")
            throw std::invalid_argument("Unsupported Steam overlay dialog");
        SteamFriends()->ActivateGameOverlay(dialog.c_str());
        return nullptr;
    }
    if (operation == "presence") {
        auto key = text(j, "key", 63), value = text(j, "value", 255);
        return SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
    }
    if (operation == "achievement_get") {
        auto name = text(j, "name", 128);
        bool value = false;
        if (!SteamUserStats()->GetAchievement(name.c_str(), &value))
            throw std::runtime_error(
                "Steam achievement unavailable; wait for stats_ready and check name");
        return value;
    }
    if (operation == "achievement_set") {
        auto name = text(j, "name", 128);
        return SteamUserStats()->SetAchievement(name.c_str());
    }
    if (operation == "stat_get") {
        auto kind = j.value("kind", "int");
        if (kind != "int" && kind != "float")
            throw std::invalid_argument("Steam stat kind must be int or float");
        auto name = text(j, "name", 128);
        if (j.value("kind", "int") == "float") {
            float value = 0;
            if (!SteamUserStats()->GetStat(name.c_str(), &value))
                throw std::runtime_error("Steam stat unavailable");
            return value;
        }
        int32_t value = 0;
        if (!SteamUserStats()->GetStat(name.c_str(), &value))
            throw std::runtime_error("Steam stat unavailable");
        return value;
    }
    if (operation == "stat_set") {
        auto kind = j.value("kind", "int");
        if (kind != "int" && kind != "float")
            throw std::invalid_argument("Steam stat kind must be int or float");
        auto name = text(j, "name", 128);
        if (j.value("kind", "int") == "float") {
            auto value = j.at("value");
            if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                std::abs(value.get<double>()) > 3.402823466e38)
                throw std::invalid_argument("Invalid Steam float stat");
            return SteamUserStats()->SetStat(name.c_str(), value.get<float>());
        }
        return SteamUserStats()->SetStat(name.c_str(),
                                         int32_t(integer(j, "value", INT32_MIN, INT32_MAX)));
    }
    if (operation == "stats_store")
        return SteamUserStats()->StoreStats();
    if (operation == "lobby_create" || operation == "lobby_list" || operation == "lobby_join") {
        if (impl->calls.size() >= 128)
            throw std::runtime_error("Steam asynchronous request budget reached");
        SteamAPICall_t handle;
        if (operation == "lobby_create")
            handle = SteamMatchmaking()->CreateLobby(ELobbyType(integer(j, "visibility", 0, 3)),
                                                     integer(j, "members", 1, 250));
        else if (operation == "lobby_join")
            handle = SteamMatchmaking()->JoinLobby(CSteamID(identity(j, "lobby")));
        else {
            SteamMatchmaking()->AddRequestLobbyListResultCountFilter(integer(j, "limit", 1, 1000));
            handle = SteamMatchmaking()->RequestLobbyList();
        }
        impl->pending(handle, operation.c_str());
        return std::to_string(handle);
    }
    if (operation == "lobby_leave") {
        SteamMatchmaking()->LeaveLobby(CSteamID(identity(j, "lobby")));
        return nullptr;
    }
    if (operation == "lobby_info") {
        auto id = CSteamID(identity(j, "lobby"));
        auto members = NetworkJson::array();
        int count = SteamMatchmaking()->GetNumLobbyMembers(id);
        for (int i = 0; i < std::min(count, 250); ++i)
            members.push_back(
                std::to_string(SteamMatchmaking()->GetLobbyMemberByIndex(id, i).ConvertToUint64()));
        return {{"owner", std::to_string(SteamMatchmaking()->GetLobbyOwner(id).ConvertToUint64())},
                {"members", members}};
    }
    if (operation == "lobby_set") {
        auto id = CSteamID(identity(j, "lobby"));
        auto key = text(j, "key", 255), value = text(j, "value", 8191);
        return SteamMatchmaking()->SetLobbyData(id, key.c_str(), value.c_str());
    }
    if (operation == "lobby_get") {
        auto id = CSteamID(identity(j, "lobby"));
        auto key = text(j, "key", 255);
        return std::string(SteamMatchmaking()->GetLobbyData(id, key.c_str()));
    }
#endif
    throw std::invalid_argument("Unknown Steam operation: " + operation);
}
} // namespace forge
