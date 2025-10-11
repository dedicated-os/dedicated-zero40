#define _GNU_SOURCE // for RTLD_NEXT
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <link.h>
#include <sys/mman.h>
#include <setjmp.h>

#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// --------------------------------------------

#define SDCARD_PATH		"/mnt/SDCARD"
#define GAMES_PATH 		SDCARD_PATH "/games"
#define DRASTIC_PATH 	SDCARD_PATH "/drastic"
#define HOOK_PATH		DRASTIC_PATH "/hook"

// --------------------------------------------

#define JOY_UP 		12
#define JOY_DOWN	15
#define JOY_LEFT	13
#define JOY_RIGHT	14
#define JOY_X		2
#define JOY_B		1
#define JOY_Y		3
#define JOY_A		11
#define JOY_START	9
#define JOY_SELECT	8
#define JOY_MENU	18
#define JOY_L1		4
#define JOY_R1		5
#define JOY_L2		6
#define JOY_R2		7
#define JOY_L3		10

#define JOY_VOLUP	17
#define JOY_VOLDN	16

#define SCAN_POWER	102

// --------------------------------------------

static struct {
	uintptr_t base;
	
	SDL_Renderer* renderer;
	int w;
	int h;
	
	SDL_Texture* top;
	SDL_Texture* bottom;
} ctx = {
	.w=256,
	.h=384,
};

// TODO: move to settings (or ctx for now)?
// both change renderer logical size
static int cropped = 1;
static int gapped = 1;

// --------------------------------------------
// settings
// --------------------------------------------

static int volume = 6; // 0-20
static const uint8_t volume_raw[21] = {
	  0, // mute
	120, // 0
	131, // 11
	138, // 7
	143, // 5
	147, // 4
	151, // 4
	154, // 3
	157, // 3
	160, // 3
	162, // 2
	164, // 2
	166, // 2
	168, // 2
	170, // 2
	172, // 2
	174, // 2
	176, // 2
	178, // 2
	179, // 2
	180, // 1
};
static void set_volume(int value) {
	volume = value;
	char cmd[256];
	sprintf(cmd, "vol %i", volume_raw[value]);
	printf("volume\n");
	system(cmd);
}

static int brightness = 3; // 0 - 10
static const uint8_t brightness_raw[11] = {
	  1, // 0
	  8, // 8
	 16, // 8
	 32, // 16
	 48, // 16
	 72, // 24
	 96, // 24
	128, // 32
	160, // 32
	192, // 32
	255, // 64
};
static void set_brightness(int value) {
	brightness = value;
	char cmd[256];
	sprintf(cmd, "bl %i", brightness_raw[value]);
	printf("brightness\n");
	system(cmd);
}

// --------------------------------------------
// real function handles
// --------------------------------------------

static int  (*real_SDL_Init)(Uint32) = NULL;
static int  (*real_SDL_PollEvent)(SDL_Event*) = NULL;
static int  (*real_SDL_RenderSetLogicalSize)(SDL_Renderer*, int, int) = NULL;
static int  (*real_SDL_RenderCopy)(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) = NULL;
static SDL_Renderer* (*real_SDL_CreateRenderer)(SDL_Window*, int, Uint32) = NULL;
static int  (*real_SDL_UpdateTexture)(SDL_Texture*, const SDL_Rect*, const void*, int) = NULL;
static SDL_Texture* (*real_SDL_CreateTexture)(SDL_Renderer*, Uint32, int, int, int) = NULL;
static void (*real_SDL_DestroyTexture)(SDL_Texture *) = NULL;
static void (*real_SDL_RenderPresent)(SDL_Renderer*) = NULL;
static int (*real_SDL_OpenAudio)(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) = NULL;

static int (*real__libc_start_main)(int (*main)(int,char**,char**), int argc, char **ubp_av, void (*init)(void), void (*fini)(void), void (*rtld_fini)(void), void *stack_end) = NULL;
static void (*real_exit)(int) __attribute__((noreturn)) = NULL;
static void (*real__exit)(int) __attribute__((noreturn)) = NULL;
static int (*real_system)(const char *) = NULL;

// --------------------------------------------
// logging
// --------------------------------------------

