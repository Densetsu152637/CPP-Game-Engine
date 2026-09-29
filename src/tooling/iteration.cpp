#include "iteration.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include "scripting/lua_source_file.h"
#include "../../third_party/picojson/picojson.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

namespace tooling
{
    namespace
    {
        project::Diagnostics error(std::string code, const std::filesystem::path& file,
            std::string path, std::string message)
        {
            return {{std::move(code), project::Severity::Error, file, std::move(path), std::move(message)}};
        }

        std::string jsonEscape(const std::string& value)
        {
            std::string result;
            for (const unsigned char c : value)
            {
                switch (c)
                {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (c < 0x20) result += '?';
                    else result += static_cast<char>(c);
                }
            }
            return result;
        }

        std::string pathText(const std::filesystem::path& path)
        {
            return path.generic_string();
        }

        bool contained(const std::filesystem::path& root, const std::filesystem::path& candidate)
        {
            auto relative = candidate.lexically_relative(root);
            if (relative.empty()) return candidate == root;
            return !relative.is_absolute() && *relative.begin() != "..";
        }

        std::string contentHash(const std::filesystem::path& file)
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream) return {};
            // FNV-1a 64 is used as a cache invalidation checksum, not an authenticity hash.
            std::uint64_t hash = 14695981039346656037ull;
            char byte = 0;
            while (stream.get(byte))
            {
                hash ^= static_cast<unsigned char>(byte);
                hash *= 1099511628211ull;
            }
            if (stream.bad()) return {};
            std::ostringstream output;
            output << std::hex << hash;
            return output.str();
        }

        std::filesystem::path canonicalExisting(const std::filesystem::path& path, std::error_code& ec);

        std::filesystem::path absoluteNormalized(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto absolute = std::filesystem::absolute(path, ec);
            if (ec) return {};
            absolute = absolute.lexically_normal();
            std::vector<std::filesystem::path> suffix;
            auto prefix = absolute;
            while (!prefix.empty())
            {
                ec.clear();
                if (std::filesystem::exists(prefix, ec) || std::filesystem::is_symlink(prefix, ec)) break;
                auto parent = prefix.parent_path();
                if (parent == prefix || parent.empty()) return {};
                suffix.push_back(prefix.filename());
                prefix = std::move(parent);
            }
            const auto canonicalPrefix = canonicalExisting(prefix, ec);
            if (ec) return {};
            auto result = canonicalPrefix;
            for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) result /= *it;
            return result.lexically_normal();
        }

        std::filesystem::path canonicalExisting(const std::filesystem::path& path, std::error_code& ec)
        {
            return std::filesystem::canonical(path, ec);
        }

        project::Result<std::filesystem::path> derivedRoot(const std::filesystem::path& root)
        {
            const auto requested = root / ".derived";
            std::error_code ec;
            if (!std::filesystem::exists(requested, ec))
            {
                ec.clear();
                std::filesystem::create_directories(requested, ec);
                if (ec) return std::unexpected(error("derived.root.create", requested, "", "Could not create derived data directory: " + ec.message()));
            }
            const auto resolved = canonicalExisting(requested, ec);
            if (ec || !std::filesystem::is_directory(resolved) || !contained(root, resolved))
                return std::unexpected(error("derived.root.outside_root", requested, "", "Derived data directory must resolve to a directory inside the project root"));
            return resolved;
        }

        std::filesystem::path stagingPath(const std::filesystem::path& target)
        {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            return target.parent_path() / (target.filename().string() + ".staging-" + std::to_string(nonce));
        }

        std::string readText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        std::string shellQuote(const std::string& value)
        {
#ifdef _WIN32
            std::string result = "\"";
            for (const char c : value) { if (c == '"') result += "\\\""; else result += c; }
            return result + '"';
#else
            std::string result = "'";
            for (const char c : value) { if (c == '\'') result += "'\\''"; else result += c; }
            return result + '\'';
#endif
        }
    }

    project::Result<std::filesystem::path> initializeProject(const std::filesystem::path& templateRoot,
        const std::filesystem::path& destination)
    {
        std::error_code ec;
        const auto source = canonicalExisting(templateRoot, ec);
        if (ec) return std::unexpected(error("project.template.invalid", templateRoot, "", "Template root cannot be resolved"));
        const auto target = absoluteNormalized(destination);
        if (target.empty()) return std::unexpected(error("project.init.destination", destination, "", "Destination path cannot be resolved"));
        if (!std::filesystem::is_directory(source) || !std::filesystem::is_regular_file(source / "project.json"))
            return std::unexpected(error("project.template.invalid", source, "", "Template must contain a project.json manifest"));
        const auto templateProject = project::loadProject(source / "project.json");
        if (!templateProject) return std::unexpected(templateProject.error());
        if (contained(source, target) || contained(target, source))
            return std::unexpected(error("project.init.overlap", target, "", "Destination must not overlap the template"));
        if (std::filesystem::exists(target))
            return std::unexpected(error("project.init.exists", target, "", "Destination already exists"));
        const auto staging = stagingPath(target);
        std::filesystem::create_directories(staging, ec);
        if (ec) return std::unexpected(error("project.init.create", staging, "", "Could not create project staging directory"));
        for (std::filesystem::recursive_directory_iterator it(source, ec), end; it != end; it.increment(ec))
        {
            if (ec) break;
            if (it->is_directory(ec))
            {
                if (it->path().filename() == ".derived") it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            const auto resolved = canonicalExisting(it->path(), ec);
            if (ec) break;
            if (!contained(source, resolved)) { ec = std::make_error_code(std::errc::permission_denied); break; }
            const auto relative = it->path().lexically_relative(source);
            const auto output = staging / relative;
            std::filesystem::create_directories(output.parent_path(), ec);
            if (ec) break;
            std::filesystem::copy_file(resolved, output, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) break;
        }
        if (ec)
        {
            std::filesystem::remove_all(staging, ec);
            return std::unexpected(error("project.init.copy", target, "", "Could not copy template into destination"));
        }
        const auto copiedProject = project::loadProject(staging / "project.json");
        if (!copiedProject)
        {
            std::filesystem::remove_all(staging, ec);
            return std::unexpected(copiedProject.error());
        }
        std::filesystem::rename(staging, target, ec);
        if (ec)
        {
            std::filesystem::remove_all(staging, ec);
            return std::unexpected(error("project.init.publish", target, "", "Could not publish initialized project"));
        }
        return target;
    }

    project::Result<AssetIndex> buildAssetIndex(const project::Project& project)
    {
        AssetIndex index;
        std::error_code ec;
        const auto root = canonicalExisting(project.root, ec);
        if (ec) return std::unexpected(error("asset.root.invalid", project.root, "", "Project root cannot be resolved"));
        // Project values normally come from loadProject(), which stores a canonical root.
        // Callers can also construct Project values directly (for tools and tests), so
        // resolve catalog paths against the canonical root used by our confinement checks.
        auto canonicalProject = project;
        canonicalProject.root = root;
        std::map<std::filesystem::path, std::string> ids;
        for (const auto& [id, asset] : project.assets)
        {
            auto resolved = project::resolveAsset(canonicalProject, id);
            if (!resolved) return std::unexpected(resolved.error());
            const auto absolute = canonicalExisting(*resolved, ec);
            if (ec || !contained(root, absolute))
                return std::unexpected(error("asset.path.outside_root", asset.path, "assets[" + id + "].path", "Asset resolves outside the project root"));
            const auto relative = absolute.lexically_relative(root);
            const auto checksum = contentHash(absolute);
            if (checksum.empty()) return std::unexpected(error("asset.read.failed", relative, "", "Could not read asset"));
            index.assets.push_back({id, relative, asset.kind, checksum, {}});
            ids.emplace(relative, id);
        }
        std::sort(index.assets.begin(), index.assets.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
        for (auto& record : index.assets)
        {
            const auto absolute = root / record.path;
            const auto source = readText(absolute);
            std::vector<std::filesystem::path> references;
            if (record.kind == "shader")
            {
                std::istringstream lines(source);
                std::string line;
                while (std::getline(lines, line))
                {
                    const auto marker = line.find("#include");
                    if (marker == std::string::npos) continue;
                    const auto first = line.find_first_of("\"<", marker + 8);
                    if (first == std::string::npos) continue;
                    const auto last = line.find_first_of("\">", first + 1);
                    if (last != std::string::npos) references.push_back(absolute.parent_path() / line.substr(first + 1, last - first - 1));
                }
            }
            else if (record.kind == "script")
            {
                std::size_t cursor = 0;
                while ((cursor = source.find("require", cursor)) != std::string::npos)
                {
                    std::size_t begin = cursor + 7;
                    while (begin < source.size() && std::isspace(static_cast<unsigned char>(source[begin]))) ++begin;
                    if (begin == source.size() || source[begin] != '(') { cursor = begin; continue; }
                    ++begin;
                    while (begin < source.size() && std::isspace(static_cast<unsigned char>(source[begin]))) ++begin;
                    if (begin == source.size() || (source[begin] != '\'' && source[begin] != '\"')) { cursor = begin; continue; }
                    const char quote = source[begin++];
                    const auto end = source.find(quote, begin);
                    if (end == std::string::npos) break;
                    auto module = source.substr(begin, end - begin);
                    std::replace(module.begin(), module.end(), '.', '/');
                    const auto scriptDir = absolute.parent_path();
                    const auto directCandidate = (scriptDir / (module + ".lua")).lexically_normal();
                    const auto packageCandidate = (scriptDir / module / "init.lua").lexically_normal();
                    if (!contained(root, directCandidate) || !contained(root, packageCandidate))
                        return std::unexpected(error("asset.dependency.outside_root", record.path, "", "Lua module resolves outside the project root"));
                    std::error_code existsError;
                    auto resolved = std::filesystem::is_regular_file(directCandidate, existsError) ?
                        canonicalExisting(directCandidate, existsError) : std::filesystem::path{};
                    if (resolved.empty() && std::filesystem::is_regular_file(packageCandidate, existsError))
                        resolved = canonicalExisting(packageCandidate, existsError);
                    if (existsError || resolved.empty())
                        return std::unexpected(error("asset.dependency.missing", record.path, "", "Missing Lua module: " + module));
                    if (!contained(root, resolved))
                        return std::unexpected(error("asset.dependency.outside_root", record.path, "", "Lua module resolves outside the project root"));
                    const auto dependencyPath = resolved.lexically_relative(root);
                    const auto dependency = ids.find(dependencyPath);
                    if (dependency == ids.end())
                        return std::unexpected(error("asset.dependency.unindexed", record.path, "", "Lua module is outside the asset index: " + pathText(dependencyPath)));
                    record.dependencies.push_back(dependency->second);
                    cursor = end + 1;
                }
            }
            for (const auto& reference : references)
            {
                const auto normalized = absoluteNormalized(reference);
                if (!contained(root, normalized))
                    return std::unexpected(error("asset.dependency.outside_root", record.path, "", "Asset dependency resolves outside the project root"));
                const auto relative = normalized.lexically_relative(root);
                const auto found = ids.find(relative);
                if (found != ids.end()) record.dependencies.push_back(found->second);
                else if (record.kind == "shader")
                    return std::unexpected(error("asset.dependency.missing", record.path, "", "Missing asset dependency: " + pathText(relative)));
            }
            std::sort(record.dependencies.begin(), record.dependencies.end());
            record.dependencies.erase(std::unique(record.dependencies.begin(), record.dependencies.end()), record.dependencies.end());
        }
        return index;
    }

    project::Result<std::string> writeAssetIndex(const project::Project& project, const AssetIndex& index)
    {
        std::error_code rootError;
        const auto root = canonicalExisting(project.root, rootError);
        if (rootError) return std::unexpected(error("asset.index.root", project.root, "", "Project root cannot be resolved"));
        auto cacheRoot = derivedRoot(root);
        if (!cacheRoot) return std::unexpected(cacheRoot.error());
        const auto file = *cacheRoot / "asset-index.json";
        std::error_code ec;
        const auto temporaryDir = stagingPath(file);
        std::filesystem::create_directory(temporaryDir, ec);
        if (ec) return std::unexpected(error("asset.index.write", file, "", "Could not create a private staging directory for the asset index"));
        const auto temporary = temporaryDir / "asset-index.json";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            std::filesystem::remove_all(temporaryDir, ec);
            return std::unexpected(error("asset.index.write", file, "", "Could not open asset index for writing"));
        }
        output << "{\n  \"schema\": " << index.schema << ",\n  \"hash\": \"fnv1a64\",\n  \"assets\": [";
        for (std::size_t i = 0; i < index.assets.size(); ++i)
        {
            const auto& record = index.assets[i];
            output << (i ? "," : "") << "\n    {\"id\":\"" << jsonEscape(record.id) << "\",\"path\":\""
                << jsonEscape(pathText(record.path)) << "\",\"kind\":\"" << record.kind << "\",\"contentHash\":\""
                << record.contentHash << "\",\"dependencies\":[";
            for (std::size_t d = 0; d < record.dependencies.size(); ++d)
                output << (d ? "," : "") << "\"" << jsonEscape(record.dependencies[d]) << "\"";
            output << "]}";
        }
        output << (index.assets.empty() ? "]\n}\n" : "\n  ]\n}\n");
        output.close();
        if (!output) { std::filesystem::remove_all(temporaryDir, ec); return std::unexpected(error("asset.index.write", file, "", "Could not flush asset index")); }
        std::filesystem::rename(temporary, file, ec);
        if (ec)
        {
            // Windows rename does not replace an existing file. Remove only the derived cache.
            std::filesystem::remove(file, ec);
            ec.clear();
            std::filesystem::rename(temporary, file, ec);
        }
        const bool replaceFailed = static_cast<bool>(ec);
        std::filesystem::remove_all(temporaryDir, ec);
        if (replaceFailed) return std::unexpected(error("asset.index.write", file, "", "Could not replace asset index"));
        return pathText(file);
    }

    project::Result<AssetIndex> readAssetIndex(const std::filesystem::path& file)
    {
        const std::string source = readText(file);
        if (source.empty()) return std::unexpected(error("asset.index.read", file, "", "Asset index is missing or empty"));
        picojson::value root;
        const auto parseError = picojson::parse(root, source);
        if (!parseError.empty() || !root.is<picojson::object>())
            return std::unexpected(error("asset.index.invalid", file, "", "Asset index is malformed JSON"));
        const auto& object = root.get<picojson::object>();
        const auto schema = object.find("schema");
        const auto hashName = object.find("hash");
        const auto assets = object.find("assets");
        if (schema == object.end() || !schema->second.is<double>() || schema->second.get<double>() != 1 ||
            hashName == object.end() || !hashName->second.is<std::string>() || hashName->second.get<std::string>() != "fnv1a64" ||
            assets == object.end() || !assets->second.is<picojson::array>())
            return std::unexpected(error("asset.index.schema", file, "", "Asset index schema or hash algorithm is unsupported"));
        AssetIndex result;
        for (const auto& entry : assets->second.get<picojson::array>())
        {
            if (!entry.is<picojson::object>()) return std::unexpected(error("asset.index.invalid", file, "assets", "Asset index entry must be an object"));
            const auto& fields = entry.get<picojson::object>();
            const auto getString = [&](const char* key) -> const std::string* {
                const auto found = fields.find(key);
                return found != fields.end() && found->second.is<std::string>() ? &found->second.get<std::string>() : nullptr;
            };
            const auto* id = getString("id");
            const auto* path = getString("path");
            const auto* kind = getString("kind");
            const auto* checksum = getString("contentHash");
            const auto deps = fields.find("dependencies");
            if (!id || !path || !kind || !checksum || deps == fields.end() || !deps->second.is<picojson::array>())
                return std::unexpected(error("asset.index.invalid", file, "assets", "Asset record fields have invalid types"));
            AssetRecord record{*id, std::filesystem::path(*path), *kind, *checksum, {}};
            if (record.path.empty() || record.path.is_absolute() || record.path.has_root_name() || record.path.has_root_directory() ||
                std::find(record.path.begin(), record.path.end(), "..") != record.path.end())
                return std::unexpected(error("asset.index.path.invalid", file, "assets.path", "Asset index contains an unsafe path"));
            for (const auto& dep : deps->second.get<picojson::array>())
            {
                if (!dep.is<std::string>()) return std::unexpected(error("asset.index.invalid", file, "assets.dependencies", "Dependency IDs must be strings"));
                record.dependencies.push_back(dep.get<std::string>());
            }
            result.assets.push_back(std::move(record));
        }
        std::sort(result.assets.begin(), result.assets.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
        return result;
    }

    project::Result<void> validateLua(const std::filesystem::path& file)
    {
        const auto source = scripting::read_lua_source_file(file);
        if (!source)
            return std::unexpected(error(source.error() == scripting::LuaSourceReadError::TooLarge
                ? "script.size.invalid" : "script.read.failed", file, "",
                std::string(scripting::lua_source_error_message(source.error()))));
        lua_State* state = luaL_newstate();
        if (!state) return std::unexpected(error("script.validation.runtime", file, "", "Could not create Lua validation state"));
        const int status = luaL_loadbuffer(state, source->data(), source->size(), ("@" + file.string()).c_str());
        if (status == LUA_OK) { lua_pop(state, 1); lua_close(state); return {}; }
        const char* message = lua_tostring(state, -1);
        const std::string reason = message ? message : "Lua compiler reported an unknown error";
        lua_close(state);
        return std::unexpected(error("script.syntax.invalid", file, "", reason));
    }

    project::Result<StagedScript> stageScriptReload(const std::filesystem::path& file)
    {
        if (auto valid = validateLua(file); !valid) return std::unexpected(valid.error());
        auto source = scripting::read_lua_source_file(file);
        if (!source)
            return std::unexpected(error(source.error() == scripting::LuaSourceReadError::TooLarge
                ? "script.size.invalid" : "script.read.failed", file, "",
                std::string(scripting::lua_source_error_message(source.error()))));
        return StagedScript{file, std::move(*source), contentHash(file)};
    }

    project::Result<void> stageRuntimeScriptReload(project::Runtime& runtime, std::string_view authoredEntityId,
        const std::filesystem::path& file)
    {
        auto candidate = stageScriptReload(file);
        if (!candidate) return std::unexpected(candidate.error());
        auto staged = runtime.stageScriptReload(std::string(authoredEntityId), std::move(candidate->contents),
            "@" + candidate->source.string());
        if (!staged) return std::unexpected(staged.error());
        return {};
    }

    project::Result<void> stageRuntimeScriptReload(project::Runtime& runtime, const project::Project& project,
        std::string_view authoredEntityId, std::string_view assetId)
    {
        const auto asset = project.assets.find(assetId);
        if (asset == project.assets.end() || asset->second.kind != "script")
            return std::unexpected(error("script.reload.asset.invalid", project.manifest, "assets", "Reload requires a declared script asset ID"));
        auto resolved = project::resolveAsset(project, assetId);
        if (!resolved) return std::unexpected(resolved.error());
        return stageRuntimeScriptReload(runtime, authoredEntityId, *resolved);
    }

    project::Result<void> validateShaders(const std::filesystem::path& projectRoot,
        const std::filesystem::path& compiler)
    {
#ifdef CPP_GAME_ENGINE_MOBILE
        (void)projectRoot;
        (void)compiler;
        return std::unexpected(error("shader.compiler.unsupported", {}, "",
            "On-device GLSL compilation is unavailable; package precompiled SPIR-V shaders"));
#else
        if (compiler.empty() || !std::filesystem::exists(compiler))
            return std::unexpected(error("shader.compiler.missing", compiler, "", "GLSL compiler was not found; install the Vulkan SDK or pass glslc path"));
        std::error_code rootError;
        const auto root = canonicalExisting(projectRoot, rootError);
        if (rootError) return std::unexpected(error("shader.project.root", projectRoot, "", "Project root cannot be resolved"));
        const auto shaderDir = root / "assets" / "shaders";
        std::error_code ec;
        if (!std::filesystem::exists(shaderDir, ec)) return {};
        const auto temp = std::filesystem::temp_directory_path() /
            ("cpp-game-engine-shader-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp, ec);
        if (ec) return std::unexpected(error("shader.validation.temp", temp, "", "Could not create temporary shader output directory"));
        project::Diagnostics diagnostics;
        for (std::filesystem::recursive_directory_iterator it(shaderDir, ec), end; it != end; it.increment(ec))
        {
            if (ec) { diagnostics.push_back({"shader.scan.failed", project::Severity::Error, shaderDir, "", ec.message()}); break; }
            const auto extension = it->path().extension();
            if (!it->is_regular_file() || (extension != ".vert" && extension != ".frag" &&
                extension != ".geom" && extension != ".comp")) continue;
            std::error_code sourceError;
            const auto source = canonicalExisting(it->path(), sourceError);
            if (sourceError || !contained(root, source))
            {
                diagnostics.push_back({"asset.path.outside_root", project::Severity::Error, it->path().lexically_relative(root), "", "Shader source resolves outside the project root"});
                continue;
            }
            const auto rel = it->path().lexically_relative(root);
            const auto output = temp / (std::to_string(diagnostics.size()) + ".spv");
            const auto log = temp / (std::to_string(diagnostics.size()) + ".log");
            const std::string command = shellQuote(compiler.string()) + " " + shellQuote(source.string()) +
                " -o " + shellQuote(output.string()) + " > " + shellQuote(log.string()) + " 2>&1";
            const int result = std::system(command.c_str());
            if (result != 0)
                diagnostics.push_back({"shader.compile.failed", project::Severity::Error, rel, "", readText(log)});
        }
        std::filesystem::remove_all(temp, ec);
        if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        return {};
#endif
    }

    project::Result<std::filesystem::path> importShaders(const std::filesystem::path& projectRoot,
        const std::filesystem::path& compiler)
    {
#ifdef CPP_GAME_ENGINE_MOBILE
        (void)projectRoot;
        (void)compiler;
        return std::unexpected(error("shader.compiler.unsupported", {}, "",
            "On-device GLSL compilation is unavailable; package precompiled SPIR-V shaders"));
#else
        if (compiler.empty() || !std::filesystem::exists(compiler))
            return std::unexpected(error("shader.compiler.missing", compiler, "", "GLSL compiler was not found; install the Vulkan SDK or pass glslc path"));
        std::error_code rootError;
        const auto root = canonicalExisting(projectRoot, rootError);
        if (rootError) return std::unexpected(error("shader.project.root", projectRoot, "", "Project root cannot be resolved"));
        const auto shaderDir = root / "assets" / "shaders";
        std::error_code ec;
        if (!std::filesystem::exists(shaderDir, ec)) return root / ".derived" / "shaders";
        auto derivedResult = derivedRoot(root);
        if (!derivedResult) return std::unexpected(derivedResult.error());
        const auto derived = *derivedResult;
        const auto cache = derived / "shaders";
        const auto stage = stagingPath(derived / "shader-import");
        std::filesystem::create_directories(stage, ec);
        if (ec) return std::unexpected(error("shader.import.temp", stage, "", "Could not create shader import staging directory"));
        struct Compiled { std::filesystem::path staged; std::filesystem::path cached; std::filesystem::path source; };
        std::vector<Compiled> compiled;
        project::Diagnostics diagnostics;
        for (std::filesystem::recursive_directory_iterator it(shaderDir, ec), end; it != end; it.increment(ec))
        {
            if (ec) { diagnostics.push_back({"shader.scan.failed", project::Severity::Error, shaderDir, "", ec.message()}); break; }
            const auto extension = it->path().extension();
            if (!it->is_regular_file() || (extension != ".vert" && extension != ".frag" &&
                extension != ".geom" && extension != ".comp")) continue;
            std::error_code sourceError;
            const auto source = canonicalExisting(it->path(), sourceError);
            if (sourceError || !contained(root, source))
            {
                diagnostics.push_back({"asset.path.outside_root", project::Severity::Error, it->path().lexically_relative(root), "", "Shader source resolves outside the project root"});
                continue;
            }
            const auto relative = it->path().lexically_relative(shaderDir);
            auto outputRelative = relative;
            outputRelative += ".spv";
            const auto staged = stage / outputRelative;
            std::filesystem::create_directories(staged.parent_path(), ec);
            if (ec) { diagnostics.push_back({"shader.import.temp", project::Severity::Error, staged, "", "Could not create staged shader output directory"}); break; }
            const auto log = staged.string() + ".log";
            const std::string command = shellQuote(compiler.string()) + " " + shellQuote(source.string()) +
                " -o " + shellQuote(staged.string()) + " > " + shellQuote(log) + " 2>&1";
            if (std::system(command.c_str()) != 0)
                diagnostics.push_back({"shader.compile.failed", project::Severity::Error, it->path().lexically_relative(root), "", readText(log)});
            else compiled.push_back({staged, cache / outputRelative, it->path().lexically_relative(root)});
        }
        if (!diagnostics.empty())
        {
            std::filesystem::remove_all(stage, ec);
            return std::unexpected(std::move(diagnostics));
        }
        std::filesystem::create_directories(cache, ec);
        if (ec) { std::filesystem::remove_all(stage, ec); return std::unexpected(error("shader.import.cache", cache, "", "Could not create shader cache directory")); }
        const auto resolvedCache = canonicalExisting(cache, ec);
        if (ec || !contained(root, resolvedCache))
        {
            std::filesystem::remove_all(stage, ec);
            return std::unexpected(error("shader.import.cache.outside_root", cache, "", "Shader cache resolves outside the project root"));
        }
        // Compile every source before publishing any cache entry. A syntax error therefore
        // leaves the previous imported shader set available for the current run.
        for (const auto& item : compiled)
        {
            std::filesystem::create_directories(item.cached.parent_path(), ec);
            if (ec) break;
            const auto resolvedParent = canonicalExisting(item.cached.parent_path(), ec);
            if (ec || !contained(resolvedCache, resolvedParent)) { ec = std::make_error_code(std::errc::permission_denied); break; }
            std::filesystem::rename(item.staged, item.cached, ec);
            if (ec)
            {
                std::filesystem::remove(item.cached, ec);
                ec.clear();
                std::filesystem::rename(item.staged, item.cached, ec);
            }
            if (ec) break;
        }
        const bool publishFailed = static_cast<bool>(ec);
        std::filesystem::remove_all(stage, ec);
        if (publishFailed) return std::unexpected(error("shader.import.publish", cache, "", "Could not publish imported shader output"));
        return cache;
#endif
    }

    project::Result<std::filesystem::path> packageProject(const project::Project& project,
        const std::filesystem::path& destination, const std::filesystem::path& runtimeExecutable)
    {
        std::error_code rootError;
        const auto root = canonicalExisting(project.root, rootError);
        if (rootError) return std::unexpected(error("package.project.root", project.root, "", "Project root cannot be resolved"));
        const auto target = absoluteNormalized(destination);
        if (target.empty()) return std::unexpected(error("package.destination.invalid", destination, "", "Package destination path cannot be resolved"));
        if (contained(root, target) || contained(target, root))
            return std::unexpected(error("package.destination.overlap", destination, "", "Package destination must not overlap the source project"));
        if (std::filesystem::exists(target))
            return std::unexpected(error("package.destination.exists", target, "", "Package destination already exists"));
        const auto staging = stagingPath(target);
        std::error_code ec;
        std::filesystem::create_directories(staging, ec);
        if (ec) return std::unexpected(error("package.create.failed", staging, "", "Could not create package staging directory: " + ec.message()));
        const auto copyFile = [&](const std::filesystem::path& relative) -> bool
        {
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return false;
            std::error_code sourceError;
            const auto resolved = canonicalExisting(root / relative, sourceError);
            if (sourceError || !contained(root, resolved) || !std::filesystem::is_regular_file(resolved)) return false;
            const auto out = staging / relative;
            std::filesystem::create_directories(out.parent_path(), ec);
            if (ec) return false;
            std::filesystem::copy_file(resolved, out, std::filesystem::copy_options::overwrite_existing, ec);
            return !ec;
        };
        const auto manifestRel = project.manifest.is_absolute() ? project.manifest.lexically_relative(root) : project.manifest;
        const auto sceneRel = project.scene.source.is_absolute() ? project.scene.source.lexically_relative(root) : project.scene.source;
        bool ok = copyFile(manifestRel) && copyFile(sceneRel);
        const auto index = buildAssetIndex(project);
        if (!index) ok = false;
        if (index)
            for (const auto& asset : index->assets) ok = copyFile(asset.path) && ok;
        auto executable = runtimeExecutable;
        if (executable.empty())
        {
#ifdef _WIN32
            executable = root / "build" / "debug-vk0" / "bin" / "CPPGameEngine.exe";
#else
            executable = root / "build" / "debug-vk0" / "bin" / "CPPGameEngine";
#endif
        }
        executable = absoluteNormalized(executable);
        const auto executableRelative = std::filesystem::path("bin") / executable.filename();
        if (ok)
        {
            if (!std::filesystem::is_regular_file(executable))
            {
                std::filesystem::remove_all(staging, ec);
                return std::unexpected(error("package.runtime.missing", executable, "", "Engine runtime executable is missing; build the headless engine or pass its path"));
            }
            std::filesystem::create_directories((staging / executableRelative).parent_path(), ec);
            if (!ec) std::filesystem::copy_file(executable, staging / executableRelative, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) ok = false;
        }
        if (ok)
        {
            const auto marker = staging / "PACKAGE.txt";
            std::ofstream info(marker, std::ios::binary);
            info << "CPP Game Engine source content package\n"
                << "manifest: " << pathText(manifestRel) << "\n"
                << "startup-scene: " << pathText(sceneRel) << "\n"
                << "runtime: " << pathText(executableRelative) << "\n"
                << "Launch run.cmd on Windows or run.sh on Unix-like systems.\n"
                << "Project Lua scripts execute as code; this package does not sandbox them.\n";
            ok = static_cast<bool>(info);
            if (ok)
            {
#ifdef _WIN32
                std::ofstream launcher(staging / "run.cmd", std::ios::binary);
                launcher << "@echo off\r\npushd \"%~dp0\"\r\n\"" << pathText(executableRelative)
                    << "\" run \"" << pathText(manifestRel) << "\" %*\r\nset \"result=%errorlevel%\"\r\npopd\r\nexit /b %result%\r\n";
                ok = static_cast<bool>(launcher);
#else
                std::ofstream launcher(staging / "run.sh", std::ios::binary);
                launcher << "#!/bin/sh\npackage_dir=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd) || exit 1\n"
                    << "cd \"$package_dir\" || exit 1\nexec \"" << pathText(executableRelative)
                    << "\" run \"" << pathText(manifestRel) << "\" \"$@\"\n";
                ok = static_cast<bool>(launcher);
                if (ok)
                {
                    std::filesystem::permissions(staging / "run.sh", std::filesystem::perms::owner_exec |
                        std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                        std::filesystem::perm_options::add, ec);
                    ok = !ec;
                }
#endif
            }
        }
        if (!ok)
        {
            std::filesystem::remove_all(staging, ec);
            if (!index) return std::unexpected(index.error());
            return std::unexpected(error("package.copy.failed", target, "", "Required project files could not be copied into package"));
        }
        std::filesystem::rename(staging, target, ec);
        if (ec) { std::filesystem::remove_all(staging, ec); return std::unexpected(error("package.publish.failed", target, "", "Could not publish package directory")); }
        return target;
    }

    project::Result<std::filesystem::path> locateRuntimeAsset(const std::filesystem::path& packageRoot,
        const std::filesystem::path& projectRelativeAsset)
    {
        if (projectRelativeAsset.empty() || projectRelativeAsset.is_absolute())
            return std::unexpected(error("runtime.asset.path.invalid", projectRelativeAsset, "", "Runtime asset path must be project-relative"));
        std::error_code rootError;
        const auto root = canonicalExisting(packageRoot, rootError);
        if (rootError) return std::unexpected(error("runtime.package.root", packageRoot, "", "Runtime package root cannot be resolved"));
        const auto candidate = absoluteNormalized(root / projectRelativeAsset);
        if (candidate.empty()) return std::unexpected(error("runtime.asset.path.invalid", projectRelativeAsset, "", "Runtime asset path could not be resolved safely"));
        if (!contained(root, candidate)) return std::unexpected(error("runtime.asset.path.outside", projectRelativeAsset, "", "Runtime asset path escapes package root"));
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec))
            return std::unexpected(error("runtime.asset.missing", projectRelativeAsset, "", "Asset is missing from runtime package"));
        return candidate;
    }
}
