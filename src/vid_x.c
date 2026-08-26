/*
 * Copyright (C) 1996-1997 Id Software, Inc.
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
// vid_x.c -- general x video driver

#include <time.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XShm.h>

#include "sys_unix.h"
#include "quakedef.h"

unsigned short d_8to16table[256];

static cvar_t	vid_vsync = {"vid_vsync", "1", CVAR_ARCHIVE};
static cvar_t	vid_fullscreen = {"vid_fullscreen", "0", CVAR_ARCHIVE};
static cvar_t	vid_refreshrate = {"vid_refreshrate", "60", CVAR_ARCHIVE};

static qboolean			use_shm;
static Display			*x_disp;
static Colormap			x_cmap;
static Window			x_win;
static GC				x_gc;
static Visual			*x_vis;
static XVisualInfo		*x_visinfo;
static int				x_shmeventtype;
static qboolean			oktodraw = false;
static int				current_framebuffer;
static XImage			*x_framebuffer[2] = {NULL, NULL};
static XShmSegmentInfo	x_shminfo[2];

static Atom WM_DELETE_WINDOW;
static Atom _NET_WM_STATE;
static Atom _NET_WM_STATE_MAXIMIZED_VERT;
static Atom _NET_WM_STATE_MAXIMIZED_HORZ;
static Atom _NET_WM_STATE_FULLSCREEN;
static Atom _NET_WM_STATE_FOCUSED;
static Atom _NET_WM_STATE_HIDDEN;

enum
{
	WINDOW_MAXIMIZED	= 1 << 0,
	WINDOW_FULLSCREEN	= 1 << 1,
	WINDOW_FOCUSED		= 1 << 2,
	WINDOW_HIDDEN		= 1 << 3,
};

static int window_state;
static qboolean vid_changed;
static byte current_palette[768];

static void VID_RegisterMenu(void);

#define COMMON_XINPUT_FLAGS \
	(StructureNotifyMask \
	| ExposureMask \
	| PropertyChangeMask \
	| EnterWindowMask \
	| LeaveWindowMask \
	| KeyPressMask \
	| KeyReleaseMask \
	| ButtonPressMask \
	| ButtonReleaseMask)

typedef uint16_t pixel16_t;
typedef uint32_t pixel24_t;

static union
{
	pixel16_t			p16[256];
	pixel24_t			p24[256];
} color_palette;

static long				r_shift, g_shift, b_shift;
static unsigned long	r_mask, g_mask, b_mask;

static uint64_t			frame_start;
static float			fps_sum;
static uint64_t			frame_time;
static uint64_t			frame;

static void InitColorsShiftAndMask(void)
{
	unsigned int x;

	r_mask	= x_vis->red_mask;
	g_mask	= x_vis->green_mask;
	b_mask	= x_vis->blue_mask;

	for (r_shift = -8, x = 1; x < r_mask; x = x << 1, r_shift++);
	for (g_shift = -8, x = 1; x < g_mask; x = x << 1, g_shift++);
	for (b_shift = -8, x = 1; x < b_mask; x = x << 1, b_shift++);
}

static inline unsigned int GetColor(int color, int mask, int shift)
{
	if (shift > 0)			return (color << (shift)) & mask;
	else if (r_shift < 0)	return (color >> (-shift)) & mask;
	else					return (color & mask);
}

static inline unsigned int GetPixelValue(int r, int g, int b)
{
	return GetColor(r, r_mask, r_shift) | GetColor(g, g_mask, g_shift) | GetColor(b, b_mask, b_shift);
}

static void Fixup16(XImage *framebuf, int x, int y, int width, int height)
{
	int			yi;
	uint8_t		*src;
	pixel16_t	*dest;
	int			count, n;

	if (Q_UNLIKELY((x < 0) || (y < 0))) return;

	for (yi = y; yi < (y + height); yi++)
	{
		src = (uint8_t *)&framebuf->data[yi * framebuf->bytes_per_line];

		// Duff's Device
		count	= width;
		n		= (count + 7) / 8;
		dest	= ((pixel16_t *)src) + x + width - 1;
		src		+= x + width - 1;

		switch (count % 8)
		{
			// format off
			case 0:	do {	*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 7:			*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 6:			*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 5:			*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 4:			*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 3:			*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 2:			*dest-- = color_palette.p16[*src--]; Q_FALLTHROUGH;
			case 1:			*dest-- = color_palette.p16[*src--];
					} while (--n > 0);
			// format on
		}
	}
}

static void Fixup24(XImage *framebuf, int x, int y, int width, int height)
{
	int			yi;
	uint8_t		*src;
	pixel24_t	*dest;
	int			count, n;

	if (Q_UNLIKELY((x < 0) || (y < 0))) return;

	for (yi = y; yi < (y + height); yi++)
	{
		src = (uint8_t *)&framebuf->data[yi * framebuf->bytes_per_line];

		// Duff's Device
		count	= width;
		n		= (count + 7) / 8;
		dest	= ((pixel24_t *)src) + x + width - 1;
		src		+= x + width - 1;

		switch (count % 8)
		{
			// format off
			case 0:	do {	*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 7:			*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 6:			*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 5:			*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 4:			*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 3:			*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 2:			*dest-- = color_palette.p24[*src--]; Q_FALLTHROUGH;
			case 1:			*dest-- = color_palette.p24[*src--];
					} while (--n > 0);
			// format on
		}
	}
}

static void CreateAtoms(void)
{
#define CREATE_ATOM(a) a = XInternAtom(x_disp, #a, True)
	CREATE_ATOM(WM_DELETE_WINDOW);
	CREATE_ATOM(_NET_WM_STATE);
	CREATE_ATOM(_NET_WM_STATE_MAXIMIZED_VERT);
	CREATE_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ);
	CREATE_ATOM(_NET_WM_STATE_FULLSCREEN);
	CREATE_ATOM(_NET_WM_STATE_FOCUSED);
	CREATE_ATOM(_NET_WM_STATE_HIDDEN);
}

static void SetInitialWindowState(int ws)
{
	size_t count = 0;
	Atom atoms[8];

	if (ws & WINDOW_MAXIMIZED)
	{
		atoms[count++] = _NET_WM_STATE_MAXIMIZED_VERT;
		atoms[count++] = _NET_WM_STATE_MAXIMIZED_HORZ;
	}
	else if (ws & WINDOW_FULLSCREEN)
	{
		atoms[count++] = _NET_WM_STATE_FULLSCREEN;
	}

	if (count)
	{
		XChangeProperty(x_disp, x_win, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, Q_ARRLEN(atoms));
	}
	else
	{
		XDeleteProperty(x_disp, x_win, _NET_WM_STATE);
	}
}

static void SetFullscreen(qboolean fullscreen)
{
	XEvent event = {0};

	if (fullscreen == !!(window_state & WINDOW_FULLSCREEN))
	{
		return;
	}

	event.type = ClientMessage;
	event.xclient.window = x_win;
	event.xclient.message_type = _NET_WM_STATE;
	event.xclient.format = 32;

	event.xclient.data.l[0] = fullscreen ? 1 : 0; // 1 = add, 0 = remove
	event.xclient.data.l[1] = _NET_WM_STATE_FULLSCREEN;
	event.xclient.data.l[2] = 0;
	event.xclient.data.l[3] = 1; // source: application
	event.xclient.data.l[4] = 0;

	XSendEvent(
		x_disp,
		DefaultRootWindow(x_disp),
		False,
		SubstructureRedirectMask | SubstructureNotifyMask,
		&event);

	XFlush(x_disp);
}

static void VID_Restart(void)
{
	SetFullscreen(!!vid_fullscreen.value);
	frame_time = NSEC_IN_SEC / (unsigned)vid_refreshrate.value;
	vid_changed = false;
}

static void VID_Changed(void)
{
	vid_changed = true;
}

static Cursor CreateNullCursor(Display *display, Window root)
{
	Pixmap		cursormask;
	XGCValues	xgc;
	GC			gc;
	XColor		dummycolour;
	Cursor		cursor;

	cursormask		= XCreatePixmap(display, root, 1, 1, 1 /*depth*/);
	xgc.function	= GXclear;
	gc				= XCreateGC(display, cursormask, GCFunction, &xgc);
	XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
	dummycolour.pixel	= 0;
	dummycolour.red		= 0;
	dummycolour.flags	= 04;
	cursor				= XCreatePixmapCursor(display, cursormask, cursormask, &dummycolour, &dummycolour, 0, 0);
	XFreePixmap(display, cursormask);
	XFreeGC(display, gc);
	return cursor;
}

