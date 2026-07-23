/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "vterm.h"

#include <string>

enum class MouseEventType {
    Press,
    Release,
    Motion
};

enum MouseProtocolModifier : unsigned {
    MouseShift = 1,
    MouseAlt = 2,
    MouseControl = 4
};

std::string encodeMouseProtocol(MouseTrackingEnc encoding, MouseEventType type, unsigned modifiers, int motionButton, int button, int column, int row);
