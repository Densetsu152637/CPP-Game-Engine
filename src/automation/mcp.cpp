#include "mcp.h"

#include "../project/project.h"
#include "../../third_party/picojson/picojson.h"

#include <iostream>
#include <optional>
#include <string>
#include <system_error>

namespace automation::mcp
{
    namespace
    {
        using Json = picojson::value;
        using Object = picojson::object;
        using Array = picojson::array;
        constexpr const char* ModernProtocolVersion = "2026-07-28";
        constexpr const char* LegacyProtocolVersion = "2025-11-25";
        constexpr std::size_t MaxRequestIdBytes = 1024;
        constexpr std::size_t MaxOutputMessageBytes = 1024 * 1024;

        Json object(Object value) { return Json(value); }
        Json string(const std::string& value) { return Json(value); }

        Json errorValue(int code, const std::string& message)
        {
            return object({{"code", Json(static_cast<double>(code))}, {"message", string(message)}});
        }

        Json response(const Json& id, const Json& result)
        {
            return object({{"jsonrpc", string("2.0")}, {"id", id}, {"result", result}});
        }

        Json errorResponse(const Json& id, int code, const std::string& message)
        {
            return object({{"jsonrpc", string("2.0")}, {"id", id}, {"error", errorValue(code, message)}});
        }

        Json unsupportedVersionResponse(const Json& id, const std::string& requested)
        {
            return object({
                {"jsonrpc", string("2.0")}, {"id", id},
                {"error", object({
                    {"code", Json(-32022.0)}, {"message", string("Unsupported protocol version")},
                    {"data", object({
                        {"supported", Json(Array{string(ModernProtocolVersion)})},
                        {"requested", string(requested)}
                    })}
                })}
            });
        }

        bool validId(const Json& id)
        {
            return (id.is<std::string>() && id.get<std::string>().size() <= MaxRequestIdBytes) || id.is<double>();
        }

        bool validUtf8(const std::string& text)
        {
            for (std::size_t i = 0; i < text.size();)
            {
                const auto first = static_cast<unsigned char>(text[i]);
                if (first <= 0x7f) { ++i; continue; }
                std::size_t count = 0;
                unsigned char secondMin = 0x80, secondMax = 0xbf;
                if (first >= 0xc2 && first <= 0xdf) count = 2;
                else if (first >= 0xe0 && first <= 0xef)
                {
                    count = 3;
                    if (first == 0xe0) secondMin = 0xa0;
                    if (first == 0xed) secondMax = 0x9f;
                }
                else if (first >= 0xf0 && first <= 0xf4)
                {
                    count = 4;
                    if (first == 0xf0) secondMin = 0x90;
                    if (first == 0xf4) secondMax = 0x8f;
                }
                else return false;
                if (i + count > text.size()) return false;
                const auto second = static_cast<unsigned char>(text[i + 1]);
                if (second < secondMin || second > secondMax) return false;
                for (std::size_t j = 2; j < count; ++j)
                {
                    const auto continuation = static_cast<unsigned char>(text[i + j]);
                    if (continuation < 0x80 || continuation > 0xbf) return false;
                }
                i += count;
            }
            return true;
        }

        bool hasOnlyKeys(const Json& value, std::initializer_list<const char*> allowed)
        {
            if (!value.is<Object>()) return false;
            for (const auto& entry : value.get<Object>())
            {
                bool found = false;
                for (auto key : allowed) if (entry.first == key) found = true;
                if (!found) return false;
            }
            return true;
        }

        std::optional<std::filesystem::path> confinedFile(const std::filesystem::path& root,
            const Json& argument)
        {
            if (!argument.is<std::string>()) return {};
            const std::filesystem::path relative(argument.get<std::string>());
            if (relative.empty() || relative.is_absolute() || relative.has_root_name()) return {};
            std::error_code ec;
            auto candidate = std::filesystem::canonical(root / relative, ec);
            if (ec) return {};
            auto within = candidate.lexically_relative(root);
            if (within.empty() || within.is_absolute()) return {};
            auto first = within.begin();
            if (first != within.end() && *first == "..") return {};
            return candidate;
        }

