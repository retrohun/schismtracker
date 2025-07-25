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

#include "it.h"
#include "config.h"
#include "keyboard.h"
#include "util.h"
#include "mem.h"
#include "str.h"
#include "palettes.h"
#include "video.h"

#include "config-parser.h"
#include "dmoz.h"
#include "osdefs.h"

/* --------------------------------------------------------------------- */
/* config settings */

// cfg_dir_modules, cfg_dir_samples, and cfg_dir_instruments all are only
// allocated here because otherwise we'd need to refactor the entire text
// entry widget stuff to be able to use dynamic buffers.
char cfg_dir_modules[SCHISM_PATH_MAX + 1], cfg_dir_samples[SCHISM_PATH_MAX + 1], cfg_dir_instruments[SCHISM_PATH_MAX + 1],
	*cfg_dir_dotschism = NULL, *cfg_font = NULL;
int cfg_video_interpolation = VIDEO_INTERPOLATION_NEAREST;
char cfg_video_format[9];
int cfg_video_fullscreen = 0;
int cfg_video_want_fixed = 0;
int cfg_video_want_fixed_width = 0;
int cfg_video_want_fixed_height = 0;
int cfg_video_mousecursor = MOUSE_EMULATED;
int cfg_video_width, cfg_video_height;
int cfg_video_hardware = 0;
int cfg_video_want_menu_bar = 1;

// If these are set to zero, it means to use the
// system key repeat or the default fallback values.
int cfg_kbd_repeat_delay = 0;
int cfg_kbd_repeat_rate = 0;

// Date & time formats
int cfg_str_date_format = STR_DATE_FORMAT_DEFAULT;
int cfg_str_time_format = STR_TIME_FORMAT_DEFAULT;

/* --------------------------------------------------------------------- */

static const char *schism_dotfolders[] = {
#if defined(SCHISM_WIN32) || defined(SCHISM_MACOS) || defined(SCHISM_MACOSX) || defined(SCHISM_OS2)
	"Schism Tracker",
#elif defined(SCHISM_WII) || defined(SCHISM_WIIU) || defined(SCHISM_XBOX)
	".",
#else
# ifdef __HAIKU__
	"config/settings/schism",
# else
	".config/schism",
# endif
	".schism",
#endif
};

void cfg_init_dir(void)
{
#if defined(__amigaos4__)
	str_realloc(&cfg_dir_dotschism, "PROGDIR:", sizeof("PROGDIR:"));
#else
	char *portable_file = NULL;

	char *app_dir = dmoz_get_exe_directory();
	if (app_dir)
		portable_file = dmoz_path_concat(app_dir, "portable.txt");

	if (portable_file && dmoz_path_is_file(portable_file)) {
		printf("In portable mode.\n");

		cfg_dir_dotschism = str_dup(app_dir);
	} else {
		int found = 0;
		char *dot_dir = dmoz_get_dot_directory();

		for (size_t i = 0; i < ARRAY_SIZE(schism_dotfolders); i++) {
			cfg_dir_dotschism = dmoz_path_concat(dot_dir, schism_dotfolders[i]);

			if (dmoz_path_is_directory(cfg_dir_dotschism)) {
				found = 1;
				break;
			}
		}

		if (!found) {
			cfg_dir_dotschism = dmoz_path_concat(dot_dir, schism_dotfolders[0]);

			printf("Creating directory %s\n", cfg_dir_dotschism);
			printf("Schism Tracker uses this directory to store your settings.\n");
			if (dmoz_path_mkdir_recursive(cfg_dir_dotschism, 0777) != 0) {
				perror("Error creating directory");
				fprintf(stderr, "Everything will still work, but preferences will not be saved.\n");
			}
		}

		free(dot_dir);
	}

	free(app_dir);
	free(portable_file);
#endif
}

/* --------------------------------------------------------------------- */

static void cfg_load_palette(cfg_file_t *cfg)
{
	const char *palette_text = cfg_get_string(cfg, "General", "palette_cur", NULL, 0, NULL);

	if (palette_text && strlen(palette_text) >= 48)
		set_palette_from_string(palette_text);

	palette_load_preset(cfg_get_number(cfg, "General", "palette", 2));
}

