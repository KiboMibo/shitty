/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <string>
#include <vector>

struct LaunchCommand {
    std::string executable;
    std::vector<std::string> arguments;
};

LaunchCommand buildLaunchCommand(int argc, char* argv[], const char* defaultShell, bool login);

void configureTerminalChildEnvironment();
