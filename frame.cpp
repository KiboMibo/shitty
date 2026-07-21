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

#include "frame.h"
#include "log.h"

#include <utf8proc.h>

#ifdef DEBUG
    #include <sstream>
#endif

namespace {

    u32 wordClass(u32 codepoint) {
        constexpr u32 whitespaceClass = 0x110000;
        constexpr u32 identifierClass = 0x110001;
        switch (utf8proc_category(codepoint)) {
            case UTF8PROC_CATEGORY_LU:
            case UTF8PROC_CATEGORY_LL:
            case UTF8PROC_CATEGORY_LT:
            case UTF8PROC_CATEGORY_LM:
            case UTF8PROC_CATEGORY_LO:
            case UTF8PROC_CATEGORY_MN:
            case UTF8PROC_CATEGORY_MC:
            case UTF8PROC_CATEGORY_ME:
            case UTF8PROC_CATEGORY_ND:
            case UTF8PROC_CATEGORY_NL:
            case UTF8PROC_CATEGORY_NO:
            case UTF8PROC_CATEGORY_PC:
                return identifierClass;
            case UTF8PROC_CATEGORY_ZS:
            case UTF8PROC_CATEGORY_ZL:
            case UTF8PROC_CATEGORY_ZP:
                return whitespaceClass;
            default:
                // Adjacent repetitions of one punctuation/symbol codepoint form
                // a useful selectable run, while unlike punctuation stays split.
                return codepoint;
        }
    }

}

Frame::Frame() {
}

void Frame::setBlinkState(bool visible, bool cursor) {
    blinkVisible = visible;
    cursorBlink = cursor;
}

void Frame::setScreenReverseVideo(bool enabled) {
    screenReverseVideo = enabled;
    expose();
}

void Frame::setSelectionColor(bool foreground, Color color, bool enabled) {
    const u8 bit = foreground ? 1 : 2;
    if (foreground) {
        selectionForeground = color;
    } else {
        selectionBackground = color;
    }
    if (enabled) {
        selectionColorMask |= bit;
    } else {
        selectionColorMask &= ~bit;
    }
    expose();
}

u32 Frame::internGrapheme(const u32* codepoints, size_t size) {
    if (size < 2) {
        return 0;
    }

    size_t hash = 1469598103934665603ull;
    for (size_t index = 0; index < size; ++index) {
        hash ^= codepoints[index];
        hash *= 1099511628211ull;
    }
    const auto [begin, end] = graphemes->ids.equal_range(hash);
    for (auto item = begin; item != end; ++item) {
        const Grapheme& candidate = graphemes->values[item->second];
        if (candidate.size() == size && std::equal(candidate.begin(), candidate.end(), codepoints)) {
            return item->second;
        }
    }

    const u32 id = graphemes->values.size();
    graphemes->values.emplace_back(codepoints, codepoints + size);
    graphemes->ids.emplace(hash, id);
    return id;
}

const Frame::Grapheme& Frame::getGrapheme(u32 id) const {
    if (id >= graphemes->values.size()) {
        return graphemes->values.front();
    }
    return graphemes->values[id];
}

Frame::Frame(u16 winPx_, u16 winPy_, u16 nCols_, u16 nRows_, u16& marginTop_, u16& marginBottom_, u16 saveLines_)
    : winPx(winPx_)
    , winPy(winPy_)
    , nCols(nCols_)
    , nRows(nRows_)
    , saveLines(saveLines_)
    , viewOffset(0)
    , cells(TerminalCell::make(nCols, nRows + saveLines))
    , screen(nRows)
{
    for (RowId row = 0; row < nRows; ++row) {
        screen[row] = row;
    }
    for (RowId row = nRows; row < nRows + saveLines; ++row) {
        freeRows.push_back(row);
    }
    marginTop_ = 0;
    marginBottom_ = nRows;
    damage.totalCells = nCols * (nRows + saveLines);
    highMemUsageReport();
}

void Frame::dropScrollbackHistory() {
    viewOffset = 0;
    if (!selection.null() && selection.tl.y < 0) {
        selection.clear();
    }
    while (!history.empty()) {
        freeRows.push_back(history.front());
        history.pop_front();
    }
    expose();
}

void Frame::collectHyperlinkIds(std::set<u32>& ids) const {
    const auto collectRow = [&](RowId row) {
        const TerminalCell* first = cells.get() + row * nCols;
        for (u16 column = 0; column < nCols; ++column) {
            if (first[column].hyperlink != 0) {
                ids.insert(first[column].hyperlink);
            }
        }
    };
    for (RowId row : screen) {
        collectRow(row);
    }
    for (RowId row : history) {
        collectRow(row);
    }
}

