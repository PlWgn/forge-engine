#pragma once
#include <vector>
namespace forge {
struct ImageData {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};
}
