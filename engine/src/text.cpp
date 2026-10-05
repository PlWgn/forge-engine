#define STB_TRUETYPE_IMPLEMENTATION
#include <forge/engine.hpp>
#include <forge/text.hpp>
#include <sstream>
namespace forge {
static std::string textFile(const fs::path &p) {
    std::ifstream s(p, std::ios::binary);
    if (!s)
        throw std::runtime_error("Cannot read: " + p.u8string());
    return {std::istreambuf_iterator<char>(s), {}};
}
std::vector<int> unicode(const std::string &s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i++];
        int code = c, count = 0;
        if (c >= 0xf0) {
            code = c & 7;
            count = 3;
        } else if (c >= 0xe0) {
            code = c & 15;
            count = 2;
        } else if (c >= 0xc0) {
            code = c & 31;
            count = 1;
        }
        for (int k = 0; k < count && i < s.size(); ++k)
            code = (code << 6) | (static_cast<unsigned char>(s[i++]) & 63);
        out.push_back(code);
    }
    return out;
}
std::vector<std::shared_ptr<FontFace>> fonts(const Config &config) {
    static std::map<std::string, std::shared_ptr<FontFace>> cache;
    auto options = config.data.value("renderer", Json::object());
    Json names = Json::array({options.value("font", std::string("font.ttf"))});
    for (auto &name : options.value("fallback_fonts", Json::array()))
        names.push_back(name);
    std::vector<std::shared_ptr<FontFace>> result;
    for (auto &name : names) {
        auto path = config.asset("graphics", name);
        auto key =
            path.u8string() +
            std::to_string(static_cast<long long>(fs::last_write_time(path).time_since_epoch().count()));
        auto found = cache.find(key);
        if (found == cache.end()) {
            auto face = std::make_shared<FontFace>();
            auto data = textFile(path);
            face->bytes.assign(data.begin(), data.end());
            face->name = path.u8string();
            if (!stbtt_InitFont(&face->font, face->bytes.data(),
                                stbtt_GetFontOffsetForIndex(face->bytes.data(), 0)))
                throw std::runtime_error("Invalid TrueType font: " + path.u8string());
            if (cache.size() >= 32)
                cache.erase(cache.begin());
            found = cache.emplace(key, face).first;
        }
        result.push_back(found->second);
    }
    return result;
}
int fontFor(const std::vector<std::shared_ptr<FontFace>> &faces, int code) {
    for (size_t i = 0; i < faces.size(); ++i)
        if (stbtt_FindGlyphIndex(&faces[i]->font, code))
            return int(i);
    static std::set<std::pair<std::string, int>> warned;
    if (warned.insert({faces.front()->name, code}).second) {
        std::ostringstream number;
        number << std::hex << std::uppercase << code;
        logger.write("WARN", "Missing glyph U+" + number.str() + " in configured fonts");
    }
    return 0;
}
std::array<float, 3> measureText(const Config &config, const std::string &value, float size) {
    if (!std::isfinite(size) || size <= 0)
        throw std::runtime_error("Text size must be positive and finite");
    auto faces = fonts(config);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&faces.front()->font, &ascent, &descent, &gap);
    double lineHeight = double(ascent - descent + gap) * stbtt_ScaleForPixelHeight(&faces.front()->font, size),
           x = 0, width = 0;
    int previous = 0, lastFace = -1, lines = 1;
    for (auto code : unicode(value)) {
        if (code == '\n') {
            width = std::max(width, x);
            x = 0;
            previous = 0;
            lastFace = -1;
            ++lines;
            continue;
        }
        int face = fontFor(faces, code), advance, bearing;
        auto &font = faces[face]->font;
        double scale = stbtt_ScaleForPixelHeight(&font, size);
        stbtt_GetCodepointHMetrics(&font, code, &advance, &bearing);
        if (previous && lastFace == face)
            x += stbtt_GetCodepointKernAdvance(&font, previous, code) * scale;
        x += advance * scale;
        previous = code;
        lastFace = face;
    }
    return {checkedFloat(std::max(width, x),"Text width"),checkedFloat(lines * lineHeight,"Text height"),checkedFloat(lineHeight,"Text line height")};
}
}
