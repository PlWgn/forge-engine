// Adaptable Android lifecycle host for the same runtime and Python scenes.
// Interpreter bootstrap follows main.cpp and retains its Core origin.
#include <forge/engine.hpp>
#include <forge/android_host.hpp>
#include <android/log.h>
namespace forge { void setAndroidApp(android_app *); }
extern "C" void android_main(android_app *application) {
    using namespace forge;
    setAndroidApp(application);
    JNIEnv *environment = nullptr;
    bool attached = application->activity->vm->GetEnv(reinterpret_cast<void **>(&environment), JNI_VERSION_1_6) == JNI_EDETACHED;
    if (attached && application->activity->vm->AttachCurrentThread(&environment, nullptr) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "Forge", "Cannot attach runtime thread to Android VM");
        ANativeActivity_finish(application->activity); return;
    }
    try {
        fs::path root = fs::path(application->activity->internalDataPath) / "forge-content";
        auto config = Config::load(root / "game.json");
        config.data["storage"]["mode"] = "user";
        logger.start(userPath(config, "logs"), false);
        config.validate(false);
        PyPreConfig pre; PyPreConfig_InitIsolatedConfig(&pre); pre.utf8_mode = 1;
        auto status = Py_PreInitialize(&pre);
        if (PyStatus_Exception(status)) throw std::runtime_error("Cannot preinitialize Android Python");
        PyConfig python; PyConfig_InitIsolatedConfig(&python); python.write_bytecode = 0; python.install_signal_handlers = 0;
        auto home = (root / "runtime").u8string();
        status = PyConfig_SetBytesString(&python, &python.home, home.c_str());
        if (PyStatus_Exception(status)) { PyConfig_Clear(&python); throw std::runtime_error("Cannot set Android Python home"); }
        py::scoped_interpreter interpreter(&python, 0, nullptr, false);
        // Destroy Python exceptions before the interpreter shuts down.
        try { Runtime runtime(std::move(config), false, false); runtime.run(-1); }
        catch (const std::exception &error) { logger.error(error.what()); }
    } catch (const std::exception &error) {
        __android_log_print(ANDROID_LOG_ERROR, "Forge", "%s", error.what());
    }
    if (attached) application->activity->vm->DetachCurrentThread();
    ANativeActivity_finish(application->activity);
    while (!application->destroyRequested) {
        int events; android_poll_source *source;
        if (ALooper_pollOnce(50, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0 && source)
            source->process(application, source);
    }
}
