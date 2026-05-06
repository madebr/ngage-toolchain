/* @file main.c
 *
 * 3D spinning triangle for the Nokia N-Gage.
 * Uses fixed-point math (16.16) and SDL_RenderGeometry.
 *
 */

#define SDL_MAIN_USE_CALLBACKS 1

#define NGAGE_W 176
#define NGAGE_H 208

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

typedef int fixed_t;

#define FP_SHIFT     16
#define FP_ONE       (1 << FP_SHIFT)
#define INT_TO_FP(x) ((x) << FP_SHIFT)
#define FP_TO_INT(x) ((x) >> FP_SHIFT)
#define FP_MUL(a,b)  (((a) >> 8) * ((b) >> 8))

// Sin/cos lookup table: 256 entries, values scaled to 32768 = 1.0 in Q15.
// FP_TRIG converts Q15 to 16.16 fixed point.
#define FP_TRIG(q15)    ((q15) << 1)

static const int sin_tbl[256] = {
	0x0000, 0x0324, 0x0648, 0x096B, 0x0C8C, 0x0FAB, 0x12C8, 0x15E2,
	0x1900, 0x1C0C, 0x1F1A, 0x2224, 0x2528, 0x2827, 0x2B1F, 0x2E11,
	0x30FC, 0x33DF, 0x36BA, 0x398D, 0x3C57, 0x3F17, 0x41CE, 0x447B,
	0x471D, 0x49B4, 0x4C40, 0x4EC0, 0x5134, 0x539B, 0x55F6, 0x5843,
	0x5A82, 0x5CB4, 0x5ED7, 0x60EC, 0x62F2, 0x64E9, 0x66D0, 0x68A7,
	0x6A6E, 0x6C24, 0x6DCA, 0x6F5F, 0x70E3, 0x7255, 0x73B6, 0x7505,
	0x7642, 0x776C, 0x7885, 0x798A, 0x7A7D, 0x7B5D, 0x7C2A, 0x7CE4,
	0x7D8A, 0x7E1E, 0x7E9D, 0x7F0A, 0x7F62, 0x7FA7, 0x7FD9, 0x7FF6,
	0x8000, 0x7FF6, 0x7FD9, 0x7FA7, 0x7F62, 0x7F0A, 0x7E9D, 0x7E1E,
	0x7D8A, 0x7CE4, 0x7C2A, 0x7B5D, 0x7A7D, 0x798A, 0x7885, 0x776C,
	0x7642, 0x7505, 0x73B6, 0x7255, 0x70E3, 0x6F5F, 0x6DCA, 0x6C24,
	0x6A6E, 0x68A7, 0x66D0, 0x64E9, 0x62F2, 0x60EC, 0x5ED7, 0x5CB4,
	0x5A82, 0x5843, 0x55F6, 0x539B, 0x5134, 0x4EC0, 0x4C40, 0x49B4,
	0x471D, 0x447B, 0x41CE, 0x3F17, 0x3C57, 0x398D, 0x36BA, 0x33DF,
	0x30FC, 0x2E11, 0x2B1F, 0x2827, 0x2528, 0x2224, 0x1F1A, 0x1C0C,
	0x1900, 0x15E2, 0x12C8, 0x0FAB, 0x0C8C, 0x096B, 0x0648, 0x0324,
	0x0000, -0x0324, -0x0648, -0x096B, -0x0C8C, -0x0FAB, -0x12C8, -0x15E2,
	-0x1900, -0x1C0C, -0x1F1A, -0x2224, -0x2528, -0x2827, -0x2B1F, -0x2E11,
	-0x30FC, -0x33DF, -0x36BA, -0x398D, -0x3C57, -0x3F17, -0x41CE, -0x447B,
	-0x471D, -0x49B4, -0x4C40, -0x4EC0, -0x5134, -0x539B, -0x55F6, -0x5843,
	-0x5A82, -0x5CB4, -0x5ED7, -0x60EC, -0x62F2, -0x64E9, -0x66D0, -0x68A7,
	-0x6A6E, -0x6C24, -0x6DCA, -0x6F5F, -0x70E3, -0x7255, -0x73B6, -0x7505,
	-0x7642, -0x776C, -0x7885, -0x798A, -0x7A7D, -0x7B5D, -0x7C2A, -0x7CE4,
	-0x7D8A, -0x7E1E, -0x7E9D, -0x7F0A, -0x7F62, -0x7FA7, -0x7FD9, -0x7FF6,
	-0x8000, -0x7FF6, -0x7FD9, -0x7FA7, -0x7F62, -0x7F0A, -0x7E9D, -0x7E1E,
	-0x7D8A, -0x7CE4, -0x7C2A, -0x7B5D, -0x7A7D, -0x798A, -0x7885, -0x776C,
	-0x7642, -0x7505, -0x73B6, -0x7255, -0x70E3, -0x6F5F, -0x6DCA, -0x6C24,
	-0x6A6E, -0x68A7, -0x66D0, -0x64E9, -0x62F2, -0x60EC, -0x5ED7, -0x5CB4,
	-0x5A82, -0x5843, -0x55F6, -0x539B, -0x5134, -0x4EC0, -0x4C40, -0x49B4,
	-0x471D, -0x447B, -0x41CE, -0x3F17, -0x3C57, -0x398D, -0x36BA, -0x33DF,
	-0x30FC, -0x2E11, -0x2B1F, -0x2827, -0x2528, -0x2224, -0x1F1A, -0x1C0C,
	-0x1900, -0x15E2, -0x12C8, -0x0FAB, -0x0C8C, -0x096B, -0x0648, -0x0324
};

