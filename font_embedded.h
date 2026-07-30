/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <cstddef>

struct Composer;
struct FontResolver;

// A view of a font file compiled into the binary; data is null when the
// build has no font backend.
struct EmbeddedFontBlob {
    const void* data = nullptr;
    size_t size = 0;
};

EmbeddedFontBlob embeddedMonoFont();
EmbeddedFontBlob embeddedEmojiFont();
EmbeddedFontBlob embeddedEmojiTextFont();

FontResolver* createEmbeddedFontResolver(Composer& composer);
