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
    , onRefresh([](const Frame&) {})
    , onOsc([](int cmd, const std::string& arg) {
        logU << "OSC: '" << cmd << ";" << arg << "'" << std::endl;
    })
    , onBell([]() {
        logI << "* Bell *" << std::endl;
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
    makePalette256(palette256);

    defaultFgPalIx = (opts.fg == palette256[15]) ? 15 : -1;
    defaultBgPalIx = (opts.bg == palette256[0]) ? 0 : -1;
    fgPalIx = defaultFgPalIx;
    bgPalIx = defaultBgPalIx;

    resetTerminal();
}

void Vterm::setRefreshHandler(const RefreshHandlerFn& onRefresh_) {
    onRefresh = onRefresh_;
}

void Vterm::setOscHandler(const OscHandlerFn& onOsc_) {
    haveOscHandler = true;
    onOsc = onOsc_;
}

void Vterm::setBellHandler(const BellHandlerFn& onBell_) {
    onBell = onBell_;
}

void Vterm::resize(uint16_t winPx_, uint16_t winPy_) {
    winPx = winPx_;
    winPy = winPy_;

    uint16_t nCols_ = std::max(1, (winPx - 2 * opts.border) / glyphPx);
    uint16_t nRows_ = std::max(1, (winPy - 2 * opts.border) / glyphPy);

    if (nCols == nCols_ && nRows == nRows_) {
        cf->winPx = winPx;
        cf->winPy = winPy;
        return;
    }

    hideCursor();

    if (altScreenBufferMode) {
        frame_alt = Frame(winPx, winPy, nCols_, nRows_,
                          marginTop, marginBottom, opts.saveLines);
    } else {
        if (nRows_ < posY + 1) {
            int nScroll = nRows - nRows_;
            cf->scrollUp(0, nRows, nScroll);
            posY -= nScroll;
        }

        frame_pri.resize(winPx, winPy, nCols_, nRows_,
                         marginTop, marginBottom);

        if (nRows < nRows_) {
            int nScroll = std::min(nRows_ - nRows, (int)cf->getHistoryRows());
            cf->restoreHistory(nScroll);
            posY += nScroll;
        }

        frame_alt.freeCells();
    }
    nCols = nCols_;
    nRows = nRows_;

    if (horizMarginMode) {
        nColsEff = std::min(nColsEff, nCols);
        hMargin = std::max(0, std::min((int)hMargin, nColsEff - 2));
    } else {
        nColsEff = nCols;
        hMargin = 0;
    }
    normalizeCursorPos();
    showCursor();

    pty_resize(ptyFd, nCols, nRows);
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

    if ((modifyOtherKeys == 2 && mod2_encode(ch)) ||
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

    if (event == KeyEventType::Release &&
        !(getKittyKeyboardFlags() & 0x02)) {
        return 0;
    }

    std::ostringstream sequence;
    sequence << "\x1b[" << spec.code << ';' << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << ':' << static_cast<unsigned>(event);
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
        if (shiftedKey) {
            sequence << ':' << shiftedKey;
            if (baseLayoutKey && baseLayoutKey != key) {
                sequence << ':' << baseLayoutKey;
            }
        } else if (baseLayoutKey && baseLayoutKey != key) {
            sequence << "::" << baseLayoutKey;
        }
    }
    sequence << ';' << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << ':' << static_cast<unsigned>(event);
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
    return write(ptyFd, ucstr, len);
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
        return (modifiers != Mod::none &&
                keypadMode == KeypadMode::Application);
    },
             is_Mod_Appl_KeypadKeys},
            {[this]() {
        return (keypadMode == KeypadMode::Application);
    },
             is_Appl_KeypadKeys},
            {[this]() {
        return (modifiers != Mod::none);
    },
             is_Mod_CursorKeys},
            {[this]() {
        return (cursorKeyMode == CursorKeyMode::Application);
    },
             is_Appl_CursorKeys},

            {[this]() {
        return (modifiers != Mod::none);
    },
             is_Mod_Ansi},
            {[this]() {
        return (modifiers != Mod::none);
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

void Vterm::processInput(const std::string& str) {
    processInput((unsigned char*)str.c_str(), str.length());
}

void Vterm::feedPtyOutput(const std::string& output) {
    processInput(output);
}

void Vterm::processInput(const unsigned char* const input, int inputSize) {
    lastEscBegin = 0;
    lastNormalBegin = 0;
    lastStopPos = 0;
    hideCursor();
    for (readPos = 0; readPos < inputSize; ++readPos) {
        const unsigned char& ch = input[readPos];
        if ((ch == '\x18' || ch == '\x1a') &&
            inputState != InputState::Normal) {
            setState(InputState::Normal);
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
        switch (inputState) {
            case InputState::Normal:
                switch (ch) {
                    case '\x00':
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
                        inputOps[0] = 0;
                        inputSeparators[0] = 0;
                        nInputOps = 1;
                        csiPrefixAllowed = true;
                        setState(InputState::CSI);
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
                if (ch >= '\x40' && ch <= '\x7e') {
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
                        csiPrefixAllowed = true;
                        setState(InputState::CSI);
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
                        logU << "DECDHL: Double-height, top half" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case '4':
                        logU << "DECDHL: Double-height, bottom half" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case '5':
                        logU << "DECSWL: Single-width line" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case '6':
                        logU << "DECDWL: Double-width line" << std::endl;
                        setState(InputState::Normal);
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
                switch (ch) {
                    COLLECT_NUMERIC_PARAMS;
                    case '\x1b':
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
                    case 'E':
                        csi_CNL();
                        break;
                    case 'F':
                        csi_CPL();
                        break;
                    case 'G':
                        csi_CHA();
                        break;
                    case 'H':
                    case 'f':
                        csi_CUP();
                        break;
                    case 'I':
                        csi_CHT();
                        break;
                    case 'J':
                        csi_ED();
                        break;
                    case 'K':
                        csi_EL();
                        break;
                    case 'L':
                        csi_IL();
                        break;
                    case 'M':
                        csi_DL();
                        break;
                    case 'P':
                        csi_DCH();
                        break;
                    case 'S':
                        csi_SU();
                        break;
                    case 'T':
                        csi_SD();
                        break;
                    case 'X':
                        csi_ECH();
                        break;
                    case 'Z':
                        csi_CBT();
                        break;
                    case '@':
                        csi_ICH();
                        break;
                    case '`':
                        csi_HPA();
                        break;
                    case 'a':
                        csi_HPR();
                        break;
                    case 'b':
                        csi_REP();
                        break;
                    case 'c':
                        csi_priDA();
                        break;
                    case 'd':
                        csi_VPA();
                        break;
                    case 'e':
                        csi_VPR();
                        break;
                    case 'g':
                        csi_TBC();
                        break;
                    case 'h':
                        csi_SM();
                        break;
                    case 'l':
                        csi_RM();
                        break;
                    case 'm':
                        csi_SGR();
                        break;
                    case 'n':
                        csi_DSR();
                        break;
                    case 'r':
                        csi_STBM();
                        break;
                    case 's':
                        csi_SCOSC_SLRM();
                        break;
                    case 't':
                        csi_XTWINOPS();
                        break;
                    case 'u':
                        csi_SCORC();
                        break;
                    case '\'':
                        setState(InputState::CSI_Quote);
                        break;
                    case '\"':
                        setState(InputState::CSI_DblQuote);
                        break;
                    case '!':
                        setState(InputState::CSI_Bang);
                        break;
                    case '?':
                        setState(csiPrefixAllowed
                                     ? InputState::CSI_priv
                                     : InputState::IgnoreSequence);
                        csiPrefixAllowed = false;
                        break;
                    case ' ':
                        setState(InputState::CSI_SPC);
                        break;
                    case '>':
                        setState(csiPrefixAllowed
                                     ? InputState::CSI_GT
                                     : InputState::IgnoreSequence);
                        csiPrefixAllowed = false;
                        break;
                    case '\a':
                        break;
                    case '\b':
                        if (readPos && input[readPos - 1] == ';') {
                            --nInputOps;
                        } else {
                            inputOps[nInputOps - 1] /= 10;
                        }
                        break;
                    case '\t':
                        inp_HT();
                        setState(InputState::CSI);
                        break;
                    case '\r':
                        inp_CR();
                        setState(InputState::CSI);
                        break;
                    case '\f':
                    case '\v':
                        esc_IND();
                        setState(InputState::CSI);
                        break;

                    case '<':
                        setState(csiPrefixAllowed
                                     ? InputState::CSI_LT
                                     : InputState::IgnoreSequence);
                        csiPrefixAllowed = false;
                        break;
                    case '=':
                        setState(csiPrefixAllowed
                                     ? InputState::CSI_EQ
                                     : InputState::IgnoreSequence);
                        csiPrefixAllowed = false;
                        break;
                    case '$':
                        setState(InputState::CSI_Dollar);
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_Bang:
                switch (ch) {
                    case 'p':
                        csi_DECSTR();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_Quote:
                switch (ch) {
                    case '}':
                        csi_DECIC();
                        break;
                    case '~':
                        csi_DECDC();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_DblQuote:
                switch (ch) {
                    case 'p':
                        csiq_DECSCL();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_SPC:
                switch (ch) {
                    case '@':
                        csi_ecma48_SL();
                        break;
                    case 'A':
                        csi_ecma48_SR();
                        break;
                    case 'q':
                        csi_DECSCUSR();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_GT:
                switch (ch) {
                    COLLECT_NUMERIC_PARAMS;
                    case 'c':
                        csi_secDA();
                        break;
                    case 'm':
                        csi_XTMODKEYS();
                        break;
                    case 'u':
                        csi_kittyKeyboardPush();
                        break;
                    case 'q':
                        csi_XTVERSION();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_LT:
                switch (ch) {
                    COLLECT_NUMERIC_PARAMS;
                    case 'u':
                        csi_kittyKeyboardPop();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_EQ:
                switch (ch) {
                    COLLECT_NUMERIC_PARAMS;
                    case 'u':
                        csi_kittyKeyboardSet();
                        break;
                    case 'c':
                        csi_terDA();
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_priv:
                switch (ch) {
                    COLLECT_NUMERIC_PARAMS;
                    case '\x1b':
                        setState(InputState::Normal);
                        break;
                    case 'h':
                        csi_privSM();
                        break;
                    case 'l':
                        csi_privRM();
                        break;
                    case 's':
                        csi_privSave();
                        break;
                    case 'r':
                        csi_privRestore();
                        break;
                    case 'u':
                        csi_kittyKeyboardQuery();
                        break;
                    case '$':
                        setState(InputState::CSI_priv_Dollar);
                        break;
                        IGNORE_SEQUENCE_ON_BAD_PARAMS;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::CSI_Dollar:
                if (ch == 'p') {
                    csi_DECRQM(false);
                } else {
                    unhandledInput(ch);
                }
                break;
            case InputState::CSI_priv_Dollar:
                if (ch == 'p') {
                    csi_DECRQM(true);
                } else {
                    unhandledInput(ch);
                }
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
                    default:
                        if (argBuf.size() < 4095) {
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
                    default:
                        if (argBuf.size() < 4095) {
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
                    default:
                        if (!argBufOverflowed && argBuf.size() <= 4093) {
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
                if (ch == 0x9c) {
                    setState(InputState::Normal);
                } else if (ch == '\x1b') {
                    setState(InputState::String_Esc);
                }
                break;
            case InputState::String_Esc:
                setState(ch == '\\' ? InputState::Normal
                                     : InputState::String);
                break;
        }
    }
    traceNormalInput();
    showCursor();
    redraw();
}

void Vterm::setHyperlink(const std::string& parametersAndUri) {
    const size_t separator = parametersAndUri.find(';');
    if (separator == std::string::npos) {
        logW << "Malformed OSC 8 argument" << std::endl;
        return;
    }

    const std::string uri = parametersAndUri.substr(separator + 1);
    if (uri.empty()) {
        activeHyperlink = 0;
        return;
    }

    const auto known = hyperlinkIds.find(uri);
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
    hyperlinkIds.emplace(uri, activeHyperlink);
    hyperlinks.emplace(activeHyperlink, uri);
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
    cf->getSelection().toggleRectangular();
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
