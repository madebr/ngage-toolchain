/* @file celeste_SDL3.c
 *
 * A C source port of the original Celeste game,
 * highly optimized for the Nokia N-Gage.
 *
 * Original game by Maddy Makes Games.
 * C source port by lemon32767.
 *
 * https://github.com/lemon32767/ccleste
 *
 */

#include <stdio.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "celeste_SDL3.h"
#include "celeste.h"
#include "tilemap.h"

extern SDL_Renderer* renderer;
extern SDL_Window* window;

static SDL_Texture* SDL_screen = NULL; // Render-target for the PICO8 128x128 framebuffer.
static SDL_Texture* frame = NULL;
static SDL_Texture* gfx = NULL;
static SDL_Texture* font = NULL;
static Mix_Chunk* snd[64] = { NULL };
#if ENABLE_MUSIC
static Mix_Music* mus[6] = { NULL };
#endif

static const SDL_Color base_palette_colors[16] =
{
	{ 0x00, 0x00, 0x00 },
	{ 0x1d, 0x2b, 0x53 },
	{ 0x7e, 0x25, 0x53 },
	{ 0x00, 0x87, 0x51 },
	{ 0xab, 0x52, 0x36 },
	{ 0x5f, 0x57, 0x4f },
	{ 0xc2, 0xc3, 0xc7 },
	{ 0xff, 0xf1, 0xe8 },
	{ 0xff, 0x00, 0x4d },
	{ 0xff, 0xa3, 0x00 },
	{ 0xff, 0xec, 0x27 },
	{ 0x00, 0xe4, 0x36 },
	{ 0x29, 0xad, 0xff },
	{ 0x83, 0x76, 0x9c },
	{ 0xff, 0x77, 0xa8 },
	{ 0xff, 0xcc, 0xaa }
};

static SDL_Color palette_colors[16];

#define getcolor(col) palette_colors[(col) % 16]

static Uint16 buttons_state = 0;
static _Bool enable_screenshake = 1;
static _Bool paused = 0;
static void* initial_game_state = NULL;
static void* game_state = NULL;
static Mix_Music* current_music = NULL;
static Mix_Music* game_state_music = NULL;

// On-screen display (for info, such as loading a state, toggling screenshake, toggling fullscreen, etc).
static char osd_text[200] = "";
static int  osd_timer = 0;

static int gettileflag(int tile, int flag);
static void loadbmpscale(char* filename, SDL_Texture** s);

static void Flip();
static void LoadData(void);
static void SetPaletteEntry(unsigned char idx, unsigned char base_idx);
static void RefreshPalette(void);
static void ResetPalette(void);

static void OSDset(const char* fmt, ...);
static void OSDdraw(void);

static void p8_line(int x0, int y0, int x1, int y1, unsigned char color);
static void p8_print(const char* str, int x, int y, int col);
static void p8_rectfill(int x0, int y0, int x1, int y1, int col);
static int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...);
static inline void Xblit(SDL_Texture* src, SDL_Rect* srcrect, SDL_Rect* dstrect, int flipx, int flipy);

#define LOGLOAD(w) SDL_Log("loading %s...", w)
#define LOGDONE() SDL_Log("done")

