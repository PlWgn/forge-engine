#pragma once
#include <forge/types.hpp>
namespace forge {
struct Config;
Json validateMaterial(const Config &, Json);
std::vector<std::string> materialTextures(const Json &);
} // namespace forge
