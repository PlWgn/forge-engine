#pragma once
// Contract extracted from engine.hpp; licensed core origin.
#include <forge/types.hpp>
namespace forge {struct Config;}
namespace forge {
struct Localization {
    struct Catalog {
        std::string name, pluralLanguage;
        std::map<std::string, Json> messages;
    };
    std::map<std::string, Catalog> catalogs;
    std::string language, defaultLanguage, fallbackLanguage, pendingPreference;
    fs::path preferencePath;
    bool saveSelection = true, warnMissing = true;
    unsigned revision = 0;
    std::set<std::pair<std::string, std::string>> warned;
    void load(const Config &, const std::string &previous = "", bool readPreference = false);
    void select(const std::string &, bool persist = true);
    std::string translate(const std::string &, const Json &params = Json::object());
    bool has(const std::string &, const std::string &locale = "", bool fallback = true) const;
    Json languages() const;
    void flush();
};
struct LocalizedText {
    std::string key;
    Json params = Json::object();
};
}