void Frame::recolorPalette(u16 index, Color color) {
    if (!cells) {
        return;
    }
    const size_t count = (size_t)(nCols) * (nRows + saveLines);
    for (size_t i = 0; i < count; ++i) {
        auto& cell = cells.get()[i];
        if (cell.fg_index == index) {
            cell.fg = color;
        }
        if (cell.bg_index == index) {
            cell.bg = color;
        }
        if (cell.underline_index == index) {
            cell.underline_color = color;
        }
    }
    expose();
}

void Frame::recolorDefault(bool foreground, Color color) {
    if (!cells) {
        return;
    }
    const size_t count = (size_t)(nCols) * (nRows + saveLines);
    for (size_t i = 0; i < count; ++i) {
        auto& cell = cells.get()[i];
        if (foreground) {
            if (cell.fg_index == -2) {
                cell.fg = color;
            }
            if (cell.underline_index == -2) {
                cell.underline_color = color;
            }
        } else if (cell.bg_index == -2) {
            cell.bg = color;
        }
    }
    expose();
}

void Frame::resize(u16 winPx_, u16 winPy_, u16 nCols_, u16 nRows_, u16& marginTop_, u16& marginBottom_) {
    if (winPx == winPx_ && winPy == winPy_) {
        return;
    }

    winPx = winPx_;
    winPy = winPy_;

    if (nCols == nCols_ && nRows == nRows_) {
        return;
    }

    const u16 oldViewOffset = viewOffset;
    const u16 historyCount = history.size();
    const int rowLen = std::min(nCols, nCols_);
    const int nCopyRows = std::min(nRows, nRows_);
    auto newCells = TerminalCell::make(nCols_, nRows_ + saveLines);
    TerminalCell* p = newCells.get();
    for (int pY = 0; pY < nCopyRows; ++pY) {
        memcpy(p, getLogicalRowPtr(pY), rowLen * cellSize);
        p += nCols_;
    }
    p = newCells.get() + nRows_ * nCols_;
    for (int pY = -historyCount; pY < 0; ++pY) {
        memcpy(p, getLogicalRowPtr(pY), rowLen * cellSize);
        p += nCols_;
    }

    // A column shrink can copy the leading half of a wide glyph while
    // clipping its continuation.  Never publish or retain such a partial
    // cell: editing code relies on the same lead/continuation invariant.
    const auto normalizeWideRow = [nCols_](TerminalCell* row) {
        for (u16 column = 0; column < nCols_; ++column) {
            const bool orphanLead = row[column].dwidth && (column + 1 == nCols_ || !row[column + 1].dwidth_cont);
            const bool orphanContinuation = row[column].dwidth_cont && (column == 0 || !row[column - 1].dwidth);
            if (orphanLead || orphanContinuation) {
                row[column] = TerminalCell{};
            }
        }
    };
    for (int row = 0; row < nCopyRows; ++row) {
        normalizeWideRow(newCells.get() + row * nCols_);
    }
    for (int row = 0; row < historyCount; ++row) {
        normalizeWideRow(newCells.get() + (nRows_ + row) * nCols_);
    }

    cells = std::move(newCells);
    nCols = nCols_;
    nRows = nRows_;
    screen.resize(nRows);
    for (RowId row = 0; row < nRows; ++row) {
        screen[row] = row;
    }
    history.clear();
    for (RowId row = nRows; row < nRows + historyCount; ++row) {
        history.push_back(row);
    }
    freeRows.clear();
    for (RowId row = nRows + historyCount; row < nRows + saveLines; ++row) {
        freeRows.push_back(row);
    }
    marginTop_ = 0;
    marginBottom_ = nRows;
    viewOffset = std::min<u16>(oldViewOffset, historyCount);
    if (!selectionValid()) {
        selection.clear();
    }
    damage.totalCells = nCols * (nRows + saveLines);
    expose();
    highMemUsageReport();
}

void Frame::fullCopyCells(TerminalCell* const dst) const {
    TerminalCell* p = dst;
    for (int pY = 0; pY < nRows; ++pY) {
        memcpy(p, getViewRowPtr(pY), nCols * cellSize);
        p += nCols;
    }
}

