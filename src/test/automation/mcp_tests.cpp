#include "mcp_tests.h"

#include "../../automation/mcp.h"
#include "../../../third_party/picojson/picojson.h"
#include "../test_assertions.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    picojson::value parseLine(const std::string& line)
    {
        picojson::value result;
        test::require(picojson::parse(result, line).empty(), "MCP output must be valid JSON");
        return result;
    }
}

void test_mcp_stdio_initialization_and_tools_list()
{
    const auto root = std::filesystem::temp_directory_path() / "cpp-game-engine-mcp-test";
    std::filesystem::create_directories(root);
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
    std::ostringstream output;
    const int status = automation::mcp::runStdio(root, input, output);
    std::filesystem::remove_all(root);

    test::require(status == 0, "MCP session should end cleanly at EOF");
    std::istringstream lines(output.str());
    std::string line;
    test::require(static_cast<bool>(std::getline(lines, line)), "MCP initialize response is missing");
    auto initialized = parseLine(line);
    test::require(initialized.get("result").get("protocolVersion").get<std::string>() == "2025-11-25",
        "legacy MCP initialization should negotiate the supported handshake version");
    test::require(static_cast<bool>(std::getline(lines, line)), "MCP tools/list response is missing");
    auto listed = parseLine(line);
    const auto& tools = listed.get("result").get("tools").get<picojson::array>();
    test::require(tools.size() == 2, "MCP should list the read-only project tools");
    test::require(tools[0].get("name").get<std::string>() == "project_validate", "project_validate should be listed first");
}

void test_mcp_stdio_confines_paths_and_bounds_messages()
{
    const auto parent = std::filesystem::temp_directory_path() / "cpp-game-engine-mcp-confinement-test";
    const auto root = parent / "project";
    std::filesystem::create_directories(root);
    { std::ofstream outside(parent / "outside.yml"); outside << "not project data"; }
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"project_validate\",\"arguments\":{\"manifest\":\"../outside.yml\"}}}\n");
    std::ostringstream output;
    const int status = automation::mcp::runStdio(root, input, output);
    std::filesystem::remove_all(parent);
    test::require(status == 0, "confined MCP call should not terminate the session");
    std::istringstream lines(output.str());
    std::string line;
    std::getline(lines, line);
    std::getline(lines, line);
    auto rejected = parseLine(line);
    test::require(rejected.get("result").get("isError").get<bool>(), "paths outside project root must be rejected");

    std::istringstream oversized("123456789\n");
    std::ostringstream boundedOutput;
    const int boundedStatus = automation::mcp::runStdio(std::filesystem::temp_directory_path(), oversized, boundedOutput, 8);
    test::require(boundedStatus == 1, "oversized MCP lines should terminate with a protocol error");
    auto bounded = parseLine(boundedOutput.str().substr(0, boundedOutput.str().find('\n')));
    test::require(bounded.get("error").get("code").get<double>() == -32700, "oversized line should report parse error");
}

void test_mcp_protocol_errors_negotiation_notifications_and_diagnostics()
{
    const auto root = std::filesystem::temp_directory_path() / "cpp-game-engine-mcp-protocol-test";
    std::filesystem::create_directories(root);
    { std::ofstream manifest(root / "broken.json"); manifest << "{}"; }
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}\n"
        "{\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"1900-01-01\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/unknown\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"project_validate\",\"arguments\":{\"manifest\":\"broken.json\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"missing_tool\",\"arguments\":{}}}\n");
    std::ostringstream output;
    const int status = automation::mcp::runStdio(root, input, output);
    std::filesystem::remove_all(root);

    test::require(status == 0, "protocol errors should not terminate an MCP session");
    std::istringstream lines(output.str());
    std::string line;
    test::require(static_cast<bool>(std::getline(lines, line)), "pre-initialization response missing");
    auto sequenceError = parseLine(line);
    test::require(sequenceError.get("error").get("code").get<double>() == -32002,
        "normal requests must wait until initialized");
    test::require(static_cast<bool>(std::getline(lines, line)), "parse error response missing");
    test::require(parseLine(line).get("error").get("code").get<double>() == -32700,
        "malformed JSON should use the JSON-RPC parse error");
    test::require(static_cast<bool>(std::getline(lines, line)), "initialize response missing");
    auto initialized = parseLine(line);
    test::require(initialized.get("result").get("protocolVersion").get<std::string>() == "2025-11-25",
        "unsupported client versions should negotiate the server's supported version");
    test::require(static_cast<bool>(std::getline(lines, line)), "invalid request response missing");
    test::require(parseLine(line).get("error").get("code").get<double>() == -32600,
        "requests without methods should be rejected");
    test::require(static_cast<bool>(std::getline(lines, line)), "validation tool response missing");
    auto validationResponse = parseLine(line);
    const auto& validation = validationResponse.get("result");
    test::require(validation.get("isError").get<bool>(), "semantic validation failures should be tool errors");
    test::require(validation.get("structuredContent").get("diagnostics").get<picojson::array>().size() > 0,
        "tool errors should preserve shared-service structured diagnostics");
    test::require(static_cast<bool>(std::getline(lines, line)), "unknown tool error response missing");
    test::require(parseLine(line).get("error").get("code").get<double>() == -32602,
        "unknown tools should be reported as invalid parameters");
    test::require(!static_cast<bool>(std::getline(lines, line)), "notifications must not produce response messages");
}

