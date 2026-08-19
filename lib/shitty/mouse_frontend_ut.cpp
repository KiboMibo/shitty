/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "mouse_frontend.h"

#include "options.h"

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
