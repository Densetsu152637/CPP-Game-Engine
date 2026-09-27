#pragma once
#include <optional>
// Returns no value for the legacy demo command line.
std::optional<int> runCommands(int argc, char** argv);