void test_mcp_modern_protocol_discovery_tools_and_version_errors()
{
    const auto root = std::filesystem::temp_directory_path() / "cpp-game-engine-mcp-modern-test";
    std::filesystem::create_directories(root / "scenes");
    std::filesystem::create_directories(root / "assets");
    { std::ofstream manifest(root / "broken.json"); manifest << "{}"; }
    {
        std::ofstream manifest(root / "project.json");
        manifest << R"({"schema":1,"name":"MCP Fixture","startup_scene":"scenes/start.json","assets":[{"id":"asset:mesh","path":"assets/player.mesh","kind":"mesh"},{"id":"asset:texture","path":"assets/player.ppm","kind":"texture"}]})";
    }
    { std::ofstream mesh(root / "assets/player.mesh"); mesh << "fixture"; }
    { std::ofstream texture(root / "assets/player.ppm"); texture << "fixture"; }
    {
        std::ofstream scene(root / "scenes/start.json");
        scene << R"({"schema":1,"scene_id":"scene:mcp","entities":[{"id":"entity:player","name":"Player","components":{"MeshRenderer":{"mesh":"asset:mesh","texture":"asset:texture"}}}]})";
    }
    {
        std::ofstream scene(root / "scenes/large.json");
        scene << R"({"schema":1,"scene_id":"scene:large","entities":[{"id":"entity:large","name":")";
        scene << std::string(600000, 'x');
        scene << R"(","components":{}}]})";
    }
    const std::string meta = "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\",\"io.modelcontextprotocol/clientCapabilities\":{}}";
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\",\"params\":{" + meta + "}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{" + meta + "}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"project_validate\",\"arguments\":{\"manifest\":\"broken.json\"}," + meta + "}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"scene_inspect\",\"arguments\":{\"scene\":\"scenes/start.json\"}," + meta + "}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"scene_inspect\",\"arguments\":{\"scene\":\"scenes/large.json\"}," + meta + "}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/list\",\"params\":{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2025-11-25\",\"io.modelcontextprotocol/clientCapabilities\":{}}}}\n");
    std::ostringstream output;
    const int status = automation::mcp::runStdio(root, input, output);
    std::filesystem::remove_all(root);
    test::require(status == 0, "modern MCP requests should be stateless and continue through EOF");
    std::istringstream lines(output.str());
    std::string line;
    test::require(static_cast<bool>(std::getline(lines, line)), "modern discovery result missing");
    auto discoveryResponse = parseLine(line);
    const auto& discovery = discoveryResponse.get("result");
    test::require(discovery.get("resultType").get<std::string>() == "complete", "modern results must declare resultType");
    test::require(discovery.get("supportedVersions").get<picojson::array>()[0].get<std::string>() == "2026-07-28",
        "server discovery should advertise the current modern protocol version");
    test::require(discovery.get("_meta").get("io.modelcontextprotocol/serverInfo").get("name").get<std::string>() == "cpp-game-engine",
        "modern results should include server identity in result metadata");

    test::require(static_cast<bool>(std::getline(lines, line)), "modern tools/list result missing");
    auto listResponse = parseLine(line);
    test::require(listResponse.get("result").get("resultType").get<std::string>() == "complete" &&
        listResponse.get("result").get("tools").get<picojson::array>().size() == 2,
        "modern tools/list should return the deterministic read-only tool catalog");
    test::require(static_cast<bool>(std::getline(lines, line)), "modern tool call result missing");
    auto callResponse = parseLine(line);
    test::require(callResponse.get("result").get("resultType").get<std::string>() == "complete" &&
        callResponse.get("result").get("isError").get<bool>(),
        "modern tool calls should use a complete result with isError for service diagnostics");
    test::require(static_cast<bool>(std::getline(lines, line)), "modern scene inspection result missing");
    auto sceneResponse = parseLine(line);
    const auto& sceneResult = sceneResponse.get("result");
    test::require(!sceneResult.get("isError").get<bool>() &&
        sceneResult.get("structuredContent").get("entities").get<picojson::array>()[0]
            .get("components").get("MeshRenderer").get("mesh").get<std::string>() == "asset:mesh",
        "scene inspection should load the opened manifest and preserve catalog asset IDs");
    test::require(static_cast<bool>(std::getline(lines, line)), "large scene response missing");
    auto boundedSceneResponse = parseLine(line);
    test::require(line.size() <= 1024 * 1024 && boundedSceneResponse.get("result").get("isError").get<bool>() &&
        boundedSceneResponse.get("result").get("structuredContent").get("error").get<std::string>() ==
            "tool response exceeds the 1 MiB output limit",
        "large tool outputs should become bounded, valid MCP tool errors");
    test::require(static_cast<bool>(std::getline(lines, line)), "unsupported protocol version response missing");
    auto unsupportedResponse = parseLine(line);
    const auto& unsupported = unsupportedResponse.get("error");
    test::require(unsupported.get("code").get<double>() == -32022 &&
        unsupported.get("data").get("supported").get<picojson::array>()[0].get<std::string>() == "2026-07-28",
        "unsupported modern versions should advertise supported versions");
}
