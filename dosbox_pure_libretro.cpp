/*
 *  Copyright (C) 2020-2023 Bernhard Schelling
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "include/dosbox.h"
#include "include/cpu.h"
#include "include/control.h"
#include "include/render.h"
#include "include/keyboard.h"
#include "include/mouse.h"
#include "include/joystick.h"
#include "include/bios_disk.h"
#include "include/callback.h"
#include "include/dbp_serialize.h"
#include "include/dbp_threads.h"
#include "src/ints/int10.h"
#include "src/dos/drives.h"
#include "keyb2joypad.h"
#include "libretro-common/include/libretro.h"
#include "libretro-common/include/retro_timers.h"
#include <string>
#include <sstream>
#include <chrono>

// RETROARCH AUDIO/VIDEO
#ifdef GEKKO // From RetroArch/config.def.h
#define DBP_DEFAULT_SAMPLERATE 44100.0
#define DBP_DEFAULT_SAMPLERATE_STRING "44100"
#elif defined(_3DS)
#define DBP_DEFAULT_SAMPLERATE 32730.0
#define DBP_DEFAULT_SAMPLERATE_STRING "32730"
#else
#define DBP_DEFAULT_SAMPLERATE 48000.0
#define DBP_DEFAULT_SAMPLERATE_STRING "48000"
#endif
static retro_system_av_info av_info;

// DOSBOX STATE
static enum DBP_State : Bit8u { DBPSTATE_BOOT, DBPSTATE_EXITED, DBPSTATE_SHUTDOWN, DBPSTATE_REBOOT, DBPSTATE_FIRST_FRAME, DBPSTATE_RUNNING } dbp_state;
static enum DBP_SerializeMode : Bit8u { DBPSERIALIZE_DISABLED, DBPSERIALIZE_STATES, DBPSERIALIZE_REWIND } dbp_serializemode;
static enum DBP_Latency : Bit8u { DBP_LATENCY_DEFAULT, DBP_LATENCY_LOW, DBP_LATENCY_VARIABLE } dbp_latency;
static bool dbp_game_running, dbp_pause_events, dbp_paused_midframe, dbp_frame_pending, dbp_force60fps, dbp_biosreboot, dbp_system_cached, dbp_system_scannable, dbp_refresh_memmaps;
static bool dbp_optionsupdatecallback, dbp_last_hideadvanced, dbp_reboot_set64mem, dbp_last_fastforward, dbp_use_network, dbp_had_game_running, dbp_strict_mode, dbp_legacy_save, dbp_swapstereo;
static char dbp_menu_time, dbp_conf_loading, dbp_reboot_machine;
static Bit8u dbp_alphablend_base;
static float dbp_auto_target, dbp_targetrefreshrate;
static Bit32u dbp_lastmenuticks, dbp_framecount, dbp_serialize_time;
static Semaphore semDoContinue, semDidPause;
static retro_throttle_state dbp_throttle;
static retro_time_t dbp_lastrun;
static std::string dbp_crash_message;
static std::string dbp_content_path;
static std::string dbp_content_name;
static retro_time_t dbp_boot_time;
static size_t dbp_serializesize;
static Bit16s dbp_content_year;
static const Bit32s Cycles1981to1999[1+1999-1981] = { 900, 1400, 1800, 2300, 2800, 3800, 4800, 6300, 7800, 14000, 23800, 27000, 44000, 55000, 66800, 93000, 125000, 200000, 350000 };

// DOSBOX AUDIO/VIDEO
static Bit8u buffer_active, dbp_overscan;
static struct DBP_Buffer { Bit32u video[SCALER_MAXWIDTH * SCALER_MAXHEIGHT], width, height, border_color; float ratio; } dbp_buffers[2];
enum { DBP_MAX_SAMPLES = 4096 }; // twice amount of mixer blocksize (96khz @ 30 fps max)
static int16_t dbp_audio[DBP_MAX_SAMPLES * 2]; // stereo
static double dbp_audio_remain;
static void* dbp_intercept_data;
typedef void(*dbp_intercept_gfx_func)(DBP_Buffer& buf, void* data);
static dbp_intercept_gfx_func dbp_intercept_gfx;

// DOSBOX DISC MANAGEMENT
struct DBP_Image { std::string path, longpath; bool mounted = false, remount = false, image_disk = false; char drive; int dirlen; };
static std::vector<DBP_Image> dbp_images;
static std::vector<std::string> dbp_osimages, dbp_shellzips;
static StringToPointerHashMap<void> dbp_vdisk_filter;
static unsigned dbp_image_index;

// DOSBOX INPUT
struct DBP_InputBind
{
	Bit8u port, device, index, id;
	Bit16s evt, meta; union { Bit16s lastval; Bit32u _32bitalign; };
	#define PORT_DEVICE_INDEX_ID(b) (*(Bit32u*)&static_cast<const DBP_InputBind&>(b))
};
enum { DBP_MAX_PORTS = 8, DBP_JOY_ANALOG_RANGE = 0x8000 }; // analog stick range is -0x8000 to 0x8000
static const char* DBP_KBDNAMES[] =
{
	"None","1","2","3","4","5","6","7","8","9","0","Q","W","E","R","T","Y","U","I","O","P","A","S","D","F","G","H","J","K","L","Z","X","C","V","B","N","M",
	"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","Esc","Tab","Backspace","Enter","Space","Left-Alt","Right-Alt","Left-Ctrl","Right-Ctrl","Left-Shift","Right-Shift",
	"Caps-Lock","Scroll-Lock","Num-Lock","Grave `","Minus -","Equals =","Backslash","Left-Bracket [","Right-Bracket ]","Semicolon ;","Quote '","Period .","Comma ,","Slash /","Backslash \\",
	"Print-Screen","Pause","Insert","Home","Page-Up","Delete","End","Page-Down","Left","Up","Down","Right","NP-1","NP-2","NP-3","NP-4","NP-5","NP-6","NP-7","NP-8","NP-9","NP-0",
	"NP-Divide /","NP-Multiply *","NP-Minus -","NP-Plus +","NP-Enter","NP-Period .",""
};
static std::vector<DBP_InputBind> dbp_input_binds;
static std::vector<Bit8u> dbp_custom_mapping;
static Bit8u dbp_port_mode[DBP_MAX_PORTS];
static bool dbp_input_binds_modified, dbp_on_screen_keyboard, dbp_analog_buttons;
static char dbp_mouse_input, dbp_auto_mapping_mode;
static Bit16s dbp_bind_mousewheel, dbp_mouse_x, dbp_mouse_y;
static int dbp_joy_analog_deadzone = (int)(0.15f * (float)DBP_JOY_ANALOG_RANGE);
static float dbp_mouse_speed = 1, dbp_mouse_speed_x = 1;
static const Bit8u* dbp_auto_mapping;
static const char *dbp_auto_mapping_names, *dbp_auto_mapping_title;
#define DBP_GET_JOY_ANALOG_VALUE(V) ((V >= -dbp_joy_analog_deadzone && V <= dbp_joy_analog_deadzone) ? 0.0f : \
	((float)((V > dbp_joy_analog_deadzone) ? (V - dbp_joy_analog_deadzone) : (V + dbp_joy_analog_deadzone)) / (float)(DBP_JOY_ANALOG_RANGE - dbp_joy_analog_deadzone)))

// DOSBOX EVENTS
enum DBP_Event_Type : Bit8u
{
	DBPET_JOY1X, DBPET_JOY1Y, DBPET_JOY2X, DBPET_JOY2Y, DBPET_JOYMX, DBPET_JOYMY, _DBPET_JOY_AXIS_MAX = DBPET_JOYMY,

	DBPET_MOUSEMOVE, _DBPET_ACCUMULATABLE_MAX = DBPET_MOUSEMOVE,
	DBPET_MOUSEDOWN, DBPET_MOUSEUP,
	DBPET_MOUSESETSPEED, DBPET_MOUSERESETSPEED,
	DBPET_JOYHATSETBIT, DBPET_JOYHATUNSETBIT,
	DBPET_JOY1DOWN, DBPET_JOY1UP,
	DBPET_JOY2DOWN, DBPET_JOY2UP,
	DBPET_KEYDOWN, DBPET_KEYUP,
	DBPET_ONSCREENKEYBOARD, DBPET_ONSCREENKEYBOARDUP,

	DBPET_AXISMAPPAIR,
	DBPET_CHANGEMOUNTS,

	#define DBP_IS_RELEASE_EVENT(EVT) ((EVT) >= DBPET_MOUSEUP && !(EVT & 1))
	#define DBP_MAPPAIR_MAKE(KEY1,KEY2) (Bit16s)(((KEY1)<<8)|(KEY2))
	#define DBP_MAPPAIR_GET(VAL,META) ((VAL) < 0 ? (Bit8u)(((Bit16u)(META))>>8) : (Bit8u)(((Bit16u)(META))&255))
	#define DBP_GETKEYDEVNAME(KEY) ((KEY) == KBD_NONE ? NULL : (KEY) < KBD_LAST ? DBPDEV_Keyboard : DBP_SpecialMappings[(KEY)-DBP_SPECIALMAPPINGS_KEY].dev)
	#define DBP_GETKEYNAME(KEY) ((KEY) < KBD_LAST ? DBP_KBDNAMES[(KEY)] : DBP_SpecialMappings[(KEY)-DBP_SPECIALMAPPINGS_KEY].name)

	_DBPET_MAX
};
//static const char* DBP_Event_Type_Names[] = { "JOY1X", "JOY1Y", "JOY2X", "JOY2Y", "JOYMX", "JOYMY", "MOUSEMOVE", "MOUSEDOWN", "MOUSEUP", "MOUSESETSPEED", "MOUSERESETSPEED", "JOYHATSETBIT", "JOYHATUNSETBIT", "JOY1DOWN", "JOY1UP", "JOY2DOWN", "JOY2UP", "KEYDOWN", "KEYUP", "ONSCREENKEYBOARD", "ONSCREENKEYBOARDUP", "AXIS_TO_KEY", "CHANGEMOUNTS", "MAX" };
static const char *DBPDEV_Keyboard = "Keyboard", *DBPDEV_Mouse = "Mouse", *DBPDEV_Joystick = "Joystick";
static const struct DBP_SpecialMapping { int16_t evt, meta; const char *dev, *name; } DBP_SpecialMappings[] =
{
	{ DBPET_JOYMY,         -1, DBPDEV_Mouse,    "Move Up"      }, // 200
	{ DBPET_JOYMY,          1, DBPDEV_Mouse,    "Move Down"    }, // 201
	{ DBPET_JOYMX,         -1, DBPDEV_Mouse,    "Move Left"    }, // 202
	{ DBPET_JOYMX,          1, DBPDEV_Mouse,    "Move Right"   }, // 203
	{ DBPET_MOUSEDOWN,      0, DBPDEV_Mouse,    "Left Click"   }, // 204
	{ DBPET_MOUSEDOWN,      1, DBPDEV_Mouse,    "Right Click"  }, // 205
	{ DBPET_MOUSEDOWN,      2, DBPDEV_Mouse,    "Middle Click" }, // 206
	{ DBPET_MOUSESETSPEED,  1, DBPDEV_Mouse,    "Speed Up"     }, // 207
	{ DBPET_MOUSESETSPEED, -1, DBPDEV_Mouse,    "Slow Down"    }, // 208
	{ DBPET_JOY1Y,         -1, DBPDEV_Joystick, "Up"           }, // 209
	{ DBPET_JOY1Y,          1, DBPDEV_Joystick, "Down"         }, // 210
	{ DBPET_JOY1X,         -1, DBPDEV_Joystick, "Left"         }, // 211
	{ DBPET_JOY1X,          1, DBPDEV_Joystick, "Right"        }, // 212
	{ DBPET_JOY1DOWN,       0, DBPDEV_Joystick, "Button 1"     }, // 213
	{ DBPET_JOY1DOWN,       1, DBPDEV_Joystick, "Button 2"     }, // 214
	{ DBPET_JOY2DOWN,       0, DBPDEV_Joystick, "Button 3"     }, // 215
	{ DBPET_JOY2DOWN,       1, DBPDEV_Joystick, "Button 4"     }, // 216
	{ DBPET_JOYHATSETBIT,   8, DBPDEV_Joystick, "Hat Up"       }, // 217
	{ DBPET_JOYHATSETBIT,   2, DBPDEV_Joystick, "Hat Down"     }, // 218
	{ DBPET_JOYHATSETBIT,   1, DBPDEV_Joystick, "Hat Left"     }, // 219
	{ DBPET_JOYHATSETBIT,   4, DBPDEV_Joystick, "Hat Right"    }, // 220
	{ DBPET_JOY2Y,         -1, DBPDEV_Joystick, "Joy 2 Up"     }, // 221
	{ DBPET_JOY2Y,          1, DBPDEV_Joystick, "Joy 2 Down"   }, // 222
	{ DBPET_JOY2X,         -1, DBPDEV_Joystick, "Joy 2 Left"   }, // 223
	{ DBPET_JOY2X,          1, DBPDEV_Joystick, "Joy 2 Right"  }, // 224
	{ DBPET_ONSCREENKEYBOARD, 0, NULL, "On Screen Keyboard"    }, // 225
};
#define DBP_SPECIALMAPPING(key) DBP_SpecialMappings[(key)-DBP_SPECIALMAPPINGS_KEY]
enum { DBP_SPECIALMAPPINGS_KEY = 200, DBP_SPECIALMAPPINGS_MAX = 200+(sizeof(DBP_SpecialMappings)/sizeof(DBP_SpecialMappings[0])) };
enum { DBP_EVENT_QUEUE_SIZE = 256, DBP_DOWN_BY_KEYBOARD = 128 };
static struct DBP_Event { DBP_Event_Type type; int val, val2; } dbp_event_queue[DBP_EVENT_QUEUE_SIZE];
static int dbp_event_queue_write_cursor;
static int dbp_event_queue_read_cursor;
static int dbp_keys_down_count;
static unsigned char dbp_keys_down[KBD_LAST+16];
static unsigned short dbp_keymap_dos2retro[KBD_LAST];
static unsigned char dbp_keymap_retro2dos[RETROK_LAST];
typedef void(*dbp_intercept_input_func)(DBP_Event_Type type, int val, int val2, void* data);
static dbp_intercept_input_func dbp_intercept_input;

// LIBRETRO CALLBACKS
static void retro_fallback_log(enum retro_log_level level, const char *fmt, ...)
{
	(void)level;
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}
#ifdef ANDROID
extern "C" int __android_log_write(int prio, const char *tag, const char *text);
static void AndroidLogFallback(int level, const char *fmt, ...) { static char buf[8192]; va_list va; va_start(va, fmt); vsprintf(buf, fmt, va); va_end(va); __android_log_write(2, "DBP", buf); }
#endif
static retro_time_t time_in_microseconds()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}
static retro_log_printf_t         log_cb = retro_fallback_log;
static retro_perf_get_time_usec_t time_cb = time_in_microseconds;
static retro_environment_t        environ_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;

// PERF OVERLAY
static enum DBP_Perf : Bit8u { DBP_PERF_NONE, DBP_PERF_SIMPLE, DBP_PERF_DETAILED } dbp_perf;
static Bit32u dbp_perf_uniquedraw, dbp_perf_count, dbp_perf_emutime, dbp_perf_totaltime;
//#define DBP_ENABLE_WAITSTATS
#ifdef DBP_ENABLE_WAITSTATS
static Bit32u dbp_wait_pause, dbp_wait_finish, dbp_wait_paused, dbp_wait_continue;
#endif

// PERF FPS COUNTERS
//#define DBP_ENABLE_FPS_COUNTERS
#ifdef DBP_ENABLE_FPS_COUNTERS
static Bit32u dbp_lastfpstick, dbp_fpscount_retro, dbp_fpscount_gfxstart, dbp_fpscount_gfxend, dbp_fpscount_event;
#define DBP_FPSCOUNT(DBP_FPSCOUNT_VARNAME) DBP_FPSCOUNT_VARNAME++;
#else
#define DBP_FPSCOUNT(DBP_FPSCOUNT_VARNAME)
#endif

void retro_notify(int duration, retro_log_level lvl, char const* format,...)
{
	static char buf[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	retro_message_ext msg;
	msg.msg = buf;
	msg.duration = (duration ? (unsigned)abs(duration) : 4000);
	msg.priority = 0;
	msg.level = lvl;
	msg.target = (duration < 0 ? RETRO_MESSAGE_TARGET_OSD : RETRO_MESSAGE_TARGET_ALL);
	msg.type = (duration < 0 ? RETRO_MESSAGE_TYPE_STATUS : RETRO_MESSAGE_TYPE_NOTIFICATION);
	if (!environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg) && duration >= 0) log_cb(RETRO_LOG_ERROR, "%s", buf);
}

static const char* retro_get_variable(const char* key, const char* default_value)
{
	retro_variable var = { key };
	return (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : default_value);
}

static void retro_set_visibility(const char* key, bool visible)
{
	retro_core_option_display disp = { key, visible };
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &disp);
}

// ------------------------------------------------------------------------------

void DBP_DOSBOX_ForceShutdown(const Bitu = 0);
void DBP_CPU_ModifyCycles(const char* val, const char* params = NULL);
void DBP_KEYBOARD_ReleaseKeys();
void DBP_CGA_SetModelAndComposite(bool new_model, Bitu new_comp_mode);
void DBP_Hercules_SetPalette(Bit8u pal);
void DBP_SetMountSwappingRequested();
Bit32u DBP_MIXER_GetFrequency();
Bit32u DBP_MIXER_DoneSamplesCount();
void MIXER_CallBack(void *userdata, uint8_t *stream, int len);
bool MSCDEX_HasDrive(char driveLetter);
int MSCDEX_AddDrive(char driveLetter, const char* physicalPath, Bit8u& subUnit);
int MSCDEX_RemoveDrive(char driveLetter);
void IDE_RefreshCDROMs();
void IDE_SetupControllers(char force_cd_drive_letter = 0);
void NET_SetupEthernet();
bool MIDI_TSF_SwitchSF(const char*);
bool MIDI_Retro_HasOutputIssue();

static void DBP_QueueEvent(DBP_Event_Type type, int val = 0, int val2 = 0, DBP_InputBind *binds = NULL, DBP_InputBind *binds_end = NULL)
{
	unsigned char* downs = dbp_keys_down;
	switch (type)
	{
		case DBPET_KEYDOWN: goto check_down;
		case DBPET_KEYUP:   goto check_up;
		case DBPET_MOUSEDOWN:          downs = dbp_keys_down + KBD_LAST +  0; goto check_down;
		case DBPET_MOUSEUP:            downs = dbp_keys_down + KBD_LAST +  0; goto check_up;
		case DBPET_JOY1DOWN:           downs = dbp_keys_down + KBD_LAST +  3; goto check_down;
		case DBPET_JOY1UP:             downs = dbp_keys_down + KBD_LAST +  3; goto check_up;
		case DBPET_JOY2DOWN:           downs = dbp_keys_down + KBD_LAST +  5; goto check_down;
		case DBPET_JOY2UP:             downs = dbp_keys_down + KBD_LAST +  5; goto check_up;
		case DBPET_JOYHATSETBIT:       downs = dbp_keys_down + KBD_LAST +  7; goto check_down;
		case DBPET_JOYHATUNSETBIT:     downs = dbp_keys_down + KBD_LAST +  7; goto check_up;
		case DBPET_ONSCREENKEYBOARD:   downs = dbp_keys_down + KBD_LAST + 15; goto check_down;
		case DBPET_ONSCREENKEYBOARDUP: downs = dbp_keys_down + KBD_LAST + 15; goto check_up;

		check_down:
			if (((++downs[val]) & 127) > 1) return;
			if (downs == dbp_keys_down) dbp_keys_down_count++;
			break;
		check_up:
			if (((downs[val]) & 127) == 0 || ((--downs[val]) & 127) > 0) return;
			if (downs == dbp_keys_down) dbp_keys_down_count--;
			break;

		case DBPET_JOY1X: case DBPET_JOY1Y: case DBPET_JOY2X: case DBPET_JOY2Y: case DBPET_JOYMX: case DBPET_JOYMY:
			if (val) break;
			for (DBP_InputBind *b = binds; b != binds_end; b++) // check if another bind is currently influencing the same axis
			{
				if (!b->lastval) continue;
				Bit16s bval = b->lastval;
				if (b->evt <= _DBPET_JOY_AXIS_MAX)
				{
					if ((DBP_Event_Type)b->evt != type) continue;
					val = (b->meta ? (bval ? 32767 : 0) * b->meta : bval);
					goto found_axis_value;
				}
				else if (b->device != RETRO_DEVICE_ANALOG) continue;
				else for (Bit16s dir = 1; dir >= -1; dir -= 2)
				{
					Bit16s map = DBP_MAPPAIR_GET(dir, b->meta), dirbval = bval * dir;
					if (map < DBP_SPECIALMAPPINGS_KEY || dirbval < 0 || (DBP_Event_Type)DBP_SPECIALMAPPING(map).evt != type) continue;
					val = (dirbval < 0 ? 0 : dirbval) * DBP_SPECIALMAPPING(map).meta;
					goto found_axis_value;
				}
			}
			found_axis_value:break;
		default:;
	}
	DBP_Event evt = { type, val, val2 };
	DBP_ASSERT(evt.type != DBPET_AXISMAPPAIR);
	int cur = dbp_event_queue_write_cursor, next = ((cur + 1) % DBP_EVENT_QUEUE_SIZE);
	if (next == dbp_event_queue_read_cursor)
	{
		// queue full, thread is probably busy (decompression?), try to collapse a duplicated event
		dbp_event_queue_write_cursor = next; //stop event processing
		for (int i = cur; (i = ((i + DBP_EVENT_QUEUE_SIZE - 1) % DBP_EVENT_QUEUE_SIZE)) != cur;)
		{
			DBP_Event ie = dbp_event_queue[i];
			for (int j = i; j != cur; j = ((j + DBP_EVENT_QUEUE_SIZE - 1) % DBP_EVENT_QUEUE_SIZE))
			{
				DBP_Event je = (j == i ? evt : dbp_event_queue[j]);
				if (je.type != ie.type) continue;
				if (ie.type <= _DBPET_ACCUMULATABLE_MAX) { ie.val += je.val; ie.val2 += je.val2; }
				cur = j;
				goto remove_element_at_cur;
			}
		}
		// Found nothing to remove, just blindly remove the last element
		DBP_ASSERT(false);
		//if (1)
		//{
		//	for (next = cur; (cur = ((cur + DBP_EVENT_QUEUE_SIZE - 1) % DBP_EVENT_QUEUE_SIZE)) != next;)
		//	{
		//		fprintf(stderr, "EVT [%3d] - %20s (%2d) - %d\n", cur, DBP_Event_Type_Names[dbp_event_queue[cur].type], dbp_event_queue[cur].type, dbp_event_queue[cur].val);
		//	}
		//	fprintf(stderr, "EVT [ADD] - %20s (%2d) - %d\n", DBP_Event_Type_Names[evt.type], evt.type, evt.val);
		//}
		// remove element at cur and shift everything up to next one down
		remove_element_at_cur:
		next = ((next + DBP_EVENT_QUEUE_SIZE - 1) % DBP_EVENT_QUEUE_SIZE);
		for (int n = cur; (n = ((n + 1) % DBP_EVENT_QUEUE_SIZE)) != next; cur = n)
			dbp_event_queue[cur] = dbp_event_queue[n];
	}
	dbp_event_queue[cur] = evt;
	dbp_event_queue_write_cursor = next;
}

static void DBP_ReportCoreMemoryMaps()
{
	extern const char* RunningProgram;
	const bool booted_os = !strcmp(RunningProgram, "BOOT");
	const size_t conventional_end = 640 * 1024, memtotal = (MEM_TotalPages() * 4096);

	// Give access to entire memory to frontend (cheat and achievements support)
	// Instead of raw [ENVIRONMENT] [GAME] [EXPANDED MEMORY] we switch the order to be
	// [GAME] [ENVIRONMENT] [EXPANDED MEMORY] so regardless of the size of the OS environment
	// the game memory (below 640k) is always at the same (virtual) address.

	struct retro_memory_descriptor mdescs[3] = { 0 }, *mdesc_expandedmem;
	if (!booted_os)
	{
		Bit16u seg_prog_start = (DOS_MEM_START + 2 + 5); // see mcb_sizes in DOS_SetupMemory
		while (DOS_MCB(seg_prog_start).GetPSPSeg() == 0x40) // tempmcb2 from DOS_SetupMemory and "memlimit" dos config
			seg_prog_start += (Bit16u)(1 + DOS_MCB(seg_prog_start).GetSize()); // skip past fake loadfix segment blocks
		const size_t prog_start = PhysMake(seg_prog_start, 0);
		mdescs[0].flags      = RETRO_MEMDESC_SYSTEM_RAM;
		mdescs[0].start      = 0;
		mdescs[0].len        = (conventional_end - prog_start);
		mdescs[0].ptr        = MemBase + prog_start;
		mdescs[1].flags      = RETRO_MEMDESC_SYSTEM_RAM;
		mdescs[1].start      = 0x00100000;
		mdescs[1].len        = prog_start;
		mdescs[1].ptr        = MemBase;
		mdesc_expandedmem = &mdescs[2];
	}
	else
	{
		mdescs[0].flags      = RETRO_MEMDESC_SYSTEM_RAM;
		mdescs[0].start      = 0x00100000;
		mdescs[0].len        = conventional_end;
		mdescs[0].ptr        = MemBase;
		mdesc_expandedmem = &mdescs[1];
	}
	mdesc_expandedmem->flags = RETRO_MEMDESC_SYSTEM_RAM;
	mdesc_expandedmem->start = 0x00200000;
	mdesc_expandedmem->len   = memtotal - conventional_end;
	mdesc_expandedmem->ptr   = MemBase + conventional_end;

	#ifndef NDEBUG
	log_cb(RETRO_LOG_INFO, "[DOSBOX STATUS] ReportCoreMemoryMaps - Program: %s - Booted OS: %d - Program Memory: %d KB\n", RunningProgram, (int)booted_os, (mdescs[0].len / 1024));
	#endif

	struct retro_memory_map mmaps = { mdescs, (unsigned)(!booted_os ? 3 : 2) };
	environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);
	dbp_refresh_memmaps = false;
}

enum DBP_ThreadCtlMode { TCM_PAUSE_FRAME, TCM_ON_PAUSE_FRAME, TCM_RESUME_FRAME, TCM_FINISH_FRAME, TCM_ON_FINISH_FRAME, TCM_NEXT_FRAME, TCM_SHUTDOWN, TCM_ON_SHUTDOWN };
static void DBP_ThreadControl(DBP_ThreadCtlMode m)
{
	//#define TCMLOG(x, y)// printf("[%10u] [THREAD CONTROL] %20s %25s - STATE: %d - PENDING: %d - PAUSEEVT: %d - MIDFRAME: %d\n", (unsigned)(time_cb() - dbp_boot_time), x, y, (int)dbp_state, dbp_frame_pending, dbp_pause_events, dbp_paused_midframe);
	DBP_ASSERT(dbp_state != DBPSTATE_BOOT && dbp_state != DBPSTATE_SHUTDOWN);
	switch (m)
	{
		case TCM_PAUSE_FRAME:
			if (!dbp_frame_pending || dbp_pause_events) goto case_TCM_EMULATION_PAUSED;
			dbp_pause_events = true;
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDidPause.Wait(); dbp_wait_pause += (Bit32u)(time_cb() - t); }
			#else
			semDidPause.Wait();
			#endif
			dbp_pause_events = dbp_frame_pending = dbp_paused_midframe;
			goto case_TCM_EMULATION_PAUSED;
		case TCM_ON_PAUSE_FRAME:
			DBP_ASSERT(dbp_pause_events && !dbp_paused_midframe);
			dbp_paused_midframe = true;
			semDidPause.Post();
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDoContinue.Wait(); dbp_wait_paused += (Bit32u)(time_cb() - t); }
			#else
			semDoContinue.Wait();
			#endif
			dbp_paused_midframe = false;
			return;
		case TCM_RESUME_FRAME:
			if (!dbp_frame_pending) return;
			DBP_ASSERT(dbp_pause_events);
			dbp_pause_events = false;
			semDoContinue.Post();
			return;
		case TCM_FINISH_FRAME:
			if (!dbp_frame_pending) goto case_TCM_EMULATION_PAUSED;
			if (dbp_pause_events) DBP_ThreadControl(TCM_RESUME_FRAME);
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDidPause.Wait(); dbp_wait_finish += (Bit32u)(time_cb() - t); }
			#else
			semDidPause.Wait();
			#endif
			DBP_ASSERT(!dbp_paused_midframe);
			dbp_frame_pending = false;
			goto case_TCM_EMULATION_PAUSED;
		case TCM_ON_FINISH_FRAME:
			semDidPause.Post();
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDoContinue.Wait(); dbp_wait_continue += (Bit32u)(time_cb() - t); }
			#else
			semDoContinue.Wait();
			#endif
			return;
		case TCM_NEXT_FRAME:
			DBP_ASSERT(!dbp_frame_pending);
			if (dbp_state == DBPSTATE_EXITED) return;
			dbp_frame_pending = true;
			semDoContinue.Post();
			return;
		case TCM_SHUTDOWN:
			if (dbp_frame_pending)
			{
				dbp_pause_events = true;
				semDidPause.Wait();
				dbp_pause_events = dbp_frame_pending = false;
			}
			if (dbp_state == DBPSTATE_EXITED) return;
			DBP_DOSBOX_ForceShutdown();
			do
			{
				semDoContinue.Post();
				semDidPause.Wait();
			} while (dbp_state != DBPSTATE_EXITED);
			return;
		case TCM_ON_SHUTDOWN:
			dbp_state = DBPSTATE_EXITED;
			semDidPause.Post();
			return;
		case_TCM_EMULATION_PAUSED:
			if (dbp_refresh_memmaps) DBP_ReportCoreMemoryMaps();
	}
}

void DBP_SetRealModeCycles()
{
	if (cpu.pmode || CPU_CycleAutoAdjust || !(CPU_AutoDetermineMode & CPU_AUTODETERMINE_CYCLES) || render.frameskip.max > 1) return;

	int year = (dbp_game_running ? dbp_content_year : 0);
	CPU_CycleMax = 
		(year <= 1970 ?   3000 : // Unknown year, dosbox default
		(year <  1981 ?    500 : // Very early 8086/8088 CPU
		(year >  1999 ? 500000 : // Pentium III, 600 MHz and later
		Cycles1981to1999[year - 1981]))); // Matching speed for year

	// Switch to dynamic core for newer real mode games 
	if (CPU_CycleMax >= 8192 && (CPU_AutoDetermineMode & CPU_AUTODETERMINE_CORE))
	{
		#if (C_DYNAMIC_X86)
		if (cpudecoder != CPU_Core_Dyn_X86_Run) { void CPU_Core_Dyn_X86_Cache_Init(bool); CPU_Core_Dyn_X86_Cache_Init(true); cpudecoder = CPU_Core_Dyn_X86_Run; }
		#elif (C_DYNREC)
		if (cpudecoder != CPU_Core_Dynrec_Run)  { void CPU_Core_Dynrec_Cache_Init(bool);  CPU_Core_Dynrec_Cache_Init(true);  cpudecoder = CPU_Core_Dynrec_Run;  }
		#endif
	}
}

static bool DBP_NeedFrameSkip(bool in_emulation)
{
	if ((in_emulation ? (dbp_throttle.rate > render.src.fps - 1) : (render.src.fps > dbp_throttle.rate - 1))
		|| dbp_throttle.rate < 10 || dbp_latency == DBP_LATENCY_VARIABLE
		|| dbp_throttle.mode == RETRO_THROTTLE_FRAME_STEPPING || dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD
		|| dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION || dbp_throttle.mode == RETRO_THROTTLE_REWINDING) return false;
	static float accum;
	accum += (in_emulation ? (render.src.fps - dbp_throttle.rate) : (dbp_throttle.rate - render.src.fps));
	if (accum < dbp_throttle.rate) return false;
	//log_cb(RETRO_LOG_INFO, "%s AT %u\n", (in_emulation ? "[GFX_EndUpdate] EMULATING TWO FRAMES" : "[retro_run] SKIP EMULATING FRAME"), dbp_framecount);
	accum -= dbp_throttle.rate;
	return true;
}

bool DBP_Image_IsCD(const DBP_Image& image)
{
	const char* ext = (image.path.size() > 3 ? &*(image.path.end()-3) : NULL);
	return (ext && !((ext[1]|0x20) == 'm' || (ext[0]|0x20) == 'v'));
}

const char* DBP_Image_Label(const DBP_Image& image)
{
	return (image.longpath.length() ? image.longpath : image.path).c_str() + image.dirlen;
}

static unsigned DBP_AppendImage(const char* in_path, bool sorted)
{
	// insert into image list ordered alphabetically, ignore already known images
	unsigned insert_index;
	for (insert_index = 0; insert_index != (unsigned)dbp_images.size(); insert_index++)
	{
		if (dbp_images[insert_index].path == in_path) return insert_index;
		if (sorted && dbp_images[insert_index].path > in_path) { break; }
	}

	dbp_images.insert(dbp_images.begin() + insert_index, DBP_Image());
	DBP_Image& i = dbp_images[insert_index];
	i.path = in_path;

	for (char longname[256], *path = &i.path[0], *pRoot = (path[0] == '$' && i.path.length() > 4 && Drives[path[1]-'A'] ? path + 4 : NULL), *p = pRoot, *pNext; p; p = pNext)
	{
		if ((pNext = strchr(p, '\\')) != NULL) *pNext = '\0';
		if (Drives[path[1]-'A']->GetLongFileName(i.path.c_str() + 4, longname))
		{
			if (!i.longpath.length()) i.longpath.append(pRoot, (p - pRoot));
			i.longpath.append(longname);
		}
		else if (i.longpath.length())
		{
			i.longpath.append(p, ((pNext ? pNext : path + i.path.length()) - p));
		}
		if (pNext) { *(pNext++) = '\\'; if (i.longpath.length()) i.longpath += '\\'; }
	}

	const char* labelpath = (i.longpath.length() ? i.longpath : i.path).c_str();
	const char *lastSlash = strrchr(labelpath, '/'), *lastBackSlash = strrchr(labelpath, '\\');
	i.dirlen = (int)((lastSlash && lastSlash > lastBackSlash ? lastSlash + 1 : (lastBackSlash ? lastBackSlash + 1 : labelpath)) - labelpath);

	return insert_index;
}

static bool DBP_ExtractPathInfo(const char* path, const char ** out_path_file = NULL, size_t* out_namelen = NULL, const char ** out_ext = NULL, const char ** out_fragment = NULL, char* out_letter = NULL)
{
	if (!path || !*path) return false;
	// Skip any slashes at the very end of path, then find the next slash as the start of the loaded file/directory name
	const char *path_end = path + strlen(path), *path_file = path_end;
	for (bool had_other = false; path_file > path; path_file--)
	{
		bool is_slash = (path_file[-1] == '/' || path_file[-1] == '\\');
		if (is_slash) { if (had_other) break; path_end = path_file - 1; } else had_other = true;
	}
	const char *ext = strrchr(path_file, '.');
	if (ext) ext++; else ext = path_end;

	const char *fragment = strrchr(path_file, '#');
	if (fragment && ext > fragment) // check if 'FOO.ZIP#BAR.EXE'
	{
		const char* real_ext = fragment - (fragment - 3 > path_file && fragment[-3] == '.' ? 3 : 4);
		if (real_ext > path_file && *real_ext == '.') ext = real_ext+1;
		else fragment = NULL;
	}

	// A drive letter can be specified either by naming the mount file '.<letter>.<extension>' or by loading a path with an added '#<letter>' suffix.
	char letter = 0;
	const char *p_fra_drive = (fragment && fragment[1] && !fragment[2] ? fragment + 1 : NULL);
	const char *p_dot_drive = (ext - path_file > 3 && ext[-3] == '.' && !p_fra_drive ? ext - 2 : NULL);
	if      (p_fra_drive && (*p_fra_drive >= 'A' && *p_fra_drive <= 'Z')) letter = *p_fra_drive;
	else if (p_fra_drive && (*p_fra_drive >= 'a' && *p_fra_drive <= 'z')) letter = *p_fra_drive - 0x20;
	else if (p_dot_drive && (*p_dot_drive >= 'A' && *p_dot_drive <= 'Z')) letter = *p_dot_drive;
	else if (p_dot_drive && (*p_dot_drive >= 'a' && *p_dot_drive <= 'z')) letter = *p_dot_drive - 0x20;
	else p_dot_drive = NULL;

	if (out_path_file) *out_path_file = path_file;
	if (out_namelen) *out_namelen = (p_dot_drive ? p_dot_drive : ext) - (ext[-1] == '.' ? 1 : 0) - path_file;
	if (out_ext) *out_ext = ext;
	if (out_fragment) *out_fragment = fragment;
	if (out_letter) *out_letter = letter;
	return true;
}

static bool DBP_IsMounted(char drive)
{
	DBP_ASSERT(drive >= 'A' && drive <= 'Z');
	return Drives[drive-'A'] || (drive < 'A'+MAX_DISK_IMAGES && imageDiskList[drive-'A']);
}

static void DBP_Unmount(char drive)
{
	DBP_ASSERT(drive >= 'A' && drive <= 'Z');
	if (Drives[drive-'A'] && Drives[drive-'A']->UnMount() != 0) { DBP_ASSERT(false); return; }
	Drives[drive-'A'] = NULL;
	MSCDEX_RemoveDrive(drive);
	if (drive < 'A'+MAX_DISK_IMAGES)
		if (imageDisk*& dsk = imageDiskList[drive-'A'])
			{ delete dsk; dsk = NULL; }
	IDE_RefreshCDROMs();
	mem_writeb(Real2Phys(dos.tables.mediaid)+(drive-'A')*9,0);
	for (DBP_Image& i : dbp_images)
		if (i.mounted && i.drive == drive)
			i.mounted = false;
}

enum DBP_SaveFileType { SFT_GAMESAVE, SFT_VIRTUALDISK, SFT_DIFFDISK, _SFT_LAST_SAVE_DIRECTORY, SFT_SYSTEMDIR, SFT_NEWOSIMAGE };
static std::string DBP_GetSaveFile(DBP_SaveFileType type, const char** out_filename = NULL, Bit32u* out_diskhash = NULL)
{
	std::string res;
	const char *env_dir = NULL;
	if (environ_cb((type < _SFT_LAST_SAVE_DIRECTORY ? RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY : RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY), &env_dir) && env_dir)
		res.assign(env_dir) += CROSS_FILESPLIT;
	size_t dir_len = res.size();
	if (type < _SFT_LAST_SAVE_DIRECTORY)
	{
		res.append(dbp_content_name.empty() ? "DOSBox-pure" : dbp_content_name.c_str());
		if (type == SFT_GAMESAVE)
		{
			if (FILE* fSave = fopen_wrap(res.append(".pure.zip").c_str(), "rb")) { fclose(fSave); } // new save exists!
			else if (FILE* fSave = fopen_wrap(res.replace(res.length()-8, 3, "sav", 3).c_str(), "rb")) { dbp_legacy_save = true; fclose(fSave); }
			else res.replace(res.length()-8, 3, "pur", 3); // use new save
		}
		else if (type == SFT_VIRTUALDISK)
		{
			if (!dbp_vdisk_filter.Len())
			{
				dbp_vdisk_filter.Put("AUTOBOOT.DBP", (void*)(size_t)true);
				dbp_vdisk_filter.Put("PADMAP.DBP", (void*)(size_t)true);
				dbp_vdisk_filter.Put("DOSBOX~1.CON", (void*)(size_t)true);
				dbp_vdisk_filter.Put("DOSBOX.CON", (void*)(size_t)true);
				for (const DBP_Image& i : dbp_images) if (i.path[0] == '$' && i.path[1] == 'C')
					dbp_vdisk_filter.Put(&i.path[4], (void*)(size_t)true);
			}
			struct Local { static void FileHash(const char* path, bool, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
			{
				size_t pathlen = strlen(path);
				if (pathlen >= 2 && path[pathlen-1] == '.' && (path[pathlen-2] == '.' || path[pathlen-2] == '\\')) return; // skip . and ..
				if (dbp_vdisk_filter.Get(path)) return;
				if (pathlen > 4 && !memcmp(&path[pathlen-4], ".SKC", 4)) { dbp_vdisk_filter.Put(path, (void*)(size_t)true); return; } // remove ZIP seek caches
				Bit32u& hash = *(Bit32u*)data;
				Bit8u arr[] = { (Bit8u)(size>>24), (Bit8u)(size>>16), (Bit8u)(size>>8), (Bit8u)(size), (Bit8u)(date>>8), (Bit8u)(date), (Bit8u)(time>>8), (Bit8u)(time), attr };
				hash = DriveCalculateCRC32(arr, sizeof(arr), DriveCalculateCRC32((const Bit8u*)path, pathlen, hash));
			}};
			Bit32u hash = (Bit32u)(0x11111111 - 1024) + (Bit32u)atoi(retro_get_variable("dosbox_pure_bootos_dfreespace", "1024"));
			DriveFileIterator(Drives['C'-'A'], Local::FileHash, (Bitu)&hash);
			res.resize(res.size() + 32);
			res.resize(res.size() - 32 + sprintf(&res[res.size() - 32], (hash == 0x11111111 ? ".sav" : "-%08X.sav"), hash));
			if (out_diskhash) *out_diskhash = hash;
		}
		else if (type == SFT_DIFFDISK)
		{
			res.append("-CDRIVE.sav");
		}
	}
	else if (type == SFT_NEWOSIMAGE)
	{
		res.append(!dbp_content_name.empty() ? dbp_content_name.c_str() : "Installed OS").append(".img");
		size_t num = 1, baselen = res.size() - 4;
		while (FILE* f = fopen_wrap(res.c_str(), "rb"))
		{
			fclose(f);
			res.resize(baselen + 16);
			res.resize(baselen + sprintf(&res[baselen], " (%d).img", (int)++num));
		}
	}
	if (out_filename) *out_filename = res.c_str() + dir_len;
	return std::move(res);
}

static void DBP_SetDriveLabelFromContentPath(DOS_Drive* drive, const char *path, char letter = 'C', const char *path_file = NULL, const char *ext = NULL, bool forceAppendExtension = false)
{
	// Use content filename as drive label, cut off at file extension, the first occurence of a ( or [ character or right white space.
	if (!path_file && !DBP_ExtractPathInfo(path, &path_file, NULL, &ext)) return;
	char lbl[11+1], *lblend = lbl + (ext - path_file > 11 ? 11 : ext - (*ext ? 1 : 0) - path_file);
	memcpy(lbl, path_file, lblend - lbl);
	for (char* c = lblend; c > lbl; c--) { if (c == lblend || *c == '(' || *c == '[' || (*c <= ' ' && !c[1])) { *c = '\0'; lblend = c; } }
	if (forceAppendExtension && ext && ext[0])
	{
		lblend = (lblend > lbl + 11 - 4 ? lbl + 11 - 4 : lblend);
		*lblend = '-';
		safe_strncpy(lblend + 1, ext, (lbl+11-lblend));
	}
	drive->label.SetLabel(lbl, (letter > 'C'), true);
}

static DOS_Drive* DBP_Mount(unsigned image_index = 0, bool unmount_existing = false, char remount_letter = 0, const char* boot = NULL)
{
	const char *path = (boot ? boot : dbp_images[image_index].path.c_str()), *path_file, *ext, *fragment; char letter;
	if (!DBP_ExtractPathInfo(path, &path_file, NULL, &ext, &fragment, &letter)) return NULL;
	if (remount_letter) letter = remount_letter;

	std::string path_no_fragment;
	if (fragment)
	{
		path_no_fragment = std::string(path, fragment - path);
		ext       = path_no_fragment.c_str() + (ext       - path);
		path_file = path_no_fragment.c_str() + (path_file - path);
		path      = path_no_fragment.c_str();
	}

	DOS_Drive* drive = NULL;
	imageDisk* disk = NULL;
	CDROM_Interface* cdrom = NULL;
	Bit8u media_byte = 0;
	const char* error_type = "content";
	if (!strcasecmp(ext, "ZIP") || !strcasecmp(ext, "DOSZ"))
	{
		if (!letter) letter = (boot ? 'C' : 'D');
		if (!unmount_existing && Drives[letter-'A']) return NULL;
		FILE* zip_file_h = fopen_wrap(path, "rb");
		if (!zip_file_h)
		{
			error_type = "ZIP";
			goto TRY_DIRECTORY;
		}
		drive = new zipDrive(new rawFile(zip_file_h, false), dbp_strict_mode, dbp_legacy_save);
		DBP_SetDriveLabelFromContentPath(drive, path, letter, path_file, ext);
		if (ext[3] == 'Z' || ext[3] == 'z')
		{
			// Load .DOSC patch file overlay for .DOSZ
			if (path_no_fragment.empty()) path_no_fragment.append(path);
			path_no_fragment.back() = (ext[3] == 'Z' ? 'C' : 'c');
			FILE* c_file_h = fopen_wrap(path_no_fragment.c_str(), "rb");
			if (c_file_h)
				drive = new patchDrive(drive, true, new rawFile(c_file_h, false), dbp_strict_mode);
		}
		if (boot && letter == 'C') return drive;
	}
	else if (!strcasecmp(ext, "IMG") || !strcasecmp(ext, "IMA") || !strcasecmp(ext, "VHD") || !strcasecmp(ext, "JRC") || !strcasecmp(ext, "TC"))
	{
		fatDrive* fat = new fatDrive(path, 512, 0, 0, 0, 0);
		if (!fat->loadedDisk || (!fat->created_successfully && letter >= 'A'+MAX_DISK_IMAGES))
		{
			FAT_TRY_ISO:
			delete fat;
			goto MOUNT_ISO;
		}
		else if (!fat->created_successfully)
		{
			// Check if ISO (based on CDROM_Interface_Image::LoadIsoFile/CDROM_Interface_Image::CanReadPVD)
			static const Bit32u pvdoffsets[] = { 32768, 32768+8, 37400, 37400+8, 37648, 37656, 37656+8 };
			for (Bit32u pvdoffset : pvdoffsets)
			{
				Bit8u pvd[8];
				if (fat->loadedDisk->Read_Raw(pvd, pvdoffset, 8) == 8 && (!memcmp(pvd, "\1CD001\1", 7) || !memcmp(pvd, "\1CDROM\1", 7)))
					goto FAT_TRY_ISO;
			}
			// Neither FAT nor ISO, just register with BIOS/CMOS for raw sector access and set media table byte
			disk = fat->loadedDisk;
			fat->loadedDisk = NULL; // don't want it deleted by ~fatDrive
			delete fat;
		}
		else
		{
			// Copied behavior from IMGMOUNT::Run, force obtaining the label and saving it in label
			RealPt save_dta = dos.dta();
			dos.dta(dos.tables.tempdta);
			DOS_DTA dta(dos.dta());
			dta.SetupSearch(255, DOS_ATTR_VOLUME, (char*)"*.*");
			fat->FindFirst((char*)"", dta);
			dos.dta(save_dta);

			drive = fat;
			disk = fat->loadedDisk;

			if (boot && disk->hardDrive && (!letter || letter == 'C'))
			{
				// also register with BIOS/CMOS to make this image bootable (make sure it's its own instance as we pass this one to be owned by unionDrive)
				fatDrive* fat2 = new fatDrive(path, 512, 0, 0, 0, 0);
				imageDiskList['C'-'A'] = fat2->loadedDisk;
				fat2->loadedDisk = NULL; // don't want it deleted by ~fatDrive
				delete fat2;
				return fat;
			}
		}
		if (!letter) letter = (disk->hardDrive ? 'D' : 'A');
		media_byte = (disk->hardDrive ? 0xF8 : (disk->active ? disk->GetBiosType() : 0));
	}
	else if (!strcasecmp(ext, "ISO") || !strcasecmp(ext, "CHD") || !strcasecmp(ext, "CUE") || !strcasecmp(ext, "INS"))
	{
		MOUNT_ISO:
		if (letter < 'D') letter = 'D';
		if (!unmount_existing && Drives[letter-'A']) return NULL;
		if (DBP_IsMounted(letter)) DBP_Unmount(letter); // needs to be done before constructing isoDrive as it registers itself with MSCDEX overwriting the current drives registration
		int error = -1;
		isoDrive* iso = new isoDrive(letter, path, 0xF8, error);
		if (error)
		{
			delete iso;
			error_type = "CD-ROM image";
			goto TRY_DIRECTORY;
		}
		cdrom = iso->GetInterface();
		drive = iso;
	}
	else if (!strcasecmp(ext, "M3U") || !strcasecmp(ext, "M3U8"))
	{
		FILE* m3u_file_h = fopen_wrap(path, "rb");
		if (!m3u_file_h)
		{
			error_type = "M3U";
			goto TRY_DIRECTORY;
		}
		fseek(m3u_file_h, 0, SEEK_END);
		size_t m3u_file_size = ftell(m3u_file_h);
		fseek(m3u_file_h, 0, SEEK_SET);
		char* m3u = new char[m3u_file_size + 1];
		if (!fread(m3u, m3u_file_size, 1, m3u_file_h)) { DBP_ASSERT(0); }
		fclose(m3u_file_h);
		m3u[m3u_file_size] = '\0';
		for (char* p = m3u, *pEnd = p + m3u_file_size; p <= pEnd; p++)
		{
			if (*p <= ' ') continue;
			char* m3u_line = (*p == '#' ? NULL : p);
			while (*p != '\0' && *p != '\r' && *p != '\n') p++;
			*p = '\0';
			if (!m3u_line) continue;
			size_t m3u_baselen = (m3u_line[0] == '\\' || m3u_line[0] == '/' || m3u_line[1] == ':' ? 0 : path_file - path);
			std::string m3u_path = std::string(path, m3u_baselen) + m3u_line;
			DBP_AppendImage(m3u_path.c_str(), false);
		}
		delete [] m3u;
		return NULL;
	}
	else // path or executable file
	{
		TRY_DIRECTORY:
		if (!letter) letter = (boot ? 'C' : 'D');
		if (!unmount_existing && Drives[letter-'A']) return NULL;
		bool isDir = (strcasecmp(ext, "EXE") && strcasecmp(ext, "COM") && strcasecmp(ext, "BAT") && strcasecmp(ext, "conf"));

		std::string dir;
		if (isDir) { char c = dir.assign(path).back(); if (c != '/' && c != '\\') dir += '/'; } // must end with slash
		else if (path_file != path) dir.assign(path, path_file - path);
		else dir.assign("./");
		strreplace((char*)dir.c_str(), (CROSS_FILESPLIT == '\\' ? '/' : '\\'), CROSS_FILESPLIT); // required by localDrive

		dir_information* dirp = open_directory(dir.c_str());
		if (!dirp)
		{
			retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s%s", error_type, path, "");
			return NULL;
		}
		close_directory(dirp);

		localDrive* localdrive = new localDrive(dir.c_str(), 512, 32, 32765, 16000, 0xF8);
		DBP_SetDriveLabelFromContentPath(localdrive, path, letter, path_file, ext);
		if (!isDir && (ext[2]|0x20) == 'n') dbp_conf_loading = 'o'; // conf loading mode set to 'o'utside will load the requested .conf file
		drive = localdrive;
		path = NULL; // don't treat as disk image, but always register with Drives even if is_boot
	}

	if (DBP_IsMounted(letter))
	{
		if (!unmount_existing)
		{
			if (drive) delete drive; // also deletes disk
			else if (disk) delete disk;
			return NULL;
		}
		DBP_Unmount(letter);
	}

	// Register emulated drive
	Drives[letter-'A'] = drive;

	// Write media bytes to dos table
	if (!media_byte) media_byte = (letter < 'C' ? 0xF0 : (drive ? drive->GetMediaByte() : 0xF8)); // F0 = floppy, F8 = hard disk
	mem_writeb(Real2Phys(dos.tables.mediaid) + (letter-'A') * 9, media_byte);

	// Register virtual drives with MSCDEX
	bool attachedVirtualDrive = (letter > 'C' && !disk && !cdrom);
	if (attachedVirtualDrive)
	{
		Bit8u subUnit;
		MSCDEX_AddDrive(letter, "", subUnit);
	}

	// Register CDROM with IDE controller only when running with 32MB or more RAM (used when booting an operating system)
	if (cdrom && (MEM_TotalPages() / 256) >= 32)
	{
		IDE_RefreshCDROMs();
	}

	// Register with BIOS/CMOS/IDE controller
	if (disk && letter < 'A'+MAX_DISK_IMAGES)
	{
		imageDiskList[letter-'A'] = disk;
		dbp_images[image_index].image_disk = true;
	}

	if (path)
	{
		if (boot) image_index = DBP_AppendImage(path, false);
		dbp_images[image_index].mounted = true;
		dbp_images[image_index].drive = letter;
		dbp_image_index = image_index;
	}
	return NULL;
}

static void DBP_Remount(char drive1, char drive2)
{
	if (!DBP_IsMounted(drive1) || DBP_IsMounted(drive2)) return;

	DBP_Image* img = NULL;
	for (DBP_Image& i : dbp_images) { if (i.mounted && i.drive == drive1) { img = &i; break; } }
	if (img)
	{
		DBP_Unmount(drive1);
		DBP_Mount((unsigned)(img - &dbp_images[0]), false, drive2);
	}
	else
	{
		// Swap registration with BIOS/CMOS
		if (imageDisk*& dsk = imageDiskList[drive1-'A'])
		{
			if (drive2 < 'A'+MAX_DISK_IMAGES) imageDiskList[drive2-'A'] = dsk;
			else if (!dynamic_cast<fatDrive*>(Drives[drive1-'A'])) delete dsk;
			dsk = NULL;
		}

		// Swap media bytes in dos table
		mem_writeb(Real2Phys(dos.tables.mediaid) + (drive2-'A') * 9, mem_readb(Real2Phys(dos.tables.mediaid) + (drive1-'A') * 9));
		mem_writeb(Real2Phys(dos.tables.mediaid) + (drive1-'A') * 9, 0);

		// Swap emulated drives
		Drives[drive2-'A'] = Drives[drive1-'A'];
		Drives[drive1-'A'] = NULL;

		// Handle MSCDEX (using path "" can only be used for virtual drives and not image file based drives)
		if (MSCDEX_RemoveDrive(drive1)) { Bit8u subUnit; MSCDEX_AddDrive(drive2, "", subUnit); }
	}

	// If the currently running batch file is placed on the remounted drive, make sure it now points to the new drive
	if (BatchFile* bf = first_shell->bf)
		if (bf->filename.length() > 2 && bf->filename[0] == drive1 && bf->filename[1] == ':')
			bf->filename[0] = drive2;

	if (DOS_GetDefaultDrive() == drive1-'A') DOS_SetDrive(drive2-'A');

	for (DBP_Image& i : dbp_images)
	{
		if (i.path[0] == '$' && i.path[1] == drive1) i.path[1] = drive2;
		if (i.mounted && i.drive == drive1) i.drive = drive2;
	}
}

struct DBP_PadMapping
{
	enum { DBP_PADMAP_MAXSIZE_PORT = (1 + (16 * (1 + 4)) + (4 * (1 + 8))), DBP_PADMAP_MAXSIZE_TOTAL = DBP_MAX_PORTS * DBP_PADMAP_MAXSIZE_PORT };
	enum EPreset : Bit8u { PRESET_NONE, PRESET_AUTOMAPPED, PRESET_GENERICKEYBOARD, PRESET_MOUSE_LEFT_ANALOG, PRESET_MOUSE_RIGHT_ANALOG, PRESET_GRAVIS_GAMEPAD, PRESET_BASIC_JOYSTICK_1, PRESET_BASIC_JOYSTICK_2, PRESET_THRUSTMASTER_FLIGHTSTICK, PRESET_BOTH_DOS_JOYSTICKS, PRESET_CUSTOM };
	enum EPortMode : Bit8u { MODE_DISABLED, MODE_MAPPER, MODE_PRESET_AUTOMAPPED, MODE_PRESET_GENERICKEYBOARD, MODE_PRESET_LAST = MODE_PRESET_AUTOMAPPED + (PRESET_CUSTOM - PRESET_AUTOMAPPED) - 1, MODE_KEYBOARD, MODE_KEYBOARD_MOUSE1, MODE_KEYBOARD_MOUSE2 };

	INLINE static EPreset DefaultPreset(Bit8u port) { return ((port || !dbp_auto_mapping) ? PRESET_GENERICKEYBOARD : PRESET_AUTOMAPPED); }
	INLINE static bool IsCustomized(Bit8u port) { return (dbp_port_mode[port] == MODE_MAPPER && GetPreset(port, DefaultPreset(port)) == PRESET_CUSTOM); }
	INLINE static const char* GetPortPresetName(Bit8u port) { return GetPresetName(GetPreset(port)); }
	INLINE static void FillGenericKeys(Bit8u port) { Apply(port, PresetBinds(PRESET_GENERICKEYBOARD, port), true, true); }
	INLINE static const char* GetKeyAutoMapButtonLabel(Bit8u key) { return FindAutoMapButtonLabel(1, 0, &key); }

	static void Load()
	{
		DOS_File *padmap = nullptr;
		if (!Drives['C'-'A'] || !Drives['C'-'A']->FileOpen(&padmap, (char*)"PADMAP.DBP", OPEN_READ))
			return;

		dbp_custom_mapping.resize(DBP_PADMAP_MAXSIZE_TOTAL);
		Bit8u version;
		Bit16u version_length = sizeof(version), padmap_length = DBP_PADMAP_MAXSIZE_TOTAL;
		padmap->AddRef();
		padmap->Read(&version, &version_length);
		padmap->Read(&dbp_custom_mapping[0], &padmap_length);
		if (!version_length || version != 0 || !padmap_length)
		{
			retro_notify(0, RETRO_LOG_ERROR, "Corrupt gamepad mapping data in %c:\\%s", 'C', "PADMAP.DBP");
			DBP_ASSERT(0);
			dbp_custom_mapping.clear();
		}
		dbp_custom_mapping.resize(padmap_length);
		padmap->Close();
		delete padmap;
	}

	static void Save()
	{
		Bit8u last_port = DBP_MAX_PORTS - 1;
		for (; last_port != 0xFF && !IsCustomized(last_port); last_port--) {}
		if (last_port == 0xFF)
		{
			if (Drives['C'-'A']) Drives['C'-'A']->FileUnlink((char*)"PADMAP.DBP");
			dbp_custom_mapping.clear();
			GenerateBindings();
		}
		else
		{
			RefreshDosJoysticks(); // do after bindings change
			dbp_custom_mapping.resize(DBP_PADMAP_MAXSIZE_PORT * (last_port + 1));
			Bit8u *data = &dbp_custom_mapping[0], *pBind = data, *pEnd = data, *pCount;
			for (Bit8u port = 0; port <= last_port; port++)
			{
				*(pCount = pBind++) = 0;
				for (Bit8u btn_id = 0; btn_id != 20; btn_id++)
				{
					Bit8u isAnalog = (btn_id >= 16), key_count;
					if ((key_count = FillBinds(pBind+1, PortDeviceIndexIdForBtn(port, btn_id), isAnalog)) == 0) continue;
					*pBind = btn_id | (Bit8u)((key_count - 1)<<6);
					pEnd = (pBind += 1 + key_count * (isAnalog ? 2 : 1));
					(*pCount)++;
				}
			}
			dbp_custom_mapping.resize(pEnd - data);

			DOS_File *padmap = nullptr;
			if (!Drives['C'-'A'] || !Drives['C'-'A']->FileCreate(&padmap, (char*)"PADMAP.DBP", DOS_ATTR_ARCHIVE))
			{
				retro_notify(0, RETRO_LOG_ERROR, "Unable to write gamepad mapping data %c:\\%s", 'C', "PADMAP.DBP");
				DBP_ASSERT(0);
				return;
			}
			Bit8u version = 0;
			Bit16u version_length = sizeof(version), padmap_length = (Bit16u)(pEnd - data);
			padmap->AddRef();
			padmap->Write(&version, &version_length);
			padmap->Write(data, &padmap_length);
			padmap->Close();
			delete padmap;
		}
		dbp_input_binds_modified = true;
	}

	static void AssignBindEvent(DBP_InputBind& b, Bit8u bind_part, Bit8u bind_key)
	{
		if (bind_key == 0) // deleting entry
		{
			dbp_input_binds.erase(dbp_input_binds.begin() + (&b - &dbp_input_binds[0]));
		}
		else if (b.device == RETRO_DEVICE_ANALOG) // Binding to an axis
		{
			Bit16s oldmeta = ((b.evt != DBPET_AXISMAPPAIR && b.evt != _DBPET_MAX) ? GetAxisSpecialMappingMeta(b.evt) : b.meta);
			Bit8u other_key = DBP_MAPPAIR_GET((bind_part ? -1 : 1), oldmeta);
			SetBindMetaFromPair(b, (bind_part ? other_key : bind_key), (bind_part ? bind_key : other_key));
		}
		else if (bind_key < DBP_SPECIALMAPPINGS_KEY) // Binding a key to a joypad button
		{
			b.evt = DBPET_KEYDOWN;
			b.meta = (Bit16s)bind_key;
		}
		else // Binding a special mapping to a joypad button
		{
			b.evt = DBP_SPECIALMAPPING(bind_key).evt;
			b.meta = DBP_SPECIALMAPPING(bind_key).meta;
		}
	}

	static const char* GetPresetName(EPreset preset)
	{
		static const char* presets[] = { "Generic Keyboard", "Mouse w/ Left Analog", "Mouse w/ Right Analog", "Gravis Gamepad (4 Buttons)", "First 2 Button Joystick", "Second 2 Button Joystick", "Thrustmaster Flight Stick", "Both DOS Joysticks", "Custom Mapping" };
		return (preset == PRESET_AUTOMAPPED ? dbp_auto_mapping_title : presets[preset - 2]);
	}

	static void SetPreset(Bit8u port, EPreset preset)
	{
		for (size_t i = dbp_input_binds.size(); i--;) { if (dbp_input_binds[i].port == port) { dbp_input_binds.erase(dbp_input_binds.begin() + i); } }
		Apply(port, PresetBinds(preset, port), true);
	}

	static EPreset GetPreset(Bit8u port, EPreset check_one = PRESET_NONE)
	{
		const Bit8u* checkPresets[PRESET_CUSTOM];
		int nBegin = (check_one ? check_one : PRESET_AUTOMAPPED + (dbp_auto_mapping ? 0 : 1)), nEnd = (check_one ? check_one + 1 : PRESET_CUSTOM);
		for (int n = nBegin; n != nEnd; n++) checkPresets[n] = PresetBinds((EPreset)n, port);

		for (Bit8u btn_id = 0; btn_id != 20; btn_id++)
		{
			Bit8u bind_buf[4*2], bind_analog = (btn_id >= 16), bind_count;
			bind_count = FillBinds(bind_buf, PortDeviceIndexIdForBtn(port, btn_id), bind_analog);

			if (btn_id == RETRO_DEVICE_ID_JOYPAD_L3 && port == 0 && dbp_on_screen_keyboard && bind_buf[0] == 225 && bind_count == 1) continue; // skip OSK bind
			bool oskshift = (btn_id == RETRO_DEVICE_ID_JOYPAD_R3 && port == 0 && dbp_on_screen_keyboard); // handle shifting due to OSK with generic keyboard

			for (int n = nBegin; n != nEnd; n++)
			{
				if (!checkPresets[n]) { if (n == nBegin) nBegin++; continue; }
				Bit8u match_id = (!oskshift || n != PRESET_GENERICKEYBOARD ? btn_id : RETRO_DEVICE_ID_JOYPAD_L3);
				Bit8u num = 0, p_val, p_id, p_analog, hasActionName, p_count, match = (bind_count == 0);
				for (const Bit8u count = *checkPresets[n], *p = checkPresets[n] + 1; num != count; p += p_count * (p_analog ? 2 : 1), num++)
				{
					p_val = *(p++), p_id = (Bit8u)(p_val & 31), p_analog = (p_id >= 16), hasActionName = !!(p_val & 32), p_count = 1 + (p_val>>6);
					if (hasActionName) while (*(p++) & 128);
					if (p_id != match_id) continue;
					match = (p_count == bind_count && !memcmp(p, bind_buf, p_count * (p_analog ? 2 : 1)));
					if (!match) checkPresets[n] = NULL;
					break;
				}
				if (check_one && !match) return PRESET_CUSTOM;
			}
		}

		for (int n = nBegin; n != nEnd; n++) if (checkPresets[n]) return (EPreset)n;
		return PRESET_CUSTOM;
	}

	static const char* GetBoundAutoMapButtonLabel(Bit32u port_device_index_id, Bit8u isAnalog)
	{
		if (!dbp_auto_mapping || !dbp_auto_mapping_names) return NULL;
		Bit8u bind_buf[4*2], bind_count = FillBinds(bind_buf, port_device_index_id, isAnalog);
		return FindAutoMapButtonLabel(bind_count, isAnalog, bind_buf);
	}

	static void SetPortMode(unsigned port, unsigned device)
	{
		Bit8u devtype = (Bit8u)(device & RETRO_DEVICE_MASK), subclass = (Bit8u)((device >> RETRO_DEVICE_TYPE_SHIFT) - 1), mode = MODE_DISABLED;
		bool is_joy = (devtype == RETRO_DEVICE_JOYPAD || devtype == RETRO_DEVICE_ANALOG), is_key = (devtype == RETRO_DEVICE_KEYBOARD);
		if      (is_joy && subclass == 99 && dbp_auto_mapping) mode = MODE_PRESET_AUTOMAPPED;
		else if (is_joy && subclass < (PRESET_CUSTOM - PRESET_GENERICKEYBOARD)) mode = MODE_PRESET_GENERICKEYBOARD + subclass;
		else if (is_joy) mode = MODE_MAPPER;
		else if (is_key) mode = (subclass == 1 ? MODE_KEYBOARD_MOUSE1 : subclass == 2 ? MODE_KEYBOARD_MOUSE2 : MODE_KEYBOARD);
		if (port >= DBP_MAX_PORTS || dbp_port_mode[port] == mode) return;
		dbp_port_mode[port] = mode;
		if (dbp_state <= DBPSTATE_SHUTDOWN) return;
		if (mode != MODE_DISABLED) RefreshInputBinds(true); else ClearBinds((Bit8u)port);
	}

	static void RefreshInputBinds(bool regenerate_bindings)
	{
		dbp_input_binds_modified = false;
		if (regenerate_bindings) GenerateBindings();

		static std::vector<std::string> input_names;
		input_names.reserve(dbp_input_binds.size() + DBP_MAX_PORTS); // otherwise strings move their data in memory when the vector reallocates
		input_names.clear();
		std::vector<retro_input_descriptor> input_descriptor;
		for (DBP_InputBind *b = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *bEnd = b + dbp_input_binds.size(), *prev = NULL; b != bEnd; prev = b++)
			if (b->device != RETRO_DEVICE_MOUSE && (!prev || PORT_DEVICE_INDEX_ID(*prev) != PORT_DEVICE_INDEX_ID(*b)))
				if (const char* desc = GenerateDesc(input_names, PORT_DEVICE_INDEX_ID(*b), b->device == RETRO_DEVICE_ANALOG))
					input_descriptor.push_back( { b->port, b->device, b->index, b->id, desc } );
		input_descriptor.push_back( { 0 } );
		environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &input_descriptor[0]);

		enum { TYPES_COUNT = 2 + (PRESET_CUSTOM - PRESET_AUTOMAPPED) + 3 };
		static retro_controller_info ports[DBP_MAX_PORTS+1];
		static retro_controller_description descs[DBP_MAX_PORTS*TYPES_COUNT];
		for (Bit8u port = 0; port != DBP_MAX_PORTS; port++)
		{
			if (dbp_port_mode[port] == MODE_MAPPER) { input_names.push_back("[Pad Mapper] "); input_names.back().append(GetPortPresetName(port)); }

			retro_controller_description *types = descs + port * TYPES_COUNT, *type = types;
			*(type++) = { "Disabled", (unsigned)RETRO_DEVICE_NONE };
			*(type++) = { (dbp_port_mode[port] == MODE_MAPPER ? input_names.back().c_str() : "Use Gamepad Mapper"), (unsigned)RETRO_DEVICE_JOYPAD };
			if (dbp_auto_mapping) *(type++) = { dbp_auto_mapping_title, (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 99) };
			for (int i = PRESET_GENERICKEYBOARD; i != PRESET_CUSTOM; i++)
				*(type++) = { GetPresetName((EPreset)i), (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, i - PRESET_GENERICKEYBOARD) };
			*(type++) = { "Custom Keyboard Bindings", (unsigned)RETRO_DEVICE_KEYBOARD };
			*(type++) = { "Custom Keyboard + Mouse on Left Stick and B/A/X",  (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 1) };
			*(type++) = { "Custom Keyboard + Mouse on Right Stick and L/R/X", (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 2) };

			ports[port].num_types = (unsigned)(type - types);
			ports[port].types = types;
		}
		ports[DBP_MAX_PORTS] = {0};
		environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
	}

	static void RefreshDosJoysticks()
	{
		// Enable DOS joysticks only when mapped
		// This helps for games which by default react to the joystick without calibration
		// This can cause problems in other games that expect the joystick to respond (but hopefully these games have a setup program that can disable that)
		bool useJoy1 = false, useJoy2 = false, useAnalogButtons = false;
		for (DBP_InputBind *b = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *bEnd = b + dbp_input_binds.size(); b != bEnd; b++)
			for (Bit16s bevt = b->evt, dir = 1;; dir -= 2)
			{
				Bit16s map = DBP_MAPPAIR_GET(dir, b->meta), evt = ((map >= DBP_SPECIALMAPPINGS_KEY && bevt == DBPET_AXISMAPPAIR) ? DBP_SPECIALMAPPING(map).evt : bevt);
				useJoy1 |= (evt == DBPET_JOY1X || evt == DBPET_JOY1Y || evt == DBPET_JOY1DOWN);
				useJoy2 |= (evt == DBPET_JOY2X || evt == DBPET_JOY2Y || evt == DBPET_JOY2DOWN || evt == DBPET_JOYHATSETBIT);
				useAnalogButtons |= (evt <= _DBPET_JOY_AXIS_MAX && b->device == RETRO_DEVICE_JOYPAD);
				if (bevt != DBPET_AXISMAPPAIR || dir < 0) break;
			}
		JOYSTICK_Enable(0, useJoy1);
		JOYSTICK_Enable(1, useJoy2);
		dbp_analog_buttons = useAnalogButtons;
	}

	static int InsertBind(const DBP_InputBind& b)
	{
		const DBP_InputBind *pEnd = (dbp_input_binds.size() ? &dbp_input_binds[0]-1 : NULL), *pBegin = pEnd + dbp_input_binds.size(), *p = pBegin;
		const Bit32u b_sort_key = (Bit32u)((b.port<<24)|(b.device<<16)|(b.index<<8)|(b.id));
		for (; p != pEnd; p--)
			if (p->device == RETRO_DEVICE_MOUSE || (Bit32u)((p->port<<24)|(p->device<<16)|(p->index<<8)|(p->id)) <= b_sort_key)
				break;
		int insert_idx = (int)(p - pEnd);
		dbp_input_binds.insert(dbp_input_binds.begin() + insert_idx, b);
		return insert_idx;
	}

private:
	static void GenerateBindings()
	{
		dbp_input_binds.clear();
		if (dbp_mouse_input != 'f')
		{
			if (dbp_mouse_input != 'p')
			{
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   DBPET_MOUSEDOWN, 0 });
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  DBPET_MOUSEDOWN, 1 });
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, DBPET_MOUSEDOWN, 2 });
			}
			if (dbp_bind_mousewheel)
			{
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,   DBPET_KEYDOWN, DBP_MAPPAIR_GET(-1, dbp_bind_mousewheel) });
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN, DBPET_KEYDOWN, DBP_MAPPAIR_GET( 1, dbp_bind_mousewheel) });
			}
		}
		const Bit8u* mapping = (!dbp_custom_mapping.empty() ? &dbp_custom_mapping[0] : NULL), *mapping_end = mapping + dbp_custom_mapping.size();
		for (Bit8u port = 0; port != DBP_MAX_PORTS; port++)
		{
			if (dbp_port_mode[port] == MODE_MAPPER)
			{
				if (mapping && mapping < mapping_end) mapping = Apply(port, mapping, false);
				else if (port == 0 && dbp_auto_mapping) Apply(port, dbp_auto_mapping, true);
				else Apply(port, PresetBinds(PRESET_GENERICKEYBOARD, port), true);
			}
			else
			{
				if (mapping && mapping < mapping_end) mapping = SkipMapping(mapping);
				bool preset_mode = (dbp_port_mode[port] >= MODE_PRESET_AUTOMAPPED && dbp_port_mode[port] <= MODE_PRESET_LAST), bind_osd = (dbp_port_mode[port] != MODE_DISABLED);
				EPreset preset = (preset_mode) ? (EPreset)(PRESET_AUTOMAPPED + (dbp_port_mode[port] - MODE_PRESET_AUTOMAPPED))
				               : (dbp_port_mode[port] == MODE_KEYBOARD_MOUSE1) ? PRESET_MOUSE_LEFT_ANALOG
				               : (dbp_port_mode[port] == MODE_KEYBOARD_MOUSE2) ? PRESET_MOUSE_RIGHT_ANALOG
				               : PRESET_NONE;
				if (bind_osd) Apply(port, PresetBinds(preset, port), true);
				if (preset_mode) FillGenericKeys(port);
			}
		}
		RefreshDosJoysticks(); // do after bindings change
	}

	static void ClearBinds(Bit8u port)
	{
		DBP_InputBind *binds = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *b = binds, *bEnd = b + dbp_input_binds.size(), *shift = b;
		for (; b != bEnd; b++, shift++) { if (b->port == port) shift--; else if (shift != b) *shift = *b; }
		if (shift != b) dbp_input_binds.resize(shift - binds);
	}

	static const char* GenerateDesc(std::vector<std::string>& input_names, Bit32u port_device_index_id, Bit8u isAnalog)
	{
		input_names.emplace_back();
		std::string& name = input_names.back();

		Bit8u bind_buf[4*2], bind_count = FillBinds(bind_buf, port_device_index_id, isAnalog), *p = bind_buf;
		const char* amn = FindAutoMapButtonLabel(bind_count, isAnalog, bind_buf);
		if (amn) ((name = amn) += ' ') += '(';

		for (const char *desc_lastdev = NULL; bind_count--;)
		{
			for (int i = 0; i <= (int)isAnalog; i++)
			{
				if (i) name += '/';
				Bit8u k =  *(p++);
				const char *desc_dev = DBP_GETKEYDEVNAME(k);
				if (desc_lastdev != desc_dev) { if (desc_dev) (name += desc_dev) += ' ';  desc_lastdev = desc_dev; }
				name += DBP_GETKEYNAME(k);
			}
			if (bind_count) name += '+';
		}
		if (amn) name += ')';
		return name.c_str();
	}

	#define DBP_ANALOGBINDID2(INDEX,ID) (16+((INDEX)*2)+(ID))
	#define DBP_ANALOGBINDID(SIDE,AXIS) DBP_ANALOGBINDID2(RETRO_DEVICE_INDEX_ANALOG_##SIDE, RETRO_DEVICE_ID_ANALOG_##AXIS)

	INLINE static DBP_InputBind BindForBtn(Bit8u port, Bit8u id) { if (id < 16) return { port, RETRO_DEVICE_JOYPAD, 0, id }; else return { port, RETRO_DEVICE_ANALOG, (Bit8u)(id >= 18), (Bit8u)(id & 1) }; }
	INLINE static Bit32u PortDeviceIndexIdForBtn(Bit8u port, Bit8u id) { DBP_InputBind bnd = BindForBtn(port, id); return PORT_DEVICE_INDEX_ID(bnd); }

	static const Bit8u* Apply(Bit8u port, const Bit8u* mapping, bool is_preset, bool only_unbound = false)
	{
		static const Bit8u bindUsedToNext[20] = { RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_X, 0xFF, 0xFF, 0xFF, 0xFF, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, 0xFF, DBP_ANALOGBINDID(LEFT,Y), DBP_ANALOGBINDID(RIGHT,X), DBP_ANALOGBINDID(RIGHT,Y), 0xFF };
		bool bound_buttons[20] = {false};
		if (only_unbound) for (const DBP_InputBind& b : dbp_input_binds)
		{
			if (b.port != port) continue;
			if (b.device == RETRO_DEVICE_JOYPAD && b.id <= RETRO_DEVICE_ID_JOYPAD_R3) bound_buttons[b.id] = true;
			else if (b.device == RETRO_DEVICE_ANALOG) bound_buttons[DBP_ANALOGBINDID2(b.index,b.id)] = true;
		}
		bool bind_osd = (port == 0 && dbp_on_screen_keyboard && !bound_buttons[RETRO_DEVICE_ID_JOYPAD_L3]);
		if (bind_osd && is_preset) bound_buttons[RETRO_DEVICE_ID_JOYPAD_L3] = true;

		const Bit8u count = (mapping ? *mapping : 0), *p = mapping + 1;
		for (Bit8u num = 0; num != count; num++)
		{
			Bit8u btn_val = *(p++), btn_id = (Bit8u)(btn_val & 31), isAnalog = (btn_id >= 16), hasActionName = !!(btn_val & 32), key_count = 1 + (btn_val>>6);
			if (btn_id > 19) { DBP_ASSERT(0); goto err; }
			if (hasActionName) while (*(p++) & 128); // skip name_offset

			while (btn_id != 0xFF && bound_buttons[btn_id]) btn_id = bindUsedToNext[btn_id];
			if (btn_id == 0xFF) { p += (key_count * (1 + isAnalog)); continue; }
			bound_buttons[btn_id] = true;

			DBP_InputBind bnd = BindForBtn(port, btn_id);
			while (key_count--)
			{
				Bit8u k[] = { *(p++), (isAnalog ? *(p++) : (Bit8u)0) };
				if (!SetBindMetaFromPair(bnd, k[0], k[1])) { DBP_ASSERT(0); goto err; }
				if (bnd.evt == DBPET_ONSCREENKEYBOARD) bind_osd = false;
				InsertBind(bnd);
			}
		}

		if (bind_osd && (is_preset || !bound_buttons[RETRO_DEVICE_ID_JOYPAD_L3]))
			InsertBind({ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, DBPET_ONSCREENKEYBOARD });

		return p;
		err: retro_notify(0, RETRO_LOG_ERROR, "Gamepad mapping data is invalid"); return mapping+DBP_PADMAP_MAXSIZE_TOTAL;
	}

	static const Bit8u* SkipMapping(const Bit8u* mapping)
	{
		const Bit8u count = (mapping ? *mapping : 0), *p = mapping + 1;
		for (Bit8u num = 0; num != count; num++)
		{
			Bit8u btn_val = *(p++), btn_id = (Bit8u)(btn_val & 31), isAnalog = (btn_id >= 16), hasActionName = !!(btn_val & 32), key_count = 1 + (btn_val>>6);
			if (btn_id > 19) { DBP_ASSERT(0); return mapping+DBP_PADMAP_MAXSIZE_TOTAL; }
			if (hasActionName) while (*(p++) & 128); // skip name_offset
			p += (key_count * (1 + isAnalog));
		}
		return p;
	}

	static Bit16s GetAxisSpecialMappingMeta(Bit16s evt)
	{
		for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
		{
			if (sm.evt != evt || sm.meta != -1) continue;
			DBP_ASSERT((&sm)[1].evt == sm.evt && (&sm)[1].meta == -sm.meta);
			int key = DBP_SPECIALMAPPINGS_KEY + (int)(&sm - DBP_SpecialMappings);
			return DBP_MAPPAIR_MAKE(key, key+1);
		}
		DBP_ASSERT(false); return 0;
	}

	static bool SetBindMetaFromPair(DBP_InputBind& b, Bit8u k0, Bit8u k1)
	{
		if (b.device != RETRO_DEVICE_ANALOG)
		{
			if (k0 < KBD_LAST && k0 != KBD_NONE)
			{
				b.evt = DBPET_KEYDOWN;
				b.meta = k0;
			}
			else if (k0 >= DBP_SPECIALMAPPINGS_KEY && k0 < DBP_SPECIALMAPPINGS_MAX)
			{
				b.evt = DBP_SPECIALMAPPING(k0).evt;
				b.meta = DBP_SPECIALMAPPING(k0).meta;
			}
			else return false;
		}
		else
		{
			if (k1 == k0 + 1 && k0 >= DBP_SPECIALMAPPINGS_KEY && k1 < DBP_SPECIALMAPPINGS_MAX && DBP_SPECIALMAPPING(k0).evt <= _DBPET_JOY_AXIS_MAX && DBP_SPECIALMAPPING(k0).evt == DBP_SPECIALMAPPING(k1).evt)
			{
				DBP_ASSERT(DBP_SPECIALMAPPING(k0).meta == -1 && DBP_SPECIALMAPPING(k1).meta == 1);
				b.evt = DBP_SPECIALMAPPING(k0).evt;
				b.meta = 0;
			}
			else if ((k0 < KBD_LAST || (k0 >= DBP_SPECIALMAPPINGS_KEY && k0 < DBP_SPECIALMAPPINGS_MAX)) && (k1 < KBD_LAST || (k1 >= DBP_SPECIALMAPPINGS_KEY && k1 < DBP_SPECIALMAPPINGS_MAX)) && (k0 != KBD_NONE || k1 != KBD_NONE))
			{
				b.evt = DBPET_AXISMAPPAIR;
				b.meta = DBP_MAPPAIR_MAKE(k0, k1);
			}
			else return false;
		}
		return true;
	}

	static Bit8u FillBinds(Bit8u* p, Bit32u port_device_index_id, Bit8u isAnalog)
	{
		Bit8u key_count = 0;
		for (const DBP_InputBind& b : dbp_input_binds)
		{
			if (PORT_DEVICE_INDEX_ID(b) != port_device_index_id) continue;
			p[0] = p[1] = (Bit8u)KBD_NONE;
			if (isAnalog)
			{
				Bit16s meta = ((b.evt != DBPET_AXISMAPPAIR && b.evt != _DBPET_MAX) ? GetAxisSpecialMappingMeta(b.evt) : b.meta);
				p[0] = DBP_MAPPAIR_GET(-1, meta), p[1] = DBP_MAPPAIR_GET(1, meta);
			}
			else if (b.evt == DBPET_KEYDOWN)
				p[0] = (Bit8u)b.meta;
			else for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
				if (sm.evt == b.evt && sm.meta == b.meta)
					{ p[0] = (Bit8u)DBP_SPECIALMAPPINGS_KEY+(Bit8u)(&sm - DBP_SpecialMappings); break; }
			if (p[0] == KBD_NONE && p[1] == KBD_NONE) continue;
			p += (isAnalog ? 2 : 1);
			if (++key_count == 4) break;
		}
		return key_count;
	}

	static const Bit8u* PresetBinds(EPreset preset, Bit8u port)
	{
		static const Bit8u
			arrPRESET_MOUSE_LEFT_ANALOG[]          = {  7,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           204,     //   Left Click
				RETRO_DEVICE_ID_JOYPAD_A,           205,     //   Right Click
				RETRO_DEVICE_ID_JOYPAD_X,           206,     //   Middle Click
				RETRO_DEVICE_ID_JOYPAD_L2,          207,     //   Speed Up
				RETRO_DEVICE_ID_JOYPAD_R2,          208,     //   Slow Down
				DBP_ANALOGBINDID(LEFT,X),           202,203, //   Move Left / Move Right
				DBP_ANALOGBINDID(LEFT,Y),           200,201, //   Move Up / Move Down
			}, arrPRESET_MOUSE_RIGHT_ANALOG[]       = {  7,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_L,           204,     //   Left Click
				RETRO_DEVICE_ID_JOYPAD_R,           205,     //   Right Click
				RETRO_DEVICE_ID_JOYPAD_X,           206,     //   Middle Click
				RETRO_DEVICE_ID_JOYPAD_L2,          207,     //   Speed Up
				RETRO_DEVICE_ID_JOYPAD_R2,          208,     //   Slow Down
				DBP_ANALOGBINDID(RIGHT,X),          202,203, //   Move Left / Move Right
				DBP_ANALOGBINDID(RIGHT,Y),          200,201, //   Move Up / Move Down
			}, arrPRESET_GRAVIS_GAMEPAD[]           = { 10,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_Y,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_UP,          209,     //   Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        210,     //   Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        211,     //   Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       212,     //   Right
				RETRO_DEVICE_ID_JOYPAD_X,           216,     //   Button 4
				RETRO_DEVICE_ID_JOYPAD_A,           214,     //   Button 2
				DBP_ANALOGBINDID(LEFT,X),           211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT,Y),           209,210, //   Up / Down
			}, arrPRESET_BASIC_JOYSTICK_1[]         = {  8,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_Y,           214,     //   Button 2
				RETRO_DEVICE_ID_JOYPAD_UP,          209,     //   Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        210,     //   Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        211,     //   Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       212,     //   Right
				DBP_ANALOGBINDID(LEFT,X),           211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT,Y),           209,210, //   Up / Down
			}, arrPRESET_BASIC_JOYSTICK_2[]         = {  8,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_Y,           216,     //   Button 4
				RETRO_DEVICE_ID_JOYPAD_UP,          221,     //   Joy 2 Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        222,     //   Joy 2 Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        223,     //   Joy 2 Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       224,     //   Joy 2 Right
				DBP_ANALOGBINDID(LEFT,X),           223,224, //   Joy 2 Left/Right
				DBP_ANALOGBINDID(LEFT,Y),           221,222, //   Joy 2 Up/Down
			}, arrPRESET_THRUSTMASTER_FLIGHTSTICK[] = { 11,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_Y,           214,     //   Button 2
				RETRO_DEVICE_ID_JOYPAD_UP,          217,     //   Hat Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        218,     //   Hat Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        219,     //   Hat Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       220,     //   Hat Right
				RETRO_DEVICE_ID_JOYPAD_A,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_X,           216,     //   Button 4
				DBP_ANALOGBINDID(LEFT, X),          211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT, Y),          209,210, //   Up / Down
				DBP_ANALOGBINDID(RIGHT,X),          223,224, //   Joy 2 Left/Right
			}, arrPRESET_BOTH_DOS_JOYSTICKS[]       = {  8,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_Y,           214,     //   Button 2
				RETRO_DEVICE_ID_JOYPAD_A,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_X,           216,     //   Button 4
				DBP_ANALOGBINDID(LEFT, X),          211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT, Y),          209,210, //   Up / Down
				DBP_ANALOGBINDID(RIGHT,X),          223,224, //   Joy 2 Left/Right
				DBP_ANALOGBINDID(RIGHT,Y),          221,222, //   Joy 2 Up/Down
			}, arrPRESET_GENERICKEYBOARD_0[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_up,                            RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_down,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_left,                          RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_right,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_esc,                           RETRO_DEVICE_ID_JOYPAD_START,       KBD_enter,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_space,                         RETRO_DEVICE_ID_JOYPAD_Y,           KBD_leftshift,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_leftctrl,                      RETRO_DEVICE_ID_JOYPAD_A,           KBD_leftalt,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_1,                             RETRO_DEVICE_ID_JOYPAD_R,           KBD_2,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_3,                             RETRO_DEVICE_ID_JOYPAD_R2,          KBD_4,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_f1,                            RETRO_DEVICE_ID_JOYPAD_R3,          KBD_f2,
				DBP_ANALOGBINDID(LEFT, X),          KBD_left,   KBD_right,             DBP_ANALOGBINDID(LEFT, Y),          KBD_up,     KBD_down,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_home,   KBD_end,               DBP_ANALOGBINDID(RIGHT,Y),          KBD_pageup, KBD_pagedown,
			}, arrPRESET_GENERICKEYBOARD_1[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_kp8,                           RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_kp2,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_kp4,                           RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_kp6,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_kpperiod,                      RETRO_DEVICE_ID_JOYPAD_START,       KBD_kpenter,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_kp5,                           RETRO_DEVICE_ID_JOYPAD_Y,           KBD_kp1,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_kp0,                           RETRO_DEVICE_ID_JOYPAD_A,           KBD_kp3,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_kp7,                           RETRO_DEVICE_ID_JOYPAD_R,           KBD_kp9,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_kpminus,                       RETRO_DEVICE_ID_JOYPAD_R2,          KBD_kpplus,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_kpdivide,                      RETRO_DEVICE_ID_JOYPAD_R3,          KBD_kpmultiply,
				DBP_ANALOGBINDID(LEFT, X),          KBD_kp4,      KBD_kp6,             DBP_ANALOGBINDID(LEFT, Y),          KBD_kp8,      KBD_kp2,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_kpminus,  KBD_kpplus,          DBP_ANALOGBINDID(RIGHT,Y),          KBD_kpdivide, KBD_kpmultiply,
			}, arrPRESET_GENERICKEYBOARD_2[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_q,                             RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_a,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_z,                             RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_x,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_g,                             RETRO_DEVICE_ID_JOYPAD_START,       KBD_h,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_d,                             RETRO_DEVICE_ID_JOYPAD_Y,           KBD_f,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_c,                             RETRO_DEVICE_ID_JOYPAD_A,           KBD_s,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_w,                             RETRO_DEVICE_ID_JOYPAD_R,           KBD_e,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_r,                             RETRO_DEVICE_ID_JOYPAD_R2,          KBD_t,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_v,                             RETRO_DEVICE_ID_JOYPAD_R3,          KBD_b,
				DBP_ANALOGBINDID(LEFT, X),          KBD_z, KBD_x,                      DBP_ANALOGBINDID(LEFT, Y),          KBD_q, KBD_a,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_j, KBD_l,                      DBP_ANALOGBINDID(RIGHT,Y),          KBD_i, KBD_k,
			}, arrPRESET_GENERICKEYBOARD_3[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_backspace,                     RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_backslash,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_semicolon,                     RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_quote,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_o,                             RETRO_DEVICE_ID_JOYPAD_START,       KBD_p,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_slash,                         RETRO_DEVICE_ID_JOYPAD_Y,           KBD_rightshift,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_rightctrl,                     RETRO_DEVICE_ID_JOYPAD_A,           KBD_rightalt,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_leftbracket,                   RETRO_DEVICE_ID_JOYPAD_R,           KBD_rightbracket,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_comma,                         RETRO_DEVICE_ID_JOYPAD_R2,          KBD_period,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_minus,                         RETRO_DEVICE_ID_JOYPAD_R3,          KBD_equals,
				DBP_ANALOGBINDID(LEFT, X),          KBD_semicolon,   KBD_quote,        DBP_ANALOGBINDID(LEFT, Y),          KBD_backspace,   KBD_backslash,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_leftbracket, KBD_rightbracket, DBP_ANALOGBINDID(RIGHT,Y),          KBD_minus,       KBD_equals,
			};

		switch (preset)
		{
			case PRESET_AUTOMAPPED:               return dbp_auto_mapping;
			case PRESET_GENERICKEYBOARD:
				switch (port & 3)
				{
					case 0: return arrPRESET_GENERICKEYBOARD_0;
					case 1: return arrPRESET_GENERICKEYBOARD_1;
					case 2: return arrPRESET_GENERICKEYBOARD_2;
					case 3: return arrPRESET_GENERICKEYBOARD_3;
				}
			case PRESET_MOUSE_LEFT_ANALOG:        return arrPRESET_MOUSE_LEFT_ANALOG;
			case PRESET_MOUSE_RIGHT_ANALOG:       return arrPRESET_MOUSE_RIGHT_ANALOG;
			case PRESET_GRAVIS_GAMEPAD:           return arrPRESET_GRAVIS_GAMEPAD;
			case PRESET_BASIC_JOYSTICK_1:         return arrPRESET_BASIC_JOYSTICK_1;
			case PRESET_BASIC_JOYSTICK_2:         return arrPRESET_BASIC_JOYSTICK_2;
			case PRESET_THRUSTMASTER_FLIGHTSTICK: return arrPRESET_THRUSTMASTER_FLIGHTSTICK;
			case PRESET_BOTH_DOS_JOYSTICKS:       return arrPRESET_BOTH_DOS_JOYSTICKS;
		}
		return NULL;
	}

	static const char* FindAutoMapButtonLabel(Bit8u bind_count, Bit8u bind_analog, const Bit8u* bind_buf)
	{
		if (!bind_count || !dbp_auto_mapping || !dbp_auto_mapping_names) return NULL;
		const Bit8u count = *dbp_auto_mapping, *p = dbp_auto_mapping + 1;
		for (Bit8u num = 0, btn_id, bnd_id, isAnalog, hasActionName, key_count; num != count; p += key_count * (isAnalog ? 2 : 1), num++)
		{
			btn_id = *(p++), bnd_id = (Bit8u)(btn_id & 31), isAnalog = (bnd_id >= 16), hasActionName = !!(btn_id & 32), key_count = 1 + (btn_id>>6);
			if (bnd_id > 19) { DBP_ASSERT(0); return NULL; }
			if (!hasActionName) continue;
			Bit32u name_offset = 0;
			do { name_offset = (name_offset<<7)|(*p&127); } while (*(p++) & 128);
			if (key_count == bind_count && bind_analog == isAnalog && !memcmp(p, bind_buf, key_count * (isAnalog ? 2 : 1)))
				return dbp_auto_mapping_names + name_offset;
		}
		return NULL;
	}
};

static void DBP_Shutdown()
{
	// to be called on the main thread
	if (dbp_state == DBPSTATE_SHUTDOWN || dbp_state == DBPSTATE_BOOT) return;
	DBP_ThreadControl(TCM_SHUTDOWN);
	if (!dbp_crash_message.empty())
	{
		retro_notify(0, RETRO_LOG_ERROR, "DOS crashed: %s", dbp_crash_message.c_str());
		dbp_crash_message.clear();
	}
	DBP_ASSERT(control);
	if (control)
	{
		DBP_ASSERT(!first_shell); //should have been properly cleaned up
		delete control;
		control = NULL;
	}
	dbp_state = DBPSTATE_SHUTDOWN;
}

void DBP_ForceReset()
{
	// to be called on the main thread
	retro_input_state_t tmp = input_state_cb;
	input_state_cb = NULL;
	retro_reset();
	input_state_cb = tmp;
}

void DBP_OnBIOSReboot()
{
	// to be called on the DOSBox thread
	dbp_biosreboot = true;
	DBP_DOSBOX_ForceShutdown();
}

static double DBP_GetFPS()
{
	// More accurate then render.src.fps would be (1000.0 / vga.draw.delay.vtotal)
	if (dbp_force60fps) return 60;
	if (dbp_latency != DBP_LATENCY_VARIABLE) return render.src.fps;
	if (!dbp_targetrefreshrate && (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &dbp_targetrefreshrate) || dbp_targetrefreshrate < 1)) dbp_targetrefreshrate = 60.0f;
	return dbp_targetrefreshrate;
}

void DBP_Crash(const char* msg)
{
	log_cb(RETRO_LOG_WARN, "[DOSBOX] Crash: %s\n", msg);
	dbp_crash_message = msg;
	DBP_DOSBOX_ForceShutdown();
}

Bit32u DBP_GetTicks()
{
	return (Bit32u)((time_cb() - dbp_boot_time) / 1000);
}

void DBP_MidiDelay(Bit32u ms)
{
	if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD) return;
	retro_sleep(ms);
}

bool DBP_IsKeyDown(KBD_KEYS key)
{
	return (dbp_keys_down[key] != 0);
}

bool DBP_IsShuttingDown()
{
	return (!first_shell || first_shell->exit);
}

bool DBP_GetRetroMidiInterface(retro_midi_interface* res)
{
	return (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE, res));
}

bool DBP_IsLowLatency()
{
	return dbp_latency == DBP_LATENCY_LOW;
}

void DBP_EnableNetwork()
{
	if (dbp_use_network) return;
	dbp_use_network = true;

	extern const char* RunningProgram;
	bool running_dos_game = (dbp_had_game_running && strcmp(RunningProgram, "BOOT"));
	if (running_dos_game && DBP_GetTicks() < 10000) { DBP_ForceReset(); return; }
	retro_set_visibility("dosbox_pure_modem", true);

	bool pauseThread = (dbp_state != DBPSTATE_BOOT && dbp_state != DBPSTATE_SHUTDOWN);
	if (pauseThread) DBP_ThreadControl(TCM_PAUSE_FRAME);
	Section* sec = control->GetSection("ipx");
	sec->ExecuteDestroy(false);
	sec->GetProp("ipx")->SetValue("true");
	sec->ExecuteInit(false);
	sec = control->GetSection("serial");
	sec->ExecuteDestroy(false);
	sec->GetProp("serial1")->SetValue((retro_get_variable("dosbox_pure_modem", "null")[0] == 'n') ? "libretro null" : "libretro");
	sec->ExecuteInit(false);
	if (pauseThread) DBP_ThreadControl(TCM_RESUME_FRAME);
}

#include "dosbox_pure_run.h"
#include "dosbox_pure_osd.h"

Bitu GFX_GetBestMode(Bitu flags)
{
	return GFX_CAN_32 | GFX_RGBONLY | GFX_SCALING | GFX_HARDWARE;
}

Bitu GFX_GetRGB(Bit8u red, Bit8u green, Bit8u blue)
{
	return (red << 16) | (green << 8) | (blue << 0);
}

Bitu GFX_SetSize(Bitu width, Bitu height, Bitu flags, double scalex, double scaley, GFX_CallBack_t cb)
{
	// Make sure DOSbox is not using any scalers that would waste performance
	DBP_ASSERT(render.src.width == width && render.src.height == height);
	if (width > SCALER_MAXWIDTH || height > SCALER_MAXHEIGHT) { DBP_ASSERT(false); return 0; }
	//const char* VGAModeNames[] { "M_CGA2","M_CGA4","M_EGA","M_VGA","M_LIN4","M_LIN8","M_LIN15","M_LIN16","M_LIN32","M_TEXT","M_HERC_GFX","M_HERC_TEXT","M_CGA16","M_TANDY2","M_TANDY4","M_TANDY16","M_TANDY_TEXT","M_ERROR"};
	//log_cb(RETRO_LOG_INFO, "[DOSBOX SIZE] Width: %u - Height: %u - Ratio: %f (%f) - DBLH: %d - DBLW: %d - BPP: %u - Mode: %s (%d)\n",
	//	(unsigned)width, (unsigned)height, (float)((width * scalex) / (height * scaley)), render.src.ratio, render.src.dblh, render.src.dblw, render.src.bpp, VGAModeNames[vga.mode], vga.mode);
	return GFX_GetBestMode(0);
}

Bit8u* GFX_GetPixels()
{
	Bit8u* pixels = (Bit8u*)dbp_buffers[buffer_active^1].video;
	if (dbp_overscan)
	{
		Bit32u w = (Bit32u)render.src.width, border = w * dbp_overscan / 160;
		pixels += ((w + border * 2) * border + border) * 4;
	}
	return pixels;
}

bool GFX_StartUpdate(Bit8u*& pixels, Bitu& pitch)
{
	if (dbp_state == DBPSTATE_BOOT) return false;
	DBP_FPSCOUNT(dbp_fpscount_gfxstart)
	Bit32u full_width = (Bit32u)render.src.width, full_height = (Bit32u)render.src.height;
	DBP_Buffer& buf = dbp_buffers[buffer_active^1];
	pixels = (Bit8u*)buf.video;
	if (dbp_overscan)
	{
		Bit32u border = full_width * dbp_overscan / 160;
		full_width += border * 2;
		full_height += border * 2;
		pixels += (full_width * border + border) * 4;
	}
	pitch = full_width * 4;

	float ratio = (float)full_width / full_height;
	if (render.aspect) ratio /= (float)render.src.ratio;
	if (ratio < 1) ratio *= 2; //because render.src.dblw is not reliable
	if (ratio > 2) ratio /= 2; //because render.src.dblh is not reliable
	if (buf.width != full_width || buf.height != full_height || buf.ratio != ratio)
	{
		buf.width = full_width;
		buf.height = full_height;
		buf.ratio = ratio;
		buf.border_color = 0xDEADBEEF; // force refresh
	}

	if (dbp_overscan)
	{
		Bit32u border_color = (Bit32u)GFX_GetRGB(vga.dac.rgb[vga.attr.overscan_color].red<<2, vga.dac.rgb[vga.attr.overscan_color].green<<2, vga.dac.rgb[vga.attr.overscan_color].blue<<2);
		if (border_color != buf.border_color)
		{
			buf.border_color = border_color;
			for (Bit32u* p = (Bit32u*)buf.video, *pEnd = p + full_width*full_height; p != pEnd;) *(p++) = border_color;
		}
	}

	return true;
}

void GFX_EndUpdate(const Bit16u *changedLines)
{
	if (!changedLines) return;
	if (dbp_state == DBPSTATE_BOOT) return;

	buffer_active ^= 1;
	DBP_Buffer& buf = dbp_buffers[buffer_active];
	//DBP_ASSERT((Bit8u*)buf.video == render.scale.outWrite - render.scale.outPitch * render.src.height); // this assert can fail after loading a save game
	DBP_ASSERT(render.scale.outWrite >= (Bit8u*)buf.video && render.scale.outWrite <= (Bit8u*)buf.video + sizeof(buf.video));

	if (dbp_intercept_gfx) dbp_intercept_gfx(buf, dbp_intercept_data);

	if (
		#ifndef DBP_ENABLE_FPS_COUNTERS
		dbp_perf == DBP_PERF_DETAILED &&
		#endif
		memcmp(dbp_buffers[0].video, dbp_buffers[1].video, buf.width * buf.height * 4))
	{
		DBP_FPSCOUNT(dbp_fpscount_gfxend)
		dbp_perf_uniquedraw++;
	}

	// frameskip is best to be modified in this function (otherwise it can be off by one)
	dbp_framecount += 1 + render.frameskip.max;
	render.frameskip.max = (DBP_NeedFrameSkip(true) ? 1 : 0);

	// handle frame skipping and CPU speed during fast forwarding
	if (dbp_last_fastforward == (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD)) return;
	static Bit32s old_max;
	static bool old_pmode;
	if (dbp_last_fastforward ^= true)
	{
		old_max = CPU_CycleMax;
		old_pmode = cpu.pmode;
		if (dbp_throttle.rate && dbp_state == DBPSTATE_RUNNING)
		{
			// If fast forwarding at a desired rate, apply custom frameskip and max cycle rules
			render.frameskip.max = (int)(dbp_throttle.rate / av_info.timing.fps * 1.5f + .4f);
			CPU_CycleMax = (Bit32s)(old_max / (CPU_CycleAutoAdjust ? dbp_throttle.rate / av_info.timing.fps : 1.0f));
		}
		else
		{
			render.frameskip.max = 10;
			CPU_CycleMax = (cpu.pmode ? 30000 : 10000);
		}
	}
	else if (old_max)
	{
		// If we switched to protected mode while locked (likely at startup) with auto adjust cycles on, choose a reasonable base rate
		CPU_CycleMax = (old_pmode == cpu.pmode || !CPU_CycleAutoAdjust ? old_max : 20000);
		old_max = 0;
		DBP_SetRealModeCycles();
	}
}

static bool GFX_Events_AdvanceFrame(bool force_skip)
{
	enum { HISTORY_STEP = 4, HISTORY_SIZE = HISTORY_STEP * 2 };
	static struct
	{
		retro_time_t TimeLast, TimeSleepUntil;
		double LastModeHash;
		Bit32u LastFrameCount, FrameTicks, Paused, HistoryCycles[HISTORY_SIZE], HistoryEmulator[HISTORY_SIZE], HistoryFrame[HISTORY_SIZE], HistoryCursor;
	} St;

	St.FrameTicks++;
	if (St.LastFrameCount == dbp_framecount)
	{
		if (dbp_pause_events)
		{
			retro_time_t time_before_pause = time_cb();
			DBP_ThreadControl(TCM_ON_PAUSE_FRAME);
			St.Paused += (Bit32u)(time_cb() - time_before_pause);
		}
		return false;
	}

	Bit32u finishedframes = (dbp_framecount - St.LastFrameCount);
	Bit32u finishedticks = St.FrameTicks;
	St.LastFrameCount = dbp_framecount;
	St.FrameTicks = 0;

	// With certain keyboard layouts, we can end up here during startup which we don't want to do anything further
	if (dbp_state == DBPSTATE_BOOT) return true;

	retro_time_t time_before = time_cb() - St.Paused;
	St.Paused = 0;

	if (force_skip)
		return true;

	if (dbp_latency != DBP_LATENCY_VARIABLE || dbp_state == DBPSTATE_FIRST_FRAME)
	{
		DBP_ThreadControl(TCM_ON_FINISH_FRAME);
	}

	retro_time_t time_after = time_cb();
	if (dbp_latency == DBP_LATENCY_VARIABLE)
	{
		while (time_after > dbp_lastrun + 100000 && !dbp_pause_events) retro_sleep(0); // paused or frame stepping
		if (dbp_pause_events) DBP_ThreadControl(TCM_ON_PAUSE_FRAME);
		if (dbp_throttle.mode != RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.rate > .1f)
		{
			Bit32u frameTime = (Bit32u)(1000000.0 / render.src.fps * (((dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION) && dbp_throttle.rate > .1f) ? av_info.timing.fps / dbp_throttle.rate : 1.0));
			if (St.TimeSleepUntil <= time_after - frameTime * 2)
				St.TimeSleepUntil = time_after;
			else
				St.TimeSleepUntil += frameTime;
			while ((Bit32s)(St.TimeSleepUntil - time_after) > 0)
			{
				retro_sleep(((St.TimeSleepUntil - time_after) > 1500 ? 1 : 0)); // double brackets because of missing brackets in macro...
				if (dbp_pause_events) DBP_ThreadControl(TCM_ON_PAUSE_FRAME);
				time_after = time_cb();
			}
		}
	}
	retro_time_t time_last = St.TimeLast;
	St.TimeLast = time_after;

	if (dbp_perf)
	{
		dbp_perf_count += finishedframes;
		dbp_perf_emutime += (Bit32u)(time_before - time_last);
		dbp_perf_totaltime += (Bit32u)(time_after - time_last);
	}

	// Skip evaluating the performance of this frame if the display mode has changed
	double modeHash = (double)render.src.fps * render.src.width * render.src.height * ((double)vga.mode+1);
	if (modeHash != St.LastModeHash)
	{
		//log_cb(RETRO_LOG_INFO, "[DBPTIMERS@%4d] NEW VIDEO MODE %f|%d|%d|%d|%d|\n", St.HistoryCursor, render.src.fps, (int)render.src.width, (int)render.src.height, (int)vga.mode);
		St.LastModeHash = modeHash;
		St.HistoryEmulator[HISTORY_SIZE-1] = 0;
		St.HistoryCursor = 0;
		return true;
	}

	if (!CPU_CycleAutoAdjust)
	{
		St.HistoryEmulator[HISTORY_SIZE-1] = 0;
		St.HistoryCursor = 0;
		return true;
	}

	if (finishedframes > 1)
		return true;

	if (dbp_throttle.mode == RETRO_THROTTLE_FRAME_STEPPING || dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION || dbp_throttle.mode == RETRO_THROTTLE_REWINDING)
		return true;

	extern Bit64s CPU_IODelayRemoved;
	Bit32u hc = St.HistoryCursor % HISTORY_SIZE;
	St.HistoryCycles[hc] = (Bit32u)(((Bit64s)CPU_CycleMax * finishedticks - CPU_IODelayRemoved) / finishedticks);
	St.HistoryEmulator[hc] = (Bit32u)(time_before - time_last);
	St.HistoryFrame[hc] = (Bit32u)(time_after - time_last);
	St.HistoryCursor++;
	CPU_IODelayRemoved = 0;

	if ((St.HistoryCursor % HISTORY_STEP) == 0)
	{
		Bit32u frameThreshold = 0;
		for (Bit32u f : St.HistoryFrame) frameThreshold += f;
		frameThreshold = (frameThreshold / HISTORY_SIZE) * 3;

		Bit32u recentCount = 0, recentCyclesSum = 0, recentEmulator = 0, recentFrameSum = 0;
		for (int i = 0; i != HISTORY_STEP; i++)
		{
			int n = ((St.HistoryCursor + (HISTORY_SIZE-1) - i) % HISTORY_SIZE);
			if (St.HistoryFrame[n] > frameThreshold) continue;
			recentCount++;
			recentCyclesSum += St.HistoryCycles[n];
			recentEmulator += St.HistoryEmulator[n];
			recentFrameSum += St.HistoryFrame[n];
		}
		recentEmulator /= (recentCount ? recentCount : 1);

		Bit32u frameTime = (Bit32u)((1000000.0f / render.src.fps) * (dbp_auto_target - 0.01f));

		//Bit32u st = dbp_serialize_time;
		if (dbp_serialize_time)
		{
			// To deal with frontends that have a rewind-feature we need to remove the time used to create rewind states from the available frame time
			dbp_serialize_time /= HISTORY_STEP;
			if (dbp_serialize_time > frameTime - 3000)
				frameTime = 3000;
			else
				frameTime -= dbp_serialize_time;
			dbp_serialize_time = 0;
		}

		Bit32s ratio = 0;
		Bit64s recentCyclesMaxSum = (Bit64s)CPU_CycleMax * recentCount;
		if (recentCount > HISTORY_STEP/2 && St.HistoryEmulator[HISTORY_SIZE-1] && recentCyclesMaxSum >= recentCyclesSum)
		{
			// ignore the cycles added due to the IO delay code in order to have smoother auto cycle adjustments
			double ratio_not_removed = 1.0 - ((double)(recentCyclesMaxSum - recentCyclesSum) / recentCyclesMaxSum);

			// scale ratio we are aiming for (1024 is no change)
			ratio = frameTime * 1024 * 100 / 100 / recentEmulator;
			ratio = (Bit32s)((double)ratio * ratio_not_removed);

			// Don't allow very high ratio which can cause us to lock as we don't scale down
			// for very low ratios. High ratio might result because of timing resolution
			if (ratio > 16384)
				ratio = 16384;

			// Limit the ratio even more when the cycles are already way above the realmode default.
			else if (ratio > 5120 && CPU_CycleMax > 50000)
				ratio = 5120;

			else if (ratio < 1000 && /*St.RepeatDowns < 5 && */CPU_CycleMax > 50000)
			{
				// When scaling down a decent amount, scale down more aggressively to avoid sound dropping out
				ratio = (ratio > 911 ? ratio * ratio * ratio / (1024*1024) : ratio * 80 / 100);
			}

			if (ratio > 1024)
			{
				Bit64s ratio_with_removed = (Bit64s)((((double)ratio - 1024.0) * ratio_not_removed) + 1024.0);
				Bit64s cmax_scaled = (Bit64s)CPU_CycleMax * ratio_with_removed;
				CPU_CycleMax = (Bit32s)(1 + (CPU_CycleMax >> 1) + cmax_scaled / (Bit64s)2048);
			}
			else
			{
				double r = (1.0 + ratio_not_removed) / (ratio_not_removed + 1024.0 / ratio);
				CPU_CycleMax = 1 + (Bit32s)(CPU_CycleMax * r);
			}

			if (CPU_CycleMax < CPU_CYCLES_LOWER_LIMIT) CPU_CycleMax = CPU_CYCLES_LOWER_LIMIT;
			if (CPU_CycleMax > 4000000) CPU_CycleMax = 4000000;
			if (CPU_CycleMax > (Bit64s)recentEmulator * 280) CPU_CycleMax = (Bit32s)(recentEmulator * 280);
		}

		//log_cb(RETRO_LOG_INFO, "[DBPTIMERS%4d] - EMU: %5d - FE: %5d - SRLZ: %u - TARGET: %5d - EffectiveCycles: %6d - Limit: %6d|%6d - CycleMax: %6d - Scale: %5d\n",
		//	St.HistoryCursor, (int)recentEmulator, (int)((recentFrameSum / recentCount) - recentEmulator), st, frameTime, 
		//	recentCyclesSum / recentCount, CPU_CYCLES_LOWER_LIMIT, recentEmulator * 280, CPU_CycleMax, ratio);
	}
	return true;
}

