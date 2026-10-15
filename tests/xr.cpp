#include <forge/xr.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge;
static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F call) { try { call(); } catch (const std::exception &) { return; } throw std::runtime_error("Invalid XR value accepted"); }
int main() {
    try {
        auto defaults = xrSettings(Json::object()); require(!defaults.enabled && defaults.space == "local" && defaults.metersPerUnit == 1, "XR defaults changed");
        for (auto value : {Json(), Json(1), Json::array()}) rejects([&] { xrSettings(value); });
        for (auto value : {Json(1), Json("true"), Json()}) rejects([&] { xrSettings({{"enabled", value}}); });
        for (auto field : {"near", "far", "meters_per_unit", "eye_budget_bytes"})
            for (auto value : {Json(true), Json(-1), Json(0), Json("1"), Json(1e100), Json(std::numeric_limits<double>::infinity())})
                rejects([&] { xrSettings({{field, value}}); });
        rejects([] { xrSettings({{"near", 1}, {"far", 1}}); });
        rejects([] { xrSettings({{"reference_space", "automatic"}}); });
        auto settings = xrSettings({{"enabled", true}, {"reference_space", "stage"}, {"custom", {{"game", 7}}}});
        require(settings.definition["custom"]["game"] == 7 && settings.space == "stage", "Unknown metadata lost");
        const double l = -.7, r = .9, d = -.8, u = .6, n = .1, f = 500;
        auto m = xrProjection(l, r, d, u, n, f);
        auto ndc = [&](glm::dvec4 p) { auto clip = m * p; return glm::dvec3(clip) / clip.w; };
        auto close = [](double a, double b) { return std::abs(a - b) < 1e-9; };
        require(close(ndc({std::tan(l)*n, 0, -n, 1}).x, -1), "Left FOV is not -1");
        require(close(ndc({std::tan(r)*n, 0, -n, 1}).x, 1), "Right FOV is not +1");
        require(close(ndc({0, std::tan(d)*n, -n, 1}).y, -1), "Bottom FOV is not -1");
        require(close(ndc({0, std::tan(u)*n, -n, 1}).y, 1), "Top FOV is not +1");
        require(close(ndc({0, 0, -n, 1}).z, -1) && close(ndc({0, 0, -f, 1}).z, 1), "Clip depth convention changed");
        rejects([&] { xrProjection(r, l, d, u, n, f); });
        rejects([&] { xrProjection(l, r, d, u, 0, f); });
        rejects([&] { xrProjection(l, r, d, u, f, n); });
        rejects([&] { xrProjection(-2, r, d, u, n, f); });
        rejects([&] { xrProjection(l, r, d, std::numeric_limits<double>::quiet_NaN(), n, f); });
        std::cout << "XR projection/configuration contracts passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
