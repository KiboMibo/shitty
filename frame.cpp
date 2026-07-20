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

namespace {

uint32_t wordClass(uint32_t codepoint) {
    constexpr uint32_t whitespaceClass = 0x110000;
    constexpr uint32_t identifierClass = 0x110001;
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

void Frame::setSelectionColor(
    bool foreground, Color color, bool enabled) {
    const uint8_t bit = foreground ? 1 : 2;
    if (foreground) selectionForeground = color;
    else selectionBackground = color;
    if (enabled) selectionColorMask |= bit;
    else selectionColorMask &= ~bit;
    expose();
}

uint32_t Frame::internGrapheme(const Grapheme& codepoints) {
    if (codepoints.size() < 2) {
        return 0;
    }
    const auto found = graphemes->ids.find(codepoints);
    if (found != graphemes->ids.end()) {
        return found->second;
    }
    const uint32_t id = graphemes->values.size();
    graphemes->values.push_back(codepoints);
    graphemes->ids.emplace(codepoints, id);
    return id;
}

const Frame::Grapheme& Frame::getGrapheme(uint32_t id) const {
    if (id >= graphemes->values.size()) {
        return graphemes->values.front();
    }
    return graphemes->values[id];
}

Frame::Frame(uint16_t winPx_, uint16_t winPy_,
             uint16_t nCols_, uint16_t nRows_,
             uint16_t& marginTop_, uint16_t& marginBottom_,
             uint16_t saveLines_)
    : winPx(winPx_)
    , winPy(winPy_)
    , nCols(nCols_)
    , nRows(nRows_)
    , saveLines(saveLines_)
    , viewOffset(0)
    , cells(CharVdev::make_cells(nCols, nRows + saveLines))
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

void Frame::collectHyperlinkIds(std::set<uint32_t>& ids) const {
    const auto collectRow = [&](RowId row) {
        const CharVdev::Cell* first = cells.get() + row * nCols;
        for (uint16_t column = 0; column < nCols; ++column) {
            if (first[column].hyperlink != 0) {
                ids.insert(first[column].hyperlink);
            }
        }
    };
    for (RowId row : screen) collectRow(row);
    for (RowId row : history) collectRow(row);
}

void Frame::recolorPalette(uint16_t index, Color color) {
    if (!cells) return;
    const size_t count = static_cast<size_t>(nCols) * (nRows + saveLines);
    for (size_t i = 0; i < count; ++i) {
        auto& cell = cells.get()[i];
        if (cell.fg_index == index) cell.fg = color;
        if (cell.bg_index == index) cell.bg = color;
        if (cell.underline_index == index) cell.underline_color = color;
    }
    expose();
}

void Frame::recolorDefault(bool foreground, Color color) {
    if (!cells) return;
    const size_t count = static_cast<size_t>(nCols) * (nRows + saveLines);
    for (size_t i = 0; i < count; ++i) {
        auto& cell = cells.get()[i];
        if (foreground) {
            if (cell.fg_index == -2) cell.fg = color;
            if (cell.underline_index == -2) cell.underline_color = color;
        } else if (cell.bg_index == -2) {
            cell.bg = color;
        }
    }
    expose();
}

void Frame::resize(uint16_t winPx_, uint16_t winPy_,
                   uint16_t nCols_, uint16_t nRows_,
                   uint16_t& marginTop_, uint16_t& marginBottom_) {
    if (winPx == winPx_ && winPy == winPy_) {
        return;
    }

    winPx = winPx_;
    winPy = winPy_;

    if (nCols == nCols_ && nRows == nRows_) {
        return;
    }

    const uint16_t historyCount = history.size();
    const int rowLen = std::min(nCols, nCols_);
    const int nCopyRows = std::min(nRows, nRows_);
    auto newCells = CharVdev::make_cells(nCols_, nRows_ + saveLines);
    CharVdev::Cell* p = newCells.get();
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
    const auto normalizeWideRow = [nCols_](CharVdev::Cell* row) {
        for (uint16_t column = 0; column < nCols_; ++column) {
            const bool orphanLead = row[column].dwidth &&
                (column + 1 == nCols_ || !row[column + 1].dwidth_cont);
            const bool orphanContinuation = row[column].dwidth_cont &&
                (column == 0 || !row[column - 1].dwidth);
            if (orphanLead || orphanContinuation) {
                row[column] = CharVdev::Cell{};
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
    for (RowId row = nRows + historyCount;
         row < nRows + saveLines; ++row) {
        freeRows.push_back(row);
    }
    marginTop_ = 0;
    marginBottom_ = nRows;
    viewOffset = 0;
    damage.totalCells = nCols * (nRows + saveLines);
    expose();
    highMemUsageReport();
}

void Frame::fullCopyCells(CharVdev::Cell* const dst) const {
    CharVdev::Cell* p = dst;
    for (int pY = 0; pY < nRows; ++pY) {
        memcpy(p, getViewRowPtr(pY), nCols * cellSize);
        p += nCols;
    }
}

void Frame::deltaCopyCells(CharVdev::Cell* const dst) const {
    CharVdev::Cell* p = dst;
    for (int pY = -viewOffset; pY < nRows - viewOffset; ++pY) {
        damageDeltaCopy(p, nCols * getLogicalRow(pY), nCols);
        p += nCols;
    }
}

Rect Frame::getSelectionForView() const {
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

    if (selection.rectangular) {
        ret.tl.y += viewOffset;
        ret.br.y += viewOffset;
        return ret;
    }

    switch (snapTo) {
        case SelectSnapTo::Char:
            break;
        case SelectSnapTo::Word: {
            const auto cellLead = [this](const CharVdev::Cell* row, int x) {
                x = std::max(0, std::min(x, static_cast<int>(nCols) - 1));
                return row[x].dwidth_cont && x > 0 ? x - 1 : x;
            };
            const auto expand = [this, &cellLead](int rowIndex, int x) {
                const auto* row = getLogicalRowPtr(rowIndex);
                int left = cellLead(row, x);
                const uint32_t selectedClass = wordClass(row[left].uc_pt);
                while (left > 0) {
                    const int previous = cellLead(row, left - 1);
                    if (wordClass(row[previous].uc_pt) != selectedClass) break;
                    left = previous;
                }

                int right = left;
                while (right < nCols) {
                    const int lead = cellLead(row, right);
                    if (wordClass(row[lead].uc_pt) != selectedClass) break;
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

    using unicodeString = std::vector<uint32_t>;
    std::vector<unicodeString> lines;
    bool wrap = false;

    auto addLine =
        [&](int y, uint16_t x1, uint16_t x2) {
        unicodeString line;
        bool wrapBack = wrap;
        wrap = false;
        const auto* cp = getLogicalRowPtr(y);
        for (uint16_t x = x1; x < x2; ++x) {
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
        while (!wrap && !sel.rectangular && x2 == nCols && line.size() &&
               line.back() == ' ') {
            line.pop_back();
        }

        if (wrapBack && lines.size()) {
            lines.back().insert(lines.back().end(),
                                line.begin(), line.end());
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
        for (uint32_t cp : codepoints) {
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
        logT << "Selected " << utf8_selection.size() << " bytes:\n'"
             << utf8_selection << "'" << std::endl;
    } else {
        logT << "Selected " << utf8_selection.size() << " bytes:\n'"
             << utf8_selection.substr(0, 40) << "' ...\n'"
             << utf8_selection.substr(utf8_selection.size() - 40) << "'"
             << std::endl;
    }
#endif
    return true;
}

inline void
Frame::damageDeltaCopy(
    CharVdev::Cell* dst, uint32_t start, uint32_t count) const {
    uint32_t end = start + count;

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

    CharVdev::Cell* const src = cells.get();

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
             << "decreasing saveLines (current value: " << saveLines
             << ") to reduce memory usage!"
             << std::endl;
    }
}
