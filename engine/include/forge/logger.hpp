#pragma once
// Contract extracted from engine.hpp; licensed core origin.
#include <forge/types.hpp>
#include <fstream>
namespace forge {
struct Logger {
    fs::path path;
    std::ofstream file;
    bool autoOpen = true, opened = false;
    void start(const fs::path &root, bool open);
    void write(const std::string &level, const std::string &message);
    void error(const std::string &message);
    void open();
};
extern Logger logger;
}
