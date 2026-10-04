#include <cctype>
#include <chrono>
#include <cstdlib>
#include <forge/engine.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif
namespace forge {
static std::string env(const char *name) {
    auto value = std::getenv(name);
    return value ? value : "";
}
static fs::path confined(const fs::path &root, const std::string &name) {
    auto input = fs::u8path(name);
    if (input.is_absolute())
        throw std::runtime_error("Storage path must be relative");
    auto result = fs::weakly_canonical(root / input),
         relative = result.lexically_relative(fs::weakly_canonical(root));
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("Path escapes user directory");
    return result;
}
fs::path userPath(const Config &c, const std::string &kind, const std::string &file) {
    if (kind != "data" && kind != "saves" && kind != "config" && kind != "cache" && kind != "logs" && kind != "captures")
        throw std::runtime_error("Unknown user directory kind");
    auto id = c.data.value("storage", Json::object()).value("application_id", std::string("org.forge.game"));
    if (id.empty() || id.size() > 120 || id == "." || id == "..")
        throw std::runtime_error("Invalid storage.application_id");
    for (unsigned char ch : id)
        if (!std::isalnum(ch) && ch != '.' && ch != '_' && ch != '-')
            throw std::runtime_error(
                "application_id must contain ASCII letters, digits, dots, underscores or hyphens");
    fs::path base;
    auto overrideRoot = env("FORGE_USER_ROOT");
    if (!overrideRoot.empty())
        base = fs::u8path(overrideRoot) / id;
    else {
#ifdef _WIN32
        auto raw = env("LOCALAPPDATA");
        if (raw.empty())
            raw = env("APPDATA");
        if (raw.empty())
            throw std::runtime_error("Application data directory unavailable");
        base = fs::u8path(raw) / id;
#elif defined(__APPLE__)
        auto home = env("HOME");
        if (home.empty())
            throw std::runtime_error("HOME unavailable");
        base = fs::u8path(home) / "Library" /
               (kind == "cache"  ? "Caches"
                : kind == "logs" ? "Logs"
                                 : "Application Support") /
               id;
#else
        auto home = env("HOME");
        auto raw = env(kind == "cache" ? "XDG_CACHE_HOME" : "XDG_DATA_HOME");
        base = (raw.empty() ? fs::u8path(home) / (kind == "cache" ? ".cache" : ".local/share")
                            : fs::u8path(raw)) /
               id;
#endif
    }
    if (kind != "data")
        base /= kind;
    return confined(base, file);
}
fs::path storagePath(const Config &c, const std::string &relative) {
    auto mode = c.data.value("storage", Json::object()).value("mode", std::string("project"));
    if (mode == "project")
        return c.resolve(relative);
    if (mode != "user")
        throw std::runtime_error("storage.mode must be project or user");
    return confined(userPath(c, "data"), relative);
}
fs::path crashReport(const std::string &message) {
    auto root = logger.path.parent_path() / "crash-reports";
    fs::create_directories(root);
    static unsigned index = 0;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto path = root / (std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()) +
                        "-" + std::to_string(++index) + ".json");
    Json report = {{"format", "forge.crash/1"},
                   {"engine_version", FORGE_VERSION},
                   {"message", message},
                   {"log", logger.path.u8string()}};
    if (active) {
        report["scene"] = active->currentScene;
        report["time"] = active->time;
        report["entities"] = active->world.entities.size();
        report["profile"] = active->profile;
    }
    std::ofstream stream(path);
    stream << report.dump(2);
    if (!stream)
        throw std::runtime_error("Cannot write crash report");
    return path;
}
#ifdef _WIN32
static HANDLE crashFile = INVALID_HANDLE_VALUE;
static LONG WINAPI nativeCrash(EXCEPTION_POINTERS *) {
    const char text[] = "Forge native exception. See Windows Error Reporting for a stack dump.\r\n";
    DWORD count;
    WriteFile(crashFile, text, sizeof(text) - 1, &count, nullptr);
    FlushFileBuffers(crashFile);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static int crashFile = -1;
static char signalStack[65536];
static void nativeCrash(int sig) {
    const char text[] = "Forge fatal native signal. See OS crash report for the stack dump.\n";
    if (crashFile >= 0) {
        auto written = write(crashFile, text, sizeof(text) - 1);
        (void)written;
    }
    // Preserve the OS's normal crash-report/core-dump path after our minimal safe write.
    signal(sig, SIG_DFL);
    if (kill(getpid(), sig) != 0)
        _exit(128 + sig);
}
#endif
void installCrashHandler(const fs::path &directory) {
    fs::create_directories(directory);
    auto file = directory / "native-last.txt";
#ifdef _WIN32
    crashFile = CreateFileW(file.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    SetUnhandledExceptionFilter(nativeCrash);
#else
    crashFile = open(file.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
    stack_t stack{};
    stack.ss_sp = signalStack;
    stack.ss_size = sizeof(signalStack);
    sigaltstack(&stack, nullptr);
    struct sigaction action{};
    action.sa_handler = nativeCrash;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL})
        sigaction(signal, &action, nullptr);
#endif
}
} // namespace forge
