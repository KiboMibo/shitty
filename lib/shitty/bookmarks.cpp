/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "bookmarks.h"

#include "toml.h"

#include <std/ios/fs_utils.h>
#include <std/ios/sys.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

using namespace stl;

bool defaultBookmarksPath(StringView configPath, StringBuilder& out) {
    if (configPath.empty()) {
        return false;
    }
    size_t afterSlash = 0;
    for (size_t at = 0; at < configPath.length(); ++at) {
        if (configPath[at] == '/') {
            afterSlash = at + 1;
        }
    }
    out << StringView(configPath.data(), afterSlash) << StringView(u8"bookmarks.toml");
    return true;
}

namespace {
    struct BookmarkSink final: public TomlSink {
        enum class Key : u8 {
            None,
            Title,
            Command,
            Directory,
        };

        StringView identifier;
        StringView path;
        ObjPool& pool;
        u64& nextId;
        Vector<Bookmark>& out;
        Bookmark entry;
        Key pending = Key::None;
        // Inside a [[bookmark]] table, as opposed to before the first one
        // or inside some other table.
        bool open = false;
        bool broken = false;
        // Nested values have no place in a bookmark; their scalars are
        // skipped rather than mistaken for the key that opened them.
        int depth = 0;

        BookmarkSink(StringView identifier_, StringView path_, ObjPool& pool_, u64& nextId_, Vector<Bookmark>& out_)
            : identifier(identifier_)
            , path(path_)
            , pool(pool_)
            , nextId(nextId_)
            , out(out_)
        {
        }

        void warn(const char* what, StringView name) {
            sysE << identifier << StringView(u8": ") << path << StringView(u8": ") << StringView(what);
            if (!name.empty()) {
                sysE << StringView(u8": ") << name;
            }
            sysE << endL;
        }

        void finish() {
            if (!open) {
                return;
            }
            Bookmark done = entry;
            const bool wasBroken = broken;
            entry = Bookmark();
            broken = false;
            pending = Key::None;
            if (wasBroken) {
                // The offending key warned already; half a bookmark
                // would open something the user did not write.
                return;
            }
            if (done.command.empty() && done.directory.empty()) {
                warn("bookmark without a command or a dir", done.title);
                return;
            }
            if (done.title.empty()) {
                done.title = done.command.empty() ? done.directory : done.command;
            }
            done.id = nextId++;
            out.pushBack(done);
        }

        bool tomlTable(const StringView* segments, size_t count, bool array) override {
            finish();
            open = false;
            if (count == 1 && segments[0] == StringView(u8"bookmark")) {
                if (array) {
                    open = true;
                } else {
                    warn("bookmark is an array of tables, write [[bookmark]]", StringView());
                }
                return true;
            }
            warn("only [[bookmark]] tables belong in this file, ignoring", count != 0 ? segments[0] : StringView());
            return true;
        }

        bool tomlKey(const StringView* segments, size_t count) override {
            pending = Key::None;
            if (depth != 0) {
                return true;
            }
            const StringView name = count != 0 ? segments[count - 1] : StringView();
            if (!open) {
                warn("key outside a [[bookmark]] table, ignoring", name);
                return true;
            }
            if (count == 1 && name == StringView(u8"title")) {
                pending = Key::Title;
            } else if (count == 1 && name == StringView(u8"command")) {
                pending = Key::Command;
            } else if (count == 1 && name == StringView(u8"dir")) {
                pending = Key::Directory;
            } else {
                warn("unknown bookmark key, ignoring", name);
            }
            return true;
        }

        bool tomlScalar(TomlType type, StringView text) override {
            const Key key = pending;
            pending = Key::None;
            if (depth != 0 || key == Key::None) {
                return true;
            }
            if (type != TomlType::String) {
                warn("bookmark value is not a string", text);
                broken = true;
                return true;
            }
            const StringView value = pool.intern(text);
            if (key == Key::Title) {
                entry.title = value;
            } else if (key == Key::Command) {
                entry.command = value;
            } else {
                entry.directory = value;
            }
            return true;
        }

        bool nestedBegin() {
            if (depth == 0 && pending != Key::None) {
                warn("bookmark value is not a string", StringView());
                broken = true;
            }
            pending = Key::None;
            ++depth;
            return true;
        }

        bool nestedEnd() {
            if (depth != 0) {
                --depth;
            }
            return true;
        }

        bool tomlArrayBegin() override {
            return nestedBegin();
        }

        bool tomlArrayEnd() override {
            return nestedEnd();
        }

        bool tomlInlineTableBegin() override {
            return nestedBegin();
        }

        bool tomlInlineTableEnd() override {
            return nestedEnd();
        }

        void tomlError(size_t line, StringView message) override {
            sysE << identifier << StringView(u8": ") << path << StringView(u8":") << line << StringView(u8": ") << message << StringView(u8"; ignoring the rest of the file") << endL;
            // What came before the error stands; the entry it cut short
            // does not.
            open = false;
            entry = Bookmark();
        }
    };

    u32 appendString(Buffer& storage, StringView text) {
        const u32 offset = (u32)(storage.used());
        storage.append(text.data(), text.length());
        storage.append("", 1);
        return offset;
    }
}

void parseBookmarks(StringView text, StringView identifier, StringView path, ObjPool& pool, u64& nextId, Vector<Bookmark>& out) {
    BookmarkSink sink(identifier, path, pool, nextId, out);
    parseToml(text, sink);
    sink.finish();
}

void loadBookmarks(StringView path, StringView identifier, ObjPool& pool, u64& nextId, Vector<Bookmark>& out) {
    if (path.empty()) {
        return;
    }
    Buffer pathBuf{path};
    Buffer text;
    try {
        readFileContent(pathBuf, text);
    } catch (Exception&) {
        return;
    }
    parseBookmarks(StringView(text), identifier, path, pool, nextId, out);
}

LaunchCommand bookmarkLaunchCommand(const LaunchCommand& shell, StringView command) {
    LaunchCommand launch;
    launch.executableOffset = appendString(launch.storage, StringView(shell.executable()));
    for (size_t at = 0; at < shell.offsets.length(); ++at) {
        launch.offsets.pushBack(appendString(launch.storage, StringView(shell.argument(at))));
    }
    if (!command.empty()) {
        launch.offsets.pushBack(appendString(launch.storage, StringView(u8"-c")));
        launch.offsets.pushBack(appendString(launch.storage, command));
    }
    return launch;
}

const Bookmark* BookmarkShelf::find(u64 id) const {
    const size_t at = indexOf(id);
    return at < items.length() ? &items[at] : nullptr;
}

size_t BookmarkShelf::indexOf(u64 id) const {
    for (size_t at = 0; at < items.length(); ++at) {
        if (items[at].id == id) {
            return at;
        }
    }
    return items.length();
}

void bookmarkStatus(const Bookmark& bookmark, bool open, StringBuilder& out) {
    out.reset();
    if (open) {
        out << StringView(u8"open · ");
    } else {
        out << StringView(u8"not open · ");
    }
    out << (bookmark.command.empty() ? bookmark.directory : bookmark.command);
}
