/*
 * Copyright (C) 2026 Mrokkk
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
// vid_sdl.c -- SDL video and input driver

#include <SDL2/SDL.h>

#include "quakedef.h"
#include "sys_unix.h"

typedef uint32_t pixel32_t;

static cvar_t	vid_vsync = {"vid_vsync", "1", CVAR_ARCHIVE, 0};
static cvar_t	vid_fullscreen = {"vid_fullscreen", "0", CVAR_ARCHIVE, 0};
static cvar_t	vid_width = {"vid_width", "960", CVAR_ARCHIVE, 960};
static cvar_t	vid_height = {"vid_height", "600", CVAR_ARCHIVE, 600};

static cvar_t	ui_vid_mode = {"ui_vid_mode", "-1", CVAR_NONE, -1};
static cvar_t	ui_vid_width = {"ui_vid_width", "960", CVAR_NONE, 960};
static cvar_t	ui_vid_height = {"ui_vid_height", "600", CVAR_NONE, 600};
static cvar_t	ui_vid_vsync = {"ui_vid_vsync", "1", CVAR_NONE, 1};
static cvar_t	ui_vid_fullscreen = {"ui_vid_fullscreen", "0", CVAR_NONE, 0};

static qboolean	initialized = false;
static qboolean	vsync = true;
static qboolean	fullscreen = true;

static SDL_Window		*window;
static Uint32			pixel_format_id;
static SDL_Renderer		*renderer;
static SDL_PixelFormat	*pixel_format;
static SDL_Texture		*texture;
static uint32_t			*image;
static byte				*qbuffer;
static pixel32_t		color_palette[256];

static qboolean			focused;
static SDL_Rect			dest;
static uint64_t			frame_start;
static float			fps_sum;
static uint64_t			frame;

unsigned short d_8to16table[256];

static void VID_RegisterMenu(void);
static void VID_Restart(void);
static void VID_Menu_Enter(void);

static void RecalculateDestRect(void)
{
	int		resx, resy;
	float	scalex, scaley, scale, posx, posy;

	SDL_GetWindowSize(window, &resx, &resy);

	scalex = (float)(resx) / vid.width;
	scaley = (float)(resy) / vid.height;
	scale = Q_MIN(scalex, scaley);
	posx = (resx - scale * vid.width) / 2;
	posy = (resy - scale * vid.height) / 2;

	dest.x = posx;
	dest.y = posy;
	dest.w = scale * vid.width;
	dest.h = scale * vid.height;
}

void VID_Init(unsigned char *palette)
{
	Uint32	flags = 0;

	VID_RegisterMenu();

	Cvar_RegisterVariable(&vid_vsync);
	Cvar_RegisterVariable(&vid_fullscreen);
	Cvar_RegisterVariable(&vid_width);
	Cvar_RegisterVariable(&vid_height);

	Cvar_RegisterVariable(&ui_vid_mode);
	Cvar_RegisterVariable(&ui_vid_width);
	Cvar_RegisterVariable(&ui_vid_height);
	Cvar_RegisterVariable(&ui_vid_vsync);
	Cvar_RegisterVariable(&ui_vid_fullscreen);

	Cmd_AddCommand("vid_restart", &VID_Restart);

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		Sys_Error_f("Failed to initialize the SDL2 library: %s\n", SDL_GetError());

	if (fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	window = SDL_CreateWindow(
		"sdlquake",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		vid_width.value,
		vid_height.value,
		flags);

	if (!window)
		Sys_Error_f("Failed to create window: %s\n", SDL_GetError());

	pixel_format_id = SDL_GetWindowPixelFormat(window);

	if (!(pixel_format = SDL_AllocFormat(pixel_format_id)))
		Sys_Error_f("Failed to allocate pixel format: %s\n", SDL_GetError());

	VID_Restart();

	VID_SetPalette(palette);

	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetRelativeMouseMode(SDL_TRUE);

	focused = true;
	frame_start = gettime_ns();
}

void VID_ShiftPalette(unsigned char *p)
{
	VID_SetPalette(p);
}

static inline pixel32_t RGBA(int r, int g, int b, int a)
{
	const SDL_PixelFormat *f = pixel_format;
	return	((r << f->Rshift) & f->Rmask)
		|	((g << f->Gshift) & f->Gmask)
		|	((b << f->Bshift) & f->Bmask)
		|	((a << f->Ashift) & f->Amask);
}

void VID_SetPalette(unsigned char *palette)
{
	size_t i;

	for (i = 0; i < Q_ARRLEN(color_palette); i++)
	{
		color_palette[i] = RGBA(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2], 0xff);
	}
}

void VID_Shutdown(void)
{
	if (window) SDL_DestroyWindow(window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static void VID_Restart(void)
{
	qboolean res_changed, set_window_size = false;

	if ((!!vid_fullscreen.value) != fullscreen)
	{
		fullscreen = !!vid_fullscreen.value;
		if (fullscreen)
		{
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		}
		else
		{
			SDL_SetWindowFullscreen(window, 0);
			set_window_size = true;
		}
	}

	res_changed = (unsigned)vid_width.value != vid.width || (unsigned)vid_height.value != vid.height;

	if ((!!vid_vsync.value) != vsync || res_changed || !initialized)
	{
		vsync = !!vid_vsync.value;

		if (res_changed || !initialized)
		{
			if (qbuffer)
			{
				free(qbuffer);
			}
			if (image)
			{
				free(image);
			}

			vid.width			= vid_width.value;
			vid.height			= vid_height.value;
			vid.rowbytes		= vid.width;
			vid.maxwarpwidth	= WARP_WIDTH;
			vid.maxwarpheight	= WARP_HEIGHT;

			if (!(image = calloc(vid.width * vid.height, sizeof(*image))))
				Sys_Error_f("Failed to allocate memory for framebuffer\n");

			if (!(qbuffer = calloc(vid.width * vid.height, sizeof(*qbuffer))))
				Sys_Error_f("Failed to allocate memory for framebuffer\n");

			vid.numpages		= 1;
			vid.colormap		= host_colormap;
			vid.fullbright		= 256 - LittleLong(*((int *)vid.colormap + 2048));
			vid.direct			= 0;
			vid.buffer			= (pixel_t *)qbuffer;
			vid.conbuffer		= (pixel_t *)qbuffer;
			vid.conrowbytes		= vid.rowbytes;
			vid.conwidth		= vid.width;
			vid.conheight		= vid.height;
			vid.recalc_refdef	= initialized;	// force a surface cache flush in case of reconfiguration

			set_window_size |= !fullscreen;
		}

		if (texture)
		{
			SDL_DestroyTexture(texture);
		}

		if (renderer)
		{
			SDL_DestroyRenderer(renderer);
		}

		if (!(renderer = SDL_CreateRenderer(window, -1, vsync ? SDL_RENDERER_PRESENTVSYNC : 0)))
			Sys_Error_f("Failed to create renderer: %s\n", SDL_GetError());

		texture = SDL_CreateTexture(
			renderer,
			pixel_format_id,
			SDL_TEXTUREACCESS_TARGET,
			vid.width,
			vid.height);

		D_AllocateBuffer();
		Con_CheckResize();
	}

	if (set_window_size)
	{
		SDL_SetWindowSize(window, vid.width, vid.height);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}

	RecalculateDestRect();

	initialized = true;
}

static inline void DrawRect(int x, int y, int width, int height)
{
	int			i;
	uint8_t		*src;
	pixel32_t	*dest;

	if (Q_UNLIKELY(x < 0 || y < 0)) return;

	src = qbuffer + y * vid.width + x;
	dest = image + y * vid.width + y;

	while (height--)
	{
		for (i = 0; i < width; ++i)
		{
			dest[i] = color_palette[src[i]];
		}
		dest += vid.width;
		src += vid.width;
	}
}

void VID_Update(vrect_t *rects)
{
	uint64_t	prev_frame;

	scr_fullupdate = 0;

	if (Q_UNLIKELY(!focused))
	{
		goto swapbuffers;
	}

	while (rects)
	{
		DrawRect(rects->x, rects->y, rects->width, rects->height);
		rects = rects->pnext;
	}

	SDL_UpdateTexture(texture, NULL, image, vid.width * sizeof(*image));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, &dest);

swapbuffers:
	SDL_RenderPresent(renderer);

	prev_frame = frame_start;
	frame_start = gettime_ns();

	fps_sum += (float)NSEC_IN_SEC / (frame_start - prev_frame);

	if (++frame % 60 == 0)
	{
		vid_fps = fps_sum / 60;
		fps_sum = 0;
	}
}

static int dither;

void VID_DitherOn(void)
{
	if (dither == 0)
	{
		vid.recalc_refdef	= 1;
		dither				= 1;
	}
}

void VID_DitherOff(void)
{
	if (dither)
	{
		vid.recalc_refdef	= 1;
		dither				= 0;
	}
}

static int ConvertKey(SDL_Keycode key)
{
	switch (key)
	{
		case SDLK_PAGEUP:		return K_PGUP;
		case SDLK_PAGEDOWN:		return K_PGDN;
		case SDLK_HOME:			return K_HOME;
		case SDLK_END:			return K_END;
		case SDLK_LEFT:			return K_LEFTARROW;
		case SDLK_RIGHT:		return K_RIGHTARROW;
		case SDLK_DOWN:			return K_DOWNARROW;
		case SDLK_UP:			return K_UPARROW;
		case SDLK_ESCAPE:		return K_ESCAPE;
		case SDLK_RETURN:		return K_ENTER;
		case SDLK_TAB:			return K_TAB;
		case SDLK_BACKSPACE:	return K_BACKSPACE;
		case SDLK_DELETE:		return K_DEL;
		case SDLK_PAUSE:		return K_PAUSE;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:		return K_SHIFT;
		case SDLK_LCTRL:
		case SDLK_RCTRL:		return K_CTRL;
		case SDLK_LALT:
		case SDLK_RALT:			return K_ALT;
		case SDLK_INSERT:		return K_INS;
		case SDLK_F1:			return K_F1;
		case SDLK_F2:			return K_F2;
		case SDLK_F3:			return K_F3;
		case SDLK_F4:			return K_F4;
		case SDLK_F5:			return K_F5;
		case SDLK_F6:			return K_F6;
		case SDLK_F7:			return K_F7;
		case SDLK_F8:			return K_F8;
		case SDLK_F9:			return K_F9;
		case SDLK_F10:			return K_F10;
		case SDLK_F11:			return K_F11;
		case SDLK_F12:			return K_F12;
		default:
			if (isascii(key) && isprint(key))
			{
				return tolower(key);
			}
			return 0;
	}
}

static inline int ConvertButton(int button)
{
	switch (button)
	{
		case 1:		return K_MOUSE1 & 3;
		case 2:		return K_MOUSE3 & 3;
		case 3:		return K_MOUSE2 & 3;
		default:	return -1;
	}
}

void IN_ReadEvents(void)
{
	int			b;
	SDL_Event	e;

	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
			case SDL_QUIT:
				Host_Quit_f();
				break;

			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if ((b = ConvertKey(e.key.keysym.sym)))
					IN_AddKey(b, e.type == SDL_KEYDOWN);
				break;

			case SDL_MOUSEMOTION:
				IN_AddMouseMove((float)e.motion.xrel, (float)e.motion.yrel);
				break;

			case SDL_MOUSEBUTTONDOWN:
				if ((b = ConvertButton(e.button.button)) >= 0)
					IN_AddMouseButton(b, true);
				break;

			case SDL_MOUSEBUTTONUP:
				if ((b = ConvertButton(e.button.button)) >= 0)
					IN_AddMouseButton(b, false);
				break;

			case SDL_WINDOWEVENT:
				switch (e.window.event)
				{
					case SDL_WINDOWEVENT_RESIZED:
						RecalculateDestRect();
						break;

					case SDL_WINDOWEVENT_FOCUS_GAINED:
						S_UnblockSound();
						focused = true;
						break;

					case SDL_WINDOWEVENT_FOCUS_LOST:
						S_BlockSound();
						focused = false;
						break;
				}
		}
	}
}

typedef struct vid_mode_s
{
	unsigned short	width, height;
	char			name[16];
} vid_mode_t;

static void VID_ApplyChanges(void);

#define ASPECT_16_9		0.5625f
#define ASPECT_16_10	0.625f
#define ASPECT_4_3		0.75f

#define MODES_FOR_WIDTH(width)				\
	{width, (int)(width * ASPECT_16_9)},	\
	{width, (int)(width * ASPECT_16_10)},	\
	{width, (int)(width * ASPECT_4_3)}

static vid_mode_t vid_modes[] = {
	{320, 200},
	{320, 240},
	MODES_FOR_WIDTH(640),
	MODES_FOR_WIDTH(800),
	MODES_FOR_WIDTH(960),
	MODES_FOR_WIDTH(1024),
	MODES_FOR_WIDTH(1280),
	MODES_FOR_WIDTH(1440),
	MODES_FOR_WIDTH(1920),
	MODES_FOR_WIDTH(2560),
	{0, 0}
};

static m_value_t m_modes[Q_ARRLEN(vid_modes)];

#define CUSTOM_MODE	(Q_ARRLEN(vid_modes) - 1)

static option_t options[] = {
	(option_t){
		.type			= option_values,
		.name			= "Resolution",
		.values			= {
			.cvar		= &ui_vid_mode,
			.count		= Q_ARRLEN(m_modes),
			.data		= m_modes,
		}
	},
	(option_t){
		.type			= option_onoff,
		.name			= "Fullscreen",
		.onoff			= {
			.cvar		= &ui_vid_fullscreen,
		}
	},
	(option_t){
		.type			= option_onoff,
		.name			= "V-Sync",
		.onoff			= {
			.cvar		= &ui_vid_vsync,
		}
	},
	(option_t){
		.type			= option_button,
		.name			= "Apply changes",
		.button			= {
			.action		= action_callback,
			.callback	= &VID_ApplyChanges,
		}
	},
};

static menu_t vid_menu = {
	.cursor			= 0,
	.options_count	= Q_ARRLEN(options),
	.options		= options,
	.title			= "Video Options",
	.enter			= &VID_Menu_Enter,
	.parent			= NULL,
};

static void VID_RegisterMenu(void)
{
	size_t i;
	for (i = 0; i < Q_ARRLEN(options); ++i)
	{
		options[i].namelen = strlen(options[i].name);
	}
	M_RegisterVideoMenu(&vid_menu);
}

static void VID_Menu_Enter(void)
{
	int		current_mode = CUSTOM_MODE;
	size_t	i;

	for (i = 0; i < Q_ARRLEN(vid_modes) - 1; ++i)
	{
		if (vid_modes[i].width == vid.width && vid_modes[i].height == vid.height)
		{
			current_mode = i;
		}
		Q_snprintf(vid_modes[i].name, sizeof(vid_modes->name), "%ux%u", vid_modes[i].width, vid_modes[i].height);
		m_modes[i].name = vid_modes[i].name;
		m_modes[i].value = i;
	}

	if (current_mode == CUSTOM_MODE)
	{
		vid_modes[current_mode].width = vid.width;
		vid_modes[current_mode].height = vid.height;
		Q_snprintf(vid_modes[current_mode].name, sizeof(vid_modes->name), "Custom %ux%u", vid.width, vid.height);
		m_modes[current_mode].name = vid_modes[current_mode].name;
		options[0].values.count = Q_ARRLEN(vid_modes);
	}
	else
	{
		options[0].values.count = Q_ARRLEN(vid_modes) - 1;
	}

	Cvar_SetValue("ui_vid_mode", current_mode);
	Cvar_SetValue("ui_vid_width", vid_width.value);
	Cvar_SetValue("ui_vid_height", vid_height.value);
	Cvar_SetValue("ui_vid_fullscreen", vid_fullscreen.value);
	Cvar_SetValue("ui_vid_vsync", vid_vsync.value);
}

static void VID_ApplyChanges(void)
{
	int mode;

	mode = (int)ui_vid_mode.value;

	if (mode >= 0 && mode < (int)Q_ARRLEN(vid_modes))
	{
		Cvar_SetValue("vid_width", vid_modes[mode].width);
		Cvar_SetValue("vid_height", vid_modes[mode].height);
	}

	Cvar_SetValue("vid_fullscreen", ui_vid_fullscreen.value);
	Cvar_SetValue("vid_vsync", ui_vid_vsync.value);
	Cbuf_AddText("vid_restart\n");
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
