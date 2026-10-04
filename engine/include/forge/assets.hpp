#pragma once
// Contract extracted from engine.hpp; licensed core origin.
#include <forge/types.hpp>
#include <forge/image.hpp>
namespace forge {struct Config;struct Model;}
namespace forge {
struct Assets {
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit Assets(Config &);
    ~Assets();
    unsigned request(const std::string &group, const std::string &name);
    Json info(unsigned);
    std::shared_ptr<ImageData> image(const fs::path &);
    std::shared_ptr<Model> model(const fs::path &);
    std::vector<unsigned char> bytes(unsigned);
    void release(unsigned);
    void budget(size_t);
    Json stats();
    unsigned checkpoint();
    void rollback(unsigned);
};
}
