#include <NanoUI/widgets/scroll_controller.h>

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int checks = 0;
int failures = 0;

#define CHECK(expr)                                        \
    do {                                                   \
        ++checks;                                          \
        if (!(expr)) {                                     \
            ++failures;                                    \
            std::printf("line %d: %s\n", __LINE__, #expr); \
        }                                                  \
    } while (false)

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.0001f;
}

void basicReveal() {
    NanoUI::ScrollController c;
    CHECK(near(c.offset(), 0.0f));
    CHECK(c.reveal(2, 8, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 0));  // already visible
    CHECK(c.reveal(12, 16, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 6));  // minimally down
    CHECK(c.reveal(1, 4, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 1));  // minimally up
    CHECK(c.reveal(40, 60, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 40));  // oversized range aligns its start
}

void boundsAndClamping() {
    NanoUI::ScrollController c;
    CHECK(c.reveal(0, 5, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 0));
    CHECK(c.reveal(95, 100, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 90));  // final boundary
    CHECK(c.setOffset(500, 10, 100) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 90));
    CHECK(c.setOffset(12, 10, 3) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 0));  // extent shrank below viewport
    CHECK(c.reveal(0, 2, 10, 3) == NanoUI::UiError::none);
    CHECK(near(c.offset(), 0));
}

void invalidInputsPreserveOffset() {
    NanoUI::ScrollController c;
    CHECK(c.setOffset(20, 10, 100) == NanoUI::UiError::none);
    const float original = c.offset();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

#define INVALID_SET(v, vp, ex)                                                \
    do {                                                                      \
        CHECK(c.setOffset((v), (vp), (ex)) == NanoUI::UiError::invalidInput); \
        CHECK(near(c.offset(), original));                                    \
    } while (false)
#define INVALID_REVEAL(s, e, vp, ex)                                            \
    do {                                                                        \
        CHECK(c.reveal((s), (e), (vp), (ex)) == NanoUI::UiError::invalidInput); \
        CHECK(near(c.offset(), original));                                      \
    } while (false)

    INVALID_SET(-1, 10, 100);
    INVALID_SET(nan, 10, 100);
    INVALID_SET(inf, 10, 100);
    INVALID_SET(1, 0, 100);
    INVALID_SET(1, 10, -1);
    INVALID_SET(1, inf, 100);
    INVALID_SET(1, 10, nan);

    INVALID_REVEAL(-1, 1, 10, 100);
    INVALID_REVEAL(4, 3, 10, 100);
    INVALID_REVEAL(0, inf, 10, 100);
    INVALID_REVEAL(nan, 1, 10, 100);
    INVALID_REVEAL(0, 1, 0, 100);
    INVALID_REVEAL(0, 1, 10, -1);
    INVALID_REVEAL(9, 11, 10, 10);
    INVALID_REVEAL(0, inf, 10, inf);

#undef INVALID_SET
#undef INVALID_REVEAL
}
}  // namespace

int main() {
    basicReveal();
    boundsAndClamping();
    invalidInputsPreserveOffset();
    std::printf("scroll controller: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
