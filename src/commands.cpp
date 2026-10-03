#include "commands.h"
#include "project/project.h"
#include "project/runtime.h"
#include "project/desktop_input_bridge.h"
#include "tooling/iteration.h"
#include "editor/editor.h"
#include "automation/mcp.h"
#include "platform/platform_check.h"
#include "rendering/content.h"
#include "rendering/camera.h"
#include "rendering/material.h"
#include "rendering/render_device.h"
#include "rendering/sprite2d.h"
#include "rendering/text2d.h"
#include "interaction/action_input.h"
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
#include <fstream>
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
                arg != "--compiler" && arg != "--input" && arg != "--shaders" && arg != "--user-data" && arg != "--license-root")
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
struct ContentCatalog {
    std::map<std::string, rendering::Texture2D> textures;
    std::map<std::string, rendering::FontAtlas> fonts;
    explicit ContentCatalog(const project::Project& project) {
        for (const auto& [id, asset] : project.assets) if (asset.kind == "texture") {
            const auto path = project::resolveAsset(project, id); if (!path) throw std::runtime_error(path.error().front().message);
            textures.emplace(id, rendering::loadTexture(*path));
        }
        for (const auto& [id, asset] : project.assets) if (asset.kind == "font") {
            const auto path = project::resolveAsset(project, id); if (!path) throw std::runtime_error(path.error().front().message);
            const auto header = rendering::loadFontAtlas(*path, 8192, 8192);
            const auto texture = textures.find(header.texture);
            if (texture == textures.end()) throw std::runtime_error("Font references an unknown or non-texture asset");
            fonts.emplace(id, rendering::loadFontAtlas(*path, texture->second.width, texture->second.height));
        }
    }
    float advance(std::string_view font, char32_t codepoint) const {
        const auto found = fonts.find(std::string(font)); if (found == fonts.end()) throw std::runtime_error("UI font is not loaded");
        auto glyph = found->second.glyphs.find(codepoint); if (glyph == found->second.glyphs.end()) glyph = found->second.glyphs.find(found->second.fallback);
        return glyph->second.advance;
    }
    void validateSprites(const project::Scene& scene) const {
        for (const auto& entity : scene.entities) if (entity.spriteRenderer) {
            const auto& sprite = *entity.spriteRenderer; const auto texture = textures.find(sprite.texture);
            if (texture == textures.end()) throw std::runtime_error("Sprite texture is not loaded");
            const auto valid = [&](const project::PixelRectangle& rectangle) {
                if (rectangle[0] > texture->second.width || rectangle[2] > texture->second.width - rectangle[0] ||
                    rectangle[1] > texture->second.height || rectangle[3] > texture->second.height - rectangle[1])
                    throw std::runtime_error("Sprite atlas rectangle exceeds its texture: " + entity.id);
            };
            if (sprite.source) valid(*sprite.source); for (const auto& frame : sprite.frames) valid(frame);
        }
    }
};
project::Result<void> compileScripts(const project::Project& value) {
    std::vector<std::pair<std::string, std::string>> declarations;
    std::map<std::string, std::pair<Path, std::string>> scriptOrigins;
    std::vector<project::Scene> scenes{value.scene};
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
            else if (asset.kind == "texture") (void)rendering::loadTexture(*path);
            else if (asset.kind == "audio") { const auto clip = audio::loadWav(*path); if (!clip) throw std::runtime_error(clip.error().message); }
            else if (asset.kind == "scene") { const auto scene = project::loadScene(*path, value); if (!scene) return std::unexpected(scene.error()); scenes.push_back(*scene); }
        } catch (const std::exception& error) {
            return std::unexpected(project::Diagnostics{{"asset.content.invalid", project::Severity::Error, *path, id, error.what()}});
        }
    }
    try { const ContentCatalog catalog(value); for (const auto& scene : scenes) catalog.validateSprites(scene); }
    catch (const std::exception& error) { return std::unexpected(project::Diagnostics{{"asset.font.invalid", project::Severity::Error, value.manifest, "assets", error.what()}}); }
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
    std::shared_ptr<ContentCatalog> catalog;
    std::shared_ptr<interaction::ActionMapper> mapper;
    std::set<std::string> combinedHeld;
    std::map<std::string, rendering::RenderResourceHandle> textures, meshes, dynamicMeshes;
    float wheelX = 0, wheelY = 0;
    rendering::Camera2DView view(const project::Runtime& runtime) const {
        int width = 0, height = 0; glfwGetFramebufferSize(window, &width, &height);
        const auto camera = runtime.camera().value_or(project::Camera2D{});
        return rendering::makeCamera2DView({camera.logicalSize, camera.pixelsPerUnit, camera.center, camera.pixelSnap},
            static_cast<uint32_t>(std::max(0, width)), static_cast<uint32_t>(std::max(0, height)));
    }
    rendering::RenderResourceHandle dynamicMesh(const std::string& id, const rendering::MeshAsset& mesh) {
        auto found = dynamicMeshes.find(id);
        if (found == dynamicMeshes.end()) return dynamicMeshes.emplace(id, device->createMesh(mesh.vertexData(), mesh.layout())).first->second;
        device->updateMesh(found->second, mesh.vertexData(), mesh.layout()); return found->second;
    }
