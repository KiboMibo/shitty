#include "mouseprotocol.h"

#include "utf8.h"

#include <sstream>

std::string encodeMouseProtocol(MouseTrackingEnc encoding,
                                MouseEventType type,
                                unsigned modifiers,
                                int motionButton,
                                int button,
                                int column,
                                int row) {
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
    } else if (type == MouseEventType::Release &&
               encoding != MouseTrackingEnc::SGR) {
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
            output << "\x1b[M" << static_cast<char>(32 + code)
                   << static_cast<char>(32 + column)
                   << static_cast<char>(32 + row);
            break;
        case MouseTrackingEnc::UTF8:
            output << "\x1b[M";
            Utf8Encoder::pushUnicode(
                32 + code, [&output](char ch) {
                output << ch;
            });
            Utf8Encoder::pushUnicode(
                32 + column, [&output](char ch) {
                output << ch;
            });
            Utf8Encoder::pushUnicode(
                32 + row, [&output](char ch) {
                output << ch;
            });
            break;
        case MouseTrackingEnc::SGR:
            output << "\x1b[<" << code << ';' << column << ';' << row
                   << (type == MouseEventType::Release ? 'm' : 'M');
            break;
        case MouseTrackingEnc::URXVT:
            output << "\x1b[" << code + 32 << ';' << column << ';' << row
                   << 'M';
            break;
    }
    return output.str();
}
