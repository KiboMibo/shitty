#include "fontresolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <system_error>
#include <tuple>
#include <vector>

namespace {
    namespace fs = std::filesystem;

    std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        return value;
    }

    enum class Style { None, Regular, Bold, Italic, BoldItalic };

    Style classify(const std::string& suffix) {
        if (suffix.empty() || suffix == "r" || suffix == "regular")
            return Style::Regular;
        if (suffix == "b" || suffix == "bold") return Style::Bold;
        if (suffix == "i" || suffix == "it" || suffix == "italic" ||
            suffix == "o" || suffix == "ob" || suffix == "oblique")
            return Style::Italic;
        if (suffix == "bi" || suffix == "boldit" ||
            suffix == "bolditalic")
            return Style::BoldItalic;
        return Style::None;
    }

    struct Candidate {
        fs::path path;
        std::string extension;
        Style style;
    };

    FontVariants resolveRoot(const fs::path& root,
                             const std::string& family) {
        std::error_code error;
        if (!fs::is_directory(root, error)) return {};

        std::vector<fs::path> paths;
        fs::recursive_directory_iterator iterator(
            root, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error)) paths.push_back(iterator->path());
            iterator.increment(error);
        }
        std::sort(paths.begin(), paths.end());

        using GroupKey = std::pair<std::string, std::string>;
        std::map<GroupKey, FontVariants> groups;
        const std::string familyLower = lower(family);
        for (const auto& path : paths) {
            const std::string filename = lower(path.filename().string());
            std::string extension;
            if (filename.size() >= 7 &&
                filename.compare(filename.size() - 7, 7, ".pcf.gz") == 0) {
                extension = ".pcf.gz";
            } else {
                extension = lower(path.extension().string());
            }
            if (extension != ".ttc" && extension != ".ttf" &&
                extension != ".otf" && extension != ".pcf" &&
                extension != ".pcf.gz")
                continue;
            const std::string stem = filename.substr(
                0, filename.size() - extension.size());
            if (stem.compare(0, familyLower.size(), familyLower) != 0)
                continue;
            std::string suffix = stem.substr(familyLower.size());
            if (!suffix.empty() &&
                (suffix[0] == '-' || suffix[0] == '_' || suffix[0] == ' '))
                suffix.erase(suffix.begin());
            const Style style = classify(suffix);
            if (style == Style::None) continue;

            FontVariants& group = groups[{path.parent_path().string(), extension}];
            const std::string value = path.string();
            if (style == Style::Regular) group.regular = value;
            else if (style == Style::Bold) group.bold = value;
            else if (style == Style::Italic) group.italic = value;
            else group.boldItalic = value;
        }

        for (const auto& entry : groups) {
            if (!entry.second.regular.empty()) return entry.second;
        }
        return {};
    }
}

FontVariants resolveFontTree(
    const std::string& fontpath, const std::string& fontname) {
    size_t start = 0;
    while (true) {
        const size_t separator = fontpath.find(':', start);
        const std::string root = fontpath.substr(
            start, separator == std::string::npos
                       ? std::string::npos : separator - start);
        FontVariants variants = resolveRoot(root, fontname);
        if (!variants.regular.empty() || separator == std::string::npos)
            return variants;
        start = separator + 1;
    }
}
