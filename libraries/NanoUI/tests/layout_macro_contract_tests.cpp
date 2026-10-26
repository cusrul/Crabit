// Arduino defines this before sketch/library headers. Keep it usable by Apps.
#define constrain(value, low, high) \
    ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#include <NanoUILegacy.h>
#include <NanoUI.h>

int main() {
    const BoxConstraints bounds = {2, 10, 3, 12};
    const Size result = bounds.constrainSize({20, 1});
    return result.width == 10 && result.height == 3 && constrain(20, 2, 10) == 10 ? 0 : 1;
}
