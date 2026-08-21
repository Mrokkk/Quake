/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.h"

typedef struct {
	vrect_t	rect;
	int		width;
	int		height;
	byte	*ptexbytes;
	int		rowbytes;
} rectdesc_t;

static rectdesc_t	r_rectdesc;

byte	*draw_chars;	// 8*8 graphic characters
qpic_t	*draw_disc;
qpic_t	*draw_backtile;

//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char			name[MAX_QPATH];
	cache_user_t	cache;
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

qpic_t	*Draw_PicFromWad (const char *name)
{
	return W_GetLumpName (name);
}

/*
================
Draw_CachePic
================
*/
qpic_t *Draw_CachePic (const char *path)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		if (!strcmp (path, pic->name))
			break;

	if (i == menu_numcachepics)
	{
		if (Q_UNLIKELY(menu_numcachepics == MAX_CACHED_PICS))
			Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
		menu_numcachepics++;
		if (Q_UNLIKELY(Q_strlcpy (pic->name, path, sizeof(pic->name)) >= sizeof(pic->name)))
			Sys_Error ("pic name truncated: %s\n", path);
	}

	dat = Cache_Check (&pic->cache);

	if (dat)
		return dat;

	// load the pic from disk
	COM_LoadCacheFile (path, &pic->cache);

	dat = (qpic_t *)pic->cache.data;
	if (!dat)
	{
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	}

	SwapPic (dat);

	return dat;
}

static void Draw_CharToConback (int num, byte *dest)
{
	int		row, col;
	byte	*source;
	int		drawline;
	int		x;

	row = num >> 4;
	col = num & 15;
	source = draw_chars + (row << 10) + (col << 3);

	drawline = FONT_HEIGHT;

	while (drawline--)
	{
		for (x = 0; x < FONT_WIDTH; x++)
		{
			if (source[x])
			{
				dest[x] = 0x60 + source[x];
			}
		}
		source += 128;
		dest += SCREEN_WIDTH;
	}
}

/*
===============
Draw_Init
===============
*/
void Draw_Init (void)
{
	size_t	x;
	byte	*dest;
	qpic_t	*conback;
	char	ver[128];

	draw_chars = W_GetLumpName ("conchars");
	draw_disc = W_GetLumpName ("disc");
	draw_backtile = W_GetLumpName ("backtile");

	r_rectdesc.width = draw_backtile->width;
	r_rectdesc.height = draw_backtile->height;
	r_rectdesc.ptexbytes = draw_backtile->data;
	r_rectdesc.rowbytes = draw_backtile->width;

	conback = Draw_CachePic ("gfx/conback.lmp");

	// hack the version number directly into the pic
	sprintf (ver, "Quake %4.2f", (float)VERSION);
	dest = conback->data + SCREEN_WIDTH * 186 + SCREEN_WIDTH - 11 - FONT_WIDTH * strlen(ver);

	for (x = 0; x < strlen(ver); x++)
	{
		Draw_CharToConback (ver[x], dest + x * FONT_WIDTH);
	}
}

static inline int GetAlignedX(int x, align_t xa)
{
	switch (xa.v)
	{
		case _CENTER:	return x * scr_scaling + scr_xoff;
		case _LEFT:		return x * scr_scaling;
		case _RIGHT:	return x * scr_scaling + vid.width;
		default:		return x;
	}
}

static inline int GetAlignedY(int y, align_t ya)
{
	switch (ya.v)
	{
		case _CENTER:	return y * scr_scaling + scr_yoff;
		case _TOP:		return y * scr_scaling;
		case _BOTTOM:	return y * scr_scaling + vid.height;
		default:		return y;
	}
}

#define FONT_ATLAS_COLUMNS	16
#define FONT_ATLAS_ROWS		16
#define FONT_ATLAS_RESX		(FONT_ATLAS_COLUMNS * FONT_WIDTH)
#define FONT_ATLAS_RESY		(FONT_ATLAS_ROWS * FONT_HEIGHT)
#define FONT_ATLAS_PITCH	(FONT_ATLAS_RESX * FONT_HEIGHT)

