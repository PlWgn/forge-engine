// Snapshot/key registry checks need no window or graphics/audio device.
#include <algorithm>
#include <forge/input_manager.hpp>
#include <forge/window.hpp>
#include <stdexcept>
void windowInputTests() {
    forge::WindowInput input;
    for (int key : {GLFW_KEY_HOME, GLFW_KEY_KP_9, GLFW_KEY_RIGHT_SUPER, GLFW_KEY_RIGHT_CONTROL})
        input.keys[key] = true;
    auto frame = forge::windowInput(input);
    for (const char *name : {"HOME", "KP_9", "RIGHT_SUPER", "SUPER", "RIGHT_CTRL", "CTRL"})
        if (std::find(frame["keys"].begin(), frame["keys"].end(), name) == frame["keys"].end() ||
            std::find(frame["pressed"].begin(), frame["pressed"].end(), name) ==
                frame["pressed"].end())
            throw std::runtime_error("Missing physical key or generic modifier alias");
    input.previous = input.keys;
    input.keys.fill(false);
    frame = forge::windowInput(input);
    if (!frame["keys"].empty() || frame["released"].size() != 6)
        throw std::runtime_error("Physical key/alias releases lost");
    if (forge::keyCode("DELETE") != GLFW_KEY_DELETE ||
        forge::keyCode("KP_ENTER") != GLFW_KEY_KP_ENTER)
        throw std::runtime_error("Extended key lookup failed");
}
