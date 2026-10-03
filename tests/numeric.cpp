#include <forge/numeric.hpp>
#include <iostream>

static void require(bool condition) {
    if(!condition)throw std::runtime_error("Numeric regression failed");
}
template<class F> static void rejected(F call) {
    bool failed=false;
    try { call(); } catch(const std::runtime_error&) { failed=true; }
    require(failed);
}
int main() {
    try {
        auto max=std::numeric_limits<float>::max();
        require(forge::glyphRasterSize(24,1,2)==48);
        require(forge::glyphRasterSize(24.1,1,1)==25);
        require(forge::glyphRasterSize(24,0,2)==1);
        require(forge::glyphRasterSize(max,max,max)==1024);
        require(forge::glyphRasterSize(1e10,1,1)==1024); // Beyond int range.
        require(forge::glyphRasterSize(1024,1,1)==1024);
        rejected([] { forge::glyphRasterSize(NAN,1,1); });
        rejected([] { forge::checkedMass(1e-40f); });
        rejected([] { forge::checkedMass(0); });
        rejected([] { forge::checkedMass(INFINITY); });
        require(std::isfinite(1.0f/forge::checkedMass(1e-38f)));
        require(forge::checkedMass(max)==max);
        rejected([&] { forge::checkedFloat(double(max)*2,"position"); });
        rejected([] { forge::checkedFloat(NAN,"position"); });
        require(forge::checkedFloat(max,"position")==max);
        std::cout << "Numeric regressions passed\n";
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';return 1;
    }
}
