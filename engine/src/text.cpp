#define STB_TRUETYPE_IMPLEMENTATION
#include <forge/engine.hpp>
#include <forge/text.hpp>
#include <limits>
#include <sstream>
namespace forge {
static std::string textFile(const fs::path &p) {
    std::ifstream s(p, std::ios::binary);
    if (!s)
        throw std::runtime_error("Cannot read: " + p.u8string());
    return {std::istreambuf_iterator<char>(s), {}};
}
static int fontOffset(const std::vector<unsigned char> &data) {
    // stb_truetype takes a pointer without a length. Check the collection/header
    // and table ranges before letting it inspect an empty or truncated file.
    auto invalid = []() {
        throw std::runtime_error("Invalid TrueType font header or table range");
    };
    auto contains = [&](size_t offset, size_t length) {
        return offset <= data.size() && length <= data.size() - offset;
    };
    auto u16 = [&](size_t offset) -> unsigned {
        if (!contains(offset, 2))
            invalid();
        return (unsigned(data[offset]) << 8) | data[offset + 1];
    };
    auto u32 = [&](size_t offset) -> uint32_t {
        if (!contains(offset, 4))
            invalid();
        return (uint32_t(u16(offset)) << 16) | u16(offset + 2);
    };
    size_t offset = 0;
    if (u32(0) == 0x74746366u) {
        auto count = u32(8);
        if (!count || !contains(12, size_t(count) * 4))
            invalid();
        offset = u32(12);
    }
    if (!contains(offset, 12) || offset > size_t(std::numeric_limits<int>::max()))
        invalid();
    auto signature = u32(offset);
    if (signature != 0x00010000u && signature != 0x4f54544fu && signature != 0x74727565u &&
        signature != 0x74797031u)
        invalid();
    auto count = u16(offset + 4);
    if (!count || !contains(offset + 12, size_t(count) * 16))
        invalid();
    for (size_t i = 0; i < count; ++i) {
        auto record = offset + 12 + i * 16;
        auto tag = u32(record), start = u32(record + 8), length = u32(record + 12);
        if (!contains(start, length) || start > uint32_t(std::numeric_limits<int>::max()))
            invalid();
        size_t minimum = tag == 0x68656164u                                               ? 54
                         : tag == 0x68686561u                                             ? 36
                         : tag == 0x6d617870u                                             ? 6
                         : tag == 0x686d7478u || tag == 0x636d6170u || tag == 0x43464620u ? 4
                                                                                          : 0;
        if (length < minimum)
            invalid();
        if (tag == 0x636d6170u) {
            auto records = u16(size_t(start) + 2);
            if (size_t(records) * 8 > length - 4)
                invalid();
            for (size_t k = 0; k < records; ++k)
                if (u32(size_t(start) + 4 + k * 8 + 4) > length - 2)
                    invalid();
        }
    }
    return int(offset);
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
        auto key = path.u8string() + "#" +
                   std::to_string(static_cast<long long>(
                       fs::last_write_time(path).time_since_epoch().count())) +
                   "#" + std::to_string(fs::file_size(path));
        auto found = cache.find(key);
        if (found == cache.end()) {
            auto face = std::make_shared<FontFace>();
            auto data = textFile(path);
            face->bytes.assign(data.begin(), data.end());
            face->name = path.u8string();
            int offset;
            try {
                offset = fontOffset(face->bytes);
            } catch (const std::exception &error) {
                throw std::runtime_error("Invalid TrueType font: " + path.u8string() + ": " +
                                         error.what());
            }
            if (!stbtt_InitFont(&face->font, face->bytes.data(), offset))
                throw std::runtime_error("Invalid TrueType font: " + path.u8string());
            int ascent, descent, gap;
            stbtt_GetFontVMetrics(&face->font, &ascent, &descent, &gap);
            if (ascent <= descent)
                throw std::runtime_error("Invalid TrueType font vertical metrics: " +
                                         path.u8string());
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