static void cfg_save_palette(cfg_file_t *cfg)
{
	cfg_set_number(cfg, "General", "palette", current_palette_index);

	char palette_text[48 + 1] = {0};
	palette_to_string(0, palette_text);
	cfg_set_string(cfg, "General", "palette_cur", palette_text);
}

/* --------------------------------------------------------------------------------------------------------- */

void cfg_load(void)
{
	char *tmp;
	const char *ptr;
	int i;
	cfg_file_t cfg;

	tmp = dmoz_path_concat(cfg_dir_dotschism, "config");
	cfg_init(&cfg, tmp);
	free(tmp);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	ptr = cfg_get_string(&cfg, "Video", "interpolation", NULL, 0, NULL);
	if (ptr) {
		if (!strcasecmp(ptr, "nearest")) {
			cfg_video_interpolation = VIDEO_INTERPOLATION_NEAREST;
		} else if (!strcasecmp(ptr, "linear")) {
			cfg_video_interpolation = VIDEO_INTERPOLATION_LINEAR;
		} else if (!strcasecmp(ptr, "best")) {
			cfg_video_interpolation = VIDEO_INTERPOLATION_BEST;
		}
	}

#ifdef SCHISM_XBOX
/* This should be adapted to other consoles :) */
# define WIDTH_DEFAULT 640
# define HEIGHT_DEFAULT 480
# define WANT_FIXED_DEFAULT 1
# define WANT_FIXED_WIDTH_DEFAULT 640
# define WANT_FIXED_HEIGHT_DEFAULT 400
#else
# define WIDTH_DEFAULT 640
# define HEIGHT_DEFAULT 400
# define WANT_FIXED_DEFAULT 0
# define WANT_FIXED_WIDTH_DEFAULT (640 * 5)
# define WANT_FIXED_HEIGHT_DEFAULT (400 * 6)
#endif

	cfg_get_string(&cfg, "Video", "format", cfg_video_format, ARRAY_SIZE(cfg_video_format) - 1, "");
	cfg_video_width = cfg_get_number(&cfg, "Video", "width", WIDTH_DEFAULT);
	cfg_video_height = cfg_get_number(&cfg, "Video", "height", HEIGHT_DEFAULT);
	cfg_video_fullscreen = !!cfg_get_number(&cfg, "Video", "fullscreen", 0);
	cfg_video_want_fixed = cfg_get_number(&cfg, "Video", "want_fixed", WANT_FIXED_DEFAULT);
	cfg_video_want_fixed_width = cfg_get_number(&cfg, "Video", "want_fixed_width", WANT_FIXED_WIDTH_DEFAULT);
	cfg_video_want_fixed_height = cfg_get_number(&cfg, "Video", "want_fixed_height", WANT_FIXED_HEIGHT_DEFAULT);
	cfg_video_mousecursor = cfg_get_number(&cfg, "Video", "mouse_cursor", MOUSE_EMULATED);
	cfg_video_mousecursor = CLAMP(cfg_video_mousecursor, 0, MOUSE_MAX_STATE);
	cfg_video_hardware = cfg_get_number(&cfg, "Video", "hardware", 1);
	cfg_video_want_menu_bar = !!cfg_get_number(&cfg, "Video", "want_menu_bar", 1);

	tmp = dmoz_get_home_directory();
	cfg_get_string(&cfg, "Directories", "modules", cfg_dir_modules, ARRAY_SIZE(cfg_dir_modules) - 1, tmp);
	cfg_get_string(&cfg, "Directories", "samples", cfg_dir_samples, ARRAY_SIZE(cfg_dir_samples) - 1, tmp);
	cfg_get_string(&cfg, "Directories", "instruments", cfg_dir_instruments, ARRAY_SIZE(cfg_dir_instruments) - 1, tmp);
	free(tmp);

	ptr = cfg_get_string(&cfg, "Directories", "module_pattern", NULL, 0, NULL);
	if (ptr) {
		strncpy(cfg_module_pattern, ptr, ARRAY_SIZE(cfg_module_pattern) - 1);
		cfg_module_pattern[ARRAY_SIZE(cfg_module_pattern) - 1] = 0;
	}

	ptr = cfg_get_string(&cfg, "Directories", "export_pattern", NULL, 0, NULL);
	if (ptr) {
		strncpy(cfg_export_pattern, ptr, ARRAY_SIZE(cfg_export_pattern) - 1);
		cfg_export_pattern[ARRAY_SIZE(cfg_export_pattern) - 1] = 0;
	}

	ptr = cfg_get_string(&cfg, "General", "numlock_setting", NULL, 0, NULL);
	if (!ptr)
		status.fix_numlock_setting = NUMLOCK_GUESS;
	else if (strcasecmp(ptr, "on") == 0)
		status.fix_numlock_setting = NUMLOCK_ALWAYS_ON;
	else if (strcasecmp(ptr, "off") == 0)
		status.fix_numlock_setting = NUMLOCK_ALWAYS_OFF;
	else
		status.fix_numlock_setting = NUMLOCK_HONOR;

	cfg_kbd_repeat_delay = cfg_get_number(&cfg, "General", "key_repeat_delay", 0);
	cfg_kbd_repeat_rate = cfg_get_number(&cfg, "General", "key_repeat_rate", 0);

	ptr = cfg_get_string(&cfg, "General", "date_format", NULL, 0, NULL);
	if (ptr) {
		if (!strcasecmp(ptr, "mmmmdyyyy")) {
			cfg_str_date_format = STR_DATE_FORMAT_MMMMDYYYY;
		} else if (!strcasecmp(ptr, "dmmmmyyyy")) {
			cfg_str_date_format = STR_DATE_FORMAT_DMMMMYYYY;
		} else if (!strcasecmp(ptr, "yyyymmmmdd")) {
			cfg_str_date_format = STR_DATE_FORMAT_YYYYMMMMDD;
		} else if (!strcasecmp(ptr, "mdyyyy")) {
			cfg_str_date_format = STR_DATE_FORMAT_MDYYYY;
		} else if (!strcasecmp(ptr, "dmyyyy")) {
			cfg_str_date_format = STR_DATE_FORMAT_DMYYYY;
		} else if (!strcasecmp(ptr, "yyyymd")) {
			cfg_str_date_format = STR_DATE_FORMAT_YYYYMD;
		} else if (!strcasecmp(ptr, "mmddyyyy")) {
			cfg_str_date_format = STR_DATE_FORMAT_MMDDYYYY;
		} else if (!strcasecmp(ptr, "ddmmyyyy")) {
			cfg_str_date_format = STR_DATE_FORMAT_DDMMYYYY;
		} else if (!strcasecmp(ptr, "yyyymmdd")) {
			cfg_str_date_format = STR_DATE_FORMAT_YYYYMMDD;
		} else if (!strcasecmp(ptr, "iso8601")) {
			cfg_str_date_format = STR_DATE_FORMAT_ISO8601;
		}
	}

	ptr = cfg_get_string(&cfg, "General", "time_format", NULL, 0, NULL);
	if (ptr) {
		if (!strcasecmp(ptr, "12hr")) {
			cfg_str_time_format = STR_TIME_FORMAT_12HR;
		} else if (!strcasecmp(ptr, "24hr")) {
			cfg_str_time_format = STR_TIME_FORMAT_24HR;
		}
	}

	// Poll the system if it's available
	if (cfg_str_date_format == STR_DATE_FORMAT_DEFAULT || cfg_str_time_format == STR_TIME_FORMAT_DEFAULT) {
		str_date_format_t date;
		str_time_format_t time;

		if (os_get_locale_format(&date, &time)) {
			if (cfg_str_date_format == STR_DATE_FORMAT_DEFAULT)
				cfg_str_date_format = date;
			if (cfg_str_time_format == STR_TIME_FORMAT_DEFAULT)
				cfg_str_time_format = time;
		}
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	cfg_load_info(&cfg);
	cfg_load_patedit(&cfg);
	cfg_load_audio(&cfg);
	cfg_load_midi(&cfg);
	cfg_load_disko(&cfg);
	cfg_load_dmoz(&cfg);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	if (cfg_get_number(&cfg, "General", "classic_mode", 0))
		status.flags |= CLASSIC_MODE;
	else
		status.flags &= ~CLASSIC_MODE;
	if (cfg_get_number(&cfg, "General", "make_backups", 1))
		status.flags |= MAKE_BACKUPS;
	else
		status.flags &= ~MAKE_BACKUPS;
	if (cfg_get_number(&cfg, "General", "numbered_backups", 0))
		status.flags |= NUMBERED_BACKUPS;
	else
		status.flags &= ~NUMBERED_BACKUPS;

	i = cfg_get_number(&cfg, "General", "time_display", TIME_PLAY_ELAPSED);
	/* default to play/elapsed for invalid values */
	if (i < 0 || i >= TIME_PLAYBACK)
		i = TIME_PLAY_ELAPSED;
	status.time_display = i;

	i = cfg_get_number(&cfg, "General", "vis_style", VIS_OSCILLOSCOPE);
	/* default to oscilloscope for invalid values */
	if (i < 0 || i >= VIS_SENTINEL)
		i = VIS_OSCILLOSCOPE;
	status.vis_style = i;

	kbd_sharp_flat_toggle(cfg_get_number(&cfg, "General", "accidentals_as_flats", 0) == 1);

#if defined(SCHISM_MACOSX) || defined(SCHISM_MACOS)
# define DEFAULT_META 1
#else
# define DEFAULT_META 0
#endif
	if (cfg_get_number(&cfg, "General", "meta_is_ctrl", DEFAULT_META))
		status.flags |= META_IS_CTRL;
	else
		status.flags &= ~META_IS_CTRL;
	if (cfg_get_number(&cfg, "General", "altgr_is_alt", 1))
		status.flags |= ALTGR_IS_ALT;
	else
		status.flags &= ~ALTGR_IS_ALT;
	if (cfg_get_number(&cfg, "Video", "lazy_redraw", 0))
		status.flags |= LAZY_REDRAW;
	else
		status.flags &= ~LAZY_REDRAW;

	if (cfg_get_number(&cfg, "General", "midi_like_tracker", 0))
		status.flags |= MIDI_LIKE_TRACKER;
	else
		status.flags &= ~MIDI_LIKE_TRACKER;

	str_realloc(&cfg_font, cfg_get_string(&cfg, "General", "font", cfg_font, 0, "font.cfg"), 0);

	cfg_load_palette(&cfg);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	cfg_free(&cfg);
}

void cfg_midipage_save(void)
{
	char *ptr;
	cfg_file_t cfg;

	ptr = dmoz_path_concat(cfg_dir_dotschism, "config");
	cfg_init(&cfg, ptr);
	free(ptr);

	cfg_save_midi(&cfg);

	cfg_write(&cfg);
	cfg_free(&cfg);
}

void cfg_save(void)
{
	char *ptr;
	cfg_file_t cfg;

	ptr = dmoz_path_concat(cfg_dir_dotschism, "config");
	cfg_init(&cfg, ptr);
	free(ptr);

	// this wart is here because Storlek is retarded
	cfg_delete_key(&cfg, "Directories", "filename_pattern");

	cfg_set_string(&cfg, "Directories", "modules", cfg_dir_modules);
	cfg_set_string(&cfg, "Directories", "samples", cfg_dir_samples);
	cfg_set_string(&cfg, "Directories", "instruments", cfg_dir_instruments);
	/* No, it's not a directory, but whatever. */
	cfg_set_string(&cfg, "Directories", "module_pattern", cfg_module_pattern);
	cfg_set_string(&cfg, "Directories", "export_pattern", cfg_export_pattern);

	cfg_save_info(&cfg);
	cfg_save_patedit(&cfg);
	cfg_save_audio(&cfg);
	cfg_save_palette(&cfg);
	cfg_save_disko(&cfg);
	cfg_save_dmoz(&cfg);

	cfg_write(&cfg);
	cfg_free(&cfg);
}

void cfg_save_output(void)
{
	char *ptr;
	cfg_file_t cfg;

	ptr = dmoz_path_concat(cfg_dir_dotschism, "config");
	cfg_init(&cfg, ptr);
	free(ptr);

	cfg_save_audio_playback(&cfg);

	cfg_write(&cfg);
	cfg_free(&cfg);
}

void cfg_atexit_save(void)
{
	char *ptr;
	cfg_file_t cfg;

	ptr = dmoz_path_concat(cfg_dir_dotschism, "config");
	cfg_init(&cfg, ptr);
	free(ptr);

	cfg_atexit_save_audio(&cfg);

	/* TODO: move these config options to video.c, this is lame :)
	Or put everything here, which is what the note in audio_loadsave.cc
	says. Very well, I contradict myself. */
	{
		const char *names[] = {
			[VIDEO_INTERPOLATION_NEAREST] = "nearest",
			[VIDEO_INTERPOLATION_LINEAR] = "linear",
			[VIDEO_INTERPOLATION_BEST] = "best",
		};

		cfg_set_string(&cfg, "Video", "interpolation", names[cfg_video_interpolation]);
	}
	
	cfg_set_number(&cfg, "Video", "fullscreen", !!(video_is_fullscreen()));
	cfg_set_number(&cfg, "Video", "mouse_cursor", video_mousecursor_visible());
	cfg_set_number(&cfg, "Video", "lazy_redraw", !!(status.flags & LAZY_REDRAW));
	cfg_set_number(&cfg, "Video", "hardware", !!(video_is_hardware()));
	cfg_set_number(&cfg, "Video", "want_menu_bar", !!cfg_video_want_menu_bar);

	cfg_set_number(&cfg, "General", "vis_style", status.vis_style);
	cfg_set_number(&cfg, "General", "time_display", status.time_display);
	cfg_set_number(&cfg, "General", "classic_mode", !!(status.flags & CLASSIC_MODE));
	cfg_set_number(&cfg, "General", "make_backups", !!(status.flags & MAKE_BACKUPS));
	cfg_set_number(&cfg, "General", "numbered_backups", !!(status.flags & NUMBERED_BACKUPS));

	cfg_set_number(&cfg, "General", "accidentals_as_flats", (kbd_sharp_flat_state() == KBD_SHARP_FLAT_FLATS));
	cfg_set_number(&cfg, "General", "meta_is_ctrl", !!(status.flags & META_IS_CTRL));
	cfg_set_number(&cfg, "General", "altgr_is_alt", !!(status.flags & ALTGR_IS_ALT));

	cfg_set_number(&cfg, "General", "midi_like_tracker", !!(status.flags & MIDI_LIKE_TRACKER));
	/* Say, whose bright idea was it to make this a string setting?
	The config file is human editable but that's mostly for developer convenience and debugging
	purposes. These sorts of things really really need to be options in the GUI so that people
	don't HAVE to touch the settings. Then we can just use an enum (and we *could* in theory
	include comments to the config by default listing what the numbers are, but that shouldn't
	be necessary in most cases. */
	switch (status.fix_numlock_setting) {
	case NUMLOCK_ALWAYS_ON:
		cfg_set_string(&cfg, "General", "numlock_setting", "on");
		break;
	case NUMLOCK_ALWAYS_OFF:
		cfg_set_string(&cfg, "General", "numlock_setting", "off");
		break;
	case NUMLOCK_HONOR:
		cfg_set_string(&cfg, "General", "numlock_setting", "system");
		break;
	case NUMLOCK_GUESS:
		/* leave empty */
		break;
	};

	/* hm... most of the time probably nothing's different, so saving the
	config file here just serves to make the backup useless. maybe add a
	'dirty' flag to the config parser that checks if any settings are
	actually *different* from those in the file? */

	cfg_write(&cfg);
	cfg_free(&cfg);
}

