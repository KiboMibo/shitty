#include "clipboard.h"

#include <utility>


namespace stl {}
using namespace stl;

void ClipboardStore::setHandlers(ReadClipboard read, WriteClipboard write) {
    readClipboard = std::move(read);
    writeClipboard = std::move(write);
}

void ClipboardStore::setPrimary(const std::string& content, bool autoCopy) {
    primarySelection = content;
    if (autoCopy) {
        writeClipboard(content);
    }
}

void ClipboardStore::setClipboard(const std::string& content) {
    writeClipboard(content);
}

bool ClipboardStore::copyPrimaryToClipboard() {
    if (primarySelection.empty()) {
        return false;
    }
    writeClipboard(primarySelection);
    return true;
}

std::string ClipboardStore::get(bool primary) const {
    return primary ? primarySelection : readClipboard();
}

void ClipboardStore::apply(const Osc52Request& request) {
    if (!request.valid || request.query) {
        return;
    }
    if (request.primary) {
        setPrimary(request.content);
    }
    if (request.clipboard) {
        setClipboard(request.content);
    }
}