static inline void LOG_event(SDL_Event* event) {
	switch (event->type) {
		case SDL_QUIT:
			printf("SDL_PollEvent: QUIT\n"); 
			break;
		
		case SDL_KEYDOWN:
			printf("SDL_PollEvent: KEYDOWN (scancode=%d sym=%d)\n", event->key.keysym.scancode, event->key.keysym.sym);
			break;
		case SDL_KEYUP:
			printf("SDL_PollEvent: KEYUP (scancode=%d sym=%d)\n", event->key.keysym.scancode, event->key.keysym.sym);
			break;
		
		case SDL_MOUSEBUTTONDOWN:
			printf("SDL_PollEvent: MOUSEBUTTONDOWN (button=%d)\n", event->button.button);
			break;
		case SDL_MOUSEBUTTONUP:
			printf("SDL_PollEvent: MOUSEBUTTONUP (button=%d)\n", event->button.button);
			break;
		case SDL_MOUSEMOTION:
			printf("SDL_PollEvent: MOUSEMOTION (x=%d y=%d)\n", event->motion.x, event->motion.y);
			break;
			
		case SDL_FINGERDOWN:
			printf("SDL_PollEvent: FINGERDOWN (fingerId=%lld touchId=%lld x=%f y=%f pressure=%f)\n", (long long)event->tfinger.fingerId, (long long)event->tfinger.touchId, event->tfinger.x, event->tfinger.y, event->tfinger.pressure);
			break;
		case SDL_FINGERUP:
			printf("SDL_PollEvent: FINGERUP (fingerId=%lld touchId=%lld x=%f y=%f pressure=%f)\n", (long long)event->tfinger.fingerId, (long long)event->tfinger.touchId, event->tfinger.x, event->tfinger.y, event->tfinger.pressure);
			break;
		case SDL_FINGERMOTION:
			printf("SDL_PollEvent: FINGERMOTION (fingerId=%lld touchId=%lld x=%f y=%f dx=%f dy=%f pressure=%f)\n", (long long)event->tfinger.fingerId, (long long)event->tfinger.touchId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy, event->tfinger.pressure);
			break;

		case SDL_JOYAXISMOTION:
			// printf("SDL_PollEvent: JOYAXISMOTION which=%d axis=%d value=%d\n", event->jaxis.which, event->jaxis.axis, event->jaxis.value);
			break;
		case SDL_JOYBUTTONDOWN:
			printf("SDL_PollEvent: JOYBUTTONDOWN which=%d button=%d\n", event->jbutton.which, event->jbutton.button);
			break;
		case SDL_JOYBUTTONUP:
			printf("SDL_PollEvent: JOYBUTTONUP which=%d button=%d\n", event->jbutton.which, event->jbutton.button);
			break;
		case SDL_JOYHATMOTION:
			printf("SDL_PollEvent: JOYHATMOTION which=%d hat=%d value=%d\n", event->jhat.which, event->jhat.hat, event->jhat.value);
			break;

		case SDL_JOYDEVICEADDED:
			printf("SDL_PollEvent: JOYDEVICEADDED which=%d\n", event->jdevice.which);
			break;
		case SDL_JOYDEVICEREMOVED:
			printf("SDL_PollEvent: JOYDEVICEREMOVED which=%d\n", event->jdevice.which);
			break;

		case SDL_CONTROLLERAXISMOTION:
			printf("SDL_PollEvent: CONTROLLERAXISMOTION which=%d axis=%d value=%d\n", event->caxis.which, event->caxis.axis, event->caxis.value);
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			printf("SDL_PollEvent: CONTROLLERBUTTONDOWN which=%d button=%d\n", event->cbutton.which, event->cbutton.button);
			break;
		case SDL_CONTROLLERBUTTONUP:
			printf("SDL_PollEvent: CONTROLLERBUTTONUP which=%d button=%d\n", event->cbutton.which, event->cbutton.button);
			break;

		case SDL_CONTROLLERDEVICEADDED:
			printf("SDL_PollEvent: CONTROLLERDEVICEADDED which=%d\n", event->cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			printf("SDL_PollEvent: CONTROLLERDEVICEREMOVED which=%d\n", event->cdevice.which);
			break;
		
		default:
			printf("SDL_PollEvent: type=%d\n", event->type);
			break;
	}
	fflush(stdout);
}
static inline void LOG_rect(const char *label, const SDL_Rect *r) {
	if (r) printf("%s[x=%d y=%d w=%d h=%d] ", label, r->x, r->y, r->w, r->h);
	else   printf("%sNULL ", label);
}
static inline void LOG_render(SDL_Renderer *renderer, SDL_Texture  *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect) {
	float sx=1.0f, sy=1.0f;
	int lw=0, lh=0, outw=0, outh=0;
	SDL_Rect vp; vp.x=vp.y=vp.w=vp.h=0;
	SDL_bool integer = SDL_FALSE;

	SDL_RenderGetScale(renderer, &sx, &sy);				  // 1,1 unless set
	SDL_RenderGetLogicalSize(renderer, &lw, &lh);			// 0,0 if none
	integer = SDL_RenderGetIntegerScale(renderer);		   // SDL_TRUE if forced integer scale
	SDL_RenderGetViewport(renderer, &vp);					// defaults to full output
	SDL_GetRendererOutputSize(renderer, &outw, &outh);	   // e.g. 1024x768

	printf("SDL_RenderCopy: renderer=%p texture=%p ", (void*)renderer, (void*)texture);
	LOG_rect("src=", srcrect);
	LOG_rect("dst=", dstrect);
	printf(" | scale=%.3fx%.3f logical=%dx%d integer=%d viewport=[%d,%d %dx%d] output=%dx%d\n", sx, sy, lw, lh, integer ? 1 : 0, vp.x, vp.y, vp.w, vp.h, outw, outh);
	fflush(stdout);
}
static inline void LOG_texture(SDL_Texture* texture, const SDL_Rect* rect, int pitch) {
	Uint32 fmt = 0; int access = 0, tw = 0, th = 0;
	SDL_QueryTexture(texture, &fmt, &access, &tw, &th);

	int rw = rect ? rect->w : tw;
	int rh = rect ? rect->h : th;
	int bpp = SDL_BYTESPERPIXEL(fmt);
	long long approx = (long long)rh * pitch;
	
	printf("Texture %p ", (void*)texture);
	LOG_rect("rect=", rect);
	printf("pitch=%d format=%s tex=%dx%d bytes=%lld\n", pitch, SDL_GetPixelFormatName(fmt), tw, th, approx);
	fflush(stdout);
}
static const char* get_access_name(int access) {
	switch (access) {
		case SDL_TEXTUREACCESS_STATIC:   return "STATIC";
		case SDL_TEXTUREACCESS_STREAMING:return "STREAMING";
		case SDL_TEXTUREACCESS_TARGET:   return "TARGET";
		default:						 return "UNKNOWN";
	}
}
static void hexdump(const void *ptr, size_t len) {
	const unsigned char *p = (const unsigned char *)ptr;
	for (size_t i = 0; i < len; i += 16) {
		printf("%08zx  ", i);
		for (size_t j = 0; j < 16; ++j) {
			if (i + j < len) printf("%02X ", p[i + j]);
			else			 printf("   ");
		}
		printf(" ");
		for (size_t j = 0; j < 16 && i + j < len; ++j) {
			unsigned char c = p[i + j];
			printf("%c", (c >= 32 && c < 127) ? c : '.');
		}
		printf("\n");
	}
	fflush(stdout);
}

// --------------------------------------------
// drastic shims
// --------------------------------------------

#define PTR_AT(ptr) (void*)(*(uintptr_t *)(ptr))
#define GET_PFN(ptr) (void*)((ptr))

typedef int (*drastic_main_t)(int,char**,char**);
typedef void (*drastic_quit_t)(void*);
typedef void (*drastic_reset_system_t)(void*);
typedef int32_t (*drastic_load_state_t)(void *, const char *, uint16_t *, uint16_t *, uint32_t);
typedef int32_t (*drastic_save_state_t)(void *, const char *, char *, uint16_t *, uint16_t *);
typedef int32_t (*drastic_load_nds_t)(void *, const char *);
typedef uint8_t (*drastic_audio_pause_t)(void *);
typedef void (*drastic_audio_unpause_t)(void *);

static inline void* drastic_var_system(void) {
	return PTR_AT(ctx.base + 0x15ff30); // follow arg in Cutter
}
static void drastic_load_nds_and_jump(const char* path) {
	drastic_load_nds_t d_load_nds = GET_PFN(ctx.base + 0x0006fd30); // nm ./drastic | grep load_nds
	drastic_reset_system_t d_reset_system = GET_PFN(ctx.base + 0x0000fd50); // nm ./drastic | grep reset_system
	
	void* sys = drastic_var_system();
	d_load_nds((uint8_t *)sys + 800, path);
	d_reset_system(sys);
	
	jmp_buf *env = (jmp_buf *)((uint8_t *)sys + 0x3b2a840); // from main in Cutter
	longjmp(*env, 1); // no return
}
static void drastic_quit(void) {
	drastic_quit_t d_quit = GET_PFN(ctx.base + 0x0000e8d0); // nm ./drastic | grep quit
	void* sys = drastic_var_system();	
	d_quit(sys);
}
static void drastic_load_state(int slot) {
	drastic_load_state_t d_load_state = GET_PFN(ctx.base + 0x000746f0); // nm ./drastic | grep load_state
	
	char* path = DRASTIC_PATH "/savestates/nsmb_2.dss"; // TODO: create this dynamically

	void* sys = drastic_var_system();	
	d_load_state(sys, path, NULL,NULL,0);
}
static void drastic_save_state(int slot) {
	drastic_save_state_t d_save_state = GET_PFN(ctx.base + 0x00074da0); // nm ./drastic | grep save_state
	
	// TODO: create these dynamically
	char* path = DRASTIC_PATH "/savestates/";
	char* name = "nsmb_2.dss";
	
	void* sys = drastic_var_system();	
	d_save_state(sys, path, name, NULL,NULL);
}
static void drastic_audio_pause(int flag) {
	// not sure this is necessary but
	// it feels like good hygiene?
	void* sys = drastic_var_system();
	void* audio = (uint8_t *)sys + 0x1587000;
	
	if (flag) {
		drastic_audio_pause_t drastic_audio_pause = GET_PFN(ctx.base + 0x0008caa0);
		drastic_audio_pause(audio);
	}
	else {
		drastic_audio_unpause_t drastic_audio_unpause = GET_PFN(ctx.base + 0x0008caf0);
		drastic_audio_unpause(audio);
	}
}

// --------------------------------------------
// support
// --------------------------------------------

static void save_texture_screenshot(SDL_Texture* texture, const char *path) {
	if (!texture || !path) return;

	void *pixels = NULL;
	int pitch = 0;

	int w,h,access;
	Uint32 fmt;
	SDL_QueryTexture(texture, &fmt, &access, &w, &h);
	SDL_LockTexture(texture, NULL, &pixels, &pitch);

	const int depth = SDL_BITSPERPIXEL(fmt);
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, depth, pitch, fmt);
	if (surface) {
		SDL_SaveBMP(surface, path);
		SDL_FreeSurface(surface);
	}

	SDL_UnlockTexture(texture);
}