void GFX_Events()
{
	// Some configuration modifications (like keyboard layout) can cause this to be called recursively
	static bool GFX_EVENTS_RECURSIVE;
	if (GFX_EVENTS_RECURSIVE) return;
	GFX_EVENTS_RECURSIVE = true;

	DBP_FPSCOUNT(dbp_fpscount_event)

	bool force_skip = false;
	if (DBP_Run::autoinput.ptr)
	{
		DBP_Run::ProcessAutoInput();
		force_skip = !!DBP_Run::autoinput.ptr;
	}
	bool wasFrameEnd = GFX_Events_AdvanceFrame(force_skip);

	static bool mouse_speed_up, mouse_speed_down;
	static int mouse_joy_x, mouse_joy_y, hatbits;
	for (;dbp_event_queue_read_cursor != dbp_event_queue_write_cursor; dbp_event_queue_read_cursor = ((dbp_event_queue_read_cursor + 1) % DBP_EVENT_QUEUE_SIZE))
	{
		DBP_Event e = dbp_event_queue[dbp_event_queue_read_cursor];
		//log_cb(RETRO_LOG_INFO, "[DOSBOX EVENT] [%4d@%6d] %s %08x%s\n", dbp_framecount, DBP_GetTicks(), (e.type > _DBPET_MAX ? "SPECIAL" : DBP_Event_Type_Names[(int)e.type]), (unsigned)e.val, (dbp_intercept_input ? " [INTERCEPTED]" : ""));
		if (dbp_intercept_input)
		{
			dbp_intercept_input(e.type, e.val, e.val2, dbp_intercept_data);
			if (!DBP_IS_RELEASE_EVENT(e.type)) continue;
		}
		#if 0
		if (e.type == DBPET_KEYDOWN && e.val == KBD_b) { void DBP_DumpPSPs(); DBP_DumpPSPs(); }
		#endif
		switch (e.type)
		{
			case DBPET_KEYDOWN: KEYBOARD_AddKey((KBD_KEYS)e.val, true);  break;
			case DBPET_KEYUP:   KEYBOARD_AddKey((KBD_KEYS)e.val, false); break;

			case DBPET_ONSCREENKEYBOARD: DBP_StartOSD(DBPOSD_OSK); break;
			case DBPET_ONSCREENKEYBOARDUP: break;

			case DBPET_MOUSEMOVE:
			{
				float mx = e.val*dbp_mouse_speed*dbp_mouse_speed_x, my = e.val2*dbp_mouse_speed; // good for 320x200?
				Mouse_CursorMoved(mx, my, (dbp_mouse_x+0x7fff)/(float)0xFFFE, (dbp_mouse_y+0x7fff)/(float)0xFFFE, (dbp_mouse_input != 'd'));
				break;
			}
			case DBPET_MOUSEDOWN: Mouse_ButtonPressed((Bit8u)e.val);  break;
			case DBPET_MOUSEUP:   Mouse_ButtonReleased((Bit8u)e.val); break;
			case DBPET_MOUSESETSPEED:   (e.val < 0 ? mouse_speed_down : mouse_speed_up) = true;  break;
			case DBPET_MOUSERESETSPEED: (e.val < 0 ? mouse_speed_down : mouse_speed_up) = false; break;
			case DBPET_JOY1X:     JOYSTICK_Move_X(0, DBP_GET_JOY_ANALOG_VALUE(e.val)); break;
			case DBPET_JOY1Y:     JOYSTICK_Move_Y(0, DBP_GET_JOY_ANALOG_VALUE(e.val)); break;
			case DBPET_JOY2X:     JOYSTICK_Move_X(1, DBP_GET_JOY_ANALOG_VALUE(e.val)); break;
			case DBPET_JOY2Y:     JOYSTICK_Move_Y(1, DBP_GET_JOY_ANALOG_VALUE(e.val)); break;
			case DBPET_JOYMX:     mouse_joy_x = (int)(DBP_GET_JOY_ANALOG_VALUE(e.val) * (float)DBP_JOY_ANALOG_RANGE); break;
			case DBPET_JOYMY:     mouse_joy_y = (int)(DBP_GET_JOY_ANALOG_VALUE(e.val) * (float)DBP_JOY_ANALOG_RANGE); break;
			case DBPET_JOY1DOWN:  JOYSTICK_Button(0, (Bit8u)e.val, true); break;
			case DBPET_JOY1UP:    JOYSTICK_Button(0, (Bit8u)e.val, false); break;
			case DBPET_JOY2DOWN:  JOYSTICK_Button(1, (Bit8u)e.val, true); break;
			case DBPET_JOY2UP:    JOYSTICK_Button(1, (Bit8u)e.val, false); break;
			case DBPET_JOYHATSETBIT:   hatbits |= e.val;  goto JOYSETHAT;
			case DBPET_JOYHATUNSETBIT: hatbits &= ~e.val; goto JOYSETHAT;
			JOYSETHAT:
				JOYSTICK_Move_Y(1,
					(hatbits == 1 ?  0.5f : //left
					(hatbits == 2 ?  0.0f : //down
					(hatbits == 4 ? -0.5f : //right
					(hatbits == 8 ? -1.0f : //up
					(hatbits == 3 ?  (JOYSTICK_GetMove_Y(1) >  0.2f ?  0.0f :  0.5f) : //down-left
					(hatbits == 6 ?  (JOYSTICK_GetMove_Y(1) < -0.2f ?  0.0f : -0.5f) : //down-right
					(hatbits == 9 ?  (JOYSTICK_GetMove_Y(1) <  0.0f ?  0.5f : -1.0f) : //up-left
					(hatbits == 12 ? (JOYSTICK_GetMove_Y(1) < -0.7f ? -0.5f : -1.0f) : //up-right
					1.0f))))))))); //centered
				break;
			case DBPET_CHANGEMOUNTS: break;
			default: DBP_ASSERT(false); break;
		}
	}

	if (wasFrameEnd)
	{
		if ((mouse_joy_x || mouse_joy_y) && (abs(mouse_joy_x) > 5 || abs(mouse_joy_y) > 5))
		{
			float mx = mouse_joy_x*.0003f, my = mouse_joy_y*.0003f;

			if (!mouse_speed_up && !mouse_speed_down) {}
			else if (mouse_speed_up && mouse_speed_down) mx *= 5, my *= 5;
			else if (mouse_speed_up) mx *= 2, my *= 2;
			else if (mouse_speed_down) mx *= .5f, my *= .5f;

			mx *= dbp_mouse_speed * dbp_mouse_speed_x;
			my *= dbp_mouse_speed;
			Mouse_CursorMoved(mx, my, 0, 0, true);
		}
	}

	GFX_EVENTS_RECURSIVE = false;
}