#define DRAW_CHAR(scaling)							\
	while (source_drawline--)						\
	{												\
		for (i = 0; i < scaling; ++i)				\
		{											\
			for (k = 0; k < FONT_WIDTH; ++k)		\
			{										\
				if (!(pixel = source[k]))			\
				{									\
					continue;						\
				}									\
				for (j = 0; j < scaling; ++j)		\
				{									\
					dest[k * scaling + j] = pixel;	\
				}									\
			}										\
			dest += vid.conrowbytes;				\
		}											\
		source += FONT_ATLAS_RESX;					\
	}

#define DRAW_LOOP(macro) \
	switch (scr_scaling)				\
	{									\
		case 7:		macro(7); break;	\
		case 6:		macro(6); break;	\
		case 5:		macro(5); break;	\
		case 4:		macro(4); break;	\
		case 3:		macro(3); break;	\
		case 2:		macro(3); break;	\
		case 1:		macro(3); break;	\
		default:	macro(scr_scaling);	\
	}

/*
================
Draw_Character_Impl

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
static void Draw_Character_Impl (int x, int y, byte num)
{
	const byte	*source;
	byte		*dest, pixel;
	int			source_drawline;
	int			row, col, i, j, k, scaled_height;

	scaled_height = FONT_HEIGHT * scr_scaling;

	if (Q_UNLIKELY(y <= -scaled_height || y >= (int)vid.height - scaled_height))
		return;			// totally off screen

	row = num / FONT_ATLAS_ROWS;
	col = num % FONT_ATLAS_ROWS;
	source = draw_chars + (row * FONT_ATLAS_PITCH) + col * FONT_WIDTH;

	if (y < 0)
	{	// clipped
		source_drawline = FONT_HEIGHT + y;
		source -= FONT_ATLAS_RESX * y / scr_scaling;
		y = 0;
	}
	else
	{
		source_drawline = FONT_HEIGHT;
	}

	dest = vid.conbuffer + y * vid.conrowbytes + x;

	DRAW_LOOP(DRAW_CHAR);
}

/*
================
Draw_Character_Align
================
*/
void Draw_Character_Align (int x, int y, align_t xa, align_t ya, byte num)
{
	Draw_Character_Impl(GetAlignedX(x, xa), GetAlignedY(y, ya), num);
}

/*
================
Draw_Character_Center
================
*/
void Draw_Character_Center (int x, int y, byte num)
{
	Draw_Character_Impl(GetAlignedX(x, CENTER), GetAlignedY(y, CENTER), num);
}

/*
================
Draw_String_Align
================
*/
void Draw_String_Align (int x, int y, align_t xa, align_t ya, const char *str)
{
	x = GetAlignedX(x, xa);
	y = GetAlignedY(y, ya);
	while (*str)
	{
		Draw_Character_Impl (x, y, *str);
		str++;
		x += FONT_WIDTH * scr_scaling;
	}
}

typedef struct
{
	short x, y;
	short w, h;
	short xoff, yoff;
} rect_t;

static inline rect_t CalculateRect(int x, int y, int w, int h)
{
	rect_t	r;
	int		xoff = 0, yoff = 0, diff;

	if (Q_UNLIKELY(x < 0))
	{
		xoff = -x;
		w -= xoff;
		x = 0;
	}
	else if (Q_UNLIKELY((diff = x + w - (int)vid.width) > 0))
	{
		w -= diff;
	}

	if (Q_UNLIKELY(y < 0))
	{
		yoff = -y;
		h -= yoff;
		y = 0;
	}
	else if (Q_UNLIKELY((diff = y + h - (int)vid.height) > 0))
	{
		h -= diff;
	}

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	r.xoff = xoff;
	r.yoff = yoff;

	return r;
}

