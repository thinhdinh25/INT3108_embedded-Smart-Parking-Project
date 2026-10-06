#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KAORUKO_FRAME_COUNT 10
#define KAORUKO_WIDTH 96
#define KAORUKO_HEIGHT 96

extern const uint16_t kaoruko_frame_delay_ms[KAORUKO_FRAME_COUNT];

/* Decode one flash-resident compressed frame into a DMA-capable RGB565 buffer. */
bool kaoruko_decode_frame(size_t frame_index, uint16_t *pixels, size_t pixel_count);
