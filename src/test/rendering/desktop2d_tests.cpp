#include "rendering/sprite2d.h"
#include "rendering/text2d.h"
#include "test/test_assertions.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
void test_desktop2d_rendering()
{
    using namespace rendering;
    const auto camera = makeCamera2DView({{320, 180}, 16, {0.031f, 0.1f}, true}, 1000, 700);
    test::require(camera.scale == 3 && camera.framebufferViewport.x == 20 &&
                      camera.framebufferViewport.y == 80,
                  "Integer viewport/letterbox mismatch");
    test::require(camera.camera.viewProjection[12] == 0 && camera.camera.viewProjection[13] > 0,
                  "Camera pixel snap mismatch");
    const auto small = makeCamera2DView({{320, 180}, 1, {}, true}, 160, 100);
    test::require(small.scale == 0.5f && small.framebufferViewport.height == 90 &&
                      small.framebufferViewport.y == 5,
                  "Small window fractional fallback mismatch");
    test::require(makeCamera2DView({{320, 180}}, 0, 0).scale == 0, "Minimized camera must skip");
    Sprite2DDescription sprite;
    sprite.position = {2, 3};
    sprite.size = {4, 2};
    sprite.pivot = {0, 1};
    sprite.source = {4, 8, 4, 2};
    const auto quad = makeSpriteQuad(sprite, 16, 16);
    test::require(quad.vertices.size() == 6 && quad.vertices[0].position[0] == 2 &&
                      quad.vertices[0].position[1] == 3 && quad.vertices[0].uv[0] == 0.25f,
                  "Sprite pivot/atlas quad mismatch");
    sprite.visible = false;
    test::require(makeSpriteQuad(sprite, 16, 16).vertices.empty(), "Invisible sprite drew geometry");
    test::require(animationFrame(1.25, 4, 4, true) == 1 && animationFrame(1.25, 4, 4, false) == 3 &&
                      animationFrame(0.249, 4, 4, true) == 0,
                  "Animation timing mismatch");
    auto rejects = [](auto fn) {
        try
        {
            fn();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("Invalid 2D value was accepted");
    };
    rejects([&] { animationFrame(-1, 4, 4, true); });
    rejects([&] {
        sprite.source = {15, 0, 2, 1};
        makeSpriteQuad(sprite, 16, 16);
    });
    rejects([&] { decodeUtf8("\xc0\xaf"); });
    rejects([&] { decodeUtf8("\xed\xa0\x80"); });
    rejects([&] { decodeUtf8("\xf4\x90\x80\x80"); });
    test::require(decodeUtf8("A\xc3\xa9\xf0\x9f\x95\x8a") == std::vector<uint32_t>({65, 233, 0x1f54a}),
                  "UTF-8 scalar decoding mismatch");
    FontAtlas font{"asset:atlas",
                   8,
                   63,
                   {{32, {{0, 0, 0, 0}, 3, {0, 0}}},
                    {63, {{0, 0, 4, 6}, 5, {0, 1}}},
                    {65, {{4, 0, 4, 6}, 5, {0, 1}}},
                    {233, {{8, 0, 4, 6}, 5, {0, 1}}}}};
    Text2DDescription text{"A A\xc3\xa9\nZ", {0, 0}, 10, 0, LogicalRect{1, 2, 8, 20}};
    const auto layout = makeTextGeometry(font, 16, 8, text);
    test::require(layout.height == 24 && layout.missingCodepoints == std::vector<uint32_t>{90},
                  "Text wrap/newline/fallback mismatch");
    for (const auto& v : layout.mesh.vertices)
        test::require(v.position[0] >= 1 && v.position[0] <= 9 && v.position[1] >= 2 && v.position[1] <= 22,
                      "Text geometry escaped clip");
    const auto clipped = makeTextGeometry(font, 16, 8, {"A", {}, 0, 0, LogicalRect{2, 3, 2, 2}});
    test::require(clipped.mesh.vertices.size() == 6 &&
                      std::abs(clipped.mesh.vertices[0].uv[0] - 0.375f) < 0.0001f,
                  "Clipped glyph UV adjustment mismatch");
    std::filesystem::create_directories("build/desktop2d");
    const auto path = std::filesystem::path("build/desktop2d/font.json");
    {
        std::ofstream file(path);
        file
            << R"({"schema":1,"texture":"asset:atlas","lineHeight":8,"fallback":63,"glyphs":[{"codepoint":63,"source":[0,0,4,6],"advance":5,"bearing":[0,1]}]})";
    }
    test::require(loadFontAtlas(path, 16, 8).glyphs.size() == 1, "Font JSON import mismatch");
    rejects([&] { loadFontAtlas(path, 2, 2); });
    // A real RGBA PNG: each scanline is generated using a zlib uncompressed DEFLATE block.
    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    auto append32 = [](std::vector<uint8_t>& v, uint32_t n) {
        v.push_back(uint8_t(n >> 24));
        v.push_back(uint8_t(n >> 16));
        v.push_back(uint8_t(n >> 8));
        v.push_back(uint8_t(n));
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        append32(png, static_cast<uint32_t>(data.size()));
        const size_t start = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        uint32_t crc = ~0u;
        for (size_t i = start; i < png.size(); ++i)
        {
            crc ^= png[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
        append32(png, ~crc);
    };
    std::vector<uint8_t> ihdr;
    append32(ihdr, 2);
    append32(ihdr, 1);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", ihdr);
    const std::vector<uint8_t> scanline = {0, 255, 0, 0, 0, 0, 255, 0, 128};
    std::vector<uint8_t> z = {0x78, 0x01, 0x01, 9, 0, 0xf6, 0xff};
    z.insert(z.end(), scanline.begin(), scanline.end());
    uint32_t a = 1, b = 0;
    for (auto c : scanline)
    {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    append32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    const auto pngPath = std::filesystem::path("build/desktop2d/alpha.png");
    {
        std::ofstream f(pngPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), png.size());
    }
    const auto image = loadTexture(pngPath);
    test::require(image.width == 2 && image.rgba8 == std::vector<uint8_t>({255, 0, 0, 0, 0, 255, 0, 128}),
                  "PNG straight RGBA transparency mismatch");
    {
        std::ofstream f("build/desktop2d/bad.png");
        f << "not png";
    }
    rejects([&] { loadTexturePng("build/desktop2d/bad.png"); });
    std::cout << "desktop2d CPU tests passed: PNG alpha/invalid import, camera viewport/resize/snap, sprite "
                 "pivot/visibility/atlas, animation loop/clamp, UTF8/font/wrap/clip/fallback\n";
}
#ifdef DESKTOP2D_TEST_MAIN
int main()
{
    try
    {
        test_desktop2d_rendering();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
