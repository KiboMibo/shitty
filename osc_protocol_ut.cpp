/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "osc_protocol.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(Osc52) {
    STD_TEST(ParsesDefaultQuerySelection) {
        const Osc52Request request = parseOsc52(";?");

        STD_INSIST(request.valid);
        STD_INSIST(request.query);
        STD_INSIST(request.primary);
        STD_INSIST(request.clipboard);
        STD_INSIST(request.replySelector == "s0");
    }

    STD_TEST(ResolvesSelectBufferAgainstConfiguration) {
        const Osc52Request primary = parseOsc52("s;?", false);
        const Osc52Request clipboard = parseOsc52("s;?", true);

        STD_INSIST(primary.primary);
        STD_INSIST(!primary.clipboard);
        STD_INSIST(!clipboard.primary);
        STD_INSIST(clipboard.clipboard);
        STD_INSIST(primary.replySelector == "s");
        STD_INSIST(clipboard.replySelector == "s");
    }

    STD_TEST(ParsesMultipleExplicitSelectors) {
        const Osc52Request request = parseOsc52("pc;?");

        STD_INSIST(request.valid);
        STD_INSIST(request.primary);
        STD_INSIST(request.clipboard);
        STD_INSIST(request.replySelector == "p");
    }

    STD_TEST(DecodesBinaryClipboardContent) {
        const Osc52Request request = parseOsc52("c;AAEC/w==");

        STD_INSIST(request.valid);
        STD_INSIST(!request.query);
        STD_INSIST(request.content.size() == 4);
        STD_INSIST((u8)(request.content[0]) == 0);
        STD_INSIST((u8)(request.content[1]) == 1);
        STD_INSIST((u8)(request.content[2]) == 2);
        STD_INSIST((u8)(request.content[3]) == 255);
    }

    STD_TEST(RejectsMalformedRequests) {
        STD_INSIST(!parseOsc52("c").valid);
        STD_INSIST(!parseOsc52("c;Zm 9v").valid);
        STD_INSIST(!parseOsc52("c;Z").valid);
    }

    STD_TEST(EncodesProtocolReply) {
        STD_INSIST(encodeOsc52Reply("c", "foo") == "\x1b]52;c;Zm9v\x1b\\");
    }

    STD_TEST(QueryPrefersPrimaryAndFallsBackToClipboard) {
        const Osc52Request both = parseOsc52(";?");

        STD_INSIST(encodeOsc52QueryReply(both, true, "primary", "clipboard") == "\x1b]52;s0;cHJpbWFyeQ==\x1b\\");
        STD_INSIST(encodeOsc52QueryReply(both, true, "", "clipboard") == "\x1b]52;s0;Y2xpcGJvYXJk\x1b\\");
    }

    STD_TEST(DeniedQueryReturnsEmptyReply) {
        const Osc52Request request = parseOsc52("c;?");

        STD_INSIST(encodeOsc52QueryReply(request, false, "primary", "clipboard") == "\x1b]52;c;\x1b\\");
    }
}

STD_TEST_SUITE(OscCwd) {
    STD_TEST(AcceptsAbsolutePaths) {
        STD_INSIST(oscCwdToPath("/tmp/project") == "/tmp/project");
    }

    STD_TEST(ExtractsFileUrlPath) {
        STD_INSIST(oscCwdToPath("file://localhost/home/user") == "/home/user");
        STD_INSIST(oscCwdToPath("file:///home/user") == "/home/user");
    }

    STD_TEST(DecodesPercentEscapesCaseInsensitively) {
        STD_INSIST(oscCwdToPath("file://host/a%20b/%2f/%AF") == "/a b///\xaf");
    }

    STD_TEST(RejectsRelativeAndMalformedPaths) {
        STD_INSIST(oscCwdToPath("tmp/project").empty());
        STD_INSIST(oscCwdToPath("file://host").empty());
        STD_INSIST(oscCwdToPath("/tmp/%").empty());
        STD_INSIST(oscCwdToPath("/tmp/%0g").empty());
    }
}
