#pragma once
#include <string>
namespace forge {
constexpr int inputKeyCount = 516;
constexpr int inputShift = 512, inputCtrl = 513, inputAlt = 514, inputSuper = 515;
// Platform-neutral names; physical modifiers coexist with legacy aliases.
int inputKey(const std::string &name);
std::string inputKeyName(int key);
unsigned inputModifier(int key);
int inputPlatformKey(int key);
} // namespace forge
