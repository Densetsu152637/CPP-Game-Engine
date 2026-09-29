#include <exception>
#include <iostream>

#include "test/automation/mcp_tests.h"

void test_asset_index_is_deterministic_tracks_dependencies_and_roundtrips();
void test_asset_index_rejects_shader_include_escape();
void test_asset_index_rejects_oversized_lua_source();
void test_lua_validation_compiles_without_executing_and_stages_only_valid_source();
void test_project_template_package_and_runtime_lookup_are_root_scoped();

int main()
{
    try
    {
        test_asset_index_is_deterministic_tracks_dependencies_and_roundtrips();
        test_asset_index_rejects_shader_include_escape();
        test_asset_index_rejects_oversized_lua_source();
        test_lua_validation_compiles_without_executing_and_stages_only_valid_source();
        test_project_template_package_and_runtime_lookup_are_root_scoped();
        test_mcp_stdio_initialization_and_tools_list();
        test_mcp_stdio_confines_paths_and_bounds_messages();
        test_mcp_protocol_errors_negotiation_notifications_and_diagnostics();
        std::cout << "[PASS] tooling and automation tests\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
