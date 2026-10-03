#pragma once
#include "project.h"
#include "../../third_party/picojson/picojson.h"
#include <functional>
#include <mutex>
namespace project
{
    // These hooks represent I/O boundaries for deterministic failure tests.
    enum class PersistenceStage { Write, Flush, Replace };
    struct PersistenceOptions
    {
        std::filesystem::path root;
        std::string projectId;
        unsigned schemaVersion = 1;
        unsigned contentVersion = 1;
        std::size_t maxBytes = 1024 * 1024;
        unsigned maxDepth = 32;
        std::function<bool(PersistenceStage)> fail;
    };
    using PersistenceMigration = std::function<Result<picojson::object>(unsigned schema, unsigned content, const picojson::object&)>;
    // The host selects root; authored scripts receive only bounded slot APIs.
    // save and settings occupy separate directories. Loading a backup is an
    // explicit recovery action, never an implicit rewrite of the primary file.
    class PersistenceStore
    {
    public:
        explicit PersistenceStore(PersistenceOptions options);
        Result<picojson::object> load(const std::string& slot, const PersistenceMigration& migration = {});
        Result<picojson::object> loadBackup(const std::string& slot, const PersistenceMigration& migration = {});
        Result<void> recoverBackup(const std::string& slot, const PersistenceMigration& migration = {});
        Result<void> save(const std::string& slot, const picojson::object& data, const PersistenceMigration& migration = {});
        Result<picojson::object> loadSettings(const PersistenceMigration& migration = {});
        Result<void> saveSettings(const picojson::object& data, const PersistenceMigration& migration = {});
        static Result<std::filesystem::path> defaultUserDataRoot(const std::string& projectId);
    private:
        PersistenceOptions options;
        std::mutex mutex;
        Result<std::filesystem::path> path(const std::string& area, const std::string& slot, bool create);
        Result<picojson::object> read(const std::filesystem::path& file, const PersistenceMigration& migration);
        Result<void> write(const std::filesystem::path& file, const picojson::object& data, const PersistenceMigration& migration);
    };
}
