#include "gate_hold.h"
#include "test.h"

int main(void)
{
    gate_hold_t hold;
    gate_hold_reset(&hold);

    /* No car: closes only after the beam has been seen clear for the whole window. */
    CHECK(!gate_hold_may_close(&hold, false, 1000000));
    CHECK(!gate_hold_may_close(&hold, false, 1000000 + GATE_CLEAR_US - 1));
    CHECK(gate_hold_may_close(&hold, false, 1000000 + GATE_CLEAR_US));

    /* A car in the beam holds the barrier for as long as it stays. */
    gate_hold_reset(&hold);
    bool closed = false;
    for (int64_t t = 0; t < 60000000; t += 50000)
        closed = closed || gate_hold_may_close(&hold, true, t);
    CHECK(!closed);

    /* A flicker clear in the middle of a car restarts the window. */
    gate_hold_reset(&hold);
    CHECK(!gate_hold_may_close(&hold, false, 0));
    CHECK(!gate_hold_may_close(&hold, true, GATE_CLEAR_US - 1));
    CHECK(!gate_hold_may_close(&hold, false, GATE_CLEAR_US));
    CHECK(!gate_hold_may_close(&hold, false, 2 * GATE_CLEAR_US - 1));
    CHECK(gate_hold_may_close(&hold, false, 2 * GATE_CLEAR_US));

    /* A new opening forgets the clear time of the previous one. */
    gate_hold_reset(&hold);
    CHECK(!gate_hold_may_close(&hold, false, 10 * GATE_CLEAR_US));
    return TEST_REPORT("gate_hold");
}