void GFX_SetTitle(Bit32s cycles, int frameskip, bool paused)
{
	extern const char* RunningProgram;
	bool was_game_running = dbp_game_running;
	dbp_had_game_running |= (dbp_game_running = (strcmp(RunningProgram, "DOSBOX") && strcmp(RunningProgram, "PUREMENU")));
	log_cb(RETRO_LOG_INFO, "[DOSBOX STATUS] Program: %s - Cycles: %d - Frameskip: %d - Paused: %d\n", RunningProgram, cycles, frameskip, paused);
	if (was_game_running != dbp_game_running && !strcmp(RunningProgram, "BOOT")) dbp_refresh_memmaps = true;
	if (cpu.pmode && CPU_CycleAutoAdjust && CPU_OldCycleMax == 3000 && CPU_CycleMax == 3000 && !dbp_content_year)
	{
		// Choose a reasonable base rate when switching to protected mode (avoid autoinput getting stuck with a very slow CPU)
		CPU_CycleMax = 30000;
	}
}

void GFX_ShowMsg(char const* format,...)
{
	static char buf[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	log_cb(RETRO_LOG_INFO, "[DOSBOX LOG] %s\n", buf);
}

void GFX_SetPalette(Bitu start,Bitu count,GFX_PalEntry * entries) { }

static void DBP_PureLabelProgram(Program** make)
{
	struct LabelProgram : Program
	{
		void Run(void)
		{
			if (!cmd->GetStringRemain(temp_line)) { WriteOut("Usage: LABEL [drive:] [new label]\n"); return; }
			const char* line = temp_line.c_str();
			char drive = (line[1] == '\0' || line[1] == ':' || line[1] == ' ' ? line[0] : 0);
			drive = (drive >= 'A' && drive <= 'Z' ? drive : (drive >= 'a' && drive <= 'z' ? drive-0x20 : 0));
			for (line += (drive ? (line[1] == ':' ? 2 : 1) : 0); *line && *line <= ' '; line++) {}
			if (!drive) drive = DOS_GetDefaultDrive()+'A';
			if (!Drives[drive-'A']) { WriteOut("Drive %c: does not exist\n", drive); return; }

			const char* msg = "Label of drive %c: is '%s'\n";
			std::string lbl = Drives[drive-'A']->GetLabel();
			if (*line)
			{
				char newlabel[20];
				Set_Label(line, newlabel, MSCDEX_HasDrive(drive));
				if (lbl == newlabel) msg = "Label of drive %c: was already set to '%s'\n";
				else
				{
					Drives[drive-'A']->label.SetLabel(newlabel, MSCDEX_HasDrive(drive), true);
					std::string result = Drives[drive-'A']->GetLabel();
					if (lbl == result) msg = "Label of drive %c: was not changed it is read-only set to '%s'\n";
					else { std::swap(lbl, result); msg = "Label of drive %c: was changed to '%s'\n"; }
				}
			}
			if (lbl.find('.') != std::string::npos) lbl.erase(lbl.find('.'), 1);
			WriteOut(msg, drive, lbl.c_str());
		}
	};
	*make = new LabelProgram;
}

static void DBP_PureRemountProgram(Program** make)
{
	struct RemountProgram : Program
	{
		void Run(void)
		{
			cmd->GetStringRemain(temp_line);
			const char* p1 = temp_line.c_str(), *p2 = (p1 ? strchr(p1, ' ') : NULL);
			char drive1 = (      p1[0] && p1[p1[1] == ':' ? 2 : 1] <= ' ' ? (p1[0] >= 'A' && p1[0] <= 'Z' ? p1[0] : (p1[0] >= 'a' && p1[0] <= 'z' ? p1[0]-0x20 : 0)) : 0);
			char drive2 = (p2 && p2[1] && p2[p2[2] == ':' ? 3 : 2] <= ' ' ? (p2[1] >= 'A' && p2[1] <= 'Z' ? p2[1] : (p2[1] >= 'a' && p2[1] <= 'z' ? p2[1]-0x20 : 0)) : 0);
			if (!drive1) { WriteOut("Usage: REMOUNT [olddrive:] [newdrive:]\n"); return; }
			if (!drive2) { drive2 = drive1; drive1 = DOS_GetDefaultDrive()+'A'; }
			if (!DBP_IsMounted(drive1)) { WriteOut("Drive %c: does not exist\n", drive1); return; }
			if ( DBP_IsMounted(drive2)) { WriteOut("Drive %c: already exists\n", drive2); return; }
			WriteOut("Remounting %c: to %c:\n", drive1, drive2);
			DBP_Remount(drive1, drive2);
		}
	};
	*make = new RemountProgram;
}

// -------------------------------------------------------------------------------------------------------

unsigned retro_get_region(void)  { return RETRO_REGION_NTSC; }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_audio_sample      (retro_audio_sample_t cb)       { }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll        (retro_input_poll_t cb)         { input_poll_cb  = cb; }
void retro_set_input_state       (retro_input_state_t cb)        { input_state_cb = cb; }
void retro_set_video_refresh     (retro_video_refresh_t cb)      { video_cb       = cb; }

void retro_get_system_info(struct retro_system_info *info) // #1
{
	memset(info, 0, sizeof(*info));
	info->library_name     = "DOSBox-pure";
	info->library_version  = "0.9.9";
	info->need_fullpath    = true;
	info->block_extract    = true;
	info->valid_extensions = "zip|dosz|exe|com|bat|iso|chd|cue|ins|img|ima|vhd|jrc|tc|m3u|m3u8|conf";
}

void retro_set_environment(retro_environment_t cb) //#2
{
	environ_cb = cb;
	bool allow_no_game = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &allow_no_game);
}

