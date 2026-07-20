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
#include <std/sys/types.h>

#include "base.h"
#include "font_pack.h"
#include "terminal_types.h"

#include <cstdint>
#include <cstddef>
#include <vector>

class CharVdev {
public:
    explicit CharVdev(Fontpack* fontpk);
    ~CharVdev() = default;

    bool resize(u16 pxWidth_, u16 pxHeight_);
    u16 pixelWidth() const {
        return pxWidth;
    }
    u16 pixelHeight() const {
        return pxHeight;
    }
    u16 columns() const {
        return nCols;
    }
    u16 rows() const {
        return nRows;
    }

    struct Mapping {
        Mapping(u16 nCols_, u16 nRows_, TerminalCell*& cells_);
        ~Mapping();

        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;

        u16 nCols;
        u16 nRows;
        TerminalCell*& cells;
    };

    Mapping getMapping();

    const TerminalCell* cellData() const {
        return cellStorage.data();
    }
    size_t cellCount() const {
        return cellStorage.size();
    }
    void clearDirty();

    void setCursor(const TerminalCursor& cursor_);
    void setSelection(const Rect& selection_);
    const TerminalCursor& getCursor() const {
        return cursor;
    }
    const Rect& getSelection() const {
        return selection;
    }

private:
    u16 px;
    u16 py;
    u16 nCols = 0;
    u16 nRows = 0;
    u16 pxWidth = 0;
    u16 pxHeight = 0;

    std::vector<TerminalCell> cellStorage;
    TerminalCell* cells = nullptr;

    TerminalCursor cursor;
    Rect selection;
};
