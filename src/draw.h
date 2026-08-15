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

#define CHAR_WIDTH	8
#define CHAR_HEIGHT	8

typedef enum
{
	CENTER,
	LEFT,
	RIGHT,
	TOP = LEFT,
	BOTTOM = RIGHT,
} align_t;

void Draw_Init (void);

// These function use virtual 320x200 screen as a reference
// They scale according to scr_scaling and apply scr_xoff/scr_yoff offset
// if needed
void Draw_Character_Align (int x, int y, align_t xa, align_t ya, int num);
void Draw_Character_Center (int x, int y, int num);
void Draw_Pic_Align (int x, int y, align_t xa, align_t ya, qpic_t *pic);
void Draw_Pic_Center (int x, int y, qpic_t *pic);
void Draw_TransPic_Align (int x, int y, align_t xa, align_t ya, qpic_t *pic);
void Draw_TransPic_Center (int x, int y, qpic_t *pic);
void Draw_TransPicTranslate_Align (int x, int y, align_t xa, align_t ya, qpic_t *pic, byte *translation);
void Draw_TileClear_Align (int x, int y, int w, int h, align_t xa, align_t ya);
void Draw_Fill_Align (int x, int y, int w, int h, int c, align_t xa, align_t ya);
void Draw_String_Align (int x, int y, align_t xa, align_t ya, char *str);

// This function draw to real screen without scaling or offset
void Draw_TileClear_Absolute (int x, int y, int w, int h);

void Draw_ConsoleBackground (int lines);
void Draw_BeginDisc (void);
void Draw_EndDisc (void);
void Draw_FadeScreen (void);
qpic_t *Draw_PicFromWad (char *name);
qpic_t *Draw_CachePic (char *path);
void Draw_Crosshair(float x, float y);

Q_END_DECLS

#endif // __DRAW_H__

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
