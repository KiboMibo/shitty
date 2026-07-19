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

#include <algorithm>
#include <cassert>

CharVdev::CharVdev(Fontpack* fontpk_)
    : px(fontpk_->getPx())
    , py(fontpk_->getPy())
{
}

bool CharVdev::resize(uint16_t pxWidth_, uint16_t pxHeight_) {
    assert(cells == nullptr);
    if (pxWidth == pxWidth_ && pxHeight == pxHeight_) {
        return false;
    }

    pxWidth = pxWidth_;
    pxHeight = pxHeight_;
    const int contentWidth = std::max(
        0, static_cast<int>(pxWidth) - 2 * opts.border);
    const int contentHeight = std::max(
        0, static_cast<int>(pxHeight) - 2 * opts.border);
    nCols = std::max(1, contentWidth / px);
    nRows = std::max(1, contentHeight / py);

    logI << "Resize to " << pxWidth << " x " << pxHeight
         << " pixels, " << nCols << " x " << nRows << " chars"
         << std::endl;

    cellStorage.assign(static_cast<size_t>(nCols) * nRows, Cell{});
    return true;
}

CharVdev::Mapping::Mapping(uint16_t nCols_, uint16_t nRows_, Cell*& cells_)
    : nCols(nCols_)
    , nRows(nRows_)
    , cells(cells_)
{
}

CharVdev::Mapping::~Mapping() {
    assert(cells != nullptr);
    cells = nullptr;
}

CharVdev::Mapping
CharVdev::getMapping() {
    assert(cells == nullptr);
    cells = cellStorage.data();
    return Mapping(nCols, nRows, cells);
}

void CharVdev::clearDirty() {
    assert(cells == nullptr);
    for (auto& cell : cellStorage) {
        cell.dirty = false;
    }
}

void CharVdev::setCursor(const Cursor& cursor_) {
    cursor = cursor_;
}

void CharVdev::setSelection(const Rect& selection_) {
    selection = selection_;
}
