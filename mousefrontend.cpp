#include "mousefrontend.h"

#include <algorithm>
#include <cmath>

int MouseWheelAccumulator::consumeAxis(double delta, double& remainder) {
    if (!std::isfinite(delta)) {
        remainder = 0.0;
        return 0;
    }
    const double total = remainder + std::clamp(delta, -100.0, 100.0);
    const int steps = static_cast<int>(std::trunc(total));
    remainder = total - steps;
    return steps;
}

MouseWheelSteps MouseWheelAccumulator::consume(
    double x, double y, bool reporting) {
    if (reporting != reporting_) {
        reporting_ = reporting;
        remainderX_ = 0.0;
        remainderY_ = 0.0;
    }

    MouseWheelSteps steps;
    steps.y = consumeAxis(y, remainderY_);
    if (reporting) {
        steps.x = consumeAxis(x, remainderX_);
    } else {
        remainderX_ = 0.0;
    }
    return steps;
}

void MouseWheelAccumulator::reset() {
    reporting_ = false;
    remainderX_ = 0.0;
    remainderY_ = 0.0;
}