int Init()
{
	int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...);

	SDL_screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, PICO8_W * SCALE, PICO8_H * SCALE);
	if (!SDL_screen)
	{
		SDL_Log("SDL_CreateTexture: %s\n", SDL_GetError());
		return false;
	}

	SDL_AudioSpec spec;
	spec.channels = 1;
	spec.format = SDL_AUDIO_S16;
	spec.freq = 8000;

	if (!Mix_OpenAudio(0, &spec))
	{
		SDL_Log("Mix_Init: %s", SDL_GetError());
	}
	Mix_Volume(-1, 10);

	ResetPalette();
	SDL_HideCursor();

	SDL_Log("game state size %gkb", Celeste_P8_get_state_size() / 1024.);
	SDL_Log("now loading...");

	LoadData();
	Celeste_P8_set_call_func(pico8emu);

	// For reset.
	initial_game_state = SDL_malloc(Celeste_P8_get_state_size());
	if (initial_game_state)
	{
		Celeste_P8_save_state(initial_game_state);
	}

	Celeste_P8_set_rndseed((unsigned)(time(NULL) + SDL_GetTicks()));

	Celeste_P8_init();

	char tmpath[256];
	SDL_snprintf(tmpath, sizeof(tmpath), "%sdata/frame.bmp", SDL_GetBasePath());

	SDL_Surface* frame_sf = SDL_LoadBMP(tmpath);
	if (!frame_sf)
	{
		SDL_Log("Failed to load image frame.bmp: %s", SDL_GetError());
		return false;
	}
	else
	{
		frame = SDL_CreateTextureFromSurface(renderer, frame_sf);
		if (!frame)
		{
			SDL_Log("Could not create texture from surface: %s", SDL_GetError());
		}
		SDL_DestroySurface(frame_sf);
	}

	return true;
}

SDL_AppResult HandleEvents(SDL_Event* ev)
{
	switch (ev->type)
	{
	case SDL_EVENT_QUIT:
	{
		return SDL_APP_SUCCESS;
	}
	case SDL_EVENT_KEY_DOWN:
	{
		if (ev->key.repeat) // No key repeat.
		{
			break;
		}

		if (ev->key.key == SDLK_SOFTRIGHT) // Do pause.
		{
			if (paused)
			{
				Mix_Resume(-1);
#if ENABLE_MUSIC
				Mix_ResumeMusic();
#endif
			}
			else
			{
				Mix_Pause(-1);
#if ENABLE_MUSIC
				Mix_PauseMusic();
#endif
			}
			paused = !paused;
		}
		else if (ev->key.key == SDLK_SOFTLEFT) // Exit.
		{
			return SDL_APP_SUCCESS;
		}
		else if (ev->key.key == SDLK_HASH) // Toggle FPS.
		{
			SDL_RenderTexture(renderer, frame, NULL, NULL);
		}
		else if (ev->key.key == SDLK_1) // Save state.
		{
			game_state = game_state ? game_state : SDL_malloc(Celeste_P8_get_state_size());
			if (game_state)
			{
				OSDset("save state");
				Celeste_P8_save_state(game_state);
				game_state_music = current_music;

				char tmpath[256];
				SDL_snprintf(tmpath, sizeof(tmpath), "%sceleste.sav", SDL_GetUserFolder(SDL_FOLDER_SAVEDGAMES));
				FILE* savefile = fopen(tmpath, "wb+");
				if (savefile)
				{
					fwrite(game_state, Celeste_P8_get_state_size(), 1, savefile);
					fclose(savefile);
				}
			}
		}
		else if (ev->key.key == SDLK_2) // Load state.
		{
			char tmpath[256];
			SDL_snprintf(tmpath, sizeof(tmpath), "%sceleste.sav", SDL_GetUserFolder(SDL_FOLDER_SAVEDGAMES));
			FILE* savefile = fopen(tmpath, "rb");
			if (savefile)
			{
				if (!game_state)
				{
					game_state = SDL_malloc(Celeste_P8_get_state_size());
					if (!game_state)
					{
						fclose(savefile);
						break;
					}
				}
				if (game_state)
				{
					fread(game_state, Celeste_P8_get_state_size(), 1, savefile);
				}
				fclose(savefile);
			}

			if (game_state)
			{
				OSDset("load state");
				if (paused)
				{
					paused = 0;
					Mix_Resume(-1);
#if ENABLE_MUSIC
					Mix_ResumeMusic();
#endif
				}
				Celeste_P8_load_state(game_state);
				if (current_music != game_state_music)
				{
#if ENABLE_MUSIC
					Mix_HaltMusic();
#endif
					current_music = game_state_music;
					if (game_state_music)
					{
#if ENABLE_MUSIC
						Mix_PlayMusic(game_state_music, -1);
#endif
					}
				}
			}
		}
		else if (ev->key.key == SDLK_3) // Toggle screenshake.
		{
			enable_screenshake = !enable_screenshake;
			OSDset("screenshake: %s", enable_screenshake ? "on" : "off");
		}
		else if (ev->key.key == SDLK_6)
		{
			static bool mute = false;
			mute = !mute;

			if (mute)
			{
				Mix_Volume(-1, 0);
				OSDset("mute");
			}
			else
			{
				Mix_Volume(-1, 10);
				OSDset("unmute");
			}
		}
		break;
	}
	}

	return SDL_APP_CONTINUE;
}

