#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../screenframe.h"

static const char *driver_name = "opengles2";
static int advertise_xrgb = 1, info_failure, fail_xrgb, fail_all;
static Uint32 created_formats[2];
static int create_calls;

static int fake_info(SDL_Renderer *renderer, SDL_RendererInfo *info) {
	(void)renderer;
	if (info_failure) return -1;
	memset(info, 0, sizeof(*info));
	info->name = driver_name;
	info->num_texture_formats = advertise_xrgb ? 2 : 1;
	info->texture_formats[0] = SDL_PIXELFORMAT_ARGB8888;
	info->texture_formats[1] = SDL_PIXELFORMAT_RGB888;
	return 0;
}

#define SDL_GetRendererInfo fake_info
#include "../gametexture.h"
#undef SDL_GetRendererInfo

static SDL_Texture *create_texture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h) {
	assert(create_calls < 2);
	created_formats[create_calls++] = format;
	if (fail_all || (fail_xrgb && format == SDL_PIXELFORMAT_RGB888)) {
		SDL_SetError("simulated texture allocation failure");
		return NULL;
	}
	return SDL_CreateTexture(renderer, format, access, w, h);
}

static SDL_Texture *make_texture(SDL_Renderer *renderer, Uint32 requested, int access,
		int w, int h, int enabled, Uint32 expected, int calls) {
	create_calls = 0;
	Uint32 actual = 0;
	SDL_Texture *texture = GameTexture_create(renderer, requested, access, w, h,
		enabled, &actual, create_texture);
	assert(create_calls == calls && actual == expected);
	assert(texture || fail_all);
	if (texture) {
		Uint32 queried;
		assert(SDL_QueryTexture(texture, &queried, NULL, NULL, NULL) == 0);
		assert(queried == expected);
	}
	return texture;
}

static void check_opaque_pixels(SDL_Renderer *renderer, SDL_Texture *texture) {
	ScreenFrame frame = {0};
	assert(ScreenFrame_init(&frame, SDL_PIXELFORMAT_RGB888, 512, 384) == 0);
	assert(frame.surface->format->Amask == 0 && frame.surface->format->BytesPerPixel == 4);
	SDL_PixelFormat *argb = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
	assert(argb && argb->Rmask == frame.surface->format->Rmask &&
		argb->Gmask == frame.surface->format->Gmask && argb->Bmask == frame.surface->format->Bmask);
	SDL_FreeFormat(argb);
	void *pixels;
	int pitch;
	assert(ScreenFrame_lock(&frame, NULL, &pixels, &pitch) == 0);
	for (int y=0; y<384; y++) {
		for (int x=0; x<512; x++) {
			// The same RGB with DraStic's broken 2x alpha pattern, including
			// entire runs of zero alpha. The unused byte must remain untouched.
			Uint32 pixel = 0x003579bd | ((x%16 < 8 && x%2 == 0) ? 0xff000000u : 0);
			memcpy((Uint8 *)pixels + (size_t)y*pitch + x*4, &pixel, 4);
		}
	}
	ScreenFrame_makeOpaque(&frame, &frame.locked_rect);
	Uint32 raw;
	memcpy(&raw, (Uint8 *)pixels + 8*4, 4);
	assert(raw == 0x003579bd); // No CPU alpha rewrite for XRGB.
	assert(SDL_UpdateTexture(texture, NULL, pixels, pitch) == 0);
	frame.locked = 0;
	frame.ready = 1;
	assert(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) == 0);
	assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) == 0);
	assert(SDL_RenderClear(renderer) == 0 && SDL_RenderCopy(renderer, texture, NULL, NULL) == 0);
	SDL_Surface *output = SDL_CreateRGBSurfaceWithFormat(0, 512, 384, 32, SDL_PIXELFORMAT_ARGB8888);
	assert(output);
	assert(SDL_RenderReadPixels(renderer, NULL, output->format->format, output->pixels, output->pitch) == 0);
	SDL_Surface *snapshot = ScreenFrame_snapshot(&frame);
	assert(snapshot && snapshot->format->Amask == 0);
	// Exercise the BMP path as well: XRGB's unused bytes must never become
	// transparency or black stripes in either a display or a saved image.
	const int capacity = 512*384*4 + 4096;
	void *bmp = SDL_malloc(capacity);
	assert(bmp);
	SDL_RWops *rw = SDL_RWFromMem(bmp, capacity);
	assert(rw && SDL_SaveBMP_RW(snapshot, rw, 0) == 0);
	assert(SDL_RWseek(rw, 0, RW_SEEK_SET) == 0);
	SDL_Surface *loaded = SDL_LoadBMP_RW(rw, 1);
	assert(loaded);
	SDL_Surface *saved = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
	assert(saved);
	for (int y=0; y<384; y++) {
		for (int x=0; x<512; x++) {
			Uint32 displayed, stored;
			memcpy(&displayed, (Uint8 *)output->pixels + (size_t)y*output->pitch + x*4, 4);
			memcpy(&stored, (Uint8 *)saved->pixels + (size_t)y*saved->pitch + x*4, 4);
			assert(displayed == 0xff3579bd && stored == 0xff3579bd);
		}
	}
	SDL_FreeSurface(saved);
	SDL_FreeSurface(loaded);
	SDL_free(bmp);
	SDL_FreeSurface(snapshot);
	SDL_FreeSurface(output);
	ScreenFrame_reset(&frame);
}

int main(void) {
	assert(SDL_Init(0) == 0);
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 512, 384, 32, SDL_PIXELFORMAT_ARGB8888);
	assert(surface);
	SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
	assert(renderer);
	SDL_Texture *texture = make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_RGB888, 1);
	check_opaque_pixels(renderer, texture);
	SDL_DestroyTexture(texture);
	// Only the validated Hi-Res ARGB streaming path may use the new format.
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 0, SDL_PIXELFORMAT_ARGB8888, 1));
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 256, 192, 1, SDL_PIXELFORMAT_ARGB8888, 1));
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_ABGR8888, 1));
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_TARGET, 512, 384, 1, SDL_PIXELFORMAT_ARGB8888, 1));
	driver_name = "software";
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_ARGB8888, 1));
	driver_name = "opengles2";
	advertise_xrgb = 0;
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_ARGB8888, 1));
	advertise_xrgb = 1;
	info_failure = 1;
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_ARGB8888, 1));
	info_failure = 0;
	fail_xrgb = 1;
	SDL_DestroyTexture(make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_ARGB8888, 2));
	assert(created_formats[0] == SDL_PIXELFORMAT_RGB888 && created_formats[1] == SDL_PIXELFORMAT_ARGB8888);
	fail_all = 1;
	assert(!make_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384, 1, SDL_PIXELFORMAT_ARGB8888, 2));
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(surface);
	SDL_Quit();
	puts("gametexture: Hi-Res format selection, guarded fallback, corrupt-alpha display and BMP preservation passed (software renderer)");
	return 0;
}