static void ResetCursorPosition(void)
{
	XSelectInput(x_disp, x_win, COMMON_XINPUT_FLAGS);
	XWarpPointer(x_disp, None, x_win, 0, 0, 0, 0, (vid.width / 2), (vid.height / 2));
	XSelectInput(x_disp, x_win, COMMON_XINPUT_FLAGS | PointerMotionMask);
}

static void ResetFrameBuffer(void)
{
	int mem;
	int pwidth;

	if (x_framebuffer[0])
	{
		free(x_framebuffer[0]->data);
		free(x_framebuffer[0]);
	}

	D_AllocateBuffer();

	pwidth = x_visinfo->depth / 8;
	if (pwidth == 3) pwidth = 4;
	mem = ((vid.width * pwidth + 7) & ~7) * vid.height;

	x_framebuffer[0] = XCreateImage(x_disp,
		x_vis,
		x_visinfo->depth,
		ZPixmap,
		0,
		malloc(mem),
		vid.width,
		vid.height,
		32,
		0);

	if (!x_framebuffer[0])
	{
		Sys_Error("VID: XCreateImage failed\n");
	}

	vid.buffer		= (byte *)(x_framebuffer[0]);
	vid.conbuffer	= vid.buffer;
}

static void ResetSharedFrameBuffers(void)
{
	int size;
	int minsize = getpagesize();
	int frm;

	D_AllocateBuffer();

	for (frm = 0 ; frm < 2 ; frm++)
	{
		// free up old frame buffer memory
		if (x_framebuffer[frm])
		{
			XShmDetach(x_disp, &x_shminfo[frm]);
			free(x_framebuffer[frm]);
			shmdt(x_shminfo[frm].shmaddr);
		}

		// create the image
		x_framebuffer[frm] = XShmCreateImage(
			x_disp,
			x_vis,
			x_visinfo->depth,
			ZPixmap,
			0,
			&x_shminfo[frm],
			vid.width,
			vid.height);

		// grab shared memory
		size = x_framebuffer[frm]->bytes_per_line * x_framebuffer[frm]->height;
		if (size < minsize)
			Sys_Error("VID: Window must use at least %d bytes\n", minsize);

		x_shminfo[frm].shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
		if (x_shminfo[frm].shmid == -1)
			Sys_Error("VID: Could not get any shared memory\n");

		// attach to the shared memory segment
		x_shminfo[frm].shmaddr = (void *)shmat(x_shminfo[frm].shmid, 0, 0);

		Sys_DPrintf("VID: shared memory id=%d, addr=0x%lx\n", x_shminfo[frm].shmid, (long)x_shminfo[frm].shmaddr);

		x_framebuffer[frm]->data = x_shminfo[frm].shmaddr;

		// get the X server to attach to it
		if (!XShmAttach(x_disp, &x_shminfo[frm]))
			Sys_Error("VID: XShmAttach() failed\n");
		XSync(x_disp, 0);
		shmctl(x_shminfo[frm].shmid, IPC_RMID, 0);
	}
}

