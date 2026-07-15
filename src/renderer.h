/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "charvdev.h"
#include "frame.h"
#include "vkpresenter.h"

#include <SDL3/SDL.h>

#include <cstdint>

namespace zutty
{
   class Renderer
   {
   public:
      Renderer (SDL_Window* window, Fontpack* fontpk);
      ~Renderer () = default;

      Renderer (const Renderer&) = delete;
      Renderer& operator= (const Renderer&) = delete;

      void update (const Frame& frame);

   private:
      VulkanPresenter presenter;
      CharVdev charVdev;
      bool delta = false;
   };

} // namespace zutty
