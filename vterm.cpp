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

#include "options.h"
#include "pty.h"
#include "vterm.h"

#include <cstring>
#include <cerrno>
#include <fcntl.h>

namespace {
    using Key = VtKey;
    using InputSpec = Vterm::InputSpec;

#define ESC "\x1b"
#define CSI ESC "["
#define SS3 ESC "O"

#define MC "\xff"

    const InputSpec is_modOtherKeys2[] =
        {
            {Key::K2, CSI "27;" MC ";50~"},
            {Key::K3, CSI "27;" MC ";51~"},
            {Key::K4, CSI "27;" MC ";52~"},
            {Key::K5, CSI "27;" MC ";53~"},
            {Key::K6, CSI "27;" MC ";54~"},
            {Key::K7, CSI "27;" MC ";55~"},
            {Key::K8, CSI "27;" MC ";56~"},
            {Key::Backtick, CSI "27;" MC ";96~"},
            {Key::Tilde, CSI "27;" MC ";126~"},
            {Key::Tab, CSI "27;" MC ";9~"},
            {Key::Return, CSI "27;" MC ";13~"},
            {Key::Space, CSI "27;" MC ";32~"},
            {Key::Backspace, CSI "27;" MC ";127~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Alt[] =
        {

            {Key::K0, "\xc2\xb0"},
            {Key::K1, "\xc2\xb1"},
            {Key::K2, "\xc2\xb2"},
            {Key::K3, "\xc2\xb3"},
            {Key::K4, "\xc2\xb4"},
            {Key::K5, "\xc2\xb5"},
            {Key::K6, "\xc2\xb6"},
            {Key::K7, "\xc2\xb7"},
            {Key::K8, "\xc2\xb8"},
            {Key::K9, "\xc2\xb9"},
            {Key::Backtick, "\xc3\xa0"},
            {Key::Tilde, "\xc3\xbe"},
            {Key::Backspace, "\xc3\xbf"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Alt_altSendsEscape[] =
        {
            {Key::K0, ESC "0"},
            {Key::K1, ESC "1"},
            {Key::K2, ESC "2"},
            {Key::K3, ESC "3"},
            {Key::K4, ESC "4"},
            {Key::K5, ESC "5"},
            {Key::K6, ESC "6"},
            {Key::K7, ESC "7"},
            {Key::K8, ESC "8"},
            {Key::K9, ESC "9"},
            {Key::Backtick, ESC "`"},
            {Key::Tilde, ESC "~"},
            {Key::Backspace, ESC "\x7f"},
            {Key::Space, ESC " "},
            {Key::Tab, ESC "\t"},
            {Key::Return, ESC "\n"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Control_modOtherKeys[] =
        {
            {Key::K0, CSI "27;" MC ";48~"},
            {Key::K1, CSI "27;" MC ";49~"},
            {Key::K9, CSI "27;" MC ";57~"},
            {Key::Tab, CSI "27;" MC ";9~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_ControlAlt_altSendsEscape[] =
        {
            {Key::K2, ESC "\x00", 2},
            {Key::K3, ESC "\x1b", 2},
            {Key::K4, ESC "\x1c", 2},
            {Key::K5, ESC "\x1d", 2},
            {Key::K6, ESC "\x1e", 2},
            {Key::K7, ESC "\x1f", 2},
            {Key::K8, ESC "\x7f", 2},
            {Key::Backtick, ESC "\x00", 2},
            {Key::Tilde, ESC "\x1e", 2},
            {Key::Space, ESC "\x00", 2},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Control[] =
        {
            {Key::K2, "\x00", 1},
            {Key::K3, "\x1b", 1},
            {Key::K4, "\x1c", 1},
            {Key::K5, "\x1d", 1},
            {Key::K6, "\x1e", 1},
            {Key::K7, "\x1f", 1},
            {Key::K8, "\x7f", 1},
            {Key::Backtick, "\x00", 1},
            {Key::Tilde, "\x1e", 1},
            {Key::Space, "\x00", 1},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Shift[] =
        {
            {Key::Tab, CSI "Z"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_modOtherKeys[] =
        {
            {Key::Return, CSI "27;" MC ";13~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi[] =
        {
            {Key::K0, "0"},
            {Key::K1, "1"},
            {Key::K2, "2"},
            {Key::K3, "3"},
            {Key::K4, "4"},
            {Key::K5, "5"},
            {Key::K6, "6"},
            {Key::K7, "7"},
            {Key::K8, "8"},
            {Key::K9, "9"},
            {Key::Backtick, "`"},
            {Key::Tilde, "~"},
            {Key::Space, " "},
            {Key::Backspace, "\x7f"},
            {Key::Tab, "\t"},
            {Key::Return, "\r"},
            {Key::Insert, CSI "2~"},
            {Key::Delete, CSI "3~"},
            {Key::PageUp, CSI "5~"},
            {Key::PageDown, CSI "6~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_Ansi[] =
        {
            {Key::Insert, CSI "2;" MC "~"},
            {Key::Delete, CSI "3;" MC "~"},
            {Key::PageUp, CSI "5;" MC "~"},
            {Key::PageDown, CSI "6;" MC "~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi_FunctionKeys[] =
        {
            {Key::F1, SS3 "P"},
            {Key::KP_F1, SS3 "P"},
            {Key::F2, SS3 "Q"},
            {Key::KP_F2, SS3 "Q"},
            {Key::F3, SS3 "R"},
            {Key::KP_F3, SS3 "R"},
            {Key::F4, SS3 "S"},
            {Key::KP_F4, SS3 "S"},
            {Key::F5, CSI "15~"},
            {Key::F6, CSI "17~"},
            {Key::F7, CSI "18~"},
            {Key::F8, CSI "19~"},
            {Key::F9, CSI "20~"},
            {Key::F10, CSI "21~"},
            {Key::F11, CSI "23~"},
            {Key::F12, CSI "24~"},
            {Key::F13, CSI "25~"},
            {Key::F14, CSI "26~"},
            {Key::F15, CSI "28~"},
            {Key::F16, CSI "29~"},
            {Key::F17, CSI "31~"},
            {Key::F18, CSI "32~"},
            {Key::F19, CSI "33~"},
            {Key::F20, CSI "34~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_Ansi_FunctionKeys[] =
        {
            {Key::F1, CSI "1;" MC "P"},
            {Key::KP_F1, CSI "1;" MC "P"},
            {Key::F2, CSI "1;" MC "Q"},
            {Key::KP_F2, CSI "1;" MC "Q"},
            {Key::F3, CSI "1;" MC "R"},
            {Key::KP_F3, CSI "1;" MC "R"},
            {Key::F4, CSI "1;" MC "S"},
            {Key::KP_F4, CSI "1;" MC "S"},
            {Key::F5, CSI "15;" MC "~"},
            {Key::F6, CSI "17;" MC "~"},
            {Key::F7, CSI "18;" MC "~"},
            {Key::F8, CSI "19;" MC "~"},
            {Key::F9, CSI "20;" MC "~"},
            {Key::F10, CSI "21;" MC "~"},
            {Key::F11, CSI "23;" MC "~"},
            {Key::F12, CSI "24;" MC "~"},
            {Key::F13, CSI "25;" MC "~"},
            {Key::F14, CSI "26;" MC "~"},
            {Key::F15, CSI "28;" MC "~"},
            {Key::F16, CSI "29;" MC "~"},
            {Key::F17, CSI "31;" MC "~"},
            {Key::F18, CSI "32;" MC "~"},
            {Key::F19, CSI "33;" MC "~"},
            {Key::F20, CSI "34;" MC "~"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi_KeypadKeys[] =
        {
            {Key::KP_Space, " "},
            {Key::KP_Tab, "\t"},
            {Key::KP_Enter, "\r"},
            {Key::KP_Star, "*"},
            {Key::KP_Plus, "+"},
            {Key::KP_Comma, ","},
            {Key::KP_Minus, "-"},
            {Key::KP_Slash, "/"},
            {Key::KP_Delete, "."},
            {Key::KP_Dot, "."},
            {Key::KP_Insert, "0"},
            {Key::KP_0, "0"},
            {Key::KP_End, "1"},
            {Key::KP_1, "1"},
            {Key::KP_Down, "2"},
            {Key::KP_2, "2"},
            {Key::KP_PageDown, "3"},
            {Key::KP_3, "3"},
            {Key::KP_Left, "4"},
            {Key::KP_4, "4"},
            {Key::KP_Begin, "5"},
            {Key::KP_5, "5"},
            {Key::KP_Right, "6"},
            {Key::KP_6, "6"},
            {Key::KP_Home, "7"},
            {Key::KP_7, "7"},
            {Key::KP_Up, "8"},
            {Key::KP_8, "8"},
            {Key::KP_PageUp, "9"},
            {Key::KP_9, "9"},
            {Key::KP_Equal, "="},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Appl_KeypadKeys[] =
        {
            {Key::KP_Space, SS3 " "},
            {Key::KP_Tab, SS3 "I"},
            {Key::KP_Enter, SS3 "M"},
            {Key::KP_Star, SS3 "j"},
            {Key::KP_Plus, SS3 "k"},
            {Key::KP_Comma, SS3 "l"},
            {Key::KP_Minus, SS3 "m"},
            {Key::KP_Delete, SS3 "n"},
            {Key::KP_Dot, SS3 "n"},
            {Key::KP_Slash, SS3 "o"},
            {Key::KP_Insert, SS3 "p"},
            {Key::KP_0, SS3 "p"},
            {Key::KP_End, SS3 "q"},
            {Key::KP_1, SS3 "q"},
            {Key::KP_Down, SS3 "r"},
            {Key::KP_2, SS3 "r"},
            {Key::KP_PageDown, SS3 "s"},
            {Key::KP_3, SS3 "s"},
            {Key::KP_Left, SS3 "t"},
            {Key::KP_4, SS3 "t"},
            {Key::KP_Begin, SS3 "u"},
            {Key::KP_5, SS3 "u"},
            {Key::KP_Right, SS3 "v"},
            {Key::KP_6, SS3 "v"},
            {Key::KP_Home, SS3 "w"},
            {Key::KP_7, SS3 "w"},
            {Key::KP_Up, SS3 "x"},
            {Key::KP_8, SS3 "x"},
            {Key::KP_PageUp, SS3 "y"},
            {Key::KP_9, SS3 "y"},
            {Key::KP_Equal, SS3 "X"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_Appl_KeypadKeys[] =
        {
            {Key::KP_Space, SS3 MC " "},
            {Key::KP_Tab, SS3 MC "I"},
            {Key::KP_Enter, SS3 MC "M"},
            {Key::KP_Star, SS3 MC "j"},
            {Key::KP_Plus, SS3 MC "k"},
            {Key::KP_Comma, SS3 MC "l"},
            {Key::KP_Minus, SS3 MC "m"},
            {Key::KP_Delete, SS3 MC "n"},
            {Key::KP_Dot, SS3 MC "n"},
            {Key::KP_Slash, SS3 MC "o"},
            {Key::KP_Insert, SS3 MC "p"},
            {Key::KP_0, SS3 MC "p"},
            {Key::KP_End, SS3 MC "q"},
            {Key::KP_1, SS3 MC "q"},
            {Key::KP_Down, SS3 MC "r"},
            {Key::KP_2, SS3 MC "r"},
            {Key::KP_PageDown, SS3 MC "s"},
            {Key::KP_3, SS3 MC "s"},
            {Key::KP_Left, SS3 MC "t"},
            {Key::KP_4, SS3 MC "t"},
            {Key::KP_Begin, SS3 MC "u"},
            {Key::KP_5, SS3 MC "u"},
            {Key::KP_Right, SS3 MC "v"},
            {Key::KP_6, SS3 MC "v"},
            {Key::KP_Home, SS3 MC "w"},
            {Key::KP_7, SS3 MC "w"},
            {Key::KP_Up, SS3 MC "x"},
            {Key::KP_8, SS3 MC "x"},
            {Key::KP_PageUp, SS3 MC "y"},
            {Key::KP_9, SS3 MC "y"},
            {Key::KP_Equal, SS3 MC "X"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_VT52_KeypadKeys[] =
        {
            {Key::KP_Space, ESC "? "},
            {Key::KP_Tab, ESC "?I"},
            {Key::KP_Enter, ESC "?M"},
            {Key::KP_Star, ESC "?j"},
            {Key::KP_Plus, ESC "?k"},
            {Key::KP_Comma, ESC "?l"},
            {Key::KP_Minus, ESC "?m"},
            {Key::KP_Delete, ESC "?n"},
            {Key::KP_Dot, ESC "?n"},
            {Key::KP_Slash, ESC "?o"},
            {Key::KP_Insert, ESC "?p"},
            {Key::KP_0, ESC "?p"},
            {Key::KP_End, ESC "?q"},
            {Key::KP_1, ESC "?q"},
            {Key::KP_Down, ESC "?r"},
            {Key::KP_2, ESC "?r"},
            {Key::KP_PageDown, ESC "?s"},
            {Key::KP_3, ESC "?s"},
            {Key::KP_Left, ESC "?t"},
            {Key::KP_4, ESC "?t"},
            {Key::KP_Begin, ESC "?u"},
            {Key::KP_5, ESC "?u"},
            {Key::KP_Right, ESC "?v"},
            {Key::KP_6, ESC "?v"},
            {Key::KP_Home, ESC "?w"},
            {Key::KP_7, ESC "?w"},
            {Key::KP_Up, ESC "?x"},
            {Key::KP_8, ESC "?x"},
            {Key::KP_PageUp, ESC "?y"},
            {Key::KP_9, ESC "?y"},
            {Key::KP_Equal, ESC "?X"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_VT52_FunctionKeys[] =
        {
            {Key::F1, ESC "P"},
            {Key::KP_F1, ESC "P"},
            {Key::F2, ESC "Q"},
            {Key::KP_F2, ESC "Q"},
            {Key::F3, ESC "R"},
            {Key::KP_F3, ESC "R"},
            {Key::F4, ESC "S"},
            {Key::KP_F4, ESC "S"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi_CursorKeys[] =
        {
            {Key::Up, CSI "A"},
            {Key::Down, CSI "B"},
            {Key::Right, CSI "C"},
            {Key::Left, CSI "D"},
            {Key::Home, CSI "H"},
            {Key::End, CSI "F"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Appl_CursorKeys[] =
        {
            {Key::Up, SS3 "A"},
            {Key::Down, SS3 "B"},
            {Key::Right, SS3 "C"},
            {Key::Left, SS3 "D"},
            {Key::Home, SS3 "H"},
            {Key::End, SS3 "F"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_CursorKeys[] =
        {
            {Key::Up, CSI "1;" MC "A"},
            {Key::Down, CSI "1;" MC "B"},
            {Key::Right, CSI "1;" MC "C"},
            {Key::Left, CSI "1;" MC "D"},
            {Key::Home, CSI "1;" MC "H"},
            {Key::End, CSI "1;" MC "F"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_VT52_CursorKeys[] =
        {
            {Key::Up, ESC "A"},
            {Key::Down, ESC "B"},
            {Key::Right, ESC "C"},
            {Key::Left, ESC "D"},
            {Key::Home, ESC "H"},
            {Key::End, ESC "F"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_ReturnKey_ANL[] =
        {
            {Key::Return, "\r\n"},
            {Key::KP_Enter, "\r\n"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_BackspaceKey_BkSp[] =
        {
            {Key::Backspace, "\b"},
            {Key::NONE, nullptr},
    };

    const InputSpec is_Alt_BackspaceKey_BkSp[] =
        {
            {Key::Backspace, ESC "\b"},
            {Key::NONE, nullptr},
    };

#undef ESC
#undef CSI
#undef SS3

    inline uint8_t
    getModifierCode(VtModifier modifiers) {
        switch (modifiers) {
            case VtModifier::none:
                return 0;
            case VtModifier::shift:
                return 2;
            case VtModifier::alt:
                return 3;
            case VtModifier::shift_alt:
                return 4;
            case VtModifier::control:
                return 5;
            case VtModifier::shift_control:
                return 6;
            case VtModifier::control_alt:
                return 7;
            case VtModifier::shift_control_alt:
                return 8;
        }
        return 0;
    }

    struct KittyKeySpec {
        uint32_t code = 0;
        char final = 'u';
    };

    bool isKittyModifierKey(VtKey key) {
        return key >= VtKey::LeftShift && key <= VtKey::RightSuper;
    }

    bool isKittyRecoveryKey(VtKey key) {
        return key == VtKey::Return || key == VtKey::Tab ||
               key == VtKey::Backspace;
    }

    VtModifier kittyToLegacyModifiers(uint16_t modifiers) {
        VtModifier result = VtModifier::none;
        if (modifiers & 1) result = result | VtModifier::shift;
        if (modifiers & 2) result = result | VtModifier::alt;
        if (modifiers & 4) result = result | VtModifier::control;
        return result;
    }

    bool validKittyAssociatedText(uint32_t codepoint) {
        return codepoint >= 0x20 &&
               !(codepoint >= 0x7f && codepoint <= 0x9f);
    }

    KittyKeySpec
    kittyKeySpec(VtKey key) {
        using Key = VtKey;
        switch (key) {
            case Key::Return:
                return {13, 'u'};
            case Key::Backspace:
                return {127, 'u'};
            case Key::Tab:
                return {9, 'u'};
            case Key::Insert:
                return {2, '~'};
            case Key::Delete:
                return {3, '~'};
            case Key::Up:
                return {1, 'A'};
            case Key::Down:
                return {1, 'B'};
            case Key::Right:
                return {1, 'C'};
            case Key::Left:
                return {1, 'D'};
            case Key::Home:
                return {1, 'H'};
            case Key::End:
                return {1, 'F'};
            case Key::PageUp:
                return {5, '~'};
            case Key::PageDown:
                return {6, '~'};
            case Key::F1:
            case Key::KP_F1:
                return {1, 'P'};
            case Key::F2:
            case Key::KP_F2:
                return {1, 'Q'};
            case Key::F3:
            case Key::KP_F3:
                return {1, 'R'};
            case Key::F4:
            case Key::KP_F4:
                return {1, 'S'};
            case Key::F5:
                return {15, '~'};
            case Key::F6:
                return {17, '~'};
            case Key::F7:
                return {18, '~'};
            case Key::F8:
                return {19, '~'};
            case Key::F9:
                return {20, '~'};
            case Key::F10:
                return {21, '~'};
            case Key::F11:
                return {23, '~'};
            case Key::F12:
                return {24, '~'};
            case Key::F13:
                return {57376, 'u'};
            case Key::F14:
                return {57377, 'u'};
            case Key::F15:
                return {57378, 'u'};
            case Key::F16:
                return {57379, 'u'};
            case Key::F17:
                return {57380, 'u'};
            case Key::F18:
                return {57381, 'u'};
            case Key::F19:
                return {57382, 'u'};
            case Key::F20:
                return {57383, 'u'};
            case Key::KP_0:
                return {57399, 'u'};
            case Key::KP_1:
                return {57400, 'u'};
            case Key::KP_2:
                return {57401, 'u'};
            case Key::KP_3:
                return {57402, 'u'};
            case Key::KP_4:
                return {57403, 'u'};
            case Key::KP_5:
                return {57404, 'u'};
            case Key::KP_6:
                return {57405, 'u'};
            case Key::KP_7:
                return {57406, 'u'};
            case Key::KP_8:
                return {57407, 'u'};
            case Key::KP_9:
                return {57408, 'u'};
            case Key::KP_Dot:
                return {57409, 'u'};
            case Key::KP_Slash:
                return {57410, 'u'};
            case Key::KP_Star:
                return {57411, 'u'};
            case Key::KP_Minus:
                return {57412, 'u'};
            case Key::KP_Plus:
                return {57413, 'u'};
            case Key::KP_Enter:
                return {57414, 'u'};
            case Key::KP_Equal:
                return {57415, 'u'};
            case Key::KP_Comma:
                return {57416, 'u'};
            case Key::KP_Left:
                return {57417, 'u'};
            case Key::KP_Right:
                return {57418, 'u'};
            case Key::KP_Up:
                return {57419, 'u'};
            case Key::KP_Down:
                return {57420, 'u'};
            case Key::KP_PageUp:
                return {57421, 'u'};
            case Key::KP_PageDown:
                return {57422, 'u'};
            case Key::KP_Home:
                return {57423, 'u'};
            case Key::KP_End:
                return {57424, 'u'};
            case Key::KP_Insert:
                return {57425, 'u'};
            case Key::KP_Delete:
                return {57426, 'u'};
            case Key::KP_Begin:
                return {57427, 'u'};
            case Key::CapsLock:
                return {57358, 'u'};
            case Key::ScrollLock:
                return {57359, 'u'};
            case Key::NumLock:
                return {57360, 'u'};
            case Key::Print:
                return {57361, 'u'};
            case Key::Pause:
                return {57362, 'u'};
            case Key::Menu:
                return {57363, 'u'};
            case Key::LeftShift:
                return {57441, 'u'};
            case Key::LeftControl:
                return {57442, 'u'};
            case Key::LeftAlt:
                return {57443, 'u'};
            case Key::LeftSuper:
                return {57444, 'u'};
            case Key::RightShift:
                return {57447, 'u'};
            case Key::RightControl:
                return {57448, 'u'};
            case Key::RightAlt:
                return {57449, 'u'};
            case Key::RightSuper:
                return {57450, 'u'};
            default:
                return {};
        }
    }

    void
    makePalette256(Color p[]) {
        opts.getColor("color0", p[0]);
        opts.getColor("color1", p[1]);
        opts.getColor("color2", p[2]);
        opts.getColor("color3", p[3]);
        opts.getColor("color4", p[4]);
        opts.getColor("color5", p[5]);
        opts.getColor("color6", p[6]);
        opts.getColor("color7", p[7]);
        opts.getColor("color8", p[8]);
        opts.getColor("color9", p[9]);
        opts.getColor("color10", p[10]);
        opts.getColor("color11", p[11]);
        opts.getColor("color12", p[12]);
        opts.getColor("color13", p[13]);
        opts.getColor("color14", p[14]);
        opts.getColor("color15", p[15]);

        for (uint8_t r = 0; r < 6; ++r) {
            for (uint8_t g = 0; g < 6; ++g) {
                for (uint8_t b = 0; b < 6; ++b) {
                    uint8_t ri = r ? 55 + 40 * r : 0;
                    uint8_t gi = g ? 55 + 40 * g : 0;
                    uint8_t bi = b ? 55 + 40 * b : 0;
                    p[16 + 36 * r + 6 * g + b] = {ri, gi, bi};
                }
            }
        }

        for (uint8_t s = 0; s < 24; ++s) {
            uint8_t i = 8 + 10 * s;
            p[232 + s] = {i, i, i};
        }
    }

    /* These tables perform translation of built-in "hard" character sets
    * to 16-bit Unicode points. All sets are defined as 96 characters, even
    * those originally designated by DEC as 94-character sets.
    *
    * These tables are referenced by Vterm::charCodes (see below).
    */

    const uint16_t uc_DecSpec[] =
        {
            0x0020,
            0x0021,
            0x0022,
            0x0023,
            0x0024,
            0x0025,
            0x0026,
            0x0027,
            0x0028,
            0x0029,
            0x002a,
            0x002b,
            0x002c,
            0x002d,
            0x002e,
            0x002f,
            0x0030,
            0x0031,
            0x0032,
            0x0033,
            0x0034,
            0x0035,
            0x0036,
            0x0037,
            0x0038,
            0x0039,
            0x003a,
            0x003b,
            0x003c,
            0x003d,
            0x003e,
            0x003f,

            0x0040,
            0x0041,
            0x0042,
            0x0043,
            0x0044,
            0x0045,
            0x0046,
            0x0047,
            0x0048,
            0x0049,
            0x004a,
            0x004b,
            0x004c,
            0x004d,
            0x004e,
            0x004f,
            0x0050,
            0x0051,
            0x0052,
            0x0053,
            0x0054,
            0x0055,
            0x0056,
            0x0057,
            0x0058,
            0x0059,
            0x005a,
            0x005b,
            0x005c,
            0x005d,
            0x005e,
            0x005f,

            0x25c6,
            0x2592,
            0x2409,
            0x240c,
            0x240d,
            0x240a,
            0x00b0,
            0x00b1,
            0x2424,
            0x240b,
            0x2518,
            0x2510,
            0x250c,
            0x2514,
            0x253c,
            0x23ba,
            0x23bb,
            0x2500,
            0x23bc,
            0x23bd,
            0x251c,
            0x2524,
            0x2534,
            0x252c,
            0x2502,
            0x2264,
            0x2265,
            0x03c0,
            0x2260,
            0x00a3,
            0x00b7,
            0x0020,
    };

    const uint16_t uc_DecSuppl[] =
        {
            0x0020,
            0x00a1,
            0x00a2,
            0x00a3,
            0x0024,
            0x00a5,
            0x0026,
            0x00a7,
            0x00a4,
            0x00a9,
            0x00aa,
            0x00ab,
            0x002c,
            0x002d,
            0x002e,
            0x002f,
            0x00b0,
            0x00b1,
            0x00b2,
            0x00b3,
            0x0034,
            0x00b5,
            0x00b6,
            0x00b7,
            0x0038,
            0x00b9,
            0x00ba,
            0x00bb,
            0x00bc,
            0x00bd,
            0x003e,
            0x00bf,

            0x00c0,
            0x00c1,
            0x00c2,
            0x00c3,
            0x00c4,
            0x00c5,
            0x00c6,
            0x00c7,
            0x00c8,
            0x00c9,
            0x00ca,
            0x00cb,
            0x00cc,
            0x00cd,
            0x00ce,
            0x00cf,
            0x0050,
            0x00d1,
            0x00d2,
            0x00d3,
            0x00d4,
            0x00d5,
            0x00d6,
            0x0152,
            0x00d8,
            0x00d9,
            0x00da,
            0x00db,
            0x00dc,
            0x0178,
            0x005e,
            0x00df,

            0x00e0,
            0x00e1,
            0x00e2,
            0x00e3,
            0x00e4,
            0x00e5,
            0x00e6,
            0x00e7,
            0x00e8,
            0x00e9,
            0x00ea,
            0x00eb,
            0x00ec,
            0x00ed,
            0x00ee,
            0x00ef,
            0x0070,
            0x00f1,
            0x00f2,
            0x00f3,
            0x00f4,
            0x00f5,
            0x00f6,
            0x0153,
            0x00f8,
            0x00f9,
            0x00fa,
            0x00fb,
            0x00fc,
            0x00ff,
            0x007e,
            0x007f,
    };

    const uint16_t uc_DecTechn[] =
        {
            0x0020,
            0x23b7,
            0x250c,
            0x2500,
            0x2320,
            0x2321,
            0x2502,
            0x23a1,
            0x23a3,
            0x23a4,
            0x23a6,
            0x239b,
            0x239d,
            0x239e,
            0x23a0,
            0x23a8,
            0x23ac,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x0020,
            0x2264,
            0x2260,
            0x2265,
            0x222b,

            0x2234,
            0x221d,
            0x221e,
            0x00f7,
            0x0394,
            0x2207,
            0x03a6,
            0x0393,
            0x223c,
            0x2243,
            0x0398,
            0x00d7,
            0x039b,
            0x21d4,
            0x21d2,
            0x2261,
            0x03a0,
            0x03a8,
            0x0020,
            0x03a3,
            0x0020,
            0x0020,
            0x221a,
            0x03a9,
            0x039e,
            0x03a5,
            0x2282,
            0x2283,
            0x2229,
            0x222a,
            0x2227,
            0x2228,

            0x00ac,
            0x03b1,
            0x03b2,
            0x03c7,
            0x03b4,
            0x03b5,
            0x03c6,
            0x03b3,
            0x03b7,
            0x03b9,
            0x03b8,
            0x03ba,
            0x03bb,
            0x0020,
            0x03bd,
            0x2202,
            0x03c0,
            0x03c8,
            0x03c1,
            0x03c3,
            0x03c4,
            0x0020,
            0x0192,
            0x03c9,
            0x03be,
            0x03c5,
            0x03b6,
            0x2190,
            0x2191,
            0x2192,
            0x2193,
            0x007f,
    };

    const uint16_t uc_IsoLatin1[] =
        {
            0x00a0,
            0x00a1,
            0x00a2,
            0x00a3,
            0x00a4,
            0x00a5,
            0x00a6,
            0x00a7,
            0x00a8,
            0x00a9,
            0x00aa,
            0x00ab,
            0x00ac,
            0x00ad,
            0x00ae,
            0x00af,
            0x00b0,
            0x00b1,
            0x00b2,
            0x00b3,
            0x00b4,
            0x00b5,
            0x00b6,
            0x00b7,
            0x00b8,
            0x00b9,
            0x00ba,
            0x00bb,
            0x00bc,
            0x00bd,
            0x00be,
            0x00bf,

            0x00c0,
            0x00c1,
            0x00c2,
            0x00c3,
            0x00c4,
            0x00c5,
            0x00c6,
            0x00c7,
            0x00c8,
            0x00c9,
            0x00ca,
            0x00cb,
            0x00cc,
            0x00cd,
            0x00ce,
            0x00cf,
            0x00d0,
            0x00d1,
            0x00d2,
            0x00d3,
            0x00d4,
            0x00d5,
            0x00d6,
            0x00d7,
            0x00d8,
            0x00d9,
            0x00da,
            0x00db,
            0x00dc,
            0x00dd,
            0x00de,
            0x00df,

            0x00e0,
            0x00e1,
            0x00e2,
            0x00e3,
            0x00e4,
            0x00e5,
            0x00e6,
            0x00e7,
            0x00e8,
            0x00e9,
            0x00ea,
            0x00eb,
            0x00ec,
            0x00ed,
            0x00ee,
            0x00ef,
            0x00f0,
            0x00f1,
            0x00f2,
            0x00f3,
            0x00f4,
            0x00f5,
            0x00f6,
            0x00f7,
            0x00f8,
            0x00f9,
            0x00fa,
            0x00fb,
            0x00fc,
            0x00fd,
            0x00fe,
            0x00ff,
    };

    const uint16_t uc_IsoUK[] =
        {
            0x0020,
            0x0021,
            0x0022,
            0x00a3,
            0x0024,
            0x0025,
            0x0026,
            0x0027,
            0x0028,
            0x0029,
            0x002a,
            0x002b,
            0x002c,
            0x002d,
            0x002e,
            0x002f,
            0x0030,
            0x0031,
            0x0032,
            0x0033,
            0x0034,
            0x0035,
            0x0036,
            0x0037,
            0x0038,
            0x0039,
            0x003a,
            0x003b,
            0x003c,
            0x003d,
            0x003e,
            0x003f,

            0x0040,
            0x0041,
            0x0042,
            0x0043,
            0x0044,
            0x0045,
            0x0046,
            0x0047,
            0x0048,
            0x0049,
            0x004a,
            0x004b,
            0x004c,
            0x004d,
            0x004e,
            0x004f,
            0x0050,
            0x0051,
            0x0052,
            0x0053,
            0x0054,
            0x0055,
            0x0056,
            0x0057,
            0x0058,
            0x0059,
            0x005a,
            0x005b,
            0x005c,
            0x005d,
            0x005e,
            0x005f,

            0x0060,
            0x0061,
            0x0062,
            0x0063,
            0x0064,
            0x0065,
            0x0066,
            0x0067,
            0x0068,
            0x0069,
            0x006a,
            0x006b,
            0x006c,
            0x006d,
            0x006e,
            0x006f,
            0x0070,
            0x0071,
            0x0072,
            0x0073,
            0x0074,
            0x0075,
            0x0076,
            0x0077,
            0x0078,
            0x0079,
            0x007a,
            0x007b,
            0x007c,
            0x007d,
            0x007e,
            0x007f,
    };

}

const uint16_t* Vterm::charCodes[] =
    {

        nullptr,
        uc_DecSpec,
        uc_DecSuppl,
        uc_DecSuppl,
        uc_DecTechn,
        uc_IsoLatin1,
        uc_IsoUK};

uint32_t Vterm::translateCharset(Charset charset, unsigned char ch) const {
    if (charset <= Charset::IsoUK)
        return charCodes[static_cast<uint8_t>(charset)][ch - 32];
    if (!nationalReplacementMode) return ch;

    const auto lookup = [ch](const std::pair<uint8_t, uint16_t>* table,
                             size_t size) -> uint32_t {
        for (size_t index = 0; index < size; ++index)
            if (table[index].first == ch) return table[index].second;
        return ch;
    };
#define NRC_TABLE(name, ...) \
    static const std::pair<uint8_t, uint16_t> name[] = {__VA_ARGS__}
    NRC_TABLE(dutch, {'#', 0x00a3}, {'@', 0x00be}, {'[', 0x0133},
              {'\\', 0x00bd}, {']', 0x007c}, {'{', 0x00a8},
              {'|', 0x0192}, {'}', 0x00bc}, {'~', 0x00b4});
    NRC_TABLE(finnish, {'[', 0x00c4}, {'\\', 0x00d6}, {']', 0x00c5},
              {'^', 0x00dc}, {'`', 0x00e9}, {'{', 0x00e4},
              {'|', 0x00f6}, {'}', 0x00e5}, {'~', 0x00fc});
    NRC_TABLE(french, {'#', 0x00a3}, {'@', 0x00e0}, {'[', 0x00b0},
              {'\\', 0x00e7}, {']', 0x00a7}, {'{', 0x00e9},
              {'|', 0x00f9}, {'}', 0x00e8}, {'~', 0x00a8});
    NRC_TABLE(frenchCanadian, {'@', 0x00e0}, {'[', 0x00e2},
              {'\\', 0x00e7}, {']', 0x00ea}, {'^', 0x00ee},
              {'`', 0x00f4}, {'{', 0x00e9}, {'|', 0x00f9},
              {'}', 0x00e8}, {'~', 0x00fb});
    NRC_TABLE(german, {'@', 0x00a7}, {'[', 0x00c4}, {'\\', 0x00d6},
              {']', 0x00dc}, {'{', 0x00e4}, {'|', 0x00f6},
              {'}', 0x00fc}, {'~', 0x00df});
    NRC_TABLE(italian, {'#', 0x00a3}, {'@', 0x00a7}, {'[', 0x00b0},
              {'\\', 0x00e7}, {']', 0x00e9}, {'`', 0x00f9},
              {'{', 0x00e0}, {'|', 0x00f2}, {'}', 0x00e8},
              {'~', 0x00ec});
    NRC_TABLE(norwegian, {'@', 0x00c4}, {'[', 0x00c6}, {'\\', 0x00d8},
              {']', 0x00c5}, {'^', 0x00dc}, {'`', 0x00e4},
              {'{', 0x00e6}, {'|', 0x00f8}, {'}', 0x00e5},
              {'~', 0x00fc});
    NRC_TABLE(portuguese, {'[', 0x00c3}, {'\\', 0x00c7}, {']', 0x00d5},
              {'{', 0x00e3}, {'|', 0x00e7}, {'}', 0x00f5});
    NRC_TABLE(spanish, {'#', 0x00a3}, {'@', 0x00a7}, {'[', 0x00a1},
              {'\\', 0x00d1}, {']', 0x00bf}, {'{', 0x00b0},
              {'|', 0x00f1}, {'}', 0x00e7});
    NRC_TABLE(swedish, {'@', 0x00c9}, {'[', 0x00c4}, {'\\', 0x00d6},
              {']', 0x00c5}, {'^', 0x00dc}, {'`', 0x00e9},
              {'{', 0x00e4}, {'|', 0x00f6}, {'}', 0x00e5},
              {'~', 0x00fc});
    NRC_TABLE(swiss, {'#', 0x00f9}, {'@', 0x00e0}, {'[', 0x00e9},
              {'\\', 0x00e7}, {']', 0x00ea}, {'^', 0x00ee},
              {'_', 0x00e8}, {'`', 0x00f4}, {'{', 0x00e4},
              {'|', 0x00f6}, {'}', 0x00fc}, {'~', 0x00fb});
    NRC_TABLE(serboCroatian, {'@', 0x017d}, {'[', 0x0160},
              {'\\', 0x0110}, {']', 0x0106}, {'^', 0x010c},
              {'`', 0x017e}, {'{', 0x0161}, {'|', 0x0111},
              {'}', 0x0107}, {'~', 0x010d});
    NRC_TABLE(turkish, {'&', 0x011f}, {'@', 0x0130}, {'[', 0x015e},
              {'\\', 0x00d6}, {']', 0x00c7}, {'^', 0x00dc},
              {'`', 0x011e}, {'{', 0x015f}, {'|', 0x00f6},
              {'}', 0x00e7}, {'~', 0x00fc});
#undef NRC_TABLE

#define LOOKUP(name) return lookup(name, sizeof(name) / sizeof(name[0]))
    switch (charset) {
        case Charset::NrcDutch: LOOKUP(dutch);
        case Charset::NrcFinnish: LOOKUP(finnish);
        case Charset::NrcFrench: LOOKUP(french);
        case Charset::NrcFrenchCanadian: LOOKUP(frenchCanadian);
        case Charset::NrcGerman: LOOKUP(german);
        case Charset::NrcItalian: LOOKUP(italian);
        case Charset::NrcNorwegianDanish: LOOKUP(norwegian);
        case Charset::NrcPortuguese: LOOKUP(portuguese);
        case Charset::NrcSpanish: LOOKUP(spanish);
        case Charset::NrcSwedish: LOOKUP(swedish);
        case Charset::NrcSwiss: LOOKUP(swiss);
        case Charset::NrcSerboCroatian: LOOKUP(serboCroatian);
        case Charset::NrcTurkish: LOOKUP(turkish);
        case Charset::NrcGreek: {
            static const uint16_t greek[] = {
                0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397,
                0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x03a7,
                0x039f, 0x03a0, 0x03a1, 0x03a3, 0x03a4, 0x03a5, 0x03a6,
                0x039e, 0x03a8, 0x03a9};
            return ch >= 'a' && ch <= 'x' ? greek[ch - 'a'] : ch;
        }
        case Charset::NrcHebrew:
            return ch >= '`' && ch <= 'z' ? 0x05d0 + ch - '`' : ch;
        case Charset::NrcRussian: {
            static const uint16_t russian[] = {
                0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424,
                0x0413, 0x0425, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c,
                0x041d, 0x041e, 0x041f, 0x042f, 0x0420, 0x0421, 0x0422,
                0x0423, 0x0416, 0x0412, 0x042c, 0x042b, 0x0417, 0x0428,
                0x042d, 0x0429, 0x0427};
            return ch >= '`' && ch <= '~' ? russian[ch - '`'] : ch;
        }
        default: return ch;
    }
#undef LOOKUP
}

Vterm::Vterm(uint16_t glyphPx_, uint16_t glyphPy_,
             uint16_t winPx_, uint16_t winPy_,
             int ptyFd_)
    : winPx(winPx_)
    , winPy(winPy_)
    , nCols((winPx - 2 * opts.border) / glyphPx_)
    , nRows((winPy - 2 * opts.border) / glyphPy_)
    , glyphPx(glyphPx_)
    , glyphPy(glyphPy_)
    , ptyFd(ptyFd_)
    , onPtyRead([this](uint8_t* buffer, size_t size) {
        return read(ptyFd, buffer, size);
    })
    , onPtyWrite([this](const uint8_t* buffer, size_t size) {
        return write(ptyFd, buffer, size);
    })
    , onRefresh([](const Frame&) { return true; })
    , onOsc([](int cmd, const std::string& arg) {
        logU << "OSC: '" << cmd << ";" << arg << "'" << std::endl;
    })
    , onBell([]() {
        logI << "* Bell *" << std::endl;
    })
    , onPrinter([](const std::string&) {})
    , onLed([](uint8_t) {})
    , onNotification([](const std::string&, const std::string&,
                        const std::string&, bool) {})
    , onProgress([](uint32_t, uint32_t) {})
    , onWindowOps([](uint32_t, uint32_t, uint32_t) {})
    , onWindowInfo([this]() {
        WindowInfo info;
        info.pixelWidth = winPx;
        info.pixelHeight = winPy;
        info.screenPixelWidth = winPx;
        info.screenPixelHeight = winPy;
        return info;
    })
    , frame_pri(winPx, winPy, nCols, nRows, marginTop, marginBottom,
                opts.saveLines)
    , cf(&frame_pri)
    , utf8dec([this]() {
        placeGraphicChar();
    })
    , nColsEff(nCols)
    , hMargin(0)
{
    const int ptyFlags = fcntl(ptyFd, F_GETFL, 0);
    if (ptyFlags < 0 || fcntl(ptyFd, F_SETFL, ptyFlags | O_NONBLOCK) < 0) {
        SYS_ERROR("cannot make PTY nonblocking");
    }
    makePalette256(palette256);
    std::copy(std::begin(palette256), std::end(palette256),
              std::begin(originalPalette256));
    defaultFgColor = opts.fg;
    defaultBgColor = opts.bg;
    cursorColor = opts.cr;
    selectionFgColor = opts.fg;
    selectionBgColor = opts.bg;
    initialModifyKeyResources[0] = 0;
    initialModifyKeyResources[1] = 2;
    initialModifyKeyResources[2] = 2;
    initialModifyKeyResources[3] = 0;
    initialModifyKeyResources[4] = opts.modifyOtherKeys;
    initialModifyKeyResources[6] = 0;
    initialModifyKeyResources[7] = 0;
    windowTitle = opts.title;
    iconTitle = opts.title;

    defaultFgPalIx = -1;
    defaultBgPalIx = -1;
    fgPalIx = defaultFgPalIx;
    bgPalIx = defaultBgPalIx;

    resetTerminal();
}

void Vterm::setRefreshHandler(const RefreshHandlerFn& onRefresh_) {
    onRefresh = onRefresh_;
}

void Vterm::setPtyReadHandler(const PtyReadHandlerFn& handler) {
    onPtyRead = handler;
}

void Vterm::setPtyWriteHandler(const PtyWriteHandlerFn& handler) {
    onPtyWrite = handler;
}

bool Vterm::servicePty(bool readable, bool writable) {
    // Bytes already queued by the frontend precede replies generated while
    // parsing newly readable PTY input.
    if (writable) {
        flushPtyOutput();
    }
    return readable && readPty();
}

void Vterm::setOscHandler(const OscHandlerFn& onOsc_) {
    haveOscHandler = true;
    onOsc = onOsc_;
}

void Vterm::setBellHandler(const BellHandlerFn& onBell_) {
    onBell = onBell_;
}

void Vterm::setPrinterHandler(const PrinterHandlerFn& handler) {
    onPrinter = handler;
}

void Vterm::setLedHandler(const LedHandlerFn& handler) {
    onLed = handler;
}

void Vterm::setNotificationHandler(const NotificationHandlerFn& handler) {
    onNotification = handler;
}

void Vterm::setProgressHandler(const ProgressHandlerFn& handler) {
    onProgress = handler;
}

void Vterm::setWindowOpsHandler(const WindowOpsHandlerFn& handler) {
    onWindowOps = handler;
}

void Vterm::setWindowInfoHandler(const WindowInfoHandlerFn& handler) {
    onWindowInfo = handler;
}

void Vterm::resize(uint16_t winPx_, uint16_t winPy_) {
    if (winPx == winPx_ && winPy == winPy_) {
        return;
    }
    winPx = winPx_;
    winPy = winPy_;

    uint16_t nCols_ = std::max(1, (winPx - 2 * opts.border) / glyphPx);
    uint16_t nRows_ = std::max(1, (winPy - 2 * opts.border) / glyphPy);

    if (nCols == nCols_ && nRows == nRows_) {
        cf->winPx = winPx;
        cf->winPy = winPy;
        if (inBandResizeMode) reportInBandResize();
        return;
    }

    hideCursor();

    if (nRows_ < posY + 1) {
        // Preserve every row above the cursor that still fits.  Scrolling
        // by the full height delta needlessly discards additional rows
        // whenever the cursor is not on the old bottom row.
        const uint16_t nScroll = posY + 1 - nRows_;
        cf->scrollUp(0, nRows, nScroll);
        posY -= nScroll;
    }

    // Both buffers have real history and must obey the same resize contract.
    // Keeping the inactive alternate allocation also lets mode 47 restore it
    // after a primary-screen resize instead of dereferencing freed storage.
    cf->resize(winPx, winPy, nCols_, nRows_, marginTop, marginBottom);

    if (nRows < nRows_) {
        const int nScroll = std::min(
            nRows_ - nRows, static_cast<int>(cf->getHistoryRows()));
        cf->restoreHistory(nScroll);
        posY += nScroll;
    }
    nCols = nCols_;
    nRows = nRows_;

    // Frame::resize resets the vertical scrolling region.  Reset the
    // horizontal region to the resized page as well; retaining a clipped
    // right edge made subsequent growth keep a stale narrow region.
    nColsEff = nCols;
    hMargin = 0;
    normalizeCursorPos();
    showCursor();

    pty_resize(ptyFd, nCols, nRows);
    if (inBandResizeMode) reportInBandResize();
}

std::string
Vterm::getLocalEcho(const unsigned char* const begin,
                    const unsigned char* const end) {
    std::ostringstream oss;
    for (const unsigned char* p = begin; p < end; ++p) {
        if (*p == '\r' || *p >= ' ') {
            oss << *p;
        } else {
            oss << '^' << (char)(*p + 0x40);
        }
    }
    return oss.str();
}

int Vterm::writePty(VtKey key, VtModifier modifiers_, bool userInput) {
#ifdef DEBUG
    if (key == VtKey::Print) {
        debugKey();
        return 0;
    }
#endif
    const auto userDefined = userDefinedKeys.find(key);
    if (userDefined != userDefinedKeys.end()) {
        return writePty(
            reinterpret_cast<const uint8_t*>(userDefined->second.data()),
            userDefined->second.size(), userInput);
    }
    modifiers = modifiers_;
    const auto& spec = getInputSpec(key);
    if (modifiers == VtModifier::none) {
        return writePty((const uint8_t*)spec.input, spec.getLength(),
                        userInput);
    } else {
        static uint8_t buf[32];
        int k = 0;
        const char* end = spec.input + spec.getLength();
        for (const char* p = spec.input; p != end; ++p) {
            if (*p == *MC) {
                buf[k++] = '0' + getModifierCode(modifiers);
            } else {
                buf[k++] = *p;
            }
        }
        buf[k] = '\0';
        return writePty(buf, k, userInput);
    }
}

int Vterm::writePty(uint8_t ch, VtModifier modifiers, bool userInput) {
    using VM = VtModifier;

    auto uch = (unsigned char*)&ch;
    logT << "pty write (mod=" << (int)modifiers << "): "
         << dumpBuffer(uch, uch + 1);

    const auto& mod2_encode =
        [&](uint8_t ch) {
        const char* exempt = "!#$%&*()-+=?.,:;<>'\"";
        auto x = const_cast<char*>(exempt);

        while (*x) {
            if (ch == *x++) {
                return (modifiers & VM::control_alt) != VM::none;
            }
        }

        return modifiers != VM::none;
    };

    if (eightBitInput && (modifiers & VM::alt) != VM::none) {
        ch |= 0x80;
        return writePty(&ch, 1, userInput);
    } else if ((modifyOtherKeys == 2 && mod2_encode(ch)) ||
        (modifyOtherKeys == 1 && (modifiers & VM::control) != VM::none &&
         ch > ' ')) {
        if (ch < ' ' && (modifiers & VM::control) != VM::none) {
            const char* ctrlmap = ((modifiers & VM::shift) != VM::none)
                                      ? "@ABCDEFGHIJKLMNOPQRSTUVWXYZ{|}^/"
                                      : " abcdefghijklmnopqrstuvwxyz[\\]^/";
            ch = ctrlmap[ch];
        }

        uint8_t wbuf[16] = {'\x1b', '[', '2', '7', ';', '_', ';'};
        wbuf[5] = '0' + getModifierCode(modifiers);
        uint8_t pos = 7;

        if (ch > 99) {
            wbuf[pos] = ch / 100;
            ch -= 100 * wbuf[pos];
            wbuf[pos] += '0';
            ++pos;
        }
        if (pos > 7 || ch > 9) {
            wbuf[pos] = ch / 10;
            ch -= 10 * wbuf[pos];
            wbuf[pos] += '0';
            ++pos;
        }
        wbuf[pos++] = '0' + ch;
        wbuf[pos++] = '~';
        wbuf[pos] = '\0';

        return writePty(wbuf, pos, userInput);
    } else if ((modifiers & VM::alt) != VM::none) {
        if (altSendsEscape) {
            static uint8_t wbuf[2] = {'\x1b', '\0'};
            wbuf[1] = ch;
            return writePty(wbuf, 2, userInput);
        } else {
            std::vector<char> utf8_out;
            auto sinkFn = [&](char ch)
                                  {
                utf8_out.push_back(ch);
            };
            Utf8Encoder::pushUnicode(ch | 0x80, sinkFn);
            return writePty((const uint8_t*)utf8_out.data(),
                            utf8_out.size(), userInput);
        }
    } else {
        return writePty(uch, 1, userInput);
    }
}

int Vterm::writeKittyKey(VtKey key, uint16_t modifiers,
                         KeyEventType event) {
    const KittyKeySpec spec = kittyKeySpec(key);
    if (!spec.code) {
        return 0;
    }

    if (isKittyRecoveryKey(key) &&
        !(getKittyKeyboardFlags() & 0x08)) {
        if (event == KeyEventType::Release) return 0;
        return writePty(key, kittyToLegacyModifiers(modifiers), true);
    }

    if (isKittyModifierKey(key) &&
        !(getKittyKeyboardFlags() & 0x08)) {
        return 0;
    }

    if (event == KeyEventType::Release &&
        !(getKittyKeyboardFlags() & 0x02)) {
        return 0;
    }

    std::ostringstream sequence;
    sequence << "\x1b[" << spec.code << ';' << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << ':' << static_cast<unsigned>(event);
    }
    if ((getKittyKeyboardFlags() & 0x10) &&
        event != KeyEventType::Release &&
        isKittyRecoveryKey(key) && validKittyAssociatedText(spec.code)) {
        sequence << ';' << spec.code;
    }
    sequence << spec.final;
    const std::string encoded = sequence.str();
    return writePty(reinterpret_cast<const uint8_t*>(encoded.data()),
                    encoded.size(), true);
}

int Vterm::writeKittyKey(uint32_t key, uint32_t shiftedKey,
                         uint32_t baseLayoutKey, uint16_t modifiers,
                         KeyEventType event) {
    if (!key || (event == KeyEventType::Release &&
                 !(getKittyKeyboardFlags() & 0x02))) {
        return 0;
    }

    std::ostringstream sequence;
    sequence << "\x1b[" << key;
    if (getKittyKeyboardFlags() & 0x04) {
        const uint32_t alternateShifted = shiftedKey != key ? shiftedKey : 0;
        const uint32_t alternateBase = baseLayoutKey != key ? baseLayoutKey : 0;
        if (alternateShifted) {
            sequence << ':' << alternateShifted;
            if (alternateBase) {
                sequence << ':' << alternateBase;
            }
        } else if (alternateBase) {
            sequence << "::" << alternateBase;
        }
    }
    sequence << ';' << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << ':' << static_cast<unsigned>(event);
    }
    if ((getKittyKeyboardFlags() & 0x10) &&
        event != KeyEventType::Release) {
        const uint32_t text = (modifiers & 1) && shiftedKey
                                  ? shiftedKey
                                  : key;
        if (validKittyAssociatedText(text)) sequence << ';' << text;
    }
    sequence << 'u';
    const std::string encoded = sequence.str();
    return writePty(reinterpret_cast<const uint8_t*>(encoded.data()),
                    encoded.size(), true);
}

int Vterm::writePty(const char* cstr, bool userInput) {
    auto ucstr = (unsigned char*)cstr;
    return writePty(ucstr, strlen(cstr), userInput);
}

void Vterm::writeCsiResponse(const std::string& payload) {
    const std::string response =
        (send8BitControls ? std::string("\x9b") : std::string("\x1b[")) +
        payload;
    writePty(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void Vterm::writeDcsResponse(const std::string& payload) {
    const std::string response =
        (send8BitControls ? std::string("\x90") : std::string("\x1bP")) +
        payload +
        (send8BitControls ? std::string("\x9c") : std::string("\x1b\\"));
    writePty(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void Vterm::writeOscResponse(const std::string& payload) {
    const std::string response =
        (send8BitControls ? std::string("\x9d") : std::string("\x1b]")) +
        payload +
        (send8BitControls ? std::string("\x9c") : std::string("\x1b\\"));
    writePty(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

int Vterm::writePty(const uint8_t* ucstr, size_t len, bool userInput) {
    if (userInput && keyboardLocked) {
        logT << "pty write: discarding due to keyboard lock (DECKAM): "
             << dumpBuffer(ucstr, ucstr + len);
        return len;
    }

    if (userInput && cf->pageToBottom()) {
        redraw();
    }

    logT << "pty write: " << dumpBuffer(ucstr, ucstr + len);
    if (userInput && localEcho) {
        processInput(getLocalEcho(ucstr, ucstr + len));
    }
    if (ptyOutputOffset == ptyOutput.size()) {
        ptyOutput.clear();
        ptyOutputOffset = 0;
    }
    ptyOutput.insert(ptyOutput.end(), ucstr, ucstr + len);
    flushPtyOutput();
    return len;
}

bool Vterm::flushPtyOutput() {
    while (ptyOutputOffset < ptyOutput.size()) {
        const ssize_t count = onPtyWrite(
            ptyOutput.data() + ptyOutputOffset,
            ptyOutput.size() - ptyOutputOffset);
        if (count > 0) {
            ptyOutputOffset += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;
        }
        if (count < 0) {
            SYS_WARN("pty write");
        }
        return false;
    }
    ptyOutput.clear();
    ptyOutputOffset = 0;
    return true;
}

using Key = VtKey;
using Mod = VtModifier;

Vterm::InputSpecTable*
Vterm::getInputSpecTable() {
    static InputSpecTable ist[] =
        {
            {[this]() {
        return (autoNewlineMode == true);
    },
             is_ReturnKey_ANL},

            {[this]() {
        return ((modifiers & Mod::alt) != Mod::none &&
                bkspSendsDel == false);
    },
             is_Alt_BackspaceKey_BkSp},

            {[this]() {
        return (modifyOtherKeys == 2 &&
                modifiers != Mod::none);
    },
             is_modOtherKeys2},

            {[this]() {
        return (modifyOtherKeys > 0 && modifiers != Mod::none);
    },
             is_modOtherKeys},

            {[this]() {
        return (modifyOtherKeys > 0 &&
                (modifiers & Mod::control) != Mod::none);
    },
             is_Control_modOtherKeys},

            {[this]() {
        return (altSendsEscape &&
                (modifiers & Mod::control_alt) == Mod::control_alt);
    },
             is_ControlAlt_altSendsEscape},

            {[this]() {
        return (altSendsEscape &&
                (modifiers & Mod::alt) != Mod::none);
    },
             is_Alt_altSendsEscape},

            {[this]() {
        return ((modifiers & Mod::alt) != Mod::none);
    },
             is_Alt},

            {[this]() {
        return ((modifiers & Mod::control) != Mod::none);
    },
             is_Control},

            {[this]() {
        return ((modifiers & Mod::shift) != Mod::none);
    },
             is_Shift},

            {[this]() {
        return (bkspSendsDel == false);
    },
             is_BackspaceKey_BkSp},

            {[this]() {
        return (compatLevel == CompatibilityLevel::VT52 &&
                keypadMode == KeypadMode::Application);
    },
             is_VT52_KeypadKeys},
            {[this]() {
        return (compatLevel == CompatibilityLevel::VT52);
    },
             is_VT52_CursorKeys},
            {[this]() {
        return (compatLevel == CompatibilityLevel::VT52);
    },
             is_VT52_FunctionKeys},

            {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[3] != 0 &&
                keypadMode == KeypadMode::Application);
    },
             is_Mod_Appl_KeypadKeys},
            {[this]() {
        return (keypadMode == KeypadMode::Application);
    },
             is_Appl_KeypadKeys},
            {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[1] != 0);
    },
             is_Mod_CursorKeys},
            {[this]() {
        return (cursorKeyMode == CursorKeyMode::Application);
    },
             is_Appl_CursorKeys},

            {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[0] != 0);
    },
             is_Mod_Ansi},
            {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[2] != 0);
    },
             is_Mod_Ansi_FunctionKeys},

            {[]() {
        return true;
    }, is_Ansi},
            {[]() {
        return true;
    }, is_Ansi_CursorKeys},
            {[]() {
        return true;
    }, is_Ansi_FunctionKeys},
            {[]() {
        return true;
    }, is_Ansi_KeypadKeys},

            {[]() {
        return true;
    }, nullptr}};
    return ist;
}

void Vterm::resetInputSpecTable() {
    for (InputSpecTable* e = getInputSpecTable(); e->specs != nullptr; ++e) {
        e->visited = false;
    }
}

const Vterm::InputSpec*
Vterm::selectInputSpecs() {
    InputSpecTable* ist = getInputSpecTable();
    for (auto e = ist; e->specs != nullptr; ++e) {
        if (!e->visited) {
            e->visited = true;
            if (e->predicate()) {
                return e->specs;
            }
        }
    }
    return nullptr;
}

const Vterm::InputSpec&
Vterm::getInputSpec(Key key) {
    static InputSpec nullSpec = {Key::NONE, ""};

    resetInputSpecTable();
    const InputSpec* specs;
    while ((specs = selectInputSpecs()) != nullptr) {
        for (int k = 0; specs[k].key != Key::NONE; ++k) {
            if (specs[k].key == key) {
                return specs[k];
            }
        }
    }

    return nullSpec;
}

#define IGNORE_SEQUENCE_ON_BAD_PARAMS         \
    case '<':                                 \
    case '=':                                 \
    case '>':                                 \
    case '?':                                 \
        setState(InputState::IgnoreSequence); \
        break

#define COLLECT_NUMERIC_PARAMS                                       \
    case '0':                                                        \
    case '1':                                                        \
    case '2':                                                        \
    case '3':                                                        \
    case '4':                                                        \
    case '5':                                                        \
    case '6':                                                        \
    case '7':                                                        \
    case '8':                                                        \
    case '9':                                                        \
        csiHadParams = true;                                         \
        csiPrefixAllowed = false;                                    \
        if (inputOps[nInputOps - 1] >                                \
            (UINT32_MAX - static_cast<uint32_t>(ch - '0')) / 10) {   \
            inputOps[nInputOps - 1] = UINT32_MAX;                    \
        } else {                                                     \
            inputOps[nInputOps - 1] *= 10;                           \
            inputOps[nInputOps - 1] += ch - '0';                     \
        }                                                            \
        break;                                                       \
    case ';':                                                        \
    case ':':                                                        \
        csiHadParams = true;                                         \
        csiPrefixAllowed = false;                                    \
        if (nInputOps < maxEscOps) {                                 \
            inputSeparators[nInputOps] = ch;                         \
            inputOps[nInputOps++] = 0;                               \
        } else {                                                     \
            logE << "inputOps full, increase maxEscOps (currently: " \
                 << maxEscOps << ")!" << std::endl;                  \
            setState(InputState::IgnoreSequence);                    \
        }                                                            \
        break

Vterm::PresentationState Vterm::capturePresentationState() const {
    return {
        cf,
        cf->getCursor(),
        cf->getSelectionForView(),
        cf->nCols,
        cf->nRows,
        cf->getViewOffset(),
        cf->getScreenReverseVideo(),
        cf->getBlinkVisible(),
        cf->getCursorBlink(),
        cf->getSelectionForeground(),
        cf->getSelectionBackground(),
        cf->getSelectionColorMask(),
    };
}

bool Vterm::presentationChanged(const PresentationState& before) const {
    if (before.frame != cf || cf->hasDamage() ||
        before.columns != cf->nCols || before.rows != cf->nRows ||
        before.viewOffset != cf->getViewOffset() ||
        before.screenReverse != cf->getScreenReverseVideo() ||
        before.blinkVisible != cf->getBlinkVisible() ||
        before.cursorBlink != cf->getCursorBlink() ||
        !(before.selectionForeground == cf->getSelectionForeground()) ||
        !(before.selectionBackground == cf->getSelectionBackground()) ||
        before.selectionColorMask != cf->getSelectionColorMask()) {
        return true;
    }
    const auto cursor = cf->getCursor();
    if (before.cursor.posX != cursor.posX ||
        before.cursor.posY != cursor.posY ||
        before.cursor.style != cursor.style ||
        !(before.cursor.color == cursor.color)) {
        return true;
    }
    const Rect selection = cf->getSelectionForView();
    return !(before.selection.tl == selection.tl) ||
           !(before.selection.br == selection.br) ||
           before.selection.rectangular != selection.rectangular;
}

void Vterm::syncPresentationCursor() {
    cf->setCursorPos(posY, posX);
    using CS = CharVdev::Cursor::Style;
    cf->setCursorStyle(
        showCursorMode ? (hasFocus ? cursorShape : CS::hollow_block)
                       : CS::hidden);
}

bool Vterm::processInput(const std::string& str) {
    return processInput((unsigned char*)str.c_str(), str.length());
}

void Vterm::feedPtyOutput(const std::string& output) {
    processInput(output);
}

void Vterm::beginCsi() {
    inputOps[0] = 0;
    inputSeparators[0] = 0;
    nInputOps = 1;
    csiHadParams = false;
    csiPrefixAllowed = true;
    csiPrivatePrefix.clear();
    csiIntermediates.clear();
    setState(InputState::CSI);
}

bool Vterm::executeC0InSequence(unsigned char ch) {
    if (ch >= 0x20 || ch == '\x18' || ch == '\x1a' || ch == '\x1b') {
        return false;
    }

    const InputState savedState = inputState;
    const size_t savedInputOps = nInputOps;
    const bool savedHadParams = csiHadParams;
    const bool savedPrefixAllowed = csiPrefixAllowed;
    const std::string savedPrivatePrefix = csiPrivatePrefix;
    const std::string savedIntermediates = csiIntermediates;
    uint32_t savedOps[maxEscOps];
    unsigned char savedSeparators[maxEscOps];
    std::copy(inputOps, inputOps + savedInputOps, savedOps);
    std::copy(inputSeparators, inputSeparators + savedInputOps,
              savedSeparators);

    switch (ch) {
        case '\a':
            onBell();
            break;
        case '\b':
            nInputOps = 1;
            inputOps[0] = 1;
            csi_CUB();
            break;
        case '\t':
            inp_HT();
            break;
        case '\n':
        case '\v':
        case '\f':
            esc_IND();
            break;
        case '\r':
            inp_CR();
            break;
        case '\x0e':
            charsetState.gl = 1;
            break;
        case '\x0f':
            charsetState.gl = 0;
            break;
        default:
            break;
    }

    inputState = savedState;
    nInputOps = savedInputOps;
    csiHadParams = savedHadParams;
    csiPrefixAllowed = savedPrefixAllowed;
    csiPrivatePrefix = savedPrivatePrefix;
    csiIntermediates = savedIntermediates;
    std::copy(savedOps, savedOps + savedInputOps, inputOps);
    std::copy(savedSeparators, savedSeparators + savedInputOps,
              inputSeparators);
    return true;
}

void Vterm::dispatchCsi(unsigned char finalByte) {
    const std::string key = csiPrivatePrefix + csiIntermediates +
                            static_cast<char>(finalByte);
    if (key == "A") csi_CUU();
    else if (key == "B") csi_CUD();
    else if (key == "C") csi_CUF();
    else if (key == "D") csi_CUB();
    else if (key == "E") csi_CNL();
    else if (key == "F") csi_CPL();
    else if (key == "G") csi_CHA();
    else if (key == "H" || key == "f") csi_CUP();
    else if (key == "I") csi_CHT();
    else if (key == "J") csi_ED();
    else if (key == "K") csi_EL();
    else if (key == "L") csi_IL();
    else if (key == "M") csi_DL();
    else if (key == "P") csi_DCH();
    else if (key == "S") csi_SU();
    else if (key == "T") {
        if (nInputOps == 5 &&
            mouseTrk.mode == MouseTrackingMode::VT200_Highlight)
            csi_XTHIMOUSE();
        else csi_SD();
    }
    else if (key == "X") csi_ECH();
    else if (key == "Z") csi_CBT();
    else if (key == "@") csi_ICH();
    else if (key == "`") csi_HPA();
    else if (key == "a") csi_HPR();
    else if (key == "b") csi_REP();
    else if (key == "c") csi_priDA();
    else if (key == "d") csi_VPA();
    else if (key == "e") csi_VPR();
    else if (key == "g") csi_TBC();
    else if (key == "h") csi_SM();
    else if (key == "l") csi_RM();
    else if (key == "m") csi_SGR();
    else if (key == "n") csi_DSR();
    else if (key == "q") csi_DECLL();
    else if (key == "i") csi_MC(false);
    else if (key == "r") csi_STBM();
    else if (key == "s") csi_SCOSC_SLRM();
    else if (key == "t") csi_XTWINOPS();
    else if (key == "u") csi_SCORC();
    else if (key == "!p") csi_DECSTR();
    else if (key == "'}") csi_DECIC();
    else if (key == "'~") csi_DECDC();
    else if (key == "'z") csi_DECELR();
    else if (key == "'{") csi_DECSLE();
    else if (key == "'|") csi_DECRQLP();
    else if (key == "'w") csi_DECEFR();
    else if (key == "\"p") csiq_DECSCL();
    else if (key == "\"q") csi_DECSCA();
    else if (key == " @") csi_ecma48_SL();
    else if (key == " A") csi_ecma48_SR();
    else if (key == " q") csi_DECSCUSR();
    else if (key == ">c") csi_secDA();
    else if (key == ">m") csi_XTMODKEYS();
    else if (key == ">u") csi_kittyKeyboardPush();
    else if (key == ">q") csi_XTVERSION();
    else if (key == "<u") csi_kittyKeyboardPop();
    else if (key == "=u") csi_kittyKeyboardSet();
    else if (key == "=c") csi_terDA();
    else if (key == "?h") csi_privSM();
    else if (key == "?l") csi_privRM();
    else if (key == "?s") csi_privSave();
    else if (key == "?r") csi_privRestore();
    else if (key == "?u") csi_kittyKeyboardQuery();
    else if (key == "?m") csi_XTQMODKEYS();
    else if (key == "?J") csi_DECSED();
    else if (key == "?K") csi_DECSEL();
    else if (key == "?i") csi_MC(true);
    else if (key == "$p") csi_DECRQM(false);
    else if (key == "$r") csi_DECCARA(false);
    else if (key == "$t") csi_DECCARA(true);
    else if (key == "$v") csi_DECCRA();
    else if (key == "$x") csi_DECFRA();
    else if (key == "$z") csi_DECERA();
    else if (key == "${") csi_DECERA(true);
    else if (key == "*y") csi_DECRQCRA();
    else if (key == "?$p") csi_DECRQM(true);
    else setState(InputState::Normal);
}

void Vterm::processCsiByte(unsigned char ch) {
    if (ch == 0x7f || executeC0InSequence(ch)) {
        return;
    }
    if (ch >= '0' && ch <= '9') {
        if (!csiIntermediates.empty()) {
            setState(InputState::IgnoreSequence);
            return;
        }
        csiHadParams = true;
        csiPrefixAllowed = false;
        if (inputOps[nInputOps - 1] >
            (UINT32_MAX - static_cast<uint32_t>(ch - '0')) / 10)
            inputOps[nInputOps - 1] = UINT32_MAX;
        else inputOps[nInputOps - 1] =
            inputOps[nInputOps - 1] * 10 + ch - '0';
        return;
    }
    if (ch == ';' || ch == ':') {
        if (!csiIntermediates.empty() || nInputOps >= maxEscOps) {
            setState(InputState::IgnoreSequence);
            return;
        }
        csiHadParams = true;
        csiPrefixAllowed = false;
        inputSeparators[nInputOps] = ch;
        inputOps[nInputOps++] = 0;
        return;
    }
    if (ch >= '<' && ch <= '?' && csiPrefixAllowed &&
        csiPrivatePrefix.empty()) {
        csiPrivatePrefix.push_back(static_cast<char>(ch));
        return;
    }
    if (ch >= 0x20 && ch <= 0x2f) {
        csiPrefixAllowed = false;
        if (csiIntermediates.size() >= 4) {
            setState(InputState::IgnoreSequence);
            return;
        }
        csiIntermediates.push_back(static_cast<char>(ch));
        return;
    }
    if (ch >= 0x40 && ch <= 0x7e) {
        dispatchCsi(ch);
        return;
    }
    setState(InputState::IgnoreSequence);
}

bool Vterm::processInput(
    const unsigned char* const input, int inputSize, bool refresh) {
    const PresentationState presentationBefore = capturePresentationState();
    lastEscBegin = 0;
    lastNormalBegin = 0;
    lastStopPos = 0;
    hideCursor();
    for (readPos = 0; readPos < inputSize; ++readPos) {
        const unsigned char& ch = input[readPos];
        if (printerControllerMode && consumePrinterControllerByte(ch)) {
            continue;
        }
        if ((ch == '\x18' || ch == '\x1a') &&
            inputState != InputState::Normal) {
            setState(InputState::Normal);
            continue;
        }
        if (ch == 0x7f) {
            continue;
        }
        if (ch == '\x1b' && inputState != InputState::Normal &&
            inputState != InputState::Escape &&
            inputState != InputState::Escape_VT52 &&
            inputState != InputState::DCS &&
            inputState != InputState::DCS_Esc &&
            inputState != InputState::OSC &&
            inputState != InputState::OSC_Esc &&
            inputState != InputState::String &&
            inputState != InputState::String_Esc) {
            setState(compatLevel == CompatibilityLevel::VT52
                         ? InputState::Escape_VT52
                         : InputState::Escape);
            inputOps[0] = 0;
            inputSeparators[0] = 0;
            nInputOps = 1;
            lastEscBegin = readPos;
            continue;
        }
        if (inputState != InputState::Normal) {
            switch (ch) {
                case 0x90:
                    argBuf.clear();
                    argBufOverflowed = false;
                    setState(InputState::DCS);
                    continue;
                case 0x98:
                case 0x9e:
                case 0x9f:
                    setState(InputState::String);
                    continue;
                case 0x9b:
                    beginCsi();
                    continue;
                case 0x9c:
                    if (inputState != InputState::DCS &&
                        inputState != InputState::OSC) {
                        setState(InputState::Normal);
                        continue;
                    }
                    break;
                case 0x9d:
                    argBuf.clear();
                    argBufOverflowed = false;
                    setState(InputState::OSC);
                    continue;
            }
        }
        switch (inputState) {
            case InputState::Normal:
                if (utf8dec.expectsContinuation() && ch >= 0x80) {
                    inputGraphicChar(ch);
                    break;
                }
                if (ch < 0x20 || ch == 0x7f ||
                    (ch >= 0x80 && ch <= 0x9f)) {
                    resetGraphemeInput();
                }
                switch (ch) {
                    case '\x00':
                    case '\x7f':
                        break;
                    case '\x1b':
                        setState(compatLevel == CompatibilityLevel::VT52
                                     ? InputState::Escape_VT52
                                     : InputState::Escape);
                        inputOps[0] = 0;
                        inputSeparators[0] = 0;
                        nInputOps = 1;
                        lastEscBegin = readPos;
                        break;
                    case 0x84:
                        esc_IND();
                        break;
                    case 0x85:
                        esc_NEL();
                        break;
                    case 0x88:
                        esc_HTS();
                        break;
                    case 0x8d:
                        esc_RI();
                        break;
                    case 0x8e:
                        charsetState.ss = 2;
                        break;
                    case 0x8f:
                        charsetState.ss = 3;
                        break;
                    case 0x90:
                        argBuf.clear();
                        argBufOverflowed = false;
                        setState(InputState::DCS);
                        break;
                    case 0x98:
                    case 0x9e:
                    case 0x9f:
                        setState(InputState::String);
                        break;
                    case 0x9b:
                        beginCsi();
                        break;
                    case 0x9c:
                        break;
                    case 0x9d:
                        argBuf.clear();
                        argBufOverflowed = false;
                        setState(InputState::OSC);
                        break;
                    case '\r':
                        traceNormalInput();
                        inp_CR();
                        break;
                    case '\f':
                    case '\v':
                    case '\n':
                        traceNormalInput();
                        esc_IND();
                        break;
                    case '\t':
                        traceNormalInput();
                        inp_HT();
                        break;
                    case '\b':
                        traceNormalInput();
                        csi_CUB();
                        break;
                    case '\a':
                        traceNormalInput();
                        onBell();
                        break;
                    case '\x0e':
                        traceNormalInput();
                        charsetState.gl = 1;
                        break;
                    case '\x0f':
                        traceNormalInput();
                        charsetState.gl = 0;
                        break;
                    case '\x05':
                        traceNormalInput();
                        break;
                    default:
                        inputGraphicChar(ch);
                }
                break;
            case InputState::IgnoreSequence:
                if (executeC0InSequence(ch)) {
                    break;
                } else if (ch >= '\x40' && ch <= '\x7e') {
                    setState(InputState::Normal);
                }
                break;
            case InputState::Escape_VT52:
                switch (ch) {
                    case '\x18':
                    case '\x1a':
                        setState(InputState::Normal);
                        break;
                    case '\x1b':
                        inputOps[0] = 0;
                        nInputOps = 1;
                        lastEscBegin = readPos;
                        break;
                    case '=':
                        keypadMode = KeypadMode::Application;
                        setState(InputState::Normal);
                        break;
                    case '>':
                        keypadMode = KeypadMode::Normal;
                        setState(InputState::Normal);
                        break;
                    case '<':
                        compatLevel = CompatibilityLevel::VT100;
                        setState(InputState::Normal);
                        break;
                    case 'A':
                        csi_CUU();
                        break;
                    case 'B':
                        csi_CUD();
                        break;
                    case 'C':
                        csi_CUF();
                        break;
                    case 'D':
                        csi_CUB();
                        break;
                    case 'F':
                        charsetState = CharsetState{};
                        charsetState.g[charsetState.gl] = Charset::DecSpec;
                        setState(InputState::Normal);
                        break;
                    case 'G':
                        charsetState = CharsetState{};
                        setState(InputState::Normal);
                        break;
                    case 'H':
                        csi_CUP();
                        break;
                    case 'I':
                        esc_RI();
                        break;
                    case 'J':
                        csi_ED();
                        break;
                    case 'K':
                        csi_EL();
                        break;
                    case 'Y':
                        setState(InputState::VT52_CUP_Arg1);
                        break;
                    case 'Z':
                        writePty("\x1b/Z");
                        break;
                    case 'c':
                        esc_RIS();
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::VT52_CUP_Arg1:
                inputOps[0] = input[readPos] - 31;
                setState(InputState::VT52_CUP_Arg2);
                break;
            case InputState::VT52_CUP_Arg2:
                inputOps[1] = input[readPos] - 31;
                nInputOps = 2;
                csi_CUP();
                break;
            case InputState::Escape:
                switch (ch) {
                    case '\x18':
                    case '\x1a':
                        setState(InputState::Normal);
                        break;
                    case '\x1b':
                        inputOps[0] = 0;
                        nInputOps = 1;
                        lastEscBegin = readPos;
                        break;
                    case ' ':
                        setState(InputState::Esc_SPC);
                        break;
                    case '#':
                        setState(InputState::Esc_Hash);
                        break;
                    case '%':
                        setState(InputState::Esc_Pct);
                        break;
                    case '[':
                        beginCsi();
                        break;
                    case ']':
                        argBuf.clear();
                        argBufOverflowed = false;
                        setState(InputState::OSC);
                        break;
                    case 'X':
                    case '^':
                    case '_':
                        setState(InputState::String);
                        break;
                    case '(':
                    case ')':
                    case '*':
                    case '+':
                    case '-':
                    case '.':
                    case '/':
                    case ',':
                    case '$':
                        scsDst = ch;
                        scsMod = '\0';
                        setState(InputState::SelectCharset);
                        break;
                    case 'D':
                        esc_IND();
                        break;
                    case 'M':
                        esc_RI();
                        break;
                    case 'E':
                        esc_NEL();
                        break;
                    case 'H':
                        esc_HTS();
                        break;
                    case 'N':
                        charsetState.ss = 2;
                        setState(InputState::Normal);
                        break;
                    case 'O':
                        charsetState.ss = 3;
                        setState(InputState::Normal);
                        break;
                    case 'P':
                        argBuf.clear();
                        argBufOverflowed = false;
                        setState(InputState::DCS);
                        break;
                    case 'c':
                        esc_RIS();
                        break;
                    case '6':
                        esc_BI();
                        break;
                    case '7':
                        esc_DECSC();
                        break;
                    case '8':
                        esc_DECRC();
                        break;
                    case '9':
                        esc_FI();
                        break;
                    case '=':
                        keypadMode = KeypadMode::Application;
                        setState(InputState::Normal);
                        break;
                    case '>':
                        keypadMode = KeypadMode::Normal;
                        setState(InputState::Normal);
                        break;
                    case '<':
                        compatLevel = CompatibilityLevel::VT400;
                        setState(InputState::Normal);
                        break;
                    case '~':
                        charsetState.gr = 1;
                        setState(InputState::Normal);
                        break;
                    case 'n':
                        charsetState.gl = 2;
                        setState(InputState::Normal);
                        break;
                    case '}':
                        charsetState.gr = 2;
                        setState(InputState::Normal);
                        break;
                    case 'o':
                        charsetState.gl = 3;
                        setState(InputState::Normal);
                        break;
                    case '|':
                        charsetState.gr = 3;
                        setState(InputState::Normal);
                        break;
                    case '\\':
                        setState(InputState::Normal);
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::Esc_SPC:
                switch (ch) {
                    case 'F':
                        logU << "S7C1T: Send 7-bit controls" << std::endl;
                        send8BitControls = false;
                        setState(InputState::Normal);
                        break;
                    case 'G':
                        logU << "S8C1T: Send 8-bit controls" << std::endl;
                        send8BitControls = true;
                        setState(InputState::Normal);
                        break;
                    case 'L':
                        logU << "Set ANSI conformance level 1" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case 'M':
                        logU << "Set ANSI conformance level 2" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case 'N':
                        logU << "Set ANSI conformance level 3" << std::endl;
                        setState(InputState::Normal);
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::Esc_Hash:
                switch (ch) {
                    case '3':
                        setLineAttribute(1);
                        break;
                    case '4':
                        setLineAttribute(2);
                        break;
                    case '5':
                        setLineAttribute(0);
                        break;
                    case '6':
                        setLineAttribute(3);
                        break;
                    case '8':
                        esch_DECALN();
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::Esc_Pct:
                switch (ch) {
                    case '@':
                        logT << "Select charset: default (ISO-8859-1)" << std::endl;
                        charsetState = CharsetState{};
                        charsetState.g[charsetState.gr] = Charset::IsoLatin1;
                        setState(InputState::Normal);
                        break;
                    case 'G':
                        logT << "Select charset: UTF-8" << std::endl;
                        charsetState = CharsetState{};
                        setState(InputState::Normal);
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::SelectCharset:
                if (ch < 0x30) {
                    scsMod = ch;
                } else {
                    esc_DCS(ch);
                }
                break;
            case InputState::CSI:
                processCsiByte(ch);
                break;
            case InputState::DCS:
                switch (ch) {
                    case 0x9c:
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_DCS();
                        }
                        break;
                    case '\x1b':
                        setState(InputState::DCS_Esc);
                        break;
                    case '\x7f':
                        break;
                    default:
                        if (executeC0InSequence(ch)) {
                            break;
                        } else if (argBuf.size() < 4095) {
                            argBuf.push_back(ch);
                        } else if (!argBufOverflowed) {
                            logE << "DCS argument string overflow" << std::endl;
                            argBufOverflowed = true;
                        }
                        break;
                }
                break;
            case InputState::DCS_Esc:
                switch (ch) {
                    case '\\':
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_DCS();
                        }
                        break;
                    case '\x1b':
                        if (!argBufOverflowed && argBuf.size() < 4095) {
                            argBuf.push_back('\x1b');
                        } else {
                            argBufOverflowed = true;
                        }
                        break;
                    default:
                        if (!argBufOverflowed && argBuf.size() <= 4093) {
                            argBuf.push_back('\x1b');
                            argBuf.push_back(ch);
                        } else {
                            argBufOverflowed = true;
                        }
                        setState(InputState::DCS);
                        break;
                }
                break;
            case InputState::OSC:
                switch (ch) {
                    case 0x9c:
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_OSC();
                        }
                        break;
                    case '\a':
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_OSC();
                        }
                        break;
                    case '\x1b':
                        setState(InputState::OSC_Esc);
                        break;
                    case '\x7f':
                        break;
                    default:
                        if (executeC0InSequence(ch)) {
                            break;
                        } else if (argBuf.size() < maxOscBytes) {
                            argBuf.push_back(ch);
                        } else if (!argBufOverflowed) {
                            logE << "OSC argument string overflow" << std::endl;
                            argBufOverflowed = true;
                        }
                        break;
                }
                break;
            case InputState::OSC_Esc:
                switch (ch) {
                    case '\\':
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_OSC();
                        }
                        break;
                    case '\x1b':
                        if (!argBufOverflowed &&
                            argBuf.size() < maxOscBytes) {
                            argBuf.push_back('\x1b');
                        } else {
                            argBufOverflowed = true;
                        }
                        break;
                    default:
                        if (!argBufOverflowed &&
                            argBuf.size() <= maxOscBytes - 2) {
                            argBuf.push_back('\x1b');
                            argBuf.push_back(ch);
                        } else {
                            argBufOverflowed = true;
                        }
                        setState(InputState::OSC);
                        break;
                }
                break;
            case InputState::String:
                if (executeC0InSequence(ch)) {
                    break;
                } else if (ch == 0x9c) {
                    setState(InputState::Normal);
                } else if (ch == '\x1b') {
                    setState(InputState::String_Esc);
                }
                break;
            case InputState::String_Esc:
                if (ch == '\\') {
                    setState(InputState::Normal);
                } else if (ch != '\x1b') {
                    setState(InputState::String);
                }
                break;
        }
    }
    traceNormalInput();
    syncPresentationCursor();
    const bool changed = presentationChanged(presentationBefore);
    if (refresh && changed) {
        redraw();
    }
    return changed;
}

void Vterm::setHyperlink(const std::string& parametersAndUri) {
    const size_t separator = parametersAndUri.find(';');
    if (separator == std::string::npos) {
        logW << "Malformed OSC 8 argument" << std::endl;
        return;
    }

    const std::string parameters = parametersAndUri.substr(0, separator);
    const std::string uri = parametersAndUri.substr(separator + 1);
    if (uri.empty()) {
        activeHyperlink = 0;
        return;
    }

    if (hyperlinks.size() >= 256 && hyperlinks.size() % 256 == 0) {
        pruneHyperlinks();
    }

    std::string identity = "uri=" + uri;
    size_t begin = 0;
    while (begin <= parameters.size()) {
        const size_t end = parameters.find(':', begin);
        const std::string parameter = parameters.substr(begin, end - begin);
        if (parameter.compare(0, 3, "id=") == 0) {
            identity = parameter + ";uri=" + uri;
            break;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }

    const auto known = hyperlinkIds.find(identity);
    if (known != hyperlinkIds.end()) {
        activeHyperlink = known->second;
        return;
    }

    if (nextHyperlink == 0) {
        logW << "OSC 8 hyperlink identifier space exhausted" << std::endl;
        activeHyperlink = 0;
        return;
    }

    activeHyperlink = nextHyperlink++;
    hyperlinkIds.emplace(identity, activeHyperlink);
    hyperlinks.emplace(activeHyperlink, uri);
}

void Vterm::pruneHyperlinks() {
    std::set<uint32_t> used;
    frame_pri.collectHyperlinkIds(used);
    frame_alt.collectHyperlinkIds(used);
    if (activeHyperlink != 0) used.insert(activeHyperlink);

    for (auto it = hyperlinks.begin(); it != hyperlinks.end();) {
        if (used.count(it->first)) {
            ++it;
            continue;
        }
        const uint32_t id = it->first;
        it = hyperlinks.erase(it);
        for (auto key = hyperlinkIds.begin(); key != hyperlinkIds.end();) {
            if (key->second == id) key = hyperlinkIds.erase(key);
            else ++key;
        }
    }
}

std::string Vterm::getHyperlink(int pX, int pY) const {
    if (pX < opts.border || pY < opts.border ||
        pX >= winPx - opts.border || pY >= winPy - opts.border) {
        return {};
    }

    const uint16_t column = (pX - opts.border) / glyphPx;
    const uint16_t row = (pY - opts.border) / glyphPy;
    if (column >= cf->nCols || row >= cf->nRows) {
        return {};
    }

    const uint32_t id = cf->getViewCell(row, column).hyperlink;
    const auto link = hyperlinks.find(id);
    return link == hyperlinks.end() ? std::string{} : link->second;
}

void Vterm::selectStart(int pX, int pY, bool cycleSnapTo) {
    logT << "selectStart (" << pX << "," << pY
         << "), cycleSnapTo=" << cycleSnapTo << std::endl;

    if (cycleSnapTo) {
        selectExtend(pX, pY, true);
        return;
    }

    pX = std::min(std::max(0, pX - opts.border), winPx - 2 * opts.border);
    pY = std::min(std::max(0, pY - opts.border), winPy - 2 * opts.border);
    Point pt = cf->getLogicalPoint(Point(pX / glyphPx, pY / glyphPy));

    Rect& selection = cf->getSelection();
    cf->setSelectSnapTo(Frame::SelectSnapTo::Char);
    selection.tl = pt;
    selection.br = pt;
    selectUpdatesTop = false;
    selectUpdatesLeft = false;

    hideCursor();
    redraw();
}

void Vterm::selectExtend(int pX, int pY, bool cycleSnapTo) {
    logT << "selectExtend (" << pX << "," << pY
         << "), cycleSnapTo=" << cycleSnapTo << std::endl;

    pX = std::min(std::max(0, pX - opts.border), winPx - 2 * opts.border);
    pY = std::min(std::max(0, pY - opts.border), winPy - 2 * opts.border);
    Point pt = cf->getLogicalPoint(Point(pX / glyphPx, pY / glyphPy));

    Rect& selection = cf->getSelection();
    if (cycleSnapTo) {
        cf->cycleSelectSnapTo();
    }

    if (selection.rectangular) {
        selectUpdatesLeft = pt.x < selection.mid().x;
        selectUpdatesTop = pt.y < selection.mid().y;
    } else {
        selectUpdatesLeft = selectUpdatesTop = pt < selection.mid();
    }

    if (selectUpdatesTop && selectUpdatesLeft) {
        selection.tl = pt;
    } else if (selectUpdatesTop) {
        selection.br.x = pt.x;
        selection.tl.y = pt.y;
    } else if (selectUpdatesLeft) {
        selection.tl.x = pt.x;
        selection.br.y = pt.y;
    } else {
        selection.br = pt;
    }

    hideCursor();
    redraw();
}

void Vterm::selectUpdate(int pX, int pY) {
    logT << "selectUpdate (" << pX << "," << pY << ")" << std::endl;

    pX = std::min(std::max(0, pX - opts.border), winPx - 2 * opts.border);
    pY = std::min(std::max(0, pY - opts.border), winPy - 2 * opts.border);
    Point pt = cf->getLogicalPoint(Point(pX / glyphPx, pY / glyphPy));

    Rect& selection = cf->getSelection();

    if (selection.rectangular) {
        if (selectUpdatesLeft && pt.x > selection.br.x) {
            std::swap(selection.tl.x, selection.br.x);
            selectUpdatesLeft = false;
        } else if (!selectUpdatesLeft && pt.x < selection.tl.x) {
            std::swap(selection.tl.x, selection.br.x);
            selectUpdatesLeft = true;
        }

        if (selectUpdatesTop && pt.y > selection.br.y) {
            std::swap(selection.tl.y, selection.br.y);
            selectUpdatesTop = false;
        } else if (!selectUpdatesTop && pt.y < selection.tl.y) {
            std::swap(selection.tl.y, selection.br.y);
            selectUpdatesTop = true;
        }

        if (selectUpdatesTop && selectUpdatesLeft) {
            selection.tl = pt;
        } else if (selectUpdatesTop) {
            selection.br.x = pt.x;
            selection.tl.y = pt.y;
        } else if (selectUpdatesLeft) {
            selection.tl.x = pt.x;
            selection.br.y = pt.y;
        } else {
            selection.br = pt;
        }
    } else if (selectUpdatesTop) {
        if (selection.br < pt) {
            selection.tl = selection.br;
            selection.br = pt;
            selectUpdatesTop = selectUpdatesLeft = false;
        } else {
            selection.tl = pt;
        }
    } else {
        if (pt < selection.tl) {
            selection.br = selection.tl;
            selection.tl = pt;
            selectUpdatesTop = selectUpdatesLeft = true;
        } else {
            selection.br = pt;
        }
    }
    redraw();
}

bool Vterm::selectFinish(std::string& utf8_selection) {
    logT << "selectFinish ()" << std::endl;

    showCursor();
    redraw();

    return cf->getSelectedUtf8(utf8_selection);
}

void Vterm::selectClear() {
    logT << "selectClear ()" << std::endl;
    cf->getSelection().clear();
    redraw();
}

void Vterm::selectRectangularModeToggle() {
    logT << "selectRectangularModeToggle ()" << std::endl;
    Rect& selection = cf->getSelection();
    selection.toggleRectangular();
    if (selection.rectangular && selection.br.x < selection.tl.x) {
        // A valid linear selection is ordered by row and may therefore have
        // its top endpoint to the right of its bottom endpoint.  Rectangular
        // selection requires independently ordered axes.  Preserve which
        // horizontal edge is being dragged while normalizing the corners.
        std::swap(selection.tl.x, selection.br.x);
        selectUpdatesLeft = true;
    }
    redraw();
}

void Vterm::pasteSelection(const std::string& utf8_selection) {
    std::ostringstream oss;

    if (bracketedPasteMode) {
        oss << "\x1b[200~";
    }

    for (const auto ch : utf8_selection) {
        oss << (ch == '\n' ? '\r' : ch);
    }

    if (bracketedPasteMode) {
        oss << "\x1b[201~";
    }

    if (oss.str().size()) {
        writePty(oss.str().c_str(), true);
    }
}