// Called at startup to set up translation tables, takes 256 8 bit RGB values
// the palette data will go away after the call, so it must be copied off if
// the video driver will need it again

void VID_Init(unsigned char *palette)
{
	int			pnum, i, tmp, screen;
	XVisualInfo template;
	int			num_visuals;
	int			template_mask;
	int			window_state = 0;

	VID_RegisterMenu();

	Cvar_RegisterVariable(&vid_vsync);
	Cvar_RegisterVariable(&vid_fullscreen);
	Cvar_RegisterVariable(&vid_refreshrate);

	vid_fullscreen.callback = &VID_Changed;
	vid_refreshrate.callback = &VID_Changed;

	Cmd_AddCommand("vid_restart", &VID_Restart);

	vid.width			= SCREEN_WIDTH * 3;
	vid.height			= SCREEN_HEIGHT * 3;
	vid.maxwarpwidth	= WARP_WIDTH;
	vid.maxwarpheight	= WARP_HEIGHT;
	vid.numpages		= 2;
	vid.colormap		= host_colormap;
	vid.fullbright		= 256 - LittleLong(*((int *)vid.colormap + 2048));

	// open the display
	x_disp = XOpenDisplay(NULL);

	if (!x_disp)
	{
		if (getenv("DISPLAY"))
			Sys_Error("VID: Could not open display [%s]\n", getenv("DISPLAY"));
		else
			Sys_Error("VID: Could not open local display\n");
	}

	CreateAtoms();

	XkbSetDetectableAutoRepeat(x_disp, true, &tmp);

	// for debugging only
	XSynchronize(x_disp, True);

	// check for command-line window size
	if ((pnum = COM_CheckParm("-winsize")))
	{
		if (pnum >= com_argc - 2)
			Sys_Error("VID: -winsize <width> <height>\n");
		vid.width	= Q_atoi(com_argv[pnum + 1]);
		vid.height	= Q_atoi(com_argv[pnum + 2]);
		if (!vid.width || !vid.height)
			Sys_Error("VID: Bad window width/height\n");
	}
	if ((pnum = COM_CheckParm("-width")))
	{
		if (pnum >= com_argc - 1)
			Sys_Error("VID: -width <width>\n");
		vid.width = Q_atoi(com_argv[pnum + 1]);
		if (!vid.width)
			Sys_Error("VID: Bad window width\n");
	}
	if ((pnum = COM_CheckParm("-height")))
	{
		if (pnum >= com_argc - 1)
			Sys_Error("VID: -height <height>\n");
		vid.height = Q_atoi(com_argv[pnum + 1]);
		if (!vid.height)
			Sys_Error("VID: Bad window height\n");
	}

	template_mask = 0;

	// specify a visual id
	if ((pnum = COM_CheckParm("-visualid")))
	{
		if (pnum >= com_argc - 1)
			Sys_Error("VID: -visualid <id#>\n");
		template.visualid	= Q_atoi(com_argv[pnum + 1]);
		template_mask		= VisualIDMask;
	}
	else // If not specified, use default visual
	{
		screen				= XDefaultScreen(x_disp);
		template.visualid	= XVisualIDFromVisual(XDefaultVisual(x_disp, screen));
		template_mask		= VisualIDMask;
	}

	// pick a visual- warn if more than one was available
	x_visinfo = XGetVisualInfo(x_disp, template_mask, &template, &num_visuals);
	if (num_visuals > 1)
	{
		Sys_Printf("Found more than one visual id at depth %d:\n", template.depth);
		for (i = 0 ; i < num_visuals ; i++)
			Sys_Printf("	-visualid %d\n", (int)(x_visinfo[i].visualid));
	}
	else if (num_visuals == 0)
	{
		if (template_mask == VisualIDMask)
			Sys_Error("VID: Bad visual id %lu\n", template.visualid);
		else
			Sys_Error("VID: No visuals at depth %d\n", template.depth);
	}

	Sys_DPrintf("Using visualid %d:\n", (int)(x_visinfo->visualid));
	Sys_DPrintf("  screen        %d\n", x_visinfo->screen);
	Sys_DPrintf("  red_mask      0x%08x\n", (int)(x_visinfo->red_mask));
	Sys_DPrintf("  green_mask    0x%08x\n", (int)(x_visinfo->green_mask));
	Sys_DPrintf("  blue_mask     0x%08x\n", (int)(x_visinfo->blue_mask));
	Sys_DPrintf("  colormap_size %d\n", x_visinfo->colormap_size);
	Sys_DPrintf("  bits_per_rgb  %d\n", x_visinfo->bits_per_rgb);

	x_vis = x_visinfo->visual;

	InitColorsShiftAndMask();

	// setup attributes for main window
	{
		int						attribmask = CWEventMask | CWColormap | CWBorderPixel;
		XSetWindowAttributes	attribs;
		Colormap				tmpcmap;

		tmpcmap = XCreateColormap(
			x_disp,
			XRootWindow(x_disp, x_visinfo->screen),
			x_vis,
			AllocNone);

		attribs.event_mask		= COMMON_XINPUT_FLAGS;
		attribs.border_pixel	= 0;
		attribs.colormap		= tmpcmap;

		// create the main window
		x_win = XCreateWindow(
			/* display		= */ x_disp,
			/* parent		= */ XRootWindow(x_disp, x_visinfo->screen),
			/* x			= */ 0,
			/* y			= */ 0,
			/* width		= */ vid.width,
			/* height		= */ vid.height,
			/* border_width	= */ 0,
			/* depth		= */ x_visinfo->depth,
			/* class		= */ InputOutput,
			/* visual		= */ x_vis,
			/* valuemask	= */ attribmask,
			/* attributes	= */ &attribs);

		XStoreName(x_disp, x_win, "xquake");

		if (x_visinfo->class != TrueColor)
			XFreeColormap(x_disp, tmpcmap);
	}

	if (x_visinfo->depth == 8)
	{
		// create and upload the palette
		if (x_visinfo->class == PseudoColor)
		{
			x_cmap = XCreateColormap(x_disp, x_win, x_vis, AllocAll);
			VID_SetPalette(palette);
			XSetWindowColormap(x_disp, x_win, x_cmap);
		}
	}

	// invisible cursor
	XDefineCursor(x_disp, x_win, CreateNullCursor(x_disp, x_win));

	// create the GC
	{
		XGCValues	xgcvalues;
		int			valuemask = GCGraphicsExposures;
		xgcvalues.graphics_exposures = False;
		x_gc = XCreateGC(x_disp, x_win, valuemask, &xgcvalues);
	}

	if (COM_CheckParm("-fullscreen"))
	{
		window_state |= WINDOW_FULLSCREEN;
	}
	else if (COM_CheckParm("-maximized"))
	{
		window_state |= WINDOW_MAXIMIZED;
	}

	SetInitialWindowState(window_state);

	// map the window
	XMapWindow(x_disp, x_win);

	// wait for first exposure event
	{
		XEvent event;
		do
		{
			XNextEvent(x_disp, &event);
			if (event.type == Expose && !event.xexpose.count)
				oktodraw = true;
		}
		while (!oktodraw);
	}
	// now safe to draw

	XSetWMProtocols(x_disp, x_win, &WM_DELETE_WINDOW, 1);

	// even if MITSHM is available, make sure it's a local connection
	if (XShmQueryExtension(x_disp))
	{
		char *displayname;
		use_shm		= true;
		displayname = (char *)getenv("DISPLAY");
		if (displayname)
		{
			char *d = displayname;
			while (*d && (*d != ':')) d++;
			if (*d) *d = 0;
			if (!(!strcasecmp(displayname, "unix") || !*displayname))
				use_shm = false;
		}
	}

	if (use_shm)
	{
		x_shmeventtype = XShmGetEventBase(x_disp) + ShmCompletion;
		ResetSharedFrameBuffers();
	}
	else
	{
		ResetFrameBuffer();
	}

	// grab the pointer
	XGrabPointer(x_disp, x_win, True, 0, GrabModeAsync, GrabModeAsync, x_win, None, CurrentTime);

	current_framebuffer = 0;
	vid.rowbytes		= x_framebuffer[0]->bytes_per_line;
	vid.buffer			= (pixel_t *)x_framebuffer[0]->data;
	vid.direct			= 0;
	vid.conbuffer		= (pixel_t *)x_framebuffer[0]->data;
	vid.conrowbytes		= vid.rowbytes;
	vid.conwidth		= vid.width;
	vid.conheight		= vid.height;
	vid.aspect			= ((float)vid.height / (float)vid.width) * ((float)SCREEN_WIDTH / 240.0f);

	frame_start = gettime_ns();
}

