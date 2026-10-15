#pragma once
#ifdef __ANDROID__
#include <android_native_app_glue.h>
#include <EGL/egl.h>
namespace forge {
android_app *androidApp();
EGLConfig androidEglConfig();
}
#endif