        Json parseServiceJson(const std::string& text)
        {
            Json value;
            std::string error = picojson::parse(value, text);
            if (!error.empty()) return object({{"raw", string(text)}});
            return value;
        }

        Json toolResult(const Json& data, bool failed)
        {
            return object({
                {"content", Json(Array{object({{"type", string("text")}, {"text", string(data.serialize())}})})},
                {"structuredContent", data},
                {"isError", Json(failed)}
            });
        }

        Json addModernResultMetadata(Json result)
        {
            if (!result.is<Object>()) result = object({{"value", result}});
            auto& fields = result.get<Object>();
            fields["resultType"] = string("complete");
            fields["_meta"] = object({{"io.modelcontextprotocol/serverInfo", object({
                {"name", string("cpp-game-engine")}, {"version", string("0.1.0")}
            })}});
            return result;
        }

        Json modernResponse(const Json& id, const Json& result)
        {
            return response(id, addModernResultMetadata(result));
        }

        Json boundedToolError(const Json& original)
        {
            const Json id = original.is<Object>() && original.contains("id") ? original.get("id") : Json();
            if (!original.is<Object>() || !original.contains("result") || !original.get("result").is<Object>() ||
                !original.get("result").contains("content"))
                return errorResponse(id, -32603, "response exceeds the 1 MiB output limit");
            const Json& originalResult = original.get("result");
            const bool modern = originalResult.is<Object>() && originalResult.contains("resultType");
            Json replacement = toolResult(object({
                {"ok", Json(false)},
                {"error", string("tool response exceeds the 1 MiB output limit")}
            }), true);
            if (modern) replacement = addModernResultMetadata(std::move(replacement));
            return response(id, replacement);
        }

        Json modernDiscovery()
        {
            return object({
                {"supportedVersions", Json(Array{string(ModernProtocolVersion)})},
                {"capabilities", object({{"tools", object({{"listChanged", Json(false)}})}})}
            });
        }

        Json diagnosticResult(const project::Diagnostics& diagnostics)
        {
            return object({{"ok", Json(false)}, {"diagnostics", parseServiceJson(project::diagnosticsJson(diagnostics))}});
        }

        Json callTool(const std::filesystem::path& root, const Json& params)
        {
            if (!hasOnlyKeys(params, {"name", "arguments"}) || !params.contains("name") ||
                !params.get("name").is<std::string>())
                return errorValue(-32602, "tools/call requires name and object arguments");

            const std::string name = params.get("name").get<std::string>();
            const Json& args = params.get("arguments");
            if (!args.is<Object>()) return errorValue(-32602, "tool arguments must be an object");

            if (name == "project_validate")
            {
                if (!hasOnlyKeys(args, {"manifest"}) || !args.contains("manifest"))
                    return errorValue(-32602, "project_validate requires only a project-relative manifest path");
                auto manifest = confinedFile(root, args.get("manifest"));
                if (!manifest) return toolResult(object({{"ok", Json(false)}, {"error", string("manifest path is invalid, missing, or outside the opened project")}}), true);
                auto validated = project::validateProject(*manifest);
                if (!validated) return toolResult(diagnosticResult(validated.error()), true);
                return toolResult(object({{"ok", Json(true)}, {"command", string("project validate")}, {"diagnostics", Json(Array{})}}), false);
            }

            if (name == "scene_inspect")
            {
                if (!hasOnlyKeys(args, {"scene"}) || !args.contains("scene"))
                    return errorValue(-32602, "scene_inspect requires only a project-relative scene path");
                auto scenePath = confinedFile(root, args.get("scene"));
                if (!scenePath) return toolResult(object({{"ok", Json(false)}, {"error", string("scene path is invalid, missing, or outside the opened project")}}), true);
                auto manifest = confinedFile(root, string("project.json"));
                if (!manifest) return toolResult(object({{"ok", Json(false)}, {"error", string("opened project manifest project.json is missing or outside the project root")}}), true);
                auto loadedProject = project::loadProject(*manifest);
                if (!loadedProject) return toolResult(diagnosticResult(loadedProject.error()), true);
                auto inspected = project::inspectScene(*scenePath, *loadedProject);
                if (!inspected) return toolResult(diagnosticResult(inspected.error()), true);
                return toolResult(parseServiceJson(project::sceneJson(*inspected)), false);
            }

            return errorValue(-32602, "unknown tool: " + name);
        }

