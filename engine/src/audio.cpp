#define MINIAUDIO_IMPLEMENTATION
#include <algorithm>
#include <cmath>
#include <forge/engine.hpp>
#include <miniaudio.h>
namespace forge {
namespace {
float bounded(const Json &value, const std::string &name, float low, float high) {
    float v = finiteNumber(value, name);
    if (v < low || v > high)
        throw std::runtime_error(name + " outside supported range");
    return v;
}
float level(float v) {
    return bounded(v, "Volume", 0, 1);
}
float duration(float v) {
    return bounded(v, "Fade duration", 0, 3600);
}
Json voiceOptions(Json value) {
    if (!value.is_object())
        throw std::runtime_error("Sound options must be an object");
    bounded(value.value("pan", Json(0)), "pan", -1, 1);
    bounded(value.value("pitch", Json(1)), "pitch", .01f, 8);
    bounded(value.value("min_distance", Json(1)), "min_distance", .001f, 100000);
    bounded(value.value("max_distance", Json(100)), "max_distance", .001f, 100000);
    if (value.value("min_distance", 1.f) > value.value("max_distance", 100.f))
        throw std::runtime_error("min_distance exceeds max_distance");
    for (auto name : {"lowpass", "highpass"}) {
        auto hz = bounded(value.value(name, Json(0)), name, 0, 23999);
        if (hz > 0 && hz < 20)
            throw std::runtime_error("DSP cutoff must be zero or at least 20 Hz");
    }
    bounded(value.value("echo_seconds", Json(0)), "echo_seconds", 0, 5);
    bounded(value.value("echo_decay", Json(.3)), "echo_decay", 0, .95f);
    bounded(value.value("echo_wet", Json(.3)), "echo_wet", 0, 1);
    bounded(value.value("priority", Json(0)), "priority", -1000, 1000);
    if (value.contains("position")) {
        auto p = value["position"];
        if (!p.is_array() || p.size() != 3)
            throw std::runtime_error("Sound position needs three components");
        for (auto &n : p)
            finiteNumber(n, "sound position");
    }
    return value;
}
} // namespace
struct Audio::Impl {
    ma_engine engine{};
    bool initialized = false, transaction = false;
    unsigned nextId = 0, stolen = 0, dropped = 0;
    struct Filter {
        ma_lpf_node low{};
        ma_hpf_node high{};
        ma_delay_node echo{};
        bool lowReady = false, highReady = false, echoReady = false;
        ma_node *input = nullptr;
        ~Filter() {
            if (echoReady)
                ma_delay_node_uninit(&echo, nullptr);
            if (highReady)
                ma_hpf_node_uninit(&high, nullptr);
            if (lowReady)
                ma_lpf_node_uninit(&low, nullptr);
        }
    };
    struct Voice {
        std::shared_ptr<ma_sound> sound;
        std::shared_ptr<Filter> filter;
        Json options = Json::object(), filterOptions = Json::object();
        std::string channel, path;
        float envelope = 1, start = 1, target = 1, elapsed = 0, seconds = 0;
        bool paused = false, stopAfter = false, streaming = true;
    };
    std::map<unsigned, Voice> voices, backup;
    std::map<std::string, float> volumes, backupVolumes, duckGains, backupDuck;
    Json settings = Json::object(), backupSettings;
    glm::vec3 listenerPosition{0}, listenerDirection{0, 0, -1}, backupPosition{0}, backupDirection{0, 0, -1};
    float volume(const std::string &channel) const {
        auto found = volumes.find(channel);
        return found == volumes.end() ? 1 : found->second;
    }
    void init(bool silent) {
        if (initialized)
            return;
        auto config = ma_engine_config_init();
        config.noDevice = silent;
        config.channels = 2;
        config.sampleRate = 48000;
        if (ma_engine_init(&config, &engine) != MA_SUCCESS)
            throw std::runtime_error("Audio device initialization failed");
        initialized = true;
    }
    void effects(Voice &voice) {
        auto desired = Json{{"lowpass", voice.options.value("lowpass", 0.f)},
                            {"highpass", voice.options.value("highpass", 0.f)},
                            {"echo_seconds", voice.options.value("echo_seconds", 0.f)},
                            {"echo_decay", voice.options.value("echo_decay", .3f)},
                            {"echo_wet", voice.options.value("echo_wet", .3f)}};
        if (desired == voice.filterOptions)
            return;
        auto next = std::make_shared<Filter>();
        auto graph = ma_engine_get_node_graph(&engine);
        ma_node *endpoint = ma_node_graph_get_endpoint(graph);
        ma_node *tail = endpoint;
        float delay = desired["echo_seconds"], high = desired["highpass"], low = desired["lowpass"];
        if (delay > 0) {
            auto config = ma_delay_node_config_init(2, 48000, ma_uint32(delay * 48000),
                                                    desired["echo_decay"].get<float>());
            if (ma_delay_node_init(graph, &config, nullptr, &next->echo) != MA_SUCCESS)
                throw std::runtime_error("Cannot create echo DSP");
            next->echoReady = true;
            ma_delay_node_set_wet(&next->echo, desired["echo_wet"].get<float>());
            ma_delay_node_set_dry(&next->echo, 1);
            ma_node_attach_output_bus(&next->echo, 0, tail, 0);
            tail = &next->echo;
        }
        if (high > 0) {
            auto config = ma_hpf_node_config_init(2, 48000, high, 2);
            if (ma_hpf_node_init(graph, &config, nullptr, &next->high) != MA_SUCCESS)
                throw std::runtime_error("Cannot create highpass DSP");
            next->highReady = true;
            ma_node_attach_output_bus(&next->high, 0, tail, 0);
            tail = &next->high;
        }
        if (low > 0) {
            auto config = ma_lpf_node_config_init(2, 48000, low, 2);
            if (ma_lpf_node_init(graph, &config, nullptr, &next->low) != MA_SUCCESS)
                throw std::runtime_error("Cannot create lowpass DSP");
            next->lowReady = true;
            ma_node_attach_output_bus(&next->low, 0, tail, 0);
            tail = &next->low;
        }
        ma_node_attach_output_bus(voice.sound.get(), 0, tail, 0);
        next->input = tail;
        voice.filter = std::move(next);
        voice.filterOptions = std::move(desired);
    }
    void apply() {
        if (!initialized)
            return;
        ma_engine_listener_set_position(&engine, 0, listenerPosition.x, listenerPosition.y,
                                        listenerPosition.z);
        ma_engine_listener_set_direction(&engine, 0, listenerDirection.x, listenerDirection.y,
                                         listenerDirection.z);
        for (auto &[id, v] : voices) {
            effects(v);
            float duck = duckGains.count(v.channel) ? duckGains[v.channel] : 1;
            ma_sound_set_volume(v.sound.get(), v.envelope * volume(v.channel) * volume("master") * duck);
            ma_sound_set_pan(v.sound.get(), v.options.value("pan", 0.f));
            ma_sound_set_pitch(v.sound.get(), v.options.value("pitch", 1.f));
            bool spatial = v.options.value("spatial", v.options.contains("position"));
            ma_sound_set_spatialization_enabled(v.sound.get(), spatial);
            if (v.options.contains("position")) {
                auto p = v.options["position"];
                ma_sound_set_position(v.sound.get(), p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }
            ma_sound_set_min_distance(v.sound.get(), v.options.value("min_distance", 1.f));
            ma_sound_set_max_distance(v.sound.get(), v.options.value("max_distance", 100.f));
            if (v.paused)
                ma_sound_stop(v.sound.get());
            else if (!ma_sound_is_playing(v.sound.get()) && !ma_sound_at_end(v.sound.get()))
                ma_sound_start(v.sound.get());
        }
    }
    ~Impl() {
        voices.clear();
        backup.clear();
        if (initialized)
            ma_engine_uninit(&engine);
    }
};
Audio::Audio() : impl(std::make_unique<Impl>()) {}
Audio::~Audio() = default;
unsigned Audio::play(const fs::path &path, bool loop, float volume, const std::string &channel, float seconds,
                     const Json &rawOptions) {
    level(volume);
    duration(seconds);
    auto options = voiceOptions(rawOptions);
    if (channel.empty() || channel == "master")
        throw std::runtime_error("Audio channel must be named and cannot be master");
    if (!fs::is_regular_file(path))
        throw std::runtime_error("Audio file missing: " + path.u8string());
    impl->init(silent);
    unsigned maximum = impl->settings.value("max_voices", 64u);
    auto limits = impl->settings.value("channel_limits", Json::object());
    unsigned channelLimit = limits.value(channel, maximum);
    unsigned count = 0;
    for (auto &[id, v] : impl->voices)
        if (v.channel == channel)
            ++count;
    if (impl->voices.size() >= maximum || count >= channelLimit) {
        bool inChannel = count >= channelLimit;
        auto victim = impl->voices.end();
        for (auto it = impl->voices.begin(); it != impl->voices.end(); ++it) {
            if (inChannel && it->second.channel != channel)
                continue;
            if (victim == impl->voices.end() ||
                it->second.options.value("priority", 0) < victim->second.options.value("priority", 0))
                victim = it;
        }
        if (victim == impl->voices.end() || impl->settings.value("overflow", "steal_oldest") == "reject" ||
            victim->second.options.value("priority", 0) > options.value("priority", 0)) {
            ++impl->dropped;
            logger.write("WARN", "Audio voice limit: rejected " + path.u8string());
            return 0;
        }
        stop(victim->first);
        ++impl->stolen;
    }
    auto policy = impl->settings.value("streaming", "auto");
    bool streaming = options.value(
        "stream",
        policy == "stream" ||
            (policy == "auto" &&
             fs::file_size(path) > impl->settings.value("stream_threshold_bytes", size_t(4 * 1024 * 1024))));
    auto raw = std::make_unique<ma_sound>();
    auto flags = streaming ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
#ifdef _WIN32
    auto result =
        ma_sound_init_from_file_w(&impl->engine, path.wstring().c_str(), flags, nullptr, nullptr, raw.get());
#else
    auto result =
        ma_sound_init_from_file(&impl->engine, path.u8string().c_str(), flags, nullptr, nullptr, raw.get());
#endif
    if (result != MA_SUCCESS)
        throw std::runtime_error("Cannot decode audio: " + path.u8string());
    auto sound = std::shared_ptr<ma_sound>(raw.release(), [](ma_sound *s) {
        ma_sound_uninit(s);
        delete s;
    });
    ma_sound_set_looping(sound.get(), loop);
    Impl::Voice voice;
    voice.sound = sound;
    voice.channel = channel;
    voice.path = path.u8string();
    voice.options = options;
    voice.streaming = streaming;
    voice.envelope = seconds > 0 ? 0 : volume;
    voice.target = volume;
    voice.start = voice.envelope;
    voice.seconds = seconds;
    unsigned id = ++impl->nextId;
    impl->voices.emplace(id, std::move(voice));
    if (!impl->transaction)
        impl->apply();
    return id;
}
void Audio::stop(unsigned id, const std::string &channel, float seconds) {
    duration(seconds);
    std::vector<unsigned> remove;
    for (auto &[key, v] : impl->voices)
        if ((!id || id == key) && (channel.empty() || v.channel == channel)) {
            if (seconds > 0)
                fade(key, 0, seconds, true);
            else
                remove.push_back(key);
        }
    for (auto key : remove) {
        if (!impl->transaction)
            ma_sound_stop(impl->voices.at(key).sound.get());
        impl->voices.erase(key);
    }
}
void Audio::volume(const std::string &channel, float value) {
    impl->volumes[channel] = level(value);
    if (!impl->transaction)
        impl->apply();
}
float Audio::volume(const std::string &channel) const {
    return impl->volume(channel);
}
void Audio::fade(unsigned id, float target, float seconds, bool stopAfter) {
    level(target);
    duration(seconds);
    auto it = impl->voices.find(id);
    if (it == impl->voices.end())
        return;
    auto &v = it->second;
    v.start = v.envelope;
    v.target = target;
    v.elapsed = 0;
    v.seconds = seconds;
    v.stopAfter = stopAfter;
    if (seconds == 0) {
        v.envelope = target;
        if (stopAfter) {
            stop(id);
            return;
        }
    }
    if (!impl->transaction)
        impl->apply();
}
float Audio::gain(unsigned id) const {
    auto it = impl->voices.find(id);
    return it == impl->voices.end() ? 0 : it->second.envelope;
}
bool Audio::playing(unsigned id) const {
    auto it = impl->voices.find(id);
    return it != impl->voices.end() && !it->second.paused && !ma_sound_at_end(it->second.sound.get());
}
std::vector<unsigned> Audio::ids(const std::string &channel) const {
    std::vector<unsigned> result;
    for (auto &[id, v] : impl->voices)
        if (channel.empty() || v.channel == channel)
            result.push_back(id);
    return result;
}
void Audio::pause(unsigned id, bool paused) {
    auto it = impl->voices.find(id);
    if (it != impl->voices.end()) {
        it->second.paused = paused;
        if (!impl->transaction)
            impl->apply();
    }
}
void Audio::options(unsigned id, const Json &values) {
    auto found = impl->voices.find(id);
    if (found == impl->voices.end())
        throw std::runtime_error("Unknown sound");
    auto next = found->second.options;
    next.merge_patch(values);
    next = voiceOptions(next);
    found->second.options = std::move(next);
    if (!impl->transaction)
        impl->apply();
}
Json Audio::info(unsigned id) const {
    auto found = impl->voices.find(id);
    if (found == impl->voices.end())
        return {{"playing", false}, {"cursor", 0}, {"duration", 0}};
    auto &v = found->second;
    float cursor = 0, length = 0;
    ma_sound_get_cursor_in_seconds(v.sound.get(), &cursor);
    ma_sound_get_length_in_seconds(v.sound.get(), &length);
    auto result = v.options;
    result.update(Json{{"playing", playing(id)},
                       {"paused", v.paused},
                       {"channel", v.channel},
                       {"path", v.path},
                       {"cursor", cursor},
                       {"duration", length},
                       {"volume", v.envelope},
                       {"stream", v.streaming}});
    return result;
}
Json Audio::diagnostics() const {
    unsigned filters = 0, streams = 0;
    for (auto &[id, v] : impl->voices) {
        if (v.options.value("lowpass", 0.f) > 0 || v.options.value("highpass", 0.f) > 0 ||
            v.options.value("echo_seconds", 0.f) > 0)
            ++filters;
        if (v.streaming)
            ++streams;
    }
    return {{"voices", impl->voices.size()},
            {"max_voices", impl->settings.value("max_voices", 64u)},
            {"stolen", impl->stolen},
            {"dropped", impl->dropped},
            {"streams", streams},
            {"dsp_voices", filters},
            {"duck_gains", impl->duckGains},
            {"settings", impl->settings}};
}
void Audio::configure(const Json &values) {
    auto next = impl->settings;
    next.merge_patch(values);
    bounded(next.value("max_voices", Json(64)), "max_voices", 1, 1024);
    auto policy = next.value("streaming", "auto");
    if (policy != "auto" && policy != "stream" && policy != "decode")
        throw std::runtime_error("streaming must be auto, stream or decode");
    auto overflow = next.value("overflow", "steal_oldest");
    if (overflow != "steal_oldest" && overflow != "reject")
        throw std::runtime_error("Unknown voice overflow policy");
    for (auto &limit : next.value("channel_limits", Json::object()))
        bounded(limit, "channel limit", 1, 1024);
    for (auto &rule : next.value("ducking", Json::array())) {
        rule.at("source").get<std::string>();
        rule.at("target").get<std::string>();
        level(rule.value("gain", .25f));
        duration(rule.value("attack", .05f));
        duration(rule.value("release", .5f));
    }
    impl->settings = std::move(next);
}
void Audio::listener(glm::vec3 position, glm::vec3 direction) {
    for (int i = 0; i < 3; ++i) {
        checkedFloat(position[i], "listener position");
        checkedFloat(direction[i], "listener direction");
    }
    if (glm::length(direction) < 1e-5f)
        throw std::runtime_error("Listener direction cannot be zero");
    impl->listenerPosition = position;
    impl->listenerDirection = glm::normalize(direction);
    if (!impl->transaction)
        impl->apply();
}
void Audio::update(float dt) {
    if (!std::isfinite(dt) || dt < 0 || dt > 1)
        throw std::runtime_error("Invalid audio dt");
    std::vector<unsigned> remove;
    for (auto &[id, v] : impl->voices) {
        if (!v.paused && v.seconds > 0) {
            v.elapsed += dt;
            v.envelope = v.start + (v.target - v.start) * std::min(1.f, v.elapsed / v.seconds);
            if (v.elapsed >= v.seconds) {
                v.seconds = 0;
                if (v.stopAfter)
                    remove.push_back(id);
            }
        }
        if (ma_sound_at_end(v.sound.get()))
            remove.push_back(id);
    }
    for (auto id : remove)
        stop(id);
    std::map<std::string, std::pair<float, float>> desired;
    for (auto &rule : impl->settings.value("ducking", Json::array())) {
        auto source = rule["source"].get<std::string>(), target = rule["target"].get<std::string>();
        bool active = false;
        for (auto &[id, v] : impl->voices)
            if (v.channel == source && playing(id) && v.envelope > 0)
                active = true;
        float gain = active ? rule.value("gain", .25f) : 1.f,
              seconds = rule.value(active ? "attack" : "release", active ? .05f : .5f);
        if (!desired.count(target) || gain < desired[target].first)
            desired[target] = {gain, seconds};
    }
    for (auto &[channel, rule] : desired) {
        if (!impl->duckGains.count(channel))
            impl->duckGains[channel] = 1;
        auto &gain = impl->duckGains[channel];
        gain += (rule.first - gain) * (rule.second > 0 ? 1 - std::exp(-dt / rule.second) : 1);
    }
    if (impl->settings.value("follow_camera", false) && active) {
        auto direction = active->world.cameraTarget - active->world.cameraPosition;
        if (glm::length(direction) > 1e-5f) {
            impl->listenerPosition = active->world.cameraPosition;
            impl->listenerDirection = glm::normalize(direction);
        }
    }
    impl->apply();
    if (silent && impl->initialized) {
        std::vector<float> output(size_t(std::ceil(dt * 48000)) * 2);
        ma_engine_read_pcm_frames(&impl->engine, output.data(), output.size() / 2, nullptr);
    }
}
void Audio::begin() {
    if (impl->transaction)
        throw std::runtime_error("Nested audio transaction");
    impl->backup = impl->voices;
    impl->backupVolumes = impl->volumes;
    impl->backupSettings = impl->settings;
    impl->backupDuck = impl->duckGains;
    impl->backupPosition = impl->listenerPosition;
    impl->backupDirection = impl->listenerDirection;
    impl->transaction = true;
}
void Audio::commit() {
    for (auto &[id, v] : impl->backup)
        if (!impl->voices.count(id))
            ma_sound_stop(v.sound.get());
    impl->backup.clear();
    impl->backupVolumes.clear();
    impl->transaction = false;
    impl->apply();
}
void Audio::rollback() {
    impl->voices = std::move(impl->backup);
    impl->volumes = std::move(impl->backupVolumes);
    impl->settings = std::move(impl->backupSettings);
    impl->duckGains = std::move(impl->backupDuck);
    impl->listenerPosition = impl->backupPosition;
    impl->listenerDirection = impl->backupDirection;
    impl->transaction = false;
    impl->apply();
}
} // namespace forge
