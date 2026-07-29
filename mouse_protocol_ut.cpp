/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "mouse_protocol.h"

#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(MouseProtocol) {
    STD_TEST(EncodesLegacyPressAndRelease) {
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::Default, MouseEventType::Press, 0, 0, 1, 1, 1) == "\x1b[M !!");
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::Default, MouseEventType::Release, 0, 0, 1, 1, 1) == "\x1b[M#!!");
    }

    STD_TEST(EncodesSgrPressReleaseAndMotion) {
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::SGR, MouseEventType::Press, MouseControl, 0, 1, 10, 20) == "\x1b[<16;10;20M");
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::SGR, MouseEventType::Release, 0, 0, 1, 10, 20) == "\x1b[<0;10;20m");
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::SGRPixels, MouseEventType::Release, 0, 0, 1, 236, 120) == "\x1b[<0;236;120m");
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::SGR, MouseEventType::Motion, MouseShift, 2, 0, 10, 20) == "\x1b[<37;10;20M");
    }

    STD_TEST(EncodesWheelAndExtendedButtons) {
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::SGR, MouseEventType::Press, 0, 0, 4, 2, 3) == "\x1b[<64;2;3M");
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::SGR, MouseEventType::Press, 0, 0, 11, 2, 3) == "\x1b[<131;2;3M");
        STD_INSIST(encodeMouseProtocol(MouseTrackingEnc::URXVT, MouseEventType::Press, MouseShift, 0, 4, 2, 3) == "\x1b[100;2;3M");
    }

    STD_TEST(ClampsLegacyCoordinates) {
        const auto low = encodeMouseProtocol(MouseTrackingEnc::Default, MouseEventType::Press, 0, 0, 1, -100, -100);
        const auto high = encodeMouseProtocol(MouseTrackingEnc::Default, MouseEventType::Press, 0, 0, 1, 1000, 1000);

        STD_INSIST(low == "\x1b[M !!");
        STD_INSIST((u8)(high[4]) == 255);
        STD_INSIST((u8)(high[5]) == 255);
    }

    STD_TEST(UsesUtf8ForLargeCoordinates) {
        const auto output = encodeMouseProtocol(MouseTrackingEnc::UTF8, MouseEventType::Press, 0, 0, 1, 300, 400);

        STD_INSIST(output.size() == 8);
        STD_INSIST(output.compare(0, 3, "\x1b[M") == 0);
        STD_INSIST((u8)(output[3]) == 32);
        STD_INSIST((u8)(output[4]) == 0xc5);
        STD_INSIST((u8)(output[5]) == 0x8c);
    }

    STD_TEST(RejectsUnknownButtonsWithoutWriting) {
        StringBuilder output;
        output << StringView(u8"prefix");

        STD_INSIST(!encodeMouseProtocol(output, MouseTrackingEnc::SGR, MouseEventType::Press, 0, 0, 0, 1, 1));
        STD_INSIST(StringView(output) == StringView(u8"prefix"));
    }
}
