#pragma once
// Contract extracted from engine.hpp; licensed core origin.
#include <forge/types.hpp>
namespace forge {
struct Audio {
    struct Impl;
    std::unique_ptr<Impl> impl;
    Audio();
    ~Audio();
    bool silent = false;
    unsigned play(const fs::path &, bool loop, float volume, const std::string &channel = "sfx",
                  float fade = 0, const Json &options = Json::object());
    void stop(unsigned id = 0, const std::string &channel = "", float fade = 0);
    void volume(const std::string &channel, float value);
    float volume(const std::string &channel) const;
    void fade(unsigned id, float target, float seconds, bool stopAfter = false);
    bool playing(unsigned id) const;
    float gain(unsigned id) const;
    std::vector<unsigned> ids(const std::string &) const;
    void pause(unsigned id, bool paused);
    void update(float dt);
    void begin();
    void commit();
    void rollback();
    void configure(const Json &);
    void options(unsigned, const Json &);
    Json info(unsigned) const;
    Json diagnostics() const;
    void listener(glm::vec3 position, glm::vec3 direction);
};
}
