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

#include "init.h"

#include "headers.h"
#include "mem.h"
#include "backend/audio.h"

struct schism_audio_device {
	SDL_AudioDeviceID id;
	void (*callback)(uint8_t *stream, int len);
};

#ifndef SDL_AUDIO_ALLOW_SAMPLES_CHANGE /* added in SDL 2.0.9 */
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE 0x00000008
#endif

static int (SDLCALL *sdl2_InitSubSystem)(uint32_t flags);
static void (SDLCALL *sdl2_QuitSubSystem)(uint32_t flags);

static int (SDLCALL *sdl2_AudioInit)(const char *driver_name);
static void (SDLCALL *sdl2_AudioQuit)(void);

static int (SDLCALL *sdl2_GetNumAudioDrivers)(void) = NULL;
static const char *(SDLCALL *sdl2_GetAudioDriver)(int i) = NULL;

static int (SDLCALL *sdl2_GetNumAudioDevices)(int) = NULL;
static const char *(SDLCALL *sdl2_GetAudioDeviceName)(int, int) = NULL;

static SDL_AudioDeviceID (SDLCALL *sdl2_OpenAudioDevice)(const char *device, int iscapture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
static void (SDLCALL *sdl2_CloseAudioDevice)(SDL_AudioDeviceID dev);
static void (SDLCALL *sdl2_LockAudioDevice)(SDL_AudioDeviceID dev);
static void (SDLCALL *sdl2_UnlockAudioDevice)(SDL_AudioDeviceID dev);
static void (SDLCALL *sdl2_PauseAudioDevice)(SDL_AudioDeviceID dev, int pause_on);

static const char * (SDLCALL *sdl2_GetError)(void);
static void (SDLCALL *sdl2_ClearError)(void);
static int (SDLCALL *sdl2_SetError)(const char *fmt, ...);

static int (SDLCALL *sdl2_setenv)(const char *name, const char *val, int overwrite);

/* explanation for this:
 * in 2.0.18, the logic for SDL's audio initialization functions
 * changed, so that you can use SDL_AudioInit() directly without
 * any repercussions; before that, SDL would do a sanity check
 * calling SDL_WasInit() which surprise surprise doesn't actually
 * get initialized from SDL_AudioInit(). to work around this, we
 * have to use a separate audio driver initialization function
 * under SDL pre-2.0.18. */
static int SDLCALL schism_init_audio_impl(const char *name)
{
	sdl2_setenv("SDL_AUDIODRIVER", name, 1);

	return sdl2_InitSubSystem(SDL_INIT_AUDIO);

	/* don't even bother cleaning up... it won't matter */
}

static void SDLCALL schism_quit_audio_impl(void)
{
	sdl2_QuitSubSystem(SDL_INIT_AUDIO);
}

static int (SDLCALL *sdl2_audio_init_func)(const char *);
static void (SDLCALL *sdl2_audio_quit_func)(void);

/* ---------------------------------------------------------- */
/* drivers */

static int sdl2_audio_driver_count(void)
{
	return sdl2_GetNumAudioDrivers();
}

static const char *sdl2_audio_driver_name(int i)
{
	return sdl2_GetAudioDriver(i);
}

/* --------------------------------------------------------------- */

static uint32_t sdl2_audio_device_count(uint32_t flags)
{
	int x;

	x = sdl2_GetNumAudioDevices(!!(flags & AUDIO_BACKEND_CAPTURE));

	return MAX(x, 0);
}

static const char *sdl2_audio_device_name(uint32_t i)
{
	return sdl2_GetAudioDeviceName(i & AUDIO_BACKEND_DEVICE_MASK, !!(i & AUDIO_BACKEND_CAPTURE));
}

/* ---------------------------------------------------------- */

static int sdl2_audio_init_driver(const char *driver)
{
	int x = sdl2_audio_init_func(driver);
	if (x < 0)
		return x;

	// force poll for audio devices
	(void)sdl2_GetNumAudioDevices(0);

	return 0;
}

static void sdl2_audio_quit_driver(void)
{
	sdl2_audio_quit_func();
}

/* -------------------------------------------------------- */

// This is here to prevent having to put SDLCALL into
// the original audio callback
static void SDLCALL sdl2_dummy_callback(void *userdata, uint8_t *stream, int len)
{
	// call our own callback
	schism_audio_device_t *dev = userdata;

	dev->callback(stream, len);
}

// nonzero on success
static inline int sdl2_audio_open_device_impl(schism_audio_device_t *dev,
	const char *name, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained,
	int change, int isinput)
{
	// cache the current error
	const char *err = sdl2_GetError();

	sdl2_ClearError();

	SDL_AudioDeviceID id = sdl2_OpenAudioDevice(name, !!isinput, desired, obtained, change);

	const char *new_err = sdl2_GetError();

	int failed = (new_err && *new_err);

	// reset the original error
	sdl2_SetError("%s", err);

	if (!failed) {
		dev->id = id;
		return 1;
	}

	return 0;
}

static schism_audio_device_t *sdl2_audio_open_device(uint32_t id, const schism_audio_spec_t *desired, schism_audio_spec_t *obtained)
{
	schism_audio_device_t *dev;
	uint32_t format;
	int change;

	dev = mem_calloc(1, sizeof(*dev));
	dev->callback = desired->callback;

	switch (desired->bits) {
	case 8: format = AUDIO_U8; break;
	default:
	case 16: format = AUDIO_S16SYS; break;
	case 32: format = AUDIO_S32SYS; break;
	}

	SDL_AudioSpec sdl_desired = {0};
	sdl_desired.freq = desired->freq;
	sdl_desired.format = format;
	sdl_desired.channels = desired->channels;
	sdl_desired.samples = desired->samples;
	sdl_desired.callback = sdl2_dummy_callback;
	sdl_desired.userdata = dev;

	SDL_AudioSpec sdl_obtained;

	const char *name = ((id & AUDIO_BACKEND_DEVICE_MASK) != AUDIO_BACKEND_DEFAULT)
		? sdl2_GetAudioDeviceName(id & AUDIO_BACKEND_DEVICE_MASK, !!(id & AUDIO_BACKEND_CAPTURE))
		: NULL;

	change = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;

	// First try opening the device without any change at all
	// (well, except frequencies ;))
	if (sdl2_audio_open_device_impl(dev, name, &sdl_desired, &sdl_obtained, change, !!(id & AUDIO_BACKEND_CAPTURE)))
		goto got_device;

	change = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE;

	// !!! FIXME: SDL_GetAudioDeviceName might change
	if (sdl2_audio_open_device_impl(dev, name, &sdl_desired, &sdl_obtained, change, !!(id & AUDIO_BACKEND_CAPTURE))) {
		int need_reopen = 0;

		switch (sdl_obtained.format) {
		case AUDIO_U8: case AUDIO_S16SYS: case AUDIO_S32SYS: break;
		// TODO we actually can do float32 now
		default: change &= ~(SDL_AUDIO_ALLOW_FORMAT_CHANGE); need_reopen = 1; break;
		}

		switch (sdl_obtained.channels) {
		case 1: case 2: break;
		default: change &= ~(SDL_AUDIO_ALLOW_CHANNELS_CHANGE); need_reopen = 1; break;
		}

		if (!need_reopen)
			goto got_device;

		sdl2_CloseAudioDevice(dev->id);

		if (sdl2_audio_open_device_impl(dev, name, &sdl_desired, &sdl_obtained, change, !!(id & AUDIO_BACKEND_CAPTURE)))
			goto got_device;
	}

	free(dev);
	return NULL;

got_device:
	memset(obtained, 0, sizeof(*obtained));
	
	obtained->freq = sdl_obtained.freq;
	obtained->bits = SDL_AUDIO_BITSIZE(sdl_obtained.format);
	obtained->channels = sdl_obtained.channels;
	obtained->samples = sdl_obtained.samples;

	return dev;
}

static void sdl2_audio_close_device(schism_audio_device_t *dev)
{
	if (!dev)
		return;

	sdl2_CloseAudioDevice(dev->id);
	free(dev);
}

static void sdl2_audio_lock_device(schism_audio_device_t *dev)
{
	if (!dev)
		return;

	sdl2_LockAudioDevice(dev->id);
}

static void sdl2_audio_unlock_device(schism_audio_device_t *dev)
{
	if (!dev)
		return;

	sdl2_UnlockAudioDevice(dev->id);
}

static void sdl2_audio_pause_device(schism_audio_device_t *dev, int paused)
{
	if (!dev)
		return;

	sdl2_PauseAudioDevice(dev->id, paused);
}

//////////////////////////////////////////////////////////////////////////////
// dynamic loading

static int sdl2_audio_load_syms(void)
{
	SCHISM_SDL2_SYM(InitSubSystem);
	SCHISM_SDL2_SYM(QuitSubSystem);

	SCHISM_SDL2_SYM(AudioInit);
	SCHISM_SDL2_SYM(AudioQuit);

	SCHISM_SDL2_SYM(GetNumAudioDrivers);
	SCHISM_SDL2_SYM(GetAudioDriver);

	SCHISM_SDL2_SYM(GetNumAudioDevices);
	SCHISM_SDL2_SYM(GetAudioDeviceName);

	SCHISM_SDL2_SYM(OpenAudioDevice);
	SCHISM_SDL2_SYM(CloseAudioDevice);
	SCHISM_SDL2_SYM(LockAudioDevice);
	SCHISM_SDL2_SYM(UnlockAudioDevice);
	SCHISM_SDL2_SYM(PauseAudioDevice);

	SCHISM_SDL2_SYM(GetError);
	SCHISM_SDL2_SYM(ClearError);
	SCHISM_SDL2_SYM(SetError);

	SCHISM_SDL2_SYM(setenv);

	return 0;
}

static int sdl2_audio_init(void)
{
	if (!sdl2_init())
		return 0;

	if (sdl2_audio_load_syms())
		return 0;

	/* see if we can use the normal audio init and quit functions
	 * fortunately, this still works under sdl2-compat... */
	if (sdl2_ver_atleast(2, 0, 18)) {
		sdl2_audio_init_func = sdl2_AudioInit;
		sdl2_audio_quit_func = sdl2_AudioQuit;
	} else {
		sdl2_audio_init_func = schism_init_audio_impl;
		sdl2_audio_quit_func = schism_quit_audio_impl;
	}

	return 1;
}

static void sdl2_audio_quit(void)
{
	// the subsystem quitting is handled by the quit driver function
	sdl2_quit();
}

//////////////////////////////////////////////////////////////////////////////

const schism_audio_backend_t schism_audio_backend_sdl2 = {
	.init = sdl2_audio_init,
	.quit = sdl2_audio_quit,

	.driver_count = sdl2_audio_driver_count,
	.driver_name = sdl2_audio_driver_name,

	.device_count = sdl2_audio_device_count,
	.device_name = sdl2_audio_device_name,

	.init_driver = sdl2_audio_init_driver,
	.quit_driver = sdl2_audio_quit_driver,

	.open_device = sdl2_audio_open_device,
	.close_device = sdl2_audio_close_device,
	.lock_device = sdl2_audio_lock_device,
	.unlock_device = sdl2_audio_unlock_device,
	.pause_device = sdl2_audio_pause_device,
};