int Iterate()
{
	int numkeys;
	const bool* kbstate = SDL_GetKeyboardState(&numkeys);
	static int reset_input_timer = 0;

	// Ensure we are drawing into the PICO8 framebuffer target.
	SDL_SetRenderTarget(renderer, SDL_screen);

	// Hold C (backspace) to reset.
	if (initial_game_state != NULL && kbstate[SDL_SCANCODE_BACKSPACE])
	{
		reset_input_timer++;
		if (reset_input_timer >= 30)
		{
			reset_input_timer = 0;
			OSDset("reset");
			paused = 0;
			Celeste_P8_load_state(initial_game_state);
			Celeste_P8_set_rndseed((unsigned)(time(NULL) + SDL_GetTicks()));
			Mix_HaltChannel(-1);
#if ENABLE_MUSIC
			Mix_HaltMusic();
#endif
			Celeste_P8_init();
		}
	}
	else
	{
		reset_input_timer = 0;
	}

	buttons_state = 0;

	if (kbstate[SDL_SCANCODE_LEFT])  buttons_state |= (1 << 0);
	if (kbstate[SDL_SCANCODE_RIGHT]) buttons_state |= (1 << 1);
	if (kbstate[SDL_SCANCODE_UP])    buttons_state |= (1 << 2);
	if (kbstate[SDL_SCANCODE_DOWN])  buttons_state |= (1 << 3);
	if (kbstate[SDL_SCANCODE_7])     buttons_state |= (1 << 4);
	if (kbstate[SDL_SCANCODE_5])     buttons_state |= (1 << 5);

	if (paused)
	{
		const int x0 = PICO8_W / 2 - 3 * 4, y0 = 8;

		p8_rectfill(x0 - 1, y0 - 1, 6 * 4 + x0 + 1, 6 + y0 + 1, 6);
		p8_rectfill(x0, y0, 6 * 4 + x0, 6 + y0, 0);
		p8_print("paused", x0 + 1, y0 + 1, 7);
	}
	else
	{
		Celeste_P8_update();
		Celeste_P8_draw();
	}
	OSDdraw();
	Flip();

	return true;
}

void Destroy()
{
	if (game_state)
	{
		SDL_free(game_state);
	}
	if (initial_game_state)
	{
		SDL_free(initial_game_state);
	}
	if (gfx)
	{
		SDL_DestroyTexture(gfx);
	}
	if (font)
	{
		SDL_DestroyTexture(font);
	}
	if (frame)
	{
		SDL_DestroyTexture(frame);
	}
	if (SDL_screen)
	{
		SDL_DestroyTexture(SDL_screen);
	}

	for (int i = 0; i < (sizeof(snd)) / (sizeof(*snd)); i++)
	{
		if (snd[i])
		{
			Mix_FreeChunk(snd[i]);
		}
	}
#if ENABLE_MUSIC
	for (int i = 0; i < (sizeof(mus)) / (sizeof(*mus)); i++)
	{
		if (mus[i])
		{
			Mix_FreeMusic(mus[i]);
		}
	}
#endif

	Mix_CloseAudio();
	Mix_Quit();
	SDL_CloseAudioDevice(audio_device);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}

