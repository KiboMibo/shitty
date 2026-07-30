/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_reference.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "hex.h"
#include "options.h"
#include "vterm.h"
#include "vterm_test.h"

#include <plt/platform_headless.h>
#include <plt/window.h>

#include <std/dbg/assert.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace stl;

namespace {
    struct ReferenceCell {
        TerminalCell source{};
        Color foreground;
        Color background;
        Color underlineColor;
        u32 hyperlink = 0;
        u32 grapheme = 0;
        u8 lineAttribute = 0;
    };

    template <typename Cell>
    unsigned cellUnderline(const Cell& cell) {
        return cell.underline;
    }

    template <>
    unsigned cellUnderline(const TerminalCell& cell) {
        return cell.underlined();
    }

    template <typename Cell>
    unsigned cellFlags(const Cell& cell, u8 lineAttribute) {
        return (cell.dwidth << 0) | (cell.dwidth_cont << 1) | (cell.bold << 2) | (cell.italic << 3) | (cellUnderline(cell) << 4) | (cell.inverse << 5) | (cell.wrap << 6) | (cell.faint << 7) | (cell.blink << 8) | (cell.conceal << 9) | (cell.strike << 10) | (cell.overline << 11) | (cell.underline_style << 12) | ((cell.protected_char != 0) << 15) | (lineAttribute << 16) | (cell.drawn << 18);
    }

    unsigned cellFlags(const ReferenceCell& cell) {
        return cellFlags(cell.source, cell.lineAttribute);
    }

    struct ModelDigest {
        u64 first = 14695981039346656037ull;
        u64 second = 1099511628211ull;

        void add(u64 value) {
            for (unsigned shift = 0; shift < 64; shift += 8) {
                const u8 byte = (u8)(value >> shift);
                first = (first ^ byte) * 1099511628211ull;
                second = (second ^ (byte + 0x9d)) * 14029467366897019727ull;
            }
        }
    };

    std::string toString(const StringBuilder& builder) {
        return std::string((const char*)(builder.data()), builder.used());
    }

    struct ReferenceRendererImpl final: ReferenceRenderer {
        ReferenceRendererImpl(Composer& composer, const plt::RenderContext& context);

        bool update(const TerminalUpdate& update) override;
        bool repaint() override;
        void attach(TestApi& testApi) override;
        ReferenceImage image() const override;
        TerminalUpdate renderUpdate() const override;
        std::string snapshot() const override;
        std::string modelSnapshot() const override;
        std::string modelDigest() const override;
        std::string renderState() const override;
        std::string selectionState() const override;
        std::string scrollbackState() const override;
        std::string screenText() const override;
        std::string lastUpdate() const override;
        std::string lastUpdateRows() const override;
        void resetUpdateStats() override;
        u16 columns() const override;
        u16 rows() const override;
        u32 historyRows() const override;
        u32 hoveredHyperlink() const override;
        u32 hoveredLinkBegin() const override;
        u32 hoveredLinkEnd() const override;

        static int minimum(int left, int right);
        static int maximum(int left, int right);
        static bool selected(const TerminalUpdate& update, const TerminalCell& cell, int column, int row);
        static u8 mix(u8 foreground, u8 background, u8 coverage);
        static Color blend(Color foreground, Color background, u8 coverage);
        static bool sameColor(Color left, Color right);
        bool targetReady() const;
        void clearTarget(Color background);
        void putPixel(int x, int y, Color color);
        void addGlyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth, int cellWidth, int cellHeight);
        void addFallback(int cellWidth, int cellHeight);
        ReferenceCell materialize(const TerminalCell& cell, u8 lineAttribute, const TerminalColors& colors) const;
        void renderCell(const TerminalUpdate& update, const ReferenceCell& cell, const GraphemeView& grapheme, u16 column, u16 row);
        bool render(const TerminalUpdate& update, const std::vector<ReferenceCell>& cells);
        void captureModel();
        void captureState(const TerminalUpdate& update);

