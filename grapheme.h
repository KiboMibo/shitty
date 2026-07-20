#pragma once
#include <std/sys/types.h>

#include <cstdint>

class GraphemeBreaker {
public:
    bool breakBefore(u32 codepoint);
    void setBoundaryAfter(u32 codepoint);
    void reset();

private:
    bool hasPrevious_ = false;
    i32 previous_ = 0;
    i32 state_ = 0;
};
