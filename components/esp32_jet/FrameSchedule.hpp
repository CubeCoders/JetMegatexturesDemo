#pragma once
#include <cstdint>

// Absolute 60 Hz deadlines; carry the fractional microsecond instead of
// accumulating rounding drift. An overrun rebases without a catch-up burst.
class FrameSchedule {
public:
    void reset(int64_t startUs) {
        remainder = 0;
        deadline = startUs + period();
    }
    int64_t nextFrame(int64_t nowUs) {
        const int64_t start = nowUs < deadline ? deadline : nowUs;
        deadline = start + period();
        return start;
    }
private:
    int64_t deadline = 0;
    unsigned remainder = 0;
    int64_t period() {
        remainder += 1000000 % 60;
        const int64_t us = 1000000 / 60 + remainder / 60;
        remainder %= 60;
        return us;
    }
};
