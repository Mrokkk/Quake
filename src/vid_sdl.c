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

static cvar_t	vid_vsync = {"vid_vsync", "1", true};
static cvar_t	vid_fullscreen = {"vid_fullscreen", "0", true};
static cvar_t	vid_width = {"vid_width", "960", true, false, 960};
static cvar_t	vid_height = {"vid_height", "600", true, false, 600};

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

static void VID_Restart(void);
static void VID_Menu_Draw(void);
static void VID_Menu_Key(int key);

static menu_t vid_menu = {
	.draw	= &VID_Menu_Draw,
	.key	= &VID_Menu_Key,
};

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

	M_RegisterVideoMenu(&vid_menu);

	Cvar_RegisterVariable(&vid_vsync);
	Cvar_RegisterVariable(&vid_fullscreen);
	Cvar_RegisterVariable(&vid_width);
	Cvar_RegisterVariable(&vid_height);

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
			vid.aspect			= ((float)vid.height / (float)vid.width) * ((float)SCREEN_WIDTH / 240.0f);
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

typedef enum menu_type_s
{
	menu_type_onoff,
	menu_type_button_cmd,
	menu_type_mode,
} menu_type_t;

typedef struct vid_option_s
{
	menu_type_t	type;
	union
	{
		qboolean	*value;
		void		(*func)(void);
	};
	const char	*name;
	int			min, max;
} vid_option_t;

typedef struct vid_mode_s
{
	unsigned	width, height;
} vid_mode_t;

#define UNSET_MODE	-1
#define CUSTOM_MODE	-2

static unsigned	vid_opt_current;
static int		current_mode = UNSET_MODE;
static int		custom_width = -1;
static int		custom_height = -1;
static qboolean	temp_fullscreen;
static qboolean	temp_vsync;

static void VID_ApplyChanges(void);

static vid_option_t vid_opts[] = {
	{
		.type	= menu_type_mode,
		.value	= NULL,
		.name	= "Resolution",
	},
	{
		.type	= menu_type_onoff,
		.value	= &temp_fullscreen,
		.name	= "Fullscreen",
	},
	{
		.type	= menu_type_onoff,
		.value	= &temp_vsync,
		.name	= "V-Sync",
	},
	{
		.type	= menu_type_button_cmd,
		.func	= &VID_ApplyChanges,
		.name	= "Apply changes",
	},
};

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
};

static void VID_ApplyChanges(void)
{
	if (current_mode == -2)
	{
		Cvar_SetValue("vid_width", custom_width);
		Cvar_SetValue("vid_height", custom_height);
	}
	else
	{
		Cvar_SetValue("vid_width", vid_modes[current_mode].width);
		Cvar_SetValue("vid_height", vid_modes[current_mode].height);
	}
	Cvar_SetValue("vid_fullscreen", temp_fullscreen);
	Cvar_SetValue("vid_vsync", temp_vsync);
	Cbuf_AddText("vid_restart\n");
	current_mode = UNSET_MODE;
}

static void VID_Menu_Draw(void)
{
	size_t			i, y;
	const char		*title;
	vid_option_t	*o;

	y = 4;

	M_DrawPlaque();
	M_DrawMenuHeader("gfx/p_option.lmp");

	y += 28;

	title = "Video Options";
	M_PrintWhite((SCREEN_WIDTH - FONT_WIDTH * strlen(title)) / 2, y, title);

	y += 2 * FONT_WIDTH;

	if (Q_UNLIKELY(current_mode == UNSET_MODE))
	{
		for (i = 0; i < Q_ARRLEN(vid_modes); ++i)
		{
			if (vid_modes[i].width == vid.width && vid_modes[i].height == vid.height)
			{
				current_mode = i;
				break;
			}
		}
		if (current_mode == UNSET_MODE)
		{
			current_mode = CUSTOM_MODE;
			custom_width = vid.width;
			custom_height = vid.height;
		}

		temp_fullscreen = fullscreen;
		temp_vsync = vsync;
	}

	for (i = 0; i < Q_ARRLEN(vid_opts); ++i, y += FONT_WIDTH)
	{
		o = &vid_opts[i];
		M_Print(16 + 18 * FONT_WIDTH - strlen(o->name) * FONT_WIDTH, y, o->name);
		switch (o->type)
		{
			case menu_type_onoff:
				M_DrawCheckbox(188, y, !!*o->value);
				break;
			case menu_type_button_cmd:
				break;
			case menu_type_mode:
				if (current_mode == CUSTOM_MODE)
				{
					M_Print(188, y, va("Custom (%ux%u)", custom_width, custom_height));
				}
				else
				{
					M_Print(188, y, va("%ux%u", vid_modes[current_mode].width, vid_modes[current_mode].height));
				}
				break;
		}

		if (vid_opt_current == i)
		{
			M_DrawMenuCursor(168, y);
		}
	}
}

static void VID_Menu_Key(int key)
{
	int				value;
	vid_option_t	*o;

	o = &vid_opts[vid_opt_current];

	switch (key)
	{
		case K_ESCAPE:
			S_LocalSound ("misc/menu1.wav");
			M_Menu_Options_f ();
			break;

		case K_LEFTARROW:
		case K_RIGHTARROW:
			S_LocalSound ("misc/menu3.wav");
			switch (o->type)
			{
				case menu_type_onoff:
					*o->value = !*o->value;
					break;

				case menu_type_button_cmd:
					break;

				case menu_type_mode:
					value = current_mode;
					if (value == -2)
					{
						value = key == K_LEFTARROW ? Q_ARRLEN(vid_modes) - 1 : 0;
					}
					else
					{
						value = value + (key == K_LEFTARROW ? -1 : 1);
						if (value < 0)
						{
							if (custom_width != -1)
							{
								value = CUSTOM_MODE;
							}
							else
							{
								value = Q_ARRLEN(vid_modes) - 1;
							}
						}
						else if (value >= (int)Q_ARRLEN(vid_modes))
						{
							if (custom_width != -1)
							{
								value = CUSTOM_MODE;
							}
							else
							{
								value = 0;
							}
						}
					}
					current_mode = value;
					break;
			}
			break;

		case K_DOWNARROW:
		case K_UPARROW:
			S_LocalSound ("misc/menu1.wav");
			value = vid_opt_current + (key == K_DOWNARROW ? 1 : -1);
			if (value < 0)
			{
				value = Q_ARRLEN(vid_opts) - 1;
			}
			else if (value >= (int)Q_ARRLEN(vid_opts))
			{
				value = 0;
			}
			vid_opt_current = value;
			break;

		case K_ENTER:
			switch (o->type)
			{
				case menu_type_onoff:
					*o->value = !*o->value;
					break;

				case menu_type_button_cmd:
					o->func();
					break;

				case menu_type_mode:
					break;

				default:
					break;
			}
	}
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