// this is required to handle timing issues with drastic
// I wonder if the timing varies with rom size...yes
static int loaded = 1;
static int defer = 30; // TODO: varies by title/file size
static int resume = 1;
static int preload_game(void) {
	loaded = 0;
	defer = 30; // TODO: varies by title/file size
	resume = 0;
}
static int preloading_game(void) {
	if (!loaded && !defer) {
		loaded = 1;
		resume = 1;
		defer = 10; // TODO: varies by title/file size, steward uses 15 ticks
		drastic_load_nds_and_jump(GAMES_PATH "/nsmb.nds");
		// drastic_load_nds_and_jump(GAMES_PATH "/nnk.nds");
		return 1; // never returns because the above jumps
	}
	
	if (defer) {
		defer -= 1;
		return 1;
	}
	
	if (resume) {
		resume = 0;
		drastic_load_state(0);
	}
	
	return 0;
}

// --------------------------------------------
// custom menu
// --------------------------------------------

static int Device_handleEvent(SDL_Event* event) {
	static int menu_down = 0;
	static int menu_combo = 0;
	if (event->type==SDL_KEYDOWN) {
		if (event->key.keysym.scancode==SCAN_POWER) {
			drastic_quit();
			return 1; // handled
		}
	}
	else if (event->type==SDL_JOYBUTTONDOWN) {
		if (event->jbutton.button==JOY_MENU) {
			menu_down = 1;
			menu_combo = 0;
		}
		
		if (event->jbutton.button==JOY_VOLUP) {
			if (menu_down) {
				if (brightness<10) {
					set_brightness(brightness+1);
					menu_combo = 1;
					return 1;
				}
			}
			else {
				if (volume<20) {
					set_volume(volume+1);
					return 1;
				}
			}
		}
		else if (event->jbutton.button==JOY_VOLDN) {
			if (menu_down) {
				if (brightness>0) {
					set_brightness(brightness-1);
					menu_combo = 1;
					return 1;
				}
			}
			else {
				if (volume>0) {
					set_volume(volume-1);
					return 1;
				}
			}
		}
	}
	else if (event->type==SDL_JOYBUTTONUP) {
		if (event->jbutton.button==JOY_MENU) {
			menu_down = 0;
			return menu_combo;
		}
	}
	return 0;
}

