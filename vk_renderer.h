/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#pragma once

struct Composer;
struct GLFWwindow;
class Frame;

struct Renderer {
    virtual bool update(const Frame& frame) = 0;
    virtual bool repaint() = 0;

    static Renderer* create(Composer& composer, GLFWwindow* window);
};
