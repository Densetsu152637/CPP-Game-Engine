#include <exception>
#include <iostream>

void test_lua_script_system_lifecycle_and_engine_api();
void test_lua_script_system_reports_runtime_errors_and_cleans_up();
void test_lua_script_system_cleans_up_failed_initialization();
void test_lua_script_system_validates_lifecycle_and_isolates_scripts();
void test_lua_script_system_translates_native_exceptions();

int main()
{
    try
    {
        test_lua_script_system_lifecycle_and_engine_api();
        test_lua_script_system_reports_runtime_errors_and_cleans_up();
        test_lua_script_system_cleans_up_failed_initialization();
        test_lua_script_system_validates_lifecycle_and_isolates_scripts();
        test_lua_script_system_translates_native_exceptions();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
    std::cout << "[PASS] Lua scripting tests\n";
    return 0;
}