#define DRAW_PIC_IMPL(scaling, get_pixel)			\
	while (r.h--)									\
	{												\
		for (j = 0; j < scaling; ++j)				\
		{											\
			for (i = 0; i < r.w; ++i)				\
			{										\
				get_pixel;							\
				for (k = 0; k < scaling; ++k)		\
				{									\
					dest[i * scaling + k] = pixel;	\
				}									\
			}										\
			dest += vid.rowbytes;					\
		}											\
		source += pic_w;							\
	}

/*
================
Draw_Pic_Impl
================
*/
static void Draw_Pic_Impl(int x, int y, const qpic_t *pic)
{
	const byte	*source;
	byte		*dest, pixel;
	int			pic_w, i, j, k;
	int			scaled_w, scaled_h;
	rect_t		r;

	pic_w = pic->width;

	scaled_w = pic_w * scr_scaling;
	scaled_h = pic->height * scr_scaling;

	r = CalculateRect(x, y, scaled_w, scaled_h);

	if (Q_UNLIKELY(r.w < 0 || r.h < 0))
	{
		return;
	}

	r.w /= scr_scaling;
	r.h /= scr_scaling;

	source = pic->data + r.yoff / scr_scaling * pic_w + r.xoff / scr_scaling;
	dest = vid.buffer + r.y * vid.rowbytes + r.x;

#define DRAW_PIC(scaling) \
	DRAW_PIC_IMPL(scaling, ({ pixel = source[i]; }))

	DRAW_LOOP(DRAW_PIC);
}

/*
=============
Draw_TransPic_Impl
=============
*/
static void Draw_TransPic_Impl (int x, int y, const qpic_t *pic)
{
	const byte	*source;
	byte		*dest, pixel;
	int			pic_w, i, j, k;
	int			scaled_w, scaled_h;
	rect_t		r;

	pic_w = pic->width;

	scaled_w = pic_w * scr_scaling;
	scaled_h = pic->height * scr_scaling;

	r = CalculateRect(x, y, scaled_w, scaled_h);

	if (Q_UNLIKELY(r.w < 0 || r.h < 0))
	{
		return;
	}

	r.w /= scr_scaling;
	r.h /= scr_scaling;

	source = pic->data + r.xoff / scr_scaling + r.yoff / scr_scaling * pic_w;
	dest = vid.buffer + r.y * vid.rowbytes + r.x;

#define DRAW_TRANS_PIC(scaling)	\
	DRAW_PIC_IMPL(scaling, ({ if ((pixel = source[i]) == TRANSPARENT_COLOR) continue; }))

	DRAW_LOOP(DRAW_TRANS_PIC);
}

/*
=============
Draw_Pic_Align
=============
*/
void Draw_Pic_Align (int x, int y, align_t xa, align_t ya, const qpic_t *pic)
{
	Draw_Pic_Impl(GetAlignedX(x, xa), GetAlignedY(y, ya), pic);
}

/*
=============
Draw_Pic_Center
=============
*/
void Draw_Pic_Center (int x, int y, const qpic_t *pic)
{
	Draw_Pic_Impl(GetAlignedX(x, CENTER), GetAlignedY(y, CENTER), pic);
}

/*
=============
Draw_TransPic_Align
=============
*/
void Draw_TransPic_Align (int x, int y, align_t xa, align_t ya, const qpic_t *pic)
{
	Draw_TransPic_Impl(GetAlignedX(x, xa), GetAlignedY(y, ya), pic);
}

/*
=============
Draw_TransPic_Center
=============
*/
void Draw_TransPic_Center (int x, int y, const qpic_t *pic)
{
	Draw_TransPic_Impl(GetAlignedX(x, CENTER), GetAlignedY(y, CENTER), pic);
}

