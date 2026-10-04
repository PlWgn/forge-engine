#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <json.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <set>
namespace forge {
namespace fs = std::filesystem;
using Json = nlohmann::json;
float finiteNumber(const Json &, const std::string &field);
}