void Frame::deltaCopyCells(TerminalCell* const dst) const {
    TerminalCell* p = dst;
    for (int pY = -viewOffset; pY < nRows - viewOffset; ++pY) {
        damageDeltaCopy(p, nCols * getLogicalRow(pY), nCols);
        p += nCols;
    }
}

Rect Frame::getSelectionForView() const {
    if (!selectionValid()) {
        return {};
    }

    Rect ret = selection;
    if (!ret.null()) {
        ret.tl.y += viewOffset;
        ret.br.y += viewOffset;
    }
    return ret;
}

Rect Frame::getSnappedSelection() const {
    Rect ret = selection;

    if (ret.null()) {
        return ret;
    }
    if (!selectionValid()) {
        return {};
    }

    if (selection.rectangular) {
        ret.tl.y += viewOffset;
        ret.br.y += viewOffset;
        return ret;
    }

    switch (snapTo) {
        case SelectSnapTo::Char:
            break;
        case SelectSnapTo::Word: {
            const auto cellLead = [this](const TerminalCell* row, int x) {
                x = std::max(0, std::min(x, (int)(nCols)-1));
                return row[x].dwidth_cont && x > 0 ? x - 1 : x;
            };
            const auto expand = [this, &cellLead](int rowIndex, int x) {
                const auto* row = getLogicalRowPtr(rowIndex);
                int left = cellLead(row, x);
                const u32 selectedClass = wordClass(row[left].uc_pt);
                while (left > 0) {
                    const int previous = cellLead(row, left - 1);
                    if (wordClass(row[previous].uc_pt) != selectedClass) {
                        break;
                    }
                    left = previous;
                }

                int right = left;
                while (right < nCols) {
                    const int lead = cellLead(row, right);
                    if (wordClass(row[lead].uc_pt) != selectedClass) {
                        break;
                    }
                    right = lead + (row[lead].dwidth ? 2 : 1);
                }
                return std::pair<int, int>{left, right};
            };

            ret.tl.x = expand(ret.tl.y, ret.tl.x).first;
            ret.br.x = expand(ret.br.y, ret.br.x).second;
        } break;
        case SelectSnapTo::Line:
            ret.tl.x = 0;
            ret.br.x = nCols;
            break;
        default:
            break;
    }

    ret.tl.y += viewOffset;
    ret.br.y += viewOffset;
    return ret;
}

bool Frame::getSelectedUtf8(std::string& utf8_selection) const {
    Rect sel = getSnappedSelection();

    if (sel.empty()) {
        return false;
    }
    sel.tl.y -= viewOffset;
    sel.br.y -= viewOffset;

    using unicodeString = std::vector<u32>;
    std::vector<unicodeString> lines;
    bool wrap = false;

    auto addLine = [&](int y, u16 x1, u16 x2) {
        unicodeString line;
        bool wrapBack = wrap;
        wrap = false;
        const auto* cp = getLogicalRowPtr(y);
        for (u16 x = x1; x < x2; ++x) {
            const auto& cell = cp[x];
            if (!cell.dwidth_cont) {
                const auto& grapheme = getGrapheme(cell.grapheme);
                if (grapheme.empty()) {
                    line.push_back(cell.uc_pt);
                } else {
                    line.insert(line.end(), grapheme.begin(), grapheme.end());
                }
            }
            if (cell.wrap) {
                wrap = true;
                break;
            }
        }

        // Trim screen padding only when a linear selection consumes the rest
        // of the row.  Explicitly selected whitespace (word or rectangle)
        // is data and must survive copying.
        while (!wrap && !sel.rectangular && x2 == nCols && line.size() && line.back() == ' ') {
            line.pop_back();
        }

        if (wrapBack && lines.size()) {
            lines.back().insert(lines.back().end(), line.begin(), line.end());
        } else {
            lines.push_back(line);
        }
    };

    if (sel.tl.y == sel.br.y) {
        addLine(sel.tl.y, sel.tl.x, sel.br.x);
    } else if (sel.rectangular) {
        for (int y = sel.tl.y; y <= sel.br.y; ++y) {
            addLine(y, sel.tl.x, sel.br.x);
        }
    } else {
        addLine(sel.tl.y, sel.tl.x, nCols);
        for (int y = sel.tl.y + 1; y < sel.br.y; ++y) {
            addLine(y, 0, nCols);
        }
        addLine(sel.br.y, 0, sel.br.x);
    }

    std::vector<char> utf8_out;
    auto sinkFn = [&](char ch) {
        utf8_out.push_back(ch);
    };
    for (const auto& codepoints : lines) {
        for (u32 cp : codepoints) {
            Utf8Encoder::pushUnicode(cp, sinkFn);
        }
        utf8_out.push_back('\n');
    }
    while (utf8_out.size() && utf8_out.back() == '\n') {
        utf8_out.pop_back();
    }

    utf8_selection = std::string(utf8_out.data(), utf8_out.size());

#if DEBUG
    if (utf8_selection.size() <= 80) {
        logT << "Selected " << utf8_selection.size() << " bytes:\n'" << utf8_selection << "'" << std::endl;
    } else {
        logT << "Selected " << utf8_selection.size() << " bytes:\n'" << utf8_selection.substr(0, 40) << "' ...\n'" << utf8_selection.substr(utf8_selection.size() - 40) << "'" << std::endl;
    }
#endif
    return true;
}

