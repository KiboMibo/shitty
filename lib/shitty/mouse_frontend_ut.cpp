/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "mouse_frontend.h"

#include "grid_geometry.h"
#include "options.h"

#include <std/alg/minmax.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    // A1: a content box whose four reserves share no value, so a helper
    // that took `top` where it meant `left` answers differently instead
    // of accidentally right. 194 x 146 pixels hold exactly 20 x 6 cells
    // of 8 x 16 once the reserves are out.
    constexpr MouseGeometry asymmetric{
        .framebufferWidth = 194,
        .framebufferHeight = 146,
        .insets = {.top = 17, .right = 23, .bottom = 33, .left = 11},
        .glyphWidth = 8,
        .glyphHeight = 16,
    };

    // The same four reserves around a surface three pixels wider and
    // three taller, so the content box ends in the middle of a cell on
    // both axes - 163 x 99 pixels over an 8 x 16 glyph.
    constexpr MouseGeometry partialCell{
        .framebufferWidth = 197,
        .framebufferHeight = 149,
        .insets = {.top = 17, .right = 23, .bottom = 33, .left = 11},
        .glyphWidth = 8,
        .glyphHeight = 16,
    };

    // A8: the same window, with the pane starting three columns in and
    // two rows down from the window's own content origin. The two offsets
    // are neither equal nor multiples of one another, so a mapping that
    // took the x origin for the y one answers differently rather than
    // accidentally right - and one that dropped the origin altogether
    // lands three cells and two rows off.
    constexpr MouseGeometry panePlaced{
        .framebufferWidth = 194,
        .framebufferHeight = 146,
        .insets = {.top = 17, .right = 23, .bottom = 33, .left = 11},
        .paneOriginX = 24,
        .paneOriginY = 32,
        .glyphWidth = 8,
        .glyphHeight = 16,
    };
}

// A1 acceptance: with a uniform border - which is every build until T5 and
// T6 reserve a side - the four pointer mappings must answer exactly what
// the scalar-border code answered. The originals are transcribed here as
// the reference; the sweep below runs both over every pixel of a surface
// and twenty more in each direction.
namespace scalarBorder {
    constexpr int border = 7;
    constexpr int fbWidth = 2 * border + 20 * 8;
    constexpr int fbHeight = 2 * border + 6 * 16;
    constexpr int glyphWidth = 8;
    constexpr int glyphHeight = 16;
    constexpr int columns = 20;
    constexpr int rows = 6;

    constexpr MouseGeometry geometry{
        .framebufferWidth = fbWidth,
        .framebufferHeight = fbHeight,
        .insets = {border, border, border, border},
        .glyphWidth = glyphWidth,
        .glyphHeight = glyphHeight,
    };

    // resolveHyperlink, before T4.
    static bool oldCell(int pixelX, int pixelY, u16& column, u16& row) {
        if (pixelX < border || pixelY < border || pixelX >= fbWidth - border || pixelY >= fbHeight - border) {
            return false;
        }
        column = (u16)((pixelX - border) / glyphWidth);
        row = (u16)((pixelY - border) / glyphHeight);
        return true;
    }

    // selectionPoint, before T4.
    static Point oldSelection(int pX, int pY) {
        const int contentWidth = max(0, fbWidth - 2 * border);
        const int contentHeight = max(1, fbHeight - 2 * border);
        pX = min(max(0, pX - border), contentWidth);
        pY = min(max(0, pY - border), contentHeight - 1);
        return Point(min(pX / glyphWidth, columns), min(pY / glyphHeight, rows - 1));
    }

    // currentSelectionAutoscrollDirection, before T4.
    static int oldAutoscroll(int pointerY) {
        const int top = border;
        const int bottom = max(top, fbHeight - border - 1);
        if (pointerY <= top) {
            return -1;
        }
        if (pointerY >= bottom) {
            return 1;
        }
        return 0;
    }

    // mouseProtocolPoint, before T4.
    static MouseProtocolPoint oldProtocol(MouseTrackingEnc encoding, int pixelX, int pixelY) {
        const int contentWidth = max(1, fbWidth - 2 * border);
        const int contentHeight = max(1, fbHeight - 2 * border);
        if (encoding == MouseTrackingEnc::SGRPixels) {
            return {
                min(max(pixelX - border + 1, 1), contentWidth),
                min(max(pixelY - border + 1, 1), contentHeight),
            };
        }
        const int c = max(1, contentWidth / max(1, glyphWidth));
        const int r = max(1, contentHeight / max(1, glyphHeight));
        return {
            min(max((pixelX - border) / max(1, glyphWidth) + 1, 1), c),
            min(max((pixelY - border) / max(1, glyphHeight) + 1, 1), r),
        };
    }
}

