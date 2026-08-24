/*
 * snd_sdl.c - SDL audio driver for Hexen II: Hammer of Thyrion (uHexen2)
 * based on implementations found in the quakeforge and ioquake3 projects.
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2005-2012 O.Sezer <sezero@users.sourceforge.net>
 * Copyright (C) 2010-2014 QuakeSpasm developers
 * Copyright (C) 2026 Mrokkk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <SDL2/SDL.h>

#include "quakedef.h"

static int	sdl_initialized;
static int	buffersize;

static void UpdateBuffer(void *userdata, Uint8 *stream, int len)
{
	int	pos, tobufend;
	int	len1, len2;

	Q_UNUSED(userdata);

	if (Q_UNLIKELY(!shm))
	{	// shouldn't happen, but just in case
		memset(stream, 0, len);
		return;
	}

	pos = (shm->samplepos * (shm->samplebits / 8));
	if (pos >= buffersize)
	{
		shm->samplepos = pos = 0;
	}

	tobufend = buffersize - pos;  // bytes to buffer's end
	len1 = len;
	len2 = 0;

	if (len1 > tobufend)
	{
		len1 = tobufend;
		len2 = len - len1;
	}

	memcpy(stream, shm->buffer + pos, len1);

	if (len2 <= 0)
	{
		shm->samplepos += (len1 / (shm->samplebits / 8));
	}
	else
	{	// wraparound?
		memcpy(stream + len1, shm->buffer, len2);
		shm->samplepos = (len2 / (shm->samplebits / 8));
	}

	if (shm->samplepos >= buffersize)
	{
		shm->samplepos = 0;
	}
}

qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec	desired;
	int				tmp, val;
	const char		*driver, *device;

	if (Q_UNLIKELY(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0))
	{
		Sys_Printf_f("Couldn't init SDL audio: %s\n", SDL_GetError());
		return false;
	}

	sdl_initialized++;

	/* Set up the desired format */
	desired.freq = 11025;
	desired.format = (loadas8bit.value) ? AUDIO_U8 : AUDIO_S16SYS;
	desired.channels = 2;
	if (desired.freq <= 11025)
		desired.samples = 256;
	else if (desired.freq <= 22050)
		desired.samples = 512;
	else if (desired.freq <= 44100)
		desired.samples = 1024;
	else if (desired.freq <= 56000)
		desired.samples = 2048; /* for 48 kHz */
	else
		desired.samples = 4096; /* for 96 kHz */
	desired.callback = UpdateBuffer;
	desired.userdata = NULL;

	// Open the audio device
	if (SDL_OpenAudio(&desired, NULL) == -1)
	{
		Sys_Printf_f("Couldn't open SDL audio: %s\n", SDL_GetError());
		SNDDMA_Shutdown();
		return false;
	}

	sdl_initialized++;

	shm = &sn;
	memset((void *)shm, 0, sizeof(dma_t));

	/* Fill the audio DMA information block */
	/* Since we passed NULL as the 'obtained' spec to SDL_OpenAudio(),
	 * SDL will convert to hardware format for us if needed, hence we
	 * directly use the desired values here. */
	shm->samplebits = (desired.format & 0xFF); /* first byte of format is bits */
	shm->speed = desired.freq;
	shm->channels = desired.channels;
	tmp = (desired.samples * desired.channels) * 10;
	if (tmp & (tmp - 1))
	{	// make it a power of two
		val = 1;
		while (val < tmp)
			val <<= 1;

		tmp = val;
	}
	shm->samples = tmp;
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	buffersize = shm->samples * (shm->samplebits / 8);

	driver = SDL_GetCurrentAudioDriver();
	device = SDL_GetAudioDeviceName(0, SDL_FALSE);

	Sys_Printf_f("Audio spec   : %d Hz, %d samples, %d channels\n", desired.freq, desired.samples, desired.channels);
	Sys_Printf_f("Audio buffer : %d bytes\n", buffersize);
	Sys_Printf_f("Audio driver : %s\n", driver);
	Sys_Printf_f("Audio device : %s\n", device);

	shm->buffer = (unsigned char *)calloc(1, buffersize);

	if (!shm->buffer)
	{
		Sys_Printf_f("Failed allocating memory for SDL audio\n");
		SNDDMA_Shutdown();
		return false;
	}

	SDL_PauseAudio(0);

	return true;
}

int SNDDMA_GetDMAPos(void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown(void)
{
	if (sdl_initialized > 1) SDL_CloseAudio();
	if (sdl_initialized > 0) SDL_QuitSubSystem(SDL_INIT_AUDIO);
	if (shm)
	{
		if (shm->buffer)
		{
			free(shm->buffer);
		}
		shm->buffer = NULL;
		shm = NULL;
	}
}

void SNDDMA_Submit(void)
{
	SDL_UnlockAudioDevice(1);
}

void SNDDMA_LockBuffer(void)
{
	SDL_UnlockAudioDevice(0);
}

void SNDDMA_BlockSound(void)
{
	SDL_UnlockAudioDevice(0);
}

void SNDDMA_UnblockSound(void)
{
	SDL_UnlockAudioDevice(1);
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