void Frame::damageDeltaCopy(TerminalCell* dst, u32 start, u32 count) const {
    u32 end = start + count;

    if (damage.end <= start || end <= damage.start) {
        return;
    }

    if (start < damage.start) {
        dst += (damage.start - start);
        start = damage.start;
    }

    if (damage.end < end) {
        end = damage.end;
    }

    TerminalCell* const src = cells.get();

    for (size_t i = 0, j = start; j < end; ++i, ++j) {
        if (dst[i] != src[j]) {
            dst[i] = src[j];
            dst[i].dirty = 1;
        }
    }
}

void Frame::highMemUsageReport() {
    auto allocKB = damage.totalCells * cellSize / 1024;
    if (allocKB > 8192) {
        logI << "Allocated " << allocKB << " KiB for cell storage; consider "
             << "decreasing saveLines (current value: " << saveLines << ") to reduce memory usage!" << std::endl;
    }
}

void Frame::setCursorPos(u16 pY, u16 pX) {
    cursor.posY = pY;
    cursor.posX = pX;
}

TerminalCursor Frame::getCursor() const {
    TerminalCursor ret = cursor;
    ret.posY += viewOffset;
    return ret;
}

Point Frame::getLogicalPoint(Point point) const {
    point.y -= viewOffset;
    return point;
}

void Frame::setCursorStyle(TerminalCursor::Style cs) {
    cursor.style = cs;
}

void Frame::setCursorColor(Color color) {
    cursor.color = color;
}

void Frame::pageUp(u16 count) {
    u16 viewOffset_ = std::min<size_t>(viewOffset + count, history.size());
    viewOffset = viewOffset_;
    expose();
}

void Frame::pageDown(u16 count) {
    u16 viewOffset_ = std::max(0, viewOffset - count);
    viewOffset = viewOffset_;
    expose();
}

bool Frame::pageToBottom() {
    if (!viewOffset) {
        return false;
    }

    viewOffset = 0;
    expose();
    return true;
}

void Frame::scrollUp(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    const bool capture = top == 0 && saveLines;
    if (!capture) {
        vscrollSelection(top, bottom, -count, false);
    }

    for (u16 k = 0; k < count; ++k) {
        const RowId outgoing = screen[top];
        RowId incoming = outgoing;

        if (capture) {
            if (history.size() == saveLines) {
                incoming = history.front();
                history.pop_front();
            } else {
                incoming = freeRows.back();
                freeRows.pop_back();
            }
            history.push_back(outgoing);
        }

        for (u16 row = top; row + 1 < bottom; ++row) {
            screen[row] = screen[row + 1];
        }
        screen[bottom - 1] = incoming;
    }

    if (capture) {
        vscrollSelection(top, bottom, -count, true);
    }

    if (capture && viewOffset) {
        viewOffset = std::min<size_t>(viewOffset + count, history.size());
    }
    expose();
}

void Frame::scrollDown(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    vscrollSelection(top, bottom, count, false);

    for (u16 k = 0; k < count; ++k) {
        const RowId incoming = screen[bottom - 1];
        for (u16 row = bottom - 1; row > top; --row) {
            screen[row] = screen[row - 1];
        }
        screen[top] = incoming;
    }

    expose();
}

void Frame::restoreHistory(u16 count) {
    count = std::min<size_t>(count, history.size());
    for (u16 k = 0; k < count; ++k) {
        const RowId incoming = history.back();
        history.pop_back();
        const RowId outgoing = screen.back();
        for (u16 row = nRows - 1; row > 0; --row) {
            screen[row] = screen[row - 1];
        }
        screen[0] = incoming;
        freeRows.push_back(outgoing);
    }
    viewOffset = viewOffset > count ? viewOffset - count : 0;
    if (!selection.null()) {
        selection.tl.y += count;
        selection.br.y += count;
    }
    expose();
}

