/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "mouse_protocol.h"

#include "utf8.h"

#include <algorithm>
#include <sstream>

namespace stl {}

using namespace stl;

std::string encodeMouseProtocol(MouseTrackingEnc encoding, MouseEventType type, unsigned modifiers, int motionButton, int button, int column, int row) {
    int code = 0;
    if (type == MouseEventType::Motion) {
        switch (motionButton) {
            case 1:
                code = 32;
                break;
            case 2:
                code = 33;
                break;
            case 3:
                code = 34;
                break;
            default:
                code = 35;
                break;
        }
    } else if (type == MouseEventType::Release && encoding != MouseTrackingEnc::SGR) {
        code = 3;
    } else {
        switch (button) {
            case 1:
                code = 0;
                break;
            case 2:
                code = 1;
                break;
            case 3:
                code = 2;
                break;
            case 4:
                code = 64;
                break;
            case 5:
                code = 65;
                break;
            case 6:
                code = 66;
                break;
            case 7:
                code = 67;
                break;
            case 8:
                code = 128;
                break;
            case 9:
                code = 129;
                break;
            case 10:
                code = 130;
                break;
            case 11:
                code = 131;
                break;
            default:
                return {};
        }
    }

    if (modifiers & MouseShift) {
        code += 4;
    }
    if (modifiers & MouseAlt) {
        code += 8;
    }
    if (modifiers & MouseControl) {
        code += 16;
    }

    std::ostringstream output;
    switch (encoding) {
        case MouseTrackingEnc::Default:
            column = std::clamp(column, 1, 223);
            row = std::clamp(row, 1, 223);
            output << "\x1b[M" << (char)(32 + code) << (char)(32 + column) << (char)(32 + row);
            break;
        case MouseTrackingEnc::UTF8:
            column = std::clamp(column, 1, 2015);
            row = std::clamp(row, 1, 2015);
            output << "\x1b[M";
            Utf8Encoder::pushUnicode(32 + code, [&output](char ch) {
                output << ch;
            });
            Utf8Encoder::pushUnicode(32 + column, [&output](char ch) {
                output << ch;
            });
            Utf8Encoder::pushUnicode(32 + row, [&output](char ch) {
                output << ch;
            });
            break;
        case MouseTrackingEnc::SGR:
        case MouseTrackingEnc::SGRPixels:
            output << "\x1b[<" << code << ';' << column << ';' << row << (type == MouseEventType::Release ? 'm' : 'M');
            break;
        case MouseTrackingEnc::URXVT:
            output << "\x1b[" << code + 32 << ';' << column << ';' << row << 'M';
            break;
    }
    return output.str();
}
