#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>

namespace automation::mcp
{
    // Runs an MCP JSON-RPC stdio session. The configured project root is the
    // only filesystem scope exposed to tool requests. Individual input lines
    // are capped to prevent unbounded request allocation.
    int runStdio(const std::filesystem::path& projectRoot, std::istream& input,
        std::ostream& output, std::size_t maxInputMessageBytes = 1024 * 1024);
}