static void Flip()
{
	// Restore default render target and composite.
	SDL_SetRenderTarget(renderer, NULL);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	// Draw the frame overlay first (background art).
	if (frame)
	{
		SDL_RenderTexture(renderer, frame, NULL, NULL);
	}

	// Draw the PICO8 framebuffer into the N-Gage screen area.
	SDL_FRect dest = { 24.f, 25.f, (float)(PICO8_W * SCALE), (float)(PICO8_H * SCALE) };
	SDL_RenderTexture(renderer, SDL_screen, NULL, &dest);

	SDL_RenderPresent(renderer);

	// Switch back to rendering into the PICO8 framebuffer target.
	SDL_SetRenderTarget(renderer, SDL_screen);
}

static void LoadData(void)
{
	static const char sndids[] = { 0,1,2,3,4,5,6,7,8,9,13,14,15,16,23,35,37,38,40,50,51,54,55 };
	int iid;
#ifdef ENABLE_MUSIC
	static const char musids[] = { 0,10,20,30,40 };
#endif
	LOGLOAD("gfx.bmp");
	loadbmpscale("gfx.bmp", &gfx);
	LOGDONE();

	LOGLOAD("font.bmp");
	loadbmpscale("font.bmp", &font);
	LOGDONE();

	for (iid = 0; iid < sizeof(sndids); iid++)
	{
		int  id = sndids[iid];
		char fname[20];
		char path[256];

		SDL_snprintf(fname, 20, "snd%i.wav", id);
		LOGLOAD(fname);
		SDL_snprintf(path, 256, "%sdata/%s", SDL_GetBasePath(), fname);
		snd[id] = Mix_LoadWAV(path);
		if (!snd[id])
		{
			SDL_Log("snd%i: Mix_LoadWAV: %s", id, SDL_GetError());
		}
		LOGDONE();
	}

#if ENABLE_MUSIC
	for (iid = 0; iid < sizeof(musids); iid++)
	{
		int  id = musids[iid];
		char fname[20];
		char path[256];

		SDL_snprintf(fname, 20, "mus%i.ogg", id);
		LOGLOAD(fname);
		SDL_snprintf(path, 256, "%s%s", SDL_GetBasePath(), fname);
		mus[id / 10] = Mix_LoadMUS(path);
		if (!mus[id / 10])
		{
			SDL_Log("mus%i: Mix_LoadMUS: %s", id, SDL_GetError());
		}
		LOGDONE();
	}
#endif
}

static void SetPaletteEntry(unsigned char idx, unsigned char base_idx)
{
	palette_colors[idx] = base_palette_colors[base_idx];
}

static void RefreshPalette(void)
{
	SDL_memcpy(palette_colors, base_palette_colors, sizeof(palette_colors));
}

static void ResetPalette(void)
{
	SDL_memcpy(palette_colors, base_palette_colors, sizeof(palette_colors));
}

static void OSDset(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(osd_text, sizeof(osd_text), fmt, ap);
	osd_text[sizeof(osd_text) - 1] = '\0'; // Make sure to add NUL terminator in case of truncation.
	SDL_Log("%s", osd_text);
	osd_timer = 30;
	va_end(ap);
}

static void OSDdraw(void)
{
	if (osd_timer > 0)
	{
		--osd_timer;
	}

	if (osd_timer > 0)
	{
		const int x = 4;
		const int y = 120 + (osd_timer < 10 ? 10 - osd_timer : 0); // Disappear by going below the screen.
		p8_rectfill(x - 2, y - 2, x + 4 * (int)SDL_strlen(osd_text), y + 6, 6); // Outline.
		p8_rectfill(x - 1, y - 1, x + 4 * (int)SDL_strlen(osd_text) - 1, y + 5, 0);
		p8_print(osd_text, x, y, 7);
	}
}

