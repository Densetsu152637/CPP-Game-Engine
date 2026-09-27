#include "content.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rendering
{
    namespace
    {
        constexpr size_t MaxMeshVertices = 1'000'000;
        constexpr size_t MaxTexturePixels = 16'777'216;
        constexpr uint32_t MaxTextureDimension = 8192;

        [[noreturn]] void invalid_asset(const std::filesystem::path& path, const std::string& detail)
        {
            throw std::runtime_error("Invalid asset '" + path.string() + "': " + detail);
        }

        uint32_t parse_unsigned(const std::string& token, const std::filesystem::path& path,
            const char* description)
        {
            uint32_t value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (token.empty() || result.ec != std::errc {} || result.ptr != token.data() + token.size())
                invalid_asset(path, std::string("invalid ") + description);
            return value;
        }

        std::string next_ppm_token(std::istream& input)
        {
            while (input)
            {
                const int next = input.peek();
                if (next == EOF) return {};
                if (std::isspace(static_cast<unsigned char>(next)))
                {
                    input.get();
                    continue;
                }
                if (next == '#')
                {
                    std::string ignored;
                    std::getline(input, ignored);
                    continue;
                }
                break;
            }

            std::string token;
            while (input)
            {
                const int next = input.peek();
                if (next == EOF || std::isspace(static_cast<unsigned char>(next)) || next == '#')
                    break;
                token.push_back(static_cast<char>(input.get()));
            }
            return token;
        }
    }

    VertexLayout MeshAsset::layout() const
    {
        return VertexLayout {
            static_cast<uint32_t>(sizeof(MeshVertex)),
            {
                { 0, VertexAttributeFormat::Float3, 0 },
                { 1, VertexAttributeFormat::Float2, static_cast<uint32_t>(sizeof(float) * 3) }
            }
        };
    }

    SerializedBufferView MeshAsset::vertexData() const
    { return serialized_buffer_view(vertices); }

    bool MeshAsset::valid() const
    { return !vertices.empty() && vertices.size() % 3 == 0 && layout().valid(); }

    MeshAsset loadMeshAsset(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        if (!input) invalid_asset(path, "file could not be opened");

        MeshAsset mesh;
        std::string line;
        size_t lineNumber = 0;
        bool header = false;
        while (std::getline(input, line))
        {
            ++lineNumber;
            const size_t comment = line.find('#');
            if (comment != std::string::npos) line.erase(comment);
            std::istringstream record(line);
            std::string kind;
            if (!(record >> kind)) continue;
            if (!header)
            {
                uint32_t version = 0;
                std::string extra;
                if (kind != "CGMESH" || !(record >> version) || version != 1 || (record >> extra))
                    invalid_asset(path, "line 1 must be 'CGMESH 1'");
                header = true;
                continue;
            }

            if (kind != "vertex")
                invalid_asset(path, "line " + std::to_string(lineNumber) + " must begin with 'vertex'");
            MeshVertex vertex;
            std::string extra;
            if (!(record >> vertex.position[0] >> vertex.position[1] >> vertex.position[2] >>
                vertex.uv[0] >> vertex.uv[1]) || (record >> extra))
                invalid_asset(path, "line " + std::to_string(lineNumber) + " must contain five floats");
            for (const float value : vertex.position)
                if (!std::isfinite(value)) invalid_asset(path, "mesh positions must be finite");
            for (const float value : vertex.uv)
                if (!std::isfinite(value)) invalid_asset(path, "mesh texture coordinates must be finite");
            if (mesh.vertices.size() >= MaxMeshVertices)
                invalid_asset(path, "mesh exceeds the one-million vertex limit");
            mesh.vertices.push_back(vertex);
        }

        if (!header) invalid_asset(path, "missing CGMESH 1 header");
        if (!mesh.valid()) invalid_asset(path, "mesh must contain a non-empty triangle list");
        return mesh;
    }

    Texture2D loadTexturePpm(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) invalid_asset(path, "file could not be opened");

        if (next_ppm_token(input) != "P3")
            invalid_asset(path, "only ASCII PPM (P3) textures are supported");
        const uint32_t width = parse_unsigned(next_ppm_token(input), path, "PPM width");
        const uint32_t height = parse_unsigned(next_ppm_token(input), path, "PPM height");
        const uint32_t maxValue = parse_unsigned(next_ppm_token(input), path, "PPM max value");
        if (width == 0 || height == 0 || width > MaxTextureDimension || height > MaxTextureDimension ||
            static_cast<size_t>(width) > MaxTexturePixels / height)
            invalid_asset(path, "texture dimensions exceed the 8192-per-axis or 16-million-pixel limit");
        if (maxValue != 255)
            invalid_asset(path, "PPM max value must be 255");

        Texture2D texture;
        texture.width = width;
        texture.height = height;
        const size_t pixels = static_cast<size_t>(width) * height;
        texture.rgba8.reserve(pixels * 4);
        for (size_t index = 0; index < pixels; ++index)
        {
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                const std::string token = next_ppm_token(input);
                if (token.empty()) invalid_asset(path, "PPM raster ended before all RGB channels were read");
                const uint32_t value = parse_unsigned(token, path, "PPM channel");
                if (value > 255) invalid_asset(path, "PPM channel values must be between 0 and 255");
                texture.rgba8.push_back(static_cast<uint8_t>(value));
            }
            texture.rgba8.push_back(255);
        }
        if (!next_ppm_token(input).empty())
            invalid_asset(path, "PPM raster contains extra channel values");
        return texture;
    }
}
