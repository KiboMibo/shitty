/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "char_vdev.h"
#include "frame.h"
#include "vk_presenter.h"

#include <cstdint>

class Renderer {
public:
    Renderer(GLFWwindow* window, Fontpack* fontpk);
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool update(const Frame& frame);
    bool repaint();

private:
    CharVdev charVdev;
    VulkanPresenter presenter;
    bool delta = false;
};