        Composer& composer_;
        plt::HeadlessRenderTarget* target_;
        Buffer coverage_;
        Buffer color_;
        std::vector<ReferenceCell> cells_;
        std::vector<TerminalCell> modelCells_;
        std::vector<u8> modelLineAttributes_;
        std::vector<std::vector<u32>> cellGraphemes_;
        std::vector<CellColor> modelUnderlineColors_;
        mutable Vector<TerminalCellSpan> renderSpans_;
        mutable std::vector<TerminalCell> renderCells_;
        std::vector<u16> lastUpdateRows_;
        TestApi* testApi_ = nullptr;
        const TerminalColors* colors_ = nullptr;
        TerminalCursor cursor_;
        Rect selection_;
        Rect snappedSelection_;
        Color selectionForeground_;
        Color selectionBackground_;
        u32 viewOffset_ = 0;
        u32 historyRows_ = 0;
        u32 hoveredHyperlink_ = 0;
        u32 hoveredLinkBegin_ = 0;
        u32 hoveredLinkEnd_ = 0;
        u64 refreshCount_ = 0;
        size_t graphemeCells_ = 0;
        size_t graphemeCodepoints_ = 0;
        size_t lastUpdateCells_ = 0;
        size_t lastUpdateSpans_ = 0;
        u16 columns_ = 0;
        u16 rows_ = 0;
        u8 selectionColorMask_ = 0;
        bool hasColor_ = false;
        bool screenReverse_ = false;
        bool blinkVisible_ = true;
        bool cursorBlink_ = false;
        bool havePresentation_ = false;
    };
}

ReferenceRendererImpl::ReferenceRendererImpl(Composer& composer, const plt::RenderContext& context)
    : composer_(composer)
    , target_(static_cast<plt::HeadlessRenderTarget*>(context.window)) {
    STD_ASSERT(context.backend == plt::RenderBackend::Headless);
}

int ReferenceRendererImpl::minimum(int left, int right) {
    return left < right ? left : right;
}

int ReferenceRendererImpl::maximum(int left, int right) {
    return left > right ? left : right;
}

bool ReferenceRendererImpl::sameColor(Color left, Color right) {
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

bool ReferenceRendererImpl::selected(const TerminalUpdate& update, const TerminalCell& cell, int column, int row) {
    const Rect& selection = update.snappedSelection;
    if (selection.empty()) {
        return false;
    }
    const auto contains = [&](int x) {
        if (selection.rectangular) {
            return row >= selection.tl.y && row <= selection.br.y && x >= selection.tl.x && x < selection.br.x;
        }
        return (row > selection.tl.y && row < selection.br.y) || (row == selection.tl.y && x >= selection.tl.x && (row < selection.br.y || x < selection.br.x)) || (row == selection.br.y && x < selection.br.x && (row > selection.tl.y || x > selection.tl.x));
    };
    return contains(column) || (cell.dwidth && contains(column + 1));
}

u8 ReferenceRendererImpl::mix(u8 foreground, u8 background, u8 coverage) {
    return (u8)(((unsigned)(foreground)*coverage + (unsigned)(background) * (255 - coverage) + 127) / 255);
}

Color ReferenceRendererImpl::blend(Color foreground, Color background, u8 coverage) {
    return {
        mix(foreground.red, background.red, coverage),
        mix(foreground.green, background.green, coverage),
        mix(foreground.blue, background.blue, coverage),
    };
}

bool ReferenceRendererImpl::targetReady() const {
    if (target_ == nullptr || target_->pixels == nullptr || target_->format != plt::HeadlessPixelFormat::RGB8) {
        return false;
    }
    if (target_->width != composer_.pixelWidth || target_->height != composer_.pixelHeight || target_->stride < target_->width * 3) {
        return false;
    }
    return target_->length >= (size_t)(target_->stride) * target_->height;
}

void ReferenceRendererImpl::clearTarget(Color background) {
    for (u32 y = 0; y < target_->height; ++y) {
        u8* row = target_->pixels + (size_t)(y)*target_->stride;
        for (u32 x = 0; x < target_->width; ++x) {
            row[3 * x] = background.red;
            row[3 * x + 1] = background.green;
            row[3 * x + 2] = background.blue;
        }
    }
}

void ReferenceRendererImpl::putPixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= (int)(target_->width) || y >= (int)(target_->height)) {
        return;
    }
    u8* const pixel = target_->pixels + (size_t)(y)*target_->stride + 3 * x;
    pixel[0] = color.red;
    pixel[1] = color.green;
    pixel[2] = color.blue;
}

void ReferenceRendererImpl::addFallback(int cellWidth, int cellHeight) {
    auto* coverage = (u8*)(coverage_.mutData());
    for (int y = 0; y < cellHeight; ++y) {
        for (int x = 0; x < cellWidth; ++x) {
            if (x == 1 || x + 2 == cellWidth || y == 1 || y + 2 == cellHeight) {
                coverage[(size_t)(y)*cellWidth + x] = maximum(coverage[(size_t)(y)*cellWidth + x], 179);
            }
        }
    }
}

