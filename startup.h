#pragma once

#include <string>
#include <vector>

struct LaunchCommand {
    std::string executable;
    std::vector<std::string> arguments;
};

LaunchCommand buildLaunchCommand(int argc, char* argv[], const char* defaultShell, bool login);

void configureTerminalChildEnvironment();
