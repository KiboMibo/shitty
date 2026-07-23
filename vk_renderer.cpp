/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "vk_renderer.h"

#include "char_vdev.h"
#include "composer.h"
#include "font_pack.h"
#include "frame.h"
#include "vk_presenter.h"

#include <std/mem/obj_pool.h>

#include <cassert>


namespace stl {}
using namespace stl;

namespace {

    class RendererImpl final: public Renderer {
    public:
        RendererImpl(GLFWwindow* window, Fontpack* fontpk);

        bool update(const Frame& frame) override;
        bool repaint() override;

    private:
        CharVdev charVdev;
        VulkanPresenter presenter;
        bool delta = false;
    };

}

RendererImpl::RendererImpl(GLFWwindow* window, Fontpack* fontpk)
    : charVdev(fontpk)
    , presenter(window, fontpk)
{
}

bool RendererImpl::update(const Frame& frame) {
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
    }

    delta = false;
    return false;
}

bool RendererImpl::repaint() {
    return presenter.repaint();
}

Renderer* Renderer::create(Composer& composer, GLFWwindow* window) {
    return composer.pool->make<RendererImpl>(window, composer.fonts);
}
