#pragma once

struct MouseWheelSteps {
    int x = 0;
    int y = 0;
};

class MouseWheelAccumulator {
public:
    MouseWheelSteps consume(double x, double y, bool reporting);
    void reset();

private:
    static int consumeAxis(double delta, double& remainder);

    bool reporting_ = false;
    double remainderX_ = 0.0;
    double remainderY_ = 0.0;
};