void ReferenceRendererImpl::addGlyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth, int cellWidth, int cellHeight) {
    if (composer_.fonts == nullptr) {
        return;
    }
    if (doubleWidth && !composer_.fonts->hasDoubleWidth()) {
        addFallback(cellWidth, cellHeight);
        return;
    }

    const FontGlyph glyph = composer_.fonts->glyph(codepoints, count, style, doubleWidth);
    const int glyphWidth = doubleWidth ? 2 * composer_.glyphWidth : composer_.glyphWidth;
    const size_t bytesPerPixel = glyph.color ? 4 : 1;
    const size_t expected = (size_t)(glyphWidth)*composer_.glyphHeight * bytesPerPixel;
    if (glyph.len != expected) {
        return;
    }

    const auto* source = (const u8*)(glyph.data);
    const int originX = (cellWidth - glyphWidth) / 2;
    for (int y = 0; y < cellHeight; ++y) {
        const int sourceY = y * composer_.glyphHeight / maximum(1, cellHeight);
        for (int x = 0; x < cellWidth; ++x) {
            const int sourceX = x - originX;
            if (sourceX < 0 || sourceX >= glyphWidth) {
                continue;
            }
            const size_t destinationIndex = (size_t)(y)*cellWidth + x;
            const size_t sourceIndex = (size_t)(sourceY)*glyphWidth + sourceX;
            if (!glyph.color) {
                auto* destination = (u8*)(coverage_.mutData());
                destination[destinationIndex] = maximum(destination[destinationIndex], source[sourceIndex]);
                continue;
            }
            hasColor_ = true;
            auto* destination = (u8*)(color_.mutData()) + 4 * destinationIndex;
            const u8* sourcePixel = source + 4 * sourceIndex;
            const unsigned inverse = 255 - sourcePixel[3];
            destination[0] = (u8)(sourcePixel[0] + (unsigned)(destination[0]) * inverse / 255);
            destination[1] = (u8)(sourcePixel[1] + (unsigned)(destination[1]) * inverse / 255);
            destination[2] = (u8)(sourcePixel[2] + (unsigned)(destination[2]) * inverse / 255);
            destination[3] = (u8)(sourcePixel[3] + (unsigned)(destination[3]) * inverse / 255);
        }
    }
}

ReferenceCell ReferenceRendererImpl::materialize(const TerminalCell& cell, u8 lineAttribute, const TerminalColors& colors) const {
    ReferenceCell result;
    result.source = cell;
    result.foreground = colors.resolveForeground(cell);
    result.background = colors.resolveBackground(cell);
    result.underlineColor = result.foreground;
    result.lineAttribute = lineAttribute;
    if (cell.hasExtra()) {
        const CellExtraView extra = composer_.cellExtras->view(cell);
        result.hyperlink = extra.hyperlinkDisplayId;
        result.grapheme = extra.grapheme.empty() ? 0 : cell.extraRef();
        if (extra.underlineColor != cell.foreground()) {
            result.underlineColor = colors.resolve(extra.underlineColor);
        }
    } else if (cell.inlineUnderlineColor() != cell.foreground()) {
        result.underlineColor = colors.resolve(cell.inlineUnderlineColor());
    }
    return result;
}

