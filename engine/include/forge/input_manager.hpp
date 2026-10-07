#pragma once
// Independent adaptable action evaluation; no runtime/window/Python ownership.
#include <array>
#include <bitset>
#include <forge/input_keys.hpp>
#include <json.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace forge {
using InputJson = nlohmann::json;
struct InputFrame {
    std::bitset<inputKeyCount> keys, pressed;
    std::bitset<8> mouse;
    struct Pad {
        bool present = false;
        std::bitset<15> buttons;
        std::array<double, 6> axes{0, 0, 0, 0, -1, -1};
    };
    std::array<Pad, 16> pads;
    std::array<double, 2> scroll{};
    unsigned modifiers = 0;
    static InputFrame parse(const InputJson &);
};
class InputManager {
  public:
    explicit InputManager(const InputJson &actions = InputJson::object(), double deadzone = .15);
    void define(const std::string &action, const InputJson &bindings,
                const std::string &context = "default");
    void bind(const std::string &action, const InputJson &bindings,
              const std::string &context = "default", const std::string &conflict = "share");
    InputJson conflicts(const std::string &action, const InputJson &bindings,
                        const std::string &context = "default") const;
    void reset(const std::string &action = "", const std::string &context = "");
    void pushContext(const std::string &);
    void popContext();
    std::string context() const;
    InputJson bindings() const;
    InputJson defaults() const;
    InputJson exportProfile() const;
    void importProfile(const InputJson &);
    void update(const InputFrame &, double dt);
    double value(const std::string &) const;
    double rawValue(const std::string &) const;
    bool down(const std::string &) const;
    bool pressed(const std::string &) const;
    bool released(const std::string &) const;
    double held(const std::string &) const;
    bool repeat(const std::string &, double delay = .4, double interval = .08) const;
    void beginRebind(const std::string &, const InputJson &options = InputJson::object(),
                     const InputFrame *baseline = nullptr);
    void cancelRebind();
    bool capturing() const;
    InputJson captureState() const;

  private:
    enum class Kind { Key, Mouse, Pad, Axis, Wheel };
    struct Binding {
        Kind kind = Kind::Key;
        int control = 0, device = -1, direction = 0;
        unsigned modifiers = 0;
        bool exact = false;
        double scale = 1, deadzone = .15;
        InputJson data;
    };
    using Actions = std::map<std::string, std::vector<Binding>>;
    using Contexts = std::map<std::string, Actions>;
    struct State {
        double value = 0, raw = 0, previous = 0, held = 0, previousHeld = 0;
    };
    struct Capture {
        std::string action, context, policy = "reject";
        size_t slot = 0;
        double remaining = 10, threshold = .6;
        bool chords = true, escapeCancels = true;
        unsigned devices = 15;
        InputFrame ignored;
        int modifier = -1;
    };
    Contexts current, initial;
    std::vector<std::string> stack{"default"};
    std::map<std::string, State> states;
    double deadzone;
    InputFrame last;
    std::optional<Capture> capture;
    std::optional<Binding> blocked;
    InputJson captureResult = {{"status", "idle"}}, profileExtras = InputJson::object();
    Binding binding(const InputJson &) const;
    std::vector<Binding> list(const InputJson &) const;
    Contexts contexts(const InputJson &) const;
    static void limits(const Contexts &);
    static bool sourcesOverlap(const Binding &, const Binding &);
    static bool overlaps(const Binding &, const Binding &);
    static double raw(const Binding &, const InputFrame &, bool primaryOnly = false);
    void apply(const std::string &, const std::vector<Binding> &, const std::string &,
               const std::string &, const std::vector<Binding> *edited = nullptr);
    void clearState();
    void captureFrame(const InputFrame &, double dt);
    const State &state(const std::string &) const;
    static InputJson document(const Contexts &);
};
} // namespace forge