static int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...)
{
	static int camera_x = 0, camera_y = 0;
	va_list    args;
	int        ret = 0;

	if (!enable_screenshake)
	{
		camera_x = camera_y = 0;
	}

	va_start(args, call);

#define INT_ARG() va_arg(args, int)
#define BOOL_ARG() (bool)va_arg(args, int)
#define RET_INT(_i)   do {ret = (_i); goto end;} while (0)
#define RET_BOOL(_b) RET_INT(!!(_b))

	switch (call)
	{
	case CELESTE_P8_MUSIC: //music(idx,fade,mask)
	{
#if ENABLE_MUSIC
		int index = INT_ARG();
		int fade = INT_ARG();
		int mask = INT_ARG();

		(void)mask; //we do not care about this since sdl mixer keeps sounds and music separate

		if (index == -1) { //stop playing
			Mix_FadeOutMusic(fade);
			current_music = NULL;
		}
		else if (mus[index / 10])
		{
			Mix_Music* musi = mus[index / 10];
			current_music = musi;
			Mix_FadeInMusic(musi, -1, fade);
		}
#endif
		break;
	}
	case CELESTE_P8_SPR: //spr(sprite,x,y,cols,rows,flipx,flipy)
	{
		int sprite = INT_ARG();
		int x = INT_ARG();
		int y = INT_ARG();
		int cols = INT_ARG();
		int rows = INT_ARG();
		int flipx = BOOL_ARG();
		int flipy = BOOL_ARG();

		(void)cols;
		(void)rows;

		SDL_assert(rows == 1 && cols == 1);

		if (sprite >= 0)
		{
			SDL_Rect srcrc =
			{
				8 * (sprite % 16),
				8 * (sprite / 16)
			};
			SDL_Rect dstrc =
			{
				(x - camera_x) * SCALE, (y - camera_y) * SCALE,
				SCALE, SCALE
			};
			srcrc.x *= SCALE;
			srcrc.y *= SCALE;
			srcrc.w = srcrc.h = SCALE * 8;
			Xblit(gfx, &srcrc, &dstrc, flipx, flipy);
		}
		break;
	}
	case CELESTE_P8_BTN: //btn(b)
	{
		int b = INT_ARG();
		SDL_assert(b >= 0 && b <= 5);
		RET_BOOL(buttons_state & (1 << b));
		break;
	}
	case CELESTE_P8_SFX: //sfx(id)
	{
		int id = INT_ARG();

		if (id < (sizeof(snd)) / (sizeof(*snd)) && snd[id])
		{
			Mix_PlayChannel(-1, snd[id], 0);
		}
		break;
	}
	case CELESTE_P8_PAL: //pal(a,b)
	{
		int a = INT_ARG();
		int b = INT_ARG();
		if (a >= 0 && a < 16 && b >= 0 && b < 16)
		{
			// Swap palette colors.
			SetPaletteEntry(a, b);
		}
		break;
	}
	case CELESTE_P8_PAL_RESET: //pal()
	{
		ResetPalette();
		break;
	}
	case CELESTE_P8_CIRCFILL: //circfill(x,y,r,col)
	{
		int cx = INT_ARG() - camera_x;
		int cy = INT_ARG() - camera_y;
		int r = INT_ARG();
		int col = INT_ARG();
		SDL_Color c = getcolor(col);
		SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);

		if (r <= 1)
		{
			SDL_FRect rect_a = { (float)(SCALE * (cx - 1)), (float)(SCALE * cy),     (float)(SCALE * 3), (float)SCALE };
			SDL_FRect rect_b = { (float)(SCALE * cx),       (float)(SCALE * (cy - 1)), (float)SCALE,       (float)(SCALE * 3) };

			SDL_RenderFillRect(renderer, &rect_a);
			SDL_RenderFillRect(renderer, &rect_b);
		}
		else if (r <= 2)
		{
			SDL_FRect rect_a = { (float)(SCALE * (cx - 2)), (float)(SCALE * (cy - 1)), (float)(SCALE * 5), (float)(SCALE * 3) };
			SDL_FRect rect_b = { (float)(SCALE * (cx - 1)), (float)(SCALE * (cy - 2)), (float)(SCALE * 3), (float)(SCALE * 5) };

			SDL_RenderFillRect(renderer, &rect_a);
			SDL_RenderFillRect(renderer, &rect_b);
		}
		else if (r <= 3)
		{
			SDL_FRect rect_a = { (float)(SCALE * (cx - 3)), (float)(SCALE * (cy - 1)), (float)(SCALE * 7), (float)(SCALE * 3) };
			SDL_FRect rect_b = { (float)(SCALE * (cx - 1)), (float)(SCALE * (cy - 3)), (float)(SCALE * 3), (float)(SCALE * 7) };
			SDL_FRect rect_c = { (float)(SCALE * (cx - 2)), (float)(SCALE * (cy - 2)), (float)(SCALE * 5), (float)(SCALE * 5) };

			SDL_RenderFillRect(renderer, &rect_a);
			SDL_RenderFillRect(renderer, &rect_b);
			SDL_RenderFillRect(renderer, &rect_c);
		}
		else  // I dont think the game uses this.
		{
			int f = 1 - r; // Used to track the progress of the drawn circle (since its semi-recursive).
			int ddFx = 1;   // Step x.
			int ddFy = -2 * r; // Step y.
			int x = 0;
			int y = r;

			// This algorithm doesn't account for the diameters
			// so we have to set them manually
			p8_line(cx, cy - y, cx, cy + r, col);
			p8_line(cx + r, cy, cx - r, cy, col);

			while (x < y)
			{
				if (f >= 0)
				{
					y--;
					ddFy += 2;
					f += ddFy;
				}
				x++;
				ddFx += 2;
				f += ddFx;

				// Build our current arc.
				p8_line(cx + x, cy + y, cx - x, cy + y, col);
				p8_line(cx + x, cy - y, cx - x, cy - y, col);
				p8_line(cx + y, cy + x, cx - y, cy + x, col);
				p8_line(cx + y, cy - x, cx - y, cy - x, col);
			}
		}
		break;
	}
	case CELESTE_P8_PRINT: //print(str,x,y,col)
	{
		const char* str = va_arg(args, const char*);
		int x = INT_ARG() - camera_x;
		int y = INT_ARG() - camera_y;
		int col = INT_ARG() % 16;

		p8_print(str, x, y, col);
		break;
	}
	case CELESTE_P8_RECTFILL: //rectfill(x0,y0,x1,y1,col)
	{
		int x0 = INT_ARG() - camera_x;
		int y0 = INT_ARG() - camera_y;
		int x1 = INT_ARG() - camera_x;
		int y1 = INT_ARG() - camera_y;
		int col = INT_ARG();

		p8_rectfill(x0, y0, x1, y1, col);
		break;
	}
	case CELESTE_P8_LINE: // line(x0,y0,x1,y1,col)
	{
		int x0 = INT_ARG() - camera_x;
		int y0 = INT_ARG() - camera_y;
		int x1 = INT_ARG() - camera_x;
		int y1 = INT_ARG() - camera_y;
		int col = INT_ARG();

		p8_line(x0, y0, x1, y1, col);
		break;
	}
	case CELESTE_P8_MGET: // mget(tx,ty)
	{
		int tx = INT_ARG();
		int ty = INT_ARG();

		RET_INT(tilemap_data[tx + ty * 128]);
		break;
	}
	case CELESTE_P8_CAMERA: //camera(x,y)
	{
		if (enable_screenshake)
		{
			camera_x = INT_ARG();
			camera_y = INT_ARG();
		}
		break;
	}
	case CELESTE_P8_FGET: //fget(tile,flag)
	{
		int tile = INT_ARG();
		int flag = INT_ARG();

		RET_INT(gettileflag(tile, flag));
		break;
	}
	case CELESTE_P8_MAP: //map(mx,my,tx,ty,mw,mh,mask)
	{
		int mx = INT_ARG(), my = INT_ARG();
		int tx = INT_ARG(), ty = INT_ARG();
		int mw = INT_ARG(), mh = INT_ARG();
		int mask = INT_ARG();
		int x, y;

		for (x = 0; x < mw; x++)
		{
			for (y = 0; y < mh; y++)
			{
				int tile = tilemap_data[x + mx + (y + my) * 128];
				// Hack.
				if (mask == 0 || (mask == 4 && tile_flags[tile] == 4) || gettileflag(tile, mask != 4 ? mask - 1 : mask))
				{
					SDL_Rect srcrc =
					{
					8 * (tile % 16),
					8 * (tile / 16)
					};
					SDL_Rect dstrc =
					{
					(tx + x * 8 - camera_x) * SCALE, (ty + y * 8 - camera_y) * SCALE,
					SCALE * 8, SCALE * 8
					};
					srcrc.x *= SCALE;
					srcrc.y *= SCALE;
					srcrc.w = srcrc.h = SCALE * 8;

					Xblit(gfx, &srcrc, &dstrc, 0, 0);
				}
			}
		}
		break;
	}
	}

