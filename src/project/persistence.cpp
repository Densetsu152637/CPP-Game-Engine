#include "persistence.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace project
{
    namespace
    {
        Diagnostics failure(const char* code, const std::filesystem::path& file, const char* message)
        { return {{code, Severity::Error, file, "", message}}; }
        bool safeName(const std::string& name)
        {
            if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
            return std::all_of(name.begin(), name.end(), [](unsigned char c)
            { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
        }
        bool link(const std::filesystem::path& file)
        {
            std::error_code ec; const auto status = std::filesystem::symlink_status(file, ec);
            if (ec && ec != std::errc::no_such_file_or_directory) return true;
            if (std::filesystem::is_symlink(status)) return true;
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(file.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
            return false;
        }
        bool safeDepth(const std::string& text, unsigned limit)
        {
            unsigned depth = 0; bool quoted = false, escaped = false;
            for (const auto c : text)
            {
                if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
                else if (c == '"') quoted = true;
                else if (c == '{' || c == '[') { if (++depth > limit) return false; }
                else if (c == '}' || c == ']') { if (depth) --depth; }
            }
            return true; // The JSON parser diagnoses truncation and mismatched syntax.
        }
        bool validValue(const picojson::value& value, unsigned depth, unsigned limit)
        {
            if (depth > limit) return false;
            if (value.is<double>()) return std::isfinite(value.get<double>());
            if (value.is<picojson::object>()) for (const auto& [key, child] : value.get<picojson::object>())
            { if (!validValue(child, depth + 1, limit)) return false; }
            if (value.is<picojson::array>()) for (const auto& child : value.get<picojson::array>())
            { if (!validValue(child, depth + 1, limit)) return false; }
            return true;
        }
        unsigned version(const picojson::object& envelope, const char* key)
        {
            const auto found = envelope.find(key);
            if (found == envelope.end() || !found->second.is<double>()) return 0;
            const double n = found->second.get<double>();
            if (!std::isfinite(n) || n < 1 || n > std::numeric_limits<unsigned>::max() || std::floor(n) != n) return 0;
            return static_cast<unsigned>(n);
        }
        struct FileLock
        {
#ifdef _WIN32
            HANDLE handle = INVALID_HANDLE_VALUE;
            explicit FileLock(const std::filesystem::path& path)
            { handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr); }
            bool valid() const { return handle != INVALID_HANDLE_VALUE; }
            ~FileLock() { if (valid()) CloseHandle(handle); }
#else
            int handle = -1;
            explicit FileLock(const std::filesystem::path& path)
            { handle = ::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600); if (handle >= 0 && flock(handle, LOCK_EX | LOCK_NB)) { ::close(handle); handle = -1; } }
            bool valid() const { return handle >= 0; }
            ~FileLock() { if (valid()) { flock(handle, LOCK_UN); ::close(handle); } }
#endif
        };
        // The destination has already been checked under a per-slot OS lock.
        Result<void> durableReplace(const std::filesystem::path& destination, const std::string& text,
            const std::function<bool(PersistenceStage)>& fail)
        {
            const auto temporary = std::filesystem::path(destination.native() + std::filesystem::path(".tmp").native());
            if (link(temporary)) return std::unexpected(failure("persistence.path.link", temporary, "Temporary file is a link"));
            std::error_code ec;
            if (std::filesystem::exists(temporary, ec) && !std::filesystem::is_regular_file(temporary, ec))
                return std::unexpected(failure("persistence.write.failed", temporary, "Temporary path is not a regular file"));
            // A stale regular temporary is an interrupted write, never a committed snapshot.
            std::filesystem::remove(temporary, ec);
            if (ec) return std::unexpected(failure("persistence.write.failed", temporary, "Cannot remove stale temporary file"));
            auto cleanup = [&] { std::error_code ignored; std::filesystem::remove(temporary, ignored); };
#ifdef _WIN32
            HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return std::unexpected(failure("persistence.write.failed", temporary, "Cannot create temporary file"));
            DWORD written = 0;
            const bool writeOk = !(fail && fail(PersistenceStage::Write)) && WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
            const bool flushOk = writeOk && !(fail && fail(PersistenceStage::Flush)) && FlushFileBuffers(handle);
            const bool closeOk = CloseHandle(handle) != 0;
#else
            const int handle = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
            if (handle < 0) return std::unexpected(failure("persistence.write.failed", temporary, "Cannot create temporary file"));
            bool writeOk = !(fail && fail(PersistenceStage::Write)); std::size_t offset = 0;
            while (writeOk && offset < text.size()) { const auto n = ::write(handle, text.data() + offset, text.size() - offset); if (n <= 0) writeOk = false; else offset += n; }
            const bool flushOk = writeOk && !(fail && fail(PersistenceStage::Flush)) && ::fsync(handle) == 0;
            const bool closeOk = ::close(handle) == 0;
#endif
            if (!writeOk || !flushOk || !closeOk)
            { cleanup(); return std::unexpected(failure(!writeOk ? "persistence.write.failed" : "persistence.flush.failed", destination, "Temporary snapshot could not be written and durably flushed")); }
            if (fail && fail(PersistenceStage::Replace))
            { cleanup(); return std::unexpected(failure("persistence.replace.failed", destination, "Injected replacement failure; previous snapshot retained")); }
#ifdef _WIN32
            const bool replaceOk = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            const bool replaceOk = ::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
            if (!replaceOk) { cleanup(); return std::unexpected(failure("persistence.replace.failed", destination, "Atomic snapshot replacement failed; backup retained")); }
#ifndef _WIN32
            const int directory = ::open(destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
            const bool directoryOk = directory >= 0 && ::fsync(directory) == 0;
            if (directory >= 0) ::close(directory);
            if (!directoryOk) return std::unexpected(failure("persistence.flush.failed", destination, "Snapshot replaced but directory flush failed; durability not confirmed"));
#endif
            return {};
        }
    }
    PersistenceStore::PersistenceStore(PersistenceOptions value) : options(std::move(value)) {}
    Result<std::filesystem::path> PersistenceStore::defaultUserDataRoot(const std::string& projectId)
    {
        if (!safeName(projectId)) return std::unexpected(failure("persistence.project.invalid", {}, "Project ID must be a bounded safe identifier"));
#ifdef _WIN32
        wchar_t* environment = nullptr; std::size_t count = 0;
        if (_wdupenv_s(&environment, &count, L"LOCALAPPDATA") != 0 || !environment)
            return std::unexpected(failure("persistence.root.unavailable", {}, "LOCALAPPDATA is unavailable"));
        std::filesystem::path root(environment); std::free(environment); return root / "CPPGameEngine";
#else
        const auto xdg = std::getenv("XDG_DATA_HOME");
        if (xdg && std::filesystem::path(xdg).is_absolute()) return std::filesystem::path(xdg) / "CPPGameEngine";
        const auto home = std::getenv("HOME");
        if (!home || !std::filesystem::path(home).is_absolute()) return std::unexpected(failure("persistence.root.unavailable", {}, "User data directory is unavailable"));
        return std::filesystem::path(home) / ".local" / "share" / "CPPGameEngine";
#endif
    }
    Result<std::filesystem::path> PersistenceStore::path(const std::string& area, const std::string& slot, bool create)
    {
        if (!safeName(options.projectId) || !safeName(slot)) return std::unexpected(failure("persistence.slot.invalid", {}, "Project/slot must be safe bounded identifiers"));
        if (options.root.empty() || !options.schemaVersion || !options.contentVersion || !options.maxBytes || options.maxBytes > 16 * 1024 * 1024 || !options.maxDepth || options.maxDepth > 128)
            return std::unexpected(failure("persistence.options.invalid", options.root, "Invalid persistence limits, versions, or host root"));
        std::error_code ec; const auto root = std::filesystem::absolute(options.root, ec).lexically_normal();
        if (ec) return std::unexpected(failure("persistence.root.invalid", options.root, "Cannot resolve user data root"));
        // Reject links/junctions through the root and all confined descendants.
        std::filesystem::path prefix;
        for (const auto& part : root) { prefix /= part; if (link(prefix)) return std::unexpected(failure("persistence.path.link", prefix, "User data path contains a link or junction")); }
        const auto directory = root / options.projectId / area;
        for (const auto& entry : {root / options.projectId, directory})
            if (link(entry)) return std::unexpected(failure("persistence.path.link", entry, "User data path contains a link or junction"));
        if (create) { std::filesystem::create_directories(directory, ec); if (ec) return std::unexpected(failure("persistence.directory.failed", directory, "Cannot create user data directory")); }
        const auto file = directory / (slot + ".json");
        for (const auto& suffix : {"", ".bak", ".tmp", ".lock", ".bak.tmp"})
            if (link(std::filesystem::path(file.native() + std::filesystem::path(suffix).native())))
                return std::unexpected(failure("persistence.path.link", file, "Snapshot path contains a link or junction"));
        return file;
    }
    Result<picojson::object> PersistenceStore::read(const std::filesystem::path& file, const PersistenceMigration& migration)
    {
        std::error_code ec;
        if (!std::filesystem::exists(file, ec) && !ec) return std::unexpected(failure("persistence.missing", file, "No snapshot exists"));
        if (ec || !std::filesystem::is_regular_file(file, ec)) return std::unexpected(failure("persistence.read.failed", file, "Snapshot is not a readable regular file"));
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input) return std::unexpected(failure("persistence.read.failed", file, "Cannot read snapshot"));
        const auto size = input.tellg();
        if (size < 0 || static_cast<std::uint64_t>(size) > options.maxBytes) return std::unexpected(failure("persistence.size", file, "Snapshot exceeds byte limit"));
        std::string text(static_cast<std::size_t>(size), '\0'); input.seekg(0);
        if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return std::unexpected(failure("persistence.read.failed", file, "Incomplete snapshot read"));
        if (!safeDepth(text, 130)) return std::unexpected(failure("persistence.depth", file, "Snapshot exceeds parser depth safety bound"));
        picojson::value envelope; std::string parseError;
        const auto end = picojson::parse(envelope, text.begin(), text.end(), &parseError);
        if (!parseError.empty() || end != text.end() || !envelope.is<picojson::object>()) return std::unexpected(failure("persistence.corrupt", file, "Malformed snapshot JSON envelope"));
        const auto& object = envelope.get<picojson::object>(); const auto schema = version(object, "schema"); const auto content = version(object, "content_version");
        if (!schema || !content) return std::unexpected(failure("persistence.corrupt", file, "Invalid snapshot schema or content version"));
        // Future versions may intentionally change the payload shape. Identify
        // their valid version header before interpreting any current-schema data.
        if (schema > options.schemaVersion || content > options.contentVersion) return std::unexpected(failure("persistence.version.newer", file, "Snapshot was written by a newer schema/content version"));
        const auto data = object.find("data");
        if (data == object.end() || !data->second.is<picojson::object>() || !validValue(data->second, 0, options.maxDepth))
            return std::unexpected(failure("persistence.corrupt", file, "Invalid snapshot object data"));
        if (schema != options.schemaVersion || content != options.contentVersion)
        {
            if (!migration) return std::unexpected(failure("persistence.migration.required", file, "Explicit version migration is required"));
            try
            {
                auto migrated = migration(schema, content, data->second.get<picojson::object>());
                if (!migrated) return std::unexpected(migrated.error());
                const auto result = picojson::value(*migrated);
                if (!validValue(result, 0, options.maxDepth) || result.serialize().size() > options.maxBytes)
                    return std::unexpected(failure("persistence.migration.invalid", file, "Migration returned invalid or oversized data"));
                return migrated;
            }
            catch (...) { return std::unexpected(failure("persistence.migration.failed", file, "Migration callback failed")); }
        }
        return data->second.get<picojson::object>();
    }
    Result<void> PersistenceStore::write(const std::filesystem::path& file, const picojson::object& data, const PersistenceMigration& migration)
    {
        const auto value = picojson::value(data);
        if (!validValue(value, 0, options.maxDepth)) return std::unexpected(failure("persistence.data.invalid", file, "Data exceeds depth limit or contains non-finite numbers"));
        const picojson::object envelope{{"schema", picojson::value(double(options.schemaVersion))}, {"content_version", picojson::value(double(options.contentVersion))}, {"data", value}};
        const auto text = picojson::value(envelope).serialize();
        if (text.size() > options.maxBytes) return std::unexpected(failure("persistence.size", file, "Serialized snapshot exceeds byte limit"));
        FileLock lock(std::filesystem::path(file.native() + std::filesystem::path(".lock").native()));
        if (!lock.valid()) return std::unexpected(failure("persistence.busy", file, "Snapshot is locked or inaccessible"));
        std::error_code ec;
        if (std::filesystem::exists(file, ec))
        {
            // Validate before preserving or replacing; newer saves are never overwritten.
            const auto previous = read(file, migration);
            if (!previous) return std::unexpected(previous.error());
            std::ifstream input(file, std::ios::binary); const std::string prior((std::istreambuf_iterator<char>(input)), {});
            if (!input.good() && !input.eof()) return std::unexpected(failure("persistence.read.failed", file, "Cannot preserve previous snapshot"));
            const auto backup = std::filesystem::path(file.native() + std::filesystem::path(".bak").native());
            // Do not replace a future or unrecognized backup with current data.
            if (std::filesystem::exists(backup, ec))
            { const auto oldBackup = read(backup, {}); if (!oldBackup && oldBackup.error().front().code != "persistence.migration.required") return std::unexpected(oldBackup.error()); }
            const auto savedBackup = durableReplace(backup, prior, {});
            if (!savedBackup) return savedBackup;
        }
        else if (ec) return std::unexpected(failure("persistence.read.failed", file, "Cannot inspect existing snapshot"));
        return durableReplace(file, text, options.fail);
    }
    Result<picojson::object> PersistenceStore::load(const std::string& slot, const PersistenceMigration& migration)
    { std::lock_guard lock(mutex); const auto file = path("saves", slot, false); if (!file) return std::unexpected(file.error()); return read(*file, migration); }
    Result<picojson::object> PersistenceStore::loadBackup(const std::string& slot, const PersistenceMigration& migration)
    { std::lock_guard lock(mutex); const auto file = path("saves", slot, false); if (!file) return std::unexpected(file.error()); return read(std::filesystem::path(file->native() + std::filesystem::path(".bak").native()), migration); }
    Result<void> PersistenceStore::recoverBackup(const std::string& slot, const PersistenceMigration& migration)
    {
        std::lock_guard guard(mutex);
        const auto file = path("saves", slot, false);
        if (!file) return std::unexpected(file.error());
        FileLock lock(std::filesystem::path(file->native() + std::filesystem::path(".lock").native()));
        if (!lock.valid()) return std::unexpected(failure("persistence.busy", *file, "Snapshot is locked or inaccessible"));
        const auto backup = read(std::filesystem::path(file->native() + std::filesystem::path(".bak").native()), migration);
        if (!backup) return std::unexpected(backup.error());
        const auto current = read(*file, migration);
        if (!current && current.error().front().code != "persistence.missing" && current.error().front().code != "persistence.corrupt")
            return std::unexpected(current.error());
        const picojson::object envelope{{"schema", picojson::value(double(options.schemaVersion))}, {"content_version", picojson::value(double(options.contentVersion))}, {"data", picojson::value(*backup)}};
        const auto text = picojson::value(envelope).serialize();
        if (text.size() > options.maxBytes) return std::unexpected(failure("persistence.size", *file, "Recovered snapshot exceeds byte limit"));
        // Keep the known-good backup unchanged until and after primary recovery.
        return durableReplace(*file, text, options.fail);
    }
    Result<void> PersistenceStore::save(const std::string& slot, const picojson::object& data, const PersistenceMigration& migration)
    { std::lock_guard lock(mutex); const auto file = path("saves", slot, true); if (!file) return std::unexpected(file.error()); return write(*file, data, migration); }
    Result<picojson::object> PersistenceStore::loadSettings(const PersistenceMigration& migration)
    { std::lock_guard lock(mutex); const auto file = path("settings", "preferences", false); if (!file) return std::unexpected(file.error()); return read(*file, migration); }
    Result<void> PersistenceStore::saveSettings(const picojson::object& data, const PersistenceMigration& migration)
    { std::lock_guard lock(mutex); const auto file = path("settings", "preferences", true); if (!file) return std::unexpected(file.error()); return write(*file, data, migration); }
}
