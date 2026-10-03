// Add "modules/native_example.cpp" to native_modules and recompile the engine.
#include <forge/engine.hpp>
FORGE_MODULE(extra_math) {
    module.def("lerp", [](float a,float b,float t){return a+(b-a)*t;});
}
