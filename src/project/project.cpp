#include "project.h"

#include <algorithm>
#include <cwctype>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <thread>
#include <type_traits>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstdio>
#endif

#include "../../third_party/picojson/picojson.h"

namespace project
{
    namespace
    {
        using Object = picojson::object;
        using Value = picojson::value;

        void add(Diagnostics& diagnostics, std::string code, const std::filesystem::path& file,
            std::string path, std::string message)
        {
            diagnostics.push_back({std::move(code), Severity::Error, file, std::move(path), std::move(message)});
        }

        std::filesystem::path canonicalPath(const std::filesystem::path& path)
        {
            return std::filesystem::canonical(path);
        }

        std::filesystem::path relativePath(const std::filesystem::path& root, const std::filesystem::path& file)
        {
            const auto relative = file.lexically_relative(root);
            return relative.empty() ? file : relative;
        }

        bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& target)
        {
            const auto relative = target.lexically_relative(root);
            return !relative.empty() && *relative.begin() != ".." && !relative.is_absolute();
        }

        std::string folded(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        bool resolveFile(const std::filesystem::path& root, const std::string& authored,
            const std::filesystem::path& sourceFile, const std::string& field,
            std::filesystem::path& result, Diagnostics& diagnostics)
        {
            const std::filesystem::path relative(authored);
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
            {
                add(diagnostics, "project.path.invalid", sourceFile, field, "Expected a non-empty project-relative file path");
                return false;
            }
            const auto lexical = (root / relative).lexically_normal();
            if (!withinRoot(root, lexical))
            {
                add(diagnostics, "project.path.outside_root", sourceFile, field,
                    "Referenced file must resolve inside the project root");
                return false;
            }
            try
            {
                const auto resolved = canonicalPath(root / relative);
                if (!withinRoot(root, resolved) || !std::filesystem::is_regular_file(resolved))
                {
                    add(diagnostics, "project.path.outside_root", sourceFile, field,
                        "Referenced file must exist inside the project root");
                    return false;
                }
                result = resolved;
                return true;
            }
            catch (const std::filesystem::filesystem_error&)
            {
                add(diagnostics, "project.asset.missing", sourceFile, field, "Referenced file does not exist");
                return false;
            }
        }

        bool readJson(const std::filesystem::path& file, const std::filesystem::path& root,
            Value& value, Diagnostics& diagnostics)
        {
            const auto displayFile = relativePath(root, file);
            std::ifstream input(file, std::ios::binary);
            if (!input)
            {
                add(diagnostics, "project.file.read", displayFile, "", "Unable to open document");
                return false;
            }
            const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (input.bad())
            {
                add(diagnostics, "project.file.read", displayFile, "", "Unable to read document");
                return false;
            }
            std::string error;
            auto end = text.end();
            try
            {
                end = picojson::parse(value, text.begin(), text.end(), &error);
            }
            catch (const std::exception&)
            {
                add(diagnostics, "project.document.malformed", displayFile, "", "JSON number or value is outside the parser range");
                return false;
            }
            while (end != text.end() && std::isspace(static_cast<unsigned char>(*end))) ++end;
            if (!error.empty() || end != text.end())
            {
                add(diagnostics, "project.document.malformed", displayFile, "", error.empty() ? "Unexpected trailing data" : error);
                return false;
            }
            return true;
        }

        const Object* object(const Value& value)
        { return value.is<Object>() ? &value.get<Object>() : nullptr; }

        const Value* member(const Object& value, const char* key)
        {
            const auto found = value.find(key);
            return found == value.end() ? nullptr : &found->second;
        }

        bool stringField(const Object& value, const char* key, const std::filesystem::path& file,
            const std::string& path, std::string& result, Diagnostics& diagnostics)
        {
            const auto* item = member(value, key);
            if (!item || !item->is<std::string>())
            {
                add(diagnostics, "project.field.type", file, path + "." + key, "Expected a string");
                return false;
            }
            result = item->get<std::string>();
            if (result.empty())
            {
                add(diagnostics, "project.field.empty", file, path + "." + key, "Value must not be empty");
                return false;
            }
            return true;
        }

        bool schemaField(const Object& value, const std::filesystem::path& file,
            const std::string& path, unsigned supported, Diagnostics& diagnostics)
        {
            const auto* item = member(value, "schema");
            if (!item || !item->is<double>() || !std::isfinite(item->get<double>()) ||
                std::floor(item->get<double>()) != item->get<double>() || item->get<double>() < 1)
            {
                add(diagnostics, "project.schema.invalid", file, path + ".schema", "Expected a positive integer schema version");
                return false;
            }
            if (item->get<double>() > std::numeric_limits<unsigned>::max())
            {
                add(diagnostics, "project.schema.unsupported", file, path + ".schema", "Schema version is not supported");
                return false;
            }
            const auto version = static_cast<unsigned>(item->get<double>());
            if (version != supported)
            {
                add(diagnostics, "project.schema.unsupported", file, path + ".schema",
                    version > supported ? "Document uses a newer unsupported schema version" : "Document schema version is too old");
                return false;
            }
            return true;
        }

        void checkFields(const Object& value, const std::set<std::string>& allowed,
            const std::filesystem::path& file, const std::string& path, Diagnostics& diagnostics)
        {
            for (const auto& [name, _] : value)
                if (!allowed.contains(name))
                    add(diagnostics, "project.field.unknown", file, path.empty() ? name : path + "." + name,
                        "Unknown field '" + name + "'");
        }

        bool parseTransform(const Value& value, const std::filesystem::path& file,
            const std::string& path, Transform& transform, Diagnostics& diagnostics)
        {
            const auto* fields = object(value);
            if (!fields)
            {
                add(diagnostics, "project.component.type", file, path, "Transform must be an object");
                return false;
            }
            checkFields(*fields, {"position"}, file, path, diagnostics);
            const auto* position = member(*fields, "position");
            if (!position || !position->is<picojson::array>() || position->get<picojson::array>().size() != 3)
            {
                add(diagnostics, "project.transform.position", file, path + ".position", "Expected three finite numbers");
                return false;
            }
            bool valid = true;
            for (size_t axis = 0; axis != 3; ++axis)
            {
                const auto& coordinate = position->get<picojson::array>()[axis];
                if (!coordinate.is<double>() || !std::isfinite(coordinate.get<double>()) ||
                    std::abs(coordinate.get<double>()) > std::numeric_limits<float>::max())
                {
                    add(diagnostics, "project.transform.position", file,
                        path + ".position[" + std::to_string(axis) + "]", "Coordinate must be a finite float");
                    valid = false;
                    continue;
                }
                transform.position[axis] = static_cast<float>(coordinate.get<double>());
            }
            return valid;
        }

        std::string quote(std::string_view value)
        {
            std::string out = "\"";
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (character < 0x20)
                    {
                        constexpr char hex[] = "0123456789abcdef";
                        out += "\\u00";
                        out += hex[character >> 4];
                        out += hex[character & 15];
                    }
                    else out += static_cast<char>(character);
                }
            }
            out += '"';
            return out;
        }

        std::string serialize(const Scene& scene)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << "{\"schema\":" << scene.schema << ",\"scene_id\":" << quote(scene.id) << ",\"entities\":[";
            for (size_t index = 0; index < scene.entities.size(); ++index)
            {
                if (index) out << ',';
                const auto& entity = scene.entities[index];
                out << "{\"id\":" << quote(entity.id) << ",\"name\":" << quote(entity.name) << ",\"components\":{";
                bool hasComponent = false;
                if (entity.transform)
                {
                    const auto& p = entity.transform->position;
                    out << "\"Transform\":{\"position\":[" << std::setprecision(std::numeric_limits<float>::max_digits10)
                        << p[0] << ',' << p[1] << ',' << p[2] << "]}";
                    hasComponent = true;
                }
                if (entity.script)
                {
                    if (hasComponent) out << ',';
                    out << "\"Script\":{\"asset\":" << quote(entity.script->asset) << '}';
                    hasComponent = true;
                }
                if (entity.meshRenderer)
                {
                    if (hasComponent) out << ',';
                    out << "\"MeshRenderer\":{\"mesh\":" << quote(entity.meshRenderer->mesh);
                    if (entity.meshRenderer->texture) out << ",\"texture\":" << quote(*entity.meshRenderer->texture);
                    out << '}';
                }
                out << "}}";
            }
            out << "]}";
            return out.str();
        }

        Diagnostics one(std::string code, const std::filesystem::path& file, std::string path, std::string message)
        {
            Diagnostics result;
            add(result, std::move(code), file, std::move(path), std::move(message));
            return result;
        }

        bool revisionMatches(const std::string& actual, const std::string& expected,
            const std::filesystem::path& file, Diagnostics& diagnostics)
        {
            if (actual == expected) return true;
            add(diagnostics, "scene.revision.stale", file, "", "Scene revision changed; reload before applying this operation");
            return false;
        }

        bool onOwnerThread(const std::thread::id owner, const std::filesystem::path& file, Diagnostics& diagnostics)
        {
            if (std::this_thread::get_id() == owner) return true;
            add(diagnostics, "project.thread.owner", file, "", "Scene session mutation must run on its owner thread");
            return false;
        }

        bool applyToScene(Scene& scene, const std::filesystem::path& root, const std::map<std::string, Asset, std::less<>>& assets,
            const EditOperation& operation, Diagnostics& diagnostics)
        {
            return std::visit([&](const auto& command)
            {
                using Command = std::decay_t<decltype(command)>;
                const auto entity = std::find_if(scene.entities.begin(), scene.entities.end(),
                    [&](const SceneEntity& value) { return value.id == command.entityId; });
                if (entity == scene.entities.end())
                {
                    add(diagnostics, "scene.entity.missing", scene.source, "entities", "Persistent entity id was not found");
                    return false;
                }
                if constexpr (std::is_same_v<Command, SetPosition>)
                {
                    if (!entity->transform)
                    {
                        add(diagnostics, "scene.entity.transform.missing", scene.source, "entities", "Entity has no Transform component");
                        return false;
                    }
                    for (const float value : command.position)
                        if (!std::isfinite(value))
                        {
                            add(diagnostics, "scene.transform.position", scene.source, "entities", "Position must contain finite values");
                            return false;
                        }
                    entity->transform->position = command.position;
                    return true;
                }
                else if constexpr (std::is_same_v<Command, SetScript>)
                {
                    if (!command.asset)
                    {
                        entity->script.reset();
                        return true;
                    }
                    const auto asset = assets.find(*command.asset);
                    if (asset == assets.end() || asset->second.kind != "script")
                    {
                        add(diagnostics, "project.asset.unknown", scene.source, "entities.Script.asset", "Script asset ID is not declared as a script asset");
                        return false;
                    }
                    std::filesystem::path resolved;
                    if (!resolveFile(root, asset->second.path.generic_string(), scene.source, "entities.Script.asset", resolved, diagnostics)) return false;
                    entity->script = Script{*command.asset};
                    return true;
                }
            }, operation);
        }
    }

    Result<Scene> loadScene(const std::filesystem::path& sceneFile, const std::filesystem::path& projectRoot)
    {
        Diagnostics diagnostics;
        std::filesystem::path root;
        std::filesystem::path file;
        try
        {
            root = canonicalPath(projectRoot);
            file = canonicalPath(sceneFile.is_absolute() ? sceneFile : root / sceneFile);
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return std::unexpected(one("project.file.missing", sceneFile, "", "Project or scene file does not exist"));
        }
        if (!withinRoot(root, file))
            return std::unexpected(one("project.path.outside_root", sceneFile, "", "Scene file must be inside the project root"));

        Value document;
        if (!readJson(file, root, document, diagnostics)) return std::unexpected(std::move(diagnostics));
        const auto* fields = object(document);
        const auto displayFile = relativePath(root, file);
        if (!fields)
            return std::unexpected(one("project.document.type", displayFile, "", "Scene document must be an object"));

        checkFields(*fields, {"schema", "scene_id", "entities"}, displayFile, "", diagnostics);
        Scene scene;
        scene.source = relativePath(root, file);
        schemaField(*fields, displayFile, "", SceneSchemaVersion, diagnostics);
        stringField(*fields, "scene_id", displayFile, "", scene.id, diagnostics);

        const auto* entities = member(*fields, "entities");
        if (!entities || !entities->is<picojson::array>())
            add(diagnostics, "project.field.type", displayFile, "entities", "Expected an array");
        else
        {
            std::unordered_set<std::string> identifiers;
            const auto& list = entities->get<picojson::array>();
            scene.entities.reserve(list.size());
            for (size_t index = 0; index < list.size(); ++index)
            {
                const std::string path = "entities[" + std::to_string(index) + "]";
                const auto* entityFields = object(list[index]);
                if (!entityFields)
                {
                    add(diagnostics, "project.entity.type", displayFile, path, "Entity must be an object");
                    continue;
                }
                checkFields(*entityFields, {"id", "name", "components"}, displayFile, path, diagnostics);
                SceneEntity entity;
                const bool idValid = stringField(*entityFields, "id", displayFile, path, entity.id, diagnostics);
                stringField(*entityFields, "name", displayFile, path, entity.name, diagnostics);
                if (idValid && !identifiers.insert(entity.id).second)
                    add(diagnostics, "scene.entity.id.duplicate", displayFile, path + ".id", "Persistent entity id is duplicated");

                const auto* componentsValue = member(*entityFields, "components");
                const auto* components = componentsValue ? object(*componentsValue) : nullptr;
                if (!components)
                {
                    add(diagnostics, "project.field.type", displayFile, path + ".components", "Expected a component object");
                    scene.entities.push_back(std::move(entity));
                    continue;
                }
                checkFields(*components, {"Transform", "Script", "MeshRenderer"}, displayFile, path + ".components", diagnostics);
                if (const auto* transform = member(*components, "Transform"))
                {
                    Transform parsed;
                    if (parseTransform(*transform, displayFile, path + ".components.Transform", parsed, diagnostics))
                        entity.transform = parsed;
                }
                if (const auto* scriptValue = member(*components, "Script"))
                {
                    const auto* scriptFields = object(*scriptValue);
                    if (!scriptFields)
                        add(diagnostics, "project.component.type", displayFile, path + ".components.Script", "Script must be an object");
                    else
                    {
                        checkFields(*scriptFields, {"asset"}, displayFile, path + ".components.Script", diagnostics);
                        std::string authored;
                        if (stringField(*scriptFields, "asset", displayFile, path + ".components.Script", authored, diagnostics))
                        {
                            entity.script = Script{authored};
                        }
                    }
                }
                if (const auto* meshValue = member(*components, "MeshRenderer"))
                {
                    const auto* meshFields = object(*meshValue);
                    if (!meshFields) add(diagnostics, "project.component.type", displayFile, path + ".components.MeshRenderer", "MeshRenderer must be an object");
                    else
                    {
                        checkFields(*meshFields, {"mesh", "texture"}, displayFile, path + ".components.MeshRenderer", diagnostics);
                        MeshRenderer renderer;
                        const bool meshValid = stringField(*meshFields, "mesh", displayFile, path + ".components.MeshRenderer", renderer.mesh, diagnostics);
                        if (const auto* textureValue = member(*meshFields, "texture"))
                        {
                            if (!textureValue->is<std::string>() || textureValue->get<std::string>().empty())
                                add(diagnostics, "project.field.type", displayFile, path + ".components.MeshRenderer.texture", "Expected a non-empty texture asset ID");
                            else renderer.texture = textureValue->get<std::string>();
                        }
                        if (meshValid) entity.meshRenderer = std::move(renderer);
                    }
                }
                scene.entities.push_back(std::move(entity));
            }
        }
        if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        return scene;
    }

    Result<std::filesystem::path> resolveAsset(const Project& project, const std::string_view assetId)
    {
        const auto found = project.assets.find(assetId);
        if (found == project.assets.end())
            return std::unexpected(one("project.asset.unknown", project.manifest, "assets", "Asset ID is not declared in the project catalog"));
        std::filesystem::path resolved;
        Diagnostics diagnostics;
        if (!resolveFile(project.root, found->second.path.generic_string(), project.manifest,
            "assets[" + found->second.id + "].path", resolved, diagnostics))
            return std::unexpected(std::move(diagnostics));
        return resolved;
    }

    Result<Scene> loadScene(const std::filesystem::path& sceneFile, const Project& project)
    {
        auto scene = loadScene(sceneFile, project.root);
        if (!scene) return std::unexpected(scene.error());
        Diagnostics diagnostics;
        for (size_t i = 0; i < scene->entities.size(); ++i)
        {
            const auto& renderer = scene->entities[i].meshRenderer;
            if (renderer)
            {
                const auto mesh = project.assets.find(renderer->mesh);
                const auto meshField = "entities[" + std::to_string(i) + "].components.MeshRenderer.mesh";
                if (mesh == project.assets.end())
                    add(diagnostics, "project.asset.unknown", scene->source, meshField, "Mesh asset ID is not declared in the project catalog");
                else if (mesh->second.kind != "mesh")
                    add(diagnostics, "project.asset.kind.mismatch", scene->source, meshField, "MeshRenderer.mesh must reference an asset of kind mesh");
                else
                {
                    std::filesystem::path resolved;
                    resolveFile(project.root, mesh->second.path.generic_string(), scene->source, meshField, resolved, diagnostics);
                }
                if (renderer->texture)
                {
                    const auto texture = project.assets.find(*renderer->texture);
                    const auto textureField = "entities[" + std::to_string(i) + "].components.MeshRenderer.texture";
                    if (texture == project.assets.end())
                        add(diagnostics, "project.asset.unknown", scene->source, textureField, "Texture asset ID is not declared in the project catalog");
                    else if (texture->second.kind != "texture")
                        add(diagnostics, "project.asset.kind.mismatch", scene->source, textureField, "MeshRenderer.texture must reference an asset of kind texture");
                    else
                    {
                        std::filesystem::path resolved;
                        resolveFile(project.root, texture->second.path.generic_string(), scene->source, textureField, resolved, diagnostics);
                    }
                }
            }
            const auto& script = scene->entities[i].script;
            if (!script) continue;
            const auto found = project.assets.find(script->asset);
            const auto path = "entities[" + std::to_string(i) + "].components.Script.asset";
            if (found == project.assets.end())
                add(diagnostics, "project.asset.unknown", scene->source, path, "Script asset ID is not declared in the project catalog");
            else if (found->second.kind != "script")
                add(diagnostics, "project.asset.kind.mismatch", scene->source, path, "Script component must reference an asset of kind script");
            else
            {
                std::filesystem::path resolved;
                resolveFile(project.root, found->second.path.generic_string(), scene->source, path, resolved, diagnostics);
            }
        }
        if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        return scene;
    }

    Result<Project> loadProject(const std::filesystem::path& manifest)
    {
        std::filesystem::path file;
        try { file = canonicalPath(manifest); }
        catch (const std::filesystem::filesystem_error&)
        { return std::unexpected(one("project.file.missing", manifest, "", "Project manifest does not exist")); }
        const auto root = file.parent_path();
        Diagnostics diagnostics;
        Value document;
        if (!readJson(file, root, document, diagnostics)) return std::unexpected(std::move(diagnostics));
        const auto* fields = object(document);
        const auto displayFile = relativePath(root, file);
        if (!fields)
            return std::unexpected(one("project.document.type", displayFile, "", "Project manifest must be an object"));

        checkFields(*fields, {"schema", "name", "startup_scene", "assets", "inputActions"}, displayFile, "", diagnostics);
        Project project;
        project.root = root;
        project.manifest = relativePath(root, file);
        schemaField(*fields, displayFile, "", ProjectSchemaVersion, diagnostics);
        stringField(*fields, "name", displayFile, "", project.name, diagnostics);
        if (const auto* assetsValue = member(*fields, "assets"))
        {
            if (!assetsValue->is<picojson::array>())
                add(diagnostics, "project.field.type", displayFile, "assets", "Expected an array");
            else
            {
                std::set<std::string> ids;
                std::set<std::string> paths;
                const auto& list = assetsValue->get<picojson::array>();
                for (size_t i = 0; i < list.size(); ++i)
                {
                    const auto path = "assets[" + std::to_string(i) + "]";
                    const auto* entry = object(list[i]);
                    if (!entry) { add(diagnostics, "project.asset.type", displayFile, path, "Asset entry must be an object"); continue; }
                    checkFields(*entry, {"id", "path", "kind"}, displayFile, path, diagnostics);
                    Asset asset;
                    bool valid = stringField(*entry, "id", displayFile, path, asset.id, diagnostics);
                    std::string authoredPath;
                    valid &= stringField(*entry, "path", displayFile, path, authoredPath, diagnostics);
                    valid &= stringField(*entry, "kind", displayFile, path, asset.kind, diagnostics);
                    if (!valid) continue;
                    const auto foldedId = folded(asset.id);
                    if (!ids.insert(foldedId).second)
                        add(diagnostics, "project.asset.id.duplicate", displayFile, path + ".id", "Asset ID is duplicated (IDs are case-insensitive)");
                    if (asset.kind != "script" && asset.kind != "mesh" && asset.kind != "texture")
                        add(diagnostics, "project.asset.kind.unsupported", displayFile, path + ".kind", "Supported asset kinds are script, mesh, and texture");
                    asset.path = std::filesystem::path(authoredPath).lexically_normal();
                    const auto foldedPath = folded(asset.path.generic_string());
                    if (!paths.insert(foldedPath).second)
                        add(diagnostics, "project.asset.path.duplicate", displayFile, path + ".path", "Asset path collides with another declared path (paths are case-insensitive)");
                    std::filesystem::path resolved;
                    if (!resolveFile(root, authoredPath, displayFile, path + ".path", resolved, diagnostics)) continue;
                    project.assets.emplace(asset.id, std::move(asset));
                }
            }
        }
        if (const auto* actionsValue = member(*fields, "inputActions"))
        {
            const auto* actions = object(*actionsValue);
            if (!actions) add(diagnostics, "project.field.type", displayFile, "inputActions", "Expected an object mapping action names to key names");
            else for (const auto& [name, keyValue] : *actions)
            {
                if (name.empty() || !keyValue.is<std::string>() || keyValue.get<std::string>().empty())
                {
                    add(diagnostics, "project.input_action.invalid", displayFile, "inputActions." + name, "Action and key names must be non-empty strings");
                    continue;
                }
                const auto& key = keyValue.get<std::string>();
                if (key != "Right" && key != "Left" && key != "Up" && key != "Down" && key != "Space")
                {
                    add(diagnostics, "project.input_action.key.unsupported", displayFile, "inputActions." + name, "Supported keys are Right, Left, Up, Down, and Space");
                    continue;
                }
                project.inputActions.emplace(name, key);
            }
        }
        std::string startup;
        if (stringField(*fields, "startup_scene", displayFile, "", startup, diagnostics))
        {
            if (resolveFile(root, startup, displayFile, "startup_scene", project.startupScene, diagnostics))
            {
                auto scene = loadScene(project.startupScene, project);
                if (!scene) diagnostics.insert(diagnostics.end(), scene.error().begin(), scene.error().end());
                else project.scene = std::move(*scene);
            }
        }
        if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        return project;
    }

    Result<void> validateProject(const std::filesystem::path& manifest)
    {
        auto project = loadProject(manifest);
        if (!project) return std::unexpected(project.error());
        return {};
    }

    Result<Scene> inspectScene(const std::filesystem::path& sceneFile, const std::filesystem::path& projectRoot)
    { return loadScene(sceneFile, projectRoot); }

    Result<Scene> inspectScene(const std::filesystem::path& sceneFile, const Project& project)
    { return loadScene(sceneFile, project); }

    std::string diagnosticsJson(const Diagnostics& diagnostics)
    {
        std::ostringstream out;
        out << '[';
        for (size_t i = 0; i < diagnostics.size(); ++i)
        {
            if (i) out << ',';
            const auto& d = diagnostics[i];
            out << "{\"code\":" << quote(d.code) << ",\"severity\":" << quote(d.severity == Severity::Error ? "error" : "warning")
                << ",\"file\":" << quote(d.file.generic_string()) << ",\"path\":" << quote(d.path)
                << ",\"message\":" << quote(d.message) << '}';
        }
        out << ']';
        return out.str();
    }

    std::string sceneJson(const Scene& scene, const std::string& revision)
    {
        std::string document = serialize(scene);
        if (revision.empty()) return document;
        document.pop_back();
        return document + ",\"revision\":" + quote(revision) + '}';
    }

    std::string resultJson(const bool ok, const std::string_view command, const std::string_view payload)
    {
        std::string result = "{\"schema\":1,\"ok\":";
        result += ok ? "true" : "false";
        result += ",\"command\":" + quote(command);
        if (!payload.empty()) result += ",\"result\":" + std::string(payload);
        result += '}';
        return result;
    }

    std::string revisionFor(const Scene& scene)
    {
        const std::string text = serialize(scene);
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char value : text) { hash ^= value; hash *= 1099511628211ull; }
        std::ostringstream out;
        out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
        return out.str();
    }

    struct SceneSession::Impl
    {
        Scene current;
        std::filesystem::path root;
        std::map<std::string, Asset, std::less<>> assets;
        std::string revision;
        std::string savedRevision;
        std::vector<Scene> undo;
        std::vector<Scene> redo;
        const std::thread::id ownerThread = std::this_thread::get_id();

        Impl(Scene initial, std::filesystem::path projectRoot, std::map<std::string, Asset, std::less<>> catalog = {})
            : current(std::move(initial)), root(std::move(projectRoot)), assets(std::move(catalog)), revision(revisionFor(current)), savedRevision(revision)
        {}

        std::filesystem::path file() const { return root / current.source; }
        bool currentRevision(const std::string& expected, Diagnostics& diagnostics) const
        { return revisionMatches(revision, expected, current.source, diagnostics); }
        void replace(Scene value, std::vector<Scene>& from, std::vector<Scene>& to)
        {
            to.push_back(current);
            current = std::move(value);
            from.pop_back();
            revision = revisionFor(current);
        }
    };

    SceneSession::SceneSession(Scene scene, std::filesystem::path projectRoot)
        : m_impl(std::make_unique<Impl>(std::move(scene), std::move(projectRoot)))
    {}
    SceneSession::SceneSession(const Project& project)
        : m_impl(std::make_unique<Impl>(project.scene, project.root, project.assets))
    {}
    SceneSession::~SceneSession() = default;
    SceneSession::SceneSession(SceneSession&&) noexcept = default;
    SceneSession& SceneSession::operator=(SceneSession&&) noexcept = default;
    const Scene& SceneSession::snapshot() const { return m_impl->current; }
    const std::string& SceneSession::revision() const { return m_impl->revision; }

    Result<std::string> SceneSession::apply(const EditOperation& operation, const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        Scene updated = m_impl->current;
        const bool valid = applyToScene(updated, m_impl->root, m_impl->assets, operation, diagnostics);
        if (!valid) return std::unexpected(std::move(diagnostics));
        m_impl->undo.push_back(std::move(m_impl->current));
        m_impl->current = std::move(updated);
        m_impl->redo.clear();
        m_impl->revision = revisionFor(m_impl->current);
        return m_impl->revision;
    }

    Result<std::string> SceneSession::undo(const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (m_impl->undo.empty()) return std::unexpected(one("scene.undo.empty", m_impl->current.source, "", "No scene operation to undo"));
        m_impl->redo.push_back(std::move(m_impl->current));
        m_impl->current = std::move(m_impl->undo.back());
        m_impl->undo.pop_back();
        m_impl->revision = revisionFor(m_impl->current);
        return m_impl->revision;
    }

    Result<std::string> SceneSession::redo(const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (m_impl->redo.empty()) return std::unexpected(one("scene.redo.empty", m_impl->current.source, "", "No scene operation to redo"));
        m_impl->undo.push_back(std::move(m_impl->current));
        m_impl->current = std::move(m_impl->redo.back());
        m_impl->redo.pop_back();
        m_impl->revision = revisionFor(m_impl->current);
        return m_impl->revision;
    }

    Result<void> SceneSession::beginPlay(const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (m_playSnapshot) return std::unexpected(one("scene.play.already_active", m_impl->current.source, "", "Play is already active"));
        m_playSnapshot = m_impl->current;
        return {};
    }

    Result<void> SceneSession::applyPlay(const EditOperation& operation)
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (!m_playSnapshot) return std::unexpected(one("scene.play.inactive", m_impl->current.source, "", "Play is not active"));
        if (!applyToScene(*m_playSnapshot, m_impl->root, m_impl->assets, operation, diagnostics))
            return std::unexpected(std::move(diagnostics));
        return {};
    }

    const Scene* SceneSession::playSnapshot() const noexcept
    { return m_playSnapshot ? &*m_playSnapshot : nullptr; }

    Result<void> SceneSession::endPlay()
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        m_playSnapshot.reset();
        return {};
    }

    Result<std::string> SceneSession::save(const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
        if (!onOwnerThread(m_impl->ownerThread, m_impl->current.source, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        auto disk = loadScene(m_impl->file(), m_impl->root);
        if (!disk) return std::unexpected(disk.error());
        if (!revisionMatches(revisionFor(*disk), m_impl->savedRevision, m_impl->current.source, diagnostics))
            return std::unexpected(std::move(diagnostics));

        const auto destination = m_impl->file();
        const auto temporary = destination.parent_path() / (destination.filename().string() + ".tmp");
        const std::string content = serialize(m_impl->current) + "\n";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return std::unexpected(one("scene.save.write", m_impl->current.source, "", "Unable to create temporary scene file"));
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.flush();
            if (!output) return std::unexpected(one("scene.save.write", m_impl->current.source, "", "Unable to write complete scene file"));
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return std::unexpected(one("scene.save.replace", m_impl->current.source, "", "Unable to replace scene file"));
        }
#else
        if (std::rename(temporary.string().c_str(), destination.string().c_str()) != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return std::unexpected(one("scene.save.replace", m_impl->current.source, "", "Unable to replace scene file"));
        }
#endif
        m_impl->savedRevision = m_impl->revision;
        return m_impl->revision;
    }
}
