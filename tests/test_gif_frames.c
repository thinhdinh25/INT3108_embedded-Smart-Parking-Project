/* Animation frame data: every frame decodes fully, and the offline matting/byte swap holds. */
#include <stdlib.h>

#include "claude_gif.h"
#include "kaoruko_gif.h"
#include "test.h"

#define K_PIXELS (KAORUKO_WIDTH * KAORUKO_HEIGHT)
#define C_PIXELS (CLAUDE_WIDTH * CLAUDE_HEIGHT)

/* Footer panel color 0x0841 as stored on flash (already byte-swapped). */
#define PANEL_SWAPPED 0x4108

int main(void)
{
    static uint16_t px[K_PIXELS];

    for (size_t f = 0; f < KAORUKO_FRAME_COUNT; ++f)
    {
        CHECK(kaoruko_decode_frame(f, px, K_PIXELS));
        /* The white matte touching the border was replaced by black offline. */
        CHECK(px[0] == 0x0000 && px[KAORUKO_WIDTH - 1] == 0x0000 && px[K_PIXELS - 1] == 0x0000);
        CHECK(kaoruko_frame_delay_ms[f] > 0);
    }
    for (size_t f = 0; f < CLAUDE_FRAME_COUNT; ++f)
    {
        CHECK(claude_decode_frame(f, px, C_PIXELS));
        CHECK(px[0] == PANEL_SWAPPED); /* corner is footer background, stored big-endian */
        CHECK(claude_frame_delay_ms[f] > 0);
    }

    /* Invalid requests are refused instead of overrunning a buffer. */
    CHECK(!kaoruko_decode_frame(KAORUKO_FRAME_COUNT, px, K_PIXELS));
    CHECK(!kaoruko_decode_frame(0, px, K_PIXELS - 1));
    CHECK(!kaoruko_decode_frame(0, NULL, K_PIXELS));
    CHECK(!claude_decode_frame(CLAUDE_FRAME_COUNT, px, C_PIXELS));
    CHECK(!claude_decode_frame(0, px, C_PIXELS + 1));
    return TEST_REPORT("gif_frames");
}
