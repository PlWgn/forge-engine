#pragma once
#include <forge/types.hpp>
#include <memory>
#include <vector>
#include <stb_truetype.h>
namespace forge {
struct Config;
struct FontFace {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo font{};
    std::string name;
};
std::vector<int> unicode(const std::string&);
std::vector<std::shared_ptr<FontFace>> fonts(const Config&);
int fontFor(const std::vector<std::shared_ptr<FontFace>>&,int);
}
