#ifndef DRASTIC_GAMETEXTURE_H
#define DRASTIC_GAMETEXTURE_H

#include <SDL2/SDL.h>
#include <string.h>

typedef SDL_Texture *(*GameTextureCreate)(SDL_Renderer *, Uint32, int, int, int);
typedef void (*GameTextureDestroy)(SDL_Texture *);
typedef int (*GameTextureUpdate)(SDL_Texture *, const SDL_Rect *, const void *, int);

static inline SDL_Texture *GameTexture_create(SDL_Renderer *renderer, Uint32 format,
		int access, int w, int h, int allow_opaque, Uint32 *upload_format,
		GameTextureCreate create) {
	*upload_format = format;
	if (allow_opaque && format == SDL_PIXELFORMAT_ARGB8888 &&
		access == SDL_TEXTUREACCESS_STREAMING && w == 512 && h == 384) {
		SDL_RendererInfo info;
		memset(&info, 0, sizeof(info));
		if (SDL_GetRendererInfo(renderer, &info) == 0 && info.name &&
			strcmp(info.name, "opengles2") == 0) {
			for (Uint32 i=0; i<info.num_texture_formats; i++) {
				if (info.texture_formats[i] != SDL_PIXELFORMAT_RGB888) continue;
				// SDL RGB888 is 32-bit XRGB, with the same RGB byte layout as
				// ARGB8888. GLES2's RGB shader outputs alpha=1 even when the
				// bundled DraStic NEON converters write corrupt alpha bytes.
				SDL_Texture *texture = create(renderer, SDL_PIXELFORMAT_RGB888, access, w, h);
				if (texture) {
					*upload_format = SDL_PIXELFORMAT_RGB888;
					return texture;
				}
				SDL_Log("Opaque GLES game texture failed: %s; using CPU alpha correction", SDL_GetError());
				break;
			}
		}
	}
	return create(renderer, format, access, w, h);
}

// The first texture is owned by DraStic. Only the extra texture is ours.
// Rotation changes which image we draw, without changing DraStic's handle.
typedef struct {
	SDL_Texture *textures[2];
	int count, current, next;
} GameUpload;

static inline void GameUpload_reset(GameUpload *upload, GameTextureDestroy destroy) {
	if (upload->textures[1]) destroy(upload->textures[1]);
	memset(upload, 0, sizeof(*upload));
}

static inline void GameUpload_init(GameUpload *upload, SDL_Renderer *renderer,
		SDL_Texture *source, int enabled, GameTextureCreate create, GameTextureDestroy destroy) {
	GameUpload_reset(upload, destroy);
	upload->textures[0] = source;
	upload->count = 1;
	if (!enabled) return;
	Uint32 format;
	int w, h;
	SDL_ScaleMode scale;
	if (SDL_QueryTexture(source, &format, NULL, &w, &h) != 0 ||
		SDL_GetTextureScaleMode(source, &scale) != 0) return;
	SDL_Texture *extra = create(renderer, format, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!extra || SDL_SetTextureBlendMode(extra, SDL_BLENDMODE_NONE) != 0 ||
		SDL_SetTextureScaleMode(extra, scale) != 0) {
		SDL_Log("Game upload buffering unavailable: %s; using one texture", SDL_GetError());
		if (extra) destroy(extra);
		return;
	}
	upload->textures[1] = extra;
	upload->count = 2;
}

static inline SDL_Texture *GameUpload_current(const GameUpload *upload) {
	return upload->textures[upload->current];
}

static inline int GameUpload_update(GameUpload *upload, const void *pixels, int pitch,
		GameTextureUpdate update) {
	// Each destination may hold an older frame. Always upload the complete CPU
	// image, including after a partial lock/update, so unchanged areas survive.
	int result = update(upload->textures[upload->next], NULL, pixels, pitch);
	if (result == 0) {
		upload->current = upload->next;
		upload->next = (upload->next + 1) % upload->count;
	}
	return result;
}

#endif
