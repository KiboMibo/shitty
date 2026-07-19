#pragma once

#include <string>

struct Osc52Request {
    bool valid = false;
    bool query = false;
    bool primary = false;
    bool clipboard = false;
    std::string content;
};

Osc52Request parseOsc52(const std::string& argument);
std::string encodeOsc52Reply(const std::string& content);
std::string oscCwdToPath(const std::string& argument);
