#include <forge/input_keys.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
namespace forge {
namespace {
const std::map<std::string, int> &names() {
    static const std::map<std::string, int> values = {{"SPACE", GLFW_KEY_SPACE},
                                                      {"ESCAPE", GLFW_KEY_ESCAPE},
                                                      {"ENTER", GLFW_KEY_ENTER},
                                                      {"TAB", GLFW_KEY_TAB},
                                                      {"BACKSPACE", GLFW_KEY_BACKSPACE},
                                                      {"INSERT", GLFW_KEY_INSERT},
                                                      {"DELETE", GLFW_KEY_DELETE},
                                                      {"PAGEUP", GLFW_KEY_PAGE_UP},
                                                      {"PAGEDOWN", GLFW_KEY_PAGE_DOWN},
                                                      {"HOME", GLFW_KEY_HOME},
                                                      {"END", GLFW_KEY_END},
                                                      {"LEFT", GLFW_KEY_LEFT},
                                                      {"RIGHT", GLFW_KEY_RIGHT},
                                                      {"UP", GLFW_KEY_UP},
                                                      {"DOWN", GLFW_KEY_DOWN},
                                                      {"CAPS_LOCK", GLFW_KEY_CAPS_LOCK},
                                                      {"SCROLL_LOCK", GLFW_KEY_SCROLL_LOCK},
                                                      {"NUM_LOCK", GLFW_KEY_NUM_LOCK},
                                                      {"PRINT_SCREEN", GLFW_KEY_PRINT_SCREEN},
                                                      {"PAUSE", GLFW_KEY_PAUSE},
                                                      {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT},
                                                      {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT},
                                                      {"LEFT_CTRL", GLFW_KEY_LEFT_CONTROL},
                                                      {"RIGHT_CTRL", GLFW_KEY_RIGHT_CONTROL},
                                                      {"LEFT_ALT", GLFW_KEY_LEFT_ALT},
                                                      {"RIGHT_ALT", GLFW_KEY_RIGHT_ALT},
                                                      {"LEFT_SUPER", GLFW_KEY_LEFT_SUPER},
                                                      {"RIGHT_SUPER", GLFW_KEY_RIGHT_SUPER},
                                                      {"MENU", GLFW_KEY_MENU},
                                                      {"WORLD_1", GLFW_KEY_WORLD_1},
                                                      {"WORLD_2", GLFW_KEY_WORLD_2},
                                                      {"KP_DECIMAL", GLFW_KEY_KP_DECIMAL},
                                                      {"KP_DIVIDE", GLFW_KEY_KP_DIVIDE},
                                                      {"KP_MULTIPLY", GLFW_KEY_KP_MULTIPLY},
                                                      {"KP_SUBTRACT", GLFW_KEY_KP_SUBTRACT},
                                                      {"KP_ADD", GLFW_KEY_KP_ADD},
                                                      {"KP_ENTER", GLFW_KEY_KP_ENTER},
                                                      {"KP_EQUAL", GLFW_KEY_KP_EQUAL},
                                                      {"SHIFT", inputShift},
                                                      {"CTRL", inputCtrl},
                                                      {"ALT", inputAlt},
                                                      {"SUPER", inputSuper}};
    return values;
}
} // namespace
int inputKey(const std::string &source) {
    std::string name = source;
    for (auto &c : name)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    if (name == "RETURN")
        name = "ENTER";
    if (name == "ESC")
        name = "ESCAPE";
    if (name == "CONTROL")
        name = "CTRL";
    if (name == "CMD" || name == "COMMAND" || name == "WIN")
        name = "SUPER";
    if (name.size() == 1 &&
        ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9') ||
         std::string("-=[]\\;'`,./").find(name[0]) != std::string::npos))
        return name[0];
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F') {
        int n = 0;
        for (size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9')
                throw std::invalid_argument("Unknown key: " + source);
            n = n * 10 + name[i] - '0';
        }
        if (n >= 1 && n <= 25)
            return GLFW_KEY_F1 + n - 1;
    }
    if (name.size() == 4 && name.substr(0, 3) == "KP_" && name[3] >= '0' && name[3] <= '9')
        return GLFW_KEY_KP_0 + name[3] - '0';
    auto found = names().find(name);
    if (found != names().end())
        return found->second;
    throw std::invalid_argument("Unknown key: " + source);
}
std::string inputKeyName(int key) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
        (key > 0 && key < 128 && std::string("-=[]\\;'`,./").find(char(key)) != std::string::npos))
        return std::string(1, char(key));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
        return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
        return "KP_" + std::to_string(key - GLFW_KEY_KP_0);
    for (const auto &entry : names())
        if (entry.second == key)
            return entry.first;
    return {};
}
unsigned inputModifier(int key) {
    if (key == inputShift || key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
        return 1;
    if (key == inputCtrl || key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
        return 2;
    if (key == inputAlt || key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
        return 4;
    if (key == inputSuper || key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER)
        return 8;
    return 0;
}
int inputPlatformKey(int key) {
    if (key == inputShift)
        return GLFW_KEY_LEFT_SHIFT;
    if (key == inputCtrl)
        return GLFW_KEY_LEFT_CONTROL;
    if (key == inputAlt)
        return GLFW_KEY_LEFT_ALT;
    if (key == inputSuper)
        return GLFW_KEY_LEFT_SUPER;
    return key;
}
} // namespace forge
