#include "drop.h"

#include "input.h"

#include <std/ios/input.h>

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

using namespace plt;
using namespace stl;

namespace {
    const StringView uriListMime(u8"text/uri-list");
    const StringView utf8Mime(u8"text/plain;charset=utf-8");
    const StringView utf8StringMime(u8"UTF8_STRING");
    const StringView plainMime(u8"text/plain");
    const StringView fileScheme(u8"file://");
    const StringView localhostName(u8"localhost");
    // The canonical target buffers one payload whole before delivery, so a
    // hostile drag source must not be able to make it arbitrarily large.
    constexpr size_t payloadLimit = 16u << 20;

    int hexValue(u8 digit) {
        if (digit >= '0' && digit <= '9') {
            return digit - '0';
        }
        if (digit >= 'a' && digit <= 'f') {
            return digit - 'a' + 10;
        }
        if (digit >= 'A' && digit <= 'F') {
            return digit - 'A' + 10;
        }
        return -1;
    }

    StringView preferredMime(const DropOffer& offer) {
        bool uriList = false;
        bool utf8 = false;
        bool utf8String = false;
        bool plain = false;
        for (size_t index = 0; index != offer.formats(); ++index) {
            const StringView mime = offer.format(index);
            uriList = uriList || mime == uriListMime;
            utf8 = utf8 || mime == utf8Mime;
            utf8String = utf8String || mime == utf8StringMime;
            plain = plain || mime == plainMime;
        }
        if (uriList) {
            return uriListMime;
        }
        if (utf8) {
            return utf8Mime;
        }
        if (utf8String) {
            return utf8StringMime;
        }
        if (plain) {
            return plainMime;
        }
        return {};
    }

    struct SinkDropTarget final: public DropTarget {
        DropReply dragOver(const DropOffer& offer, i32 x, i32 y) override;
        void dragLeft() override;
        void dropped(Drop& drop) override;

        InputSink* sink = nullptr;
    };
}

DropReply SinkDropTarget::dragOver(const DropOffer& offer, i32, i32) {
    return {
        .mime = preferredMime(offer),
        .action = DropAction::Copy,
    };
}

void SinkDropTarget::dragLeft() {
}

void SinkDropTarget::dropped(Drop& drop) {
    const StringView mime = preferredMime(*drop.what());
    if (mime.empty()) {
        return;
    }
    const bool uris = mime == uriListMime;
    Input* const source = drop.read(mime);
    Buffer content;
    bool overflow = false;
    for (;;) {
        u8 chunk[16 * 1024];
        const size_t count = source->read(chunk, sizeof(chunk));
        if (count == 0) {
            break;
        }
        if (count > payloadLimit - content.length()) {
            overflow = true;
            break;
        }
        content.append(chunk, count);
    }
    // Deleting after an overflow abandons the transfer mid-stream.
    delete source;
    if (overflow || content.empty()) {
        return;
    }
    if (uris) {
        StringView payload(content);
        StringView entry;
        Buffer path;
        bool delivered = false;
        while (nextUriListEntry(payload, entry)) {
            path.reset();
            if (fileUriToPath(entry, path)) {
                sink->dropPath(StringView(path));
            } else {
                sink->dropPath(entry);
            }
            delivered = true;
        }
        if (delivered) {
            sink->flush();
        }
    } else {
        sink->drop(StringView(content));
        sink->flush();
    }
}

DropTarget* DropTarget::create(ObjPool& owner, InputSink& sink) {
    SinkDropTarget* const target = owner.make<SinkDropTarget>();
    target->sink = &sink;
    return target;
}

bool plt::nextUriListEntry(StringView& payload, StringView& entry) {
    const u8* const bytes = payload.data();
    const size_t length = payload.length();
    size_t index = 0;
    while (index != length) {
        const size_t lineStart = index;
        while (index != length && bytes[index] != '\n') {
            ++index;
        }
        size_t lineEnd = index;
        if (index != length) {
            ++index;
        }
        if (lineEnd != lineStart && bytes[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        if (lineEnd == lineStart || bytes[lineStart] == '#') {
            continue;
        }
        entry = StringView(bytes + lineStart, lineEnd - lineStart);
        payload = StringView(bytes + index, length - index);
        return true;
    }
    payload = {};
    return false;
}

bool plt::fileUriToPath(StringView uri, Buffer& path) {
    if (!uri.startsWith(fileScheme)) {
        return false;
    }
    const StringView rest = uri.suffix(uri.length() - fileScheme.length());
    size_t slash = 0;
    while (slash != rest.length() && rest[slash] != '/') {
        ++slash;
    }
    if (slash == rest.length()) {
        return false;
    }
    const StringView host = rest.prefix(slash);
    if (!host.empty() && !(host == localhostName)) {
        return false;
    }
    const StringView encoded = rest.suffix(rest.length() - slash);
    for (size_t index = 0; index != encoded.length();) {
        u8 byte = encoded[index];
        if (byte == '%') {
            if (encoded.length() - index < 3) {
                return false;
            }
            const int high = hexValue(encoded[index + 1]);
            const int low = hexValue(encoded[index + 2]);
            if (high < 0 || low < 0) {
                return false;
            }
            byte = (u8)(high * 16 + low);
            index += 3;
        } else {
            ++index;
        }
        path.append(&byte, 1);
    }
    return true;
}
