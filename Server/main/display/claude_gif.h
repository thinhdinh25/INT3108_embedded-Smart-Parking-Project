#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLAUDE_FRAME_COUNT 18
#define CLAUDE_WIDTH 68
#define CLAUDE_HEIGHT 46

extern const uint16_t claude_frame_delay_ms[CLAUDE_FRAME_COUNT];
bool claude_decode_frame(size_t frame_index, uint16_t *pixels, size_t pixel_count);