static void set_variables(bool force_midi_scan = false)
{
	static std::vector<std::string> dynstr;
	dynstr.clear();
	dbp_osimages.clear();
	dbp_shellzips.clear();
	std::string path, subdir;
	const char *system_dir = NULL;
	struct retro_vfs_interface_info vfs = { 3, NULL };
	retro_time_t scan_start = time_cb();
	if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir && environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) && vfs.required_interface_version >= 3 && vfs.iface)
	{
		std::vector<std::string> subdirs;
		subdirs.emplace_back();
		while (subdirs.size())
		{
			std::swap(subdir, subdirs.back());
			subdirs.pop_back();
			struct retro_vfs_dir_handle *dir = vfs.iface->opendir(path.assign(system_dir).append(subdir.length() ? "/" : "").append(subdir).c_str(), false);
			if (!dir) continue;
			while (vfs.iface->readdir(dir))
			{
				const char* entry_name = vfs.iface->dirent_get_name(dir);
				size_t ln = strlen(entry_name);
				if (vfs.iface->dirent_is_dir(dir) && strcmp(entry_name, ".") && strcmp(entry_name, ".."))
					subdirs.emplace_back(path.assign(subdir).append(subdir.length() ? "/" : "").append(entry_name));
				else if ((ln > 4 && !strncasecmp(entry_name + ln - 4, ".SF", 3)) || (ln > 12 && !strcasecmp(entry_name + ln - 12, "_CONTROL.ROM")))
				{
					dynstr.emplace_back(path.assign(subdir).append(subdir.length() ? "/" : "").append(entry_name));
					dynstr.emplace_back((entry_name[ln-2]|0x20) == 'f' ? "General MIDI SoundFont" : "Roland MT-32/CM-32L");
					dynstr.back().append(": ").append(path, 0, path.size() - ((entry_name[ln-2]|0x20) == 'f' ? 4 : 12));
				}
				else if (ln > 4 && (!strcasecmp(entry_name + ln - 4, ".IMG") || !strcasecmp(entry_name + ln - 4, ".IMA") || !strcasecmp(entry_name + ln - 4, ".VHD")))
				{
					int32_t entry_size = 0;
					std::string subpath(subdir); subpath.append(subdir.length() ? "/" : "").append(entry_name);
					FILE* f = fopen_wrap(path.assign(system_dir).append("/").append(subpath).c_str(), "rb");
					Bit64u fsize = 0; if (f) { fseek_wrap(f, 0, SEEK_END); fsize = (Bit64u)ftell_wrap(f); fclose(f); }
					if (fsize < 1024*1024*7 || (fsize % 512)) continue; // min 7MB hard disk image made up of 512 byte sectors
					dbp_osimages.push_back(std::move(subpath));
				}
				else if (ln > 5 && !strcasecmp(entry_name + ln - 5, ".DOSZ"))
				{
					dbp_shellzips.emplace_back(path.assign(subdir).append(subdir.length() ? "/" : "").append(entry_name));
				}
				else if (ln == 23 && !subdir.length() && !force_midi_scan && !strcasecmp(entry_name, "DOSBoxPureMidiCache.txt"))
				{
					std::string content;
					ReadAndClose(FindAndOpenDosFile(path.assign(system_dir).append("/").append(entry_name).c_str()), content);
					dynstr.clear();
					dbp_osimages.clear();
					dbp_shellzips.clear();
					for (const char *pLine = content.c_str(), *pEnd = pLine + content.size() + 1, *p = pLine; p != pEnd; p++)
					{
						if (*p >= ' ') continue;
						if (p == pLine) { pLine++; continue; }
						if ((p[-3]|0x21) == 's' || dynstr.size() & 1) // check ROM/rom/SF*/sf* extension, always add description from odd rows
							dynstr.emplace_back(pLine, p - pLine);
						else
							((p[-1]|0x20) == 'z' ? dbp_shellzips : dbp_osimages).emplace_back(pLine, p - pLine);
						pLine = p + 1;
					}
					if (dynstr.size() & 1) dynstr.pop_back();
					dbp_system_cached = true;
					subdirs.clear();
					break;
				}
			}
			vfs.iface->closedir(dir);
		}
	}
	if (force_midi_scan || (!dbp_system_cached && time_cb() - scan_start > 2000000 && system_dir))
	{
		if (FILE* f = fopen_wrap(path.assign(system_dir).append("/").append("DOSBoxPureMidiCache.txt").c_str(), "w"))
		{
			for (const std::string& s : dynstr) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			for (const std::string& s : dbp_osimages) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			for (const std::string& s : dbp_shellzips) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			fclose(f);
		}
	}

	#include "core_options.h"
	for (retro_core_option_v2_definition& def : option_defs)
	{
		if (!def.key || strcmp(def.key, "dosbox_pure_midi")) continue;
		size_t i = 0, numfiles = (dynstr.size() > (RETRO_NUM_CORE_OPTION_VALUES_MAX-4)*2 ? (RETRO_NUM_CORE_OPTION_VALUES_MAX-4)*2 : dynstr.size());
		for (size_t f = 0; f != numfiles; f += 2)
			if (((&dynstr[f].back())[-1]|0x20) == 'f') // .SF* extension soundfont
				def.values[i++] = { dynstr[f].c_str(), dynstr[f+1].c_str() };
		for (size_t f = 0; f != numfiles; f += 2)
			if (((&dynstr[f].back())[-1]|0x20) != 'f') // .ROM extension munt rom
				def.values[i++] = { dynstr[f].c_str(), dynstr[f+1].c_str() };
		def.values[i++] = { "disabled", "Disabled" };
		def.values[i++] = { "frontend", "Frontend MIDI driver" };
		if (dbp_system_cached)
			def.values[i++] = { "scan", (!strcmp(retro_get_variable("dosbox_pure_midi", ""), "scan") ? "System directory scan finished" : "Scan System directory for soundfonts (open this menu again after)") };
		def.values[i] = { 0, 0 };
		def.default_value = def.values[0].value;
		break;
	}

	unsigned options_ver = 0;
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &options_ver);
	if (options_ver >= 2)
	{
		// Category options support, skip the first 'Show Advanced Options' entry
		static const struct retro_core_options_v2 options = { option_cats, option_defs+1 };
		environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void*)&options);
	}
	else if (options_ver == 1)
	{
		// Convert options to V1 format, keep first 'Show Advanced Options' entry
		static std::vector<retro_core_option_definition> v1defs;
		for (const retro_core_option_v2_definition& v2def : option_defs)
		{
			if (v2def.category_key)
			{
				// Build desc string "CATEGORY > V2DESC"
				dynstr.emplace_back(v2def.category_key);
				dynstr.back().append(" > ").append(v2def.desc);
			}
			v1defs.push_back({ v2def.key, (v2def.category_key ? dynstr.back().c_str() : v2def.desc), v2def.info, {}, v2def.default_value });
			memcpy(v1defs.back().values, v2def.values, sizeof(v2def.values));
		}
		environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, (void*)&v1defs[0]);
	}
	else
	{
		// Convert options to legacy format, skip the first 'Show Advanced Options' entry
		static std::vector<retro_variable> v0defs;
		for (const retro_core_option_v2_definition& v2def : option_defs)
		{
			if (!v2def.desc) { v0defs.push_back({0,0}); break; }
			dynstr.resize(dynstr.size() + 1);
			if (v2def.category_key)
				dynstr.back().append(v2def.category_key).append(" > ");
			dynstr.back().append(v2def.desc).append("; ").append(v2def.default_value);
			for (const retro_core_option_value& v2val : v2def.values)
				if (v2val.value && strcmp(v2def.default_value, v2val.value))
					dynstr.back().append("|").append(v2val.value);
			v0defs.push_back({ v2def.key, dynstr.back().c_str() });
		}
		environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)&v0defs[1]);
	}
}

