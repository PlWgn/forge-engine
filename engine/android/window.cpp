// Adaptable GLFW-shaped platform boundary. Android owns lifecycle; XR owns presentation.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <forge/android_host.hpp>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <stdexcept>
struct GLFWwindow { void *user = nullptr; int width = 1280, height = 720; bool close = false; };
namespace {
android_app *application = nullptr;
EGLDisplay display = EGL_NO_DISPLAY;
EGLContext context = EGL_NO_CONTEXT;
EGLSurface surface = EGL_NO_SURFACE;
EGLConfig config = nullptr;
GLFWwindow *window = nullptr;
GLFWerrorfun errorCallback = nullptr;
}
namespace forge {
void setAndroidApp(android_app *app) { application = app; }
android_app *androidApp() { return application; }
EGLConfig androidEglConfig() { return config; }
}
extern "C" {
int glfwInit() { return application ? GLFW_TRUE : GLFW_FALSE; }
void glfwDefaultWindowHints() {}
void glfwWindowHint(int, int) {}
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun fn) { auto old = errorCallback; errorCallback = fn; return old; }
GLFWwindow *glfwCreateWindow(int width, int height, const char *, GLFWmonitor *, GLFWwindow *) {
    if (window) return nullptr;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) return nullptr;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || !count) return nullptr;
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context)) return nullptr;
    window = new GLFWwindow; window->width = width; window->height = height; return window;
}
void glfwMakeContextCurrent(GLFWwindow *) { eglMakeCurrent(display, surface, surface, context); }
GLFWwindow *glfwGetCurrentContext() { return context == EGL_NO_CONTEXT ? nullptr : window; }
GLFWglproc glfwGetProcAddress(const char *name) {
    auto result = eglGetProcAddress(name);
    if (!result) { static void *gles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL); result = reinterpret_cast<__eglMustCastToProperFunctionPointerType>(dlsym(gles, name)); }
    return reinterpret_cast<GLFWglproc>(result);
}
void glfwDestroyWindow(GLFWwindow *value) { delete value; window = nullptr; }
void glfwTerminate() {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY; context = EGL_NO_CONTEXT; surface = EGL_NO_SURFACE; config = nullptr;
}
void glfwPollEvents() {
    int events; android_poll_source *source;
    while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
        if (source) source->process(application, source);
        if (application->destroyRequested) break;
    }
    if (window && application->destroyRequested) window->close = true;
}
int glfwWindowShouldClose(GLFWwindow *value) { return value->close; }
void glfwSetWindowShouldClose(GLFWwindow *value, int flag) { value->close = flag; }
void glfwGetFramebufferSize(GLFWwindow *value, int *width, int *height) { *width = value->width; *height = value->height; }
void glfwGetWindowSize(GLFWwindow *value, int *width, int *height) { *width = value->width; *height = value->height; }
void glfwSetWindowUserPointer(GLFWwindow *value, void *pointer) { value->user = pointer; }
void *glfwGetWindowUserPointer(GLFWwindow *value) { return value->user; }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *, GLFWscrollfun) { return nullptr; }
GLFWcharfun glfwSetCharCallback(GLFWwindow *, GLFWcharfun) { return nullptr; }
int glfwGetKey(GLFWwindow *, int) { return GLFW_RELEASE; }
int glfwGetMouseButton(GLFWwindow *, int) { return GLFW_RELEASE; }
void glfwGetCursorPos(GLFWwindow *, double *x, double *y) { *x = *y = 0; }
int glfwGetGamepadState(int, GLFWgamepadstate *) { return GLFW_FALSE; }
const char *glfwGetGamepadName(int) { return nullptr; }
void glfwSetInputMode(GLFWwindow *, int, int) {}
void glfwSwapInterval(int) {}
void glfwSwapBuffers(GLFWwindow *) {} // headset compositor submits via xrEndFrame
void glfwSetWindowIcon(GLFWwindow *, int, const GLFWimage *) {}
GLFWmonitor *glfwGetPrimaryMonitor() { return nullptr; }
GLFWmonitor *glfwGetWindowMonitor(GLFWwindow *) { return nullptr; }
const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *) { return nullptr; }
void glfwSetWindowMonitor(GLFWwindow *value, GLFWmonitor *, int, int, int width, int height, int) { value->width = width; value->height = height; }
}
