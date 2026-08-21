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
// draw.h -- these are the only functions outside the refresh allowed
// to touch the vid buffer

#ifndef __DRAW_H__
#define __DRAW_H__

Q_BEGIN_DECLS

extern	qpic_t		*draw_disc;	// also used on sbar

#define FONT_WIDTH			8
#define FONT_HEIGHT			8

typedef enum
{
	_CENTER,
	_LEFT,
	_RIGHT,
	_TOP = _LEFT,
	_BOTTOM = _RIGHT,
} _align_t;

typedef struct
{
	_align_t v;
} align_t;

#define CENTER	(align_t){_CENTER}
#define LEFT	(align_t){_LEFT}
#define RIGHT	(align_t){_RIGHT}
#define TOP		(align_t){_TOP}
#define BOTTOM	(align_t){_BOTTOM}

void Draw_Init (void);

// These function use coordinates of virtual 320x200 screen
// They scale according to scr_scaling and apply scr_xoff/scr_yoff offset
// if needed (depending on xa/ya)
void Draw_Character_Align (int x, int y, align_t xa, align_t ya, byte num);
void Draw_Character_Center (int x, int y, byte num);
void Draw_Pic_Align (int x, int y, align_t xa, align_t ya, const qpic_t *pic);
void Draw_Pic_Center (int x, int y, const qpic_t *pic);
void Draw_TransPic_Align (int x, int y, align_t xa, align_t ya, const qpic_t *pic);
void Draw_TransPic_Center (int x, int y, const qpic_t *pic);
void Draw_TransPicTranslate_Align (int x, int y, align_t xa, align_t ya, const qpic_t *pic, const byte *translation);
void Draw_TileClear_Align (int x, int y, int w, int h, align_t xa, align_t ya);
void Draw_Fill_Align (int x, int y, int w, int h, align_t xa, align_t ya, int c);
void Draw_String_Align (int x, int y, align_t xa, align_t ya, const char *str);

// This function draws using real screen coordinates
void Draw_TileClear_Absolute (int x, int y, int w, int h);

void Draw_ConsoleBackground (int lines);
void Draw_BeginDisc (void);
void Draw_EndDisc (void);
void Draw_FadeScreen (void);
qpic_t *Draw_PicFromWad (const char *name);
qpic_t *Draw_CachePic (const char *path);
void Draw_Crosshair(float x, float y);

Q_END_DECLS

#endif // __DRAW_H__

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
