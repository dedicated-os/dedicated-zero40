#ifndef DRASTIC_FASTSCALE_H
#define DRASTIC_FASTSCALE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON) && !defined(FAST_SCALE_DISABLE_NEON)
#include <arm_neon.h>
#endif

#define FAST_SCALE_SOURCE_WIDTH 512
#define FAST_SCALE_SOURCE_HEIGHT 384
#define FAST_SCALE_WIDTH 480
#define FAST_SCALE_HEIGHT 360

// Packed 32-bit pixels, separate buffers, positive byte pitches. Copy directly
// into the destination texture's locked pixels without a temporary output image.
static inline void FastScale_copy(void *destination, int destination_pitch,
		const void *source, int source_pitch) {
	for (int y=0; y<FAST_SCALE_HEIGHT; y++) {
		// floor((y + 0.5) * 16 / 15): omit row 7 of each 16-row block.
		int source_y = y + (y + 8) / 15;
		const uint8_t *s = (const uint8_t *)source + (size_t)source_y * source_pitch;
		uint8_t *d = (uint8_t *)destination + (size_t)y * destination_pitch;
		for (int block=0; block<FAST_SCALE_SOURCE_WIDTH/16; block++, s+=64, d+=60) {
			// Keep pixels 0..6 and 8..15, matching the same centered nearest
			// neighbor mapping horizontally. Unaligned row/block starts are valid.
#if defined(__ARM_NEON) && !defined(FAST_SCALE_DISABLE_NEON)
			uint8x16_t a = vld1q_u8(s);
			uint8x16_t b = vld1q_u8(s + 16);
			uint8x16_t c = vld1q_u8(s + 32);
			uint8x16_t e = vld1q_u8(s + 48);
			vst1q_u8(d, a);
			vst1q_u8(d + 16, b);
			// Overwrite the omitted pixel from b; neither store exceeds the
			// 60-byte output block, including the last block of the last row.
			vst1q_u8(d + 28, c);
			vst1q_u8(d + 44, e);
#else
			// Constant-size memcpy permits wide loads/stores without alignment
			// or strict-aliasing assumptions on the caller's pixel buffers.
			memcpy(d, s, 7 * 4);
			memcpy(d + 7 * 4, s + 8 * 4, 8 * 4);
#endif
		}
	}
}

#endif
