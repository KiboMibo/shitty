#include "grapheme.h"

#include <utf8proc.h>

bool GraphemeBreaker::breakBefore(u32 codepoint) {
    if (!hasPrevious_) {
        hasPrevious_ = true;
        previous_ = (i32)(codepoint);
        return true;
    }

    const bool boundary = utf8proc_grapheme_break_stateful(previous_, (i32)(codepoint), &state_);
    previous_ = (i32)(codepoint);
    if (boundary) {
        state_ = 0;
    }
    return boundary;
}

void GraphemeBreaker::reset() {
    hasPrevious_ = false;
    previous_ = 0;
    state_ = 0;
}
