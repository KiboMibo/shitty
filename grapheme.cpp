#include "grapheme.h"

#include <utf8proc.h>


namespace stl {}
using namespace stl;

static bool isDefaultWideCjk(u32 codepoint) {
    // UAX #11 section 6.1 assigns Wide to unassigned codepoints in blocks
    // reserved for CJK ideographs.  utf8proc only reports the width of
    // assigned characters, so retain the Unicode default for the holes.
    return (codepoint >= 0x3400 && codepoint <= 0x4dbf)
        || (codepoint >= 0x4e00 && codepoint <= 0x9fff)
        || (codepoint >= 0xf900 && codepoint <= 0xfaff)
        || (codepoint >= 0x20000 && codepoint <= 0x2fffd)
        || (codepoint >= 0x30000 && codepoint <= 0x3fffd);
}

static bool isSpacingFormat(u32 codepoint) {
    // These Cf characters carry a visible sign.  Unicode terminal wcwidth
    // profiles give them one cell rather than treating every Cf as a
    // zero-width control, as utf8proc_charwidth does.
    return (codepoint >= 0x600 && codepoint <= 0x605)
        || codepoint == 0x6dd
        || codepoint == 0x70f
        || (codepoint >= 0x890 && codepoint <= 0x891)
        || codepoint == 0x8e2
        || codepoint == 0x110bd
        || codepoint == 0x110cd;
}

int codepointWidth(u32 codepoint) {
    const int width = utf8proc_charwidth((i32)(codepoint));
    if (width == 1 && (isDefaultWideCjk(codepoint)
                      || (codepoint >= 0x1f1e6 && codepoint <= 0x1f1ff))) {
        return 2;
    }
    if (width == 0 && isSpacingFormat(codepoint)) {
        return 1;
    }
    return width;
}

bool GraphemeBreaker::breakBefore(u32 codepoint) {
    if (!hasPrevious_) {
        hasPrevious_ = true;
        previous_ = (i32)(codepoint);
        return true;
    }

    if (previous_ >= 0x20 && previous_ < 0x7f && codepoint >= 0x20 && codepoint < 0x7f) {
        previous_ = (i32)(codepoint);
        state_ = 0;
        return true;
    }

    const bool boundary = utf8proc_grapheme_break_stateful(previous_, (i32)(codepoint), &state_);
    previous_ = (i32)(codepoint);
    if (boundary) {
        state_ = 0;
    }
    return boundary;
}

void GraphemeBreaker::setBoundaryAfter(u32 codepoint) {
    hasPrevious_ = true;
    previous_ = (i32)(codepoint);
    state_ = 0;
}

void GraphemeBreaker::reset() {
    hasPrevious_ = false;
    previous_ = 0;
    state_ = 0;
}