static bool check_variables(bool is_startup = false)
{
	struct Variables
	{
		static bool DosBoxSet(const char* section_name, const char* var_name, const char* new_value, bool disallow_in_game = false, bool need_restart = false)
		{
			if (!control) return false;

			Section* section = control->GetSection(section_name);
			DBP_ASSERT(section);
			Property* prop = section->GetProp(var_name);
			DBP_ASSERT(prop);
			std::string tmpval;
			const char* old_val = (prop->Get_type() == Value::V_STRING ? (const char*)prop->GetValue() : (tmpval = prop->GetValue().ToString()).c_str());
			if (!section || !strcmp(new_value, old_val) || prop->getChange() == Property::Changeable::OnlyByConfigProgram) return false;

			bool reInitSection = (dbp_state != DBPSTATE_BOOT);
			if (disallow_in_game && dbp_game_running)
			{
				retro_notify(0, RETRO_LOG_WARN, "Unable to change value while game is running");
				reInitSection = false;
			}
			if (need_restart && reInitSection && dbp_game_running)
			{
				retro_notify(2000, RETRO_LOG_INFO, "Setting will be applied after restart");
				reInitSection = false;
			}
			else if (need_restart && reInitSection)
			{
				dbp_state = DBPSTATE_REBOOT;
			}

			//log_cb(RETRO_LOG_INFO, "[DOSBOX] variable %s::%s updated from %s to %s\n", section_name, var_name, old_val.c_str(), new_value);
			bool sectionExecInit = false;
			if (reInitSection) DBP_ThreadControl(TCM_PAUSE_FRAME);
			if (reInitSection)
			{
				if (!strcmp(var_name, "midiconfig") && MIDI_TSF_SwitchSF(new_value))
				{
					// Do the SF2 reload directly (otherwise midi output stops until dos program restart)
				}
				else if (!strcmp(var_name, "cycles"))
				{
					// Set cycles value without Destroy/Init (because that can cause FPU overflow crashes)
					DBP_CPU_ModifyCycles(new_value);
				}
				else
				{
					section->ExecuteDestroy(false);
					sectionExecInit = true;
				}
			}
			bool res = prop->SetValue(new_value);
			DBP_ASSERT(res && prop->GetValue().ToString() == new_value);
			if (sectionExecInit) section->ExecuteInit(false);
			if (reInitSection) DBP_ThreadControl(TCM_RESUME_FRAME);
			return true;
		}
	};

	// Depending on this we call set_variables, which needs to be done before any retro_set_visibility call
	const char* midi = retro_get_variable("dosbox_pure_midi", "");
	if (dbp_system_cached)
	{
		if (!strcmp(midi, "scan"))
		{
			if (dbp_system_scannable) set_variables(true); // rescan and update label on "scan" option
			dbp_system_scannable = false;
			midi = "";
		}
		else if (!dbp_system_scannable)
		{
			if (!is_startup) set_variables(); // just update label on "scan" option
			dbp_system_scannable = true;
		}
	}

	char buf[16];
	unsigned options_ver = 0;
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &options_ver);
	bool show_advanced = (options_ver != 1 || retro_get_variable("dosbox_pure_advanced", "false")[0] != 'f');
	bool visibility_changed = false;

	if (dbp_last_hideadvanced == show_advanced)
	{
		static const char* advanced_options[] =
		{
			"dosbox_pure_mouse_speed_factor_x",
			"dosbox_pure_auto_mapping",
			"dosbox_pure_joystick_timed",
			"dosbox_pure_keyboard_layout",
			"dosbox_pure_joystick_analog_deadzone",
			"dosbox_pure_cpu_core",
			"dosbox_pure_menu_time",
			"dosbox_pure_sblaster_type",
			"dosbox_pure_sblaster_adlib_mode",
			"dosbox_pure_sblaster_adlib_emu",
			"dosbox_pure_gus",
			"dosbox_pure_swapstereo",
		};
		for (const char* i : advanced_options) retro_set_visibility(i, show_advanced);
		dbp_last_hideadvanced = !show_advanced;
		visibility_changed = true;
	}

	dbp_auto_mapping_mode = retro_get_variable("dosbox_pure_auto_mapping", "true")[0];

	bool old_strict_mode = dbp_strict_mode;
	dbp_strict_mode = (retro_get_variable("dosbox_pure_strict_mode", "false")[0] == 't');
	if (old_strict_mode != dbp_strict_mode && dbp_state != DBPSTATE_BOOT && !dbp_game_running)
		dbp_state = DBPSTATE_REBOOT;

	char mchar = (dbp_reboot_machine ? dbp_reboot_machine : retro_get_variable("dosbox_pure_machine", "svga")[0]);
	int mch = (dbp_state != DBPSTATE_BOOT ? machine : -1);
	bool machine_is_svga = (mch == MCH_VGA && svgaCard != SVGA_None), machine_is_cga = (mch == MCH_CGA), machine_is_hercules = (mch == MCH_HERC);
	const char* dbmachine;
	switch (mchar)
	{
		case 's': dbmachine = retro_get_variable("dosbox_pure_svga", "svga_s3"); machine_is_svga = true; break;
		case 'v': dbmachine = "vgaonly"; break;
		case 'e': dbmachine = "ega"; break;
		case 'c': dbmachine = "cga"; machine_is_cga = true; break;
		case 't': dbmachine = "tandy"; break;
		case 'h': dbmachine = "hercules"; machine_is_hercules = true; break;
		case 'p': dbmachine = "pcjr"; break;
	}
	visibility_changed |= Variables::DosBoxSet("dosbox", "machine", dbmachine, false, true);
	Variables::DosBoxSet("dosbox", "vmemsize", retro_get_variable("dosbox_pure_svgamem", "2"), false, true);
	if (dbp_reboot_machine)
		control->GetSection("dosbox")->GetProp("machine")->OnChangedByConfigProgram(), dbp_reboot_machine = 0;

	const char* mem = retro_get_variable("dosbox_pure_memory_size", "16");
	if (dbp_reboot_set64mem) mem = "64";
	bool mem_use_extended = (atoi(mem) > 0);
	Variables::DosBoxSet("dos", "xms", (mem_use_extended ? "true" : "false"), true);
	Variables::DosBoxSet("dos", "ems", (mem_use_extended ? "true" : "false"), true);
	Variables::DosBoxSet("dosbox", "memsize", (mem_use_extended ? mem : "16"), false, true);

	const char* audiorate = retro_get_variable("dosbox_pure_audiorate", DBP_DEFAULT_SAMPLERATE_STRING);
	Variables::DosBoxSet("mixer", "rate", audiorate, false, true);
	Variables::DosBoxSet("mixer", "swapstereo", retro_get_variable("dosbox_pure_swapstereo", "false"));
	dbp_swapstereo = (bool)control->GetSection("mixer")->GetProp("swapstereo")->GetValue(); // to also get dosbox.conf override

	if (dbp_state == DBPSTATE_BOOT)
	{
		Variables::DosBoxSet("sblaster", "oplrate",   audiorate);
		Variables::DosBoxSet("speaker",  "pcrate",    audiorate);
		Variables::DosBoxSet("speaker",  "tandyrate", audiorate);

		// initiate audio buffer, we don't need SDL specific behavior, so just set a large enough buffer
		Variables::DosBoxSet("mixer", "prebuffer", "0");
		Variables::DosBoxSet("mixer", "blocksize", "2048");
	}

	// Emulation options
	dbp_force60fps = (retro_get_variable("dosbox_pure_force60fps", "default")[0] == 't');

	const char latency = retro_get_variable("dosbox_pure_latency", "none")[0];
	bool toggled_variable = (dbp_state != DBPSTATE_BOOT && (dbp_latency == DBP_LATENCY_VARIABLE) != (latency == 'v'));
	if (toggled_variable) DBP_ThreadControl(TCM_PAUSE_FRAME);
	switch (latency)
	{
		case 'l': dbp_latency = DBP_LATENCY_LOW;      break;
		case 'v': dbp_latency = DBP_LATENCY_VARIABLE; break;
		default:  dbp_latency = DBP_LATENCY_DEFAULT;  break;
	}
	if (toggled_variable) DBP_ThreadControl(dbp_pause_events ? TCM_RESUME_FRAME : TCM_NEXT_FRAME);
	retro_set_visibility("dosbox_pure_auto_target", (dbp_latency == DBP_LATENCY_LOW));

	switch (retro_get_variable("dosbox_pure_perfstats", "none")[0])
	{
		case 's': dbp_perf = DBP_PERF_SIMPLE; break;
		case 'd': dbp_perf = DBP_PERF_DETAILED; break;
		default:  dbp_perf = DBP_PERF_NONE; break;
	}
	switch (retro_get_variable("dosbox_pure_savestate", "on")[0])
	{
		case 'd': dbp_serializemode = DBPSERIALIZE_DISABLED; break;
		case 'r': dbp_serializemode = DBPSERIALIZE_REWIND; break;
		default: dbp_serializemode = DBPSERIALIZE_STATES; break;
	}
	DBPArchive::accomodate_delta_encoding = (dbp_serializemode == DBPSERIALIZE_REWIND);
	dbp_conf_loading = retro_get_variable("dosbox_pure_conf", "false")[0];
	dbp_menu_time = (char)atoi(retro_get_variable("dosbox_pure_menu_time", "99"));

	const char* cycles = retro_get_variable("dosbox_pure_cycles", "auto");
	bool cycles_numeric = (cycles[0] >= '0' && cycles[0] <= '9');
	retro_set_visibility("dosbox_pure_cycles_scale", cycles_numeric);
	retro_set_visibility("dosbox_pure_cycle_limit", !cycles_numeric);
	if (cycles_numeric)
	{
		snprintf(buf, sizeof(buf), "%d", (int)(atoi(cycles) * (float)atof(retro_get_variable("dosbox_pure_cycles_scale", "1.0")) + .499));
		cycles = buf;
	}
	visibility_changed |= Variables::DosBoxSet("cpu", "cycles", cycles);

	dbp_auto_target =
		(dbp_latency == DBP_LATENCY_LOW ? (float)atof(retro_get_variable("dosbox_pure_auto_target", "0.8")) : 1.0f)
		* (cycles_numeric ? 1.0f : (float)atof(retro_get_variable("dosbox_pure_cycle_limit", "1.0")));

	extern const char* RunningProgram;
	Variables::DosBoxSet("cpu", "core", ((!memcmp(RunningProgram, "BOOT", 5) && retro_get_variable("dosbox_pure_bootos_forcenormal", "false")[0] == 't') ? "normal" : retro_get_variable("dosbox_pure_cpu_core", "auto")));
	Variables::DosBoxSet("cpu", "cputype", retro_get_variable("dosbox_pure_cpu_type", "auto"), true);

	retro_set_visibility("dosbox_pure_modem", dbp_use_network);
	if (dbp_use_network)
		Variables::DosBoxSet("serial", "serial1", ((retro_get_variable("dosbox_pure_modem", "null")[0] == 'n') ? "libretro null" : "libretro"));

	retro_set_visibility("dosbox_pure_svga", machine_is_svga);
	retro_set_visibility("dosbox_pure_svgamem", machine_is_svga);
	retro_set_visibility("dosbox_pure_voodoo", machine_is_svga);
	retro_set_visibility("dosbox_pure_voodoo_perf", machine_is_svga);
	if (machine_is_svga)
	{
		Variables::DosBoxSet("pci", "voodoo", retro_get_variable("dosbox_pure_voodoo", "12mb"), true, true);
		Variables::DosBoxSet("pci", "voodoo_perf", retro_get_variable("dosbox_pure_voodoo_perf", "1"), true);
	}

	retro_set_visibility("dosbox_pure_cga", machine_is_cga);
	if (machine_is_cga)
	{
		const char* cga = retro_get_variable("dosbox_pure_cga", "early_auto");
		bool cga_new_model = false;
		const char* cga_mode = NULL;
		if (!memcmp(cga, "early_", 6)) { cga_new_model = false; cga_mode = cga + 6; }
		if (!memcmp(cga, "late_",  5)) { cga_new_model = true;  cga_mode = cga + 5; }
		DBP_CGA_SetModelAndComposite(cga_new_model, (!cga_mode || cga_mode[0] == 'a' ? 0 : ((cga_mode[0] == 'o' && cga_mode[1] == 'n') ? 1 : 2)));
	}

	retro_set_visibility("dosbox_pure_hercules", machine_is_hercules);
	if (machine_is_hercules)
	{
		const char herc_mode = retro_get_variable("dosbox_pure_hercules", "white")[0];
		DBP_Hercules_SetPalette(herc_mode == 'a' ? 1 : (herc_mode == 'g' ? 2 : 0));
	}

	Variables::DosBoxSet("render", "aspect", retro_get_variable("dosbox_pure_aspect_correction", "false"));

	unsigned char new_overscan = (unsigned char)atoi(retro_get_variable("dosbox_pure_overscan", "0"));
	if (new_overscan != dbp_overscan)
	{
		for (DBP_Buffer& buf : dbp_buffers) buf.border_color = 0xDEADBEEF; // force refresh
		dbp_overscan = new_overscan;
	}

	const char* sblaster_conf = retro_get_variable("dosbox_pure_sblaster_conf", "A220 I7 D1 H5");
	static const char sb_attribs[] = { 'A', 'I', 'D', 'H' };
	static const char* sb_props[] = { "sbbase", "irq", "dma", "hdma" };
	for (int i = 0; i != 4; i++)
	{
		const char *p = strchr(sblaster_conf, sb_attribs[i]), *pend = (p ? strchr(p, ' ') : NULL);
		if (!p++) continue; //the ++ moves past the attrib char
		size_t plen = (pend ? (pend - p) : strlen(p));
		if (plen >= sizeof(buf)) continue;
		memcpy(buf, p, plen);
		buf[plen] = '\0';
		Variables::DosBoxSet("sblaster", sb_props[i], buf);
	}

	std::string soundfontpath;
	if (!strcmp(midi, "disabled") || !strcasecmp(midi, "none")) midi = "";
	else if (*midi && strcmp(midi, "frontend") && strcmp(midi, "scan"))
		midi = (soundfontpath = DBP_GetSaveFile(SFT_SYSTEMDIR)).append(midi).c_str();
	Variables::DosBoxSet("midi", "midiconfig", midi);

	Variables::DosBoxSet("sblaster", "sbtype", retro_get_variable("dosbox_pure_sblaster_type", "sb16"));
	Variables::DosBoxSet("sblaster", "oplmode", retro_get_variable("dosbox_pure_sblaster_adlib_mode", "auto"));
	Variables::DosBoxSet("sblaster", "oplemu", retro_get_variable("dosbox_pure_sblaster_adlib_emu", "default"));
	Variables::DosBoxSet("gus", "gus", retro_get_variable("dosbox_pure_gus", "false"));

	Variables::DosBoxSet("joystick", "timed", retro_get_variable("dosbox_pure_joystick_timed", "true"));

	// Keyboard layout can't be change in protected mode (extracting keyboard layout doesn't work when EMS/XMS is in use)
	Variables::DosBoxSet("dos", "keyboardlayout", retro_get_variable("dosbox_pure_keyboard_layout", "us"), true);

	const char* mouse_wheel = retro_get_variable("dosbox_pure_mouse_wheel", "67/68");
	const char* mouse_wheel2 = (mouse_wheel ? strchr(mouse_wheel, '/') : NULL);
	int wkey1 = (mouse_wheel ? atoi(mouse_wheel) : 0);
	int wkey2 = (mouse_wheel2 ? atoi(mouse_wheel2 + 1) : 0);
	Bit16s bind_mousewheel = (wkey1 > KBD_NONE && wkey1 < KBD_LAST && wkey2 > KBD_NONE && wkey2 < KBD_LAST ? DBP_MAPPAIR_MAKE(wkey1, wkey2) : 0);

	bool on_screen_keyboard = (retro_get_variable("dosbox_pure_on_screen_keyboard", "true")[0] != 'f');
	char mouse_input = retro_get_variable("dosbox_pure_mouse_input", "true")[0];
	if (on_screen_keyboard != dbp_on_screen_keyboard || mouse_input != dbp_mouse_input || bind_mousewheel != dbp_bind_mousewheel)
	{
		dbp_on_screen_keyboard = on_screen_keyboard;
		dbp_mouse_input = mouse_input;
		dbp_bind_mousewheel = bind_mousewheel;
		if (dbp_state > DBPSTATE_SHUTDOWN) DBP_PadMapping::RefreshInputBinds(true);
	}
	dbp_alphablend_base = (Bit8u)((atoi(retro_get_variable("dosbox_pure_menu_transparency", "50")) + 30) * 0xFF / 130);
	dbp_mouse_speed = (float)atof(retro_get_variable("dosbox_pure_mouse_speed_factor", "1.0"));
	dbp_mouse_speed_x = (float)atof(retro_get_variable("dosbox_pure_mouse_speed_factor_x", "1.0"));

	dbp_joy_analog_deadzone = (int)((float)atoi(retro_get_variable("dosbox_pure_joystick_analog_deadzone", "15")) * 0.01f * (float)DBP_JOY_ANALOG_RANGE);

	return visibility_changed;
}