public:
    Preview(const project::Project& project, const Path& shaders, std::shared_ptr<ContentCatalog> content,
            std::shared_ptr<interaction::ActionMapper> actions) : catalog(std::move(content)), mapper(std::move(actions)) {
        shader.addSpirv(rendering::ShaderStage::Vertex, shaders / "mesh_textured.vert.spv");
        shader.addSpirv(rendering::ShaderStage::Fragment, shaders / "mesh_textured.frag.spv");
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(960, 600, project.name.c_str(), nullptr, nullptr);
        if (!window) { glfwTerminate(); throw std::runtime_error("GLFW window creation failed"); }
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, [](GLFWwindow* window, double x, double y) {
            auto* owner = static_cast<Preview*>(glfwGetWindowUserPointer(window));
            owner->wheelX += static_cast<float>(x); owner->wheelY += static_cast<float>(y);
        });
        try {
            surface = std::make_unique<vulkan::VulkanGlfwSurfaceProvider>(window);
            device = std::make_unique<rendering::RenderDevice>(std::make_unique<vulkan::VulkanFrameBackend>(*surface));
            for (const auto feature : std::array{rendering::RenderFeature::BackendAvailable, rendering::RenderFeature::SampledTextures, rendering::RenderFeature::DepthAttachment}) {
                const auto status = device->capabilities().feature(feature);
                if (!status.supported) throw std::runtime_error(std::string("Authored preview requires ") + status.name + ": " + status.reason);
            }
            shaderHandle = device->createShader(shader);
            textures.emplace("__white", device->createTexture({1, 1, {255,255,255,255}}));
            for (const auto& [id, texture] : catalog->textures) textures.emplace(id, device->createTexture(texture));
            for (const auto& [id, asset] : project.assets) if (asset.kind == "mesh") {
                const auto path = project::resolveAsset(project, id); if (!path) throw std::runtime_error(path.error().front().message);
                const auto mesh = rendering::loadMeshAsset(*path); meshes.emplace(id, device->createMesh(mesh.vertexData(), mesh.layout()));
            }
        } catch (...) { device.reset(); surface.reset(); glfwDestroyWindow(window); window = nullptr; glfwTerminate(); throw; }
    }
    ~Preview() {
        try { if (device) device->shutdown(); } catch (const std::exception& error) { std::cerr << "Renderer shutdown: " << error.what() << '\n'; }
        device.reset(); surface.reset(); if (window) glfwDestroyWindow(window); glfwTerminate();
    }
    void finish() { if (device) { device->waitIdle(); device->shutdown(); } }
    bool sample(project::InputSnapshot& input, const project::Runtime& runtime) {
        glfwPollEvents(); if (glfwWindowShouldClose(window)) return false;
        interaction::RawInput raw; raw.focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
        raw.wheelX = wheelX; raw.wheelY = wheelY; wheelX = wheelY = 0;
        GLFWgamepadstate gamepad{};
        for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id)
            if (glfwJoystickIsGamepad(id) && glfwGetGamepadState(id, &gamepad)) { raw.gamepadConnected = true; break; }
        for (const auto& binding : interaction::bindingRegistry()) {
            if (binding.device == interaction::Device::Keyboard && glfwGetKey(window, binding.code) == GLFW_PRESS) raw.down.insert(binding.token);
            else if (binding.device == interaction::Device::MouseButton && glfwGetMouseButton(window, binding.code) == GLFW_PRESS) raw.down.insert(binding.token);
            else if (binding.device == interaction::Device::GamepadButton && raw.gamepadConnected && gamepad.buttons[binding.code] == GLFW_PRESS) raw.down.insert(binding.token);
        }
        if (raw.gamepadConnected) {
            const std::string names[]{"LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger"};
            for (int axis = 0; axis < 6; ++axis) raw.axes[names[axis]] = axis < 4 ? gamepad.axes[axis] : (gamepad.axes[axis] + 1) * 0.5f;
        }
        double x = 0, y = 0; int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetCursorPos(window, &x, &y); glfwGetWindowSize(window, &ww, &wh); glfwGetFramebufferSize(window, &fw, &fh);
        const auto cameraView = view(runtime);
        const auto point=project::desktop::logicalPointer(x,y,ww,wh,fw,fh,cameraView.framebufferViewport,
            runtime.camera().value_or(project::Camera2D{}).logicalSize);
        raw.pointerX=point[0];raw.pointerY=point[1];
        const auto frame = mapper->sample(raw);
        input=project::desktop::mergeInput(frame,input,combinedHeld);
        return true;
    }
    void draw(const project::Runtime& runtime) {
        const auto cameraView = view(runtime); if (cameraView.scale == 0) return;
        const auto cameraDescription = runtime.camera().value_or(project::Camera2D{});
        auto frame = device->makeFrame(); std::set<std::string> used;
        const auto record = [&](rendering::RenderResourceHandle mesh, rendering::RenderResourceHandle texture,
                const rendering::CameraUniform& camera, rendering::DrawState state, const std::array<float,4>& color,
                const std::array<float,16>& model) {
            rendering::DrawCommand draw{shaderHandle, mesh, texture}; draw.state = state;
            rendering::MaterialUniform material; material.baseColor = color;
            rendering::appendUniform(draw, "model", model, {0,0}); rendering::appendUniform(draw, "material", material, {0,1}); rendering::appendUniform(draw, "camera", camera, {0,2}); frame->record(std::move(draw));
        };
        const std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (const auto& entity : runtime.activeScene().entities) if (entity.meshRenderer) {
            const auto position = runtime.position(entity.id); if (!position) continue;
            auto camera = cameraView.camera;
            if (!runtime.camera()) camera.viewProjection = {0.5f,0,0,0, 0,-0.8f,0,0, 0,0,-0.1f,0, 0,0,0.5f,1};
            auto model = identity; model[12] = (*position)[0]; model[13] = (*position)[1]; model[14] = (*position)[2];
            record(meshes.at(entity.meshRenderer->mesh), textures.at(entity.meshRenderer->texture.value_or("__white")), camera, {}, {1,1,1,1}, model);
        }
        for (const auto& sprite : runtime.sprites()) {
            const auto& texture = catalog->textures.at(sprite.sprite.texture);
            project::PixelRectangle source{0,0,texture.width,texture.height};
            if (!sprite.sprite.frames.empty()) source = sprite.sprite.frames.at(sprite.frame);
            else if (sprite.sprite.source) source = *sprite.sprite.source;
            const rendering::Sprite2DDescription description{{sprite.position[0],sprite.position[1]},sprite.sprite.size,sprite.sprite.pivot,{source[0],source[1],source[2],source[3]},sprite.sprite.tint,true};
            const auto geometry = rendering::makeSpriteQuad(description, texture.width, texture.height, cameraDescription.pixelsPerUnit, cameraDescription.pixelSnap);
            const auto key = "sprite:" + sprite.id; used.insert(key);
            record(dynamicMesh(key, geometry), textures.at(sprite.sprite.texture), cameraView.camera, rendering::painter2DState(cameraView), sprite.sprite.tint, identity);
        }
        const auto uiCamera = rendering::makeLogicalUiCamera(cameraDescription.logicalSize[0],cameraDescription.logicalSize[1]);
        auto panels = runtime.panels(); std::stable_sort(panels.begin(), panels.end(), [](const auto& a, const auto& b) { return a.layer < b.layer; });
        for (const auto& panel : panels) {
            const auto& font = catalog->fonts.at(panel.fontAsset); const auto& texture = catalog->textures.at(font.texture);
            rendering::MeshAsset background;
            const float x = panel.rect.x, y = panel.rect.y, w = panel.rect.width, h = panel.rect.height;
            background.vertices = {{{x,y,0},{0,0}},{{x+w,y,0},{1,0}},{{x,y+h,0},{0,1}},{{x,y+h,0},{0,1}},{{x+w,y,0},{1,0}},{{x+w,y+h,0},{1,1}}};
            const auto backgroundKey = "panel:" + panel.id; used.insert(backgroundKey);
            record(dynamicMesh(backgroundKey,background),textures.at("__white"),uiCamera,rendering::painter2DState(cameraView),{0.025f,0.035f,0.06f,0.94f},identity);
            std::string text;
            for (size_t index = 0; index < panel.lines.size(); ++index) { if (index) text += '\n'; text += panel.lines[index]; }
            for (size_t index = 0; index < panel.choices.size(); ++index) { text += '\n'; text += panel.focused && index == panel.selected ? "> " : "  "; text += panel.choices[index]; }
            rendering::Text2DDescription description{text,{0,0},0,panel.scrollOffset / panel.scale,
                rendering::LogicalRect{0,0,panel.clip.width / panel.scale,panel.clip.height / panel.scale}};
            auto geometry = rendering::makeTextGeometry(font,texture.width,texture.height,description).mesh;
            for (auto& vertex : geometry.vertices) { vertex.position[0] = vertex.position[0] * panel.scale + panel.rect.x; vertex.position[1] = vertex.position[1] * panel.scale + panel.rect.y; }
            if (!geometry.vertices.empty()) { const auto key = "text:" + panel.id; used.insert(key); record(dynamicMesh(key,geometry),textures.at(font.texture),uiCamera,rendering::painter2DState(cameraView),panel.color,identity); }
        }
        device->submit(frame);
        for (auto item = dynamicMeshes.begin(); item != dynamicMeshes.end();) {
            if (!used.contains(item->first)) { device->destroy(item->second); item = dynamicMeshes.erase(item); } else ++item;
        }
    }
};
#endif
int runProject(const Arguments& args) {
    args.require(1, {"--headless", "--ticks", "--input", "--shaders", "--user-data"});
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
            if (!loaded->inputActions.contains(event.substr(0, colon)) && !loaded->inputBindings.contains(event.substr(0, colon)))
                return fail("run", {{"command.input.unknown", project::Severity::Error, loaded->manifest, "--input", "Input action is not declared by the project: " + event.substr(0, colon)}}, 2);
            const unsigned tick = number(event.substr(colon + 1), 0, ticks - 1);
            inputs[tick].pressed.insert(event.substr(0, colon));
            inputs[tick].held.insert(event.substr(0, colon));
            if (tick + 1 < ticks) inputs[tick + 1].released.insert(event.substr(0, colon));
        }
    }
    auto catalog = std::make_shared<ContentCatalog>(*loaded);
    interaction::BindingMap bindings;
    for (const auto& [action, key] : loaded->inputActions) bindings[action] = {"Key:" + key};
    for (const auto& [action, tokens] : loaded->inputBindings) bindings[action] = tokens;
    auto mapper = std::make_shared<interaction::ActionMapper>(std::move(bindings));
    std::uint64_t profileHash = 14695981039346656037ull;
    for (const unsigned char character : loaded->name) { profileHash ^= character; profileHash *= 1099511628211ull; }
    const auto projectId = "project-" + std::to_string(profileHash);
    auto root = args.has("--user-data") ? project::Result<Path>(Path(args.get("--user-data"))) : project::PersistenceStore::defaultUserDataRoot(projectId);
    if (!root) return fail("run", root.error(), 3);
    auto persistence = std::make_shared<project::PersistenceStore>(project::PersistenceOptions{*root, projectId});
    bool playback = false;
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    playback = !args.has("--headless") && std::any_of(loaded->assets.begin(), loaded->assets.end(), [](const auto& asset) { return asset.second.kind == "audio"; });
#endif
    auto audioSystem = std::make_shared<audio::AudioSystem>(audio::DeviceOptions{playback});
    const auto audioReady = audioSystem->initialize();
    if (!audioReady) return fail("run", {{audioReady.error().code, project::Severity::Error, loaded->manifest, "audio", audioReady.error().message}}, 3);
    project::RuntimeOptions options; options.audio = audioSystem; options.persistence = persistence; options.inputMapper = mapper;
    options.glyphAdvance = [catalog](std::string_view font, char32_t codepoint) { return catalog->advance(font, codepoint); };
    options.fontLineHeight = [catalog](std::string_view font) { return catalog->fonts.at(std::string(font)).lineHeight; };
    project::Runtime runtime(*loaded, std::move(options));
    auto started = runtime.start();
    if (!started) return fail("run", started.error());
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    std::unique_ptr<Preview> preview;
    if (!args.has("--headless")) preview = std::make_unique<Preview>(*loaded, args.get("--shaders", "build/debug-vk1/shaders"), catalog, mapper);