#define FONT_SCALE 2
static TTF_Font* font;
static void Menu_init(void) {
	SDL_Log("Menu_init");
	
	// called immediately 
	// TODO:
	// load games directory listing
	// figure out which game we're reloading (or first)
	// setup rom_path, rom_name, rom_slot globals
	
	TTF_Init();
	font = TTF_OpenFont(HOOK_PATH "/Inter_24pt-Black.ttf", 24 * FONT_SCALE);
}
static void Menu_quit(void) {
	SDL_Log("Menu_quit");
	TTF_CloseFont(font);
	TTF_Quit();
}
static void Menu_loop(void) {
	drastic_audio_pause(1);
	
	// static int screenshot = 0;
	// char path[256];
	// sprintf(path, "./screenshot-%i-top.bmp", screenshot);
	// save_texture_screenshot(ctx.top, path);
	// sprintf(path, "./screenshot-%i-bot.bmp", screenshot);
	// save_texture_screenshot(ctx.bottom, path);
	// screenshot += 1;
	
	int w = 1; // we can just stretch horizontally on the GPU
	int h = 800;
	SDL_Surface* tmp = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	// SDL_FillRect(surface, NULL, SDL_MapRGBA(surface->format, 0, 0, 0, 128));

	uint32_t* d = tmp->pixels;
	int total = w * h;
	for (int i=0; i<total; i++,d++) {
		*d = ((total - i) * 224 / total) << 24;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(ctx.renderer, tmp);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_FreeSurface(tmp);
	
	char* text = "New Super Mario Bros.";
	tmp = TTF_RenderUTF8_Blended(font, text, (SDL_Color){255,255,255,255});
	int gw,gh;
	gw = tmp->w / FONT_SCALE;
	gh = tmp->h / FONT_SCALE;
	SDL_Texture* game_name = SDL_CreateTextureFromSurface(ctx.renderer, tmp);
	SDL_SetTextureBlendMode(game_name, SDL_BLENDMODE_BLEND);
	SDL_FreeSurface(tmp);
	
	int in_menu = 1;
	SDL_Event event;
	while (in_menu) {
		while (real_SDL_PollEvent(&event)) {
			LOG_event(&event);
			
			if (Device_handleEvent(&event)) continue;
			
			if (event.type==SDL_JOYBUTTONDOWN) {
				if (event.jbutton.button==JOY_B) { // RESET
					preload_game(); // this just queues up a load and jump but doesn't jump itself
					in_menu = 0;
				}
				else if (event.jbutton.button==JOY_A) { // LOAD
					drastic_load_state(0);
					in_menu = 0;
				}
				else if (event.jbutton.button==JOY_X) { // SAVE
					drastic_save_state(0);
					in_menu = 0;
				}
				else if (event.jbutton.button==JOY_Y) { // SWITCH
					
					// loading jumps out of this function so we need to clean up first
					SDL_DestroyTexture(texture);
					
					drastic_audio_pause(0);
					
					// TODO: can this be queued to do at the end of the frame?
					// drastic_load_nds_and_jump(GAMES_PATH "/ba.nds");
					drastic_load_nds_and_jump(GAMES_PATH "/nnk.nds");
				}
			}
			else if (event.type==SDL_JOYBUTTONUP) {
				if (event.jbutton.button==JOY_MENU) {
					in_menu = 0;
					break;
				}
			}
		}
		
		// let the hook position them (for now)
		SDL_RenderCopy(ctx.renderer, ctx.top, NULL, &(SDL_Rect){0,0,256,192});
		SDL_RenderCopy(ctx.renderer, ctx.bottom, NULL, &(SDL_Rect){0,192,256,192});
		real_SDL_RenderCopy(ctx.renderer, texture, NULL, NULL);
		real_SDL_RenderCopy(ctx.renderer, game_name, NULL, &(SDL_Rect){0,0,gw,gh});
		real_SDL_RenderPresent(ctx.renderer);
	}
	
	
	SDL_DestroyTexture(game_name);
	SDL_DestroyTexture(texture);

	drastic_audio_pause(0);
}

// --------------------------------------------
// hook SDL
// --------------------------------------------

int SDL_Init(Uint32 flags) {
	set_brightness(brightness);
	return real_SDL_Init(flags);
}
int SDL_PollEvent(SDL_Event* event) {
	// loop is required to capture button presses
	while (1) {
		int result = real_SDL_PollEvent(event);
		if (!result) return 0;
		
		if (Device_handleEvent(event)) continue;
		
		// if (event->type==SDL_KEYDOWN) {
		// 	if (event->key.keysym.scancode==SCAN_POWER) {
		// 		drastic_quit();
		// 		return 0;
		// 	}
		// }
		if (event->type==SDL_JOYBUTTONDOWN) {
			if (event->jbutton.button==JOY_L2) {
				cropped = !cropped;
				SDL_RenderSetLogicalSize(ctx.renderer,ctx.w,ctx.h);
				continue;
			}
			else if (event->jbutton.button==JOY_R2) {
				gapped = !gapped;
				SDL_RenderSetLogicalSize(ctx.renderer,ctx.w,ctx.h);
				continue;
			}
			else if (event->jbutton.button==JOY_MENU) {
				continue;
			}
		}
		else if (event->type==SDL_JOYBUTTONUP) {
			if (event->jbutton.button==JOY_MENU) {
				Menu_loop();
				continue;
			}
		}
		
		switch (event->type) {
			case SDL_FINGERDOWN:
			case SDL_FINGERUP:
			case SDL_FINGERMOTION: {
				// for some reason drastic isn't expecting normalized coords so convert to window/screen
				int w = 480;
				int h = 800;
				event->tfinger.x *= w;
				event->tfinger.y *= h;
				event->tfinger.dx *= w;
				event->tfinger.dy *= h;
			} break;
		}
		
		return result;
	}
}

int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h) {
	if (cropped) {
		w = 240;
		h = 400;
	}
	else {
		ctx.w = w;
		ctx.h = h;

		if (w==256) {
			h = gapped ? 426 : 384;
		}
	}
	SDL_Log("HOOK: SDL_RenderSetLogicalSize(%d, %d)", w, h);
	int result = real_SDL_RenderSetLogicalSize(renderer, w, h);
	SDL_RenderClear(ctx.renderer);
	SDL_RenderPresent(ctx.renderer);
	SDL_RenderClear(ctx.renderer);
	return result;
}
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture  *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect) {
	// return 1;
	// LOG_render(renderer, texture, srcrect, dstrect);

	if (cropped) {
		// game
		if (dstrect) {
			srcrect = &(SDL_Rect){8,0,240,192};
			int y = dstrect->y;
			if (y>0) y = 400-192;
			
			if (!gapped) {
				if (y==0) y += 8;
				else y -= 8;
			}
			
			dstrect = &(SDL_Rect){0,y,240,192};
			
		}
		// menu, for now
		else {
			srcrect = &(SDL_Rect){48,200,400,400};
			dstrect = &(SDL_Rect){0,0,240,400};
		}
	}
	else {
		// game
		if (dstrect) {
			int y = dstrect->y;
			if (gapped && y>0) {
				y = 426 - 192;
				dstrect = &(SDL_Rect){0,y,256,192};
			}
		}
	}
	
	return real_SDL_RenderCopy(renderer, texture, srcrect, dstrect);
	// return 1;
}

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, int index, Uint32 flags) {
	ctx.renderer = real_SDL_CreateRenderer(window, index, flags);
	return ctx.renderer;
}
void SDL_RenderPresent(SDL_Renderer * renderer) {
	if (preloading_game()) return;

	// real_SDL_RenderCopy(renderer, ctx.top, NULL, &(SDL_Rect){0,0,256,192});
	// real_SDL_RenderCopy(renderer, ctx.bottom, NULL, &(SDL_Rect){0,192,256,192});
	real_SDL_RenderPresent(renderer);
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int type, int w, int h) {
	SDL_Texture *texture = real_SDL_CreateTexture(renderer, format, type, w, h);
	if (type==SDL_TEXTUREACCESS_STREAMING && w==256 && h==192) {
		if (!ctx.top) ctx.top = texture;
		else if (!ctx.bottom) ctx.bottom = texture;
	}
	return texture;
}
void SDL_DestroyTexture(SDL_Texture *texture) {
	if (texture==ctx.top) ctx.top = NULL;
	else if (texture==ctx.bottom) ctx.bottom = NULL;
	
	real_SDL_DestroyTexture(texture);
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
	int result = real_SDL_OpenAudio(desired, obtained);
	set_volume(volume); // must be done here (or later)
	return result;
}

