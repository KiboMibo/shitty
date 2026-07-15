/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "renderer.h"

#include <cassert>

namespace zutty
{
   Renderer::Renderer (SDL_Window* window, Fontpack* fontpk)
      : charVdev (fontpk)
      , presenter (window, fontpk)
   {
   }

   void
   Renderer::update (const Frame& frame)
   {
      if (!frame)
         return;

      Frame currentFrame = frame;

      if (charVdev.resize (frame.winPx, frame.winPy))
         delta = false;

      {
         CharVdev::Mapping mapping = charVdev.getMapping ();
         assert (mapping.nCols == frame.nCols);
         assert (mapping.nRows == frame.nRows);
         if (delta)
            currentFrame.deltaCopyCells (mapping.cells);
         else
            currentFrame.fullCopyCells (mapping.cells);
      }

      charVdev.setCursor (frame.getCursor ());
      charVdev.setSelection (frame.getSnappedSelection ());
      if (presenter.present (charVdev, delta))
      {
         charVdev.clearDirty ();
         delta = true;
      }
      else
         delta = false;
   }

} // namespace zutty