static void init_dosbox_load_dosboxconf(const std::string& cfg, Section** ref_autoexec)
{
	std::string line; Section* section = NULL; std::string::size_type loc;
	for (std::istringstream in(cfg); std::getline(in, line);)
	{
		trim(line);
		switch (line.size() ? line[0] : 0)
		{
			case '%': case '\0': case '#': case ' ': case '\r': case '\n': continue;
			case '[':
				if ((loc = line.find(']')) == std::string::npos) continue;
				if (Section* sec = control->GetSection(line.erase(loc).erase(0, 1))) section = sec;
				continue;
			default:
				if (!section || !section->HandleInputline(line)) continue;
				if (section == *ref_autoexec) *ref_autoexec = NULL; // skip our default autoexec
				if ((loc = line.find('=')) == std::string::npos) continue;
				trim(line.erase(loc));
				if (Property* p = section->GetProp(line.c_str())) p->OnChangedByConfigProgram();
		}
	}
}

static void init_dosbox_load_dos_yml(const std::string& yml, Section** ref_autoexec)
{
	struct Local
	{
		const char *Key, *End, *Next, *KeyX, *Val, *ValX;
		int cpu_cycles = 0, cpu_hz = 0, cpu_year = 0;
		bool Parse(const char *yml_key, const char* db_section, const char* db_key, ...)
		{
			if (yml_key && (strncmp(yml_key, Key, (size_t)(KeyX - Key)) || yml_key[KeyX - Key])) return false;
			va_list ap; va_start(ap, db_key); std::string val;
			for (;;)
			{
				const char* mapFrom = va_arg(ap, const char*);
				if (!*mapFrom) { va_end(ap); return false; }
				if (*mapFrom == '~')
				{
					val.append(Val, (size_t)(ValX - Val));
				}
				else if (*mapFrom == '/')
				{
					char buf[32];
					sprintf(buf, "%d", (atoi(Val) / 1024));
					val = buf;
				}
				else
				{
					const char* mapTo = va_arg(ap, const char*);
					if (strncmp(mapFrom, Val, (size_t)(ValX - Val))) continue;
					val.append(mapTo);
				}
				Property* prop = control->GetSection(db_section)->GetProp(db_key);
				bool res = (prop->SetValue(val) && !strcasecmp(prop->GetValue().ToString().c_str(), val.c_str()));
				if (res) prop->OnChangedByConfigProgram();
				va_end(ap);
				return res;
			}
		}
		bool ParseCPU(const char *yml_key)
		{
			if (strncmp(yml_key, Key, (size_t)(KeyX - Key)) || yml_key[KeyX - Key]) return false;
			switch (yml_key[4])
			{
				case 'c': // cpu_cycles
					return ((cpu_cycles = atoi(Val)) >= 100);
				case 'h': // cpu_hz
					return ((cpu_hz = atoi(Val)) >= 500);
				case 'y': // cpu_year
					return ((cpu_year = atoi(Val)) >= 1970);
			}
			return false;
		}
		bool ParseRun(const char *yml_key, Section* autoexec)
		{
			if (strncmp(yml_key, Key, (size_t)(KeyX - Key)) || yml_key[KeyX - Key]) return false;
			if (dbp_biosreboot) return true; // ignore run keys on bios reboot
			DBP_Run::ResetAutoboot();
			switch (yml_key[4])
			{
				case 'i': // run_input
					DBP_Run::autoinput.ptr = NULL;
					DBP_Run::autoinput.str.clear();
					DBP_Run::autoinput.str.append(Val, (size_t)(ValX - Val));
					break;
				case 'p': // run_path
					DBP_Run::startup.str = std::string(Val, (size_t)(ValX - Val));
					if (DBP_Run::startup.mode == DBP_Run::RUN_BOOTIMG) goto exec2bootimg;
					DBP_Run::startup.mode = DBP_Run::RUN_EXEC;
					break;
				case 'b': // run_boot
				case 'm': // run_mount
					{
						int imgidx = -1;
						for (DBP_Image& i : dbp_images)
							if ((i.path.size() == 4+(ValX - Val) && i.path[0] == '$' && !strncasecmp(&i.path[4], Val, (ValX - Val)))
								|| (i.longpath.size() == (ValX - Val) &&  !strncasecmp(&i.longpath[0], Val, (ValX - Val))))
								{ imgidx = (int)(&i - &dbp_images[0]); break; }
						if (imgidx == -1) return false;
						dbp_images[imgidx].remount = true;
					}
					if (yml_key[4] == 'm') break; // run_mount
					if (DBP_Run::startup.mode == DBP_Run::RUN_EXEC)
					{
						exec2bootimg:
						((static_cast<Section_line*>(autoexec)->data += '@') += DBP_Run::startup.str) += '\n';
					}
					DBP_Run::startup.mode = DBP_Run::RUN_BOOTIMG;
					DBP_Run::startup.info = 0;
					break;
			}
			return true;
		}
	} l;
	for (l.Key = yml.c_str(), l.End = l.Key+yml.size(); l.Key < l.End; l.Key = l.Next + 1)
	{
		for (l.Next = l.Key; *l.Next != '\n' && *l.Next != '\r' && *l.Next; l.Next++) {}
		if (l.Next == l.Key || *l.Key == '#') continue;
		for (l.KeyX = l.Key; *l.KeyX && *l.KeyX != ':' && *l.KeyX > ' '; l.KeyX++) {}
		if (*l.KeyX != ':' || l.KeyX == l.Key || l.KeyX[1] != ' ' ) goto syntaxerror;
		for (l.Val = l.KeyX + 2; *l.Val == ' '; l.Val++) {}
		for (l.ValX = l.Val; *l.ValX && *l.ValX != '\r' && *l.ValX != '\n' && (*l.ValX != '#' || l.ValX[-1] != ' '); l.ValX++) {}
		while (l.ValX[-1] == ' ') l.ValX--;
		if (l.ValX <= l.Val) goto syntaxerror;
		switch (*l.Key)
		{
			case 'c':
				if (0
						||l.Parse("cpu_type", "cpu", "cputype" , "auto","auto" , "generic_386","386" , "generic_486","486_slow" , "generic_pentium","pentium_slow" , "")
						||l.ParseCPU("cpu_cycles")||l.ParseCPU("cpu_hz")||l.ParseCPU("cpu_year")
					) break; else goto syntaxerror;
			case 'm':
				if (0
						||l.Parse("mem_size", "dosbox", "memsize", "/")
						||l.Parse("mem_xms", "dos", "xms" , "true","true" , "false","false" , "")
						||l.Parse("mem_ems", "dos", "ems" , "true","true" , "false","false" , "")
						||l.Parse("mem_umb", "dos", "umb" , "true","true" , "false","false" , "")
						||l.Parse("mem_doslimit", "dos", "memlimit", "~")
					) break; else goto syntaxerror;
			case 'v':
				if (0
						||l.Parse("video_card", "dosbox", "machine" , "generic_svga","svga_s3" , "generic_hercules","hercules" , "generic_cga","cga" , "generic_tandy","tandy" , "generic_pcjr","pcjr" , "generic_ega","ega" , "generic_vga","vgaonly" , "svga_s3_trio","svga_s3", "svga_tseng_et3000","svga_et3000" , "svga_tseng_et4000","svga_et4000" , "svga_paradise_pvga1a","svga_paradise" , "")
						||l.Parse("video_memory", "dosbox", "vmemsize", "/")
						||l.Parse("video_voodoo", "pci", "voodoo" , "true","12mb" , "false","false" , "")
					) break; else goto syntaxerror;
			case 's':
				if (0
						||l.Parse("sound_card", "sblaster", "sbtype" , "sb16","sb16" , "sb1","sb1" , "sb2","sb2" , "sbpro1","sbpro1" , "sbpro2","sbpro2" , "gameblaster","gb" , "none","none" , "")
						||l.Parse("sound_port", "sblaster", "sbbase" , "~")
						||l.Parse("sound_irq", "sblaster", "irq", "~")
						||l.Parse("sound_dma", "sblaster", "dma", "~")
						||l.Parse("sound_hdma", "sblaster", "hdma", "~")
						||l.Parse("sound_mpu401", "midi", "mpu401" , "true","intelligent" , "false","none" , "")
						||l.Parse("sound_mt32", "midi", "mpu401" , "true","intelligent" , "false","none" , "")
						||l.Parse("sound_gus", "gus", "gus" , "true","true" , "false","false" , "")
					) break; else goto syntaxerror;
			case 'r':
				if (0
					||l.ParseRun("run_path", *ref_autoexec)
					||l.ParseRun("run_boot", *ref_autoexec)
					||l.ParseRun("run_mount", *ref_autoexec)
					||l.ParseRun("run_input", *ref_autoexec)
				) break; else goto syntaxerror;
		}
		continue;
		syntaxerror:
		retro_notify(0, RETRO_LOG_ERROR, "Error in DOS.YML: %.*s", (int)(l.Next-l.Key), l.Key);
		continue;
	}
	if (l.cpu_cycles || l.cpu_year || l.cpu_hz)
	{
		if (l.cpu_cycles) {}
		else if (l.cpu_year)
		{
			l.cpu_cycles = (int)(
				(l.cpu_year < 1981 ? 500 : // Very early 8086/8088 CPU
				(l.cpu_year > 1999 ? (500000 + ((l.cpu_year - 2000) * 200000)) : // Pentium III, 600 MHz and later
				Cycles1981to1999[l.cpu_year - 1981]))); // Matching speed for year
		}
		else
		{
			float cycle_per_hz = .3f; // default with auto (just a bad guess)
			switch (*(const char*)control->GetSection("cpu")->GetProp("cputype")->GetValue())
			{
				case 'p': cycle_per_hz = .55700f; break; // Pentium (586):  Mhz * 557.00
				case '4': cycle_per_hz = .38000f; break; // 486:            Mhz * 380.00
				case '3': cycle_per_hz = .18800f; break; // 386:            Mhz * 188.00
				case '2': cycle_per_hz = .09400f; break; // AT (286):       Mhz *  94.00
				case '8': cycle_per_hz = .05828f; break; // XT (8088/8086): Mhz *  58.28
			}
			l.cpu_cycles = (int)(l.cpu_hz * cycle_per_hz + .4999f);
		}
		char buf[32];
		l.ValX = (l.Val = buf) + sprintf(buf, "%d", (int)l.cpu_cycles);
		if (l.Parse(NULL, "cpu", "cycles", "~") && l.cpu_cycles >= 8192) // Switch to dynamic core for newer real mode games 
			{ l.ValX = (l.Val = "dynamic") + 7; l.Parse(NULL, "cpu", "core", "~"); }
	}
}

