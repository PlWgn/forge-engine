#include <forge/window.hpp>
#include <cctype>
namespace forge {
int keyCode(std::string name) {
    for (auto &c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (name.size() == 1)
        return name[0];
    static const std::map<std::string, int> keys = {
        {"PAGEUP", GLFW_KEY_PAGE_UP},      {"PAGEDOWN", GLFW_KEY_PAGE_DOWN},
        {"SPACE", GLFW_KEY_SPACE},         {"ESCAPE", GLFW_KEY_ESCAPE},
        {"ENTER", GLFW_KEY_ENTER},         {"TAB", GLFW_KEY_TAB},
        {"BACKSPACE", GLFW_KEY_BACKSPACE}, {"LEFT", GLFW_KEY_LEFT},
        {"RIGHT", GLFW_KEY_RIGHT},         {"UP", GLFW_KEY_UP},
        {"DOWN", GLFW_KEY_DOWN},           {"SHIFT", GLFW_KEY_LEFT_SHIFT},
        {"CTRL", GLFW_KEY_LEFT_CONTROL},   {"ALT", GLFW_KEY_LEFT_ALT}};
    if (name.size() >= 2 && name[0] == 'F') {
        int n = std::stoi(name.substr(1));
        if (n >= 1 && n <= 25)
            return GLFW_KEY_F1 + n - 1;
    }
    auto it = keys.find(name);
    if (it == keys.end())
        throw std::runtime_error("Unknown key: " + name);
    return it->second;
}
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
    std::map<int, std::string> names = {{GLFW_KEY_ESCAPE, "ESCAPE"},
                                        {GLFW_KEY_ENTER, "ENTER"},
                                        {GLFW_KEY_TAB, "TAB"},
                                        {GLFW_KEY_BACKSPACE, "BACKSPACE"},
                                        {GLFW_KEY_INSERT, "INSERT"},
                                        {GLFW_KEY_DELETE, "DELETE"},
                                        {GLFW_KEY_PAGE_UP, "PAGEUP"},
                                        {GLFW_KEY_PAGE_DOWN, "PAGEDOWN"},
                                        {GLFW_KEY_HOME, "HOME"},
                                        {GLFW_KEY_END, "END"},
                                        {GLFW_KEY_LEFT, "LEFT"},
                                        {GLFW_KEY_RIGHT, "RIGHT"},
                                        {GLFW_KEY_UP, "UP"},
                                        {GLFW_KEY_DOWN, "DOWN"},
                                        {GLFW_KEY_LEFT_SHIFT, "SHIFT"},
                                        {GLFW_KEY_LEFT_CONTROL, "CTRL"},
                                        {GLFW_KEY_LEFT_ALT, "ALT"},
                                        {GLFW_KEY_LEFT_SUPER, "SUPER"},
                                        {GLFW_KEY_SPACE, "SPACE"}};
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k) {
        std::string name;
        if (names.count(k))
            name = names.at(k);
        else if (k >= GLFW_KEY_F1 && k <= GLFW_KEY_F25)
            name = "F" + std::to_string(k - GLFW_KEY_F1 + 1);
        else if (k >= 32 && k <= 96)
            name = std::string(1, char(k));
        else
            continue;
        bool down = state.keys[k], before = state.previous[k];
        if (k == GLFW_KEY_LEFT_SHIFT) {
            down |= state.keys[GLFW_KEY_RIGHT_SHIFT];
            before |= state.previous[GLFW_KEY_RIGHT_SHIFT];
        }
        if (k == GLFW_KEY_LEFT_CONTROL) {
            down |= state.keys[GLFW_KEY_RIGHT_CONTROL];
            before |= state.previous[GLFW_KEY_RIGHT_CONTROL];
        }
        if (k == GLFW_KEY_LEFT_ALT) {
            down |= state.keys[GLFW_KEY_RIGHT_ALT];
            before |= state.previous[GLFW_KEY_RIGHT_ALT];
        }
        if (down)
            keys.push_back(name);
        if (down && !before) {
            pressed.push_back(name);
            events.push_back({{"type", "key"}, {"key", name}, {"action", "press"}});
        }
        if (!down && before) {
            released.push_back(name);
            events.push_back({{"type", "key"}, {"key", name}, {"action", "release"}});
        }
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
    glfwSwapInterval(vsync ? 1 : 0);
}
}
