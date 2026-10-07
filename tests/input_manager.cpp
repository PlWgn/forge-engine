#include <cmath>
#include <forge/input_manager.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge;
using J = InputJson;
void windowInputTests();
namespace {
void check(bool condition, const char *text) {
    if (!condition)
        throw std::runtime_error(text);
}
void near(double a, double b) { check(std::abs(a - b) < 1e-9, "Unexpected action value"); }
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid input accepted");
}
InputFrame frame(J keys = J::array(), J mouse = J::array(), J pads = J::array()) {
    return InputFrame::parse({{"keys", keys.is_null() ? J::array() : keys},
                              {"buttons", mouse.is_null() ? J::array() : mouse},
                              {"gamepads", pads.is_null() ? J::array() : pads}});
}
J pad(int id, J values, J buttons = J::array()) {
    return {{"id", id}, {"axes", values}, {"buttons", buttons}};
}
void actions() {
    InputManager m(
        {{"move", J::array({J{{"input", "key:A"}, {"scale", -1}}, "key:D", "axis:any:left_x"})},
         {"jump", J::array({"key:SPACE", "pad:any:a"})},
         {"save",
          J{{"input", "key:S"}, {"modifiers", J::array({"CTRL"})}, {"exact_modifiers", true}}},
         {"trigger", "axis:0:left_trigger"},
         {"wheel", "wheel:down"}});
    m.update(frame({"A", "SPACE"}), .1);
    near(m.value("move"), -1);
    check(m.pressed("jump"), "Missing press");
    m.update(frame({"A", "SPACE"}), .1);
    near(m.held("jump"), .1);
    check(!m.pressed("jump"), "Repeated edge");
    m.update(frame(), .1);
    check(m.released("jump"), "Missing release");
    near(m.value("trigger"), 0);
    m.update(frame({"RIGHT_CTRL", "S"}), .1);
    check(m.down("save"), "Right modifier chord failed");
    m.update(frame({"RIGHT_CTRL", "SHIFT", "S"}), .1);
    check(!m.down("save"), "Exact modifiers ignored");
    m.update(frame({}, {}, {pad(0, {.575, 0, 0, 0, -1, -1}), pad(1, {-.8, 0, 0, 0, -1, -1}, {0})}),
             .1);
    near(m.value("move"), -(.8 - .15) / .85);
    check(m.down("jump"), "Wildcard pad failed");
    m.update(frame({}, {}, {pad(0, {0, 0, 0, 0, 0, -1})}), .1);
    near(m.value("trigger"), (.5 - .15) / .85);
    m.update(InputFrame::parse({{"scroll", {0, -100}}}), .1);
    near(m.value("wheel"), 1);
    m.define("accept", "key:ENTER", "menu");
    m.pushContext("menu");
    m.update(frame({"SPACE", "ENTER"}), .1);
    check(!m.down("jump") && m.pressed("accept"), "Context not exclusive");
    m.popContext();
    check(!m.down("accept"), "Stale context state");
    rejects([&] { m.popContext(); });
    rejects([&] { m.pushContext("missing"); });
    m.update(frame({"SPACE"}), .1);
    check(m.repeat("jump"), "Initial repeat pulse missing");
    for (int i = 0; i < 4; ++i)
        m.update(frame({"SPACE"}), .1);
    check(m.repeat("jump", .4, .1), "Delayed repeat pulse missing");
    rejects([&] { m.repeat("jump", .4, 0); });
}
void profiles() {
    InputManager m({{"jump", "key:SPACE"}, {"fire", J::array({"key:F", "mouse:0"})}});
    auto before = m.exportProfile();
    rejects([&] { m.bind("jump", "mouse:0", "default", "reject"); });
    check(m.exportProfile() == before, "Rejected conflict mutated profile");
    check(m.conflicts("jump", "mouse:0").size() == 1, "Conflict not reported");
    m.bind("jump", "mouse:0", "default", "replace");
    check(m.bindings()["default"]["fire"].size() == 1, "Replace removed unrelated binding");
    m.reset("jump");
    check(m.bindings()["default"]["jump"][0]["input"] == "key:SPACE", "Reset lost defaults");
    m.bind("jump", J{{"input", "key:J"}, {"plugin", {{"custom", true}}}});
    auto saved = m.exportProfile();
    saved["extension"] = {{"language", "en"}};
    InputManager upgraded({{"jump", "key:SPACE"}, {"new_action", "key:N"}});
    upgraded.importProfile(saved);
    check(upgraded.bindings()["default"].contains("new_action"), "New defaults lost on load");
    check(upgraded.exportProfile()["extension"] == saved["extension"],
          "Unknown profile metadata lost");
    check(upgraded.bindings()["default"]["jump"][0]["plugin"]["custom"] == true,
          "Binding metadata lost");
    upgraded.update(frame({"J"}), .1);
    auto good = upgraded.exportProfile();
    auto bad = good;
    bad["contexts"]["default"]["jump"][0]["scale"] = 1e100;
    rejects([&] { upgraded.importProfile(bad); });
    check(upgraded.exportProfile() == good && upgraded.down("jump"), "Bad load mutated state");
    // Explicit compiled deadzones survive changed profile defaults and another load.
    auto override = good;
    override["deadzone"] = .5;
    override["contexts"]["default"]["jump"][0].erase("deadzone");
    upgraded.importProfile(override);
    InputManager roundtrip({{"added", "axis:any:left_x"}});
    roundtrip.importProfile(upgraded.exportProfile());
    auto exported = roundtrip.exportProfile();
    InputManager second;
    second.importProfile(exported);
    check(second.exportProfile() == exported, "Compiled defaults changed across profile roundtrip");
    upgraded.reset();
    check(upgraded.bindings() == upgraded.defaults(), "Full reset failed");
    rejects([&] { upgraded.bind("bad", "key:F1garbage"); });
    rejects([&] { upgraded.bind("bad", J{{"input", "key:A"}, {"scale", true}}); });
    rejects([&] { upgraded.bind("bad", J{{"input", "key:A"}, {"deadzone", 1}}); });
    rejects([&] { upgraded.bind("bad", J{{"input", "axis:any:left_x"}, {"direction", .5}}); });
    rejects([&] { upgraded.bind("bad", J{{"input", "key:A"}, {"modifiers", {"CTRL", "CTRL"}}}); });
    rejects([&] { upgraded.bind("bad", J{{"input", "key:A"}, {"exact_modifiers", 1}}); });
    rejects([&] { upgraded.update(frame(), std::numeric_limits<double>::infinity()); });
    for (const char *key : {"HOME", "END", "DELETE", "F25", "KP_9", "RIGHT_SUPER", ";"})
        check(inputKey(inputKeyName(inputKey(key))) == inputKey(key),
              "Key registry roundtrip failed");
    rejects([&] {
        InputFrame::parse(
            {{"gamepads", {pad(0, {0, 0, 0, 0, -1, -1}), pad(0, {0, 0, 0, 0, -1, -1})}}});
    });
    rejects([&] { InputFrame::parse({{"gamepads", {pad(0, {0, 0, 0, 0, -1, 2})}}}); });
    rejects([&] { InputFrame::parse({{"keys", {true}}}); });
}
void capture() {
    InputManager m({{"jump", "key:SPACE"}, {"fire", "key:F"}});
    m.update(frame({"SPACE"}), .1);
    m.beginRebind("jump");
    check(!m.down("jump") && m.released("jump"),
          "Capture did not immediately suppress evaluated actions");
    m.cancelRebind();
    auto opening = frame({}, {0});
    m.beginRebind("jump", J::object(), &opening);
    m.update(opening, .1);
    check(m.capturing(), "Opening click captured");
    m.update(frame(), .1);
    m.update(frame({"CTRL", "J"}), .1);
    check(!m.capturing() && m.captureState()["status"] == "bound", "Chord not captured");
    check(m.bindings()["default"]["jump"][0]["modifiers"] == J::array({"CTRL"}),
          "Chord modifier lost");
    check(!m.down("jump"), "Confirmation leaked into actions");
    m.update(frame({"CTRL", "J"}), .1);
    check(!m.down("jump"), "Held confirmation leaked into actions");
    m.update(frame({"CTRL"}), .1);
    m.update(frame({"CTRL", "J"}), .1);
    check(m.pressed("jump"), "Bound action not usable after release");
    m.beginRebind("jump");
    m.update(frame(), .1);
    m.update(frame({"F"}), .1);
    check(m.capturing() && m.captureState()["status"] == "conflict",
          "Conflict should keep capture listening");
    m.update(frame(), .1);
    m.update(frame({"K"}), .1);
    check(m.captureState()["status"] == "bound", "Retry after conflict failed");
    m.beginRebind("jump", {{"timeout", .15}});
    m.update(frame(), .1);
    m.update(frame(), .1);
    check(!m.capturing() && m.captureState()["status"] == "timeout", "Capture timeout failed");
    m.beginRebind("jump");
    m.update(frame({"ESCAPE"}), .1);
    check(m.captureState()["status"] == "canceled", "Escape did not cancel");
    m.update(frame(), .1);
    m.beginRebind("jump");
    m.update(frame({"RIGHT_SHIFT"}), .1);
    check(m.capturing(), "Modifier should wait for chord or release");
    m.update(frame(), .1);
    check(m.bindings()["default"]["jump"][0]["input"] == "key:RIGHT_SHIFT",
          "Standalone modifier failed");
    auto held = frame({}, {}, {pad(0, {.9, 0, 0, 0, -1, -1})});
    m.beginRebind("jump", {{"devices", {"gamepad"}}}, &held);
    m.update(held, .1);
    check(m.capturing(), "Held stick captured");
    m.update(frame({}, {}, {pad(0, {0, 0, 0, 0, -1, -1})}), .1);
    m.update(frame({}, {}, {pad(0, {-.9, 0, 0, 0, -1, -1})}), .1);
    check(m.captureState()["binding"]["direction"] == -1, "Negative half-axis not captured");
    m.update(frame(), .1);
    m.beginRebind("jump", {{"devices", {"wheel"}}});
    m.update(InputFrame::parse({{"scroll", {1, 0}}}), .1);
    check(m.captureState()["binding"]["input"] == "wheel:right", "Wheel not captured");
    m.beginRebind("jump");
    auto before = m.exportProfile();
    rejects([&] { m.beginRebind("jump", {{"slot", 2}}); });
    check(m.capturing() && m.exportProfile() == before, "Rejected capture settings mutated state");
    auto invalid = frame();
    invalid.pads[0].present = true;
    invalid.pads[0].axes[0] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { m.update(invalid, .1); });
    check(m.capturing(), "Invalid frame changed capture");
    rejects([&] { m.beginRebind("jump", J::object(), &invalid); });
    check(m.capturing(), "Invalid baseline changed capture");
    // A captured axis must stay blocked below its old large deadzone, until neutral.
    InputManager axisManager(
        {{"jump", J{{"input", "key:J"}, {"deadzone", .9}}}, {"other", "axis:any:left_x"}});
    axisManager.beginRebind("jump", {{"devices", {"gamepad"}}, {"conflict", "share"}});
    auto moving = frame({}, {}, {pad(0, {.7, 0, 0, 0, -1, -1})});
    axisManager.update(moving, .1);
    axisManager.update(moving, .1);
    check(!axisManager.down("other"), "Captured stick leaked below old binding deadzone");
    axisManager.update(frame(), .1);
    axisManager.update(moving, .1);
    check(axisManager.down("other"), "Neutral axis did not clear capture suppression");
    auto pulse = InputFrame::parse({{"pressed", {"ENTER"}}});
    m.beginRebind("jump", J::object(), &pulse);
    m.update(pulse, .1);
    check(m.capturing(), "Opening keyboard pulse captured");
    m.cancelRebind();
}
} // namespace
int main() {
    try {
        windowInputTests();
        actions();
        profiles();
        capture();
        std::cout << "Native input actions/profiles/capture passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
