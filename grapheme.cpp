#include "grapheme.h"

#include <utf8proc.h>

bool GraphemeBreaker::breakBefore(uint32_t codepoint) {
    if (!hasPrevious_) {
        hasPrevious_ = true;
        previous_ = static_cast<int32_t>(codepoint);
        return true;
    }

    const bool boundary = utf8proc_grapheme_break_stateful(
        previous_, static_cast<int32_t>(codepoint), &state_);
    previous_ = static_cast<int32_t>(codepoint);
    if (boundary) state_ = 0;
    return boundary;
}

void GraphemeBreaker::reset() {
    hasPrevious_ = false;
    previous_ = 0;
    state_ = 0;
}
