/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "color.h"

#include <std/str/view.h>

bool parseXColor(stl::StringView spec, Color& color);
