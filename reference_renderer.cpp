/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "reference_renderer.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "options.h"
#include "vterm.h"

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <math.h>

using namespace stl;

namespace {
    struct Pixel {
        u8 red;
        u8 green;
        u8 blue;
    };

    struct ReferenceRendererImpl final: public ReferenceRenderer {
        ReferenceRendererImpl(Composer& composer, Fontpack& fonts);

        ReferenceImage render(const TerminalUpdate& update) override;

        Composer& composer_;
        Fontpack& fonts_;
        Buffer pixels_;
        Buffer coverage_;
        Buffer color_;
        bool hasColor_ = false;

        static int minimum(int left, int right);
        static int maximum(int left, int right);
        static bool selected(const TerminalUpdate& update, int column, int row);
        static u8 mix(u8 foreground, u8 background, u8 coverage);
        static Color blend(Color foreground, Color background, u8 coverage);
        void addGlyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth, int cellWidth, int cellHeight);
        void addFallback(int cellWidth, int cellHeight);
        void renderCell(const TerminalUpdate& update, const RenderCell& cell, const GraphemeView& grapheme, u16 column, u16 row);
        void putPixel(int x, int y, Color color);
    };
}

ReferenceRendererImpl::ReferenceRendererImpl(Composer& composer, Fontpack& fonts)
    : composer_(composer)
    , fonts_(fonts)
{
}

int ReferenceRendererImpl::minimum(int left, int right) {
    return left < right ? left : right;
}

int ReferenceRendererImpl::maximum(int left, int right) {
    return left > right ? left : right;
}

bool ReferenceRendererImpl::selected(const TerminalUpdate& update, int column, int row) {
    const Rect& selection = update.snappedSelection;
    if (selection.empty()) {
        return false;
    }
    if (selection.rectangular) {
        return row >= selection.tl.y && row <= selection.br.y && column >= selection.tl.x && column < selection.br.x;
    }
    return (row > selection.tl.y && row < selection.br.y) || (row == selection.tl.y && column >= selection.tl.x && (row < selection.br.y || column < selection.br.x)) || (row == selection.br.y && column < selection.br.x && (row > selection.tl.y || column > selection.tl.x));
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

void ReferenceRendererImpl::putPixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= composer_.pixelWidth || y >= composer_.pixelHeight) {
        return;
    }
    auto* pixels = (Pixel*)(pixels_.mutData());
    pixels[(size_t)(y)*composer_.pixelWidth + x] = {
        color.red,
        color.green,
        color.blue,
    };
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
    if (doubleWidth && !fonts_.hasDoubleWidth()) {
        addFallback(cellWidth, cellHeight);
        return;
    }

    const FontGlyph glyph = fonts_.glyph(codepoints, count, style, doubleWidth);
    const int glyphWidth = doubleWidth ? 2 * composer_.glyphWidth : composer_.glyphWidth;
    const size_t bytesPerPixel = glyph.color ? 4 : 1;
    const size_t expected = (size_t)(glyphWidth)*composer_.glyphHeight * bytesPerPixel;
    if (glyph.data == nullptr || glyph.len != expected) {
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

void ReferenceRendererImpl::renderCell(const TerminalUpdate& update, const RenderCell& cell, const GraphemeView& grapheme, u16 column, u16 row) {
    if (cell.dwidth_cont && cell.line_attr == 0) {
        return;
    }

    const bool doubleLine = cell.line_attr != 0;
    const bool doubleWidth = doubleLine || cell.dwidth;
    const int cellWidth = composer_.glyphWidth * (doubleWidth && !doubleLine ? 2 : 1);
    const int cellHeight = composer_.glyphHeight;
    coverage_.zero((size_t)(cellWidth)*cellHeight);
    color_.zero((size_t)(cellWidth)*cellHeight * 4);
    hasColor_ = false;
    const FontStyle style = doubleWidth ? FontStyle::Regular : (FontStyle)((cell.bold ? 1 : 0) | (cell.italic ? 2 : 0));
    if (grapheme.empty()) {
        addGlyph(&cell.uc_pt, 1, style, doubleWidth, cellWidth, cellHeight);
    } else {
        addGlyph(grapheme.data(), grapheme.size(), style, doubleWidth, cellWidth, cellHeight);
    }

    Color foreground = cell.fg;
    Color background = cell.bg;
    if ((cell.inverse != 0) != update.screenReverse) {
        const Color temporary = foreground;
        foreground = background;
        background = temporary;
    }
    if (selected(update, column, row)) {
        if (update.selectionColorMask == 0) {
            const Color temporary = foreground;
            foreground = background;
            background = temporary;
        } else {
            if (update.selectionColorMask & 1) {
                foreground = update.selectionForeground;
            }
            if (update.selectionColorMask & 2) {
                background = update.selectionBackground;
            }
        }
    }
    if (cell.faint) {
        foreground = blend(foreground, background, 128);
    }
    if (cell.conceal || (cell.blink && !update.blinkVisible)) {
        foreground = background;
    }

    const int outputX = opts.border + column * composer_.glyphWidth;
    const int outputY = opts.border + row * composer_.glyphHeight;
    const auto* coverage = (const u8*)(coverage_.data());
    const auto* color = (const u8*)(color_.data());
    const bool hidden = cell.conceal || (cell.blink && !update.blinkVisible);
    for (int y = 0; y < cellHeight; ++y) {
        for (int x = 0; x < cellWidth; ++x) {
            const size_t index = (size_t)(y)*cellWidth + x;
            if (hasColor_ && !hidden && color[4 * index + 3] != 0) {
                const unsigned strength = cell.faint ? 128 : 255;
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
    const bool hyperlinkUnderline = !cell.underline && (explicitLink || plainLink);
    if (cell.underline || hyperlinkUnderline) {
        const u8 underlineStyle = hyperlinkUnderline ? 1 : cell.underline_style;
        const Color underlineColor = hyperlinkUnderline ? foreground : cell.underline_color;
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
    if (cell.strike) {
        for (int x = 0; x < cellWidth; ++x) {
            putPixel(outputX + x, outputY + cellHeight / 2, foreground);
        }
    }
    if (cell.overline) {
        for (int x = 0; x < cellWidth; ++x) {
            putPixel(outputX + x, outputY, foreground);
        }
    }
}

ReferenceImage ReferenceRendererImpl::render(const TerminalUpdate& update) {
    const size_t cellCount = (size_t)(composer_.columns) * composer_.rows;
    if (update.cells == nullptr || update.cellCount != cellCount || update.cellExtras == nullptr) {
        return {};
    }

    pixels_.zero((size_t)(composer_.pixelWidth) * composer_.pixelHeight * sizeof(Pixel));
    for (u16 row = 0; row < composer_.rows; ++row) {
        for (u16 column = 0; column < composer_.columns; ++column) {
            const RenderCell& cell = update.cells[(size_t)(row)*composer_.columns + column];
            const GraphemeView grapheme = cell.grapheme ? update.cellExtras->grapheme(cell.grapheme) : GraphemeView{};
            renderCell(update, cell, grapheme, column, row);
        }
    }
    return {
        .pixels = (const u8*)(pixels_.data()),
        .length = pixels_.used(),
        .width = composer_.pixelWidth,
        .height = composer_.pixelHeight,
    };
}

ReferenceRenderer* ReferenceRenderer::create(Composer& composer, Fontpack& fonts) {
    return composer.pool->make<ReferenceRendererImpl>(composer, fonts);
}
