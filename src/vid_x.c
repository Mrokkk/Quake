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
 *
 */
// vid_x.c -- general x video driver

#include <time.h>
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
#include "d_local.h"

static cvar_t	m_filter = {"m_filter", "0", true};
static cvar_t	vid_vsync = {"vid_vsync", "1", true};
static cvar_t	vid_fullscreen = {"vid_fullscreen", "0", true};
static cvar_t	vid_refreshrate = {"vid_refreshrate", "60", true};

static qboolean	mouse_avail;
static int		mouse_buttons = 3;
static int		mouse_oldbuttonstate;
static int		mouse_buttonstate;
static float	mouse_x, mouse_y;
static float	old_mouse_x, old_mouse_y;
static int		ignorenext;

typedef struct
{
	int input;
	int output;
} keymap_t;

unsigned short d_8to16table[256];

int	d_con_indirect = 0;

static qboolean		use_shm;
static Display		*x_disp;
static Colormap		x_cmap;
static Window		x_win;
static GC			x_gc;
static Visual		*x_vis;
static XVisualInfo	*x_visinfo;

static int x_shmeventtype;

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

static long X11_highhunkmark;
static long X11_buffersize;

static void VID_Menu_Draw(void);
static void VID_Menu_Key(int key);

static menu_t vid_menu = {
	.draw	= &VID_Menu_Draw,
	.key	= &VID_Menu_Key,
};

int		vid_surfcachesize;
void	*vid_surfcache;

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

static pixel16_t		st2d_8to16table[256];
static pixel24_t		st2d_8to24table[256];
static int				shiftmask_fl = 0;
static long				r_shift, g_shift, b_shift;
static unsigned long	r_mask, g_mask, b_mask;

static uint64_t frame_start;
static float fps_sum;
static uint64_t frame_time;
static uint64_t frame;

static void shiftmask_init()
{
	unsigned int x;

	r_mask	= x_vis->red_mask;
	g_mask	= x_vis->green_mask;
	b_mask	= x_vis->blue_mask;
	for (r_shift = -8, x = 1; x < r_mask; x = x << 1) r_shift++;
	for (g_shift = -8, x = 1; x < g_mask; x = x << 1) g_shift++;
	for (b_shift = -8, x = 1; x < b_mask; x = x << 1) b_shift++;
	shiftmask_fl = 1;
}

static pixel16_t Rgb16(int r, int g, int b)
{
	pixel16_t p;

	if (shiftmask_fl == 0)shiftmask_init();
	p = 0;

	if (r_shift > 0)
	{
		p = (r << (r_shift)) & r_mask;
	}
	else if (r_shift < 0)
	{
		p = (r >> (-r_shift)) & r_mask;
	}
	else p |= (r & r_mask);

	if (g_shift > 0)
	{
		p |= (g << (g_shift)) & g_mask;
	}
	else if (g_shift < 0)
	{
		p |= (g >> (-g_shift)) & g_mask;
	}
	else p |= (g & g_mask);

	if (b_shift > 0)
	{
		p |= (b << (b_shift)) & b_mask;
	}
	else if (b_shift < 0)
	{
		p |= (b >> (-b_shift)) & b_mask;
	}
	else p |= (b & b_mask);

	return p;
}

static pixel24_t Rgb24(int r, int g, int b)
{
	pixel24_t p;

	if (shiftmask_fl == 0)shiftmask_init();
	p = 0;

	if (r_shift > 0)
	{
		p = (r << (r_shift)) & r_mask;
	}
	else if (r_shift < 0)
	{
		p = (r >> (-r_shift)) & r_mask;
	}
	else p |= (r & r_mask);

	if (g_shift > 0)
	{
		p |= (g << (g_shift)) & g_mask;
	}
	else if (g_shift < 0)
	{
		p |= (g >> (-g_shift)) & g_mask;
	}
	else p |= (g & g_mask);

	if (b_shift > 0)
	{
		p |= (b << (b_shift)) & b_mask;
	}
	else if (b_shift < 0)
	{
		p |= (b >> (-b_shift)) & b_mask;
	}
	else p |= (b & b_mask);

	return p;
}

