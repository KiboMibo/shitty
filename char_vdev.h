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
#include "font_pack.h"
#include "terminal_types.h"

#include <cstdint>
#include <cstddef>
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

    struct Mapping {
        Mapping(uint16_t nCols_, uint16_t nRows_, TerminalCell*& cells_);
        ~Mapping();

        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;

        uint16_t nCols;
        uint16_t nRows;
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
    uint16_t px;
    uint16_t py;
    uint16_t nCols = 0;
    uint16_t nRows = 0;
    uint16_t pxWidth = 0;
    uint16_t pxHeight = 0;

    std::vector<TerminalCell> cellStorage;
    TerminalCell* cells = nullptr;

    TerminalCursor cursor;
    Rect selection;
};
