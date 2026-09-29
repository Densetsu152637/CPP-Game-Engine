#include "commands.h"
#include "project/project.h"
#include "project/runtime.h"
#include "tooling/iteration.h"
#include "editor/editor.h"
#include "automation/mcp.h"
#include "platform/platform_check.h"
#include "rendering/content.h"
#include "rendering/camera.h"
#include "rendering/material.h"
#include "rendering/render_device.h"
#include "scripting/lua_script_system.h"
#include "scripting/lua_source_file.h"
#include "vulcan/vulkan_frame_backend.h"
#include "vulcan/vulkan_surface_provider.h"
#include "../third_party/picojson/picojson.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using Path = std::filesystem::path;
struct Arguments {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> options;
    Arguments(int argc, char** argv, int start) {
        for (int i = start; i < argc; ++i) {
            std::string arg = argv[i];
            if (!arg.starts_with("--")) { positional.push_back(arg); continue; }
            if (arg == "--headless" || arg == "--rebuild") { options[arg].push_back("true"); continue; }
            if (arg != "--format" && arg != "--ticks" && arg != "--project-root" && arg != "--runtime" &&
                arg != "--compiler" && arg != "--input" && arg != "--shaders")
                throw std::invalid_argument("Unknown option: " + arg);
            if (i + 1 == argc) throw std::invalid_argument("Missing value for " + arg);
            options[arg].push_back(argv[++i]);
        }
        if (get("--format", "json") != "json") throw std::invalid_argument("Only --format json is supported");
        for (const auto& [key, values] : options)
            if (key != "--input" && values.size() != 1) throw std::invalid_argument("Duplicate option: " + key);
    }
    std::string get(const std::string& key, std::string fallback = {}) const {
        const auto found = options.find(key); return found == options.end() ? fallback : found->second.front();
    }
    bool has(const std::string& key) const { return options.contains(key); }
    void require(size_t count, std::initializer_list<std::string> allowed) const {
        if (positional.size() != count) throw std::invalid_argument("Incorrect number of command arguments");
        for (const auto& [key, value] : options)
            if (key != "--format" && std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                throw std::invalid_argument("Option is not supported for this command: " + key);
    }
};
unsigned number(const std::string& text, unsigned minimum, unsigned maximum) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < minimum || value > maximum)
        throw std::invalid_argument("Expected integer between " + std::to_string(minimum) + " and " + std::to_string(maximum));
    return value;
}
std::string quote(const std::string& text) { return picojson::value(text).serialize(); }
int fail(const std::string& command, const project::Diagnostics& diagnostics, int code = 1) {
    for (const auto& d : diagnostics) std::cerr << d.code << ": " << d.file.generic_string() << ':' << d.path << ": " << d.message << '\n';
    std::cout << project::resultJson(false, command, "{\"diagnostics\":" + project::diagnosticsJson(diagnostics) + "}") << '\n';
    return code;
}
int success(const std::string& command, const std::string& result = "{}") {
    std::cout << project::resultJson(true, command, result) << '\n'; return 0;
}
project::Result<void> compileScripts(const project::Project& value) {
    std::vector<std::pair<std::string, std::string>> declarations;
    std::map<std::string, std::pair<Path, std::string>> scriptOrigins;
    for (const auto& [id, asset] : value.assets) {
        auto path = project::resolveAsset(value, id);
        if (!path) return std::unexpected(path.error());
        try {
            if (asset.kind == "script") {
                auto source = scripting::read_lua_source_file(*path);
                if (!source) {
                    const bool tooLarge = source.error() == scripting::LuaSourceReadError::TooLarge;
                    return std::unexpected(project::Diagnostics{{tooLarge ? "project.script.too_large" : "project.script.read",
                        project::Severity::Error, *path, id, std::string(scripting::lua_source_error_message(source.error()))}});
                }
                auto valid = tooling::validateLua(*path);
                if (!valid) return std::unexpected(valid.error());
                const std::string chunkName = "@" + path->string();
                scriptOrigins.emplace(chunkName, std::pair<Path, std::string>{*path, id});
                declarations.emplace_back(std::move(*source), chunkName);
            } else if (asset.kind == "mesh") (void)rendering::loadMeshAsset(*path);
            else if (asset.kind == "texture") (void)rendering::loadTexturePpm(*path);
        } catch (const std::exception& error) {
            return std::unexpected(project::Diagnostics{{"asset.content.invalid", project::Severity::Error, *path, id, error.what()}});
        }
    }
    if (!declarations.empty()) {
        const auto valid = LuaScriptSystem::validate_declarations(declarations, value.root);
        if (!valid) {
            Path file = value.manifest;
            std::string field = "assets";
            for (const auto& [chunkName, origin] : scriptOrigins)
                if (valid.error().starts_with(chunkName + ": ")) {
                    file = origin.first;
                    field = origin.second;
                    break;
                }
            return std::unexpected(project::Diagnostics{{"project.script.declaration", project::Severity::Error,
                file, field, valid.error()}});
        }
    }
    return {};
}
#ifdef CPP_GAME_ENGINE_USE_VULKAN
class Preview {
    GLFWwindow* window = nullptr;
    std::unique_ptr<vulkan::VulkanGlfwSurfaceProvider> surface;
    std::unique_ptr<rendering::RenderDevice> device;
    vulkan::VulkanShaderProgram shader{"authored content"};
    rendering::RenderResourceHandle shaderHandle;
    struct Drawable {
        std::string entityId;
        rendering::RenderResourceHandle mesh;
        rendering::RenderResourceHandle texture;
    };
    std::vector<Drawable> drawables;
    std::map<std::string, bool> held;
    std::map<std::string, int> keys;
public:
    Preview(const project::Project& project, const Path& shaders) {
        struct PendingDrawable {
            std::string entityId;
            rendering::MeshAsset mesh;
            rendering::Texture2D texture;
        };
        std::vector<PendingDrawable> pending;
        const std::map<std::string, int> keyCodes{{"Right",GLFW_KEY_RIGHT},{"Left",GLFW_KEY_LEFT},{"Up",GLFW_KEY_UP},{"Down",GLFW_KEY_DOWN},{"Space",GLFW_KEY_SPACE}};
        for (const auto& [action, key] : project.inputActions) {
            const auto code = keyCodes.find(key);
            if (code == keyCodes.end()) throw std::runtime_error("Unsupported authored input key: " + key);
            keys.emplace(action, code->second);
        }
        for (const auto& entity : project.scene.entities) {
            if (!entity.meshRenderer) continue;
            auto meshPath = project::resolveAsset(project, entity.meshRenderer->mesh);
            if (!meshPath) throw std::runtime_error("Unable to resolve mesh for " + entity.id);
            PendingDrawable drawable{entity.id, rendering::loadMeshAsset(*meshPath), {1, 1, {255,255,255,255}}};
            if (entity.meshRenderer->texture) {
                auto texturePath = project::resolveAsset(project, *entity.meshRenderer->texture);
                if (!texturePath) throw std::runtime_error("Unable to resolve texture for " + entity.id);
                drawable.texture = rendering::loadTexturePpm(*texturePath);
            }
            pending.push_back(std::move(drawable));
        }
        if (pending.empty()) throw std::runtime_error("Visible preview requires at least one entity with MeshRenderer");
        shader.addSpirv(rendering::ShaderStage::Vertex, shaders / "mesh_textured.vert.spv");
        shader.addSpirv(rendering::ShaderStage::Fragment, shaders / "mesh_textured.frag.spv");
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(960, 600, project.name.c_str(), nullptr, nullptr);
        if (!window) { glfwTerminate(); throw std::runtime_error("GLFW window creation failed"); }
        try {
            surface = std::make_unique<vulkan::VulkanGlfwSurfaceProvider>(window);
            device = std::make_unique<rendering::RenderDevice>(
                std::make_unique<vulkan::VulkanFrameBackend>(*surface));
            const auto capabilities = device->capabilities();
            for (const auto feature : std::array{rendering::RenderFeature::BackendAvailable,
                    rendering::RenderFeature::SampledTextures, rendering::RenderFeature::DepthAttachment}) {
                const auto status = capabilities.feature(feature);
                if (!status.supported)
                    throw std::runtime_error(std::string("Authored preview requires ") + status.name + ": " + status.reason);
            }
            shaderHandle = device->createShader(shader);
            for (const auto& drawable : pending)
                drawables.push_back({drawable.entityId,
                    device->createMesh(drawable.mesh.vertexData(), drawable.mesh.layout()),
                    device->createTexture(drawable.texture)});
        } catch (...) {
            device.reset();
            surface.reset();
            glfwDestroyWindow(window);
            window = nullptr;
            glfwTerminate();
            throw;
        }
    }
    ~Preview() {
        try { if (device) device->shutdown(); }
        catch (const std::exception& error) { std::cerr << "Renderer shutdown: " << error.what() << '\n'; }
        catch (...) { std::cerr << "Renderer shutdown failed\n"; }
        device.reset();
        surface.reset();
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
    void finish() { if (device) { device->waitIdle(); device->shutdown(); } }
    bool sample(project::InputSnapshot& input) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) return false;
        for (const auto& [action, code] : keys) {
            const bool down = glfwGetWindowAttrib(window, GLFW_FOCUSED) && glfwGetKey(window, code) == GLFW_PRESS;
            if (down) input.held.insert(action);
            if (down && !held[action]) input.pressed.insert(action);
            if (!down && held[action]) input.released.insert(action);
            held[action] = down;
        }
        return true;
    }
    void draw(const project::Runtime& runtime) {
        auto frame = device->makeFrame();
        rendering::CameraUniform camera;
        camera.viewProjection = {0.5f,0,0,0, 0,-0.8f,0,0, 0,0,-0.1f,0, 0,0,0.5f,1};
        rendering::MaterialUniform material;
        for (const auto& drawable : drawables) {
            const auto position = runtime.position(drawable.entityId);
            if (!position) continue;
            const std::array<float, 16> model{1,0,0,0, 0,1,0,0, 0,0,1,0, (*position)[0],(*position)[1],(*position)[2],1};
            rendering::DrawCommand draw{shaderHandle, drawable.mesh, drawable.texture};
            rendering::appendUniform(draw, "model", model, {0,0});
            rendering::appendUniform(draw, "material", material, {0,1});
            rendering::appendUniform(draw, "camera", camera, {0,2});
            frame->record(std::move(draw));
        }
        device->submit(frame);
    }
};
#endif
int runProject(const Arguments& args) {
    args.require(1, {"--headless", "--ticks", "--input", "--shaders"});
    const auto ticks = number(args.get("--ticks", "120"), 1, 1000000);
    auto loaded = project::loadProject(args.positional[0]);
    if (!loaded) return fail("run", loaded.error());
    auto compiled = compileScripts(*loaded);
    if (!compiled) return fail("run", compiled.error());
    std::map<unsigned, project::InputSnapshot> inputs;
    if (const auto found = args.options.find("--input"); found != args.options.end()) {
        for (const auto& event : found->second) {
            const auto colon = event.rfind(':');
            if (colon == std::string::npos || colon == 0) throw std::invalid_argument("--input expects action:tick");
            if (!loaded->inputActions.contains(event.substr(0, colon)))
                return fail("run", {{"command.input.unknown", project::Severity::Error, loaded->manifest, "--input", "Input action is not declared by the project: " + event.substr(0, colon)}}, 2);
            const unsigned tick = number(event.substr(colon + 1), 0, ticks - 1);
            inputs[tick].pressed.insert(event.substr(0, colon));
            inputs[tick].held.insert(event.substr(0, colon));
            if (tick + 1 < ticks) inputs[tick + 1].released.insert(event.substr(0, colon));
        }
    }
    project::Runtime runtime(*loaded);
    auto started = runtime.start();
    if (!started) return fail("run", started.error());
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    std::unique_ptr<Preview> preview;
    if (!args.has("--headless")) preview = std::make_unique<Preview>(*loaded, args.get("--shaders", "build/debug-vk1/shaders"));
#endif
    for (unsigned tick = 0; tick < ticks; ++tick) {
        const auto next = std::chrono::steady_clock::now() + std::chrono::microseconds(16667);
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (preview && !preview->sample(inputs[tick])) break;
#endif
        const auto advanced = runtime.tick(inputs[tick]);
        if (!advanced) return fail("run", advanced.error(), 3);
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (preview) { preview->draw(runtime); std::this_thread::sleep_until(next); }
#endif
    }
    auto scene = loaded->scene;
    std::sort(scene.entities.begin(), scene.entities.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (auto& entity : scene.entities) if (const auto p = runtime.position(entity.id)) entity.transform = project::Transform{*p};
    const auto completedTicks = runtime.tickCount();
    auto stopped = runtime.stop();
    if (!stopped) return fail("run", stopped.error(), 3);
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    if (preview) preview->finish();
#endif
    return success("run", "{\"ticks\":" + std::to_string(completedTicks) + ",\"fixed_delta\":0.016666667,\"scene\":" + project::sceneJson(scene) + "}");
}
}