        Json toolsList()
        {
            auto schema = [](const char* field, const char* description)
            {
                return object({
                    {"type", string("object")},
                    {"properties", object({{field, object({{"type", string("string")}, {"description", string(description)}})}})},
                    {"required", Json(Array{string(field)})},
                    {"additionalProperties", Json(false)}
                });
            };
            const Json readOnly = object({{"readOnlyHint", Json(true)}, {"destructiveHint", Json(false)},
                {"idempotentHint", Json(true)}, {"openWorldHint", Json(false)}});
            return object({{"tools", Json(Array{
                object({{"name", string("project_validate")}, {"description", string("Validate a project manifest and its referenced content.")}, {"inputSchema", schema("manifest", "Project-relative manifest path.")}, {"annotations", readOnly}}),
                object({{"name", string("scene_inspect")}, {"description", string("Inspect a scene and return its authored entities and components.")}, {"inputSchema", schema("scene", "Project-relative scene path.")}, {"annotations", readOnly}})
            })}});
        }

        std::optional<Json> process(const std::filesystem::path& root, const Json& request, bool& initialized,
            bool& receivedInitialized)
        {
            const Json nullId;
            if (!request.is<Object>() || !request.contains("jsonrpc") || !request.get("jsonrpc").is<std::string>() ||
                request.get("jsonrpc").get<std::string>() != "2.0" || !request.contains("method") ||
                !request.get("method").is<std::string>())
                return errorResponse(nullId, -32600, "invalid JSON-RPC request");

            const bool hasId = request.contains("id");
            const Json id = hasId ? request.get("id") : nullId;
            if (hasId && !validId(id))
                return errorResponse(nullId, -32600, "request id must be a number or a string of at most 1024 bytes");
            const std::string method = request.get("method").get<std::string>();
            const Json params = request.contains("params") ? request.get("params") : object({});

            // MCP 2026-07-28 carries protocol metadata per request and has no
            // initialize handshake. A request with modern metadata is always
            // handled statelessly, independently of any legacy session.
            const bool hasMeta = params.is<Object>() && params.contains("_meta");
            if (method == "server/discover" || hasMeta)
            {
                if (!hasId) return {};
                if (!params.is<Object>() || !params.contains("_meta") || !params.get("_meta").is<Object>())
                    return errorResponse(id, -32602, "modern MCP requests require per-request metadata");
                const Json& metadata = params.get("_meta");
                const std::string versionKey = "io.modelcontextprotocol/protocolVersion";
                const std::string capabilitiesKey = "io.modelcontextprotocol/clientCapabilities";
                if (!metadata.contains(versionKey) || !metadata.get(versionKey).is<std::string>() ||
                    !metadata.contains(capabilitiesKey) || !metadata.get(capabilitiesKey).is<Object>())
                    return errorResponse(id, -32602, "modern MCP metadata requires protocolVersion and clientCapabilities");
                const std::string& version = metadata.get(versionKey).get<std::string>();
                if (version != ModernProtocolVersion) return unsupportedVersionResponse(id, version);
                if (method == "server/discover") return modernResponse(id, modernDiscovery());
                if (method == "tools/list") return modernResponse(id, toolsList());
                if (method == "tools/call")
                {
                    Object cleanParams = params.get<Object>();
                    cleanParams.erase("_meta");
                    Json result = callTool(root, Json(cleanParams));
                    if (result.is<Object>() && result.contains("code"))
                        return errorResponse(id, static_cast<int>(result.get("code").get<double>()), result.get("message").get<std::string>());
                    return modernResponse(id, result);
                }
                return errorResponse(id, -32601, "method not found: " + method);
            }

            if (method == "notifications/initialized")
            {
                if (hasId) return errorResponse(id, -32600, "initialized must be a notification");
                if (initialized) receivedInitialized = true;
                return {};
            }
            if (!hasId) return {}; // Other notifications have no response.

            if (method == "initialize")
            {
                if (initialized || !params.is<Object>() || !params.contains("protocolVersion") ||
                    !params.get("protocolVersion").is<std::string>())
                    return errorResponse(id, -32602, "initialize requires protocolVersion and may only be sent once");
                initialized = true;
                return response(id, object({
                    {"protocolVersion", string(LegacyProtocolVersion)},
                    {"capabilities", object({{"tools", object({{"listChanged", Json(false)}})}})},
                    {"serverInfo", object({{"name", string("cpp-game-engine")}, {"version", string("0.1.0")}})}
                }));
            }
            if (!initialized || !receivedInitialized)
                return errorResponse(id, -32002, "server is not initialized");
            if (method == "ping") return response(id, object({}));
            if (method == "tools/list") return response(id, toolsList());
            if (method == "tools/call")
            {
                Json result = callTool(root, params);
                if (result.is<Object>() && result.contains("code"))
                    return errorResponse(id, static_cast<int>(result.get("code").get<double>()), result.get("message").get<std::string>());
                return response(id, result);
            }
            return errorResponse(id, -32601, "method not found: " + method);
        }
    }