/*
=============
Draw_TransPicTranslate_Impl
=============
*/
static void Draw_TransPicTranslate_Impl (int x, int y, const qpic_t *pic, const byte *translation)
{
	const byte	*source;
	byte		*dest, tbyte, pixel;
	int			pic_w, i, j, k;
	int			scaled_w, scaled_h;
	rect_t		r;

	pic_w = pic->width;

	scaled_w = pic_w * scr_scaling;
	scaled_h = pic->height * scr_scaling;

	r = CalculateRect(x, y, scaled_w, scaled_h);

	if (Q_UNLIKELY(r.w < 0 || r.h < 0))
	{
		return;
	}

	r.w /= scr_scaling;
	r.h /= scr_scaling;

	source = pic->data + r.xoff / scr_scaling + r.yoff / scr_scaling * pic_w;
	dest = vid.buffer + r.y * vid.rowbytes + r.x;

#define DRAW_TRANS_PIC_TRANSLATE(scaling) \
	DRAW_PIC_IMPL(scaling, ({ if ((tbyte = source[i]) == TRANSPARENT_COLOR) continue; pixel = translation[tbyte]; }))

	DRAW_LOOP(DRAW_TRANS_PIC_TRANSLATE);
}

/*
=============
Draw_TransPicTranslate_Align
=============
*/
void Draw_TransPicTranslate_Align (int x, int y, align_t xa, align_t ya, const qpic_t *pic, const byte *translation)
{
	Draw_TransPicTranslate_Impl(GetAlignedX(x, xa), GetAlignedY(y, ya), pic, translation);
}

/*
================
Draw_ConsoleBackground
================
*/
void Draw_ConsoleBackground (int lines)
{
	int				x, y, v;
	const byte		*src;
	byte			*dest;
	int				f, fstep;
	const qpic_t	*conback;

	conback = Draw_CachePic ("gfx/conback.lmp");

	// draw the pic
	dest = vid.conbuffer;

	if (vid.conwidth == SCREEN_WIDTH)
	{
		for (y = 0; y < lines; y++, dest += vid.conrowbytes)
		{
			v = (vid.conheight - lines + y) * SCREEN_HEIGHT / vid.conheight;
			src = conback->data + v * SCREEN_WIDTH;
			memcpy (dest, src, vid.conwidth);
		}
	}
	else
	{
		for (y = 0; y < lines; y++, dest += vid.conrowbytes)
		{
			v = (vid.conheight - lines + y) * SCREEN_HEIGHT / vid.conheight;
			src = conback->data + v * SCREEN_WIDTH;
			f = 0;
			fstep = SCREEN_WIDTH * (1 << 16) / vid.conwidth;
			for (x = 0; x < (int)vid.conwidth; x += 4)
			{
				dest[x + 0] = src[f >> 16];
				f += fstep;
				dest[x + 1] = src[f >> 16];
				f += fstep;
				dest[x + 2] = src[f >> 16];
				f += fstep;
				dest[x + 3] = src[f >> 16];
				f += fstep;
			}
		}
	}
}

/*
==============
R_DrawRect8
==============
*/
static void R_DrawRect8 (vrect_t *prect, int rowbytes, byte *psrc, int transparent)
{
	byte	t;
	int		i, j, srcdelta, destdelta;
	byte	*pdest;

	pdest = vid.buffer + (prect->y * vid.rowbytes) + prect->x;

	srcdelta = rowbytes - prect->width;
	destdelta = vid.rowbytes - prect->width;

	if (transparent)
	{
		for (i=0 ; i<prect->height ; i++)
		{
			for (j=0 ; j<prect->width ; j++)
			{
				t = *psrc;
				if (t != TRANSPARENT_COLOR)
				{
					*pdest = t;
				}

				psrc++;
				pdest++;
			}

			psrc += srcdelta;
			pdest += destdelta;
		}
	}
	else
	{
		for (i=0 ; i<prect->height ; i++)
		{
			memcpy (pdest, psrc, prect->width);
			psrc += rowbytes;
			pdest += vid.rowbytes;
		}
	}
}