end:
	va_end(args);
	return ret;
}

static int gettileflag(int tile, int flag)
{
	return tile < sizeof(tile_flags) / sizeof(*tile_flags) && (tile_flags[tile] & (1 << flag)) != 0;
}

static void loadbmpscale(char* filename, SDL_Texture** s)
{
	char tmpath[256];

	if (*s)
	{
		SDL_DestroyTexture(*s), * s = NULL;
	}

	SDL_snprintf(tmpath, sizeof(tmpath), "%sdata/%s", SDL_GetBasePath(), filename);

	SDL_Surface* bmp = SDL_LoadBMP(tmpath);
	if (!bmp)
	{
		SDL_Log("Error loading bmp '%s': %s", filename, SDL_GetError());
		return;
	}

	// Convert to RGBA so we can set per-pixel alpha for the color key.
	SDL_Surface* rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA8888);
	SDL_DestroySurface(bmp);
	if (!rgba)
	{
		SDL_Log("Error converting bmp '%s': %s", filename, SDL_GetError());
		return;
	}

	// Make black pixels (0,0,0) transparent (color key).
	Uint8* pixels = (Uint8*)rgba->pixels;
	for (int y = 0; y < rgba->h; y++)
	{
		for (int x = 0; x < rgba->w; x++)
		{
			Uint8* p = pixels + y * rgba->pitch + x * 4;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			Uint8 r = p[0], g = p[1], b = p[2];
			if (r == 0 && g == 0 && b == 0) p[3] = 0; // alpha
#else
			// RGBA8888 in little-endian: bytes are A,B,G,R
			Uint8 r = p[3], g = p[2], b = p[1];
			if (r == 0 && g == 0 && b == 0) p[0] = 0; // alpha byte
#endif
		}
	}