STD_TEST_SUITE(MouseFrontend) {
    STD_TEST(ConvertsLogicalCoordinatesToFramebufferPixels) {
        STD_INSIST(mouseFramebufferCoordinate(10.25, 2.0) == 21);
        STD_INSIST(mouseFramebufferCoordinate(-10.25, 2.0) == -21);
        STD_INSIST(mouseFramebufferCoordinate(10.0, 0.5) == 10);
        STD_INSIST(mouseFramebufferCoordinate(__builtin_inf(), 2.0) == 0);
        STD_INSIST(mouseFramebufferCoordinate(10.0, __builtin_nan("")) == 0);
    }

    STD_TEST(ConvertsPixelsToCellCoordinates) {
        const MouseGeometry geometry{
            .framebufferWidth = 84,
            .framebufferHeight = 68,
            .insets = {2, 2, 2, 2},
            .glyphWidth = 8,
            .glyphHeight = 16,
        };

        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 2, 2, geometry).column == 1);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 2, 2, geometry).row == 1);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 81, 65, geometry).column == 10);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 81, 65, geometry).row == 4);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, -100, -100, geometry).column == 1);

        // The first pixel of a cell belongs to that cell, the last one to
        // the same cell — no drift at the boundary.
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 9, 2, geometry).column == 1);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 10, 2, geometry).column == 2);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 2, 17, geometry).row == 1);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 2, 18, geometry).row == 2);
    }

    STD_TEST(SgrPixelsUseContentPixelCoordinates) {
        const MouseGeometry geometry{
            .framebufferWidth = 104,
            .framebufferHeight = 54,
            .insets = {2, 2, 2, 2},
            .glyphWidth = 8,
            .glyphHeight = 16,
        };

        const MouseProtocolPoint first = mouseProtocolPoint(MouseTrackingEnc::SGRPixels, 2, 2, geometry);
        const MouseProtocolPoint last = mouseProtocolPoint(MouseTrackingEnc::SGRPixels, 101, 51, geometry);

        STD_INSIST(first.column == 1);
        STD_INSIST(first.row == 1);
        STD_INSIST(last.column == 100);
        STD_INSIST(last.row == 50);
    }

    // The composer is the only source of a pointer geometry, and the
    // uniform border it reports today has to arrive on all four sides.
    STD_TEST(ReadsItsGeometryFromTheComposer) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 5;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);
        composer.resize(194, 146);

        const MouseGeometry geometry = mouseGeometry(composer);

        STD_INSIST(geometry.framebufferWidth == 194);
        STD_INSIST(geometry.framebufferHeight == 146);
        STD_INSIST(geometry.glyphWidth == 8);
        STD_INSIST(geometry.glyphHeight == 16);
        STD_INSIST(geometry.insets.left == composer.borderPixels());
        STD_INSIST(geometry.insets.top == composer.borderPixels());
        STD_INSIST(geometry.insets.right == composer.borderPixels());
        STD_INSIST(geometry.insets.bottom == composer.borderPixels());
    }

    // Hyperlink hover: the cell under the pointer, or nothing when the
    // pointer is over a reserve rather than over text.
    STD_TEST(FindsTheCellUnderThePointer) {
        u16 column = 0;
        u16 row = 0;

        STD_INSIST(mouseCell(11, 17, asymmetric, column, row));
        STD_INSIST(column == 0 && row == 0);

        STD_INSIST(mouseCell(170, 112, asymmetric, column, row));
        STD_INSIST(column == 19 && row == 5);
    }

    // Each edge of the content box has to come from its own side. Every
    // probe here is answered one way by the right side and the other way
    // by the side a mixup would reach for.
    STD_TEST(TakesEachEdgeFromItsOwnSide) {
        u16 column = 0;
        u16 row = 0;

        // Left of `top`, right of `left`: inside, and only `left` says so.
        STD_INSIST(mouseCell(15, 50, asymmetric, column, row));
        STD_INSIST(column == 0);
        // Above `top` but below `left`: outside, and only `top` says so.
        STD_INSIST(!mouseCell(50, 15, asymmetric, column, row));
        // Past the `bottom` edge measured across, still inside the width.
        STD_INSIST(mouseCell(165, 50, asymmetric, column, row));
        STD_INSIST(column == 19);
        // Past the `bottom` edge, short of where `right` would put it.
        STD_INSIST(!mouseCell(50, 115, asymmetric, column, row));

        // The offsets, too: the column counts from `left`, the row from
        // `top`, and neither from the other.
        STD_INSIST(mouseCell(19, 29, asymmetric, column, row));
        STD_INSIST(column == 1);
        STD_INSIST(row == 0);
    }

    // A pointer over a reserve leaves the cell alone rather than naming
    // a nearby one - the caller tells "no cell" from "cell 0,0" that way.
    STD_TEST(LeavesTheCellAloneOutsideTheContent) {
        u16 column = 7;
        u16 row = 9;

        STD_INSIST(!mouseCell(0, 0, asymmetric, column, row));
        STD_INSIST(!mouseCell(193, 145, asymmetric, column, row));
        STD_INSIST(column == 7 && row == 9);
    }

    STD_TEST(ReportsMouseProtocolCellsFromTheLeftAndTopReserves) {
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 19, 29, asymmetric).column == 2);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 19, 29, asymmetric).row == 1);

        // Far outside, the report clamps to the grid the content box
        // holds - 20 x 6 here, which neither reserve pair alone gives.
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 10000, 10000, asymmetric).column == 20);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 10000, 10000, asymmetric).row == 6);

        // Pixel reporting counts from the same corner, in pixels.
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGRPixels, 170, 112, asymmetric).column == 160);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGRPixels, 170, 112, asymmetric).row == 96);
    }

    STD_TEST(ClampsSelectionEndpointsIntoTheContentBox) {
        STD_INSIST(mouseSelectionCell(11, 17, asymmetric, 20, 6) == Point(0, 0));
        STD_INSIST(mouseSelectionCell(27, 45, asymmetric, 20, 6) == Point(2, 1));

        // The open end of an extent is allowed one column past the last
        // cell, but never one row past the last row.
        STD_INSIST(mouseSelectionCell(10000, 10000, asymmetric, 20, 6) == Point(20, 5));
        STD_INSIST(mouseSelectionCell(-10000, -10000, asymmetric, 20, 6) == Point(0, 0));
    }

    STD_TEST(ScrollsWhenTheDragLeavesTheContentBox) {
        STD_INSIST(mouseAutoscrollDirection(13, asymmetric) == -1);
        STD_INSIST(mouseAutoscrollDirection(17, asymmetric) == -1);
        STD_INSIST(mouseAutoscrollDirection(18, asymmetric) == 0);
        STD_INSIST(mouseAutoscrollDirection(111, asymmetric) == 0);
        STD_INSIST(mouseAutoscrollDirection(112, asymmetric) == 1);
        STD_INSIST(mouseAutoscrollDirection(115, asymmetric) == 1);
    }

    // The acceptance criterion for T4 in one test: without new options the
    // pointer lands in exactly the cell it landed in before, on every pixel
    // of the surface and twenty past every edge, at a border of seven.
    STD_TEST(EveryPixelMapsWhereItUsedTo) {
        size_t compared = 0;
        for (int y = -20; y < scalarBorder::fbHeight + 20; ++y) {
            for (int x = -20; x < scalarBorder::fbWidth + 20; ++x) {
                u16 oldColumn = 0xffff;
                u16 oldRow = 0xffff;
                u16 newColumn = 0xffff;
                u16 newRow = 0xffff;
                const bool oldInside = scalarBorder::oldCell(x, y, oldColumn, oldRow);
                const bool newInside = mouseCell(x, y, scalarBorder::geometry, newColumn, newRow);
                STD_INSIST(oldInside == newInside);
                STD_INSIST(oldColumn == newColumn);
                STD_INSIST(oldRow == newRow);

                STD_INSIST(scalarBorder::oldSelection(x, y) == mouseSelectionCell(x, y, scalarBorder::geometry, scalarBorder::columns, scalarBorder::rows));

                const MouseProtocolPoint oldSgr = scalarBorder::oldProtocol(MouseTrackingEnc::SGR, x, y);
                const MouseProtocolPoint newSgr = mouseProtocolPoint(MouseTrackingEnc::SGR, x, y, scalarBorder::geometry);
                STD_INSIST(oldSgr.column == newSgr.column && oldSgr.row == newSgr.row);

                const MouseProtocolPoint oldPix = scalarBorder::oldProtocol(MouseTrackingEnc::SGRPixels, x, y);
                const MouseProtocolPoint newPix = mouseProtocolPoint(MouseTrackingEnc::SGRPixels, x, y, scalarBorder::geometry);
                STD_INSIST(oldPix.column == newPix.column && oldPix.row == newPix.row);

                ++compared;
            }
            STD_INSIST(scalarBorder::oldAutoscroll(y) == mouseAutoscrollDirection(y, scalarBorder::geometry));
        }
        STD_INSIST(compared == (size_t)(scalarBorder::fbWidth + 40) * (size_t)(scalarBorder::fbHeight + 40));
    }

    // R3-test, the plan's "pointer mapping at non-zero reserves", done
    // exhaustively: every pixel of a content box whose four reserves
    // share no value, plus eight past each edge. Each probe is checked
    // against the sides spelled out by hand, so a helper that reached
    // for the wrong side answers differently rather than accidentally
    // right - and against grid_geometry, so the cell the pointer names
    // and the grid the layout counts cannot drift apart.
    STD_TEST(EveryPixelOfAnAsymmetricContentBoxAnswersFromItsOwnSide) {
        const int left = asymmetric.insets.left;
        const int top = asymmetric.insets.top;
        const int firstOutsideX = asymmetric.framebufferWidth - asymmetric.insets.right;
        const int firstOutsideY = asymmetric.framebufferHeight - asymmetric.insets.bottom;
        const u32 columns = gridColumns(asymmetric.framebufferWidth, asymmetric.insets, (u16)(asymmetric.glyphWidth));
        const u32 rows = gridRows(asymmetric.framebufferHeight, asymmetric.insets, (u16)(asymmetric.glyphHeight));

        STD_INSIST(columns == 20);
        STD_INSIST(rows == 6);

        size_t inside = 0;
        for (int y = -8; y < asymmetric.framebufferHeight + 8; ++y) {
            for (int x = -8; x < asymmetric.framebufferWidth + 8; ++x) {
                const bool expected = x >= left && x < firstOutsideX && y >= top && y < firstOutsideY;

                u16 column = 0xffff;
                u16 row = 0xffff;
                STD_INSIST(mouseCell(x, y, asymmetric, column, row) == expected);
                if (!expected) {
                    // A pointer over a reserve names no cell at all.
                    STD_INSIST(column == 0xffff && row == 0xffff);
                    continue;
                }
                ++inside;

                STD_INSIST(column == (x - left) / asymmetric.glyphWidth);
                STD_INSIST(row == (y - top) / asymmetric.glyphHeight);

                // The cell the pointer names exists in the grid the
                // layout counts out of the same insets.
                STD_INSIST(column < columns);
                STD_INSIST(row < rows);

                // The other three mappings agree with it inside the box:
                // the protocol reports the same cell one-based, and a
                // selection endpoint lands on it.
                const MouseProtocolPoint sgr = mouseProtocolPoint(MouseTrackingEnc::SGR, x, y, asymmetric);
                STD_INSIST(sgr.column == column + 1);
                STD_INSIST(sgr.row == row + 1);
                STD_INSIST(mouseSelectionCell(x, y, asymmetric, (int)(columns), (int)(rows)) == Point(column, row));
            }

            // Autoscroll takes the top reserve and the bottom one, and
            // neither of the horizontal pair.
            const int expectedScroll = y <= top ? -1 : (y >= firstOutsideY - 1 ? 1 : 0);
            STD_INSIST(mouseAutoscrollDirection(y, asymmetric) == expectedScroll);
        }

        STD_INSIST(inside == (size_t)(columns)*asymmetric.glyphWidth * (size_t)(rows)*asymmetric.glyphHeight);
    }

    // The columns and rows a caller passes are a second clamp, not a
    // restatement of the geometry: they come from the Composer's own
    // grid, which a caller may hold at a smaller size than the content
    // box would hold. Every probe here is answered one way by the
    // caller's grid and another by the box.
    STD_TEST(ClampsTheSelectionToTheGridTheCallerNames) {
        // The box holds 20 x 6; the caller names 12 x 3.
        STD_INSIST(mouseSelectionCell(10000, 10000, asymmetric, 12, 3) == Point(12, 2));
        STD_INSIST(mouseSelectionCell(170, 112, asymmetric, 12, 3) == Point(12, 2));

        // Inside the named grid the pixels still decide.
        STD_INSIST(mouseSelectionCell(27, 45, asymmetric, 12, 3) == Point(2, 1));

        // A one-row grid puts every endpoint on row zero, and a grid
        // with no columns has only the open end to offer.
        STD_INSIST(mouseSelectionCell(170, 112, asymmetric, 20, 1) == Point(19, 0));
        STD_INSIST(mouseSelectionCell(170, 112, asymmetric, 0, 6) == Point(0, 5));
    }

    // Characterization, not endorsement: when the content box ends in
    // the middle of a cell, the pixels of that sliver are inside the box
    // and name a cell the grid does not have. Callers range-check the
    // answer against their own grid - resolveHyperlink does - so this is
    // the contract they are checking against, and it predates A1.
    STD_TEST(NamesACellPastTheGridWhenTheContentBoxEndsMidCell) {
        STD_INSIST(gridColumns(partialCell.framebufferWidth, partialCell.insets, (u16)(partialCell.glyphWidth)) == 20);
        STD_INSIST(gridRows(partialCell.framebufferHeight, partialCell.insets, (u16)(partialCell.glyphHeight)) == 6);

        u16 column = 0;
        u16 row = 0;

        // The last pixel inside the box on each axis.
        STD_INSIST(mouseCell(173, 115, partialCell, column, row));
        STD_INSIST(column == 20);
        STD_INSIST(row == 6);

        // The mappings that carry their own clamp do not let it out.
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 173, 115, partialCell).column == 20);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 173, 115, partialCell).row == 6);
        STD_INSIST(mouseSelectionCell(173, 115, partialCell, 20, 6) == Point(20, 5));
    }

    // A8: the pane's origin arrives as its own pair of numbers and stays
    // that way. The window's insets have to read back exactly as the
    // Composer reported them - an implementation that added the origin
    // into insets.left/top would answer every mapping below the same way
    // and still be wrong, because the reserve and the offset would no
    // longer be separable by the layer that owns each.
    STD_TEST(KeepsThePaneOriginApartFromTheWindowInsets) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 5;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);
        composer.resize(194, 146);

        const MouseGeometry geometry = mouseGeometry(composer, 24, 32);

        STD_INSIST(geometry.paneOriginX == 24);
        STD_INSIST(geometry.paneOriginY == 32);
        STD_INSIST(geometry.insets.left == composer.borderPixels());
        STD_INSIST(geometry.insets.top == composer.borderPixels());
        STD_INSIST(geometry.insets.right == composer.borderPixels());
        STD_INSIST(geometry.insets.bottom == composer.borderPixels());
        STD_INSIST(geometry.contentLeft() == composer.borderPixels() + 24);
        STD_INSIST(geometry.contentTop() == composer.borderPixels() + 32);

        // The form without an origin is the pane that fills the window,
        // which is every build until splits land.
        const MouseGeometry whole = mouseGeometry(composer);
        STD_INSIST(whole.paneOriginX == 0);
        STD_INSIST(whole.paneOriginY == 0);
        STD_INSIST(whole.contentLeft() == composer.borderPixels());
        STD_INSIST(whole.contentTop() == composer.borderPixels());
    }

    // The debt A8 leaves behind, written down as behaviour rather than as
    // a comment: the near edges of the content box moved to the pane, the
    // far ones did not. Nothing hands out a pane's extent yet, so every
    // clamp below still stops at the window's trailing inset, and a pane
    // that begins inside the window is therefore told about pixels past
    // its own last cell.
    //
    // While one pane fills the window the two edges are the same number
    // and this is a tautology. It is written as a test anyway so the debt
    // cannot be retired silently: whoever gives a pane an extent - T9/T10
    // - makes these assertions false, and has to come here, decide what
    // the new answer is, and say so. A comment at the clamp would have
    // been deleted along with the clamp.
    STD_TEST(TheFarEdgesAreStillTheWindowsWhileNoPaneHasAnExtent) {
        u16 column = 0;
        u16 row = 0;

        // panePlaced starts at (35, 49) and the window's content box ends
        // before (171, 113). A pane of ten columns would end at 115 and
        // one of three rows at 97; both pixels below are past that and
        // still inside the window, and both are accepted.
        STD_INSIST(mouseCell(150, 100, panePlaced, column, row));
        STD_INSIST(column == 14);
        STD_INSIST(row == 3);

        // The clamps carry the window's extent too, so the protocol point
        // and the selection endpoint can name a column no ten-column pane
        // has. The numbers are the window's content box divided by the
        // glyph - 160 / 8 across - not the pane's.
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 10000, 10000, panePlaced).column == 20);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 10000, 10000, panePlaced).row == 6);
        STD_INSIST(mouseAutoscrollDirection(111, panePlaced) == 0);
        STD_INSIST(mouseAutoscrollDirection(112, panePlaced) == 1);
    }

    // A8: every pixel-to-cell mapping counts from the pane's origin, not
    // from the window's. Each probe here is answered one way with the
    // origin applied and another way without it, or with the two axes
    // exchanged.
    STD_TEST(EveryPointerMappingCountsFromThePaneOrigin) {
        u16 column = 0;
        u16 row = 0;

        // The pane's first cell: 11 + 24 across, 17 + 32 down.
        STD_INSIST(mouseCell(35, 49, panePlaced, column, row));
        STD_INSIST(column == 0);
        STD_INSIST(row == 0);
        STD_INSIST(mouseCell(43, 65, panePlaced, column, row));
        STD_INSIST(column == 1);
        STD_INSIST(row == 1);

        // One pixel short of the pane, but well inside the window's own
        // content box: no cell of this pane.
        STD_INSIST(!mouseCell(34, 49, panePlaced, column, row));
        STD_INSIST(!mouseCell(35, 48, panePlaced, column, row));

        // The protocol point, cell-encoded and pixel-encoded.
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 35, 49, panePlaced).column == 1);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGR, 35, 49, panePlaced).row == 1);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGRPixels, 40, 56, panePlaced).column == 6);
        STD_INSIST(mouseProtocolPoint(MouseTrackingEnc::SGRPixels, 40, 56, panePlaced).row == 8);

        // The selection endpoint.
        STD_INSIST(mouseSelectionCell(35, 49, panePlaced, 20, 6) == Point(0, 0));
        STD_INSIST(mouseSelectionCell(43, 65, panePlaced, 20, 6) == Point(1, 1));

        // Autoscroll: the top edge is the pane's, so a drag held at the
        // window's own top inset is already above this pane.
        STD_INSIST(mouseAutoscrollDirection(49, panePlaced) == -1);
        STD_INSIST(mouseAutoscrollDirection(50, panePlaced) == 0);
        STD_INSIST(mouseAutoscrollDirection(49, asymmetric) == 0);
    }

    STD_TEST(MapsModifiersAndButtons) {
        STD_INSIST(mouseProtocolModifiers(FrontendShift | FrontendControl | FrontendAlt) == (MouseShift | MouseControl | MouseAlt));
        STD_INSIST(mouseProtocolModifiers(FrontendShift | FrontendControl | FrontendAlt, false) == (MouseShift | MouseControl));
        STD_INSIST(mouseTerminalButton(0) == 1);
        STD_INSIST(mouseTerminalButton(2) == 2);
        STD_INSIST(mouseTerminalButton(1) == 3);
        STD_INSIST(mouseTerminalButton(3) == 8);
        STD_INSIST(mouseTerminalButton(6) == 11);
        STD_INSIST(mouseTerminalButton(-1) == 0);
    }

    STD_TEST(AppliesButtonReportingRules) {
        STD_INSIST(!mouseButtonReportAllowed(MouseTrackingMode::Disabled, MouseEventType::Press, 1));
        STD_INSIST(mouseButtonReportAllowed(MouseTrackingMode::VT200, MouseEventType::Press, 1));
        STD_INSIST(!mouseButtonReportAllowed(MouseTrackingMode::X10_Compat, MouseEventType::Release, 1));
        STD_INSIST(!mouseButtonReportAllowed(MouseTrackingMode::VT200, MouseEventType::Release, 4));
        STD_INSIST(!mouseButtonReportAllowed(MouseTrackingMode::VT200, MouseEventType::Press, 12));
    }

    STD_TEST(AccumulatesFractionalWheelSteps) {
        MouseWheelAccumulator wheel;

        STD_INSIST(wheel.consume(0.0, 0.4, false).y == 0);
        STD_INSIST(wheel.consume(0.0, 0.4, false).y == 0);
        STD_INSIST(wheel.consume(0.0, 0.4, false).y == 1);
        STD_INSIST(wheel.consume(0.0, -0.5, false).y == 0);
        STD_INSIST(wheel.consume(0.0, -0.8, false).y == -1);
    }

    STD_TEST(ResetsWheelRemaindersWhenModeChanges) {
        MouseWheelAccumulator wheel;
        wheel.consume(0.75, 0.75, false);

        const MouseWheelSteps reporting = wheel.consume(0.5, 0.5, true);

        STD_INSIST(reporting.x == 0);
        STD_INSIST(reporting.y == 0);
        STD_INSIST(wheel.consume(0.5, 0.5, true).x == 1);
        wheel.reset();
        STD_INSIST(wheel.consume(0.5, 0.5, true).x == 0);
    }

    STD_TEST(TracksPressedButtonsAndMotionButtonPriority) {
        MouseFrontendState state;
        state.updateButton(1, true);
        state.updateButton(2, true);
        state.updateButton(0, true);

        STD_INSIST(state.buttons() == 7);
        STD_INSIST(state.primaryButtonPressed());
        STD_INSIST(state.motionButton() == 1);

        state.updateButton(0, false);
        STD_INSIST(state.motionButton() == 2);
        state.clearButtons();
        STD_INSIST(state.buttons() == 0);
        STD_INSIST(!state.primaryButtonPressed());
    }

    STD_TEST(SuppressesProtocolDuringSelectionOrShift) {
        MouseFrontendState state;

        STD_INSIST(state.protocolActive(0, MouseTrackingMode::VT200));
        STD_INSIST(!state.protocolActive(FrontendShift, MouseTrackingMode::VT200));
        STD_INSIST(!state.protocolActive(0, MouseTrackingMode::Disabled));
        state.beginSelection();
        STD_INSIST(!state.protocolActive(0, MouseTrackingMode::VT200));
        state.endSelection();
        STD_INSIST(state.protocolActive(0, MouseTrackingMode::VT200));
    }

    STD_TEST(CountsOnlyNearbyRapidClicks) {
        MouseFrontendState state;

        STD_INSIST(state.registerClick(0, 10.0, 10.0, 1.0) == 1);
        STD_INSIST(state.registerClick(0, 12.0, 12.0, 1.4) == 2);
        STD_INSIST(state.registerClick(0, 12.0, 12.0, 1.8) == 3);
        STD_INSIST(state.registerClick(0, 20.0, 12.0, 2.0) == 1);
        STD_INSIST(state.registerClick(1, 20.0, 12.0, 2.1) == 1);
        STD_INSIST(state.registerClick(1, 20.0, 12.0, 1.0) == 1);
    }

    STD_TEST(DeduplicatesMotionWithinSameContext) {
        MouseFrontendState state;

        STD_INSIST(state.reportMotion(1, 1, MouseTrackingMode::VT200, MouseTrackingEnc::SGR, 1));
        STD_INSIST(!state.reportMotion(1, 1, MouseTrackingMode::VT200, MouseTrackingEnc::SGR, 1));
        STD_INSIST(state.reportMotion(2, 1, MouseTrackingMode::VT200, MouseTrackingEnc::SGR, 1));
        STD_INSIST(state.reportMotion(2, 1, MouseTrackingMode::VT200_ButtonEvent, MouseTrackingEnc::SGR, 1));
        STD_INSIST(state.reportMotion(2, 1, MouseTrackingMode::VT200_ButtonEvent, MouseTrackingEnc::UTF8, 1));
        STD_INSIST(state.reportMotion(2, 1, MouseTrackingMode::VT200_ButtonEvent, MouseTrackingEnc::UTF8, 2));
        state.resetMotion();
        STD_INSIST(state.reportMotion(2, 1, MouseTrackingMode::VT200_ButtonEvent, MouseTrackingEnc::UTF8, 2));
    }
}