static void init_dosbox(bool firsttime, bool forcemenu = false, void(*loadcfg)(const std::string&, Section**) = NULL, const std::string* cfg = NULL)
{
	if (loadcfg)
	{
		DBP_ASSERT(dbp_state == DBPSTATE_BOOT && control != NULL && !first_shell);
		delete control;
		DBPArchiveZeroer ar;
		DBPSerialize_All(ar);
	}
	if (dbp_state != DBPSTATE_BOOT)
	{
		DBP_Shutdown();
		DBPArchiveZeroer ar;
		DBPSerialize_All(ar);
		extern const char* RunningProgram;
		RunningProgram = "DOSBOX";
		dbp_crash_message.clear();
		dbp_state = DBPSTATE_BOOT;
		dbp_throttle = { RETRO_THROTTLE_NONE };
		dbp_game_running = dbp_had_game_running = false;
		dbp_last_fastforward = false;
		dbp_serializesize = 0;
		dbp_intercept_gfx = NULL;
		dbp_intercept_input = NULL;
		for (DBP_Image& i : dbp_images) { i.remount = i.mounted; i.mounted = false; }
	}
	if (!dbp_biosreboot) DBP_Run::ResetStartup();
	control = new Config();
	DOSBOX_Init();
	check_variables(true);
	Section* autoexec = control->GetSection("autoexec");
	if (loadcfg) loadcfg(*cfg, &autoexec);
	dbp_boot_time = time_cb();
	control->Init();
	PROGRAMS_MakeFile("PUREMENU.COM", DBP_PureMenuProgram);
	PROGRAMS_MakeFile("LABEL.COM", DBP_PureLabelProgram);
	PROGRAMS_MakeFile("REMOUNT.COM", DBP_PureRemountProgram);

	const char* path = (dbp_content_path.empty() ? NULL : dbp_content_path.c_str());
	const char *path_file, *path_ext; size_t path_namelen;
	if (path && DBP_ExtractPathInfo(path, &path_file, &path_namelen, &path_ext))
	{
		dbp_content_name = std::string(path_file, path_namelen);
	}

	dbp_legacy_save = false;
	std::string save_file = DBP_GetSaveFile(SFT_GAMESAVE); // this can set dbp_legacy_save to true, needed by DBP_Mount
	DOS_Drive* union_underlay = (path ? DBP_Mount(0, false, 0, path) : NULL);

	if (!Drives['C'-'A'])
	{
		if (!union_underlay)
		{
			union_underlay = new memoryDrive();
			if (path) DBP_SetDriveLabelFromContentPath(union_underlay, path, 'C', path_file, path_ext);
		}
		unionDrive* uni = new unionDrive(*union_underlay, (save_file.empty() ? NULL : &save_file[0]), true, dbp_strict_mode);
		Drives['C'-'A'] = uni;
		mem_writeb(Real2Phys(dos.tables.mediaid) + ('C'-'A') * 9, uni->GetMediaByte());
	}

	// Detect content year and auto mapping
	if (firsttime && !loadcfg)
	{
		DBP_PadMapping::Load(); // if loaded don't show auto map notification

		struct Local
		{
			static void FileIter(const char* path, bool is_dir, Bit32u size, Bit16u, Bit16u, Bit8u, Bitu data)
			{
				if (is_dir) return;
				const char* lastslash = strrchr(path, '\\'), *fname = (lastslash ? lastslash + 1 : path);

				// Check mountable disk images on drive C
				const char* fext = (data == ('C'-'A') ? strrchr(fname, '.') : NULL);
				if (fext++)
				{
					bool isFS = (!strcmp(fext, "ISO") || !strcmp(fext, "CHD") || !strcmp(fext, "CUE") || !strcmp(fext, "INS") || !strcmp(fext, "IMG") || !strcmp(fext, "IMA") || !strcmp(fext, "VHD") || !strcmp(fext, "JRC") || !strcmp(fext, "TC"));
					if (isFS && !strncmp(fext, "IM", 2) && (size < 163840 || (size <= 2949120 && (size % 20480) && (size % 20480) != 1024))) isFS = false; //validate floppy images
					if (isFS && !strcmp(fext, "INS"))
					{
						// Make sure this is an actual CUE file with an INS extension
						Bit8u cmd[6];
						if (size >= 16384 || DriveReadFileBytes(Drives[data], path, cmd, (Bit16u)sizeof(cmd)) != sizeof(cmd) || memcmp(cmd, "FILE \"", sizeof(cmd))) isFS = false;
					}
					if (isFS)
					{
						std::string entry;
						entry.reserve(4 + (fext - path) + 4);
						(entry += "$C:\\") += path; // the '$' is for FindAndOpenDosFile
						DBP_AppendImage(entry.c_str(), true);
					}
				}

				if (dbp_auto_mapping) return;
				Bit32u hash = 0x811c9dc5;
				for (const char* p = fname; *p; p++)
					hash = ((hash * 0x01000193) ^ (Bit8u)*p);
				hash ^= (size<<3);

				for (Bit32u idx = hash;; idx++)
				{
					if (!map_keys[idx %= MAP_TABLE_SIZE]) break;
					if (map_keys[idx] != hash) continue;

					static std::vector<Bit8u> static_buf;
					static std::string static_title;

					const MAPBucket& idents_bk = map_buckets[idx % MAP_BUCKETS];

					static_buf.resize(idents_bk.idents_size_uncompressed);
					Bit8u* buf = &static_buf[0];
					zipDrive::Uncompress(idents_bk.idents_compressed, idents_bk.idents_size_compressed, buf, idents_bk.idents_size_uncompressed);

					const Bit8u* ident = buf + (idx / MAP_BUCKETS) * 5;
					const MAPBucket& mappings_bk = map_buckets[ident[0] % MAP_BUCKETS];
					const Bit16u map_offset = (ident[1]<<8) + ident[2];
					const char* map_title = (char*)buf + (MAP_TABLE_SIZE/MAP_BUCKETS) * 5 + (ident[3]<<8) + ident[4];

					dbp_content_year = (Bit16s)(1970 + (Bit8u)map_title[0]);
					if (dbp_auto_mapping_mode == 'f')
						return;

					static_title = "Game: ";
					static_title += map_title + 1;
					dbp_auto_mapping_title = static_title.c_str();

					static_buf.resize(mappings_bk.mappings_size_uncompressed);
					buf = &static_buf[0];
					zipDrive::Uncompress(mappings_bk.mappings_compressed, mappings_bk.mappings_size_compressed, buf, mappings_bk.mappings_size_uncompressed);

					dbp_auto_mapping = buf + map_offset;
					dbp_auto_mapping_names = (char*)buf + mappings_bk.mappings_action_offset;

					if (dbp_auto_mapping_mode == 'n' && !dbp_custom_mapping.size()) //notify
						retro_notify(0, RETRO_LOG_INFO, "Detected Automatic Key %s", static_title.c_str());
					return;
				}
			}
		};

		for (int i = 0; i != ('Z'-'A'); i++)
			if (Drives[i])
				DriveFileIterator(Drives[i], Local::FileIter, i);

		if (dbp_images.size())
		{
			for (size_t i = 0; i != dbp_images.size(); i++)
			{
				// Filter image files that have the same name as a cue file
				const char *imgpath = dbp_images[i].path.c_str(), *imgext = imgpath + dbp_images[i].path.length() - 3;
				if (strcmp(imgext, "CUE") && strcmp(imgext, "INS")) continue;
				for (size_t j = dbp_images.size(); j--;)
				{
					if (i == j || memcmp(dbp_images[j].path.c_str(), imgpath, imgext - imgpath)) continue;
					dbp_images.erase(dbp_images.begin() + j);
					if (i > j) i--;
				}
			}
		}

		if (!dbp_content_year && path)
		{
			// Try to find a year somewhere in the content path, i.e. "Game (1993).zip" or "/DOS/1993/Game.zip"
			for (const char *p = path + strlen(path), *pMin = path + 5; p >= pMin; p--)
			{
				while (p >= pMin && *p != ')' && *p != '/' && *p != '\\') p--;
				if (p < pMin || (p[-5] != '(' && p[-5] != '/' && p[-5] != '\\')) continue;
				int year = (int)atoi(p-4) * ((p[-2]|0x20) == 'x' ? 100 : ((p[-1]|0x20) == 'x' ? 10 : 1));
				if (year > 1970 && year < 2100) { dbp_content_year = (Bit16s)year; break; }
			}
		}
	}

	DOS_Drive* drive_c = Drives['C'-'A']; // guaranteed not NULL
	if (!loadcfg && drive_c->FileExists("DOS.YML"))
	{
		DOS_Drive* drvarr[3] = { drive_c, };
		if (drvarr[0]->GetShadows(drvarr[0], drvarr[2])) drvarr[0]->GetShadows(drvarr[0], drvarr[1]);
		std::string ymlcontent;
		for (DOS_Drive* drv : drvarr)
		{
			DOS_File *df;
			if (!drv || !drv->FileOpen(&df, (char*)"DOS.YML", OPEN_READ)) continue;
			df->AddRef();
			if (ymlcontent.length()) ymlcontent += '\n';
			ReadAndClose(df, ymlcontent);
		}
		if (ymlcontent.length()) return init_dosbox(firsttime, forcemenu, init_dosbox_load_dos_yml, &ymlcontent);
	}

	const bool force_puremenu = (dbp_biosreboot || forcemenu);
	if (!loadcfg && dbp_conf_loading != 'f' && !force_puremenu)
	{
		const char* confpath = NULL; std::string strconfpath, confcontent;
		if (dbp_conf_loading == 'i') // load confs 'i'nside content
		{
			if (drive_c->FileExists("$C:\\DOSBOX.CON"+4)) { confpath = "$C:\\DOSBOX.CON"; } //8.3 filename in ZIPs
			else if (drive_c->FileExists("$C:\\DOSBOX~1.CON"+4)) { confpath = "$C:\\DOSBOX~1.CON"; } //8.3 filename in local file systems
		}
		else if (dbp_conf_loading == 'o' && path) // load confs 'o'utside content
		{
			confpath = strconfpath.assign(path, path_ext - path).append(path_ext[-1] == '.' ? 0 : 1, '.').append("conf").c_str();
		}
		if (confpath && ReadAndClose(FindAndOpenDosFile(confpath), confcontent))
			return init_dosbox(firsttime, forcemenu, init_dosbox_load_dosboxconf, &confcontent);
	}

	// Try to load either DOSBOX.SF2 or a pair of MT32_CONTROL.ROM/MT32_PCM.ROM from the mounted C: drive and use as fixed midi config
	const char* mountedMidi;
	if (drive_c->FileExists((mountedMidi = "$C:\\DOSBOX.SF2")+4) || (drive_c->FileExists("$C:\\MT32_PCM.ROM"+4) && (drive_c->FileExists((mountedMidi = "$C:\\MT32TROL.ROM")+4) || drive_c->FileExists((mountedMidi = "$C:\\MT32_C~1.ROM")+4))))
	{
		Section* sec = control->GetSection("midi");
		Property* prop = sec->GetProp("midiconfig");
		sec->ExecuteDestroy(false);
		prop->SetValue(mountedMidi);
		prop->OnChangedByConfigProgram();
		sec->ExecuteInit(false);
	}

	// Always start network again when it has been used once (or maybe we're restarting to start it up the first time)
	if (dbp_use_network) { dbp_use_network = false; DBP_EnableNetwork(); }

	// Joysticks are refreshed after control modifications but needs to be done here also to happen on core restart
	DBP_PadMapping::RefreshDosJoysticks();

	// If mounted, always switch to the C: drive directly (for puremenu, to run DOSBOX.BAT and to run the autoexec of the dosbox conf)
	// For DBP we modified init_line to always run Z:\AUTOEXEC.BAT and not just any AUTOEXEC.BAT of the current drive/directory
	DOS_SetDrive('C'-'A');

	// Clear any dos errors that could have been set by drive file access until now
	dos.errorcode = DOSERR_NONE;

	if (autoexec)
	{
		bool auto_mount = true;
		autoexec->ExecuteDestroy();
		if (!force_puremenu && dbp_menu_time != (char)-1 && path && (!strcasecmp(path_ext, "EXE") || !strcasecmp(path_ext, "COM") || !strcasecmp(path_ext, "BAT")))
		{
			((((((static_cast<Section_line*>(autoexec)->data += "echo off") += '\n') += ((path_ext[0]|0x20) == 'b' ? "call " : "")) += path_file) += '\n') += "Z:PUREMENU") += " -FINISH\n";
		}
		else if (!force_puremenu && drive_c->FileExists("DOSBOX.BAT"))
		{
			((static_cast<Section_line*>(autoexec)->data += '@') += "DOSBOX.BAT") += '\n';
			auto_mount = false;
		}
		else
		{
			// Boot into puremenu, it will take care of further auto start options
			((((static_cast<Section_line*>(autoexec)->data += "echo off") += '\n') += "Z:PUREMENU") += ((!force_puremenu || dbp_biosreboot) ? " -BOOT" : "")) += '\n';
		}
		autoexec->ExecuteInit();

		// Mount the first found cdrom image as well as the first found floppy, or reinsert them on core restart (and keep selected index)
		unsigned active_disk_image_index = dbp_image_index;
		for (unsigned i = 0; auto_mount && i != (unsigned)dbp_images.size(); i++)
			if (!firsttime && (dbp_images[i].path[0] == '$' && !Drives[dbp_images[i].path[1]-'A']))
				dbp_images.erase(dbp_images.begin() + (i--));
			else if (firsttime || dbp_images[i].remount)
				DBP_Mount(i, dbp_images[i].remount);
		if (!firsttime) dbp_image_index = (active_disk_image_index >= dbp_images.size() ? 0 : active_disk_image_index);
	}
	dbp_biosreboot = false;
	DBP_ReportCoreMemoryMaps();

	struct Local
	{
		static Thread::RET_t THREAD_CC ThreadDOSBox(void*)
		{
			control->StartUp();
			DBP_ThreadControl(TCM_ON_SHUTDOWN);
			return 0;
		}
	};

	// Start DOSBox thread
	dbp_frame_pending = true;
	dbp_state = DBPSTATE_FIRST_FRAME;
	Thread::StartDetached(Local::ThreadDOSBox);
}

void retro_init(void) //#3
{
	static const struct { unsigned short retroID; unsigned char dosboxID; } keymap[] =
	{
		{RETROK_1, KBD_1}, {RETROK_2, KBD_2}, {RETROK_3, KBD_3}, {RETROK_4, KBD_4},
		{RETROK_5, KBD_5}, {RETROK_6, KBD_6}, {RETROK_7, KBD_7}, {RETROK_8, KBD_8},
		{RETROK_9, KBD_9}, {RETROK_0, KBD_0}, {RETROK_a, KBD_a}, {RETROK_b, KBD_b},
		{RETROK_c, KBD_c}, {RETROK_d, KBD_d}, {RETROK_e, KBD_e}, {RETROK_f, KBD_f},
		{RETROK_g, KBD_g}, {RETROK_h, KBD_h}, {RETROK_i, KBD_i}, {RETROK_j, KBD_j},
		{RETROK_k, KBD_k}, {RETROK_l, KBD_l}, {RETROK_m, KBD_m}, {RETROK_n, KBD_n},
		{RETROK_o, KBD_o}, {RETROK_p, KBD_p}, {RETROK_q, KBD_q}, {RETROK_r, KBD_r},
		{RETROK_s, KBD_s}, {RETROK_t, KBD_t}, {RETROK_u, KBD_u}, {RETROK_v, KBD_v},
		{RETROK_w, KBD_w}, {RETROK_x, KBD_x}, {RETROK_y, KBD_y}, {RETROK_z, KBD_z},
		{RETROK_F1, KBD_f1}, {RETROK_F2, KBD_f2}, {RETROK_F3, KBD_f3}, {RETROK_F4, KBD_f4},
		{RETROK_F5, KBD_f5}, {RETROK_F6, KBD_f6}, {RETROK_F7, KBD_f7}, {RETROK_F8, KBD_f8},
		{RETROK_F9, KBD_f9}, {RETROK_F10, KBD_f10}, {RETROK_F11, KBD_f11}, {RETROK_F12, KBD_f12},
		{RETROK_ESCAPE, KBD_esc}, {RETROK_TAB, KBD_tab}, {RETROK_BACKSPACE, KBD_backspace},
		{RETROK_RETURN, KBD_enter}, {RETROK_SPACE, KBD_space}, {RETROK_LALT, KBD_leftalt},
		{RETROK_RALT, KBD_rightalt}, {RETROK_LCTRL, KBD_leftctrl}, {RETROK_RCTRL, KBD_rightctrl},
		{RETROK_LSHIFT, KBD_leftshift}, {RETROK_RSHIFT, KBD_rightshift}, {RETROK_CAPSLOCK, KBD_capslock},
		{RETROK_SCROLLOCK, KBD_scrolllock}, {RETROK_NUMLOCK, KBD_numlock}, {RETROK_MINUS, KBD_minus},
		{RETROK_EQUALS, KBD_equals}, {RETROK_BACKSLASH, KBD_backslash}, {RETROK_LEFTBRACKET, KBD_leftbracket},
		{RETROK_RIGHTBRACKET, KBD_rightbracket}, {RETROK_SEMICOLON, KBD_semicolon}, {RETROK_QUOTE, KBD_quote},
		{RETROK_PERIOD, KBD_period}, {RETROK_COMMA, KBD_comma}, {RETROK_SLASH, KBD_slash},
		{RETROK_PRINT, KBD_printscreen}, {RETROK_SYSREQ, KBD_printscreen}, {RETROK_PAUSE, KBD_pause},
		{RETROK_INSERT, KBD_insert}, {RETROK_HOME, KBD_home}, {RETROK_PAGEUP, KBD_pageup},
		{RETROK_PAGEDOWN, KBD_pagedown}, {RETROK_DELETE, KBD_delete}, {RETROK_END, KBD_end},
		{RETROK_LEFT, KBD_left}, {RETROK_UP, KBD_up}, {RETROK_DOWN, KBD_down}, {RETROK_RIGHT, KBD_right},
		{RETROK_KP1, KBD_kp1}, {RETROK_KP2, KBD_kp2}, {RETROK_KP3, KBD_kp3}, {RETROK_KP4, KBD_kp4},
		{RETROK_KP5, KBD_kp5}, {RETROK_KP6, KBD_kp6}, {RETROK_KP7, KBD_kp7}, {RETROK_KP8, KBD_kp8},
		{RETROK_KP9, KBD_kp9}, {RETROK_KP0, KBD_kp0}, {RETROK_KP_DIVIDE, KBD_kpdivide},
		{RETROK_KP_MULTIPLY, KBD_kpmultiply}, {RETROK_KP_MINUS, KBD_kpminus},
		{RETROK_KP_PLUS, KBD_kpplus}, {RETROK_KP_ENTER, KBD_kpenter}, {RETROK_KP_PERIOD, KBD_kpperiod},
		{RETROK_BACKQUOTE, KBD_grave}
	};
	for (int i = 0; i != (sizeof(keymap)/sizeof(keymap[0])); i++)
	{
		dbp_keymap_dos2retro[keymap[i].dosboxID] = keymap[i].retroID;
		dbp_keymap_retro2dos[keymap[i].retroID] = keymap[i].dosboxID;
	}

	struct CallBacks
	{
		static void RETRO_CALLCONV keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
		{
			// This can be called from another thread. Hopefully we can get away without a mutex in DBP_QueueEvent.
			int val = dbp_keymap_retro2dos[keycode];
			if (!val) return;
			if (down && !dbp_keys_down[val])
			{
				dbp_keys_down[val] |= DBP_DOWN_BY_KEYBOARD;
				DBP_QueueEvent(DBPET_KEYDOWN, val);
			}
			else if (!down && (dbp_keys_down[val] & DBP_DOWN_BY_KEYBOARD))
			{
				dbp_keys_down[val] = 1;
				DBP_QueueEvent(DBPET_KEYUP, val);
			}
		}

		static bool RETRO_CALLCONV set_eject_state(bool ejected)
		{
			if (dbp_images.size() == 0) return ejected;
			if (dbp_images[dbp_image_index].mounted != ejected) return true;
			DBP_ThreadControl(TCM_PAUSE_FRAME);
			if (ejected)
				DBP_Unmount(dbp_images[dbp_image_index].drive);
			else
				DBP_Mount(dbp_image_index, true);
			DBP_SetMountSwappingRequested(); // set swapping_requested flag for CMscdex::GetMediaStatus
			DBP_ThreadControl(TCM_RESUME_FRAME);
			DBP_QueueEvent(DBPET_CHANGEMOUNTS);
			return true;
		}

		static bool RETRO_CALLCONV get_eject_state()
		{
			if (dbp_images.size() == 0) return true;
			return !dbp_images[dbp_image_index].mounted;
		}

		static unsigned RETRO_CALLCONV get_image_index()
		{
			return dbp_image_index;
		}

		static bool RETRO_CALLCONV set_image_index(unsigned index)
		{
			if (index >= dbp_images.size()) return false;
			dbp_image_index = index;
			return true;
		}

		static unsigned RETRO_CALLCONV get_num_images()
		{
			return (unsigned)dbp_images.size();
		}

		static bool RETRO_CALLCONV replace_image_index(unsigned index, const struct retro_game_info *info)
		{
			if (index >= dbp_images.size()) return false;
			if (dbp_images[dbp_image_index].mounted)
				DBP_Unmount(dbp_images[dbp_image_index].drive);
			if (info == NULL)
			{
				if (dbp_image_index > index) dbp_image_index--;
				dbp_images.erase(dbp_images.begin() + index);
				if (dbp_image_index == dbp_images.size()) dbp_image_index--;
			}
			else
			{
				dbp_images[index].path = info->path;
			}
			return true;
		}

		static bool RETRO_CALLCONV add_image_index()
		{
			dbp_images.resize(dbp_images.size() + 1);
			return true;
		}

		static bool RETRO_CALLCONV set_initial_image(unsigned index, const char *path)
		{
			return true;
		}

		static bool RETRO_CALLCONV get_image_path(unsigned index, char *path, size_t len)
		{
			if (index >= dbp_images.size()) return false;
			safe_strncpy(path, dbp_images[index].path.c_str(), len);
			return true;
		}

		static bool RETRO_CALLCONV get_image_label(unsigned index, char *label, size_t len)
		{
			if (index >= dbp_images.size()) return false;
			safe_strncpy(label, DBP_Image_Label(dbp_images[index]), len);
			return true;
		}

		static bool RETRO_CALLCONV options_update_display(void)
		{
			// because we read variables here, clear variable dirty flag in frontend
			bool variable_update; environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &variable_update);
			return check_variables();
		}
	};

	struct retro_log_callback logging;
	log_cb = (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging) ? logging.log : retro_fallback_log);
	#ifdef ANDROID
	log_cb = (void (*)(enum retro_log_level, const char *, ...))AndroidLogFallback;
	#endif

	static const struct retro_keyboard_callback kc = { CallBacks::keyboard_event };
	environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void*)&kc);

	static const struct retro_core_options_update_display_callback coudc = { CallBacks::options_update_display };
	dbp_optionsupdatecallback = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, (void*)&coudc);

	static const retro_disk_control_ext_callback disk_control_callback =
	{
		// Don't set get_image_path to avoid RetroArch making ldci files
		CallBacks::set_eject_state, CallBacks::get_eject_state,
		CallBacks::get_image_index, CallBacks::set_image_index, 
		CallBacks::get_num_images,  CallBacks::replace_image_index,
		CallBacks::add_image_index, CallBacks::set_initial_image,
		NULL/*CallBacks::get_image_path*/, CallBacks::get_image_label,
	};
	if (!environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void*)&disk_control_callback))
		environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, (void*)&disk_control_callback);

	void DBP_Network_SetCallbacks(retro_environment_t envcb);
	DBP_Network_SetCallbacks(environ_cb);

	struct retro_perf_callback perf;
	if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf) && perf.get_time_usec) time_cb = perf.get_time_usec;

	// Set default port modes
	dbp_port_mode[0] = dbp_port_mode[1] = dbp_port_mode[2] = dbp_port_mode[3] = DBP_PadMapping::MODE_MAPPER;

	set_variables();
}

bool retro_load_game(const struct retro_game_info *info) //#4
{
	enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format))
	{
		retro_notify(0, RETRO_LOG_ERROR, "Frontend does not support XRGB8888.\n");
		return false;
	}
	
	//// RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK crashes RetroArch with the XAudio driver when launching from the command line
	//// Also it explicitly doesn't support save state rewinding when used so give up on this for now.
	//struct CallBacks
	//{
	//	static void RETRO_CALLCONV audio_callback(void)
	//	{
	//		Bit32u mix_samples = 256;
	//		if (mix_samples > DBP_MIXER_DoneSamplesCount()) mix_samples = DBP_MIXER_DoneSamplesCount();
	//		dbp_audiomutex.Lock();
	//		MIXER_CallBack(0, audioData, mix_samples * 4);
	//		dbp_audiomutex.Unlock();
	//		audio_batch_cb((int16_t*)audioData, mix_samples);
	//	}
	//	static void RETRO_CALLCONV audio_set_state_callback(bool enabled) { }
	//};
	//static const struct retro_audio_callback rac = { CallBacks::audio_callback, CallBacks::audio_set_state_callback };
	//bool use_audio_callback = environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, (void*)&rac);

	if (info && info->path && *info->path) dbp_content_path = info->path;
	init_dosbox(true);

	bool support_achievements = true;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &support_achievements);

	DBP_PadMapping::RefreshInputBinds(true);

	return true;
}

void retro_get_system_av_info(struct retro_system_av_info *info) // #5
{
	DBP_ASSERT(dbp_state != DBPSTATE_BOOT);
	av_info.geometry.max_width = SCALER_MAXWIDTH;
	av_info.geometry.max_height = SCALER_MAXHEIGHT;
	DBP_ThreadControl(TCM_FINISH_FRAME);
	if (dbp_biosreboot || dbp_state == DBPSTATE_EXITED)
	{
		// A reboot can happen during the first frame if puremenu wants to change DOSBox machine config
		DBP_ASSERT(dbp_biosreboot && dbp_state == DBPSTATE_EXITED);
		DBP_ForceReset();
		DBP_ThreadControl(TCM_FINISH_FRAME);
		DBP_ASSERT(!dbp_biosreboot && dbp_state == DBPSTATE_FIRST_FRAME);
	}
	DBP_ASSERT(render.src.fps > 10.0); // validate initialized video mode after first frame
	const DBP_Buffer& buf = dbp_buffers[buffer_active];
	av_info.geometry.base_width = buf.width;
	av_info.geometry.base_height = buf.height;
	av_info.geometry.aspect_ratio = buf.ratio;
	av_info.timing.fps = DBP_GetFPS();
	av_info.timing.sample_rate = DBP_MIXER_GetFrequency();
	*info = av_info;
}

void retro_unload_game(void)
{
	DBP_Shutdown();
}

void retro_set_controller_port_device(unsigned port, unsigned device) //#5
{
	DBP_PadMapping::SetPortMode(port, device);
}

void retro_reset(void)
{
	// Calling input_state_cb before the first frame can be fatal (RetroArch would crash), but during retro_reset it should be fine
	init_dosbox(false, input_state_cb && (
		input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LSHIFT) ||
		input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RSHIFT) ||
		input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2) ||
		input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2)));
}

void retro_run_touchpad(bool has_press, Bit16s absx, Bit16s absy)
{
	static Bit8u last_presses, down_btn, is_move, is_tap;
	static Bit16s lastx, lasty, remx, remy;
	static Bit32u press_tick, down_tick;
	Bit32u tick = DBP_GetTicks();
	Bit8u presses = 0;
	if (has_press) for (presses = 1; presses < 3; presses++) if (!input_state_cb(0, RETRO_DEVICE_POINTER, presses, RETRO_DEVICE_ID_POINTER_PRESSED)) break;
	if (last_presses != presses)
	{
		const bool add_press = (presses > last_presses);
		if (add_press)
			press_tick = tick;
		if (!down_tick && !add_press && press_tick && (!is_move || presses))
			{ down_tick = tick; is_tap = true; down_btn = presses; DBP_QueueEvent(DBPET_MOUSEDOWN, down_btn); press_tick = 0; }
		else if (down_tick && (!presses || add_press))
			{ DBP_QueueEvent(DBPET_MOUSEUP, down_btn); down_tick = 0; }
		if (!presses)
			is_move = false;
		if (!last_presses || !add_press)
			lastx = absx, lasty = absy, remx = remy = 0;
		last_presses = presses;
	}
	if (presses == 1 && (absx != lastx || absy != lasty))
	{
		int dx = absx - lastx, dy = absy - lasty;
		if (is_move || abs(dx) >= 256 || abs(dy) >= 256)
		{
			lastx = absx; int tx = dx + dbp_mouse_x; dbp_mouse_x = (Bit16s)(tx < -32768 ? -32768 : (tx > 32767 ? 32767 : tx)); dx += remx; remx = (Bit16s)(dx % 32);
			lasty = absy; int ty = dy + dbp_mouse_y; dbp_mouse_y = (Bit16s)(ty < -32768 ? -32768 : (ty > 32767 ? 32767 : ty)); dy += remy; remy = (Bit16s)(dy % 32);
			DBP_QueueEvent(DBPET_MOUSEMOVE, dx / 32, dy / 32);
			is_move = true;
		}
	}
	if (!down_tick && presses && !is_move && press_tick && (tick - press_tick) >= 500)
		{ down_tick = tick; is_tap = false; down_btn = presses - 1; DBP_QueueEvent(DBPET_MOUSEDOWN, down_btn); }
	else if (down_tick && is_tap && (tick - down_tick) >= 100)
		{ DBP_QueueEvent(DBPET_MOUSEUP, down_btn); down_tick = 0; }
}

