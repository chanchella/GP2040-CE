#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/mouse_to_stick_mapper.h"

using namespace oag;

namespace {

double axisToUnit(std::int32_t value) {
    if (value < 0) {
        return static_cast<double>(value) /
            (static_cast<double>(
                std::numeric_limits<std::int32_t>::max()
            ) + 1.0);
    }

    return static_cast<double>(value) /
        static_cast<double>(std::numeric_limits<std::int32_t>::max());
}

} // namespace

int main() {
    KeyboardState keyboard {};
    keyboard.setPressed(0x04, true); // HID Keyboard 'A'
    assert(keyboard.pressed(0x04));
    keyboard.setPressed(0x04, false);
    assert(!keyboard.pressed(0x04));

    const MouseToStickMapper mapper;

    MouseStickConfig linear {};
    linear.sensitivityX = 0.01;
    linear.sensitivityY = 0.01;
    linear.exponent = 1.0;
    linear.deadzoneX = 0.10;
    linear.deadzoneY = 0.10;
    linear.boundary = StickBoundary::Circle;

    const StickVector zero = mapper.map({0, 0}, linear);
    assert(zero.x == 0);
    assert(zero.y == 0);

    const StickVector smallRight = mapper.map({1, 0}, linear);
    assert(smallRight.x > 0);
    assert(smallRight.y == 0);
    assert(axisToUnit(smallRight.x) > 0.10);

    const StickVector smallLeft = mapper.map({-1, 0}, linear);
    assert(smallLeft.x < 0);
    assert(smallLeft.y == 0);
    assert(std::abs(
        axisToUnit(smallLeft.x) + axisToUnit(smallRight.x)
    ) < 0.000001);

    const StickVector down = mapper.map({0, 10}, linear);
    assert(down.x == 0);
    assert(down.y > 0);

    MouseStickConfig inverted = linear;
    inverted.invertY = true;
    const StickVector up = mapper.map({0, 10}, inverted);
    assert(up.x == 0);
    assert(up.y < 0);

    MouseStickConfig ballistic = linear;
    ballistic.exponent = 2.0;

    const StickVector linearMid = mapper.map({10, 0}, linear);
    const StickVector ballisticMid = mapper.map({10, 0}, ballistic);

    // Exponent > 1 gives finer low-speed control for the same movement.
    assert(std::abs(ballisticMid.x) < std::abs(linearMid.x));

    MouseStickConfig circle {};
    circle.sensitivityX = 0.01;
    circle.sensitivityY = 0.01;
    circle.exponent = 1.0;
    circle.deadzoneX = 0.0;
    circle.deadzoneY = 0.0;
    circle.boundary = StickBoundary::Circle;

    const StickVector circleCorner = mapper.map({100, 100}, circle);
    const double circleX = axisToUnit(circleCorner.x);
    const double circleY = axisToUnit(circleCorner.y);
    assert(std::hypot(circleX, circleY) <= 1.000001);
    assert(circleX > 0.70 && circleX < 0.72);
    assert(circleY > 0.70 && circleY < 0.72);

    MouseStickConfig square = circle;
    square.boundary = StickBoundary::Square;
    const StickVector squareCorner = mapper.map({100, 100}, square);
    assert(squareCorner.x == std::numeric_limits<std::int32_t>::max());
    assert(squareCorner.y == std::numeric_limits<std::int32_t>::max());

    return 0;
}
