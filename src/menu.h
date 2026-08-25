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

#ifndef __MENU_H__
#define __MENU_H__

Q_BEGIN_DECLS

//
// the net drivers should just set the apropriate bits in m_activenet,
// instead of having the menu code look through their internal tables
//
#define	MNET_IPX		1
#define	MNET_TCP		2

extern	int	m_activenet;

typedef enum m_state_e
{
	m_none,
	m_main,
	m_singleplayer,
	m_load,
	m_save,
	m_multiplayer,
	m_setup,
	m_net,
	m_options,
	m_video,
	m_keys,
	m_help,
	m_quit,
	m_serialconfig,
	m_modemconfig,
	m_lanconfig,
	m_gameoptions,
	m_search,
	m_slist
} m_state_t;

extern m_state_t m_state;
extern m_state_t m_return_state;
extern qboolean m_return_onerror;
extern char m_return_reason[32];

enum
{
	option_onoff,
	option_slider,
	option_button,
	option_values,
};

enum
{
	action_none,
	action_callback,
	action_cmd,
};

enum
{
	slider_dynamic_range	= (1 << 0),
	slider_inverted			= (1 << 1),
};

typedef struct
{
	cvar_t *cvar;
} onoff_t;

typedef struct
{
	cvar_t			*cvar;
	const char		*fmt;
	int				flags;
	union
	{
		struct
		{
			float	min, max, step;
		};
		void		(*read)(float *min, float *max, float *step);
	};
} slider_t;

typedef struct
{
	int				action;
	union
	{
		void		(*callback)(void);
		const char	*cmd;
	};
} button_t;

typedef struct
{
	const char	*name;
	float		value;
} m_value_t;

typedef struct
{
	cvar_t		*cvar;
	unsigned	count;
	m_value_t	*data;
} values_t;

typedef struct option_s
{
	int				type;
	unsigned		namelen;
	const char		*name;
	union
	{
		onoff_t		onoff;
		slider_t	slider;
		button_t	button;
		values_t	values;
	};
} option_t;

typedef struct menu_s
{
	int				cursor;
	unsigned		options_count;
	const char		*title;
	option_t		*options;
	void			(*enter)(void);
	struct menu_s	*parent;
} menu_t;

//
// menus
//
void M_Init (void);
void M_Keydown (int key);
void M_Draw (void);
void M_ToggleMenu_f (void);
void M_RegisterVideoMenu (menu_t *m);

void M_Menu_Main_f (void);
void M_Menu_Options_f (void);
void M_Menu_Quit_f (void);

//
// drawing
//
void M_DrawCharacter (int cx, int cy, int num);
void M_Print (int cx, int cy, const char *str);
void M_PrintWhite (int cx, int cy, const char *str);
void M_DrawTransPic (int x, int y, qpic_t *pic);
void M_DrawPic (int x, int y, qpic_t *pic);
void M_DrawSlider (int x, int y, float range, float value, const char *fmt);
void M_DrawCheckbox (int x, int y, int on);
void M_DrawMenuCursor (int x, int y);
void M_DrawTextCursor (int x, int y);
void M_DrawPlaque (void);
void M_DrawMenuHeader (const char *path);

Q_END_DECLS

#endif // __MENU_H__

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
