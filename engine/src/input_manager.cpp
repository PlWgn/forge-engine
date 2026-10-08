#include <algorithm>
#include <cmath>
#include <forge/input_manager.hpp>
#include <stdexcept>

namespace forge {
namespace {
using J = InputJson;
const std::vector<std::string> buttons = {
    "a",     "b",          "x",           "y",  "left_bumper", "right_bumper", "back", "start",
    "guide", "left_stick", "right_stick", "up", "right",       "down",         "left"};
const std::vector<std::string> axes = {"left_x",  "left_y",       "right_x",
                                       "right_y", "left_trigger", "right_trigger"};
void require(bool valid, const std::string &message) {
    if (!valid)
        throw std::invalid_argument(message);
}
void require(bool valid, const char *message) {
    if (!valid)
        throw std::invalid_argument(message);
}
void name(const std::string &value) {
    require(!value.empty() && value.size() <= 128, "Input name needs 1..128 bytes");
}
double number(double n, double low, double high, const char *field) {
    if (!std::isfinite(n) || n < low || n > high)
        throw std::invalid_argument(std::string(field) + " outside supported range");
    return n;
}
double number(const J &value, double low, double high, const char *field) {
    if (!value.is_number())
        throw std::invalid_argument(std::string(field) + " must be a number");
    return number(value.get<double>(), low, high, field);
}
bool boolean(const J &value, const char *field) {
    require(value.is_boolean(), std::string(field) + " must be boolean");
    return value.get<bool>();
}
int integer(const J &value, int low, int high, const char *field) {
    if (!value.is_number_integer())
        throw std::invalid_argument(std::string(field) + " must be an integer");
    return int(number(value, low, high, field));
}
int index(const std::string &text, int maximum) {
    require(!text.empty() && text.size() <= 2, "Invalid input device/control index");
    int n = 0;
    for (char c : text) {
        require(c >= '0' && c <= '9', "Invalid input index");
        n = n * 10 + c - '0';
    }
    require(n <= maximum, "Input index outside supported range");
    return n;
}
std::vector<std::string> split(const std::string &text) {
    std::vector<std::string> result;
    size_t start = 0;
    for (;;) {
        auto end = text.find(':', start);
        result.push_back(text.substr(start, end - start));
        if (end == std::string::npos)
            return result;
        start = end + 1;
    }
}
int control(const std::string &text, const std::vector<std::string> &names) {
    auto found = std::find(names.begin(), names.end(), text);
    require(found != names.end(), "Unknown gamepad control: " + text);
    return int(found - names.begin());
}
J modifiers(unsigned mask) {
    J result = J::array();
    unsigned bit = 1;
    for (const char *text : {"SHIFT", "CTRL", "ALT", "SUPER"}) {
        if (mask & bit)
            result.push_back(text);
        bit <<= 1;
    }
    return result;
}
void policy(const std::string &value) {
    require(value == "share" || value == "reject" || value == "replace",
            "Conflict policy needs share, reject or replace");
}
bool active(double n) { return std::abs(n) > .001; }
void validateFrame(const InputFrame &frame) {
    if (frame.modifiers > 15)
        throw std::invalid_argument("Invalid input modifier mask");
    for (double value : frame.scroll)
        number(value, -1e100, 1e100, "scroll");
    for (const auto &pad : frame.pads)
        if (pad.present)
            for (double value : pad.axes)
                number(value, -1, 1, "axis");
}
} // namespace

InputFrame InputFrame::parse(const J &snapshot) {
    require(snapshot.is_object(), "Input snapshot must be an object");
    InputFrame frame;
    static const J empty = J::array(), zeroScroll = J::array({0, 0});
    auto keys = [&](const char *field, auto &destination) {
        const auto &values = snapshot.contains(field) ? snapshot.at(field) : empty;
        require(values.is_array() && values.size() <= 512, "Input keys need a bounded list");
        for (const auto &value : values) {
            require(value.is_string() && !value.get_ref<const std::string &>().empty() &&
                        value.get_ref<const std::string &>().size() <= 32,
                    "Invalid input key");
            try {
                int key = inputKey(value.get<std::string>());
                destination.set(key);
                unsigned mod = inputModifier(key);
                if (mod) {
                    int offset = mod == 1 ? 0 : mod == 2 ? 1 : mod == 4 ? 2 : 3;
                    destination.set(inputShift + offset);
                }
            } catch (const std::invalid_argument &) { /* Unknown extension keys are ignored. */
            }
        }
    };
    keys("keys", frame.keys);
    keys("pressed", frame.pressed);
    for (int i = 0; i < 4; ++i)
        if (frame.keys[inputShift + i] || frame.pressed[inputShift + i])
            frame.modifiers |= 1u << i;
    auto readButtons = [&](const J &values, auto &destination, int maximum) {
        require(values.is_array() && values.size() <= size_t(maximum + 1),
                "Input buttons need a bounded list");
        for (const auto &value : values)
            destination.set(integer(value, 0, maximum, "button"));
    };
    readButtons(snapshot.contains("buttons") ? snapshot.at("buttons") : empty, frame.mouse, 7);
    const auto &pads = snapshot.contains("gamepads") ? snapshot.at("gamepads") : empty;
    require(pads.is_array() && pads.size() <= 16, "Input gamepads need a bounded list");
    for (const auto &pad : pads) {
        require(pad.is_object() && pad.contains("id"), "Gamepad needs an id");
        int id = integer(pad.at("id"), 0, 15, "gamepad id");
        require(!frame.pads[id].present, "Duplicate gamepad id");
        auto &target = frame.pads[id];
        target.present = true;
        readButtons(pad.contains("buttons") ? pad.at("buttons") : empty, target.buttons, 14);
        if (pad.contains("axes")) {
            const auto &values = pad.at("axes");
            require(values.is_array() && values.size() == 6, "Gamepad needs six axes");
            for (int i = 0; i < 6; ++i)
                target.axes[i] = number(values[i], -1, 1, "axis");
        }
    }
    const auto &scroll = snapshot.contains("scroll") ? snapshot.at("scroll") : zeroScroll;
    require(scroll.is_array() && scroll.size() == 2, "Scroll needs two components");
    for (int i = 0; i < 2; ++i)
        frame.scroll[i] = number(scroll[i], -1e100, 1e100, "scroll");
    return frame;
}

InputManager::InputManager(const J &actions, double zone) : deadzone(zone) {
    number(J(zone), 0, std::nextafter(1.0, 0.0), "deadzone");
    current = contexts(J{{"default", actions}});
    initial = current;
}
InputManager::Binding InputManager::binding(const J &source) const {
    J data = source.is_string() ? J{{"input", source}} : source;
    require(data.is_object() && data.contains("input") && data.at("input").is_string(),
            "Binding needs an input string");
    auto text = data.at("input").get<std::string>();
    require(text.size() <= 64, "Binding input too long");
    auto parts = split(text);
    Binding b;
    if (parts.size() == 2 && parts[0] == "key") {
        b.kind = Kind::Key;
        b.control = inputKey(parts[1]);
        text = "key:" + inputKeyName(b.control);
    } else if (parts.size() == 2 && parts[0] == "mouse") {
        b.kind = Kind::Mouse;
        b.control = index(parts[1], 7);
        text = "mouse:" + std::to_string(b.control);
    } else if (parts.size() == 3 && (parts[0] == "pad" || parts[0] == "axis")) {
        b.kind = parts[0] == "pad" ? Kind::Pad : Kind::Axis;
        b.device = parts[1] == "any" ? -1 : index(parts[1], 15);
        b.control = control(parts[2], b.kind == Kind::Pad ? buttons : axes);
        text = parts[0] + ":" + (b.device < 0 ? "any" : std::to_string(b.device)) + ":" + parts[2];
    } else if (parts.size() == 2 && parts[0] == "wheel") {
        b.kind = Kind::Wheel;
        b.control = control(parts[1], {"up", "down", "left", "right"});
    } else
        throw std::invalid_argument("Unknown binding: " + text);
    b.scale = number(data.value("scale", J(1)), -1000, 1000, "scale");
    b.deadzone =
        number(data.value("deadzone", J(deadzone)), 0, std::nextafter(1.0, 0.0), "deadzone");
    b.direction = integer(data.value("direction", J(0)), -1, 1, "direction");
    require(b.direction == 0 || b.kind == Kind::Axis, "direction applies to axes only");
    b.exact = boolean(data.value("exact_modifiers", J(false)), "exact_modifiers");
    auto mods = data.value("modifiers", J::array());
    require(mods.is_array() && mods.size() <= 4, "modifiers need a list of SHIFT/CTRL/ALT/SUPER");
    for (const auto &mod : mods) {
        require(mod.is_string(), "Modifier must be a string");
        int key = inputKey(mod.get<std::string>());
        unsigned bit = inputModifier(key);
        require(key >= inputShift && bit && !(b.modifiers & bit),
                "Modifiers must be unique generic modifier names");
        b.modifiers |= bit;
    }
    data["input"] = text;
    data["modifiers"] = modifiers(b.modifiers);
    data["scale"] = b.scale;
    data["deadzone"] = b.deadzone;
    data["direction"] = b.direction;
    data["exact_modifiers"] = b.exact;
    b.data = std::move(data);
    return b;
}
std::vector<InputManager::Binding> InputManager::list(const J &source) const {
    auto values = (source.is_string() || source.is_object()) ? J::array({source}) : source;
    require(values.is_array() && values.size() <= 16, "Action needs at most 16 bindings");
    std::vector<Binding> result;
    result.reserve(values.size());
    for (const auto &item : values)
        result.push_back(binding(item));
    return result;
}
InputManager::Contexts InputManager::contexts(const J &source) const {
    require(source.is_object() && source.size() <= 64, "Contexts need at most 64 entries");
    Contexts result;
    size_t count = 0;
    for (auto context = source.begin(); context != source.end(); ++context) {
        name(context.key());
        require(context.value().is_object() && context.value().size() <= 1024,
                "Context needs at most 1024 actions");
        count += context.value().size();
        require(count <= 4096, "Input action limit (4096) exceeded");
        auto &actions = result[context.key()];
        for (auto action = context.value().begin(); action != context.value().end(); ++action) {
            name(action.key());
            actions[action.key()] = list(action.value());
        }
    }
    limits(result);
    return result;
}
void InputManager::limits(const Contexts &source) {
    require(source.size() <= 64, "Input context limit (64) exceeded");
    size_t count = 0;
    for (const auto &ctx : source) {
        require(ctx.second.size() <= 1024, "Input actions per context limit (1024) exceeded");
        count += ctx.second.size();
    }
    require(count <= 4096, "Input action limit (4096) exceeded");
}
void InputManager::define(const std::string &action, const J &source, const std::string &ctx) {
    name(action);
    name(ctx);
    auto parsed = list(source);
    auto next = current, defaults = initial;
    next[ctx][action] = parsed;
    defaults[ctx][action] = parsed;
    limits(next);
    limits(defaults);
    current.swap(next);
    initial.swap(defaults);
    clearState();
}
bool InputManager::sourcesOverlap(const Binding &a, const Binding &b) {
    if (a.kind != b.kind)
        return false;
    if (a.control != b.control) {
        if (a.kind != Kind::Key || !inputModifier(a.control) ||
            inputModifier(a.control) != inputModifier(b.control) ||
            (a.control < inputShift && b.control < inputShift))
            return false;
    }
    if ((a.kind == Kind::Axis || a.kind == Kind::Pad) && a.device >= 0 && b.device >= 0 &&
        a.device != b.device)
        return false;
    if (a.kind == Kind::Axis && a.direction * b.direction == -1)
        return false;
    return true;
}
bool InputManager::overlaps(const Binding &a, const Binding &b) {
    if (!sourcesOverlap(a, b))
        return false;
    unsigned primary = a.kind == Kind::Key ? inputModifier(a.control) : 0;
    auto am = a.modifiers | primary, bm = b.modifiers | primary;
    if (a.exact && b.exact)
        return am == bm;
    if (a.exact)
        return (am & bm) == bm;
    if (b.exact)
        return (bm & am) == am;
    return true;
}
J InputManager::conflicts(const std::string &action, const J &source,
                          const std::string &ctx) const {
    name(action);
    name(ctx);
    auto parsed = list(source);
    J result = J::array();
    auto context = current.find(ctx);
    if (context == current.end())
        return result;
    for (const auto &other : context->second)
        if (other.first != action) {
            for (size_t slot = 0; slot < other.second.size(); ++slot)
                if (std::any_of(parsed.begin(), parsed.end(),
                                [&](const Binding &b) { return overlaps(b, other.second[slot]); }))
                    result.push_back({{"action", other.first},
                                      {"context", ctx},
                                      {"slot", slot},
                                      {"binding", other.second[slot].data}});
        }
    return result;
}
void InputManager::apply(const std::string &action, const std::vector<Binding> &bindings,
                         const std::string &ctx, const std::string &mode,
                         const std::vector<Binding> *edited) {
    policy(mode);
    auto next = current;
    auto &actions = next[ctx];
    for (auto &other : actions)
        if (other.first != action) {
            auto conflict = [&](const Binding &old) {
                const auto &changed = edited ? *edited : bindings;
                return std::any_of(changed.begin(), changed.end(),
                                   [&](const Binding &b) { return overlaps(b, old); });
            };
            if (mode == "reject")
                require(std::none_of(other.second.begin(), other.second.end(), conflict),
                        "Input binding conflict with " + other.first);
            if (mode == "replace")
                other.second.erase(
                    std::remove_if(other.second.begin(), other.second.end(), conflict),
                    other.second.end());
        }
    actions[action] = bindings;
    limits(next);
    current.swap(next);
}
void InputManager::bind(const std::string &action, const J &source, const std::string &ctx,
                        const std::string &mode) {
    name(action);
    name(ctx);
    auto parsed = list(source);
    apply(action, parsed, ctx, mode);
    clearState();
}
J InputManager::document(const Contexts &source) {
    J result = J::object();
    for (const auto &ctx : source) {
        result[ctx.first] = J::object();
        for (const auto &action : ctx.second) {
            auto &values = result[ctx.first][action.first];
            values = J::array();
            for (const auto &b : action.second)
                values.push_back(b.data);
        }
    }
    return result;
}
J InputManager::bindings() const { return document(current); }
J InputManager::defaults() const { return document(initial); }
J InputManager::exportProfile() const {
    auto result = profileExtras;
    result["format"] = "forge.input.bindings/1";
    result["deadzone"] = deadzone;
    result["contexts"] = document(current);
    return result;
}
void InputManager::importProfile(const J &source) {
    require(source.is_object() && source.value("format", J()) == "forge.input.bindings/1",
            "Unsupported input profile format");
    double zone =
        number(source.value("deadzone", J(deadzone)), 0, std::nextafter(1.0, 0.0), "deadzone");
    require(source.contains("contexts"), "Input profile needs contexts");
    InputManager parser(J::object(), zone);
    auto overrides = parser.contexts(source.at("contexts"));
    auto next = initial;
    for (const auto &ctx : overrides) {
        auto &target = next[ctx.first];
        for (const auto &action : ctx.second)
            target[action.first] = action.second;
    }
    limits(next);
    auto extras = source;
    extras.erase("contexts");
    extras.erase("deadzone");
    extras.erase("format");
    current.swap(next);
    profileExtras.swap(extras);
    deadzone = zone;
    stack = {"default"};
    clearState();
}
void InputManager::reset(const std::string &action, const std::string &ctx) {
    auto next = current;
    if (action.empty() && ctx.empty())
        next = initial;
    else {
        auto target = ctx.empty() ? "default" : ctx;
        auto defaults = initial.find(target);
        require(defaults != initial.end(), "No default input context: " + target);
        if (action.empty())
            next[target] = defaults->second;
        else {
            auto found = defaults->second.find(action);
            require(found != defaults->second.end(), "No default input action: " + action);
            next[target][action] = found->second;
        }
    }
    current.swap(next);
    // Contexts added only through bind() have no defaults. Besides the active
    // context, drop stale lower entries so a later pop cannot expose one.
    if (!current.count(context()))
        stack = {"default"};
    else
        stack.erase(std::remove_if(stack.begin() + 1, stack.end(),
                                   [&](const std::string &ctx) { return !current.count(ctx); }),
                    stack.end());
    clearState();
}
void InputManager::clearState() {
    states.clear();
    capture.reset();
    blocked.reset();
    captureResult = {{"status", "idle"}};
}
void InputManager::pushContext(const std::string &ctx) {
    require(current.count(ctx) != 0, "Unknown input context: " + ctx);
    require(stack.size() < 32, "Input context stack limit (32) exceeded");
    stack.push_back(ctx);
    clearState();
}
void InputManager::popContext() {
    require(stack.size() > 1, "Cannot pop default input context");
    stack.pop_back();
    clearState();
}
std::string InputManager::context() const { return stack.back(); }
double InputManager::raw(const Binding &b, const InputFrame &frame, bool primaryOnly) {
    unsigned primary = b.kind == Kind::Key ? inputModifier(b.control) : 0;
    if (!primaryOnly && ((frame.modifiers & b.modifiers) != b.modifiers ||
                         (b.exact && (frame.modifiers | primary) != (b.modifiers | primary))))
        return 0;
    if (b.kind == Kind::Key)
        return frame.keys[b.control] || frame.pressed[b.control] ? 1 : 0;
    if (b.kind == Kind::Mouse)
        return frame.mouse[b.control] ? 1 : 0;
    if (b.kind == Kind::Wheel) {
        double v = (b.control < 2 ? frame.scroll[1] : frame.scroll[0]) *
                   (b.control == 1 || b.control == 2 ? -1 : 1);
        return std::clamp(v, 0.0, 1.0);
    }
    double result = 0;
    for (int id = 0; id < 16; ++id)
        if ((b.device < 0 || b.device == id) && frame.pads[id].present) {
            double v = b.kind == Kind::Pad ? (frame.pads[id].buttons[b.control] ? 1 : 0)
                                           : frame.pads[id].axes[b.control];
            if (b.kind == Kind::Axis) {
                if (b.control >= 4)
                    v = (v + 1) * .5;
                if (b.direction)
                    v = std::max(0.0, v * b.direction);
                v = std::abs(v) <= b.deadzone
                        ? 0
                        : std::copysign((std::abs(v) - b.deadzone) / (1 - b.deadzone), v);
            }
            if (std::abs(v) > std::abs(result))
                result = v;
        }
    return result;
}
void InputManager::update(const InputFrame &frame, double dt) {
    number(dt, 0, 1, "input dt");
    validateFrame(frame);
    bool suppress = bool(capture);
    if (capture)
        captureFrame(frame, dt);
    if (blocked && !active(raw(*blocked, frame, true)))
        blocked.reset();
    const auto &actions = current.at(context());
    for (const auto &action : actions) {
        auto &s = states[action.first];
        s.previous = s.value;
        s.previousHeld = s.held;
        s.raw = 0;
        if (!suppress)
            for (const auto &b : action.second) {
                if (blocked && sourcesOverlap(*blocked, b))
                    continue;
                s.raw += raw(b, frame) * b.scale;
            }
        s.value = std::clamp(s.raw, -1.0, 1.0);
        s.held = active(s.value) ? (active(s.previous) ? s.held + dt : 0) : 0;
    }
    last = frame;
}
const InputManager::State &InputManager::state(const std::string &action) const {
    static const State empty;
    auto it = states.find(action);
    return it == states.end() ? empty : it->second;
}
double InputManager::value(const std::string &action) const { return state(action).value; }
double InputManager::rawValue(const std::string &action) const { return state(action).raw; }
bool InputManager::down(const std::string &action) const { return active(state(action).value); }
bool InputManager::pressed(const std::string &action) const {
    const auto &s = state(action);
    return active(s.value) && !active(s.previous);
}
bool InputManager::released(const std::string &action) const {
    const auto &s = state(action);
    return !active(s.value) && active(s.previous);
}
double InputManager::held(const std::string &action) const { return state(action).held; }
bool InputManager::repeat(const std::string &action, double delay, double interval) const {
    number(delay, 0, 3600, "repeat delay");
    number(interval, .001, 60, "repeat interval");
    const auto &s = state(action);
    if (pressed(action))
        return true;
    if (!down(action) || s.held < delay)
        return false;
    if (s.previousHeld < delay)
        return true;
    return std::floor((s.held - delay) / interval) >
           std::floor((s.previousHeld - delay) / interval);
}
void InputManager::beginRebind(const std::string &action, const J &options,
                               const InputFrame *baseline) {
    require(options.is_object(), "Rebind options must be an object");
    Capture next;
    next.action = action;
    next.context = options.value("context", context());
    auto ctx = current.find(next.context);
    require(ctx != current.end() && ctx->second.count(action),
            "Unknown input action/context for rebind");
    const auto &slots = ctx->second.at(action);
    next.slot = size_t(integer(options.value("slot", J(0)), 0, 15, "slot"));
    require(next.slot <= slots.size(), "Rebind slot must replace or append a binding");
    next.policy = options.value("conflict", std::string("reject"));
    policy(next.policy);
    next.remaining = number(options.value("timeout", J(10)), .001, 300, "capture timeout");
    next.threshold = number(options.value("threshold", J(.6)), .001, 1, "capture threshold");
    next.chords = boolean(options.value("chords", J(true)), "chords");
    next.escapeCancels = boolean(options.value("escape_cancels", J(true)), "escape_cancels");
    auto devices = options.value("devices", J::array({"keyboard", "mouse", "gamepad", "wheel"}));
    require(devices.is_array() && !devices.empty() && devices.size() <= 4,
            "Capture devices need a nonempty list");
    next.devices = 0;
    for (const auto &device : devices) {
        require(device.is_string(), "Invalid capture device");
        auto id = control(device.get<std::string>(), {"keyboard", "mouse", "gamepad", "wheel"});
        require(!(next.devices & (1u << id)), "Duplicate capture device");
        next.devices |= 1u << id;
    }
    next.ignored = baseline ? *baseline : last;
    validateFrame(next.ignored);
    next.ignored.keys |= next.ignored.pressed;
    capture = std::move(next);
    // UI callbacks can start capture after this frame's automatic evaluation.
    // Suppress immediately so later gameplay callbacks cannot reuse its opening key.
    for (auto &action : states) {
        auto &state = action.second;
        state.previous = state.value;
        state.previousHeld = state.held;
        state.value = state.raw = state.held = 0;
    }
    blocked.reset();
    last = capture->ignored;
    captureResult = {{"status", "listening"}, {"action", action}, {"context", capture->context}};
}
void InputManager::cancelRebind() {
    capture.reset();
    captureResult = {{"status", "canceled"}};
}
bool InputManager::capturing() const { return bool(capture); }
J InputManager::captureState() const { return captureResult; }
void InputManager::captureFrame(const InputFrame &frame, double dt) {
    auto &c = *capture;
    c.remaining -= dt;
    if (c.remaining <= 0) {
        capture.reset();
        captureResult = {{"status", "timeout"}};
        return;
    }
    auto freshKey = [&](int key) {
        return !c.ignored.keys[key] && !last.keys[key] && (frame.keys[key] || frame.pressed[key]);
    };
    if (c.escapeCancels && freshKey(inputKey("ESCAPE"))) {
        cancelRebind();
        return;
    }
    std::optional<Binding> found;
    auto candidate = [&](const std::string &input, int direction = 0, unsigned mods = 0) {
        const auto &slots = current.at(c.context).at(c.action);
        J data = c.slot < slots.size() ? slots[c.slot].data : J::object();
        data["input"] = input;
        data["modifiers"] = modifiers(mods);
        data["exact_modifiers"] = false;
        data.erase("direction");
        if (direction)
            data["direction"] = direction;
        found = binding(data);
    };
    if (c.devices & 1) {
        for (int key = 0; key < inputShift; ++key)
            if (!inputModifier(key) && freshKey(key)) {
                auto text = inputKeyName(key);
                if (!text.empty()) {
                    candidate("key:" + text, 0, c.chords ? frame.modifiers : 0);
                    break;
                }
            }
        if (!found) {
            for (int key = 0; key < inputKeyCount; ++key)
                if (inputModifier(key) && freshKey(key)) {
                    c.modifier = key;
                    break;
                }
            if (c.modifier >= 0 && !frame.keys[c.modifier] && !frame.pressed[c.modifier]) {
                candidate("key:" + inputKeyName(c.modifier));
                c.modifier = -1;
            }
        }
    }
    if (!found && (c.devices & 2))
        for (int i = 0; i < 8; ++i)
            if (frame.mouse[i] && !last.mouse[i] && !c.ignored.mouse[i]) {
                candidate("mouse:" + std::to_string(i));
                break;
            }
    if (!found && (c.devices & 4))
        for (int id = 0; id < 16 && !found; ++id)
            if (frame.pads[id].present) {
                for (int i = 0; i < 15; ++i)
                    if (frame.pads[id].buttons[i] && !last.pads[id].buttons[i] &&
                        !c.ignored.pads[id].buttons[i]) {
                        candidate("pad:" + std::to_string(id) + ":" + buttons[i]);
                        break;
                    }
                for (int i = 0; i < 6 && !found; ++i) {
                    auto v = frame.pads[id].axes[i], previous = last.pads[id].axes[i],
                         ignored = c.ignored.pads[id].axes[i];
                    if (i >= 4) {
                        v = (v + 1) * .5;
                        previous = (previous + 1) * .5;
                        ignored = (ignored + 1) * .5;
                    }
                    if (std::abs(v) >= c.threshold && std::abs(previous) < c.threshold &&
                        std::abs(ignored) < c.threshold)
                        candidate("axis:" + std::to_string(id) + ":" + axes[i], v < 0 ? -1 : 1);
                }
            }
    if (!found && (c.devices & 8) && c.ignored.scroll[0] == 0 && c.ignored.scroll[1] == 0) {
        if (frame.scroll[1] != 0)
            candidate(frame.scroll[1] > 0 ? "wheel:up" : "wheel:down");
        else if (frame.scroll[0] != 0)
            candidate(frame.scroll[0] > 0 ? "wheel:right" : "wheel:left");
    }
    c.ignored.keys &= frame.keys;
    c.ignored.mouse &= frame.mouse;
    c.ignored.scroll = {0, 0};
    for (int id = 0; id < 16; ++id) {
        c.ignored.pads[id].buttons &= frame.pads[id].buttons;
        for (int i = 0; i < 6; ++i) {
            auto v = frame.pads[id].axes[i];
            if (i >= 4)
                v = (v + 1) * .5;
            if (std::abs(v) < c.threshold)
                c.ignored.pads[id].axes[i] = i >= 4 ? -1 : 0;
        }
    }
    if (!found)
        return;
    auto hits = conflicts(c.action, J::array({found->data}), c.context);
    captureResult = {{"status", "conflict"}, {"action", c.action},     {"context", c.context},
                     {"slot", c.slot},       {"binding", found->data}, {"conflicts", hits}};
    if (!hits.empty() && c.policy == "reject")
        return;
    auto next = current.at(c.context).at(c.action);
    if (c.slot == next.size())
        next.push_back(*found);
    else
        next[c.slot] = *found;
    std::vector<Binding> changed{*found};
    apply(c.action, next, c.context, c.policy, &changed);
    blocked = *found;
    if (blocked->kind == Kind::Axis)
        blocked->deadzone = std::min(blocked->deadzone, c.threshold * .5);
    captureResult["status"] = "bound";
    capture.reset();
}
} // namespace forge