void ReferenceRendererImpl::renderCell(const TerminalUpdate& update, const ReferenceCell& cell, const GraphemeView& grapheme, u16 column, u16 row) {
    const TerminalCell& source = cell.source;
    if (source.dwidth_cont && cell.lineAttribute == 0) {
        return;
    }

    const bool doubleLine = cell.lineAttribute != 0;
    const bool doubleWidth = doubleLine || source.dwidth;
    const int cellWidth = composer_.glyphWidth * (doubleWidth && !doubleLine ? 2 : 1);
    const int cellHeight = composer_.glyphHeight;
    coverage_.zero((size_t)(cellWidth)*cellHeight);
    color_.zero((size_t)(cellWidth)*cellHeight * 4);
    hasColor_ = false;
    const FontStyle style = doubleWidth ? FontStyle::Regular : (FontStyle)((source.bold ? 1 : 0) | (source.italic ? 2 : 0));
    if (grapheme.empty()) {
        const u32 codepoint = source.uc_pt ? source.uc_pt : ' ';
        addGlyph(&codepoint, 1, style, doubleWidth, cellWidth, cellHeight);
    } else {
        addGlyph(grapheme.data(), grapheme.size(), style, doubleWidth, cellWidth, cellHeight);
    }

    Color foreground = cell.foreground;
    Color background = cell.background;
    if ((source.inverse != 0) != update.screenReverse) {
        std::swap(foreground, background);
    }
    if (selected(update, source, column, row)) {
        if (update.selectionColorMask == 0) {
            std::swap(foreground, background);
        } else {
            if (update.selectionColorMask & 1) {
                foreground = update.selectionForeground;
            }
            if (update.selectionColorMask & 2) {
                background = update.selectionBackground;
            }
        }
    }
    if (source.faint) {
        foreground = blend(foreground, background, 128);
    }
    if (source.conceal || (source.blink && !update.blinkVisible)) {
        foreground = background;
    }

    Color cursor = update.cursor.color;
    if (sameColor(cursor, background)) {
        cursor = {(u8)(255 - cursor.red), (u8)(255 - cursor.green), (u8)(255 - cursor.blue)};
    }
    const bool cursorHere = column == update.cursor.posX && row == update.cursor.posY && (!update.cursorBlink || update.blinkVisible);
    if (cursorHere && update.cursor.style == TerminalCursor::Style::filled_block) {
        foreground = background;
        background = cursor;
    }

    const int outputX = opts.border + column * composer_.glyphWidth;
    const int outputY = opts.border + row * composer_.glyphHeight;
    const auto* coverage = (const u8*)(coverage_.data());
    const auto* color = (const u8*)(color_.data());
    const bool hidden = source.conceal || (source.blink && !update.blinkVisible);
    for (int y = 0; y < cellHeight; ++y) {
        for (int x = 0; x < cellWidth; ++x) {
            const size_t index = (size_t)(y)*cellWidth + x;
            if (hasColor_ && !hidden && color[4 * index + 3] != 0) {
                const unsigned strength = source.faint ? 128 : 255;
                const unsigned alpha = (unsigned)(color[4 * index + 3]) * strength / 255;
                putPixel(
                    outputX + x,
                    outputY + y,
                    {
                        (u8)((unsigned)(color[4 * index]) * strength / 255 + (unsigned)(background.red) * (255 - alpha) / 255),
                        (u8)((unsigned)(color[4 * index + 1]) * strength / 255 + (unsigned)(background.green) * (255 - alpha) / 255),
                        (u8)((unsigned)(color[4 * index + 2]) * strength / 255 + (unsigned)(background.blue) * (255 - alpha) / 255),
                    }
                );
            } else {
                putPixel(outputX + x, outputY + y, blend(foreground, background, coverage[index]));
            }
        }
    }

    const u32 cellIndex = (u32)(row)*composer_.columns + column;
    const bool explicitLink = cell.hyperlink != 0 && cell.hyperlink == update.hoveredHyperlink;
    const bool plainLink = cellIndex >= update.hoveredLinkBegin && cellIndex < update.hoveredLinkEnd;
    const bool hyperlinkUnderline = !source.underlined() && (explicitLink || plainLink);
    if (source.underlined() || hyperlinkUnderline) {
        const u8 underlineStyle = hyperlinkUnderline ? 1 : source.underline_style;
        const Color underlineColor = hyperlinkUnderline ? foreground : cell.underlineColor;
        for (int x = 0; x < cellWidth; ++x) {
            const bool draw = underlineStyle != 4 || (x & 1) == 0;
            const bool patterned = underlineStyle != 5 || x % 6 < 4;
            const int waveY = underlineStyle == 3 ? x & 1 : 0;
            if (draw && patterned) {
                putPixel(outputX + x, outputY + cellHeight - 1 - waveY, underlineColor);
            }
            if (underlineStyle == 2 && cellHeight > 2) {
                putPixel(outputX + x, outputY + cellHeight - 3, underlineColor);
            }
        }
    }
    if (source.strike) {
        for (int x = 0; x < cellWidth; ++x) {
            putPixel(outputX + x, outputY + cellHeight / 2, foreground);
        }
    }
    if (source.overline) {
        for (int x = 0; x < cellWidth; ++x) {
            putPixel(outputX + x, outputY, foreground);
        }
    }
    if (opts.showWraps && source.wrap) {
        for (int y = 0; y < cellHeight; y += 2) {
            putPixel(outputX + cellWidth - 1, outputY + y, foreground);
        }
    }
    if (cursorHere && update.cursor.style == TerminalCursor::Style::hollow_block) {
        for (int x = 0; x < cellWidth; ++x) {
            putPixel(outputX + x, outputY, cursor);
            putPixel(outputX + x, outputY + cellHeight - 1, cursor);
        }
        for (int y = 1; y + 1 < cellHeight; ++y) {
            putPixel(outputX, outputY + y, cursor);
            putPixel(outputX + cellWidth - 1, outputY + y, cursor);
        }
    } else if (cursorHere && update.cursor.style == TerminalCursor::Style::underline) {
        const int thickness = maximum(1, cellHeight / 8);
        for (int y = cellHeight - thickness; y < cellHeight; ++y) {
            for (int x = 0; x < cellWidth; ++x) {
                putPixel(outputX + x, outputY + y, cursor);
            }
        }
    } else if (cursorHere && update.cursor.style == TerminalCursor::Style::bar) {
        const int thickness = maximum(1, composer_.glyphWidth / 6);
        for (int y = 0; y < cellHeight; ++y) {
            for (int x = 0; x < thickness; ++x) {
                putPixel(outputX + x, outputY + y, cursor);
            }
        }
    }
}