// --------------------------------------------
// hijack main to modify args
// --------------------------------------------

#define DISABLE_LOGGING

#ifdef DISABLE_LOGGING
int __printf_chk(int flag, const char *fmt, ...) {
	return 0;
}
int puts (const char *__s) {
	return 0;
}
#endif

__attribute__((noreturn))
void exit(int status) {
	Menu_quit();
    real_exit(status);
}
__attribute__((noreturn))
void _exit(int status) {
	Menu_quit();
    real__exit(status);
}
int system(const char *command) {
	unsetenv("LD_PRELOAD");
	return real_system(command);
}

static int pick_main(struct dl_phdr_info *i, size_t s, void *out) {
	(void)s;
	if (!i->dlpi_name || !i->dlpi_name[0]) {
		*(uintptr_t *)out = (uintptr_t)i->dlpi_addr;
		return 1;
	}
	return 0;
}
static inline uintptr_t find_exe_base(void) {
	uintptr_t base = 0;
	dl_iterate_phdr(pick_main, &base);
	return base;
}

static void resolve_real(void) {
	// hook glibc? functions
	real__libc_start_main = dlsym(RTLD_NEXT, "__libc_start_main");
	real_exit  = dlsym(RTLD_NEXT, "exit");
	real__exit = dlsym(RTLD_NEXT, "_exit");
	real_system = dlsym(RTLD_NEXT, "system");
	
	// hook SDL functions
	real_SDL_Init = dlsym(RTLD_NEXT, "SDL_Init");
	real_SDL_PollEvent = dlsym(RTLD_NEXT, "SDL_PollEvent");
	real_SDL_RenderSetLogicalSize = dlsym(RTLD_NEXT, "SDL_RenderSetLogicalSize");
	real_SDL_RenderCopy = dlsym(RTLD_NEXT, "SDL_RenderCopy");
	real_SDL_CreateRenderer = dlsym(RTLD_NEXT, "SDL_CreateRenderer");
	real_SDL_DestroyTexture = dlsym(RTLD_NEXT, "SDL_DestroyTexture");
	real_SDL_CreateTexture = dlsym(RTLD_NEXT, "SDL_CreateTexture");
	real_SDL_RenderPresent = dlsym(RTLD_NEXT, "SDL_RenderPresent");
	real_SDL_OpenAudio = dlsym(RTLD_NEXT, "SDL_OpenAudio");
	
	ctx.base = find_exe_base();
	
	printf("resolved real function pointers\n"); fflush(stdout);
}

static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;
static inline void hook(void) {
	pthread_once(&resolve_once, resolve_real);
}

static drastic_main_t drastic_main = NULL;
static int override_args(int argc, char **argv, char **envp) {
	hook();
	Menu_init();
	
	// TODO: pull game from globals set by Menu_init()
	char **new_argv = (char**)malloc(3 * sizeof(char*));
	new_argv[0] = argv[0];
	new_argv[1] = GAMES_PATH "/nsmb.nds";
	new_argv[2] = NULL;

	return drastic_main(2, new_argv, envp);
}

int __libc_start_main(int (*main)(int,char**,char**), int argc, char **ubp_av, void (*init)(void), void (*fini)(void), void (*rtld_fini)(void), void *stack_end) {
	hook();
	drastic_main = main;
	return real__libc_start_main(override_args, argc, ubp_av, init, fini, rtld_fini, stack_end);
}