const TerminalCell& Frame::getCell(u16 pY, u16 pX) const {
    return operator[](getIdx(pY, pX));
}

TerminalCell& Frame::getCell(u16 pY, u16 pX) {
    u32 idx = getIdx(pY, pX);
    damage.add(idx, idx + 1);
    if (!selection.empty()) {
        invalidateSelection(Rect(pX, pY));
    }
    return operator[](idx);
}

TerminalCell* Frame::writeSpan(u16 pY, u16 startX, u16 count) {
    const u32 idx = getIdx(pY, startX);
    damage.add(idx, idx + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, pY, startX + count, pY));
    }
    return cells.get() + idx;
}

const TerminalCell& Frame::getViewCell(u16 pY, u16 pX) const {
    return getViewRowPtr(pY)[pX];
}

void Frame::fillCells(u16 ch, const TerminalCell& attrs) {
    for (u16 r = 0; r < nRows; ++r) {
        u32 start = getIdx(r, 0);
        u32 end = start + nCols;
        for (u32 k = start; k < end; ++k) {
            cells.get()[k] = attrs;
            cells.get()[k].uc_pt = ch;
        }
        damage.add(start, end);
    }
}

void Frame::eraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    if (!count) {
        return;
    }

#ifdef DEBUG
    if (nCols < startX + count || nRows <= pY) {
        std::ostringstream oss;
        oss << "Frame::eraseInRow (pY=" << pY << " startX=" << startX << " count=" << count << ") out of bounds, nCols=" << nCols << ", nRows=" << nRows;
        throw std::runtime_error(oss.str());
    }
#endif
    u32 idx = getIdx(pY, startX);
    TerminalCell erased = attrs;
    erased.line_attr = static_cast<const Frame&>(*this).getCell(pY, 0).line_attr;
    if (startX == 0 && count == nCols) {
        if (!erasedRowTemplateValid || erasedRowTemplate.size() != nCols || erasedRowCell != erased) {
            erasedRowTemplate.assign(nCols, erased);
            erasedRowCell = erased;
            erasedRowTemplateValid = true;
        }
        memcpy(cells.get() + idx, erasedRowTemplate.data(), nCols * cellSize);
        damage.add(idx, idx + count);
    } else {
        eraseRange(idx, idx + count, erased);
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, pY, startX + count, pY));
    }
}

void Frame::selectiveEraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    TerminalCell erased = attrs;
    erased.uc_pt = ' ';
    erased.protected_char = 0;
    erased.hyperlink = 0;
    erased.grapheme = 0;
    for (u16 x = startX; x < startX + count; ++x) {
        const u32 index = getIdx(pY, x);
        auto& cell = operator[](index);
        if (!cell.protected_char) {
            erased.line_attr = cell.line_attr;
            cell = erased;
            cell.dirty = 1;
            damage.add(index, index + 1);
            if (!selection.empty()) {
                invalidateSelection(Rect(x, pY));
            }
        }
    }
    expose();
}

void Frame::moveInRow(u16 pY, u16 dstX, u16 srcX, u16 count) {
    if (!count) {
        return;
    }

#ifdef DEBUG
    if (nCols < dstX + count || nCols < srcX + count || nRows <= pY) {
        std::ostringstream oss;
        oss << "Frame::moveInRow (pY=" << pY << " dstX=" << dstX << " srcX=" << srcX << " count=" << count << ") out of bounds, nCols=" << nCols << ", nRows=" << nRows;
        throw std::runtime_error(oss.str());
    }
#endif
    u32 dstIdx = getIdx(pY, dstX);
    u32 srcIdx = getIdx(pY, srcX);
    moveCells(dstIdx, srcIdx, count);
    if (!selection.empty()) {
        invalidateSelection(Rect(dstX, pY, dstX + count, pY));
    }
}

void Frame::copyRow(u16 dstY, u16 srcY, u16 startX, u16 count) {
    if (!count) {
        return;
    }

#ifdef DEBUG
    if (nCols < startX + count || nRows <= dstY || nRows <= srcY) {
        std::ostringstream oss;
        oss << "Frame::copyRow (dstY=" << dstY << " srcY=" << srcY << " startX=" << startX << " count=" << count << ") out of bounds, nCols=" << nCols << ", nRows=" << nRows;
        throw std::runtime_error(oss.str());
    }
#endif
    u32 dstIdx = getIdx(dstY, startX);
    u32 srcIdx = getIdx(srcY, startX);
    copyCells(dstIdx, srcIdx, count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, dstY, startX + count, dstY));
    }
}

