/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 Storlek <storlek@rigelseven.com>
 * copyright (c) 2005-2008 Mrs. Brisby <mrs.brisby@nimh.org>
 * copyright (c) 2009 Storlek & Mrs. Brisby
 * copyright (c) 2010-2012 Storlek
 * URL: http://schismtracker.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "headers.h"
#include "mem.h"
#include "util.h"
#include "backend/audio.h"
#include "loadso.h"

#include "init.h"

struct schism_audio_device {
	void (*callback)(uint8_t *stream, int len);
};

static int (SDLCALL *sdl12_InitSubSystem)(uint32_t flags);
static void (SDLCALL *sdl12_QuitSubSystem)(uint32_t flags);

static int (SDLCALL *sdl12_OpenAudio)(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
static void (SDLCALL *sdl12_CloseAudio)(void);
static void (SDLCALL *sdl12_LockAudio)(void);
static void (SDLCALL *sdl12_UnlockAudio)(void);
static void (SDLCALL *sdl12_PauseAudio)(int);

static char *(SDLCALL *sdl12_AudioDriverName)(char *buf, int maxlen);

/* ---------------------------------------------------------- */
/* drivers */

static int sdl12_audio_init_driver(const char *name)
{
	/* sigh... */
	static char var[512];
	char buf[256];
	int x;

	snprintf(var, sizeof(var), "SDL_AUDIODRIVER=%s", name);

	sdl12_putenv(var);

	x = sdl12_InitSubSystem(SDL_INIT_AUDIO);

	if (x >= 0) {
		/* verify that our driver is the one we want */
		sdl12_AudioDriverName(buf, sizeof(buf));

		if (!strcmp(buf, name))
			return 0;

		/* the driver we requested could not be init */
		sdl12_QuitSubSystem(SDL_INIT_AUDIO);
	}
	return -1;
}

static void sdl12_audio_quit_driver(void)
{
	sdl12_QuitSubSystem(SDL_INIT_AUDIO);
}

/* ---------------------------------------------------------- */

/* This method will silently fail for drivers that don't work on startup,
 * but this can be remedied by simply restarting Schism. */

static struct sdl12_audio_driver_info {
	const char *driver;
	int exists;
} drivers[] = {
	/* SDL 1.2 doesn't provide an API for this, so we have to
	 * build this when initializing the audio subsystem.
	 * These in the same order as the bootstrap array in
	 * src/audio/SDL_audio.c to hopefully mimic SDL's original
	 * behavior. */
	{"pipewire", 0},  // Pipewire (sdl2-compat)
	{"pulseaudio", 0},// PulseAudio (sdl2-compat)
	{"pulse", 0},     // PulseAudio
	{"alsa", 0},      // ALSA
	{"jack", 0},      // JACK (sdl2-compat)
	{"sndio", 0},     // OpenBSD sndio
	{"netbsd", 0},    // NetBSD audio
	{"openbsd", 0},   // OpenBSD audio (outdated)
	{"dsp", 0},       // /dev/dsp (OSS)
	{"dma", 0},       // /dev/dsp DMA audio (OSS)
	{"qsa-nto", 0},   // QNX NTO audio
	{"audio", 0},     // Sun Microsystems audio
	{"AL", 0},        // IRIX DMedia audio
	{"arts", 0},      // artsc audio (?)
	{"esd", 0},       // Enlightened Sound Daemon
	{"nas", 0},       // Network Audio System
	{"wasapi", 0},    // Win32 WASAPI (sdl2-compat)
	{"dsound", 0},    // Win32 DirectSound
#ifdef SCHISM_WIN32
	{"waveout", 0},   // Win32 WaveOut
#endif
	{"paud", 0},      // AIX Paudio
	{"baudio", 0},    // BeOS
	{"coreaudio", 0}, // Mac OS X CoreAudio
	{"sndmgr", 0},    // Mac OS SoundManager 3.0
	{"AHI", 0},       // AmigaOS
	{"dcaudio", 0},   // Dreamcast AICA audio
	{"nds", 0},       // Nintendo DS Audio
#ifndef SCHISM_WIN32
	{"waveout", 0},   // Tru64 MME WaveOut
#endif
	{"dart", 0},      // OS/2 Direct Audio RouTines 
	{"epoc", 0},      // EPOC streaming audio
	{"ums", 0},       // AIX UMS audio

	// These two are pretty much guaranteed to exist
	{"disk", 1},
	{"dummy", 1},
};

static int sdl12_audio_driver_info_init(void)
{
	int atleast_one_loaded = 0;

	for (size_t i = 0; i < ARRAY_SIZE(drivers); i++) {
		/* don't check twice for a driver */
		if (drivers[i].exists) {
			atleast_one_loaded = 1;
			continue;
		}

		if (!sdl12_audio_init_driver(drivers[i].driver)) {
			atleast_one_loaded = drivers[i].exists = 1;
			sdl12_audio_quit_driver();
		}
	}

	return atleast_one_loaded;
}

static void sdl12_audio_driver_info_quit(void)
{
	// reset
	for (size_t i = 0; i < ARRAY_SIZE(drivers); i++)
		drivers[i].exists = 0;
}

static int sdl12_audio_driver_count(void)
{
	int c = 0;
	for (size_t i = 0; i < ARRAY_SIZE(drivers); i++)
		if (drivers[i].exists)
			c++;
	return c;
}

static const char *sdl12_audio_driver_name(int x)
{
	size_t i = 0;
	for (int c = 0; i < ARRAY_SIZE(drivers); i++) {
		if (!drivers[i].exists)
			continue;

		if (c == x)
			break;

		c++;
	}
	return (i < ARRAY_SIZE(drivers)) ? drivers[i].driver : NULL;
}

/* --------------------------------------------------------------- */

/* SDL 1.2 doesn't have a concept of audio devices */
static uint32_t sdl12_audio_device_count(SCHISM_UNUSED uint32_t flags)
{
	return 0;
}

static const char *sdl12_audio_device_name(SCHISM_UNUSED uint32_t i)
{
	return NULL;
}

/* -------------------------------------------------------- */

static void SDLCALL sdl12_dummy_callback(void *userdata, uint8_t *stream, int len)
{
	// call our own callback
	schism_audio_device_t *dev = userdata;

	dev->callback(stream, len);
}

static int sdl12_audio_opened = 0;

static schism_audio_device_t *sdl12_audio_open_device(SCHISM_UNUSED uint32_t id, const schism_audio_spec_t *desired, schism_audio_spec_t *obtained)
{
	/* SDL 1.2 only supports opening one device at a time! */
	if (sdl12_audio_opened)
		return NULL;

	schism_audio_device_t *dev = mem_calloc(1, sizeof(*dev));
	dev->callback = desired->callback;
	//dev->userdata = desired->userdata;

	SDL_AudioSpec sdl_desired = {
		.freq = desired->freq,
		// SDL 1.2 has no support for 32-bit audio at all
		.format = (desired->bits == 8) ? (AUDIO_U8) : (AUDIO_S16SYS),
		.channels = desired->channels,
		.samples = desired->samples,
		.callback = sdl12_dummy_callback,
		.userdata = dev,
	};
	SDL_AudioSpec sdl_obtained;

	if (sdl12_OpenAudio(&sdl_desired, &sdl_obtained)) {
		free(dev);
		return NULL;
	}

	*obtained = (schism_audio_spec_t){
		.freq = sdl_obtained.freq,
		.bits = sdl_obtained.format & 0xFF,
		.channels = sdl_obtained.channels,
		.samples = sdl_obtained.samples,
	};

	sdl12_audio_opened = 1;

	return dev;
}

static void sdl12_audio_close_device(schism_audio_device_t *dev)
{
	if (!dev)
		return;

	sdl12_CloseAudio();
	free(dev);
	sdl12_audio_opened = 0;
}

/* lock/unlock/pause */

static void sdl12_audio_lock_device(schism_audio_device_t *dev)
{
	if (!dev)
		return;

	sdl12_LockAudio();
}

static void sdl12_audio_unlock_device(schism_audio_device_t *dev)
{
	if (!dev)
		return;

	sdl12_UnlockAudio();
}

static void sdl12_audio_pause_device(schism_audio_device_t *dev, int paused)
{
	if (!dev)
		return;

	sdl12_PauseAudio(paused);
}

//////////////////////////////////////////////////////////////////////////////
// dynamic loading

static int sdl12_audio_load_syms(void)
{
	SCHISM_SDL12_SYM(InitSubSystem);
	SCHISM_SDL12_SYM(QuitSubSystem);

	SCHISM_SDL12_SYM(OpenAudio);
	SCHISM_SDL12_SYM(CloseAudio);
	SCHISM_SDL12_SYM(LockAudio);
	SCHISM_SDL12_SYM(UnlockAudio);
	SCHISM_SDL12_SYM(PauseAudio);

	SCHISM_SDL12_SYM(AudioDriverName);

	return 0;
}

static int sdl12_audio_init(void)
{
	if (!sdl12_init())
		return 0;

	if (sdl12_audio_load_syms())
		return 0;

	if (!sdl12_audio_driver_info_init())
		return 0;

	return 1;
}

static void sdl12_audio_quit(void)
{
	// the subsystem quitting is handled by the quit driver function
	sdl12_audio_driver_info_quit();
	sdl12_quit();
}

//////////////////////////////////////////////////////////////////////////////

const schism_audio_backend_t schism_audio_backend_sdl12 = {
	.init = sdl12_audio_init,
	.quit = sdl12_audio_quit,

	.driver_count = sdl12_audio_driver_count,
	.driver_name = sdl12_audio_driver_name,

	.device_count = sdl12_audio_device_count,
	.device_name = sdl12_audio_device_name,

	.init_driver = sdl12_audio_init_driver,
	.quit_driver = sdl12_audio_quit_driver,

	.open_device = sdl12_audio_open_device,
	.close_device = sdl12_audio_close_device,
	.lock_device = sdl12_audio_lock_device,
	.unlock_device = sdl12_audio_unlock_device,
	.pause_device = sdl12_audio_pause_device,
};