static fixed_t fp_sin(int angle) { return FP_TRIG(sin_tbl[angle & 255]); }
static fixed_t fp_cos(int angle) { return FP_TRIG(sin_tbl[(angle + 64) & 255]); }

// Three vertices of the triangle in 3-D object space (fixed-point).
#define V_SCALE INT_TO_FP(60)

static const fixed_t obj_verts[3][3] = {
	/*        X             Y             Z  */
	{  INT_TO_FP(0),  -V_SCALE,      INT_TO_FP(0)  },
	{ -V_SCALE,        V_SCALE,      INT_TO_FP(0)  },
	{  V_SCALE,        V_SCALE,      INT_TO_FP(0)  }
};

// Vertex colours.
static const SDL_FColor vert_colors[3] = {
	{ 1.0f, 0.2f, 0.2f, 1.0f },
	{ 0.2f, 1.0f, 0.2f, 1.0f },
	{ 0.2f, 0.2f, 1.0f, 1.0f }
};

// Perspective divide constant: camera distance in fixed-point.
#define CAM_DIST INT_TO_FP(200)

// Rotation angles (0-255 each axis).
static int angle_x = 0;
static int angle_y = 0;
static int angle_z = 0;

static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_AudioDeviceID  audio_device;

// Project one 3-D point -> 2-D screen coords (fixed-point perspective).
static void project(fixed_t x, fixed_t y, fixed_t z, float* sx, float* sy)
{
	fixed_t denom = CAM_DIST + z;
	/* Guard against division by zero / behind camera. */
	if (denom <= 0) denom = 1;

	/* Perspective divide: sx = (x * CAM_DIST) / denom */
	/* To stay in fixed-point: multiply then shift. */
	fixed_t px = (int)(((long long)x * CAM_DIST) / denom);
	fixed_t py = (int)(((long long)y * CAM_DIST) / denom);

	*sx = (float)(FP_TO_INT(px) + NGAGE_W / 2);
	*sy = (float)(FP_TO_INT(py) + NGAGE_H / 2);
}

