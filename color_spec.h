/* This file is part of Zutty.
 * Copyright (C) 2026 Zutty contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "base.h"

#include <string>

bool parseXColor(const std::string& spec, Color& color);