static void Fixup16(XImage *framebuf, int x, int y, int width, int height)
{
	int			yi;
	uint8_t		*src;
	pixel16_t	*dest;
	int			count, n;

	if ((x < 0) || (y < 0)) return;

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
			case 0:	do {	*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 7:			*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 6:			*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 5:			*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 4:			*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 3:			*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 2:			*dest-- = st2d_8to16table[*src--]; Q_FALLTHROUGH;
			case 1:			*dest-- = st2d_8to16table[*src--];
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

	if ((x < 0) || (y < 0)) return;

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
			case 0:	do {	*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 7:			*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 6:			*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 5:			*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 4:			*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 3:			*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 2:			*dest-- = st2d_8to24table[*src--]; Q_FALLTHROUGH;
			case 1:			*dest-- = st2d_8to24table[*src--];
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

// ========================================================================
// makes a null cursor
// ========================================================================

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

	if (d_pzbuffer)
	{
		D_FlushCaches();
		Hunk_FreeToHighMark(X11_highhunkmark);
		d_pzbuffer = NULL;
	}
	X11_highhunkmark = Hunk_HighMark();

	// alloc an extra line in case we want to wrap, and allocate the z-buffer
	X11_buffersize = vid.width * vid.height * sizeof (*d_pzbuffer);

	vid_surfcachesize = D_SurfaceCacheForRes(vid.width, vid.height);

	X11_buffersize += vid_surfcachesize;

	d_pzbuffer = Hunk_HighAllocName(X11_buffersize, "video");
	if (d_pzbuffer == NULL)
		Sys_Error("Not enough memory for video mode\n");

	vid_surfcache = (byte *)d_pzbuffer
		+ vid.width * vid.height * sizeof (*d_pzbuffer);

	D_InitCaches(vid_surfcache, vid_surfcachesize);

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

	if (d_pzbuffer)
	{
		D_FlushCaches();
		Hunk_FreeToHighMark(X11_highhunkmark);
		d_pzbuffer = NULL;
	}

	X11_highhunkmark = Hunk_HighMark();

	// alloc an extra line in case we want to wrap, and allocate the z-buffer
	X11_buffersize = vid.width * vid.height * sizeof (*d_pzbuffer);

	vid_surfcachesize = D_SurfaceCacheForRes(vid.width, vid.height);

	X11_buffersize += vid_surfcachesize;

	d_pzbuffer = Hunk_HighAllocName(X11_buffersize, "video");
	if (d_pzbuffer == NULL)
		Sys_Error("Not enough memory for video mode\n");

	vid_surfcache = (byte *)d_pzbuffer
		+ vid.width * vid.height * sizeof (*d_pzbuffer);

	D_InitCaches(vid_surfcache, vid_surfcachesize);

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
		x_framebuffer[frm] = XShmCreateImage(x_disp,
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

		x_shminfo[frm].shmid	= shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
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

	M_RegisterVideoMenu(&vid_menu);

	Cvar_RegisterVariable(&vid_vsync);
	Cvar_RegisterVariable(&vid_fullscreen);
	Cvar_RegisterVariable(&vid_refreshrate);

	vid_fullscreen.callback = &VID_Changed;
	vid_refreshrate.callback = &VID_Changed;

	Cmd_AddCommand("vid_restart", &VID_Restart);

	ignorenext			= 0;
	vid.width			= SCREEN_WIDTH * 3;
	vid.height			= SCREEN_HEIGHT * 3;
	vid.maxwarpwidth	= WARP_WIDTH;
	vid.maxwarpheight	= WARP_HEIGHT;
	vid.numpages		= 2;
	vid.colormap		= host_colormap;
	vid.fullbright		= 256 - LittleLong(*((int *)vid.colormap + 2048));

	srandom(getpid());

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
	int		i;
	XColor	colors[256];

	for (i = 0; i < 256; i++)
	{
		st2d_8to16table[i]	= Rgb16(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
		st2d_8to24table[i]	= Rgb24(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
	}

	if (x_visinfo->class == PseudoColor && x_visinfo->depth == 8)
	{
		if (palette != current_palette)
			memcpy(current_palette, palette, 768);
		for (i = 0 ; i < 256 ; i++)
		{
			colors[i].pixel = i;
			colors[i].flags = DoRed | DoGreen | DoBlue;
			colors[i].red	= palette[i * 3] * 257;
			colors[i].green = palette[i * 3 + 1] * 257;
			colors[i].blue	= palette[i * 3 + 2] * 257;
		}
		XStoreColors(x_disp, x_cmap, colors, 256);
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

int XLateKey(XKeyEvent *ev)
{
	int		key;
	char	buf[64];
	KeySym	keysym;

	XLookupString(ev, buf, sizeof buf, &keysym, 0);

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

		case XK_KP_Multiply:	return '*';
		case XK_KP_Add:			return '+';
		case XK_KP_Subtract:	return '-';
		case XK_KP_Divide:		return '/';

		default:
			key = *(unsigned char *)buf;
			if (key >= 'A' && key <= 'Z')
				key = key - 'A' + 'a';
			return key;
	}
}

struct
{
	int key;
	int down;
} keyq[64];

static int	keyq_head	= 0;
static int	keyq_tail	= 0;

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

static void GetEvent(void)
{
	XEvent	x_event;
	int		b;

	XNextEvent(x_disp, &x_event);
	switch (x_event.type)
	{
		case KeyPress:
			keyq[keyq_head].key		= XLateKey(&x_event.xkey);
			keyq[keyq_head].down	= true;
			keyq_head				= (keyq_head + 1) & 63;
			break;
		case KeyRelease:
			keyq[keyq_head].key		= XLateKey(&x_event.xkey);
			keyq[keyq_head].down	= false;
			keyq_head				= (keyq_head + 1) & 63;
			break;

		case MotionNotify:
			mouse_x = (float)((int)x_event.xmotion.x - (int)(vid.width / 2));
			mouse_y = (float)((int)x_event.xmotion.y - (int)(vid.height / 2));

			// move the mouse to the window center again
			ResetCursorPosition();
			break;

		case ButtonPress:
			b = -1;
			if (x_event.xbutton.button == 1)
				b = 0;
			else if (x_event.xbutton.button == 2)
				b = 2;
			else if (x_event.xbutton.button == 3)
				b = 1;
			if (b >= 0)
				mouse_buttonstate |= 1 << b;
			break;

		case ButtonRelease:
			b = -1;
			if (x_event.xbutton.button == 1)
				b = 0;
			else if (x_event.xbutton.button == 2)
				b = 2;
			else if (x_event.xbutton.button == 3)
				b = 1;
			if (b >= 0)
				mouse_buttonstate &= ~(1 << b);
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
	Con_Clear_f();

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

// flushes the given rectangles from the view buffer to the screen
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
				Fixup16(x_framebuffer[current_framebuffer], rects->x, rects->y, rects->width,	rects->height);
			}
			else if (x_visinfo->depth == 24)
			{
				Fixup24(x_framebuffer[current_framebuffer], rects->x, rects->y, rects->width,	rects->height);
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

void Sys_SendKeyEvents(void)
{
	// get events from x server
	if (x_disp)
	{
		while (XPending(x_disp)) GetEvent();
		while (keyq_head != keyq_tail)
		{
			Key_Event(keyq[keyq_tail].key, keyq[keyq_tail].down);
			keyq_tail = (keyq_tail + 1) & (Q_ARRLEN(keyq) - 1);
		}
	}
}

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height)
{
	// direct drawing of the "accessing disk" icon isn't supported under Linux
	Q_UNUSED(x && y && pbitmap && width && height);
}

void D_EndDirectRect(int x, int y, int width, int height)
{
	// direct drawing of the "accessing disk" icon isn't supported under Linux
	Q_UNUSED(x && y && width && height);
}

void IN_Init(void)
{
	Cvar_RegisterVariable(&m_filter);

	if (COM_CheckParm("-nomouse"))
	{
		return;
	}

	mouse_x = mouse_y = 0.0;
	mouse_avail = 1;
}

void IN_Shutdown(void)
{
	mouse_avail = 0;
}

void IN_Commands(void)
{
	int i;

	if (!mouse_avail) return;

	for (i = 0; i < mouse_buttons; i++)
	{
		if ((mouse_buttonstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i)))
			Key_Event(K_MOUSE1 + i, true);

		if (!(mouse_buttonstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i)))
			Key_Event(K_MOUSE1 + i, false);
	}
	mouse_oldbuttonstate = mouse_buttonstate;
}

void IN_Move(usercmd_t *cmd)
{
	if (!mouse_avail)
		return;

	if (m_filter.value)
	{
		mouse_x = (mouse_x + old_mouse_x) * 0.5;
		mouse_y = (mouse_y + old_mouse_y) * 0.5;
	}

	old_mouse_x = mouse_x;
	old_mouse_y = mouse_y;

	mouse_x *= sensitivity.value;
	mouse_y *= sensitivity.value;

	if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;
	if (in_mlook.state & 1)
		V_StopPitchDrift();

	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}
	mouse_x = mouse_y = 0.0;
}

typedef enum menu_type_s
{
	menu_type_value,
	menu_type_onoff,
} menu_type_t;

typedef struct vid_option_s
{
	menu_type_t	type;
	union
	{
		cvar_t*	cvar;
		void	(*func)(void);
	};
	const char	*name;
	int			min, max;
} vid_option_t;

static unsigned vid_opt_current;

static vid_option_t vid_opts[] = {
	{
		.type	= menu_type_onoff,
		.cvar	= &vid_fullscreen,
		.name	= "Fullscreen",
	},
	{
		.type	= menu_type_onoff,
		.cvar	= &vid_vsync,
		.name	= "V-Sync",
	},
	{
		.type	= menu_type_value,
		.cvar	= &vid_refreshrate,
		.name	= "Refresh rate",
		.min	= 20,
		.max	= 120,
	},
};

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
	M_PrintWhite((SCREEN_WIDTH - CHAR_WIDTH * strlen(title)) / 2, y, title);

	y += 2 * CHAR_WIDTH;

	for (i = 0; i < Q_ARRLEN(vid_opts); ++i, y += CHAR_WIDTH)
	{
		o = &vid_opts[i];
		M_Print (16 + 22 * CHAR_WIDTH - strlen(o->name) * CHAR_WIDTH, y, o->name);
		switch (o->type)
		{
			case menu_type_onoff:
				M_DrawCheckbox(220, y, !!o->cvar->value);
				break;
			case menu_type_value:
				M_Print(220, y, va("%u", (unsigned)o->cvar->value));
				break;
		}

		if (vid_opt_current == i)
		{
			M_DrawMenuCursor(200, y);
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
					Cvar_SetValue(o->cvar->name, !o->cvar->value);
					break;

				case menu_type_value:
					value = o->cvar->value + (key == K_LEFTARROW ? -5 : 5);
					if (value > o->max)
					{
						value = o->max;
					}
					else if (value < o->min)
					{
						value = o->min;
					}
					Cvar_SetValue(o->cvar->name, value);
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
	}
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