bool ReferenceRendererImpl::render(const TerminalUpdate& update, const std::vector<ReferenceCell>& cells) {
    if (!targetReady()) {
        return false;
    }
    // The padding follows the live default background (OSC 11), matching
    // xterm, kitty, foot, and the rest.
    clearTarget(update.colors != nullptr ? update.colors->defaultBackground : opts.bg);
    CellExtraStore& extras = *composer_.cellExtras;
    for (u16 row = 0; row < composer_.rows; ++row) {
        for (u16 column = 0; column < composer_.columns; ++column) {
            const ReferenceCell& cell = cells[(size_t)(row)*composer_.columns + column];
            const GraphemeView grapheme = cell.grapheme ? extras.grapheme(cell.grapheme) : GraphemeView{};
            renderCell(update, cell, grapheme, column, row);
        }
    }
    return true;
}

void ReferenceRendererImpl::captureModel() {
    if (testApi_ == nullptr) {
        return;
    }
    const size_t count = (size_t)(columns_)*rows_;
    modelCells_.resize(count);
    modelLineAttributes_.resize(count);
    cellGraphemes_.resize(count);
    modelUnderlineColors_.resize(count);
    for (u16 row = 0; row < rows_; ++row) {
        for (u16 column = 0; column < columns_; ++column) {
            const size_t index = (size_t)(row)*columns_ + column;
            const VtermTestCell inspected = testApi_->cell(row, column);
            modelCells_[index] = inspected.cell;
            modelLineAttributes_[index] = inspected.lineAttribute;
            if (inspected.graphemeSize == 0) {
                cellGraphemes_[index].clear();
            } else {
                cellGraphemes_[index].assign(inspected.grapheme, inspected.grapheme + inspected.graphemeSize);
            }
            modelUnderlineColors_[index] = inspected.underlineColor;
        }
    }
}

void ReferenceRendererImpl::captureState(const TerminalUpdate& update) {
    cursor_ = update.cursor;
    selection_ = update.selection;
    snappedSelection_ = update.snappedSelection;
    viewOffset_ = update.viewOffset;
    historyRows_ = update.historyRows;
    screenReverse_ = update.screenReverse;
    blinkVisible_ = update.blinkVisible;
    cursorBlink_ = update.cursorBlink;
    selectionForeground_ = update.selectionForeground;
    selectionBackground_ = update.selectionBackground;
    selectionColorMask_ = update.selectionColorMask;
    hoveredHyperlink_ = update.hoveredHyperlink;
    hoveredLinkBegin_ = update.hoveredLinkBegin;
    hoveredLinkEnd_ = update.hoveredLinkEnd;
    graphemeCells_ = 0;
    graphemeCodepoints_ = 0;
    for (const ReferenceCell& cell : cells_) {
        if (cell.grapheme == 0) {
            continue;
        }
        const GraphemeView grapheme = composer_.cellExtras->grapheme(cell.grapheme);
        if (!grapheme.empty()) {
            ++graphemeCells_;
            graphemeCodepoints_ += grapheme.size();
        }
    }
}

