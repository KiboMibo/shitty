/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "vk_renderer.h"

#include <cassert>

Renderer::Renderer(GLFWwindow* window, Fontpack* fontpk)
    : charVdev(fontpk)
    , presenter(window, fontpk)
{
}

bool Renderer::update(const Frame& frame) {
    if (!frame) {
        return false;
    }

    if (charVdev.resize(frame.winPx, frame.winPy)) {
        delta = false;
    }

    {
        CharVdev::Mapping mapping = charVdev.getMapping();
        assert(mapping.nCols == frame.nCols);
        assert(mapping.nRows == frame.nRows);
        if (delta) {
            frame.deltaCopyCells(mapping.cells);
        } else {
            frame.fullCopyCells(mapping.cells);
        }
    }

    charVdev.setCursor(frame.getCursor());
    charVdev.setSelection(frame.getSnappedSelection());
    if (presenter.present(charVdev, frame, delta)) {
        charVdev.clearDirty();
        delta = true;
        return true;
    } else {
        delta = false;
        return false;
    }
}

bool Renderer::repaint() {
    return presenter.repaint();
}
