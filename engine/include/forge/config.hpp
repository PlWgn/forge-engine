#pragma once
// Contract extracted from engine.hpp; licensed core origin.
#include <forge/types.hpp>
namespace pybind11 {class module_;}
namespace forge {
struct Config {
    fs::path root, file;
    Json data;
    std::map<std::string, fs::path> paths;
    static Config load(const fs::path &file);
    fs::path resolve(const std::string &path) const;
    fs::path asset(const std::string &group, const std::string &name) const;
    std::string entry() const;
    void validate(bool media = true) const;
};
Json validateEntity(const Config &, Json);
Json readJson(const fs::path &file);
void validateMedia(const Config &config);
fs::path userPath(const Config &, const std::string &kind, const std::string &file = "");
fs::path storagePath(const Config &, const std::string &relative);
void installCrashHandler(const fs::path &directory);
fs::path crashReport(const std::string &message);
Json validateRenderSettings(Json);
void bindFeatures(pybind11::module_ &);
}
