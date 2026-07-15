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

#include "base.h"
#include "fontpack.h"
#include "options.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace zutty
{
   // Character video device. It keeps the terminal's compact cell buffer and
   // rasterizes it into an RGBA8 image consumed by the Vulkan presenter.
   class CharVdev
   {
   public:
      explicit CharVdev (Fontpack* fontpk);
      ~CharVdev () = default;

      bool resize (uint16_t pxWidth_, uint16_t pxHeight_);
      void draw ();

      const uint8_t* pixelData () const { return pixels.data (); }
      size_t pixelBytes () const { return pixels.size (); }
      uint16_t pixelWidth () const { return pxWidth; }
      uint16_t pixelHeight () const { return pxHeight; }

      struct Cell
      {
         uint16_t uc_pt = ' ';
         uint8_t dwidth: 1;
         uint8_t dwidth_cont: 1;
         uint8_t bold: 1;
         uint8_t italic: 1;
         uint8_t underline: 1;
         uint8_t inverse: 1;
         uint8_t wrap: 1;
         uint8_t dirty: 1;
         uint16_t _fill0: 8;
         Color fg;
         uint8_t _fill1;
         Color bg;
         uint8_t _fill2;

         Cell ():
            dwidth (0), dwidth_cont (0),
            bold (0), italic (0), underline (0), inverse (0), wrap (0),
            dirty (0), fg (opts.fg), bg (opts.bg)
         {}

         using Ptr = std::shared_ptr <Cell>;

         bool operator == (const Cell& rhs) const
         {
            return memcmp (this, &rhs, sizeof (Cell)) == 0;
         }

         bool operator != (const Cell& rhs) const
         {
            return !operator == (rhs);
         }
      };
      static_assert (sizeof (Cell) == 12, "Cell size mismatch");

      static Cell::Ptr make_cells (uint16_t nCols, uint16_t nRows)
      {
         return std::shared_ptr <Cell> (new Cell [nRows * nCols],
                                        std::default_delete <Cell []> ());
      }

      struct Mapping
      {
         Mapping (uint16_t nCols_, uint16_t nRows_, Cell *& cells_);
         ~Mapping ();

         Mapping (const Mapping&) = delete;
         Mapping& operator= (const Mapping&) = delete;

         uint16_t nCols;
         uint16_t nRows;
         Cell *& cells;
      };

      Mapping getMapping ();

      struct Cursor
      {
         Color color = opts.cr;
         uint16_t posX = 0;
         uint16_t posY = 0;

         enum class Style: uint8_t
         {
            hidden = 0,
            filled_block = 1,
            hollow_block = 2
         };
         Style style = Style::hidden;
      };

      void setCursor (const Cursor& cursor_);
      void setSelection (const Rect& selection_);
      void setDeltaFrame (bool delta_);

   private:
      Fontpack* fontpk;
      uint16_t px;
      uint16_t py;
      uint16_t nCols = 0;
      uint16_t nRows = 0;
      uint16_t pxWidth = 0;
      uint16_t pxHeight = 0;

      std::vector <Cell> cellStorage;
      std::vector <uint8_t> pixels;
      Cell* cells = nullptr;

      Cursor cursor;
      Rect selection;
      bool delta = false;

      const Font& fontFor (const Cell& cell) const;
      const Font::AtlasPos& glyphPosition (const Font& font,
                                           uint16_t codepoint) const;
      bool isSelected (int col, int row) const;
      void renderCell (int col, int row, Cell& cell);
      void putPixel (int x, int y, const Color& color);
      void putBlendedPixel (int x, int y, const Color& fg,
                            const Color& bg, uint8_t alpha);
   };

} // namespace zutty