void VID_ShiftPalette(unsigned char *p)
{
	VID_SetPalette(p);
}

void VID_SetPalette(unsigned char *palette)
{
	size_t	i;
	XColor	colors[256];

	if (x_visinfo->depth == 16)
	{
		for (i = 0; i < Q_ARRLEN(color_palette.p16); i++)
		{
			color_palette.p16[i] = GetPixelValue(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
		}
	}
	else if (x_visinfo->depth == 24)
	{
		for (i = 0; i < Q_ARRLEN(color_palette.p24); i++)
		{
			color_palette.p24[i] = GetPixelValue(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
		}
	}

	if (x_visinfo->class == PseudoColor && x_visinfo->depth == 8)
	{
		if (palette != current_palette)
		{
			memcpy(current_palette, palette, sizeof(current_palette));
		}
		for (i = 0; i < Q_ARRLEN(colors); i++)
		{
			colors[i].pixel	= i;
			colors[i].flags	= DoRed | DoGreen | DoBlue;
			colors[i].red	= palette[i * 3] * 257;
			colors[i].green	= palette[i * 3 + 1] * 257;
			colors[i].blue	= palette[i * 3 + 2] * 257;
		}
		XStoreColors(x_disp, x_cmap, colors, Q_ARRLEN(colors));
	}
}

// Called at shutdown
void VID_Shutdown(void)
{
	if (x_disp)
	{
		XCloseDisplay(x_disp);
	}
}

static int ConvertKey(XKeyEvent *ev)
{
	int		key;
	char	buf[64];
	KeySym	keysym;

	XLookupString(ev, buf, sizeof(buf), &keysym, 0);

	switch (keysym)
	{
		case XK_KP_Page_Up:
		case XK_Page_Up:		return K_PGUP;
		case XK_KP_Page_Down:
		case XK_Page_Down:		return K_PGDN;
		case XK_KP_Home:
		case XK_Home:			return K_HOME;
		case XK_KP_End:
		case XK_End:			return K_END;
		case XK_KP_Left:
		case XK_Left:			return K_LEFTARROW;
		case XK_KP_Right:
		case XK_Right:			return K_RIGHTARROW;
		case XK_KP_Down:
		case XK_Down:			return K_DOWNARROW;
		case XK_KP_Up:
		case XK_Up:				return K_UPARROW;
		case XK_Escape:			return K_ESCAPE;
		case XK_KP_Enter:
		case XK_Return:			return K_ENTER;
		case XK_Tab:			return K_TAB;
		case XK_F1:				return K_F1;
		case XK_F2:				return K_F2;
		case XK_F3:				return K_F3;
		case XK_F4:				return K_F4;
		case XK_F5:				return K_F5;
		case XK_F6:				return K_F6;
		case XK_F7:				return K_F7;
		case XK_F8:				return K_F8;
		case XK_F9:				return K_F9;
		case XK_F10:			return K_F10;
		case XK_F11:			return K_F11;
		case XK_F12:			return K_F12;
		case XK_BackSpace:		return K_BACKSPACE;
		case XK_KP_Delete:
		case XK_Delete:			return K_DEL;
		case XK_Pause:			return K_PAUSE;
		case XK_Shift_L:
		case XK_Shift_R:		return K_SHIFT;
		case XK_Execute:
		case XK_Control_L:
		case XK_Control_R:		return K_CTRL;
		case XK_Alt_L:
		case XK_Meta_L:
		case XK_Alt_R:
		case XK_Meta_R:			return K_ALT;
		case XK_KP_Begin:		return K_AUX30;
		case XK_Insert:
		case XK_KP_Insert:		return K_INS;

		default:
			key = *(unsigned char *)buf;
			if (isascii(key) && isprint(key))
			{
				return tolower(key);
			}
			return 0;
	}
}

static int		config_notify = 0;
static unsigned	config_notify_width;
static unsigned	config_notify_height;

static void HandleWindowStateChange(void)
{
	size_t			i;
	Atom			actual_type, *states, s;
	int				actual_format, result;
	unsigned long	nitems;
	unsigned long	bytes_after;
	unsigned char	*data = NULL;
	qboolean		hmax = false, vmax = false;

	result = XGetWindowProperty(
		/* display				= */ x_disp,
		/* w					= */ x_win,
		/* property				= */ _NET_WM_STATE,
		/* long_offset			= */ 0,
		/* long_length			= */ 1024,
		/* delete				= */ False,
		/* req_type				= */ XA_ATOM,
		/* actual_type_return	= */ &actual_type,
		/* actual_format_return	= */ &actual_format,
		/* nitems_return		= */ &nitems,
		/* bytes_after_return	= */ &bytes_after,
		/* prop_return			= */ &data);

	if (result == Success && actual_type == XA_ATOM && actual_format == 32)
	{
		window_state = 0;
		states = (Atom *)data;

		for (i = 0; i < nitems; i++)
		{
			s = states[i];
			if (s == _NET_WM_STATE_FULLSCREEN)
			{
				window_state |= WINDOW_FULLSCREEN;
			}
			else if (s == _NET_WM_STATE_FOCUSED)
			{
				window_state |= WINDOW_FOCUSED;
			}
			else if (s == _NET_WM_STATE_HIDDEN)
			{
				window_state |= WINDOW_HIDDEN;
			}
			else if (s == _NET_WM_STATE_MAXIMIZED_VERT)
			{
				vmax = true;
			}
			else if (s == _NET_WM_STATE_MAXIMIZED_HORZ)
			{
				hmax = true;
			}
		}

		if (vmax && hmax)
		{
			window_state |= WINDOW_MAXIMIZED;
		}
	}

	if (data)
	{
		XFree(data);
	}
}

static inline int ConvertButton(int button)
{
	switch (button)
	{
		case 1:		return 0;
		case 2:		return 2;
		case 3:		return 1;
		default:	return -1;
	}
}

static void GetEvent(void)
{
	XEvent	x_event;
	int		b;

	XNextEvent(x_disp, &x_event);
	switch (x_event.type)
	{
		case KeyPress:
			IN_AddKey(ConvertKey(&x_event.xkey), true);
			break;

		case KeyRelease:
			IN_AddKey(ConvertKey(&x_event.xkey), false);
			break;

		case MotionNotify:
			IN_AddMouseMove(
				(float)((int)x_event.xmotion.x - (int)(vid.width / 2)),
				(float)((int)x_event.xmotion.y - (int)(vid.height / 2)));
			// move the mouse to the window center again
			ResetCursorPosition();
			break;

		case ButtonPress:
			if ((b = ConvertButton(x_event.xbutton.button)) >= 0)
				IN_AddMouseButton(b, true);
			break;

		case ButtonRelease:
			if ((b = ConvertButton(x_event.xbutton.button)) >= 0)
				IN_AddMouseButton(b, false);
			break;

		case ConfigureNotify:
			config_notify_width		= x_event.xconfigure.width;
			config_notify_height	= x_event.xconfigure.height;
			config_notify			= 1;
			break;

		case EnterNotify:
			if (mouse_avail)
			{
				XGrabPointer(x_disp, x_win, True, 0, GrabModeAsync, GrabModeAsync, x_win, None, CurrentTime);
				ResetCursorPosition();
			}
			break;

		case LeaveNotify:
			if (mouse_avail)
			{
				XUngrabPointer(x_disp, CurrentTime);
			}
			break;

		case ClientMessage:
			if (x_event.xclient.data.l[0] == (long)WM_DELETE_WINDOW)
			{
				Host_Quit_f();
			}
			break;

		case PropertyNotify:
			if (x_event.xproperty.atom == _NET_WM_STATE)
			{
				HandleWindowStateChange();
			}
			break;

		default:
			if (use_shm && x_event.type == x_shmeventtype)
			{
				oktodraw = true;
			}
	}
}

static qboolean HandleConfigNotify(void)
{
	config_notify = 0;

	if ((vid.width == config_notify_width && vid.height == config_notify_height))
	{
		return false;
	}

	vid.width	= config_notify_width;
	vid.height	= config_notify_height;

	if (use_shm)
	{
		ResetSharedFrameBuffers();
	}
	else
	{
		ResetFrameBuffer();
	}

	vid.rowbytes		= x_framebuffer[0]->bytes_per_line;
	vid.buffer			= (pixel_t *)x_framebuffer[current_framebuffer]->data;
	vid.conbuffer		= vid.buffer;
	vid.conwidth		= vid.width;
	vid.conheight		= vid.height;
	vid.conrowbytes		= vid.rowbytes;
	vid.recalc_refdef	= 1;			// force a surface cache flush
	vid.aspect			= ((float)vid.height / (float)vid.width) * ((float)SCREEN_WIDTH / 240.0f);

	Con_CheckResize();

	if (mouse_avail)
	{
		ResetCursorPosition();
	}

	frame_start = gettime_ns();

	return true;
}

static void WaitForNextFrame(void)
{
	timespec_t ts;
	int64_t diff;
	uint64_t prev_frame, next_frame, current;

	next_frame = frame_start + frame_time;
	current = gettime_ns();
	diff = next_frame - current;

	// we're ahead of time
	if (Q_LIKELY(diff > 0))
	{
		ts.tv_sec = 0;
		ts.tv_nsec = diff;
		while (nanosleep(&ts, &ts) == -1);
	}

	prev_frame = frame_start;
	frame_start = gettime_ns();

	fps_sum += (float)NSEC_IN_SEC / (frame_start - prev_frame);

	if (++frame % 60 == 0)
	{
		vid_fps = fps_sum / 60;
		fps_sum = 0;
	}
}

void VID_Update(vrect_t *rects)
{
	if (config_notify)
	{
		// if the window changes dimension, skip this frame
		if (HandleConfigNotify())
		{
			return;
		}
	}

	if (vid_changed)
	{
		VID_Restart();
	}

	// force full update if not 8bit
	if (x_visinfo->depth != 8)
	{
		scr_fullupdate = 0;
	}

	if ((window_state & WINDOW_HIDDEN) || !(window_state & WINDOW_FOCUSED))
	{
		goto vsync;
	}

	if (use_shm)
	{
		while (rects)
		{
			if (x_visinfo->depth == 16)
			{
				Fixup16(x_framebuffer[current_framebuffer], rects->x, rects->y, rects->width, rects->height);
			}
			else if (x_visinfo->depth == 24)
			{
				Fixup24(x_framebuffer[current_framebuffer], rects->x, rects->y, rects->width, rects->height);
			}
			if (!XShmPutImage(x_disp,
				x_win,
				x_gc,
				x_framebuffer[current_framebuffer],
				rects->x,
				rects->y,
				rects->x,
				rects->y,
				rects->width,
				rects->height,
				True))
			{
				Sys_Error("VID_Update: XShmPutImage failed\n");
			}
			oktodraw = false;
			while (!oktodraw)
			{
				GetEvent();
			}
			rects = rects->pnext;
		}
		current_framebuffer = !current_framebuffer;
		vid.buffer			= (pixel_t *)x_framebuffer[current_framebuffer]->data;
		vid.conbuffer		= vid.buffer;
		XSync(x_disp, False);
	}
	else
	{
		while (rects)
		{
			if (x_visinfo->depth == 16)
			{
				Fixup16(x_framebuffer[current_framebuffer], rects->x, rects->y, rects->width, rects->height);
			}
			else if (x_visinfo->depth == 24)
			{
				Fixup24(x_framebuffer[current_framebuffer], rects->x, rects->y, rects->width, rects->height);
			}

			XPutImage(x_disp,
				x_win,
				x_gc,
				x_framebuffer[0],
				rects->x,
				rects->y,
				rects->x,
				rects->y,
				rects->width,
				rects->height);

			rects = rects->pnext;
		}
		XSync(x_disp, False);
	}

vsync:
	if (vid_vsync.value)
	{
		WaitForNextFrame();
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

void IN_ReadEvents(void)
{
	while (XPending(x_disp)) GetEvent();
}

static option_t options[] = {
	(option_t){
		.type		= option_onoff,
		.name		= "Fullscreen",
		.onoff		= {
			.cvar	= &vid_fullscreen,
		}
	},
	(option_t){
		.type	= option_onoff,
		.name	= "V-Sync",
		.onoff	= {
			.cvar	= &vid_vsync,
		}
	},
	(option_t){
		.type		= option_slider,
		.name		= "Refresh rate",
		.slider		= {
			.cvar	= &vid_refreshrate,
			.fmt	= "%0.0f",
			.flags	= 0,
			.min	= 20,
			.max	= 120,
			.step	= 5,
		},
	}
};

static menu_t vid_menu = {
	.cursor			= 0,
	.options_count	= Q_ARRLEN(options),
	.options		= options,
	.title			= "Video Options",
	.enter			= NULL,
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

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
