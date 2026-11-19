#ifndef DRASTIC_DISPLAYMODE_H
#define DRASTIC_DISPLAYMODE_H

#include <SDL2/SDL.h>
#include <string.h>

typedef enum {
	DISPLAY_OFF,
	DISPLAY_X2,
	DISPLAY_HIRES,
	DISPLAY_MODE_COUNT
} DisplayMode;

static inline int DisplayMode_valid(int mode) {
	return mode >= DISPLAY_OFF && mode < DISPLAY_MODE_COUNT;
}

static inline DisplayMode DisplayMode_next(DisplayMode mode) {
	return (DisplayMode)((mode + 1) % DISPLAY_MODE_COUNT);
}

static inline const char *DisplayMode_label(DisplayMode mode) {
	static const char *const labels[] = {"Off", "x2", "Hi-Res"};
	return DisplayMode_valid(mode) ? labels[mode] : labels[DISPLAY_OFF];
}

static inline int DisplayMode_needsRestart(DisplayMode before, DisplayMode after) {
	return (before == DISPLAY_HIRES) != (after == DISPLAY_HIRES);
}

typedef int (*DisplayCopy)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);

typedef struct {
	SDL_Texture *source;
	SDL_Texture *doubled;
	SDL_ScaleMode original_scale;
	SDL_ScaleMode current_scale;
	int disabled;
} DisplayScreen;

static inline void DisplayScreen_reset(DisplayScreen *screen) {
	if (screen->doubled) SDL_DestroyTexture(screen->doubled);
	memset(screen, 0, sizeof(*screen));
}

static inline void DisplayScreen_init(DisplayScreen *screen, SDL_Texture *source) {
	DisplayScreen_reset(screen);
	screen->source = source;
	SDL_GetTextureScaleMode(source, &screen->original_scale);
	screen->current_scale = screen->original_scale;
}

static inline int DisplayScreen_copy(DisplayScreen *screen, SDL_Renderer *renderer,
		DisplayMode mode, int cropped, const SDL_Rect *destination, DisplayCopy copy) {
	SDL_ScaleMode wanted =
		(mode == DISPLAY_X2 || (mode == DISPLAY_OFF && cropped)) ?
		SDL_ScaleModeNearest : screen->original_scale;
	// GLES2 sets texture parameters in the driver on every call, even unchanged.
	if (wanted != screen->current_scale) {
		if (SDL_SetTextureScaleMode(screen->source, wanted) != 0) return -1;
		screen->current_scale = wanted;
	}
	if (mode != DISPLAY_X2 || cropped || screen->disabled)
		return copy(renderer, screen->source, NULL, destination);

	// Uncropped x2: nearest 256x192 -> 512x384, then linear -> 480x360.
	// Reuse the GPU target; screenshots continue to use the native CPU frame.
	if (!screen->doubled) {
		Uint32 format;
		int width, height;
		if (SDL_QueryTexture(screen->source, &format, NULL, &width, &height) != 0)
			return -1;
		if (width != 256 || height != 192)
			return copy(renderer, screen->source, NULL, destination);
		screen->doubled = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, 512, 384);
		if (!screen->doubled) {
			SDL_Log("x2 render target unavailable; using direct nearest scaling: %s", SDL_GetError());
			screen->disabled = 1;
			return copy(renderer, screen->source, NULL, destination);
		}
		SDL_SetTextureBlendMode(screen->doubled, SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(screen->doubled, SDL_ScaleModeLinear);
	}
	SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
	if (SDL_SetRenderTarget(renderer, screen->doubled) != 0) {
		SDL_Log("x2 render target failed; using direct nearest scaling: %s", SDL_GetError());
		screen->disabled = 1;
		return copy(renderer, screen->source, NULL, destination);
	}
	int result = copy(renderer, screen->source, NULL, NULL);
	if (SDL_SetRenderTarget(renderer, previous_target) != 0) return -1;
	if (result != 0) return result;
	return copy(renderer, screen->doubled, NULL, destination);
}

#endif
