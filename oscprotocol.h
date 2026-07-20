#pragma once

#include <string>

struct Osc52Request {
    bool valid = false;
    bool query = false;
    bool primary = false;
    bool clipboard = false;
    std::string replySelector;
    std::string content;
};

Osc52Request parseOsc52(const std::string& argument,
                        bool selectClipboard = false);
std::string encodeOsc52Reply(const std::string& selector,
                             const std::string& content);
std::string encodeOsc52QueryReply(const Osc52Request& request,
                                  bool allowRead,
                                  const std::string& primary,
                                  const std::string& clipboard);
std::string oscCwdToPath(const std::string& argument);