#endif
    std::array<float, 1600> offlineAudio{}; // 800 stereo samples per authored 60 Hz tick at 48 kHz.
    for (unsigned tick = 0; tick < ticks; ++tick) {
        const auto next = std::chrono::steady_clock::now() + std::chrono::microseconds(16667);
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (preview && !preview->sample(inputs[tick], runtime)) break;
#endif
        const auto advanced = runtime.tick(inputs[tick]);
        if (!advanced) return fail("run", advanced.error(), 3);
        if (!playback) audioSystem->mix(offlineAudio);
        if (!audioSystem->deviceError().empty()) return fail("run", {{"audio.device.failure", project::Severity::Error, loaded->manifest, "audio", audioSystem->deviceError()}}, 3);
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (preview) { preview->draw(runtime); std::this_thread::sleep_until(next); }
#endif
    }
    auto scene = runtime.activeScene();
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
            if (command == "project package") args.require(2, {"--runtime","--shaders","--license-root"});
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
                Path licenseRoot=args.get("--license-root");
                if(licenseRoot.empty())
                {
                    auto ancestor=std::filesystem::absolute(argv[0]).parent_path();
                    for(unsigned level=0;level<8&&!ancestor.empty();++level)
                    {
                        if(std::filesystem::is_regular_file(ancestor/"LICENSE")&&
                            std::filesystem::is_regular_file(ancestor/"third_party/stb/LICENSE")&&
                            std::filesystem::is_regular_file(ancestor/"third_party/lua/lua.h")) {licenseRoot=ancestor;break;}
                        const auto parent=ancestor.parent_path();if(parent==ancestor) break;ancestor=parent;
                    }
                    if(licenseRoot.empty()) throw std::invalid_argument("project package requires --license-root pointing to the actual engine source checkout");
                }
                const auto packaged = tooling::packageProject(*loaded, args.positional[1], args.get("--runtime", std::filesystem::absolute(argv[0]).string()),args.get("--shaders"),licenseRoot);
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
