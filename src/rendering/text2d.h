#pragma once
#include "sprite2d.h"
#include <map>
#include <string>
#include <string_view>
namespace rendering
{
    struct FontGlyph
    {
        PixelRect source;
        float advance = 0;
        std::array<float, 2> bearing{};
    };
    struct FontAtlas
    {
        std::string texture; // Stable asset ID; caller resolves the declared atlas texture.
        float lineHeight = 0;
        uint32_t fallback = 0;
        std::map<uint32_t, FontGlyph> glyphs;
    };
    FontAtlas loadFontAtlas(const std::filesystem::path&, uint32_t textureWidth, uint32_t textureHeight);
    std::vector<uint32_t> decodeUtf8(std::string_view); // Rejects malformed/overlong Unicode.
    struct LogicalRect
    {
        float x = 0, y = 0, width = 0, height = 0;
    };
    struct Text2DDescription
    {
        std::string_view text;
        std::array<float, 2> origin{};
        float wrapWidth = 0; // Zero disables wrapping. Words wrap first; long words break by glyph.
        float scrollY = 0;
        std::optional<LogicalRect> clip;
    };
    struct Text2DGeometry
    {
        MeshAsset mesh;
        float width = 0, height = 0;
        std::vector<uint32_t> missingCodepoints; // Unique codepoints rendered using the explicit fallback.
    };
    // Atlas pixels are logical UI units; bearing is measured from the top of each line.
    Text2DGeometry makeTextGeometry(const FontAtlas&, uint32_t textureWidth, uint32_t textureHeight,
                                    const Text2DDescription&);
} // namespace rendering
