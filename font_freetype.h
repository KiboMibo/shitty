/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

struct Composer;

// null when the build has no FreeType backend.
FontRenderer* createFreeTypeFontRenderer(Composer& composer);