#if SCALE > 1
	SDL_Surface* scaled = SDL_CreateSurface(rgba->w * SCALE, rgba->h * SCALE, SDL_PIXELFORMAT_RGBA8888);
	if (!scaled)
	{
		SDL_DestroySurface(rgba);
		SDL_Log("Error scaling bmp '%s': %s", filename, SDL_GetError());
		return;
	}
	SDL_BlitSurfaceScaled(rgba, NULL, scaled, NULL, SDL_SCALEMODE_NEAREST);
	SDL_DestroySurface(rgba);
	rgba = scaled;
#endif

	SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, rgba);
	SDL_DestroySurface(rgba);
	if (!tex)
	{
		SDL_Log("Error creating texture for '%s': %s", filename, SDL_GetError());
		return;
	}
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

	*s = tex;
}

// Coordinates should NOT be scaled before calling this.
static void p8_line(int x0, int y0, int x1, int y1, unsigned char color)
{
	SDL_Color c = getcolor(color);
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);

	int sx, sy, dx, dy, err;
	SDL_FRect rect;

#define CLAMP(v,min,max) v = v < min ? min : v >= max ? max-1 : v;
	CLAMP(x0, 0, PICO8_W);
	CLAMP(y0, 0, PICO8_H);
	CLAMP(x1, 0, PICO8_W);
	CLAMP(y1, 0, PICO8_H);