void retro_run(void)
{
	#ifdef DBP_ENABLE_FPS_COUNTERS
	DBP_FPSCOUNT(dbp_fpscount_retro)
	uint32_t curTick = DBP_GetTicks();
	if (curTick - dbp_lastfpstick >= 1000)
	{
		double fpsf = 1000.0 / (double)(curTick - dbp_lastfpstick), gfxf = fpsf * (render.frameskip.max < 1 ? 1 : render.frameskip.max);
		log_cb(RETRO_LOG_INFO, "[DBP FPS] RETRO: %3.2f - GFXSTART: %3.2f - GFXEND: %3.2f - EVENT: %5.1f - EMULATED: %3.2f - CyclesMax: %d\n",
			dbp_fpscount_retro * fpsf, dbp_fpscount_gfxstart * gfxf, dbp_fpscount_gfxend * gfxf, dbp_fpscount_event * fpsf, render.src.fps, CPU_CycleMax);
		dbp_lastfpstick = (curTick - dbp_lastfpstick >= 1500 ? curTick : dbp_lastfpstick + 1000);
		dbp_fpscount_retro = dbp_fpscount_gfxstart = dbp_fpscount_gfxend = dbp_fpscount_event = 0;
	}
	#endif

	if (dbp_state < DBPSTATE_RUNNING)
	{
		if (dbp_state == DBPSTATE_EXITED || dbp_state == DBPSTATE_SHUTDOWN || dbp_state == DBPSTATE_REBOOT)
		{
			DBP_Buffer& buf = dbp_buffers[buffer_active];
			if (!dbp_crash_message.empty()) // unexpected shutdown
				DBP_Shutdown();
			else if (dbp_state == DBPSTATE_REBOOT || dbp_biosreboot)
				DBP_ForceReset();
			else if (dbp_state == DBPSTATE_EXITED) // expected shutdown
			{
				// On statically linked platforms shutdown would exit the frontend, so don't do that. Just tint the screen red and sleep
				#ifndef STATIC_LINKING
				if (dbp_menu_time >= 0 && dbp_menu_time < 99) // only auto shut down for users that want auto shut down in general
				{
					environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
				}
				else
				#endif
				{
					for (Bit8u *p = (Bit8u*)buf.video, *pEnd = p + sizeof(buf.video); p < pEnd; p += 56) p[2] = 255;
					retro_sleep(10);
				}
			}

			// submit last frame
			Bit32u numEmptySamples = (Bit32u)(av_info.timing.sample_rate / av_info.timing.fps);
			memset(dbp_audio, 0, numEmptySamples * 4);
			audio_batch_cb(dbp_audio, numEmptySamples);
			video_cb(buf.video, buf.width, buf.height, buf.width * 4);
			return;
		}

		DBP_ASSERT(dbp_state == DBPSTATE_FIRST_FRAME);
		DBP_ThreadControl(TCM_FINISH_FRAME);
		DBP_ASSERT(dbp_state == DBPSTATE_FIRST_FRAME || (dbp_state == DBPSTATE_EXITED && dbp_biosreboot));
		if (MIDI_Retro_HasOutputIssue())
			retro_notify(0, RETRO_LOG_WARN, "The frontend MIDI output is not set up correctly");
		if (dbp_state == DBPSTATE_FIRST_FRAME)
			dbp_state = DBPSTATE_RUNNING;
		if (dbp_latency == DBP_LATENCY_VARIABLE)
		{
			DBP_ThreadControl(TCM_NEXT_FRAME);
			dbp_targetrefreshrate = 0; // force refresh this because only in retro_run RetroArch will return the correct value
		}
	}

	if (!environ_cb(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &dbp_throttle))
	{
		bool fast_forward = false;
		if (environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast_forward) && fast_forward)
			dbp_throttle = { RETRO_THROTTLE_FAST_FORWARD, 0.0f };
		else
			dbp_throttle = { RETRO_THROTTLE_NONE, (float)av_info.timing.fps };
	}

	static retro_throttle_state throttle_last;
	if (dbp_throttle.mode != throttle_last.mode || dbp_throttle.rate != throttle_last.rate)
	{
		static const char* throttle_modes[] = { "NONE", "FRAME_STEPPING", "FAST_FORWARD", "SLOW_MOTION", "REWINDING", "VSYNC", "UNBLOCKED" };
		log_cb(RETRO_LOG_INFO, "[DBP THROTTLE] %s %f -> %s %f\n", throttle_modes[throttle_last.mode], throttle_last.rate, throttle_modes[dbp_throttle.mode], dbp_throttle.rate);
		throttle_last = dbp_throttle;
	}

	bool variable_update = false;
	if (!dbp_optionsupdatecallback && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &variable_update) && variable_update)
		check_variables();

	// Use fixed mappings using only port 0 to use in the menu and the on-screen keyboard
	static DBP_InputBind intercept_binds[] =
	{
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   DBPET_MOUSEDOWN, 0 },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  DBPET_MOUSEDOWN, 1 },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, DBPET_MOUSEDOWN, 2 },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,   DBPET_KEYDOWN, KBD_kpminus },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN, DBPET_KEYDOWN, KBD_kpplus  },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, DBPET_ONSCREENKEYBOARD },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    DBPET_KEYDOWN, KBD_up },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  DBPET_KEYDOWN, KBD_down },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  DBPET_KEYDOWN, KBD_left },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, DBPET_KEYDOWN, KBD_right },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, DBPET_JOY1X },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, DBPET_JOY1Y },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, DBPET_JOY2X },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, DBPET_JOY2Y },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, DBPET_JOY1DOWN, 0 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, DBPET_JOY1DOWN, 1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, DBPET_JOY2DOWN, 0 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, DBPET_JOY2DOWN, 1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, DBPET_KEYDOWN, KBD_grave },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, DBPET_KEYDOWN, KBD_tab },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, DBPET_KEYDOWN, KBD_esc },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  DBPET_KEYDOWN, KBD_enter },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, DBPET_MOUSESETSPEED,  1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, DBPET_MOUSESETSPEED, -1 },
	};
	static bool use_input_intercept;
	bool toggled_intercept = (use_input_intercept != !!dbp_intercept_input);
	if (toggled_intercept)
	{
		use_input_intercept ^= 1;
		if (!use_input_intercept) for (DBP_InputBind* b = intercept_binds; b != &intercept_binds[sizeof(intercept_binds)/sizeof(*intercept_binds)]; b++)
		{
			// Release all pressed events when leaving intercepted screen
			DBP_ASSERT(b->evt != DBPET_AXISMAPPAIR);
			if (!b->lastval) continue;
			if (b->evt <= _DBPET_JOY_AXIS_MAX) DBP_QueueEvent((DBP_Event_Type)b->evt, 0);
			else DBP_QueueEvent((DBP_Event_Type)(b->evt + 1), b->meta);
			b->lastval = 0;
		}
		if (dbp_input_binds_modified) DBP_PadMapping::RefreshInputBinds(false);
	}
	DBP_InputBind *binds = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *binds_end = binds + dbp_input_binds.size();
	input_poll_cb();
	if (use_input_intercept)
	{
		//input_state_cb(0, RETRO_DEVICE_NONE, 0, 0); // poll keys? 
		//input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_SPACE); // get latest keyboard callbacks?
		if (toggled_intercept)
			for (DBP_InputBind* b = intercept_binds; b != &intercept_binds[sizeof(intercept_binds)/sizeof(*intercept_binds)]; b++)
				b->lastval = input_state_cb(b->port, b->device, b->index, b->id);
		binds = intercept_binds + ((dbp_mouse_input != 'f') ? ((dbp_mouse_input != 'p') ? 0 : 3) : 5);
		binds_end = &intercept_binds[sizeof(intercept_binds)/sizeof(*intercept_binds)];

		static bool warned_game_focus;
		if (!dbp_intercept_gfx && !warned_game_focus && input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK))
		{
			for (Bit8u i = KBD_NONE + 1; i != KBD_LAST; i++)
			{
				if (!input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, dbp_keymap_dos2retro[i])) continue;
				warned_game_focus = true;
				retro_notify(10000, RETRO_LOG_WARN,
					"Detected keyboard and joypad being pressed at the same time.\n"
					"To freely use the keyboard without hotkeys enable 'Game Focus' (Scroll Lock key by default) if available.");
				break;
			}
		}
		if (dbp_input_binds_modified) DBP_PadMapping::RefreshInputBinds(false);
	}

	// query mouse movement before querying mouse buttons
	if (dbp_mouse_input != 'f')
	{
		int16_t movx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
		int16_t movy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
		int16_t absx = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
		int16_t absy = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
		int16_t prss = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
		bool absvalid = (absx || absy || prss);
		if (dbp_mouse_input == 'p')
			retro_run_touchpad(!!prss, absx, absy);
		else if (movx || movy || (absvalid && (absx != dbp_mouse_x || absy != dbp_mouse_y)))
		{
			//log_cb(RETRO_LOG_INFO, "[DOSBOX MOUSE] [%4d@%6d] Rel: %d,%d - Abs: %d,%d - Press: %d - Count: %d\n", dbp_framecount, DBP_GetTicks(), movx, movy, absx, absy, prss, input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_COUNT));
			if (absvalid) dbp_mouse_x = absx, dbp_mouse_y = absy;
			DBP_QueueEvent(DBPET_MOUSEMOVE, movx, movy);
		}
	}
	// query buttons mapped to analog functions
	if (dbp_analog_buttons && !use_input_intercept)
	{
		for (DBP_InputBind *b = binds; b != binds_end; b++)
		{
			if (b->evt > _DBPET_JOY_AXIS_MAX || b->device != RETRO_DEVICE_JOYPAD) continue; // handled below
			DBP_ASSERT(b->meta == 1 || b->meta == -1); // buttons mapped to analog functions should always have 1 or -1 in meta
			Bit16s val = input_state_cb(b->port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, b->id);
			if (!val) val = (input_state_cb(b->port, RETRO_DEVICE_JOYPAD, 0, b->id) ? (Bit16s)32767 : (Bit16s)0); // old frontend fallback
			if (val == b->lastval) continue;
			b->lastval = val; // set before calling DBP_QueueEvent
			DBP_QueueEvent((DBP_Event_Type)b->evt, val * b->meta, 0, binds, binds_end);
		}
	}
	// query input states and generate input events
	for (DBP_InputBind *b = binds; b != binds_end; b++)
	{
		Bit16s val = input_state_cb(b->port, b->device, b->index, b->id), lastval = b->lastval;
		if (val == lastval) continue;
		b->lastval = val; // set before calling DBP_QueueEvent
		if (b->evt <= _DBPET_JOY_AXIS_MAX)
		{
			// handle analog axis mapped to analog functions
			if (b->device == RETRO_DEVICE_JOYPAD) { b->lastval = lastval; continue; } // handled above
			DBP_ASSERT(b->meta == 0); // analog axis mapped to analog functions should always have 0 in meta
			DBP_QueueEvent((DBP_Event_Type)b->evt, (b->meta ? (val ? 32767 : 0) * b->meta : val), 0, binds, binds_end);
		}
		else if (b->device != RETRO_DEVICE_ANALOG)
		{
			// if button is pressed, send the _DOWN, otherwise send _UP
			DBP_QueueEvent((DBP_Event_Type)(val ? b->evt : b->evt + 1), b->meta);
		}
		else for (Bit16s dir = 1; dir >= -1; dir -= 2)
		{
			DBP_ASSERT(b->evt == DBPET_AXISMAPPAIR);
			Bit16s map = DBP_MAPPAIR_GET(dir, b->meta), dirval = val * dir, dirlastval = lastval * dir;
			if (map == KBD_NONE) continue;
			if (map < KBD_LAST)
			{
				if (dirval >= 12000 && dirlastval <  12000) DBP_QueueEvent(DBPET_KEYDOWN, map);
				if (dirval <  12000 && dirlastval >= 12000) DBP_QueueEvent(DBPET_KEYUP,   map);
				continue;
			}
			if (map < DBP_SPECIALMAPPINGS_KEY) { DBP_ASSERT(false); continue; }
			if (dirval <= 0 && dirlastval <= 0) continue;
			const DBP_SpecialMapping& sm = DBP_SPECIALMAPPING(map);
			if (sm.evt <= _DBPET_JOY_AXIS_MAX)               DBP_QueueEvent((DBP_Event_Type)sm.evt, (dirval < 0 ? 0 : dirval) * sm.meta, 0, binds, binds_end);
			else if (dirval >= 12000 && dirlastval <  12000) DBP_QueueEvent((DBP_Event_Type)sm.evt, sm.meta);
			else if (dirval <  12000 && dirlastval >= 12000) DBP_QueueEvent((DBP_Event_Type)(sm.evt + 1), sm.meta);
		}
	}
	if (dbp_keys_down_count)
	{
		// This catches sticky keys due to various frontend/driver issues
		// For example ALT key can easily get stuck when using ALT-TAB, menu opening or fast forwarding also can get stuck
		// We also release all keys when switching between key event intercepting
		for (Bit8u i = KBD_NONE + 1; i != KBD_LAST; i++)
		{
			if (!dbp_keys_down[i] || (!(dbp_keys_down[i] & DBP_DOWN_BY_KEYBOARD) && !toggled_intercept)) continue;
			if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, dbp_keymap_dos2retro[i])) continue;
			dbp_keys_down[i] = 1;
			DBP_QueueEvent(DBPET_KEYUP, i);
		}
	}

	bool skip_emulate = DBP_NeedFrameSkip(false);
	switch (dbp_latency)
	{
		case DBP_LATENCY_DEFAULT:
			DBP_ThreadControl(skip_emulate ? TCM_PAUSE_FRAME : TCM_FINISH_FRAME);
			break;
		case DBP_LATENCY_LOW:
			if (skip_emulate) break;
			if (!dbp_frame_pending) DBP_ThreadControl(TCM_NEXT_FRAME);
			DBP_ThreadControl(TCM_FINISH_FRAME);
			break;
		case DBP_LATENCY_VARIABLE:
			dbp_lastrun = time_cb();
			break;
	}

	Bit32u tpfActual = 0, tpfTarget = 0, tpfDraws = 0;
	#ifdef DBP_ENABLE_WAITSTATS
	Bit32u waitPause = 0, waitFinish = 0, waitPaused = 0, waitContinue = 0;
	#endif
	if (dbp_perf && dbp_perf_totaltime > 1000000)
	{
		tpfActual = dbp_perf_totaltime / dbp_perf_count;
		tpfTarget = (Bit32u)(1000000.f / render.src.fps);
		tpfDraws = dbp_perf_uniquedraw;
		#ifdef DBP_ENABLE_WAITSTATS
		waitPause = dbp_wait_pause / dbp_perf_count, waitFinish = dbp_wait_finish / dbp_perf_count, waitPaused = dbp_wait_paused / dbp_perf_count, waitContinue = dbp_wait_continue / dbp_perf_count;
		dbp_wait_pause = dbp_wait_finish = dbp_wait_paused = dbp_wait_continue = 0;
		#endif
		dbp_perf_uniquedraw = dbp_perf_count = dbp_perf_emutime = dbp_perf_totaltime = 0;
	}

	// mix audio
	Bit32u haveSamples = DBP_MIXER_DoneSamplesCount(), mixSamples = 0; double numSamples;
	if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD && dbp_throttle.rate < 1)
		numSamples = haveSamples;
	else if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION || dbp_throttle.rate < 1)
		numSamples = (av_info.timing.sample_rate / av_info.timing.fps) + dbp_audio_remain;
	else
		numSamples = (av_info.timing.sample_rate / dbp_throttle.rate) + dbp_audio_remain;
	if (numSamples && haveSamples > numSamples * .99) // Allow 1 percent stretch on underrun
	{
		mixSamples = (numSamples > haveSamples ? haveSamples : (Bit32u)numSamples);
		dbp_audio_remain = ((numSamples <= mixSamples || numSamples > haveSamples) ? 0.0 : (numSamples - mixSamples));
		if (mixSamples > DBP_MAX_SAMPLES) mixSamples = DBP_MAX_SAMPLES;
		if (dbp_latency == DBP_LATENCY_VARIABLE)
		{
			if (dbp_pause_events) DBP_ThreadControl(TCM_RESUME_FRAME); // can be paused by serialize
			while (DBP_MIXER_DoneSamplesCount() < mixSamples * 12 / 10) { dbp_lastrun = time_cb(); retro_sleep(0); } // buffer ahead a bit
			DBP_ThreadControl(TCM_PAUSE_FRAME); 
		}
		MIXER_CallBack(0, (Bit8u*)dbp_audio, mixSamples * 4);
		if (dbp_latency == DBP_LATENCY_VARIABLE)
		{
			DBP_ThreadControl(TCM_RESUME_FRAME);
		}
	}

	// Read buffer_active before waking up emulation thread
	const DBP_Buffer& buf = dbp_buffers[buffer_active];

	if (dbp_latency == DBP_LATENCY_DEFAULT)
	{
		DBP_ThreadControl(skip_emulate ? TCM_RESUME_FRAME : TCM_NEXT_FRAME);
	}

	// submit audio
	//log_cb(RETRO_LOG_INFO, "[retro_run] Submit %d samples (remain %f) - Had: %d - Left: %d\n", mixSamples, dbp_audio_remain, haveSamples, DBP_MIXER_DoneSamplesCount());
	if (mixSamples)
	{
		if (dbp_swapstereo) for (int16_t *p = dbp_audio, *pEnd = p + mixSamples*2; p != pEnd; p += 2) std::swap(p[0], p[1]);
		audio_batch_cb(dbp_audio, mixSamples);
	}

	if (tpfActual)
	{
		extern const char* DBP_CPU_GetDecoderName();
		if (dbp_perf == DBP_PERF_DETAILED)
			retro_notify(-1500, RETRO_LOG_INFO, "Speed: %4.1f%%, DOS: %dx%d@%4.2ffps, Actual: %4.2ffps, Drawn: %dfps, Cycles: %u (%s)"
				#ifdef DBP_ENABLE_WAITSTATS
				", Waits: p%u|f%u|z%u|c%u"
				#endif
				, ((float)tpfTarget / (float)tpfActual * 100), (int)render.src.width, (int)render.src.height, render.src.fps, (1000000.f / tpfActual), tpfDraws, CPU_CycleMax, DBP_CPU_GetDecoderName()
				#ifdef DBP_ENABLE_WAITSTATS
				, waitPause, waitFinish, waitPaused, waitContinue
				#endif
				);
		else
			retro_notify(-1500, RETRO_LOG_INFO, "Emulation Speed: %4.1f%%",
				((float)tpfTarget / (float)tpfActual * 100));
	}

	// handle video mode changes
	double targetfps = DBP_GetFPS();
	if (av_info.geometry.base_width != buf.width || av_info.geometry.base_height != buf.height || av_info.geometry.aspect_ratio != buf.ratio || av_info.timing.fps != targetfps)
	{
		log_cb(RETRO_LOG_INFO, "[DOSBOX] Resolution changed %ux%u @ %.3fHz AR: %.5f => %ux%u @ %.3fHz AR: %.5f\n",
			av_info.geometry.base_width, av_info.geometry.base_height, av_info.timing.fps, av_info.geometry.aspect_ratio,
			buf.width, buf.height, av_info.timing.fps, buf.ratio);
		bool newfps = (av_info.timing.fps != targetfps);
		av_info.geometry.base_width = buf.width;
		av_info.geometry.base_height = buf.height;
		av_info.geometry.aspect_ratio = buf.ratio;
		av_info.timing.fps = targetfps;
		environ_cb((newfps ? RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO : RETRO_ENVIRONMENT_SET_GEOMETRY), &av_info);
	}

	// submit video
	video_cb(buf.video, buf.width, buf.height, buf.width * 4);
}

static bool retro_serialize_all(DBPArchive& ar, bool unlock_thread)
{
	if (dbp_serializemode == DBPSERIALIZE_DISABLED) return false;
	bool pauseThread = (dbp_state != DBPSTATE_BOOT && dbp_state != DBPSTATE_SHUTDOWN);
	if (pauseThread) DBP_ThreadControl(TCM_PAUSE_FRAME);
	retro_time_t timeStart = time_cb();
	DBPSerialize_All(ar, (dbp_state == DBPSTATE_RUNNING), dbp_game_running);
	dbp_serialize_time += (Bit32u)(time_cb() - timeStart);
	//log_cb(RETRO_LOG_WARN, "[SERIALIZE] [%d] [%s] %u\n", (dbp_state == DBPSTATE_RUNNING && dbp_game_running), (ar.mode == DBPArchive::MODE_LOAD ? "LOAD" : ar.mode == DBPArchive::MODE_SAVE ? "SAVE" : ar.mode == DBPArchive::MODE_SIZE ? "SIZE" : ar.mode == DBPArchive::MODE_MAXSIZE ? "MAXX" : ar.mode == DBPArchive::MODE_ZERO ? "ZERO" : "???????"), (Bit32u)ar.GetOffset());
	if (dbp_game_running && ar.mode == DBPArchive::MODE_LOAD) dbp_lastmenuticks = DBP_GetTicks(); // force show menu on immediate emulation crash
	if (pauseThread && unlock_thread) DBP_ThreadControl(TCM_RESUME_FRAME);

	if (ar.had_error && (ar.mode == DBPArchive::MODE_LOAD || ar.mode == DBPArchive::MODE_SAVE))
	{
		static const char* machine_names[MCH_VGA+1] = { "hercules", "cga", "tandy", "pcjr", "ega", "vga" };
		static Bit32u lastErrorId, lastErrorTick;
		Bit32u ticks = DBP_GetTicks();
		Bit32u errorId = (ar.mode << 8) | ar.had_error;
		if (lastErrorId == errorId && (ticks - lastErrorTick) < 5000U) return false; // don't spam errors (especially when doing rewind)
		lastErrorId = errorId;
		lastErrorTick = ticks;
		switch (ar.had_error)
		{
			case DBPArchive::ERR_LAYOUT:
				retro_notify(0, RETRO_LOG_ERROR, "%s%s", "Load State Error: ", "Invalid file format");
				break;
			case DBPArchive::ERR_VERSION:
				retro_notify(0, RETRO_LOG_ERROR, "%sUnsupported version (%d)", "Load State Error: ", ar.version);
				break;
			case DBPArchive::ERR_DOSNOTRUNNING:
				if (ar.mode == DBPArchive::MODE_LOAD)
					retro_notify(0, RETRO_LOG_WARN, "Unable to load a save state while game the isn't running, start it first.");
				else if (dbp_serializemode != DBPSERIALIZE_REWIND)
					retro_notify(0, RETRO_LOG_ERROR, "%sUnable to %s not running.\nIf using rewind, make sure to modify the related core option.", (ar.mode == DBPArchive::MODE_LOAD ? "Load State Error: " : "Save State Error: "), (ar.mode == DBPArchive::MODE_LOAD ? "load state made while DOS was" : "save state while DOS is"));
				break;
			case DBPArchive::ERR_GAMENOTRUNNING:
				if (ar.mode == DBPArchive::MODE_LOAD)
					retro_notify(0, RETRO_LOG_WARN, "Unable to load a save state while game the isn't running, start it first.");
				else if (dbp_serializemode != DBPSERIALIZE_REWIND)
					retro_notify(0, RETRO_LOG_ERROR, "%sUnable to %s not running.\nIf using rewind, make sure to modify the related core option.", (ar.mode == DBPArchive::MODE_LOAD ? "Load State Error: " : "Save State Error: "), (ar.mode == DBPArchive::MODE_LOAD ? "load state made while game was" : "save state while game is"));
				break;
			case DBPArchive::ERR_WRONGMACHINECONFIG:
				retro_notify(0, RETRO_LOG_ERROR, "%sWrong graphics chip configuration (%s instead of %s)", "Load State Error: ",
					(machine <= MCH_VGA ? machine_names[machine] : "UNKNOWN"), (ar.error_info <= MCH_VGA ? machine_names[ar.error_info] : "UNKNOWN"));
				break;
			case DBPArchive::ERR_WRONGMEMORYCONFIG:
				retro_notify(0, RETRO_LOG_ERROR, "%sWrong memory size configuration (%d MB instead of %d MB)", "Load State Error: ",
					(Bit8u)(MEM_TotalPages() / 256), (ar.error_info < 225 ? ar.error_info : (ar.error_info-223)*128));
				break;
			case DBPArchive::ERR_WRONGVGAMEMCONFIG:
				retro_notify(0, RETRO_LOG_ERROR, "%sWrong SVGA mode configuration (%d KB VGA RAM instead of %D KB)", "Load State Error: ",
					(Bit8u)(vga.vmemsize / 1024), ar.error_info * 128);
				break;
		}
	}
	else if (ar.warnings && ar.mode == DBPArchive::MODE_LOAD)
	{
		if (ar.warnings & DBPArchive::WARN_WRONGDRIVES)  retro_notify(0, RETRO_LOG_WARN, "%s%s", "Serialize Warning: ", "Inconsistent file system state or wrong disks mounted");
		if (ar.warnings & DBPArchive::WARN_WRONGDEVICES) retro_notify(0, RETRO_LOG_WARN, "%s%s", "Serialize Warning: ", "Inconsistent device handlers");
		if (ar.warnings & DBPArchive::WARN_WRONGPROGRAM) retro_notify(0, RETRO_LOG_WARN, "%s%s", "Serialize Warning: ", "Loaded into different program type, risk of system crash");
	}
	return !ar.had_error;
}

size_t retro_serialize_size(void)
{
	bool rewind = (dbp_state != DBPSTATE_RUNNING || dbp_serializemode == DBPSERIALIZE_REWIND);
	if (rewind && dbp_serializesize) return dbp_serializesize;
	DBPArchiveCounter ar(rewind);
	return dbp_serializesize = (retro_serialize_all(ar, false) ? ar.count : 0);
}

bool retro_serialize(void *data, size_t size)
{
	DBPArchiveWriter ar(data, size);
	if (!retro_serialize_all(ar, true) && ((ar.had_error != DBPArchive::ERR_DOSNOTRUNNING && ar.had_error != DBPArchive::ERR_GAMENOTRUNNING) || dbp_serializemode != DBPSERIALIZE_REWIND)) return false;
	memset(ar.ptr, 0, ar.end - ar.ptr);
	return true;
}

bool retro_unserialize(const void *data, size_t size)
{
	DBPArchiveReader ar(data, size);
	bool res = retro_serialize_all(ar, true);
	if ((ar.had_error != DBPArchive::ERR_DOSNOTRUNNING && ar.had_error != DBPArchive::ERR_GAMENOTRUNNING) || dbp_serializemode != DBPSERIALIZE_REWIND) return res;
	if (dbp_state != DBPSTATE_RUNNING || dbp_game_running) retro_reset();
	return true;
}

// Unused features
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { return false; }
void retro_deinit(void) { }

// UTF8 fopen
#include "libretro-common/include/compat/fopen_utf8.h"
FILE *fopen_wrap(const char *path, const char *mode)
{
	#ifdef WIN32
	for (const unsigned char* p = (unsigned char*)path; *p; p++)
		if (*p >= 0x80)
			return (FILE*)fopen_utf8(path, mode);
	#endif
	return fopen(path, mode);
}
#ifndef STATIC_LINKING
#include "libretro-common/compat/fopen_utf8.c"
#include "libretro-common/compat/compat_strl.c"
#include "libretro-common/encodings/encoding_utf.c"
#endif

bool fpath_nocase(char* path)
{
	if (!path || !*path) return false;
	struct stat test;
	if (stat(path, &test) == 0) return true; // exists as is

	#ifdef WIN32
	// If stat could handle utf8 strings, we just return false here because paths are not case senstive on Windows
	size_t rootlen = ((path[1] == ':' && (path[2] == '/' || path[2] == '\\')) ? 3 : 0);
	#else
	size_t rootlen = ((path[0] == '/' || path[0] == '\\') ? 1 : 0);
	#endif
	if (!path[rootlen]) return false;
	std::string subdir;
	const char* base_dir = (rootlen ? subdir.append(path, rootlen).c_str() : NULL);
	path += rootlen;

	struct retro_vfs_interface_info vfs = { 3, NULL };
	if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) || vfs.required_interface_version < 3 || !vfs.iface)
		return false;

	for (char* psubdir;; *psubdir = CROSS_FILESPLIT, path = psubdir + 1)
	{
		char *next_slash = strchr(path, '/'), *next_bslash = strchr(path, '\\');
		psubdir = (next_slash && (!next_bslash || next_slash < next_bslash) ? next_slash : next_bslash);
		if (psubdir == path) continue;
		if (psubdir) *psubdir = '\0';

		// On Android opendir fails for directories the user/app doesn't have access so just assume it exists as is
		if (struct retro_vfs_dir_handle *dir = (base_dir ? vfs.iface->opendir(base_dir, true) : NULL))
		{
			while (dir && vfs.iface->readdir(dir))
			{
				const char* entry_name = vfs.iface->dirent_get_name(dir);
				if (strcasecmp(entry_name, path)) continue;
				strcpy(path, entry_name);
				break;
			}
			vfs.iface->closedir(dir);
		}
		if (!psubdir) return true;
		if (subdir.empty() && base_dir) subdir = base_dir;
		if (!subdir.empty() && subdir.back() != '/' && subdir.back() != '\\') subdir += CROSS_FILESPLIT;
		base_dir = subdir.append(path).c_str();
	}
}
