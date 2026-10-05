#include <forge/engine.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif
namespace forge {
Logger logger;
static std::string stamp() {
    auto now = std::chrono::system_clock::now(); auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out; out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S"); return out.str();
}
void Logger::start(const fs::path& root, bool openFile) {
    if(file.is_open()) file.close();
    path = root / "forge.log"; autoOpen = openFile; opened = false;
    fs::create_directories(root);
    file.open(path, std::ios::app);
    if(!file) throw std::runtime_error("Cannot create log: " + path.u8string());
    write("INFO", "Session started");
}
void Logger::write(const std::string& level, const std::string& message) {
    auto line = "[" + stamp() + "] [" + level + "] " + message;
    (protocol || level == "ERROR" ? std::cerr : std::cout) << line << std::endl;
    if(file) { file << line << '\n'; file.flush(); }
}
void Logger::error(const std::string& message) { write("ERROR", message);if(active)try{write("INFO","Crash report: "+crashReport(message).u8string());}catch(const std::exception& error){write("WARN",error.what());} if(autoOpen && !opened) open(); }
void Logger::open() {
    if(opened || path.empty()) return;
    opened = true;write("INFO","Opening log: " + path.u8string());
#ifdef _WIN32
    auto result = reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if(result <= 32) write("WARN", "Unable to open log automatically: " + path.u8string());
#else
    std::string filePath = path.u8string();
#ifdef __APPLE__
    const char* command = "/usr/bin/open";
#else
    const char* command = "/usr/bin/xdg-open";
#endif
    char* args[] = {const_cast<char*>(command), const_cast<char*>(filePath.c_str()), nullptr};
    pid_t child; auto status = posix_spawn(&child, command, nullptr, nullptr, args, environ);
    if(status == 0) { int result = 0; waitpid(child, &result, 0); if(result != 0) write("WARN", "Log opener returned an error"); }
    else write("WARN", "Unable to open log: " + filePath);
#endif
}
}
