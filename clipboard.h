#pragma once

#include "oscprotocol.h"

#include <functional>
#include <string>

class ClipboardStore {
public:
    using ReadClipboard = std::function<std::string()>;
    using WriteClipboard = std::function<void(const std::string&)>;

    void setHandlers(ReadClipboard read, WriteClipboard write);
    void setPrimary(const std::string& content, bool autoCopy = false);
    void setClipboard(const std::string& content);
    bool copyPrimaryToClipboard();
    std::string get(bool primary) const;
    void apply(const Osc52Request& request);

private:
    std::string primarySelection;
    ReadClipboard readClipboard = [] { return std::string{}; };
    WriteClipboard writeClipboard = [](const std::string&) {};
};
