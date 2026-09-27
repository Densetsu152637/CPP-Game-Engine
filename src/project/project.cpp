#include "project.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
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
                    out << "\"Script\":{\"asset\":" << quote(entity.script->asset.generic_string()) << '}';
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

        bool applyToScene(Scene& scene, const std::filesystem::path& root,
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
                    if (command.asset->empty() || command.asset->is_absolute() || command.asset->has_root_name() || command.asset->has_root_directory())
                    {
                        add(diagnostics, "project.path.invalid", scene.source, "entities", "Script asset must be project-relative");
                        return false;
                    }
                    std::filesystem::path resolved;
                    if (!resolveFile(root, command.asset->generic_string(), scene.source, "entities", resolved, diagnostics)) return false;
                    entity->script = Script{command.asset->lexically_normal()};
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
                checkFields(*components, {"Transform", "Script"}, displayFile, path + ".components", diagnostics);
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
                            std::filesystem::path resolved;
                            if (resolveFile(root, authored, displayFile, path + ".components.Script.asset", resolved, diagnostics))
                                entity.script = Script{std::filesystem::path(authored).lexically_normal()};
                        }
                    }
                }
                scene.entities.push_back(std::move(entity));
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

        checkFields(*fields, {"schema", "name", "startup_scene"}, displayFile, "", diagnostics);
        Project project;
        project.root = root;
        project.manifest = relativePath(root, file);
        schemaField(*fields, displayFile, "", ProjectSchemaVersion, diagnostics);
        stringField(*fields, "name", displayFile, "", project.name, diagnostics);
        std::string startup;
        if (stringField(*fields, "startup_scene", displayFile, "", startup, diagnostics))
        {
            if (resolveFile(root, startup, displayFile, "startup_scene", project.startupScene, diagnostics))
            {
                auto scene = loadScene(project.startupScene, root);
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
        std::string revision;
        std::string savedRevision;
        std::vector<Scene> undo;
        std::vector<Scene> redo;

        Impl(Scene initial, std::filesystem::path projectRoot)
            : current(std::move(initial)), root(std::move(projectRoot)), revision(revisionFor(current)), savedRevision(revision)
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
    SceneSession::~SceneSession() = default;
    SceneSession::SceneSession(SceneSession&&) noexcept = default;
    SceneSession& SceneSession::operator=(SceneSession&&) noexcept = default;
    const Scene& SceneSession::snapshot() const { return m_impl->current; }
    const std::string& SceneSession::revision() const { return m_impl->revision; }

    Result<std::string> SceneSession::apply(const EditOperation& operation, const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        Scene updated = m_impl->current;
        const bool valid = applyToScene(updated, m_impl->root, operation, diagnostics);
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
        if (!m_impl->currentRevision(expectedRevision, diagnostics)) return std::unexpected(std::move(diagnostics));
        if (m_playSnapshot) return std::unexpected(one("scene.play.already_active", m_impl->current.source, "", "Play is already active"));
        m_playSnapshot = m_impl->current;
        return {};
    }

    Result<void> SceneSession::applyPlay(const EditOperation& operation)
    {
        if (!m_playSnapshot) return std::unexpected(one("scene.play.inactive", m_impl->current.source, "", "Play is not active"));
        Diagnostics diagnostics;
        if (!applyToScene(*m_playSnapshot, m_impl->root, operation, diagnostics))
            return std::unexpected(std::move(diagnostics));
        return {};
    }

    const Scene* SceneSession::playSnapshot() const noexcept
    { return m_playSnapshot ? &*m_playSnapshot : nullptr; }

    void SceneSession::endPlay() noexcept
    { m_playSnapshot.reset(); }

    Result<std::string> SceneSession::save(const std::string& expectedRevision)
    {
        Diagnostics diagnostics;
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
