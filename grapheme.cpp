#include "grapheme.h"

#include <utf8proc.h>


namespace stl {}
using namespace stl;

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
