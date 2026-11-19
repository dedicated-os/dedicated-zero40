#include <assert.h>
#include <stdio.h>
#include "../gametexture.h"
#include "../screenframe.h"

static int fail_create, fail_update, destroyed;
static SDL_Texture *protected_source;

static SDL_Texture *create_texture(SDL_Renderer *r, Uint32 format, int access, int w, int h) {
	if (fail_create) {
		SDL_SetError("simulated allocation failure");
		return NULL;
	}
	return SDL_CreateTexture(r, format, access, w, h);
}

static void destroy_texture(SDL_Texture *texture) {
	assert(texture != protected_source);
	destroyed++;
	SDL_DestroyTexture(texture);
}

static int update_texture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) {
	assert(rect == NULL); // Alternating textures must receive the complete image.
	if (fail_update) return SDL_SetError("simulated upload failure");
	return SDL_UpdateTexture(texture, rect, pixels, pitch);
}

static void check_image(SDL_Renderer *renderer, const GameUpload *upload,
		SDL_Surface *readback, SDL_Surface *expected) {
	assert(SDL_RenderClear(renderer) == 0);
	assert(SDL_RenderCopy(renderer, GameUpload_current(upload), NULL, NULL) == 0);
	assert(SDL_RenderReadPixels(renderer, NULL, readback->format->format,
		readback->pixels, readback->pitch) == 0);
	for (int y=0; y<expected->h; y++) {
		for (int x=0; x<expected->w; x++) {
			Uint32 input, output;
			memcpy(&input, (Uint8 *)expected->pixels + (size_t)y*expected->pitch + x*4, 4);
			memcpy(&output, (Uint8 *)readback->pixels + (size_t)y*readback->pitch + x*4, 4);
			assert(output == (input | 0xff000000u));
		}
	}
	SDL_RenderPresent(renderer);
}

int main(void) {
	assert(SDL_Init(0) == 0);
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 512, 384, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface *readback = SDL_CreateRGBSurfaceWithFormat(0, 512, 384, 32, SDL_PIXELFORMAT_ARGB8888);
	assert(surface && readback);
	SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
	assert(renderer);
	SDL_Texture *source = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
		SDL_TEXTUREACCESS_STREAMING, 512, 384);
	assert(source);
	protected_source = source;
	assert(SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE) == 0);
	assert(SDL_SetTextureScaleMode(source, SDL_ScaleModeLinear) == 0);
	GameUpload upload = {0};
	GameUpload_init(&upload, renderer, source, 1, create_texture, destroy_texture);
	assert(upload.count == 2 && upload.textures[0] == source && upload.textures[1] != source);
	SDL_ScaleMode scale;
	SDL_BlendMode blend;
	assert(SDL_GetTextureScaleMode(upload.textures[1], &scale) == 0 && scale == SDL_ScaleModeLinear);
	assert(SDL_GetTextureBlendMode(upload.textures[1], &blend) == 0 && blend == SDL_BLENDMODE_NONE);
	ScreenFrame frame = {0};
	assert(ScreenFrame_init(&frame, SDL_PIXELFORMAT_RGB888, 512, 384) == 0);
	assert(SDL_FillRect(frame.surface, NULL, 0x003579bd) == 0);
	assert(GameUpload_update(&upload, frame.surface->pixels, frame.surface->pitch, update_texture) == 0);
	assert(GameUpload_current(&upload) == source);
	check_image(renderer, &upload, readback, frame.surface);
	for (int i=0; i<20; i++) {
		SDL_Rect rect = {i*13, i*7, 11, 9};
		// Change only one small region of the CPU image; previously changed
		// regions must survive when the next destination holds an older frame.
		assert(SDL_FillRect(frame.surface, &rect, (Uint32)(0x00010305u*(i+1))) == 0);
		SDL_Texture *previous = GameUpload_current(&upload);
		assert(GameUpload_update(&upload, frame.surface->pixels, frame.surface->pitch, update_texture) == 0);
		assert(GameUpload_current(&upload) != previous);
		check_image(renderer, &upload, readback, frame.surface);
	}
	frame.ready = 1;
	SDL_Surface *completed = ScreenFrame_snapshot(&frame);
	assert(completed && completed->w == 512 && completed->h == 384);
	SDL_Texture *last_good = GameUpload_current(&upload);
	int next = upload.next;
	fail_update = 1;
	assert(GameUpload_update(&upload, frame.surface->pixels, frame.surface->pitch, update_texture) < 0);
	assert(GameUpload_current(&upload) == last_good && upload.next == next);
	check_image(renderer, &upload, readback, frame.surface);
	fail_update = 0;
	assert(SDL_FillRect(frame.surface, NULL, 0x009abcde) == 0);
	assert(GameUpload_update(&upload, frame.surface->pixels, frame.surface->pitch, update_texture) == 0);
	check_image(renderer, &upload, readback, frame.surface);
	GameUpload_reset(&upload, destroy_texture);
	assert(destroyed == 1 && !upload.count && !upload.textures[0] && !upload.textures[1]);
	GameUpload_reset(&upload, destroy_texture);
	assert(destroyed == 1);

	fail_create = 1;
	GameUpload_init(&upload, renderer, source, 1, create_texture, destroy_texture);
	assert(upload.count == 1 && GameUpload_current(&upload) == source);
	assert(GameUpload_update(&upload, frame.surface->pixels, frame.surface->pitch, update_texture) == 0);
	check_image(renderer, &upload, readback, frame.surface);
	fail_create = 0;
	GameUpload_init(&upload, renderer, source, 0, create_texture, destroy_texture);
	assert(upload.count == 1 && !upload.textures[1]);
	GameUpload_init(&upload, renderer, source, 1, create_texture, destroy_texture);
	assert(upload.count == 2);
	GameUpload_init(&upload, renderer, source, 0, create_texture, destroy_texture);
	assert(upload.count == 1 && destroyed == 2);
	GameUpload_reset(&upload, destroy_texture);
	Uint32 format;
	assert(SDL_QueryTexture(source, &format, NULL, NULL, NULL) == 0 && format == SDL_PIXELFORMAT_RGB888);
	SDL_FreeSurface(completed);
	ScreenFrame_reset(&frame);
	SDL_DestroyTexture(source);
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(readback);
	SDL_FreeSurface(surface);
	SDL_Quit();
	puts("gameupload: alternating latest frames, partial-image preservation, opaque RGB, filter preservation, upload/allocation failures and texture ownership passed");
	return 0;
}
