#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../screenframe.h"

static Uint32 pixel_at(SDL_Surface *surface, int x, int y) {
	Uint32 pixel = 0;
	int bpp = surface->format->BytesPerPixel;
	const Uint8 *p = (const Uint8 *)surface->pixels + (size_t)y * surface->pitch + x * bpp;
	if (bpp == 3) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		pixel = ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | p[2];
#else
		pixel = p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
#endif
	} else memcpy(&pixel, p, (size_t)bpp);
	return pixel;
}

static void check_format(Uint32 format, int width, int height) {
	ScreenFrame frame = {0};
	assert(ScreenFrame_init(&frame, format, width, height) == 0);
	assert(ScreenFrame_snapshot(&frame) == NULL);
	int bpp = frame.surface->format->BytesPerPixel;
	int input_pitch = width * bpp + 13;
	Uint8 *allocation = malloc((size_t)height * input_pitch + 1);
	assert(allocation);
	Uint8 *input = allocation + 1; // Deliberately unaligned, padded source rows.
	memset(input, 0xa5, (size_t)height * input_pitch);
	for (int y=0; y<height; y++) {
		for (int x=0; x<width; x++) {
			// Include the alternating/zero alpha written by DraStic's 2x NEON path.
			Uint8 alpha = (x % 16 < 8 && x % 2 == 0) ? 255 : 0;
			Uint32 pixel = SDL_MapRGBA(frame.surface->format, (Uint8)(x*3),
				(Uint8)(y*5), (Uint8)(x+y), alpha);
			Uint8 *p = input + (size_t)y * input_pitch + x * bpp;
			if (bpp == 3) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				p[0] = (Uint8)(pixel >> 16); p[1] = (Uint8)(pixel >> 8); p[2] = (Uint8)pixel;
#else
				p[0] = (Uint8)pixel; p[1] = (Uint8)(pixel >> 8); p[2] = (Uint8)(pixel >> 16);
#endif
			} else memcpy(p, &pixel, (size_t)bpp);
		}
	}
	Uint8 *unchanged = malloc((size_t)height * input_pitch);
	assert(unchanged);
	memcpy(unchanged, input, (size_t)height * input_pitch);
	SDL_Rect updated;
	assert(ScreenFrame_update(&frame, NULL, input, input_pitch, &updated) == 0);
	assert(memcmp(input, unchanged, (size_t)height * input_pitch) == 0);
	for (int y=0; y<height; y++) {
		for (int x=0; x<width; x++) {
			Uint8 r, g, b, a;
			SDL_GetRGBA(pixel_at(frame.surface, x, y), frame.surface->format, &r, &g, &b, &a);
			assert(a == 255);
			// Compare against the encoded source to account for RGB565 quantization.
			Uint32 original = SDL_MapRGBA(frame.surface->format, (Uint8)(x*3),
				(Uint8)(y*5), (Uint8)(x+y), 255);
			assert(pixel_at(frame.surface, x, y) == original);
		}
	}

	// Partial locks use the full image pitch and preserve surrounding pixels.
	SDL_Rect rect = {1, 1, width-2, height-2};
	Uint32 untouched = pixel_at(frame.surface, 0, 0);
	void *pixels;
	int pitch;
	assert(ScreenFrame_lock(&frame, &rect, &pixels, &pitch) == 0);
	assert(pitch == frame.surface->pitch);
	assert(pixels == (Uint8 *)frame.surface->pixels + pitch + bpp);
	assert(ScreenFrame_snapshot(&frame) == NULL);
	assert(ScreenFrame_lock(&frame, NULL, &pixels, &pitch) != 0);
	assert(ScreenFrame_update(&frame, NULL, input, input_pitch, &updated) != 0);
	for (int y=0; y<rect.h; y++) memset((Uint8 *)pixels + (size_t)y * pitch, 0, (size_t)rect.w*bpp);
	ScreenFrame_makeOpaque(&frame, &frame.locked_rect);
	frame.locked = 0;
	assert(pixel_at(frame.surface, 0, 0) == untouched);
	assert(pixel_at(frame.surface, 1, 1) == frame.surface->format->Amask);
	// Partial SDL_UpdateTexture source rows have their own independent pitch.
	assert(ScreenFrame_update(&frame, &rect, input, input_pitch, &updated) == 0);
	assert(pixel_at(frame.surface, 0, 0) == untouched);
	assert(pixel_at(frame.surface, 1, 1) == untouched);

	frame.ready = 1;
	SDL_Surface *snapshot = ScreenFrame_snapshot(&frame);
	assert(snapshot && snapshot->format->Amask == 0);
	assert(snapshot->w == width && snapshot->h == height);
	// Include enough space for row padding and the BMP header.
	size_t bmp_capacity = (size_t)width * height * 4 + 4096;
	void *bmp = malloc(bmp_capacity);
	assert(bmp);
	SDL_RWops *rw = SDL_RWFromMem(bmp, (int)bmp_capacity);
	assert(rw && SDL_SaveBMP_RW(snapshot, rw, 0) == 0);
	assert(SDL_RWseek(rw, 0, RW_SEEK_SET) == 0);
	SDL_Surface *loaded = SDL_LoadBMP_RW(rw, 1);
	assert(loaded && loaded->format->Amask == 0);
	SDL_Surface *rgb = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGB24, 0);
	assert(rgb && rgb->w == width && rgb->h == height);
	for (int y=0; y<height; y++)
		assert(memcmp((Uint8 *)rgb->pixels + (size_t)y*rgb->pitch,
			(Uint8 *)snapshot->pixels + (size_t)y*snapshot->pitch, (size_t)width*3) == 0);
	SDL_FreeSurface(rgb);
	SDL_FreeSurface(loaded);
	SDL_FreeSurface(snapshot);
	free(bmp);
	free(unchanged);
	free(allocation);
	assert(ScreenFrame_lock(&frame, &(SDL_Rect){width-1, 0, 2, 1}, &pixels, &pitch) != 0);
	assert(ScreenFrame_update(&frame, NULL, "", 1, &updated) != 0);
	ScreenFrame_reset(&frame);
	assert(!frame.surface && !frame.ready && !frame.locked);
	assert(ScreenFrame_snapshot(&frame) == NULL);
	assert(ScreenFrame_init(&frame, format, 256, 192) == 0);
	assert(ScreenFrame_snapshot(&frame) == NULL); // No stale frame after recreation.
	ScreenFrame_reset(&frame);
}

int main(void) {
	const Uint32 formats[] = {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_ABGR8888,
		SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_BGRA8888, SDL_PIXELFORMAT_RGB888,
		SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_RGBA4444,
		SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_BGR24};
	for (size_t i=0; i<sizeof(formats)/sizeof(formats[0]); i++) {
		check_format(formats[i], 512, 384);
		check_format(formats[i], 257, 193); // Row padding in 16/24-bit SDL surfaces.
	}
	puts("screenframe: alpha correction, RGB preservation, partial updates, BMP round-trip and recreation passed");
	return 0;
}
