#include <forge/window.hpp>
#include <forge/input_keys.hpp>
namespace forge {
int keyCode(std::string name) { return inputPlatformKey(inputKey(name)); }
void pollInput(GLFWwindow* window,WindowInput& state) {
    state.previous = state.keys;
    state.previousButtons = state.buttons;
    state.mouseScroll = {0, 0};
    state.characters.clear();
    glfwPollEvents();
    for (int i = GLFW_KEY_SPACE; i <= GLFW_KEY_LAST; ++i)
        state.keys[i] = glfwGetKey(window, i) == GLFW_PRESS;
    for (int i = 0; i < 8; ++i)
        state.buttons[i] = glfwGetMouseButton(window, i) == GLFW_PRESS;
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    auto position = glm::vec2(x, y);
    state.mouseDelta = state.firstMouse ? glm::vec2(0) : position - state.mousePosition;
    state.firstMouse = false;
    state.mousePosition = position;
}
Json windowInput(const WindowInput& state) {
    Json keys = Json::array(), pressed = Json::array(), released = Json::array(), buttons = Json::array(),
         events = Json::array(), pads = Json::array();
    auto appendKey = [&](const std::string& name, bool down, bool before) {
        if (down) keys.push_back(name);
        if (down && !before) {
            pressed.push_back(name);
            events.push_back({{"type", "key"}, {"key", name}, {"action", "press"}});
        }
        if (!down && before) {
            released.push_back(name);
            events.push_back({{"type", "key"}, {"key", name}, {"action", "release"}});
        }
    };
    static const auto names=[] {
        std::array<std::string,GLFW_KEY_LAST+1> result{};
        for (int k=GLFW_KEY_SPACE;k<=GLFW_KEY_LAST;++k) result[k]=inputKeyName(k);
        return result;
    }();
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k)
        if (!names[k].empty()) appendKey(names[k], state.keys[k], state.previous[k]);
    // Preserve generic modifier aliases while also exposing physical left/right keys.
    for (const auto& pair : {std::pair<int,int>{GLFW_KEY_LEFT_SHIFT,GLFW_KEY_RIGHT_SHIFT},
                            {GLFW_KEY_LEFT_CONTROL,GLFW_KEY_RIGHT_CONTROL},
                            {GLFW_KEY_LEFT_ALT,GLFW_KEY_RIGHT_ALT},
                            {GLFW_KEY_LEFT_SUPER,GLFW_KEY_RIGHT_SUPER}}) {
        unsigned bit=inputModifier(pair.first);
        int offset=bit==1?0:bit==2?1:bit==4?2:3;
        appendKey(inputKeyName(inputShift+offset),state.keys[pair.first] || state.keys[pair.second],
                  state.previous[pair.first] || state.previous[pair.second]);
    }
    for (int i = 0; i < 8; ++i) {
        if (state.buttons[i])
            buttons.push_back(i);
        if (state.buttons[i] != state.previousButtons[i])
            events.push_back({{"type", "mouse_button"},
                              {"button", i},
                              {"action", state.buttons[i] ? "press" : "release"}});
    }
    for (auto code : state.characters)
        events.push_back({{"type", "text"}, {"codepoint", code}});
    if (state.mouseScroll != glm::vec2(0))
        events.push_back({{"type", "scroll"}, {"value", {state.mouseScroll.x, state.mouseScroll.y}}});
    for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id) {
        GLFWgamepadstate state{};
        if (glfwGetGamepadState(id, &state)) {
            Json values = Json::array(), axes = Json::array();
            for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b)
                if (state.buttons[b] == GLFW_PRESS)
                    values.push_back(b);
            for (float value : state.axes)
                axes.push_back(value);
            pads.push_back(
                {{"id", id}, {"name", glfwGetGamepadName(id)}, {"buttons", values}, {"axes", axes}});
        }
    }
    return {{"keys", keys},
            {"pressed", pressed},
            {"released", released},
            {"buttons", buttons},
            {"position", {state.mousePosition.x, state.mousePosition.y}},
            {"delta", {state.mouseDelta.x, state.mouseDelta.y}},
            {"scroll", {state.mouseScroll.x, state.mouseScroll.y}},
            {"events", events},
            {"gamepads", pads}};
}
Json windowOptions(GLFWwindow* window,bool vsync) {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    return {{"width", w},
            {"height", h},
            {"fullscreen", glfwGetWindowMonitor(window) != nullptr},
            {"vsync", vsync}};
}
void windowOptions(GLFWwindow* window,bool& vsync,const Json &options) {
    auto current = windowOptions(window,vsync);
    current.merge_patch(options);
    for(auto field:{"width","height"}) {
        auto &value=current.at(field);auto minimum=std::string(field)=="width"?320:240;
        if(!value.is_number_integer() || value<minimum || value>16384)
            throw std::runtime_error("Window dimensions must be integers in 320..16384 by 240..16384");
    }
    for(auto field:{"fullscreen","vsync"})if(!current.at(field).is_boolean())throw std::runtime_error(std::string("Window ")+field+" must be boolean");
    int width = current.at("width"), height = current.at("height");
    bool fullscreen = current.at("fullscreen");
    if (fullscreen) {
        auto monitor = glfwGetPrimaryMonitor();
        auto mode = monitor?glfwGetVideoMode(monitor):nullptr;
        if(!mode)throw std::runtime_error("Primary monitor/video mode unavailable");
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else
        glfwSetWindowMonitor(window, nullptr, 100, 100, width, height, GLFW_DONT_CARE);
    vsync = current.value("vsync", true);
    if (glfwGetCurrentContext()) glfwSwapInterval(vsync ? 1 : 0);
}
}
