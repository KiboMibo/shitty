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

#include "char_vdev.h"
#include "log.h"

#include <algorithm>
#include <cassert>


namespace stl {}
using namespace stl;

CharVdev::CharVdev(Fontpack* fontpk_)
    : px(fontpk_->getPx())
    , py(fontpk_->getPy())
{
}

bool CharVdev::resize(u16 pxWidth_, u16 pxHeight_) {
    assert(cells == nullptr);
    if (pxWidth == pxWidth_ && pxHeight == pxHeight_) {
        return false;
    }

    pxWidth = pxWidth_;
    pxHeight = pxHeight_;
    const int contentWidth = std::max(0, (int)(pxWidth)-2 * opts.border);
    const int contentHeight = std::max(0, (int)(pxHeight)-2 * opts.border);
    nCols = std::max(1, contentWidth / px);
    nRows = std::max(1, contentHeight / py);

    logI << "Resize to " << pxWidth << " x " << pxHeight << " pixels, " << nCols << " x " << nRows << " chars" << std::endl;

    cellStorage.assign((size_t)(nCols)*nRows, RenderCell{});
    return true;
}

CharVdev::Mapping::Mapping(u16 nCols_, u16 nRows_, RenderCell*& cells_)
    : nCols(nCols_)
    , nRows(nRows_)
    , cells(cells_)
{
}

CharVdev::Mapping::~Mapping() {
    assert(cells != nullptr);
    cells = nullptr;
}

CharVdev::Mapping CharVdev::getMapping() {
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

void CharVdev::setCursor(const TerminalCursor& cursor_) {
    cursor = cursor_;
}

void CharVdev::setSelection(const Rect& selection_) {
    selection = selection_;
}
