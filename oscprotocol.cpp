#include "oscprotocol.h"

#include "base64.h"

#include <cctype>

Osc52Request parseOsc52(const std::string& argument) {
    Osc52Request request;
    const size_t separator = argument.find(';');
    if (separator == std::string::npos) {
        return request;
    }

    const std::string selectors = argument.substr(0, separator);
    const std::string payload = argument.substr(separator + 1);
    request.valid = true;
    request.primary = selectors.empty() ||
                      selectors.find('s') != std::string::npos ||
                      selectors.find('p') != std::string::npos;
    request.clipboard = selectors.empty() ||
                        selectors.find('c') != std::string::npos;
    request.query = payload == "?";
    if (!request.query) {
        if (!base64::decode(payload, request.content)) {
            request = {};
        }
    }
    return request;
}

std::string encodeOsc52Reply(const std::string& content) {
    return "\x1b]52;;" + base64::encode(content) + "\x1b\\";
}

std::string oscCwdToPath(const std::string& argument) {
    constexpr const char scheme[] = "file://";
    constexpr const size_t schemeLen = sizeof(scheme) - 1;

    std::string url = argument;
    if (url.compare(0, schemeLen, scheme) == 0) {
        const size_t pathStart = url.find('/', schemeLen);
        if (pathStart == std::string::npos) {
            return {};
        }
        url = url.substr(pathStart);
    }
    if (url.empty() || url[0] != '/') {
        return {};
    }

    std::string path;
    path.reserve(url.size());
    for (size_t k = 0; k < url.size(); ++k) {
        if (url[k] == '%' && k + 2 < url.size() &&
            std::isxdigit(static_cast<unsigned char>(url[k + 1])) &&
            std::isxdigit(static_cast<unsigned char>(url[k + 2]))) {
            const auto hexDigit = [](char ch) {
                return ch >= '0' && ch <= '9'
                           ? ch - '0'
                           : (ch | 0x20) - 'a' + 10;
            };
            path.push_back(static_cast<char>(
                hexDigit(url[k + 1]) * 16 + hexDigit(url[k + 2])));
            k += 2;
        } else {
            path.push_back(url[k]);
        }
    }
    return path;
}