bool ReferenceRendererImpl::update(const TerminalUpdate& update) {
    if (!targetReady() || update.colors == nullptr) {
        return false;
    }
    const size_t count = (size_t)(composer_.columns) * composer_.rows;
    const bool shapeChanged = columns_ != composer_.columns || rows_ != composer_.rows;
    if (shapeChanged) {
        size_t covered = 0;
        for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
            const TerminalCellSpan& span = update.spans[spanIndex];
            if (span.cells == nullptr || span.count == 0 || span.index != covered) {
                return false;
            }
            covered += span.count;
        }
        if (covered != count) {
            return false;
        }
    }
    for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
        const TerminalCellSpan& span = update.spans[spanIndex];
        if (span.cells == nullptr || span.count == 0 || (size_t)(span.index) + span.count > count) {
            return false;
        }
    }

    std::vector<ReferenceCell> next = shapeChanged ? std::vector<ReferenceCell>(count) : cells_;
    for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
        const TerminalCellSpan& span = update.spans[spanIndex];
        for (u32 index = 0; index < span.count; ++index) {
            next[span.index + index] = materialize(span.cells[index], span.lineAttribute, *update.colors);
        }
    }
    if (!render(update, next)) {
        return false;
    }

    columns_ = composer_.columns;
    rows_ = composer_.rows;
    cells_ = std::move(next);
    colors_ = update.colors;
    lastUpdateCells_ = 0;
    lastUpdateSpans_ = update.spanCount;
    lastUpdateRows_.clear();
    for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
        const TerminalCellSpan& span = update.spans[spanIndex];
        lastUpdateCells_ += span.count;
        const u16 firstRow = span.index / columns_;
        const u16 lastRow = (span.index + span.count - 1) / columns_;
        for (u16 row = firstRow; row <= lastRow; ++row) {
            if (std::find(lastUpdateRows_.begin(), lastUpdateRows_.end(), row) == lastUpdateRows_.end()) {
                lastUpdateRows_.push_back(row);
            }
        }
    }
    captureModel();
    captureState(update);
    havePresentation_ = true;
    ++refreshCount_;
    return true;
}

bool ReferenceRendererImpl::repaint() {
    if (!havePresentation_ || !targetReady()) {
        return false;
    }
    TerminalUpdate update = renderUpdate();
    return render(update, cells_);
}

void ReferenceRendererImpl::attach(TestApi& testApi) {
    testApi_ = &testApi;
}

ReferenceImage ReferenceRendererImpl::image() const {
    if (target_ == nullptr) {
        return {};
    }
    return {
        .pixels = target_->pixels,
        .length = target_->length,
        .width = (u16)(target_->width),
        .height = (u16)(target_->height),
    };
}

TerminalUpdate ReferenceRendererImpl::renderUpdate() const {
    renderSpans_.clear();
    renderSpans_.grow(rows_);
    renderCells_.resize(cells_.size());
    for (size_t index = 0; index < cells_.size(); ++index) {
        renderCells_[index] = cells_[index].source;
    }
    for (u16 row = 0; row < rows_; ++row) {
        const size_t offset = (size_t)(row)*columns_;
        renderSpans_.pushBack({
            (u32)(offset),
            columns_,
            renderCells_.data() + offset,
            cells_[offset].lineAttribute,
        });
    }
    return {
        .spans = renderSpans_.data(),
        .spanCount = renderSpans_.length(),
        .colors = colors_,
        .viewOffset = viewOffset_,
        .historyRows = historyRows_,
        .cursor = cursor_,
        .selection = selection_,
        .snappedSelection = snappedSelection_,
        .selectionForeground = selectionForeground_,
        .selectionBackground = selectionBackground_,
        .selectionColorMask = selectionColorMask_,
        .hoveredHyperlink = hoveredHyperlink_,
        .hoveredLinkBegin = hoveredLinkBegin_,
        .hoveredLinkEnd = hoveredLinkEnd_,
        .screenReverse = screenReverse_,
        .blinkVisible = blinkVisible_,
        .cursorBlink = cursorBlink_,
    };
}

