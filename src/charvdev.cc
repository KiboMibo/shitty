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

#include "charvdev.h"
#include "log.h"
#include "utf8.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace zutty
{
   CharVdev::CharVdev (Fontpack* fontpk_)
      : fontpk (fontpk_)
      , px (fontpk->getPx ())
      , py (fontpk->getPy ())
   {
   }

   bool
   CharVdev::resize (uint16_t pxWidth_, uint16_t pxHeight_)
   {
      assert (cells == nullptr);
      if (pxWidth == pxWidth_ && pxHeight == pxHeight_)
         return false;

      pxWidth = pxWidth_;
      pxHeight = pxHeight_;
      const int contentWidth = std::max (
         0, static_cast <int> (pxWidth) - 2 * opts.border);
      const int contentHeight = std::max (
         0, static_cast <int> (pxHeight) - 2 * opts.border);
      nCols = std::max (1, contentWidth / px);
      nRows = std::max (1, contentHeight / py);

      logI << "Resize to " << pxWidth << " x " << pxHeight
           << " pixels, " << nCols << " x " << nRows << " chars"
           << std::endl;

      cellStorage.assign (static_cast <size_t> (nCols) * nRows, Cell {});
      pixels.resize (static_cast <size_t> (pxWidth) * pxHeight * 4);
      return true;
   }

   const Font&
   CharVdev::fontFor (const Cell& cell) const
   {
      if (cell.bold && cell.italic && fontpk->hasBoldItalic ())
         return fontpk->getBoldItalic ();
      if (cell.bold && fontpk->hasBold ())
         return fontpk->getBold ();
      if (cell.italic && fontpk->hasItalic ())
         return fontpk->getItalic ();
      return fontpk->getRegular ();
   }

   const Font::AtlasPos&
   CharVdev::glyphPosition (const Font& font, uint16_t codepoint) const
   {
      const auto& map = font.getAtlasMap ();
      auto glyph = map.find (codepoint);
      if (glyph != map.end ())
         return glyph->second;

      const uint16_t fallback =
         (codepoint >= 0xd800 && codepoint < 0xe000) || codepoint >= 0xfffe
            ? Unicode_Replacement_Character
            : Missing_Glyph_Marker;
      glyph = map.find (fallback);
      if (glyph != map.end ())
         return glyph->second;

      static const Font::AtlasPos blank {};
      return blank;
   }

   bool
   CharVdev::isSelected (int col, int row) const
   {
      if (selection.rectangular)
      {
         return row >= selection.tl.y && row <= selection.br.y &&
                col >= selection.tl.x && col < selection.br.x;
      }

      return (row > selection.tl.y && row < selection.br.y) ||
             (row == selection.tl.y && col >= selection.tl.x &&
              (row < selection.br.y || col < selection.br.x)) ||
             (row == selection.br.y && col < selection.br.x &&
              (row > selection.tl.y || col > selection.tl.x));
   }

   void
   CharVdev::putPixel (int x, int y, const Color& color)
   {
      if (x < 0 || y < 0 || x >= pxWidth || y >= pxHeight)
         return;
      const size_t offset = (static_cast <size_t> (y) * pxWidth + x) * 4;
      pixels [offset + 0] = color.red;
      pixels [offset + 1] = color.green;
      pixels [offset + 2] = color.blue;
      pixels [offset + 3] = 0xff;
   }

   void
   CharVdev::putBlendedPixel (int x, int y, const Color& fg,
                              const Color& bg, uint8_t alpha)
   {
      const unsigned inv = 255 - alpha;
      putPixel (x, y, Color {
         static_cast <uint8_t> ((fg.red * alpha + bg.red * inv + 127) / 255),
         static_cast <uint8_t> ((fg.green * alpha + bg.green * inv + 127) / 255),
         static_cast <uint8_t> ((fg.blue * alpha + bg.blue * inv + 127) / 255)
      });
   }

   void
   CharVdev::renderCell (int col, int row, Cell& cell)
   {
      if (cell.dwidth_cont)
      {
         cell.dirty = false;
         return;
      }

      bool doubleWidth = cell.dwidth && col + 1 < nCols &&
                         cellStorage [row * nCols + col + 1].dwidth_cont;
      const int cellWidth = doubleWidth ? 2 * px : px;
      const int originX = opts.border + col * px;
      const int originY = opts.border + row * py;

      Color fg = cell.fg;
      Color bg = cell.bg;
      if (cell.inverse != isSelected (col, row))
         std::swap (fg, bg);

      Color cursorColor = cursor.color;
      if (cursorColor == bg)
      {
         cursorColor.red = 255 - cursorColor.red;
         cursorColor.green = 255 - cursorColor.green;
         cursorColor.blue = 255 - cursorColor.blue;
      }

      const bool cursorHere = col == cursor.posX && row == cursor.posY;
      if (cursorHere && cursor.style == Cursor::Style::filled_block)
      {
         fg = bg;
         bg = cursorColor;
      }

      const Font* font = nullptr;
      if (doubleWidth && fontpk->hasDoubleWidth ())
         font = &fontpk->getDoubleWidth ();
      else if (!doubleWidth)
         font = &fontFor (cell);

      if (font != nullptr)
      {
         const Font::AtlasPos& position = glyphPosition (*font, cell.uc_pt);
         const int atlasWidth = font->getNx () * font->getPx ();
         const int glyphX = position.x * font->getPx ();
         const int glyphY = position.y * font->getPy ();
         const auto& atlas = font->getAtlas ();

         for (int y = 0; y < py; ++y)
         {
            for (int x = 0; x < cellWidth; ++x)
            {
               const size_t source = static_cast <size_t> (glyphY + y) *
                                     atlasWidth + glyphX + x;
               const uint8_t alpha = source < atlas.size () ? atlas [source] : 0;
               putBlendedPixel (originX + x, originY + y, fg, bg, alpha);
            }
         }
      }
      else
      {
         for (int y = 0; y < py; ++y)
         {
            for (int x = 0; x < cellWidth; ++x)
            {
               const bool box = x == 1 || x == cellWidth - 2 ||
                                y == 1 || y == py - 2;
               putBlendedPixel (originX + x, originY + y,
                                fg, bg, box ? 178 : 0);
            }
         }
      }

      if (cell.underline)
         for (int x = 0; x < cellWidth; ++x)
            putPixel (originX + x, originY + py - 1, fg);

      if (opts.showWraps && cell.wrap)
         for (int y = 0; y < py; y += 2)
            putPixel (originX + cellWidth - 1, originY + y, fg);

      if (cursorHere && cursor.style == Cursor::Style::hollow_block)
      {
         for (int x = 0; x < cellWidth; ++x)
         {
            putPixel (originX + x, originY, cursorColor);
            putPixel (originX + x, originY + py - 1, cursorColor);
         }
         for (int y = 1; y + 1 < py; ++y)
         {
            putPixel (originX, originY + y, cursorColor);
            putPixel (originX + cellWidth - 1, originY + y, cursorColor);
         }
      }

      cell.dirty = false;
   }

   void
   CharVdev::draw ()
   {
      assert (cells == nullptr);
      if (pixels.empty ())
         return;

      for (size_t offset = 0; offset < pixels.size (); offset += 4)
      {
         pixels [offset + 0] = opts.bg.red;
         pixels [offset + 1] = opts.bg.green;
         pixels [offset + 2] = opts.bg.blue;
         pixels [offset + 3] = 0xff;
      }

      for (int row = 0; row < nRows; ++row)
         for (int col = 0; col < nCols; ++col)
            renderCell (col, row, cellStorage [row * nCols + col]);
   }

   CharVdev::Mapping::Mapping (uint16_t nCols_, uint16_t nRows_, Cell *& cells_)
      : nCols (nCols_)
      , nRows (nRows_)
      , cells (cells_)
   {
   }

   CharVdev::Mapping::~Mapping ()
   {
      assert (cells != nullptr);
      cells = nullptr;
   }

   CharVdev::Mapping
   CharVdev::getMapping ()
   {
      assert (cells == nullptr);
      cells = cellStorage.data ();
      return Mapping (nCols, nRows, cells);
   }

   void
   CharVdev::setCursor (const Cursor& cursor_)
   {
      cursor = cursor_;
   }

   void
   CharVdev::setSelection (const Rect& selection_)
   {
      selection = selection_;
   }

   void
   CharVdev::setDeltaFrame (bool delta_)
   {
      delta = delta_;
      (void)delta;
   }

} // namespace zutty
