#include "project/persistence.h"
#include "test/test_assertions.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace
{
    struct Fixture
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("cpp-engine-persistence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { std::filesystem::create_directories(root); }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
        project::PersistenceOptions options() const { return {root, "fixture"}; }
        std::filesystem::path primary() const { return root / "fixture" / "saves" / "main.json"; }
        void text(const std::string& data) { std::ofstream(primary(), std::ios::binary | std::ios::trunc) << data; }
    };
    picojson::object state(const char* room) { return {{"room", picojson::value(std::string(room))}, {"events", picojson::value(picojson::array{picojson::value(std::string("event:verse"))})}}; }
    void freshRestartBackupAndCorruption()
    {
        Fixture f; project::PersistenceStore store(f.options()); auto absent = store.load("main");
        test::require(!absent && absent.error().front().code == "persistence.missing", "missing save distinct");
        test::require(store.save("main", state("room:a")).has_value(), "durable first save");
        project::PersistenceStore restarted(f.options()); auto loaded = restarted.load("main");
        test::require(loaded.has_value() && loaded->at("room").get<std::string>() == "room:a", "restart restores stable IDs");
        test::require(store.save("main", state("room:b")).has_value(), "durable replace");
        const auto backup = store.loadBackup("main"); test::require(backup && backup->at("room").get<std::string>() == "room:a", "previous snapshot backup");
        test::require(store.saveSettings({{"reduced_motion", picojson::value(true)}, {"master_volume", picojson::value(.2)}, {"remap", picojson::value(picojson::object{{"interact", picojson::value(std::string("Space"))}})}}).has_value(), "settings saved independently");
        f.text("{\"schema\":1"); auto corrupt = store.load("main");
        test::require(!corrupt && corrupt.error().front().code == "persistence.corrupt", "truncated save distinct");
        test::require(store.loadBackup("main").has_value(), "explicit backup recovery");
        test::require(!store.save("main", state("room:c")), "corrupt primary requires recovery rather than silent replacement");
        test::require(store.recoverBackup("main").has_value() && restarted.load("main")->at("room").get<std::string>() == "room:a", "explicit backup restoration repairs corrupt primary durably");
        auto settings = restarted.loadSettings(); test::require(settings && settings->at("reduced_motion").get<bool>(), "save corruption preserves settings");
    }
    void failuresRetainCommittedPrimary()
    {
        Fixture f; project::PersistenceStore normal(f.options()); test::require(normal.save("main", state("room:a")).has_value(), "fault fixture save");
        for (const auto stage : {project::PersistenceStage::Write, project::PersistenceStage::Flush, project::PersistenceStage::Replace})
        {
            auto options = f.options(); options.fail = [stage](auto boundary) { return stage == boundary; };
            project::PersistenceStore failing(options); auto saved = failing.save("main", state("room:b")); test::require(!saved, "I/O boundary failure reported");
            const auto loaded = normal.load("main"); test::require(loaded && loaded->at("room").get<std::string>() == "room:a", "failed write retains old primary");
            test::require(normal.loadBackup("main").has_value(), "failed write leaves recoverable backup");
            test::require(!std::filesystem::exists(std::filesystem::path(f.primary().native() + std::filesystem::path(".tmp").native())), "failed write cleans temporary");
        }
        test::require(normal.save("main", state("room:c")).has_value(), "retry succeeds after failures");
        auto blocked = f.options(); blocked.root = f.primary(); project::PersistenceStore unavailable(blocked);
        test::require(!unavailable.save("main", state("room:x")), "unwritable destination fails explicitly");
    }
    void versionsMigrateExplicitlyAndNeverOverwriteFuture()
    {
        Fixture f; project::PersistenceStore store(f.options()); test::require(store.save("main", state("room:a")).has_value(), "version fixture");
        f.text(R"({"schema":2,"content_version":1,"data":{"room":"room:future"}})"); auto future = store.load("main");
        test::require(!future && future.error().front().code == "persistence.version.newer", "future schema rejected");
        test::require(!store.save("main", state("room:b")), "future schema cannot be overwritten");
        test::require(!store.recoverBackup("main"), "future primary cannot be overwritten by recovery");
        f.text(R"({"schema":1,"content_version":3,"data":{"room":"room:future-content"}})");
        test::require(!store.save("main", state("room:b")), "future content cannot be overwritten");
        f.text(R"({"schema":1,"content_version":1,"data":{"room":"room:old","events":["event:verse"]}})");
        auto options = f.options(); options.schemaVersion = 2; project::PersistenceStore upgraded(options);
        const auto without = upgraded.load("main"); test::require(!without && without.error().front().code == "persistence.migration.required", "migration must be explicit");
        project::PersistenceMigration migration = [](unsigned schema, unsigned content, const auto& original) -> project::Result<picojson::object>
        { test::require(schema == 1 && content == 1, "migration receives old versions"); auto transformed = original; transformed["migrated"] = picojson::value(true); return transformed; };
        const auto migrated = upgraded.load("main", migration);
        test::require(migrated && migrated->at("events").get<picojson::array>()[0].get<std::string>() == "event:verse", "migration preserves stable progress IDs");
        test::require(!upgraded.load("main"), "migration load does not silently rewrite disk");
        test::require(upgraded.save("main", *migrated, migration).has_value() && upgraded.load("main").has_value(), "explicit migration commit survives restart");
        test::require(upgraded.loadBackup("main", migration).has_value(), "migration preserves old snapshot backup");
    }
    void confinementAndBounds()
    {
        Fixture f; project::PersistenceStore store(f.options());
        for (const auto* slot : {"../escape", "a/b", "C:\\escape", "", "a.json"}) test::require(!store.save(slot, state("room:a")), "slot path traversal rejected");
        auto small = f.options(); small.maxBytes = 32; project::PersistenceStore bounded(small);
        test::require(!bounded.save("main", state("room:a")), "byte bound");
        picojson::value nonfinite(0.0); nonfinite.get<double>() = std::numeric_limits<double>::infinity(); test::require(!store.save("main", {{"value", nonfinite}}), "nonfinite JSON rejected");
        auto shallow = f.options(); shallow.maxDepth = 1; project::PersistenceStore depth(shallow);
        test::require(!depth.save("main", {{"deep", picojson::value(picojson::object{{"more", picojson::value(picojson::object{{"v", picojson::value(true)}})}})}}), "object depth bound");
        test::require(store.save("main", state("room:a")).has_value(), "bounded read fixture");
        f.text(std::string(40, '[') + std::string(40, ']')); test::require(!store.load("main"), "JSON nesting rejected before parser recursion");
        const auto outside = f.root / "outside"; std::filesystem::create_directory(outside);
        const auto alias = f.root / "alias";
#ifdef _WIN32
        if (!CreateSymbolicLinkW(alias.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        {
            const auto command = L"cmd.exe /d /s /c mklink /J \"" + alias.native() + L"\" \"" + outside.native() + L"\"";
            test::require(_wsystem(command.c_str()) == 0, "junction fixture creation");
        }
#else
        std::filesystem::create_directory_symlink(outside, alias);
#endif
        auto escaped = f.options(); escaped.projectId = "alias"; project::PersistenceStore linked(escaped);
        const auto rejected = linked.save("main", state("room:x")); test::require(!rejected && rejected.error().front().code == "persistence.path.link", "linked user-data descendant rejected");
        test::require(std::filesystem::is_empty(outside), "link rejection writes nothing outside root"); std::filesystem::remove(alias);
    }
}
void runPersistenceTests()
{ freshRestartBackupAndCorruption(); failuresRetainCommittedPrimary(); versionsMigrateExplicitlyAndNeverOverwriteFuture(); confinementAndBounds(); }
#ifdef CPP_GAME_ENGINE_SERVICE_TEST_MAIN
int main() { try { runPersistenceTests(); std::cout << "[PASS] durable persistence service tests\n"; return 0; } catch (const std::exception& e) { std::cerr << "[FAIL] " << e.what() << '\n'; return 1; } }
#endif
