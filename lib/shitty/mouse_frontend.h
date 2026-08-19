/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include "composer.h"
#include "mouse_protocol.h"
#include "point.h"

enum FrontendModifier : unsigned {
    FrontendShift = 1,
    FrontendControl = 2,
    FrontendAlt = 4,
};

// A1: the pointer's half of the layout. The content box starts at
// (insets.left, insets.top) and ends before (framebufferWidth -
// insets.right, framebufferHeight - insets.bottom); everything outside
// it belongs to the border and to whatever chrome reserves a side.
struct MouseGeometry {
    int framebufferWidth = 1;
    int framebufferHeight = 1;
    Insets insets;
    int glyphWidth = 1;
    int glyphHeight = 1;
};

struct MouseProtocolPoint {
    int column = 1;
    int row = 1;
};

int mouseFramebufferCoordinate(double logical, double scale);

// The one place a Composer becomes a pointer geometry, so no caller can
// pair a side with the wrong axis on its way in.
MouseGeometry mouseGeometry(const Composer& composer);

MouseProtocolPoint mouseProtocolPoint(MouseTrackingEnc encoding, int pixelX, int pixelY, const MouseGeometry& geometry);

// The cell a pixel lands in; false when it lands in an inset rather than
// in the content box, and then column and row are left alone.
bool mouseCell(int pixelX, int pixelY, const MouseGeometry& geometry, u16& column, u16& row);

// The cell a selection endpoint names: clamped into the content box, and
// allowed to land one column past the last one, which is what the open
// end of an extent means.
Point mouseSelectionCell(int pixelX, int pixelY, const MouseGeometry& geometry, int columns, int rows);

// -1 above the content box, +1 below it, 0 inside - the direction a drag
// held at this pixel scrolls.
int mouseAutoscrollDirection(int pixelY, const MouseGeometry& geometry);

unsigned mouseProtocolModifiers(unsigned frontendModifiers, bool reportAlt = true);
int mouseTerminalButton(int frontendButton);
bool mouseButtonReportAllowed(MouseTrackingMode mode, MouseEventType type, int button);

struct MouseWheelSteps {
    int x = 0;
    int y = 0;
};

class MouseWheelAccumulator {
public:
    MouseWheelSteps consume(double x, double y, bool reporting);
    void reset();

private:
    static int consumeAxis(double delta, double& remainder);

    bool reporting_ = false;
    double remainderX_ = 0.0;
    double remainderY_ = 0.0;
};

class MouseFrontendState {
public:
    bool protocolActive(unsigned modifiers, MouseTrackingMode mode) const;
    void updateButton(int button, bool pressed);

    void clearButtons() {
        buttons_ = 0;
    }

    unsigned buttons() const {
        return buttons_;
    }

    int motionButton() const;
    bool primaryButtonPressed() const;

    int registerClick(int button, double x, double y, double time);

    void beginSelection() {
        selectionOngoing_ = true;
    }

    void endSelection() {
        selectionOngoing_ = false;
    }

    bool selectionOngoing() const {
        return selectionOngoing_;
    }

    bool reportMotion(int column, int row, MouseTrackingMode mode, MouseTrackingEnc encoding, u32 generation);
    void resetMotion();

    MouseWheelSteps consumeWheel(double x, double y, bool reporting) {
        return wheel_.consume(x, y, reporting);
    }

private:
    bool selectionOngoing_ = false;
    unsigned buttons_ = 0;
    int lastButton_ = -1;
    int clickCount_ = 0;
    double lastClickTime_ = 0.0;
    double lastClickX_ = 0.0;
    double lastClickY_ = 0.0;
    bool hasReportedMotion_ = false;
    bool hasMotionContext_ = false;
    MouseTrackingMode lastMotionMode_ = MouseTrackingMode::Disabled;
    MouseTrackingEnc lastMotionEncoding_ = MouseTrackingEnc::Default;
    u32 lastMotionGeneration_ = 0;
    int lastReportColumn_ = 0;
    int lastReportRow_ = 0;
    MouseWheelAccumulator wheel_;
};
