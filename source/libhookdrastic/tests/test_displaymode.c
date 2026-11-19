#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

static int scale_changes;
static int tracked_scale_mode(SDL_Texture *texture, SDL_ScaleMode scale) {
	scale_changes++;
	return SDL_SetTextureScaleMode(texture, scale);
}
#define SDL_SetTextureScaleMode tracked_scale_mode
#include "../displaymode.h"
#undef SDL_SetTextureScaleMode

static Uint32 source_pixel(int x, int y) {
	return 0xff000000u | ((Uint32)(x & 255) << 16) |
		((Uint32)(y & 255) << 8) | ((x/3 + y/5) % 2 ? 255u : 0u);
}

static void check_cycle(void) {
	DisplayMode mode = DISPLAY_OFF;
	assert(strcmp(DisplayMode_label(mode), "Off") == 0);
	mode = DisplayMode_next(mode);
	assert(mode == DISPLAY_X2 && strcmp(DisplayMode_label(mode), "x2") == 0);
	assert(!DisplayMode_needsRestart(DISPLAY_OFF, mode));
	mode = DisplayMode_next(mode);
	assert(mode == DISPLAY_HIRES && strcmp(DisplayMode_label(mode), "Hi-Res") == 0);
	assert(DisplayMode_needsRestart(DISPLAY_X2, mode));
	mode = DisplayMode_next(mode);
	assert(mode == DISPLAY_OFF && DisplayMode_needsRestart(DISPLAY_HIRES, mode));
	assert(!DisplayMode_valid(-1) && !DisplayMode_valid(DISPLAY_MODE_COUNT));
}

static SDL_Texture *make_source(SDL_Renderer *renderer, int width, int height) {
	Uint32 *pixels = malloc((size_t)width * height * sizeof(*pixels));
	assert(pixels);
	for (int y=0; y<height; y++)
		for (int x=0; x<width; x++) pixels[y*width+x] = source_pixel(x, y);
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, width, height);
	assert(texture && SDL_UpdateTexture(texture, NULL, pixels, width*4) == 0);
	assert(SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear) == 0);
	free(pixels);
	return texture;
}

