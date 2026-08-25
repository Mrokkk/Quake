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
// input.c -- common input handling

#include "quakedef.h"

struct
{
	int key;
	int down;
} keyq[64];

#define KEYQ_SIZE	Q_ARRLEN(keyq)
#define KEYQ_MASK	(KEYQ_SIZE - 1)

static int	keyq_head	= 0;
static int	keyq_tail	= 0;

qboolean		mouse_avail;
static int		mouse_oldbuttonstate;
static int		mouse_buttonstate;
static float	mouse_x, mouse_y;
static float	old_mouse_x, old_mouse_y;

void IN_Init(void)
{
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

	for (i = 0; i < 3; i++)
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
	if (!mouse_avail || cl.paused || key_dest != key_game)
		return;

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
		cl.viewangles[PITCH] = Clampf(-70, 80, cl.viewangles[PITCH] + m_pitch.value * mouse_y);
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

void IN_AddKey (int key, qboolean down)
{
	keyq[keyq_head].key		= key;
	keyq[keyq_head].down	= down;
	keyq_head				= (keyq_head + 1) & KEYQ_MASK;
}

void IN_AddMouseMove (float x, float y)
{
	mouse_x = x;
	mouse_y = y;
}

void IN_AddMouseButton (int button, qboolean down)
{
	if (down)
	{
		mouse_buttonstate |= 1 << button;
	}
	else
	{
		mouse_buttonstate &= ~(1 << button);
	}
}

void IN_SendKeyEvents(void)
{
	int tail;
	IN_ReadEvents();
	while (keyq_head != keyq_tail)
	{
		tail = keyq_tail;
		keyq_tail = (keyq_tail + 1) & KEYQ_MASK;
		Key_Event(keyq[tail].key, keyq[tail].down);
	}
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
