#pragma once

#include <cstdint>

class GraphemeBreaker {
public:
    bool breakBefore(uint32_t codepoint);
    void reset();

private:
    bool hasPrevious_ = false;
    int32_t previous_ = 0;
    int32_t state_ = 0;
};