void Frame::invalidateSelection(const Rect&& damage) {
    if (selection.empty()) {
        return;
    }

    if (selection.rectangular) {
        const bool outsideRows = damage.tl.y > selection.br.y || damage.br.y < selection.tl.y;
        const bool outsideColumns = damage.br.x <= selection.tl.x || selection.br.x <= damage.tl.x;
        if (outsideRows || outsideColumns) {
            return;
        }
        selection.clear();
        return;
    }

    if (selection.br <= damage.tl || damage.br <= selection.tl) {
        return;
    }

    selection.clear();
}

bool Frame::selectionValid() const {
    if (selection.null()) {
        return true;
    }

    const int firstRow = -(int)(history.size());
    const auto valid = [&](Point point) {
        return point.x >= 0 && point.x <= nCols &&
               point.y >= firstRow && point.y < nRows;
    };
    return valid(selection.tl) && valid(selection.br);
}

void Frame::vscrollSelection(u16 top, u16 bottom, int vertOffset, bool captureHistory) {
    if (selection.null()) {
        return;
    }

    if (captureHistory) {
        if (selection.tl.y >= bottom) {
            return;
        }
        if (selection.br.y >= bottom) {
            selection.clear();
            return;
        }
        selection.tl.y += vertOffset;
        selection.br.y += vertOffset;
        if (selection.tl.y < -(int)(history.size())) {
            selection.clear();
        }
        return;
    }

    const bool topInside = selection.tl.y >= top && selection.tl.y < bottom;
    const bool bottomInside = selection.br.y >= top && selection.br.y < bottom;
    if (!topInside && !bottomInside) {
        if (selection.br.y < top || selection.tl.y >= bottom) {
            return;
        }
        selection.clear();
        return;
    }
    if (topInside != bottomInside) {
        selection.clear();
        return;
    }

    selection.tl.y += vertOffset;
    selection.br.y += vertOffset;
    if (selection.tl.y < top || selection.br.y >= bottom) {
        selection.clear();
    }
}

Frame::RowId Frame::getLogicalRow(int pY) const {
    if (pY < 0) {
        const int index = (int)(history.size()) + pY;
        return history[(size_t)(index)];
    }
    return screen[pY];
}

const TerminalCell* Frame::getLogicalRowPtr(int pY) const {
    return &operator[](nCols* getLogicalRow(pY));
}

const TerminalCell* Frame::getViewRowPtr(int pY) const {
    return getLogicalRowPtr(pY - viewOffset);
}

u32 Frame::getIdx(u16 pY, u16 pX) const {
#ifdef DEBUG
    if (nCols <= pX || nRows <= pY) {
        std::ostringstream oss;
        oss << "Frame::getIdx (pY=" << pY << " pX=" << pX << ") out of bounds, nCols=" << nCols << ", nRows=" << nRows;
        throw std::runtime_error(oss.str());
    }
#endif
    return nCols * screen[pY] + pX;
}

const TerminalCell& Frame::operator[](u32 idx) const {
    return cells.get()[idx];
}

TerminalCell& Frame::operator[](u32 idx) {
    return cells.get()[idx];
}

void Frame::eraseRange(u32 start, u32 end, const TerminalCell& attrs) {
    TerminalCell* ca = &(cells.get()[start]);
    TerminalCell* const cz = ca - start + end;
    damage.add(start, end);
    while (ca < cz) {
        *ca++ = attrs;
    }
}

void Frame::copyCells(u32 dstIx, u32 srcIx, u32 count) {
    memcpy(cells.get() + dstIx, cells.get() + srcIx, count * cellSize);
    damage.add(dstIx, dstIx + count);
}

void Frame::moveCells(u32 dstIx, u32 srcIx, u32 count) {
    memmove(cells.get() + dstIx, cells.get() + srcIx, count * cellSize);
    damage.add(dstIx, dstIx + count);
}

void Frame::Damage::reset() {
    start = 0;
    end = 0;
}

void Frame::Damage::expose() {
    start = 0;
    end = totalCells;
}

void Frame::Damage::add(u32 start_, u32 end_) {
    if (end_ < start_) {
        start_ = 0;
        end_ = totalCells;
    }

    if (start == end) {
        start = start_;
        end = end_;
    } else {
        start = std::min(start, start_);
        end = std::max(end, end_);
    }
}