// Rotate a vertex around all three axes and return world coords.
// Rotation order: Y -> X -> Z.
static void rotate_vertex(
	fixed_t ix, fixed_t iy, fixed_t iz,
	int ax, int ay, int az,
	fixed_t* ox, fixed_t* oy, fixed_t* oz)
{
	fixed_t sx, cx, sy, cy, sz, cz;
	fixed_t tx, ty, tz;

	sx = fp_sin(ax); cx = fp_cos(ax);
	sy = fp_sin(ay); cy = fp_cos(ay);
	sz = fp_sin(az); cz = fp_cos(az);

	/* Rotate around Y */
	tx = FP_MUL(cy, ix) + FP_MUL(sy, iz);
	ty = iy;
	tz = -FP_MUL(sy, ix) + FP_MUL(cy, iz);

	ix = tx; iy = ty; iz = tz;

	/* Rotate around X */
	tx = ix;
	ty = FP_MUL(cx, iy) - FP_MUL(sx, iz);
	tz = FP_MUL(sx, iy) + FP_MUL(cx, iz);

	ix = tx; iy = ty; iz = tz;

	/* Rotate around Z */
	*ox = FP_MUL(cz, ix) - FP_MUL(sz, iy);
	*oy = FP_MUL(sz, ix) + FP_MUL(cz, iy);
	*oz = iz;
}

// This function runs once at startup.
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	SDL_SetHint("SDL_RENDER_NGAGE_SHOW_FPS", "1");
	SDL_SetHint("SDL_RENDER_VSYNC", "1");
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
	SDL_SetAppMetadata("triangle", "1.0", "com.triangle.ngagesdk");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
	{
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if (!SDL_CreateWindowAndRenderer("triangle", NGAGE_W, NGAGE_H, 0, &window, &renderer))
	{
		SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	// I recommend initialising the audio device in any case,
	// even if you don't need it. The backend is started at a
	// higher level and this ensures that everything is terminated
	// properly.
	SDL_AudioSpec spec;
	spec.channels = 1;
	spec.format = SDL_AUDIO_S16;
	spec.freq = 8000;

	audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
	if (audio_device == 0)
	{
		SDL_Log("SDL_OpenAudioDevice: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	return SDL_APP_SUCCESS;
}

// This function runs when a new event (Keypresses, etc) occurs.
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	switch (event->type)
	{
	case SDL_EVENT_QUIT:
	{
		return SDL_APP_SUCCESS;
	}
	case SDL_EVENT_KEY_DOWN:
	{
		if (event->key.repeat) // No key repeat.
		{
			break;
		}

		if (event->key.key == SDLK_SOFTLEFT)
		{
			return SDL_APP_SUCCESS;
		}

		break;
	}
	}

	return SDL_APP_CONTINUE;
}

// This function runs once per frame, and is the heart of the program.
SDL_AppResult SDL_AppIterate(void* appstate)
{
	int i;
	SDL_Vertex verts[3];
	fixed_t wx, wy, wz;

	/* Clear to black. */
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	/* Transform and project each vertex. */
	for (i = 0; i < 3; i++)
	{
		rotate_vertex(
			obj_verts[i][0], obj_verts[i][1], obj_verts[i][2],
			angle_x, angle_y, angle_z,
			&wx, &wy, &wz);

		project(wx, wy, wz, &verts[i].position.x, &verts[i].position.y);
		verts[i].color = vert_colors[i];
		verts[i].tex_coord.x = 0.0f;
		verts[i].tex_coord.y = 0.0f;
	}

	SDL_RenderGeometry(renderer, NULL, verts, 3, NULL, 0);

	SDL_RenderPresent(renderer);

	/* Advance rotation angles. */
	angle_x = (angle_x + 1) & 255;
	angle_y = (angle_y + 2) & 255;
	angle_z = (angle_z + 1) & 255;

	return SDL_APP_CONTINUE;
}

// This function runs once at shutdown.
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	SDL_CloseAudioDevice(audio_device);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}
