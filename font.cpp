/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font.h"

#include "font_freetype.h"

// font_freetype.cpp only enters the build when the FreeType backend is
// available; this stub keeps the factory linkable everywhere else.
#if !(defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ))
FontRenderer* createFreeTypeFontRenderer(Composer& composer) {
    (void)(composer);
    return nullptr;
}
#endif
