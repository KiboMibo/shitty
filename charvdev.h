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
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

class CharVdev {
public:
    explicit CharVdev(Fontpack* fontpk);
    ~CharVdev() = default;

    bool resize(uint16_t pxWidth_, uint16_t pxHeight_);
    uint16_t pixelWidth() const {
        return pxWidth;
    }
    uint16_t pixelHeight() const {
        return pxHeight;
    }
    uint16_t columns() const {
        return nCols;
    }
    uint16_t rows() const {
        return nRows;
    }

    struct Cell {
        uint32_t uc_pt = ' ';
        uint8_t dwidth : 1;
        uint8_t dwidth_cont : 1;
        uint8_t bold : 1;
        uint8_t italic : 1;
        uint8_t underline : 1;
        uint8_t inverse : 1;
        uint8_t wrap : 1;
        uint8_t dirty : 1;
        uint8_t faint : 1;
        uint8_t blink : 1;
        uint8_t conceal : 1;
        uint8_t strike : 1;
        uint8_t overline : 1;
        uint8_t underline_style : 3;
        uint16_t _fill0 = 0;
        Color fg;
        uint8_t _fill1;
        Color bg;
        uint8_t _fill2;
        Color underline_color;
        uint8_t _fill3;
        uint32_t hyperlink = 0;
        uint32_t grapheme = 0;
        int32_t fg_index = -2;
        int32_t bg_index = -2;
        int32_t underline_index = -2;

        Cell()
            : dwidth(0)
            , dwidth_cont(0)
            , bold(0)
            , italic(0)
            , underline(0)
            , inverse(0)
            , wrap(0)
            , dirty(0)
            , faint(0)
            , blink(0)
            , conceal(0)
            , strike(0)
            , overline(0)
            , underline_style(0)
            , fg(opts.fg)
            , bg(opts.bg)
            , underline_color(opts.fg)
        {
        }

        using Ptr = std::shared_ptr<Cell>;

        bool operator==(const Cell& rhs) const {
            return memcmp(this, &rhs, sizeof(Cell)) == 0;
        }

        bool operator!=(const Cell& rhs) const {
            return !operator==(rhs);
        }
    };
    static_assert(sizeof(Cell) == 40, "Cell size mismatch");
    static_assert(offsetof(Cell, uc_pt) == 0, "Cell codepoint offset mismatch");
    static_assert(offsetof(Cell, fg) == 8, "Cell foreground offset mismatch");
    static_assert(offsetof(Cell, bg) == 12, "Cell background offset mismatch");
    static_assert(offsetof(Cell, underline_color) == 16,
                  "Cell underline offset mismatch");
    static_assert(offsetof(Cell, hyperlink) == 20,
                  "Cell hyperlink offset mismatch");
    static_assert(offsetof(Cell, grapheme) == 24,
                  "Cell grapheme offset mismatch");
    static_assert(offsetof(Cell, fg_index) == 28,
                  "Cell foreground source offset mismatch");

    static Cell::Ptr make_cells(uint16_t nCols, uint16_t nRows) {
        return std::shared_ptr<Cell>(new Cell[nRows * nCols],
                                     std::default_delete<Cell[]>());
    }

    struct Mapping {
        Mapping(uint16_t nCols_, uint16_t nRows_, Cell*& cells_);
        ~Mapping();

        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;

        uint16_t nCols;
        uint16_t nRows;
        Cell*& cells;
    };

    Mapping getMapping();

    const Cell* cellData() const {
        return cellStorage.data();
    }
    size_t cellCount() const {
        return cellStorage.size();
    }
    void clearDirty();

    struct Cursor {
        Color color = opts.cr;
        uint16_t posX = 0;
        uint16_t posY = 0;

        enum class Style : uint8_t {
            hidden = 0,
            filled_block = 1,
            hollow_block = 2,
            underline = 3,
            bar = 4
        };
        Style style = Style::hidden;
    };

    void setCursor(const Cursor& cursor_);
    void setSelection(const Rect& selection_);
    const Cursor& getCursor() const {
        return cursor;
    }
    const Rect& getSelection() const {
        return selection;
    }

private:
    uint16_t px;
    uint16_t py;
    uint16_t nCols = 0;
    uint16_t nRows = 0;
    uint16_t pxWidth = 0;
    uint16_t pxHeight = 0;

    std::vector<Cell> cellStorage;
    Cell* cells = nullptr;

    Cursor cursor;
    Rect selection;
};
