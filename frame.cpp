/* This file is part of Shitty.
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

#include <algorithm>
#include <cassert>


namespace stl {}
using namespace stl;

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

GraphemeView Frame::getGrapheme(u32 ref) const {
    return extras == nullptr ? GraphemeView{} : extras->grapheme(ref);
}

Frame::Frame(u16 winPx_, u16 winPy_, u16 nCols_, u16 nRows_, u16& marginTop_, u16& marginBottom_, const TerminalColors* colors_, CellExtraStore* extras_, u16 saveLines_)
    : winPx(winPx_)
    , winPy(winPy_)
    , nCols(nCols_)
    , nRows(nRows_)
    , saveLines(saveLines_)
    , viewOffset(0)
    , cells(TerminalCell::make(nCols, nRows + saveLines))
    , colors(colors_)
    , extras(extras_)
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

void Frame::collectExtraRefLocations(stl::Vector<u32*>& locations) {
    const auto collectRow = [&](RowId row) {
        TerminalCell* first = cells.get() + row * nCols;
        for (u16 column = 0; column < nCols; ++column) {
            if (first[column].hasExtra()) {
                locations.pushBack(&first[column].payload);
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

Frame::ResizeState Frame::resize(u16 winPx_, u16 winPy_, u16 nCols_, u16 nRows_, u16& marginTop_, u16& marginBottom_, ResizeState state, bool reflow) {
    if (winPx == winPx_ && winPy == winPy_) {
        return state;
    }

    winPx = winPx_;
    winPy = winPy_;

    if (nCols == nCols_ && nRows == nRows_) {
        return state;
    }

    if (reflow && nCols != nCols_) {
        struct LogicalLine {
            std::vector<TerminalCell> cells;
            bool reflowable = true;
        };
        struct Anchor {
            int oldRow = 0;
            int oldColumn = 0;
            size_t line = 0;
            size_t offset = 0;
            Point mapped;
            bool found = false;
        };
        struct Boundary {
            size_t row = 0;
            int column = 0;
        };

        const int oldHistoryCount = history.size();
        const bool wasScrolled = viewOffset != 0;
        const int oldTotalRows = oldHistoryCount + nRows;
        Anchor cursorAnchor{
            oldHistoryCount + state.cursor.y,
            state.cursor.x + (state.pendingWrap ? 1 : 0),
        };
        Anchor viewAnchor{oldHistoryCount - viewOffset, 0};
        Anchor screenAnchor{oldHistoryCount, 0};
        Anchor selectionStart;
        Anchor selectionEnd;
        const bool keepSelection = !selection.null() && !selection.rectangular;
        if (keepSelection) {
            selectionStart.oldRow = oldHistoryCount + selection.tl.y;
            selectionStart.oldColumn = selection.tl.x;
            selectionEnd.oldRow = oldHistoryCount + selection.br.y;
            selectionEnd.oldColumn = selection.br.x;
        }
        std::vector<Anchor*> anchors = {&cursorAnchor, &viewAnchor, &screenAnchor};
        if (keepSelection) {
            anchors.push_back(&selectionStart);
            anchors.push_back(&selectionEnd);
        }

        const auto cellHasContent = [](const TerminalCell& source) {
            TerminalCell cell = source;
            cell.wrap = 0;
            cell.dirty = 0;
            cell.line_attr = 0;
            return cell != TerminalCell{};
        };

        std::vector<LogicalLine> lines;
        bool continueLine = false;
        for (int oldRow = 0; oldRow < oldTotalRows; ++oldRow) {
            const TerminalCell* row = getLogicalRowPtr(oldRow - oldHistoryCount);
            const bool normalWidth = row[0].line_attr == 0;
            const bool join = continueLine && normalWidth;
            if (!join) {
                lines.emplace_back();
            }
            LogicalLine& line = lines.back();
            line.reflowable &= normalWidth;
            const size_t rowOffset = line.cells.size();

            int contentEnd = 0;
            int wrapEnd = 0;
            for (int column = 0; column < nCols; ++column) {
                if (cellHasContent(row[column])) {
                    contentEnd = column + 1;
                }
                if (row[column].wrap) {
                    wrapEnd = column + 1;
                }
            }
            int copyEnd = wrapEnd ? wrapEnd : contentEnd;
            if (!normalWidth) {
                copyEnd = nCols;
            }
            for (Anchor* anchor : anchors) {
                if (anchor->oldRow == oldRow) {
                    anchor->line = lines.size() - 1;
                    anchor->offset = rowOffset + std::min(anchor->oldColumn, (int)(nCols));
                    anchor->found = true;
                    copyEnd = std::max(copyEnd, std::min(anchor->oldColumn, (int)(nCols)));
                }
            }
            for (int column = 0; column < copyEnd; ++column) {
                TerminalCell cell = row[column];
                cell.wrap = 0;
                cell.dirty = 0;
                line.cells.push_back(cell);
            }
            continueLine = wrapEnd && normalWidth;
        }

        std::vector<std::vector<TerminalCell>> output;
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            LogicalLine& line = lines[lineIndex];
            std::vector<Boundary> boundaries(line.cells.size() + 1);
            const size_t lineOutputStart = output.size();
            size_t outputRow = output.size();
            output.emplace_back(nCols_);
            int column = 0;
            boundaries[0] = {outputRow, 0};

            if (line.reflowable) {
                size_t offset = 0;
                while (offset < line.cells.size()) {
                    const bool wide = line.cells[offset].dwidth
                        && offset + 1 < line.cells.size()
                        && line.cells[offset + 1].dwidth_cont;
                    const size_t width = wide ? 2 : 1;
                    if (width > nCols_) {
                        boundaries[offset] = {outputRow, column};
                        ++offset;
                        boundaries[offset] = {outputRow, column};
                        if (wide) {
                            ++offset;
                            boundaries[offset] = {outputRow, column};
                        }
                        continue;
                    }
                    if (column + (int)(width) > nCols_) {
                        output[outputRow][column ? column - 1 : nCols_ - 1].wrap = 1;
                        outputRow = output.size();
                        output.emplace_back(nCols_);
                        column = 0;
                    }
                    boundaries[offset] = {outputRow, column};
                    for (size_t cellIndex = 0; cellIndex < width; ++cellIndex) {
                        TerminalCell cell = line.cells[offset + cellIndex];
                        cell.wrap = 0;
                        cell.dirty = 0;
                        output[outputRow][column++] = cell;
                        boundaries[offset + cellIndex + 1] = {outputRow, column};
                    }
                    offset += width;
                    if (column == nCols_ && offset < line.cells.size()) {
                        output[outputRow][nCols_ - 1].wrap = 1;
                        outputRow = output.size();
                        output.emplace_back(nCols_);
                        column = 0;
                        boundaries[offset] = {outputRow, 0};
                    }
                }
            } else {
                const size_t count = std::min<size_t>(line.cells.size(), nCols_);
                for (size_t offset = 0; offset < count; ++offset) {
                    output[outputRow][offset] = line.cells[offset];
                    output[outputRow][offset].wrap = 0;
                    output[outputRow][offset].dirty = 0;
                    boundaries[offset] = {outputRow, (int)(offset)};
                    boundaries[offset + 1] = {outputRow, (int)(offset + 1)};
                }
                for (size_t offset = count + 1; offset < boundaries.size(); ++offset) {
                    boundaries[offset] = {outputRow, (int)(count)};
                }
            }

            const auto normalizeWideRow = [nCols_](std::vector<TerminalCell>& row) {
                for (u16 column = 0; column < nCols_; ++column) {
                    const bool orphanLead = row[column].dwidth
                        && (column + 1 == nCols_ || !row[column + 1].dwidth_cont);
                    const bool orphanContinuation = row[column].dwidth_cont
                        && (column == 0 || !row[column - 1].dwidth);
                    if (orphanLead || orphanContinuation) {
                        row[column] = TerminalCell{};
                    }
                }
            };
            for (size_t row = lineOutputStart; row < output.size(); ++row) {
                normalizeWideRow(output[row]);
            }

            for (Anchor* anchor : anchors) {
                if (!anchor->found || anchor->line != lineIndex) {
                    continue;
                }
                const Boundary boundary = boundaries[std::min(anchor->offset, boundaries.size() - 1)];
                anchor->mapped = Point(boundary.column, (int)(boundary.row));
            }
        }

        const size_t cursorScreenStart = cursorAnchor.mapped.y >= nRows_
            ? cursorAnchor.mapped.y - (nRows_ - 1)
            : 0;
        size_t preferredScreenStart = screenAnchor.mapped.y;
        if (nRows_ > nRows) {
            preferredScreenStart -= std::min<size_t>(preferredScreenStart, nRows_ - nRows);
        }
        const size_t screenStart = std::max(preferredScreenStart, cursorScreenStart);
        while (output.size() < screenStart + nRows_) {
            output.emplace_back(nCols_);
        }
        const size_t retainedStart = screenStart > saveLines ? screenStart - saveLines : 0;
        const size_t historyCount = screenStart - retainedStart;

        auto newCells = TerminalCell::make(nCols_, nRows_ + saveLines);
        for (size_t row = 0; row < nRows_; ++row) {
            memcpy(newCells.get() + row * nCols_, output[screenStart + row].data(), nCols_ * cellSize);
        }
        for (size_t row = 0; row < historyCount; ++row) {
            memcpy(newCells.get() + (nRows_ + row) * nCols_, output[retainedStart + row].data(), nCols_ * cellSize);
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

        if (!wasScrolled) {
            viewOffset = 0;
        } else if (viewAnchor.mapped.y >= (int)(retainedStart) && viewAnchor.mapped.y < (int)(screenStart)) {
            viewOffset = screenStart - viewAnchor.mapped.y;
        } else if (viewAnchor.mapped.y < (int)(retainedStart)) {
            viewOffset = historyCount;
        } else {
            viewOffset = 0;
        }
        if (keepSelection
            && selectionStart.mapped.y >= (int)(retainedStart)
            && selectionStart.mapped.y < (int)(screenStart + nRows_)
            && selectionEnd.mapped.y >= (int)(retainedStart)
            && selectionEnd.mapped.y < (int)(screenStart + nRows_)) {
            selection.tl = Point(selectionStart.mapped.x, selectionStart.mapped.y - screenStart);
            selection.br = Point(selectionEnd.mapped.x, selectionEnd.mapped.y - screenStart);
        } else {
            selection.clear();
        }

        state.pendingWrap = cursorAnchor.mapped.x == nCols_;
        state.cursor.x = state.pendingWrap ? nCols_ - 1 : std::min(cursorAnchor.mapped.x, (int)(nCols_ - 1));
        state.cursor.y = std::max(0, std::min(cursorAnchor.mapped.y - (int)(screenStart), (int)(nRows_ - 1)));
        marginTop_ = 0;
        marginBottom_ = nRows;
        erasedRowTemplateValid = false;
        damage.totalCells = nCols * (nRows + saveLines);
        expose();
        highMemUsageReport();
        return state;
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
    return state;
}

void Frame::fullCopyCells(RenderCell* const dst) const {
    RenderCell* p = dst;
    for (int pY = 0; pY < nRows; ++pY) {
        const TerminalCell* src = getViewRowPtr(pY);
        for (u16 pX = 0; pX < nCols; ++pX) {
            *p++ = materialize(src[pX]);
        }
    }
}

void Frame::deltaCopyCells(RenderCell* const dst) const {
    RenderCell* p = dst;
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
                const u32 selectedClass = wordClass(row[left].uc_pt ? row[left].uc_pt : ' ');
                while (left > 0) {
                    const int previous = cellLead(row, left - 1);
                    const u32 codepoint = row[previous].uc_pt ? row[previous].uc_pt : ' ';
                    if (wordClass(codepoint) != selectedClass) {
                        break;
                    }
                    left = previous;
                }

                int right = left;
                while (right < nCols) {
                    const int lead = cellLead(row, right);
                    const u32 codepoint = row[lead].uc_pt ? row[lead].uc_pt : ' ';
                    if (wordClass(codepoint) != selectedClass) {
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
        size_t contentEnd = 0;
        bool wrapBack = wrap;
        wrap = false;
        const auto* cp = getLogicalRowPtr(y);
        for (u16 x = x1; x < x2; ++x) {
            const auto& cell = cp[x];
            if (!cell.dwidth_cont) {
                const auto grapheme = extras->grapheme(cell);
                if (grapheme.empty()) {
                    line.push_back(cell.uc_pt ? cell.uc_pt : ' ');
                } else {
                    line.insert(line.end(), grapheme.begin(), grapheme.end());
                }
                if (cell.drawn || (cell.uc_pt != 0 && cell.uc_pt != ' ') || !grapheme.empty()) {
                    contentEnd = line.size();
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
        if (!wrap && !sel.rectangular && x2 == nCols) {
            line.resize(contentEnd);
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

RenderCell Frame::materialize(const TerminalCell& cell) const {
    assert(colors != nullptr);
    assert(extras != nullptr);
    RenderCell result;
    result.uc_pt = cell.uc_pt ? cell.uc_pt : ' ';
    result.dwidth = cell.dwidth;
    result.dwidth_cont = cell.dwidth_cont;
    result.bold = cell.bold;
    result.italic = cell.italic;
    result.underline = cell.underlined();
    result.inverse = cell.inverse;
    result.wrap = cell.wrap;
    result.dirty = cell.dirty;
    result.faint = cell.faint;
    result.blink = cell.blink;
    result.conceal = cell.conceal;
    result.strike = cell.strike;
    result.overline = cell.overline;
    result.underline_style = cell.underline_style;
    result.protected_char = cell.protected_char;
    result.drawn = cell.drawn;
    result.line_attr = cell.line_attr;
    result.fg = colors->resolveForeground(cell);
    result.bg = colors->resolveBackground(cell);
    const CellColor underlineColor = extras->underlineColor(cell);
    result.underline_color = colors->resolve(underlineColor);
    if (cell.underlined() && underlineColor == cell.foreground()) {
        result.underline_color = result.fg;
    }
    result.hyperlink = extras->hyperlinkDisplayId(cell);
    result.grapheme = extras->grapheme(cell).empty() ? 0 : cell.extraRef();
    result.semantic = cell.semantic;
    return result;
}

void Frame::damageDeltaCopy(RenderCell* dst, u32 start, u32 count) const {
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
        RenderCell rendered = materialize(src[j]);
        if (dst[i] != rendered) {
            dst[i] = rendered;
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

const TerminalCell* Frame::getRow(u16 pY) const {
    return cells.get() + getIdx(pY, 0);
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
            cells.get()[k].uc_pt = ch == ' ' ? 0 : ch;
            cells.get()[k].drawn = ch != ' ';
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
    erased.line_attr = ((const Frame&)*this).getCell(pY, 0).line_attr;
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

void Frame::eraseWideInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    if (!count) {
        return;
    }
    const u16 endX = startX + count;
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    TerminalCell erased = attrs;
    erased.line_attr = row[0].line_attr;
    const bool eraseLeft = startX > 0 &&
        (row[startX - 1].dwidth || row[startX].dwidth_cont);
    const bool eraseRight = endX < nCols &&
        (row[endX - 1].dwidth || row[endX].dwidth_cont);
    if (eraseLeft) {
        row[startX - 1] = erased;
    }
    if (eraseRight) {
        row[endX] = erased;
    }
    if (startX == 0 && count == nCols) {
        if (!erasedRowTemplateValid || erasedRowTemplate.size() != nCols || erasedRowCell != erased) {
            erasedRowTemplate.assign(nCols, erased);
            erasedRowCell = erased;
            erasedRowTemplateValid = true;
        }
        memcpy(row, erasedRowTemplate.data(), nCols * cellSize);
    } else {
        for (u16 x = startX; x < endX; ++x) {
            row[x] = erased;
        }
    }
    const u32 damageStart = rowIdx + (eraseLeft ? startX - 1 : startX);
    const u32 damageEnd = rowIdx + (eraseRight ? endX + 1 : endX);
    damage.add(damageStart, damageEnd);
    if (!selection.empty()) {
        invalidateSelection(Rect(eraseLeft ? startX - 1 : startX, pY,
                                 eraseRight ? endX + 1 : endX, pY));
    }
}

TerminalCell* Frame::overwriteSpan(u16 pY, u16 startX, u16 count, const TerminalCell& eraseAttrs) {
    const u16 endX = startX + count;
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    TerminalCell erased = eraseAttrs;
    erased.line_attr = row[0].line_attr;
    const bool eraseLeft = startX > 0 &&
        (row[startX - 1].dwidth || row[startX].dwidth_cont);
    const bool eraseRight = endX < nCols &&
        (row[endX - 1].dwidth || row[endX].dwidth_cont);
    if (eraseLeft) {
        row[startX - 1] = erased;
    }
    if (eraseRight) {
        row[endX] = erased;
    }
    const u32 damageStart = rowIdx + (eraseLeft ? startX - 1 : startX);
    const u32 damageEnd = rowIdx + (eraseRight ? endX + 1 : endX);
    damage.add(damageStart, damageEnd);
    if (!selection.empty()) {
        invalidateSelection(Rect(eraseLeft ? startX - 1 : startX, pY,
                                 eraseRight ? endX + 1 : endX, pY));
    }
    return row + startX;
}

void Frame::clearWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs) {
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    const bool eraseLeft = boundary > 0 && row[boundary - 1].dwidth;
    const bool eraseRight = boundary < nCols && row[boundary].dwidth_cont;
    if (!eraseLeft && !eraseRight) {
        return;
    }
    TerminalCell erased = attrs;
    erased.line_attr = row[0].line_attr;
    if (eraseLeft) {
        row[boundary - 1] = erased;
        damage.add(rowIdx + boundary - 1, rowIdx + boundary);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary - 1, pY));
        }
    }
    if (eraseRight) {
        row[boundary] = erased;
        damage.add(rowIdx + boundary, rowIdx + boundary + 1);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary, pY));
        }
    }
}

void Frame::repairWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs) {
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    const bool leftLead = boundary > 0 && row[boundary - 1].dwidth;
    const bool rightContinuation = boundary < nCols && row[boundary].dwidth_cont;
    if (leftLead == rightContinuation) {
        return;
    }
    TerminalCell erased = attrs;
    erased.line_attr = row[0].line_attr;
    if (leftLead) {
        row[boundary - 1] = erased;
        damage.add(rowIdx + boundary - 1, rowIdx + boundary);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary - 1, pY));
        }
    } else {
        row[boundary] = erased;
        damage.add(rowIdx + boundary, rowIdx + boundary + 1);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary, pY));
        }
    }
}

void Frame::selectiveEraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs, u8 protectionMask) {
    TerminalCell erased = attrs;
    erased.uc_pt = 0;
    erased.protected_char = 0;
    extras->clearExtra(erased, extras->underlineColor(attrs));
    for (u16 x = startX; x < startX + count; ++x) {
        const u32 index = getIdx(pY, x);
        auto& cell = operator[](index);
        if (!(cell.protected_char & protectionMask)) {
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

void Frame::rotateRowsUp(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    if (!count) {
        return;
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(0, top, 0, bottom));
    }
    std::rotate(screen.begin() + top, screen.begin() + top + count,
                screen.begin() + bottom);
    expose();
}

void Frame::rotateRowsDown(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    if (!count) {
        return;
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(0, top, 0, bottom));
    }
    std::rotate(screen.begin() + top, screen.begin() + bottom - count,
                screen.begin() + bottom);
    expose();
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
