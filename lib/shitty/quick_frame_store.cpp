/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "quick_frame_store.h"

#include "num.h"

#include <std/ios/fs_utils.h>
#include <std/lib/buffer.h>
#include <std/str/builder.h>
#include <std/sys/fd.h>
#include <std/sys/throw.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

using namespace stl;

bool defaultQuickFramePath(StringView configPath, StringBuilder& out) {
    if (configPath.empty()) {
        return false;
    }

    // The extension is the substring after the last '.' that comes after
    // the last '/', so a dotted directory name in the path never gets
    // mistaken for one.
    size_t afterSlash = 0;
    for (size_t at = 0; at < configPath.length(); ++at) {
        if (configPath[at] == '/') {
            afterSlash = at + 1;
        }
    }
    size_t extensionAt = configPath.length();
    for (size_t at = afterSlash; at < configPath.length(); ++at) {
        if (configPath[at] == '.') {
            extensionAt = at;
        }
    }

    out << StringView(configPath.data(), extensionAt) << StringView(u8"-quick-frame");
    return true;
}

namespace {

    // A single "key=value" line, value parsed as a plain signed integer.
    // False on any shape this doesn't recognize - the caller treats a
    // line it can't use exactly like one that was never there, rather
    // than failing the whole file over it.
    bool parseFrameLine(StringView line, StringView& outKey, i64& outValue) {
        StringView key;
        StringView value;
        if (!line.split('=', key, value)) {
            return false;
        }
        if (!parseI64(value.stripSpace(), outValue)) {
            return false;
        }
        outKey = key;
        return true;
    }
}

bool loadQuickFrame(StringView path, QuickFrame& out) {
    Buffer pathBuf{path};
    Buffer text;
    try {
        readFileContent(pathBuf, text);
    } catch (Exception&) {
        return false;
    }

    QuickFrame parsed;
    bool sawX = false;
    bool sawY = false;
    bool sawWidth = false;
    bool sawHeight = false;

    StringView remaining(text);
    while (!remaining.empty()) {
        StringView line;
        StringView rest;
        if (remaining.split('\n', line, rest)) {
            remaining = rest;
        } else {
            line = remaining;
            remaining = StringView();
        }
        line = line.stripCr().stripSpace();
        if (line.empty()) {
            continue;
        }

        StringView key;
        i64 value = 0;
        if (!parseFrameLine(line, key, value)) {
            continue;
        }

        if (key == StringView(u8"x")) {
            parsed.x = (i32)(value);
            sawX = true;
        } else if (key == StringView(u8"y")) {
            parsed.y = (i32)(value);
            sawY = true;
        } else if (key == StringView(u8"width") && value >= 0) {
            parsed.width = (u32)(value);
            sawWidth = true;
        } else if (key == StringView(u8"height") && value >= 0) {
            parsed.height = (u32)(value);
            sawHeight = true;
        }
    }

    if (!sawX || !sawY || !sawWidth || !sawHeight) {
        return false;
    }

    out = parsed;
    return true;
}

bool saveQuickFrame(StringView path, const QuickFrame& frame) {
    StringBuilder tmpPath;
    tmpPath << path << StringView(u8".tmp.") << (i64)(getpid());
    Buffer tmpPathBuf{StringView(tmpPath)};

    StringBuilder content;
    content << StringView(u8"x=") << (i64)(frame.x) << StringView(u8"\n");
    content << StringView(u8"y=") << (i64)(frame.y) << StringView(u8"\n");
    content << StringView(u8"width=") << (i64)(frame.width) << StringView(u8"\n");
    content << StringView(u8"height=") << (i64)(frame.height) << StringView(u8"\n");

    try {
        const int rawFd = ::open(tmpPathBuf.cStr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (rawFd < 0) {
            Errno().raise(StringBuilder() << StringView(u8"open() failed"));
        }
        ScopedFD fd(rawFd);
        const StringView written(content);
        fd.write(written.data(), written.length());
        fd.fsync();
        fd.close();
    } catch (Exception&) {
        ::unlink(tmpPathBuf.cStr());
        return false;
    }

    Buffer pathBuf{path};
    if (::rename(tmpPathBuf.cStr(), pathBuf.cStr()) < 0) {
        ::unlink(tmpPathBuf.cStr());
        return false;
    }
    return true;
}