/*
=============
Draw_TileClear_Absolute

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear_Absolute (int x, int y, int w, int h)
{
	int				width, height, tileoffsetx, tileoffsety;
	byte			*psrc;
	vrect_t			vr;

	r_rectdesc.rect.x = x;
	r_rectdesc.rect.y = y;
	r_rectdesc.rect.width = w;
	r_rectdesc.rect.height = h;

	vr.y = r_rectdesc.rect.y;
	height = r_rectdesc.rect.height;

	tileoffsety = vr.y % r_rectdesc.height;

	while (height > 0)
	{
		vr.x = r_rectdesc.rect.x;
		width = r_rectdesc.rect.width;

		if (tileoffsety != 0)
			vr.height = r_rectdesc.height - tileoffsety;
		else
			vr.height = r_rectdesc.height;

		if (vr.height > height)
			vr.height = height;

		tileoffsetx = vr.x % r_rectdesc.width;

		while (width > 0)
		{
			if (tileoffsetx != 0)
				vr.width = r_rectdesc.width - tileoffsetx;
			else
				vr.width = r_rectdesc.width;

			if (vr.width > width)
				vr.width = width;

			psrc = r_rectdesc.ptexbytes +
					(tileoffsety * r_rectdesc.rowbytes) + tileoffsetx;

			R_DrawRect8 (&vr, r_rectdesc.rowbytes, psrc, 0);

			vr.x += vr.width;
			width -= vr.width;
			tileoffsetx = 0;	// only the left tile can be left-clipped
		}

		vr.y += vr.height;
		height -= vr.height;
		tileoffsety = 0;		// only the top tile can be top-clipped
	}
}

/*
=============
Draw_TileClear_Align

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear_Align (int x, int y, int w, int h, align_t xa, align_t ya)
{
	Draw_TileClear_Absolute(GetAlignedX(x, xa), GetAlignedY(y, ya), w * scr_scaling, h * scr_scaling);
}

static void Draw_Fill_Impl (int x, int y, int w, int h, int c)
{
	byte	*dest;
	int		u, v;

	w = Q_MIN(w, (int)vid.width - x);
	h = Q_MIN(h, (int)vid.height - y);

	dest = vid.buffer + y * vid.rowbytes + x;
	for (v = 0; v < h; v++, dest += vid.rowbytes)
		for (u = 0; u < w; u++)
			dest[u] = c;
}

/*
=============
Draw_Fill_Align

Fills a box of pixels with a single color
=============
*/
void Draw_Fill_Align (int x, int y, int w, int h, align_t xa, align_t ya, int c)
{
	Draw_Fill_Impl(GetAlignedX(x, xa), GetAlignedY(y, ya), w * scr_scaling, h * scr_scaling, c);
}

//=============================================================================

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen (void)
{
	size_t	x,y;
	byte	*pbuf;

	VID_UnlockBuffer ();
	S_ExtraUpdate ();
	VID_LockBuffer ();

	for (y = 0; y < vid.height; y++)
	{
		size_t	t;

		pbuf = (byte *)(vid.buffer + vid.rowbytes * y);
		t = (y & 1) << 1;

		for (x = 0; x < vid.width; x++)
		{
			if ((x & 3) != t)
			{
				pbuf[x] = 0;
			}
		}
	}

	VID_UnlockBuffer ();
	S_ExtraUpdate ();
	VID_LockBuffer ();
}

//=============================================================================

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc (void)
{
	if (draw_disc)
	{
		D_BeginDirectRect (vid.width - 24, 0, draw_disc->data, 24, 24);
	}
}


/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc (void)
{
	D_EndDirectRect (vid.width - 24, 0, 24, 24);
}

/*
================
Draw_Crosshair
================
*/
void Draw_Crosshair(float x, float y)
{
	// TODO: add custom crosshair
	Draw_Character_Impl (
		scr_vrect.x + scr_vrect.width / 2 + (int)x - (FONT_WIDTH / 2) * scr_scaling,
		scr_vrect.y + scr_vrect.height / 2 + (int)y - (FONT_HEIGHT / 2) * scr_scaling,
		'+');
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
