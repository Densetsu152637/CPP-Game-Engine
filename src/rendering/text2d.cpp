#include "text2d.h"
#include "../../third_party/picojson/picojson.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
namespace rendering
{
    namespace
    {
        [[noreturn]] void invalid(const char* text)
        {
            throw std::invalid_argument(text);
        }
        bool scalar(uint32_t c)
        {
            return c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff);
        }
        const picojson::object& object(const picojson::value& v)
        {
            if (!v.is<picojson::object>())
                invalid("Font requires JSON objects");
            return v.get<picojson::object>();
        }
        void fields(const picojson::object& o, std::initializer_list<const char*> allowed)
        {
            for (const auto& [key, v] : o)
                if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return key == a; }))
                    invalid("Unknown font field");
        }
        const picojson::value& required(const picojson::object& o, const char* key)
        {
            const auto i = o.find(key);
            if (i == o.end())
                invalid("Missing font field");
            return i->second;
        }
        float number(const picojson::value& v)
        {
            if (!v.is<double>() || !std::isfinite(v.get<double>()) || std::abs(v.get<double>()) > 1e7)
                invalid("Font metric must be a finite bounded number");
            return static_cast<float>(v.get<double>());
        }
        uint32_t integer(const picojson::value& v)
        {
            const float n = number(v);
            if (n < 0 || std::floor(n) != n)
                invalid("Font integer field is invalid");
            return static_cast<uint32_t>(n);
        }
        const picojson::array& array(const picojson::value& v, size_t count)
        {
            if (!v.is<picojson::array>() || v.get<picojson::array>().size() != count)
                invalid("Font array has wrong length");
            return v.get<picojson::array>();
        }
        void validate(const FontAtlas& font, uint32_t tw, uint32_t th)
        {
            if (!tw || !th || !std::isfinite(font.lineHeight) || font.lineHeight <= 0 ||
                font.texture.empty() || font.glyphs.empty() || !font.glyphs.contains(font.fallback))
                invalid("Font has no valid atlas metrics/fallback");
            for (const auto& [cp, g] : font.glyphs)
            {
                const auto r = g.source;
                if (!scalar(cp) || !std::isfinite(g.advance) || g.advance < 0 ||
                    !std::isfinite(g.bearing[0]) || !std::isfinite(g.bearing[1]) || r.x > tw || r.y > th ||
                    r.width > tw - r.x || r.height > th - r.y)
                    invalid("Font glyph has invalid metrics or exceeds atlas");
            }
        }
    } // namespace
    FontAtlas loadFontAtlas(const std::filesystem::path& path, uint32_t tw, uint32_t th)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0 || input.tellg() > 4 * 1024 * 1024)
            invalid("Font JSON cannot be read or exceeds 4 MiB");
        input.seekg(0);
        picojson::value doc;
        const auto error = picojson::parse(doc, input);
        if (!error.empty())
            throw std::invalid_argument("Invalid font JSON: " + error);
        const auto& o = object(doc);
        fields(o, {"schema", "texture", "lineHeight", "fallback", "glyphs"});
        if (integer(required(o, "schema")) != 1)
            invalid("Unsupported font schema");
        FontAtlas result;
        const auto& texture = required(o, "texture");
        if (!texture.is<std::string>())
            invalid("Font texture must be a stable asset ID");
        result.texture = texture.get<std::string>();
        result.lineHeight = number(required(o, "lineHeight"));
        result.fallback = integer(required(o, "fallback"));
        const auto& gs = required(o, "glyphs");
        if (!gs.is<picojson::array>() || gs.get<picojson::array>().size() > 65536)
            invalid("Font glyph list exceeds limit");
        for (const auto& v : gs.get<picojson::array>())
        {
            const auto& g = object(v);
            fields(g, {"codepoint", "source", "advance", "bearing"});
            const auto cp = integer(required(g, "codepoint"));
            const auto& src = array(required(g, "source"), 4);
            const auto& b = array(required(g, "bearing"), 2);
            FontGlyph glyph{{integer(src[0]), integer(src[1]), integer(src[2]), integer(src[3])},
                            number(required(g, "advance")),
                            {number(b[0]), number(b[1])}};
            if (!result.glyphs.emplace(cp, glyph).second)
                invalid("Duplicate font codepoint");
        }
        validate(result, tw, th);
        return result;
    }
    std::vector<uint32_t> decodeUtf8(std::string_view text)
    {
        if (text.size() > 1024 * 1024)
            invalid("Text exceeds 1 MiB");
        std::vector<uint32_t> result;
        for (size_t i = 0; i < text.size();)
        {
            const uint8_t first = static_cast<uint8_t>(text[i++]);
            uint32_t cp = 0;
            unsigned tail = 0;
            uint32_t minimum = 0;
            if (first < 0x80)
                cp = first;
            else if ((first & 0xe0) == 0xc0)
            {
                cp = first & 0x1f;
                tail = 1;
                minimum = 0x80;
            }
            else if ((first & 0xf0) == 0xe0)
            {
                cp = first & 0xf;
                tail = 2;
                minimum = 0x800;
            }
            else if ((first & 0xf8) == 0xf0)
            {
                cp = first & 7;
                tail = 3;
                minimum = 0x10000;
            }
            else
                invalid("Malformed UTF-8 leading byte");
            for (unsigned n = 0; n < tail; ++n)
            {
                if (i == text.size())
                    invalid("Truncated UTF-8 sequence");
                const uint8_t next = static_cast<uint8_t>(text[i++]);
                if ((next & 0xc0) != 0x80)
                    invalid("Malformed UTF-8 continuation");
                cp = (cp << 6) | (next & 0x3f);
            }
            if (cp < minimum || !scalar(cp))
                invalid("UTF-8 is overlong or not a Unicode scalar");
            result.push_back(cp);
        }
        return result;
    }
    Text2DGeometry makeTextGeometry(const FontAtlas& font, uint32_t tw, uint32_t th,
                                    const Text2DDescription& desc)
    {
        validate(font, tw, th);
        if (!std::isfinite(desc.origin[0]) || !std::isfinite(desc.origin[1]) ||
            !std::isfinite(desc.scrollY) || !std::isfinite(desc.wrapWidth) || desc.wrapWidth < 0)
            invalid("Invalid text layout coordinate");
        if (desc.clip)
        {
            const auto c = *desc.clip;
            if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.width) ||
                !std::isfinite(c.height) || c.width < 0 || c.height < 0)
                invalid("Invalid text clip");
        }
        const auto cps = decodeUtf8(desc.text);
        Text2DGeometry result;
        std::set<uint32_t> missing;
        auto glyph = [&](uint32_t c) -> const FontGlyph& {
            const auto i = font.glyphs.find(c);
            if (i != font.glyphs.end())
                return i->second;
            missing.insert(c);
            return font.glyphs.at(font.fallback);
        };
        float x = 0, y = 0;
        bool lineHasGlyph = false;
        auto newline = [&] {
            result.width = std::max(result.width, x);
            x = 0;
            y += font.lineHeight;
            lineHasGlyph = false;
        };
        for (size_t i = 0; i < cps.size(); ++i)
        {
            const auto cp = cps[i];
            if (cp == '\r')
            {
                if (i + 1 < cps.size() && cps[i + 1] == '\n')
                    continue;
                newline();
                continue;
            }
            if (cp == '\n')
            {
                newline();
                continue;
            }
            if (desc.wrapWidth > 0 && cp != ' ' && (i == 0 || cps[i - 1] == ' ' || cps[i - 1] == '\n'))
            {
                float word = 0;
                for (size_t n = i; n < cps.size() && cps[n] != ' ' && cps[n] != '\n' && cps[n] != '\r'; ++n)
                    word += glyph(cps[n]).advance;
                if (lineHasGlyph && x + word > desc.wrapWidth)
                    newline();
            }
            const auto& g = glyph(cp);
            if (desc.wrapWidth > 0 && lineHasGlyph && x + g.advance > desc.wrapWidth)
                newline();
            if (cp == ' ' && !lineHasGlyph && desc.wrapWidth > 0)
                continue;
            float l = desc.origin[0] + x + g.bearing[0], t = desc.origin[1] + y + g.bearing[1] - desc.scrollY;
            float r = l + g.source.width, b = t + g.source.height;
            float u = float(g.source.x) / tw, v = float(g.source.y) / th,
                  ur = float(g.source.x + g.source.width) / tw, vb = float(g.source.y + g.source.height) / th;
            if (g.source.width && g.source.height)
            {
                if (desc.clip)
                {
                    const auto c = *desc.clip;
                    const float cl = std::max(l, c.x), ct = std::max(t, c.y), cr = std::min(r, c.x + c.width),
                                cb = std::min(b, c.y + c.height);
                    const float du = (ur - u) / (r - l), dv = (vb - v) / (b - t);
                    ur -= (r - cr) * du;
                    vb -= (b - cb) * dv;
                    u += (cl - l) * du;
                    v += (ct - t) * dv;
                    l = cl;
                    t = ct;
                    r = cr;
                    b = cb;
                }
                if (l < r && t < b)
                {
                    const MeshVertex vertices[] = {{{l, t, 0}, {u, v}},   {{l, b, 0}, {u, vb}},
                                                   {{r, b, 0}, {ur, vb}}, {{l, t, 0}, {u, v}},
                                                   {{r, b, 0}, {ur, vb}}, {{r, t, 0}, {ur, v}}};
                    result.mesh.vertices.insert(result.mesh.vertices.end(), std::begin(vertices),
                                                std::end(vertices));
                }
            }
            x += g.advance;
            lineHasGlyph = true;
        }
        result.width = std::max(result.width, x);
        result.height = cps.empty() ? 0 : y + font.lineHeight;
        result.missingCodepoints.assign(missing.begin(), missing.end());
        return result;
    }
} // namespace rendering