std::optional<int> runCommands(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]).starts_with("--")) return std::nullopt;
    std::string command = argv[1];
    try {
        if (command == "mcp") {
            if (argc != 3) throw std::invalid_argument("Usage: CPPGameEngine mcp <project-root>");
            return automation::mcp::runStdio(argv[2], std::cin, std::cout);
        }
        if (command == "editor") {
            if (argc != 3) throw std::invalid_argument("Usage: CPPGameEngine editor <project.json>");
            return editor::run(argv[2]);
        }
        if (command == "run") return runProject(Arguments(argc, argv, 2));
        if (command == "platform") {
            if (argc < 3 || std::string(argv[2]) != "check") throw std::invalid_argument("Usage: CPPGameEngine platform check <android|ios> [--format json]");
            command = "platform check";
            const Arguments args(argc, argv, 3);
            args.require(1, {});
            if (args.positional[0] != "android" && args.positional[0] != "ios")
                throw std::invalid_argument("Platform target must be android or ios");
            const auto report = platform::checkPlatform(args.positional[0]);
            if (!report.readyToBuild())
                for (const auto& diagnostic : report.diagnostics)
                    if (diagnostic.kind == platform::DiagnosticKind::Missing || diagnostic.kind == platform::DiagnosticKind::Unsupported)
                        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            std::cout << project::resultJson(report.readyToBuild(), command, report.toJson()) << '\n';
            return report.readyToBuild() ? 0 : 1;
        }
        if ((command != "project" && command != "scene") || argc < 3) throw std::invalid_argument("Expected project, scene, run, platform, editor, or mcp command");
        command += " " + std::string(argv[2]);
        const Arguments args(argc, argv, 3);
        if (command == "project init") {
            args.require(2, {});
            const auto initialized = tooling::initializeProject(args.positional[0], args.positional[1]);
            if (!initialized) return fail(command, initialized.error());
            return success(command, "{\"directory\":" + quote(initialized->generic_string()) + ",\"manifest\":" + quote((*initialized / "project.json").generic_string()) + "}");
        }
        if (command == "scene inspect") {
            args.require(1, {"--project-root"});
            if (!args.has("--project-root")) throw std::invalid_argument("scene inspect requires --project-root");
            auto loaded = project::loadProject(Path(args.get("--project-root")) / "project.json");
            if (!loaded) return fail(command, loaded.error());
            auto scene = project::inspectScene(args.positional[0], *loaded);
            if (!scene) return fail(command, scene.error());
            return success(command, project::sceneJson(*scene, project::revisionFor(*scene)));
        }
        if (command == "project validate" || command == "project assets" || command == "project package" || command == "project shaders") {
            if (command == "project package") args.require(2, {"--runtime"});
            else if (command == "project assets") args.require(1, {"--rebuild", "--compiler"});
            else if (command == "project shaders") args.require(1, {"--rebuild", "--compiler"});
            else args.require(1, {});
            auto loaded = project::loadProject(args.positional[0]);
            if (!loaded) return fail(command, loaded.error());
            if (command == "project validate") {
                auto valid = compileScripts(*loaded);
                if (!valid) return fail(command, valid.error());
                return success(command, "{\"diagnostics\":[]}");
            }
            if (command == "project package") {
                const auto packaged = tooling::packageProject(*loaded, args.positional[1], args.get("--runtime", std::filesystem::absolute(argv[0]).string()));
                if (!packaged) return fail(command, packaged.error());
                return success(command, "{\"directory\":" + quote(packaged->generic_string()) + "}");
            }
            if (command == "project shaders" || args.has("--rebuild")) {
                auto imported = tooling::importShaders(loaded->root, args.get("--compiler", "glslc"));
                if (!imported) return fail(command, imported.error());
                if (command == "project shaders") return success(command, "{\"cache\":" + quote(imported->generic_string()) + "}");
            }
            auto index = tooling::buildAssetIndex(*loaded);
            if (!index) return fail(command, index.error());
            auto written = tooling::writeAssetIndex(*loaded, *index);
            if (!written) return fail(command, written.error());
            return success(command, "{\"assets\":" + std::to_string(index->assets.size()) + ",\"index_path\":" + quote(*written) + "}");
        }
        throw std::invalid_argument("Unknown command: " + command);
    } catch (const std::invalid_argument& error) {
        return fail(command, {{"command.arguments", project::Severity::Error, {}, {}, error.what()}}, 2);
    } catch (const std::exception& error) {
        return fail(command, {{"command.failure", project::Severity::Error, {}, {}, error.what()}}, 3);
    }
}
