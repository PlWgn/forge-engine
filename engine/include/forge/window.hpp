#pragma once
#include <forge/types.hpp>
#include <forge/gl.hpp>
#include <array>
namespace forge {
struct WindowInput {
    bool firstMouse=true;
    std::array<bool,GLFW_KEY_LAST+1> keys{},previous{};
    std::array<bool,8> buttons{},previousButtons{};
    glm::vec2 mousePosition{0},mouseDelta{0},mouseScroll{0};
    std::vector<unsigned> characters;
};
int keyCode(std::string);
void pollInput(GLFWwindow*,WindowInput&);
Json windowInput(const WindowInput&);
Json windowOptions(GLFWwindow*,bool vsync);
void windowOptions(GLFWwindow*,bool& vsync,const Json&);
}
