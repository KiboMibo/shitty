/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vt_config.h"

#include "darts.h"

using namespace stl;

bool VtConfig::uriSchemeAllowed(StringView scheme) const {
    u8 folded[128];
    if (scheme.length() > sizeof(folded)) {
        return false;
    }
    for (size_t index = 0; index < scheme.length(); ++index) {
        const u8 byte = scheme[index];
        folded[index] = byte >= 'A' && byte <= 'Z' ? (u8)(byte + ('a' - 'A')) : byte;
    }
    return uriSchemeTrie->find(StringView(folded, scheme.length())) != Darts::missing;
}