std::string ReferenceRendererImpl::snapshot() const {
    StringBuilder output;
    output << StringView(u8"OK ") << columns_ << StringView(u8" ") << rows_ << StringView(u8" ") << cursor_.posX << StringView(u8" ") << cursor_.posY << StringView(u8" ") << (unsigned)(cursor_.style) << StringView(u8" ") << viewOffset_ << StringView(u8" ") << refreshCount_ << StringView(u8" ") << selection_.tl.x << StringView(u8" ") << selection_.tl.y << StringView(u8" ") << selection_.br.x << StringView(u8" ") << selection_.br.y << StringView(u8" ") << (unsigned)(selection_.rectangular) << StringView(u8" ");
    for (const ReferenceCell& cell : cells_) {
        const unsigned flags = cellFlags(cell);
        const u32 codepoint = cell.source.uc_pt ? cell.source.uc_pt : ' ';
        output << Hex{codepoint, 8} << Hex{flags, 8} << Hex{cell.foreground.red, 2} << Hex{cell.foreground.green, 2} << Hex{cell.foreground.blue, 2} << Hex{cell.background.red, 2} << Hex{cell.background.green, 2} << Hex{cell.background.blue, 2} << Hex{cell.underlineColor.red, 2} << Hex{cell.underlineColor.green, 2} << Hex{cell.underlineColor.blue, 2} << Hex{cell.hyperlink, 8} << Hex{cell.source.semantic, 8};
    }
    output << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::modelSnapshot() const {
    StringBuilder output;
    output << StringView(u8"OK ") << columns_ << StringView(u8" ") << rows_ << StringView(u8" ") << cursor_.posX << StringView(u8" ") << cursor_.posY << StringView(u8" ") << (unsigned)(cursor_.style) << StringView(u8" ") << viewOffset_ << StringView(u8" ") << refreshCount_ << StringView(u8" ") << selection_.tl.x << StringView(u8" ") << selection_.tl.y << StringView(u8" ") << selection_.br.x << StringView(u8" ") << selection_.br.y << StringView(u8" ") << (unsigned)(selection_.rectangular) << StringView(u8" ");
    for (size_t index = 0; index < cells_.size(); ++index) {
        const ReferenceCell& cell = cells_[index];
        const TerminalCell& modelCell = modelCells_[index];
        const unsigned flags = cellFlags(modelCell, modelLineAttributes_[index]);
        const u32 codepoint = cell.source.uc_pt ? cell.source.uc_pt : ' ';
        output << Hex{codepoint, 8} << Hex{flags, 8} << Hex{cell.foreground.red, 2} << Hex{cell.foreground.green, 2} << Hex{cell.foreground.blue, 2} << Hex{cell.background.red, 2} << Hex{cell.background.green, 2} << Hex{cell.background.blue, 2} << Hex{cell.underlineColor.red, 2} << Hex{cell.underlineColor.green, 2} << Hex{cell.underlineColor.blue, 2} << Hex{cell.hyperlink, 8} << Hex{cell.source.semantic, 8} << Hex{(u32)(modelCell.foreground().legacyIndex()), 8} << Hex{(u32)(modelCell.background().legacyIndex()), 8} << Hex{(u32)(modelUnderlineColors_[index].legacyIndex()), 8} << Hex{cellGraphemes_[index].size(), 8};
        for (const u32 codepoint_ : cellGraphemes_[index]) {
            output << Hex{codepoint_, 8};
        }
    }
    output << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::modelDigest() const {
    ModelDigest digest;
    digest.add(columns_);
    digest.add(rows_);
    digest.add(cursor_.style == TerminalCursor::Style::hidden ? (u64)-1 : cursor_.posX);
    digest.add(cursor_.style == TerminalCursor::Style::hidden ? (u64)-1 : cursor_.posY);
    digest.add((u8)(cursor_.style));
    digest.add(viewOffset_);
    digest.add(selection_.tl.x);
    digest.add(selection_.tl.y);
    digest.add(selection_.br.x);
    digest.add(selection_.br.y);
    digest.add(selection_.rectangular);
    digest.add(cells_.size());
    for (size_t index = 0; index < cells_.size(); ++index) {
        const ReferenceCell& cell = cells_[index];
        const TerminalCell& modelCell = modelCells_[index];
        digest.add(cell.source.uc_pt ? cell.source.uc_pt : ' ');
        digest.add(cellFlags(modelCell, modelLineAttributes_[index]));
        digest.add(cell.foreground.red);
        digest.add(cell.foreground.green);
        digest.add(cell.foreground.blue);
        digest.add(cell.background.red);
        digest.add(cell.background.green);
        digest.add(cell.background.blue);
        digest.add(cell.underlineColor.red);
        digest.add(cell.underlineColor.green);
        digest.add(cell.underlineColor.blue);
        digest.add(cell.hyperlink);
        digest.add(cell.source.semantic);
        digest.add((u32)(modelCell.foreground().legacyIndex()));
        digest.add((u32)(modelCell.background().legacyIndex()));
        digest.add((u32)(modelUnderlineColors_[index].legacyIndex()));
        digest.add(cellGraphemes_[index].size());
        for (const u32 codepoint : cellGraphemes_[index]) {
            digest.add(codepoint);
        }
    }
    StringBuilder output;
    output << StringView(u8"OK ") << Hex{digest.first, 16} << StringView(u8" ") << Hex{digest.second, 16} << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::renderState() const {
    StringBuilder output;
    output << StringView(u8"OK ") << (unsigned)(screenReverse_) << StringView(u8" ") << (unsigned)(blinkVisible_) << StringView(u8" ") << (unsigned)(cursorBlink_) << StringView(u8" ") << (unsigned)(selectionColorMask_) << StringView(u8" ") << (unsigned)(selectionForeground_.red) << StringView(u8" ") << (unsigned)(selectionForeground_.green) << StringView(u8" ") << (unsigned)(selectionForeground_.blue) << StringView(u8" ") << (unsigned)(selectionBackground_.red) << StringView(u8" ") << (unsigned)(selectionBackground_.green) << StringView(u8" ") << (unsigned)(selectionBackground_.blue) << StringView(u8" ") << graphemeCells_ << StringView(u8" ") << graphemeCodepoints_ << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::selectionState() const {
    StringBuilder output;
    output << StringView(u8"OK ") << selection_.tl.x << StringView(u8" ") << selection_.tl.y << StringView(u8" ") << selection_.br.x << StringView(u8" ") << selection_.br.y << StringView(u8" ") << (unsigned)(selection_.rectangular) << StringView(u8" ") << snappedSelection_.tl.x << StringView(u8" ") << snappedSelection_.tl.y << StringView(u8" ") << snappedSelection_.br.x << StringView(u8" ") << snappedSelection_.br.y << StringView(u8" ") << (unsigned)(snappedSelection_.rectangular) << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::scrollbackState() const {
    StringBuilder output;
    output << StringView(u8"OK ") << historyRows_ << StringView(u8" ") << historyRows_ + rows_ << StringView(u8" ") << rows_ << StringView(u8" ") << historyRows_ - viewOffset_ << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::screenText() const {
    std::string output;
    output.reserve(cells_.size() + rows_);
    for (size_t index = 0; index < cells_.size(); ++index) {
        const u32 codepoint = cells_[index].source.uc_pt;
        output.push_back(codepoint >= 0x20 && codepoint <= 0x7e ? (char)(codepoint) : ' ');
        if ((index + 1) % columns_ == 0) {
            output.push_back('\n');
        }
    }
    return output;
}

std::string ReferenceRendererImpl::lastUpdate() const {
    StringBuilder output;
    output << StringView(u8"OK ") << lastUpdateCells_ << StringView(u8" ") << lastUpdateSpans_ << StringView(u8"\n");
    return toString(output);
}

std::string ReferenceRendererImpl::lastUpdateRows() const {
    StringBuilder output;
    output << StringView(u8"OK");
    for (u16 row : lastUpdateRows_) {
        output << StringView(u8" ") << row;
    }
    output << StringView(u8"\n");
    return toString(output);
}

void ReferenceRendererImpl::resetUpdateStats() {
    lastUpdateCells_ = 0;
    lastUpdateSpans_ = 0;
    lastUpdateRows_.clear();
}

u16 ReferenceRendererImpl::columns() const {
    return columns_;
}

u16 ReferenceRendererImpl::rows() const {
    return rows_;
}

u32 ReferenceRendererImpl::historyRows() const {
    return historyRows_;
}

u32 ReferenceRendererImpl::hoveredHyperlink() const {
    return hoveredHyperlink_;
}

u32 ReferenceRendererImpl::hoveredLinkBegin() const {
    return hoveredLinkBegin_;
}

u32 ReferenceRendererImpl::hoveredLinkEnd() const {
    return hoveredLinkEnd_;
}

ReferenceRenderer* ReferenceRenderer::create(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context) {
    return pool.make<ReferenceRendererImpl>(composer, context);
}