static void check_rendering(void) {
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 800, 32, SDL_PIXELFORMAT_ARGB8888);
	assert(surface);
	SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
	assert(renderer);
	SDL_Texture *source = make_source(renderer, 256, 192);
	DisplayScreen screen = {0};
	DisplayScreen_init(&screen, source);
	SDL_Rect cropped = {-16, 0, 512, 384};
	// Both native-resolution modes must preserve exact source pixels when cropped.
	for (DisplayMode mode=DISPLAY_OFF; mode<=DISPLAY_X2; mode++) {
		assert(DisplayScreen_copy(&screen, renderer, mode, 1, &cropped, SDL_RenderCopy) == 0);
		SDL_RenderPresent(renderer);
		assert(!screen.doubled); // Cropped Off/x2 need only one nearest copy.
		for (int y=0; y<384; y++) {
			const Uint32 *row = (Uint32 *)((Uint8 *)surface->pixels + (size_t)y*surface->pitch);
			for (int x=0; x<480; x++) assert(row[x] == source_pixel((x+16)/2, y/2));
		}
	}
	assert(scale_changes == 1); // Off/x2 crop ON share nearest; no repeated driver changes.

	// The second pass must use linear filtering, and restore the main target.
	SDL_Rect full = {0, 440, 480, 360};
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_X2, 0, &full, SDL_RenderCopy) == 0);
	assert(screen.doubled && SDL_GetRenderTarget(renderer) == NULL);
	SDL_Texture *intermediate = screen.doubled;
	SDL_ScaleMode scale;
	assert(SDL_GetTextureScaleMode(source, &scale) == 0 && scale == SDL_ScaleModeNearest);
	assert(SDL_GetTextureScaleMode(intermediate, &scale) == 0 && scale == SDL_ScaleModeLinear);
	assert(SDL_SetRenderTarget(renderer, intermediate) == 0);
	Uint32 *pixels = malloc(512*384*sizeof(*pixels));
	assert(pixels);
	assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 512*4) == 0);
	for (int y=0; y<384; y++)
		for (int x=0; x<512; x++) assert(pixels[y*512+x] == source_pixel(x/2, y/2));
	assert(SDL_SetRenderTarget(renderer, NULL) == 0);

	// An independently constructed doubled image produces the same final output.
	SDL_Surface *reference = SDL_CreateRGBSurfaceWithFormat(0, 480, 800, 32, SDL_PIXELFORMAT_ARGB8888);
	assert(reference);
	SDL_Renderer *reference_renderer = SDL_CreateSoftwareRenderer(reference);
	assert(reference_renderer);
	SDL_Texture *reference_texture = SDL_CreateTexture(reference_renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384);
	assert(reference_texture);
	for (int y=0; y<384; y++)
		for (int x=0; x<512; x++) pixels[y*512+x] = source_pixel(x/2, y/2);
	assert(SDL_UpdateTexture(reference_texture, NULL, pixels, 512*4) == 0);
	assert(SDL_SetTextureScaleMode(reference_texture, SDL_ScaleModeLinear) == 0);
	assert(SDL_RenderCopy(reference_renderer, reference_texture, NULL, &full) == 0);
	SDL_RenderPresent(reference_renderer);
	SDL_RenderPresent(renderer);
	for (int y=440; y<800; y++)
		assert(memcmp((Uint8 *)surface->pixels + (size_t)y*surface->pitch,
			(Uint8 *)reference->pixels + (size_t)y*reference->pitch, 480*4) == 0);
	free(pixels);
	SDL_DestroyTexture(reference_texture);
	SDL_DestroyRenderer(reference_renderer);
	SDL_FreeSurface(reference);

	// Reuse the target, preserve rendering state and refresh changed source data.
	SDL_Rect viewport = {7, 9, 450, 750};
	SDL_Rect clip = {2, 3, 400, 700};
	assert(SDL_RenderSetViewport(renderer, &viewport) == 0);
	assert(SDL_RenderSetClipRect(renderer, &clip) == 0);
	assert(SDL_SetRenderDrawColor(renderer, 1, 2, 3, 4) == 0);
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_X2, 0, &full, SDL_RenderCopy) == 0);
	assert(screen.doubled == intermediate && SDL_GetRenderTarget(renderer) == NULL);
	SDL_Rect actual;
	SDL_RenderGetViewport(renderer, &actual);
	assert(memcmp(&actual, &viewport, sizeof(actual)) == 0);
	SDL_RenderGetClipRect(renderer, &actual);
	assert(SDL_RenderIsClipEnabled(renderer) && memcmp(&actual, &clip, sizeof(actual)) == 0);
	Uint8 r, g, b, a;
	assert(SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a) == 0);
	assert(r == 1 && g == 2 && b == 3 && a == 4);
	assert(SDL_RenderSetViewport(renderer, NULL) == 0);
	assert(SDL_RenderSetClipRect(renderer, NULL) == 0);
	Uint32 replacement = 0xff123456u;
	assert(SDL_UpdateTexture(source, &(SDL_Rect){0, 0, 1, 1}, &replacement, 4) == 0);
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_X2, 0, &full, SDL_RenderCopy) == 0);
	assert(SDL_SetRenderTarget(renderer, intermediate) == 0);
	Uint32 block[4];
	assert(SDL_RenderReadPixels(renderer, &(SDL_Rect){0, 0, 2, 2}, SDL_PIXELFORMAT_ARGB8888, block, 8) == 0);
	for (int i=0; i<4; i++) assert(block[i] == replacement);
	assert(SDL_SetRenderTarget(renderer, NULL) == 0);

	// Off keeps nearest when cropped and restores the original filter when fitted.
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_OFF, 1, &cropped, SDL_RenderCopy) == 0);
	assert(SDL_GetTextureScaleMode(source, &scale) == 0 && scale == SDL_ScaleModeNearest);
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_OFF, 0, &full, SDL_RenderCopy) == 0);
	assert(SDL_GetTextureScaleMode(source, &scale) == 0 && scale == SDL_ScaleModeLinear);
	int previous_scale_changes = scale_changes;
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_OFF, 0, &full, SDL_RenderCopy) == 0);
	assert(scale_changes == previous_scale_changes);
	// A failed target falls back to nearest.
	screen.disabled = 1;
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_X2, 0, &full, SDL_RenderCopy) == 0);
	assert(SDL_GetTextureScaleMode(source, &scale) == 0 && scale == SDL_ScaleModeNearest);

	SDL_Texture *hires = make_source(renderer, 512, 384);
	DisplayScreen_init(&screen, hires);
	assert(!screen.doubled && !screen.disabled);
	assert(DisplayScreen_copy(&screen, renderer, DISPLAY_HIRES, 1, &cropped, SDL_RenderCopy) == 0);
	assert(SDL_GetTextureScaleMode(hires, &scale) == 0 && scale == SDL_ScaleModeLinear);
	DisplayScreen_reset(&screen);
	assert(!screen.source && !screen.doubled);
	SDL_DestroyTexture(hires);
	SDL_DestroyTexture(source);
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(surface);
}

int main(void) {
	check_cycle();
	check_rendering();
	puts("displaymode: cycle, restart policy, nearest x2/crop, two-pass scaling, state restoration, live updates and cleanup passed");
	return 0;
}
