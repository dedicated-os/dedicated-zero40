#ifndef DRASTIC_SCREENFRAME_H
#define DRASTIC_SCREENFRAME_H

#include <SDL2/SDL.h>
#include <string.h>

#if defined(__ARM_NEON) && !defined(SCREEN_FRAME_DISABLE_NEON)
#include <arm_neon.h>
#endif

// Own the last uploaded game image. SDL streaming locks are write-only and
// cannot be used to read an earlier frame back for screenshots.
typedef struct {
	SDL_Surface *surface;
	SDL_Rect locked_rect;
	int locked;
	int ready;
	Uint64 pixels_started; // Optional detailed profile: lock return to unlock entry.
} ScreenFrame;

static inline void ScreenFrame_reset(ScreenFrame *frame) {
	if (frame->surface) SDL_FreeSurface(frame->surface);
	memset(frame, 0, sizeof(*frame));
}

static inline int ScreenFrame_init(ScreenFrame *frame, Uint32 format, int w, int h) {
	ScreenFrame_reset(frame);
	if (SDL_ISPIXELFORMAT_FOURCC(format)) return SDL_SetError("Unsupported game pixel format");
	frame->surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, SDL_BITSPERPIXEL(format), format);
	if (!frame->surface) return -1;
	SDL_SetSurfaceBlendMode(frame->surface, SDL_BLENDMODE_NONE);
	SDL_FillRect(frame->surface, NULL, SDL_MapRGBA(frame->surface->format, 0, 0, 0, 255));
	return 0;
}

static inline int ScreenFrame_rect(const ScreenFrame *frame, const SDL_Rect *rect, SDL_Rect *out) {
	if (!frame->surface) return SDL_SetError("Game frame is unavailable");
	*out = rect ? *rect : (SDL_Rect){0, 0, frame->surface->w, frame->surface->h};
	if (out->x < 0 || out->y < 0 || out->w <= 0 || out->h <= 0 ||
		out->x >= frame->surface->w || out->y >= frame->surface->h ||
		out->w > frame->surface->w - out->x || out->h > frame->surface->h - out->y)
		return SDL_SetError("Invalid game frame rectangle");
	return 0;
}

static inline void *ScreenFrame_pixels(const ScreenFrame *frame, const SDL_Rect *rect) {
	return (Uint8 *)frame->surface->pixels + (size_t)rect->y * frame->surface->pitch +
		(size_t)rect->x * frame->surface->format->BytesPerPixel;
}

static inline void ScreenFrame_makeOpaque(ScreenFrame *frame, const SDL_Rect *rect) {
	Uint32 mask = frame->surface->format->Amask;
	if (!mask) return;
	int bpp = frame->surface->format->BytesPerPixel;
	const int width = rect->w, height = rect->h, pitch = frame->surface->pitch;
	Uint8 *row = ScreenFrame_pixels(frame, rect);
	// Keep format tests and aliased surface/rectangle reads out of the pixel loop.
	// Hi-res has four times as many input pixels as the native/x2 display modes.
	if (bpp == 4) {
#if defined(__ARM_NEON) && !defined(SCREEN_FRAME_DISABLE_NEON)
		const uint8x16_t alpha = vreinterpretq_u8_u32(vdupq_n_u32(mask));
#endif
		for (int y=0; y<height; y++, row+=pitch) {
			int x = 0;
#if defined(__ARM_NEON) && !defined(SCREEN_FRAME_DISABLE_NEON)
			for (; x+4<=width; x+=4)
				vst1q_u8(row+x*4, vorrq_u8(vld1q_u8(row+x*4), alpha));
#endif
			for (; x<width; x++) {
				// memcpy permits unaligned pixels without aliasing assumptions.
				Uint32 pixel;
				memcpy(&pixel, row + x * 4, sizeof(pixel));
				pixel |= mask;
				memcpy(row + x * 4, &pixel, sizeof(pixel));
			}
		}
	} else if (bpp == 2) {
#if defined(__ARM_NEON) && !defined(SCREEN_FRAME_DISABLE_NEON)
		const uint8x16_t alpha = vreinterpretq_u8_u16(vdupq_n_u16((Uint16)mask));
#endif
		for (int y=0; y<height; y++, row+=pitch) {
			int x = 0;
#if defined(__ARM_NEON) && !defined(SCREEN_FRAME_DISABLE_NEON)
			for (; x+8<=width; x+=8)
				vst1q_u8(row+x*2, vorrq_u8(vld1q_u8(row+x*2), alpha));
#endif
			for (; x<width; x++) {
				Uint16 pixel;
				memcpy(&pixel, row + x * 2, sizeof(pixel));
				pixel |= (Uint16)mask;
				memcpy(row + x * 2, &pixel, sizeof(pixel));
			}
		}
	}
}

static inline int ScreenFrame_lock(ScreenFrame *frame, const SDL_Rect *rect, void **pixels, int *pitch) {
	if (frame->locked) return SDL_SetError("Game frame is already locked");
	if (!pixels || !pitch) return SDL_SetError("Missing game frame lock output");
	if (ScreenFrame_rect(frame, rect, &frame->locked_rect) != 0) return -1;
	*pixels = ScreenFrame_pixels(frame, &frame->locked_rect);
	*pitch = frame->surface->pitch;
	frame->locked = 1;
	return 0;
}

static inline int ScreenFrame_update(ScreenFrame *frame, const SDL_Rect *rect,
		const void *pixels, int pitch, SDL_Rect *updated) {
	if (frame->locked) return SDL_SetError("Game frame is locked");
	if (ScreenFrame_rect(frame, rect, updated) != 0) return -1;
	int bytes = updated->w * frame->surface->format->BytesPerPixel;
	if (!pixels || pitch < bytes) return SDL_SetError("Invalid game frame pixels or pitch");
	Uint8 *destination = ScreenFrame_pixels(frame, updated);
	for (int y=0; y<updated->h; y++) {
		memmove(destination + (size_t)y * frame->surface->pitch,
			(const Uint8 *)pixels + (size_t)y * pitch, (size_t)bytes);
	}
	ScreenFrame_makeOpaque(frame, updated);
	return 0;
}

static inline SDL_Surface *ScreenFrame_snapshot(const ScreenFrame *frame) {
	if (!frame->surface || !frame->ready || frame->locked) {
		SDL_SetError("No completed game frame is available");
		return NULL;
	}
	// RGB24 BMPs have no alpha channel for image viewers to misinterpret.
	return SDL_ConvertSurfaceFormat(frame->surface, SDL_PIXELFORMAT_RGB24, 0);
}

#endif