    int runStdio(const std::filesystem::path& projectRoot, std::istream& input,
        std::ostream& output, std::size_t maxInputMessageBytes)
    {
        std::error_code ec;
        auto root = std::filesystem::canonical(projectRoot, ec);
        if (ec || !std::filesystem::is_directory(root))
        {
            std::cerr << "MCP: project root is not an accessible directory\n";
            return 2;
        }
        bool initialized = false;
        bool receivedInitialized = false;
        std::string line;
        bool oversized = false;
        char byte = 0;
        while (input.get(byte))
        {
            if (byte != '\n')
            {
                if (line.size() < maxInputMessageBytes) line.push_back(byte);
                else oversized = true;
                continue;
            }
            if (oversized)
            {
                output << errorResponse(Json(), -32700, "message exceeds configured size limit").serialize() << '\n' << std::flush;
                return 1;
            }
            Json request;
            std::string parseError = validUtf8(line) ? picojson::parse(request, line) : "invalid UTF-8";
            if (!parseError.empty())
            {
                output << errorResponse(Json(), -32700, "parse error").serialize() << '\n';
                output.flush();
                line.clear();
                continue;
            }
            try
            {
                auto result = process(root, request, initialized, receivedInitialized);
                if (result)
                {
                    std::string encoded = result->serialize();
                    if (encoded.size() > MaxOutputMessageBytes)
                    {
                        *result = boundedToolError(*result);
                        encoded = result->serialize();
                    }
                    output << encoded << '\n' << std::flush;
                }
            }
            catch (const std::exception& e)
            {
                const Json id = request.is<Object>() && request.contains("id") ? request.get("id") : Json();
                if (request.is<Object>() && request.contains("id"))
                    output << errorResponse(validId(id) ? id : Json(), -32603, "internal server error").serialize() << '\n' << std::flush;
                std::cerr << "MCP request failed: " << e.what() << '\n';
            }
            line.clear();
            oversized = false;
        }
        if (!line.empty() || oversized)
        {
            if (oversized) output << errorResponse(Json(), -32700, "message exceeds configured size limit").serialize() << '\n';
            else output << errorResponse(Json(), -32700, "unterminated JSON-RPC message").serialize() << '\n';
            output.flush();
            return 1;
        }
        output.flush();
        return input.bad() ? 1 : 0;
    }
}