#undef CLAMP

#define PLOT(xa, ya) \
	rect.x = (float)(xa * SCALE); \
	rect.y = (float)(ya * SCALE); \
	rect.w = (float)SCALE; \
	rect.h = (float)SCALE; \
	SDL_RenderFillRect(renderer, &rect)

	dx = SDL_abs(x1 - x0);
	dy = SDL_abs(y1 - y0);
	if (!dx && !dy)
	{
		return;
	}

	sx = (x0 < x1) ? 1 : -1;
	sy = (y0 < y1) ? 1 : -1;
	err = dx - dy;

	if (!dy && !dx)
	{
		return;
	}
	else if (!dx) // Vertical line.
	{
		for (int y = y0; y != y1; y += sy) { PLOT(x0, y); }
	}
	else if (!dy) // Horizontal line.
	{
		for (int x = x0; x != x1; x += sx) { PLOT(x, y0); }
	}

	while (x0 != x1 || y0 != y1)
	{
		int e2;
		PLOT(x0, y0);
		e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
#undef PLOT
}

static void p8_print(const char* str, int x, int y, int col)
{
	SDL_Color c = getcolor(col);
	SDL_SetTextureColorMod(font, c.r, c.g, c.b);

	for (char ch = *str; ch; ch = *(++str))
	{
		SDL_FRect srcrc, dstrc;
		ch &= 0x7F;
		srcrc.x = (float)(8 * SCALE * (ch % 16));
		srcrc.y = (float)(8 * SCALE * (ch / 16));
		srcrc.w = srcrc.h = (float)(8 * SCALE);

		dstrc.x = (float)(x * SCALE);
		dstrc.y = (float)(y * SCALE);
		dstrc.w = dstrc.h = (float)(8 * SCALE);

		SDL_RenderTexture(renderer, font, &srcrc, &dstrc);
		x += 4;
	}
}

static void p8_rectfill(int x0, int y0, int x1, int y1, int col)
{
	int w = (x1 - x0 + 1) * SCALE;
	int h = (y1 - y0 + 1) * SCALE;
	if (w > 0 && h > 0)
	{
		SDL_Color c = getcolor(col);
		SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
		SDL_FRect rc = { (float)(x0 * SCALE), (float)(y0 * SCALE), (float)w, (float)h };
		SDL_RenderFillRect(renderer, &rc);
	}
}

//coordinates should be scaled already
static inline void Xblit(SDL_Texture* src, SDL_Rect* srcrect, SDL_Rect* dstrect, int flipx, int flipy)
{
	SDL_FlipMode flip = SDL_FLIP_NONE;
	if (flipx && flipy)      flip = SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL;
	else if (flipx)          flip = SDL_FLIP_HORIZONTAL;
	else if (flipy)          flip = SDL_FLIP_VERTICAL;

	SDL_FRect fsrc, fdst;
	SDL_FRect* fsrcp = NULL;
	SDL_FRect* fdstp = NULL;

	if (srcrect)
	{
		fsrc.x = (float)srcrect->x;
		fsrc.y = (float)srcrect->y;
		fsrc.w = (float)srcrect->w;
		fsrc.h = (float)srcrect->h;
		fsrcp = &fsrc;
	}
	if (dstrect)
	{
		fdst.x = (float)dstrect->x;
		fdst.y = (float)dstrect->y;
		// Use source dimensions for output size (same behaviour as old Xblit).
		fdst.w = fsrcp ? fsrc.w : (float)dstrect->w;
		fdst.h = fsrcp ? fsrc.h : (float)dstrect->h;
		fdstp = &fdst;
	}

	SDL_RenderTextureRotated(renderer, src, fsrcp, fdstp, 0.0, NULL, flip);
}
