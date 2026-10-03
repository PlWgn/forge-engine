#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace forge {
inline float checkedFloat(double value, const std::string& field) {
    if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max())
        throw std::runtime_error(field+" must be finite and within float range");
    return static_cast<float>(value);
}

inline float checkedMass(float mass) {
    if(!std::isfinite(mass) || mass<=0 || 1.0/double(mass)>std::numeric_limits<float>::max())
        throw std::runtime_error("mass must be positive with a finite reciprocal in float range");
    return mass;
}

inline int glyphRasterSize(double fontSize, double scale, double density) {
    if(!std::isfinite(fontSize) || !std::isfinite(scale) || !std::isfinite(density) ||
       fontSize<0 || scale<0 || density<0)
        throw std::runtime_error("Invalid glyph raster dimensions");
    if(fontSize==0 || scale==0 || density==0)return 1;
    // Float inputs are multiplied in double, then bounded BEFORE conversion to int.
    return static_cast<int>(std::clamp(std::ceil(fontSize*scale*density),1.0,1024.0));
}
}
