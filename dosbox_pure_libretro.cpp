/*
 *  Copyright (C) 2020-2025 Bernhard Schelling
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
#include "include/dbp_opengl.h"
#include "src/ints/int10.h"
#include "src/dos/drives.h"
#include "keyb2joypad.h"
#include "libretro-common/include/libretro.h"
#include "libretro-common/include/retro_timers.h"
#include <string>
#include <sstream>

// RETROARCH AUDIO/VIDEO
#if defined(GEKKO) || defined(MIYOO) // From RetroArch/config.def.h
#define DBP_DEFAULT_SAMPLERATE 32000.0
#define DBP_DEFAULT_SAMPLERATE_STRING "32000"
#elif defined(_3DS)
#define DBP_DEFAULT_SAMPLERATE 32730.0
#define DBP_DEFAULT_SAMPLERATE_STRING "32730"
#else
#define DBP_DEFAULT_SAMPLERATE 48000.0
#define DBP_DEFAULT_SAMPLERATE_STRING "48000"
#endif
#include "core_options.h"
static retro_system_av_info av_info;

// DOSBOX STATE
static enum DBP_State : Bit8u { DBPSTATE_BOOT, DBPSTATE_EXITED, DBPSTATE_SHUTDOWN, DBPSTATE_REBOOT, DBPSTATE_FIRST_FRAME, DBPSTATE_RUNNING } dbp_state;
static enum DBP_SerializeMode : Bit8u { DBPSERIALIZE_STATES, DBPSERIALIZE_REWIND, DBPSERIALIZE_DISABLED } dbp_serializemode;
static bool dbp_game_running, dbp_pause_events, dbp_paused_midframe, dbp_frame_pending, dbp_biosreboot, dbp_system_cached, dbp_system_scannable, dbp_refresh_memmaps;
static bool dbp_optionsupdatecallback, dbp_reboot_set64mem, dbp_use_network, dbp_had_game_running, dbp_strict_mode, dbp_legacy_save, dbp_wasloaded, dbp_skip_c_mount;
static signed char dbp_menu_time, dbp_conf_loading, dbp_reboot_machine;
static Bit8u dbp_alphablend_base;
static float dbp_auto_target, dbp_last_fastforward;
static Bit32u dbp_lastmenuticks, dbp_framecount, dbp_emu_waiting, dbp_paused_work;
static Semaphore semDoContinue, semDidPause;
static retro_throttle_state dbp_throttle;
static std::string dbp_crash_message;
static std::string dbp_content_path;
static std::string dbp_content_name;
static retro_time_t dbp_boot_time;
static size_t dbp_serializesize;
static Bit16s dbp_content_year, dbp_forcefps;

// DOSBOX AUDIO/VIDEO
static Bit8u buffer_active, dbp_overscan;
static bool dbp_doublescan, dbp_padding;
static struct DBP_Buffer { Bit32u *video, width, height, cap, pad_x, pad_y, border_color; float ratio; } dbp_buffers[3];
#ifndef DBP_STANDALONE
static struct DBP_Audio { int16_t* audio; Bit32u length; } dbp_audio[2];
static Bit8u dbp_audio_active;
#endif
static double dbp_audio_remain;
static struct retro_hw_render_callback dbp_hw_render;
static void (*dbp_opengl_draw)(const DBP_Buffer& buf);

// DOSBOX DISC MANAGEMENT
struct DBP_Image { std::string path, longpath; bool mounted = false, remount = false, image_disk = false, imgmount = false, imfat = false, imiso = false; char drive; int dirlen, dd; };
static std::vector<DBP_Image> dbp_images;
static std::vector<std::string> dbp_osimages, dbp_shellzips;
static StringToPointerHashMap<void> dbp_vdisk_filter;
static unsigned dbp_image_index;

// DOSBOX INPUT
struct DBP_InputBind
{
	Bit8u port, device, index, id;
	Bit16s evt, meta; union { Bit16s lastval; Bit32u _32bitalign; };
	void Update(Bit16s val, bool is_analog_button = false);
	#define PORT_DEVICE_INDEX_ID(b) (*(Bit32u*)&static_cast<const DBP_InputBind&>(b))
};
enum { DBP_MAX_PORTS = 8, DBP_KEYBOARD_PORT, DBP_PORT_MASK = 0x7, DBP_SHIFT_PORT_BIT = 0x80, DBP_NO_PORT = 255, DBP_JOY_ANALOG_RANGE = 0x8000 }; // analog stick range is -0x8000 to 0x8000
static const char* DBP_KBDNAMES[KBD_LAST+1] =
{
	"None","1","2","3","4","5","6","7","8","9","0","Q","W","E","R","T","Y","U","I","O","P","A","S","D","F","G","H","J","K","L","Z","X","C","V","B","N","M",
	"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","Esc","Tab","Backspace","Enter","Space","Left-Alt","Right-Alt","Left-Ctrl","Right-Ctrl","Left-Shift","Right-Shift",
	"Caps-Lock","Scroll-Lock","Num-Lock","Grave `","Minus -","Equals =","Backslash","Left-Bracket [","Right-Bracket ]","Semicolon ;","Quote '","Period .","Comma ,","Slash /","Backslash \\",
	"Print-Screen","Pause","Insert","Home","Page-Up","Delete","End","Page-Down","Left","Up","Down","Right","NP-1","NP-2","NP-3","NP-4","NP-5","NP-6","NP-7","NP-8","NP-9","NP-0",
	"NP-Divide /","NP-Multiply *","NP-Minus -","NP-Plus +","NP-Enter","NP-Period .",""
};
static const char* DBP_YMLKeyCommands[KBD_LAST+3] =
{
	"","1","2","3","4","5","6","7","8","9","0","q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l","z","x","c","v","b","n","m",
	"f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","esc","tab","backspace","enter","space","leftalt","rightalt","leftctrl","rightctrl","leftshift","rightshift",
	"capslock","scrolllock","numlock","grave","minus","equals","backslash","leftbracket","rightbracket","semicolon","quote","period","comma","slash","extra_lt_gt",
	"printscreen","pause","insert","home","pageup","delete","end","pagedown","left","up","down","right","kp1","kp2","kp3","kp4","kp5","kp6","kp7","kp8","kp9","kp0",
	"kpdivide","kpmultiply","kpminus","kpplus","kpenter","kpperiod",
	"wait","waitmodechange","delay"
};
static std::vector<DBP_InputBind> dbp_input_binds;
static Bit8u dbp_port_mode[DBP_MAX_PORTS], dbp_binds_changed, dbp_actionwheel_inputs;
static Bit16s dbp_mouse_x, dbp_mouse_y;
static int dbp_joy_analog_deadzone = (int)(0.15f * (float)DBP_JOY_ANALOG_RANGE);
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
	DBPET_TOGGLEOSD, DBPET_TOGGLEOSDUP,
	DBPET_ACTIONWHEEL, DBPET_ACTIONWHEELUP,
	DBPET_SHIFTPORT, DBPET_SHIFTPORTUP,

	DBPET_AXISMAPPAIR,
	DBPET_CHANGEMOUNTS,
	DBPET_REFRESHSYSTEM,

	#define DBP_IS_RELEASE_EVENT(EVT) ((EVT) >= DBPET_MOUSEUP && !(EVT & 1))
	#define DBP_MAPPAIR_MAKE(KEY1,KEY2) (Bit16s)(((KEY1)<<8)|(KEY2))
	#define DBP_MAPPAIR_GET(VAL,META) ((VAL) < 0 ? (Bit8u)(((Bit16u)(META))>>8) : (Bit8u)(((Bit16u)(META))&255))
	#define DBP_GETKEYDEVNAME(KEY) ((KEY) == KBD_NONE ? NULL : (KEY) < KBD_LAST ? DBPDEV_Keyboard : DBP_SpecialMappings[(KEY)-DBP_SPECIALMAPPINGS_KEY].dev)
	#define DBP_GETKEYNAME(KEY) ((KEY) < KBD_LAST ? DBP_KBDNAMES[(KEY)] : DBP_SpecialMappings[(KEY)-DBP_SPECIALMAPPINGS_KEY].name)

	_DBPET_MAX
};
//static const char* DBP_Event_Type_Names[] = { "JOY1X", "JOY1Y", "JOY2X", "JOY2Y", "JOYMX", "JOYMY", "MOUSEMOVE", "MOUSEDOWN", "MOUSEUP", "MOUSESETSPEED", "MOUSERESETSPEED", "JOYHATSETBIT", "JOYHATUNSETBIT", "JOY1DOWN", "JOY1UP", "JOY2DOWN", "JOY2UP", "KEYDOWN", "KEYUP", "ONSCREENKEYBOARD", "ONSCREENKEYBOARDUP", "ACTIONWHEEL", "ACTIONWHEELUP", "SHIFTPORT", "SHIFTPORTUP", "AXIS_TO_KEY", "CHANGEMOUNTS", "REFRESHSYSTEM", "MAX" };
static const char *DBPDEV_Keyboard = "Keyboard", *DBPDEV_Mouse = "Mouse", *DBPDEV_Joystick = "Joystick";
static const struct DBP_SpecialMapping { int16_t evt, meta; const char *dev, *name, *ymlid; } DBP_SpecialMappings[] =
{
	{ DBPET_JOYMY,         -1, DBPDEV_Mouse,    "Move Up",      "mouse_move_up"      }, // 200
	{ DBPET_JOYMY,          1, DBPDEV_Mouse,    "Move Down",    "mouse_move_down"    }, // 201
	{ DBPET_JOYMX,         -1, DBPDEV_Mouse,    "Move Left",    "mouse_move_left"    }, // 202
	{ DBPET_JOYMX,          1, DBPDEV_Mouse,    "Move Right",   "mouse_move_right"   }, // 203
	{ DBPET_MOUSEDOWN,      0, DBPDEV_Mouse,    "Left Click",   "mouse_left_click"   }, // 204
	{ DBPET_MOUSEDOWN,      1, DBPDEV_Mouse,    "Right Click",  "mouse_right_click"  }, // 205
	{ DBPET_MOUSEDOWN,      2, DBPDEV_Mouse,    "Middle Click", "mouse_middle_click" }, // 206
	{ DBPET_MOUSESETSPEED,  1, DBPDEV_Mouse,    "Speed Up",     "mouse_speed_up"     }, // 207
	{ DBPET_MOUSESETSPEED, -1, DBPDEV_Mouse,    "Slow Down",    "mouse_speed_down"   }, // 208
	{ DBPET_JOY1Y,         -1, DBPDEV_Joystick, "Up",           "joy_up"             }, // 209
	{ DBPET_JOY1Y,          1, DBPDEV_Joystick, "Down",         "joy_down"           }, // 210
	{ DBPET_JOY1X,         -1, DBPDEV_Joystick, "Left",         "joy_left"           }, // 211
	{ DBPET_JOY1X,          1, DBPDEV_Joystick, "Right",        "joy_right"          }, // 212
	{ DBPET_JOY1DOWN,       0, DBPDEV_Joystick, "Button 1",     "joy_button1"        }, // 213
	{ DBPET_JOY1DOWN,       1, DBPDEV_Joystick, "Button 2",     "joy_button2"        }, // 214
	{ DBPET_JOY2DOWN,       0, DBPDEV_Joystick, "Button 3",     "joy_button3"        }, // 215
	{ DBPET_JOY2DOWN,       1, DBPDEV_Joystick, "Button 4",     "joy_button4"        }, // 216
	{ DBPET_JOYHATSETBIT,   8, DBPDEV_Joystick, "Hat Up",       "joy_hat_up"         }, // 217
	{ DBPET_JOYHATSETBIT,   2, DBPDEV_Joystick, "Hat Down",     "joy_hat_down"       }, // 218
	{ DBPET_JOYHATSETBIT,   1, DBPDEV_Joystick, "Hat Left",     "joy_hat_left"       }, // 219
	{ DBPET_JOYHATSETBIT,   4, DBPDEV_Joystick, "Hat Right",    "joy_hat_right"      }, // 220
	{ DBPET_JOY2Y,         -1, DBPDEV_Joystick, "Joy 2 Up",     "joy_2_up"           }, // 221
	{ DBPET_JOY2Y,          1, DBPDEV_Joystick, "Joy 2 Down",   "joy_2_down"         }, // 222
	{ DBPET_JOY2X,         -1, DBPDEV_Joystick, "Joy 2 Left",   "joy_2_left"         }, // 223
	{ DBPET_JOY2X,          1, DBPDEV_Joystick, "Joy 2 Right",  "joy_2_right"        }, // 224
	{ DBPET_TOGGLEOSD,      0, NULL, "Open Menu / Keyboard"  }, // 225
	{ DBPET_ACTIONWHEEL,    0, NULL, "Action Wheel", "wheel" }, // 226
	{ DBPET_SHIFTPORT,      0, NULL, "Port #1 while holding" }, // 227
	{ DBPET_SHIFTPORT,      1, NULL, "Port #2 while holding" }, // 228
	{ DBPET_SHIFTPORT,      2, NULL, "Port #3 while holding" }, // 229
	{ DBPET_SHIFTPORT,      3, NULL, "Port #4 while holding" }, // 230
};
#define DBP_SPECIALMAPPING(key) DBP_SpecialMappings[(key)-DBP_SPECIALMAPPINGS_KEY]
enum { DBP_SPECIALMAPPINGS_KEY = 200, DBP_SPECIALMAPPINGS_MAX = 200+(sizeof(DBP_SpecialMappings)/sizeof(DBP_SpecialMappings[0])) };
enum { DBP_SPECIALMAPPINGS_OSD = 225, DBP_SPECIALMAPPINGS_ACTIONWHEEL = 226 };
enum { DBP_EVENT_QUEUE_SIZE = 256, DBP_DOWN_COUNT_MASK = 127, DBP_DOWN_BY_KEYBOARD = 128 };
static struct DBP_Event { DBP_Event_Type type; Bit8u port; int val, val2; } dbp_event_queue[DBP_EVENT_QUEUE_SIZE];
static int dbp_event_queue_write_cursor;
static int dbp_event_queue_read_cursor;
static int dbp_keys_down_count;
static unsigned char dbp_keys_down[KBD_LAST + 21];
static unsigned short dbp_keymap_dos2retro[KBD_LAST];
static unsigned char dbp_keymap_retro2dos[RETROK_LAST];

// DOSBOX PURE OSD INTERCEPT FUNCTIONS
struct DBP_Interceptor
{
	virtual void gfx(DBP_Buffer& buf) = 0;
	virtual void input() = 0;
	virtual void close() = 0;
	virtual bool evnt(DBP_Event_Type type, int val, int val2) { return false; }
	virtual bool usegfx() { return true; }
};
static DBP_Interceptor *dbp_intercept, *dbp_intercept_next;
static void DBP_SetIntercept(DBP_Interceptor* intercept) { if (!dbp_intercept) dbp_intercept = intercept; dbp_intercept_next = intercept; }

// LIBRETRO CALLBACKS
#ifndef ANDROID
static void retro_fallback_log(enum retro_log_level level, const char *fmt, ...)
{
	(void)level;
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}
#else
extern "C" int __android_log_write(int prio, const char *tag, const char *text);
static void retro_fallback_log(enum retro_log_level level, const char *fmt, ...) { static char buf[8192]; va_list va; va_start(va, fmt); vsprintf(buf, fmt, va); va_end(va); __android_log_write(2, "DBP", buf); }
#endif
extern retro_time_t dbp_cpu_features_get_time_usec(void);
static retro_perf_get_time_usec_t time_cb = dbp_cpu_features_get_time_usec;
static retro_log_printf_t         log_cb = retro_fallback_log;
static retro_environment_t        environ_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;

// PERF OVERLAY
static enum DBP_Perf : Bit8u { DBP_PERF_NONE, DBP_PERF_SIMPLE, DBP_PERF_DETAILED } dbp_perf;
static Bit32u dbp_perf_uniquedraw, dbp_perf_count, dbp_perf_totaltime;
//#define DBP_ENABLE_WAITSTATS
#ifdef DBP_ENABLE_WAITSTATS
static Bit32u dbp_wait_pause, dbp_wait_finish, dbp_wait_paused, dbp_wait_continue;
#endif

// PERF FPS COUNTERS
//#define DBP_ENABLE_FPS_COUNTERS
#ifdef DBP_ENABLE_FPS_COUNTERS
static Bit32u dbp_lastfpstick, dbp_fpscount_retro, dbp_fpscount_gfxstart, dbp_fpscount_gfxend, dbp_fpscount_event, dbp_fpscount_skip_run, dbp_fpscount_skip_render;
#define DBP_FPSCOUNT(DBP_FPSCOUNT_VARNAME) DBP_FPSCOUNT_VARNAME++;
#else
#define DBP_FPSCOUNT(DBP_FPSCOUNT_VARNAME)
#endif

void setup_retro_notify(retro_message_ext& msg, int duration, retro_log_level lvl, char const* format, va_list ap)
{
	static char buf[1024];
	vsnprintf(buf, sizeof(buf), format, ap);
	msg.msg = buf;
	msg.duration = (duration ? (unsigned)abs(duration) : (lvl == RETRO_LOG_ERROR ? 12000 : 4000));
	msg.priority = 0;
	msg.level = lvl;
	msg.target = (duration < 0 ? RETRO_MESSAGE_TARGET_OSD : RETRO_MESSAGE_TARGET_ALL);
	msg.type = (duration < 0 ? RETRO_MESSAGE_TYPE_STATUS : RETRO_MESSAGE_TYPE_NOTIFICATION);
}

void retro_notify(int duration, retro_log_level lvl, char const* format,...)
{
	retro_message_ext msg;
	va_list ap; va_start(ap, format); setup_retro_notify(msg, duration, lvl, format, ap); va_end(ap);
	if (!environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg) && msg.type == RETRO_MESSAGE_TYPE_NOTIFICATION) log_cb(RETRO_LOG_ERROR, "%s", msg.msg);
}

static retro_message_ext* dbp_message_queue;
void emuthread_notify(int duration, LOG_SEVERITIES lvl, char const* format,...)
{
	retro_log_level rlvl = (lvl == LOG_NORMAL ? RETRO_LOG_INFO : lvl == LOG_WARN ? RETRO_LOG_WARN : RETRO_LOG_ERROR);
	retro_message_ext stk, *msg = (dbp_state == DBPSTATE_BOOT ? &stk : (retro_message_ext*)malloc(sizeof(retro_message_ext)+sizeof(retro_message_ext*)));
	va_list ap; va_start(ap, format); setup_retro_notify(*msg, duration, rlvl, format, ap); va_end(ap);
	if (msg == &stk) { if (!environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, msg) && msg->type == RETRO_MESSAGE_TYPE_NOTIFICATION) log_cb(RETRO_LOG_ERROR, "%s", msg->msg); return; }
	msg->msg = strdup(msg->msg);
	*(retro_message_ext**)(msg+1) = dbp_message_queue;
	dbp_message_queue = msg;
}

static void run_emuthread_notify()
{
	while (dbp_message_queue)
	{
		retro_message_ext* msg = dbp_message_queue;
		dbp_message_queue = *(retro_message_ext**)(msg+1);
		if (!environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, msg) && msg->type == RETRO_MESSAGE_TYPE_NOTIFICATION) log_cb(RETRO_LOG_ERROR, "%s", msg->msg);
		free((void*)msg->msg);
		free((void*)msg);
	}
}

static const char* retro_get_variable(const char* key, const char* default_value)
{
	retro_variable var = { key };
	return (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : default_value);
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
void DBP_MIXER_ScrapAudio();
void MIXER_CallBack(void *userdata, uint8_t *stream, int len);
bool MSCDEX_HasDrive(char driveLetter);
int MSCDEX_AddDrive(char driveLetter, const char* physicalPath, Bit8u& subUnit);
int MSCDEX_RemoveDrive(char driveLetter);
void IDE_RefreshCDROMs();
void IDE_SetupControllers(bool alwaysHaveCDROM);
void NET_SetupEthernet();
bool MIDI_TSF_SwitchSF(const char*);
const char* DBP_MIDI_StartupError(Section* midisec, const char*& arg);
static void DBP_ForceReset(bool forcemenu = false);

static void DBP_QueueEvent(DBP_Event_Type type, Bit8u port, int val = 0, int val2 = 0)
{
	unsigned char* downs = dbp_keys_down;
	switch (type)
	{
		case DBPET_KEYDOWN: DBP_ASSERT(val > KBD_NONE && val < KBD_LAST); goto check_down;
		case DBPET_KEYUP:   DBP_ASSERT(val > KBD_NONE && val < KBD_LAST); goto check_up;
		case DBPET_MOUSEDOWN:      DBP_ASSERT(val >= 0 && val < 3); downs += KBD_LAST +  0; goto check_down;
		case DBPET_MOUSEUP:        DBP_ASSERT(val >= 0 && val < 3); downs += KBD_LAST +  0; goto check_up;
		case DBPET_JOY1DOWN:       DBP_ASSERT(val >= 0 && val < 2); downs += KBD_LAST +  3; goto check_down;
		case DBPET_JOY1UP:         DBP_ASSERT(val >= 0 && val < 2); downs += KBD_LAST +  3; goto check_up;
		case DBPET_JOY2DOWN:       DBP_ASSERT(val >= 0 && val < 2); downs += KBD_LAST +  5; goto check_down;
		case DBPET_JOY2UP:         DBP_ASSERT(val >= 0 && val < 2); downs += KBD_LAST +  5; goto check_up;
		case DBPET_JOYHATSETBIT:   DBP_ASSERT(val >= 0 && val < 8); downs += KBD_LAST +  7; goto check_down;
		case DBPET_JOYHATUNSETBIT: DBP_ASSERT(val >= 0 && val < 8); downs += KBD_LAST +  7; goto check_up;
		case DBPET_TOGGLEOSD:      DBP_ASSERT(val >= 0 && val < 1); downs += KBD_LAST + 15; goto check_down;
		case DBPET_TOGGLEOSDUP:    DBP_ASSERT(val >= 0 && val < 1); downs += KBD_LAST + 15; goto check_up;
		case DBPET_ACTIONWHEEL:    DBP_ASSERT(val >= 0 && val < 1); downs += KBD_LAST + 16; goto check_down;
		case DBPET_ACTIONWHEELUP:  DBP_ASSERT(val >= 0 && val < 1); downs += KBD_LAST + 16; goto check_up;
		case DBPET_SHIFTPORT:      DBP_ASSERT(val >= 0 && val < 4); downs += KBD_LAST + 17; goto check_down;
		case DBPET_SHIFTPORTUP:    DBP_ASSERT(val >= 0 && val < 4); downs += KBD_LAST + 17; goto check_up;

		check_down:
			if (((++downs[val]) & DBP_DOWN_COUNT_MASK) > 1) return;
			if (downs == dbp_keys_down) dbp_keys_down_count++;
			break;
		check_up:
			if (((downs[val]) & DBP_DOWN_COUNT_MASK) == 0 || ((--downs[val]) & DBP_DOWN_COUNT_MASK) > 0) return;
			if (downs == dbp_keys_down) dbp_keys_down_count--;
			break;

		case DBPET_JOY1X: case DBPET_JOY1Y: case DBPET_JOY2X: case DBPET_JOY2Y: case DBPET_JOYMX: case DBPET_JOYMY:
			if (val || dbp_intercept) break;
			for (const DBP_InputBind& b : dbp_input_binds) // check if another bind is currently influencing the same axis
			{
				if (!b.lastval) continue;
				if (b.evt <= _DBPET_JOY_AXIS_MAX)
				{
					if ((DBP_Event_Type)b.evt != type) continue;
					val = (b.meta ? (b.lastval ? 32767 : 0) * b.meta : b.lastval);
					goto found_axis_value;
				}
				else if (b.device != RETRO_DEVICE_ANALOG) continue;
				else for (Bit16s dir = 1; dir >= -1; dir -= 2)
				{
					Bit16s map = DBP_MAPPAIR_GET(dir, b.meta), dirbval = b.lastval * dir;
					if (map < DBP_SPECIALMAPPINGS_KEY || dirbval < 0 || (DBP_Event_Type)DBP_SPECIALMAPPING(map).evt != type) continue;
					val = (dirbval < 0 ? 0 : dirbval) * DBP_SPECIALMAPPING(map).meta;
					goto found_axis_value;
				}
			}
			found_axis_value:break;
		default:;
	}
	DBP_Event evt = { type, port, val, val2 };
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

static void DBP_ReleaseKeyEvents(bool onlyPhysicalKeys)
{
	for (Bit8u i = KBD_NONE + 1, iEnd = (onlyPhysicalKeys ? KBD_LAST : KBD_LAST + 21); i != iEnd; i++)
	{
		if (!dbp_keys_down[i] || (onlyPhysicalKeys && (!(dbp_keys_down[i] & DBP_DOWN_BY_KEYBOARD) || input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, dbp_keymap_dos2retro[i])))) continue;
		dbp_keys_down[i] = 1;
		DBP_Event_Type type; int val = i;
		if      (i < KBD_LAST +  0) type = DBPET_KEYUP;
		else if (i < KBD_LAST +  3) { val -=  KBD_LAST +  0; type = DBPET_MOUSEUP; }
		else if (i < KBD_LAST +  5) { val -=  KBD_LAST +  3; type = DBPET_JOY1UP; }
		else if (i < KBD_LAST +  7) { val -=  KBD_LAST +  5; type = DBPET_JOY2UP; }
		else if (i < KBD_LAST + 15) { val -=  KBD_LAST +  7; type = DBPET_JOYHATUNSETBIT; }
		else if (i < KBD_LAST + 16) { val -=  KBD_LAST + 15; type = DBPET_TOGGLEOSDUP; }
		else if (i < KBD_LAST + 17) { val -=  KBD_LAST + 16; type = DBPET_ACTIONWHEELUP; }
		else                        { val -=  KBD_LAST + 17; type = DBPET_SHIFTPORTUP; }
		DBP_QueueEvent(type, DBP_NO_PORT, val);
	}
}

void DBP_InputBind::Update(Bit16s val, bool is_analog_button)
{
	Bit16s prevval = lastval;
	lastval = val; // set before calling DBP_QueueEvent
	if (evt <= _DBPET_JOY_AXIS_MAX)
	{
		// handle analog axis mapped to analog functions
		if (device == RETRO_DEVICE_JOYPAD && !is_analog_button) { lastval = prevval; return; } // handled by dbp_analog_buttons
		DBP_ASSERT(device == RETRO_DEVICE_JOYPAD || meta == 0); // analog axis mapped to analog functions should always have 0 in meta
		DBP_ASSERT(device != RETRO_DEVICE_JOYPAD || meta == 1 || meta == -1); // buttons mapped to analog functions should always have 1 or -1 in meta
		DBP_QueueEvent((DBP_Event_Type)evt, port, (meta ? val * meta : val), 0);
	}
	else if (device != RETRO_DEVICE_ANALOG)
	{
		// if button is pressed, send the _DOWN, otherwise send _UP
		DBP_QueueEvent((DBP_Event_Type)(val ? evt : evt + 1), port, meta);
	}
	else for (Bit16s dir = 1; dir >= -1; dir -= 2)
	{
		DBP_ASSERT(evt == DBPET_AXISMAPPAIR);
		Bit16s map = DBP_MAPPAIR_GET(dir, meta), dirval = val * dir, dirprevval = prevval * dir;
		if (map == KBD_NONE) continue;
		if (map < KBD_LAST)
		{
			if (dirval >= 12000 && dirprevval <  12000) DBP_QueueEvent(DBPET_KEYDOWN, port, map);
			if (dirval <  12000 && dirprevval >= 12000) DBP_QueueEvent(DBPET_KEYUP,   port, map);
			continue;
		}
		if (map < DBP_SPECIALMAPPINGS_KEY) { DBP_ASSERT(false); continue; }
		if (dirval <= 0 && dirprevval <= 0) continue;
		const DBP_SpecialMapping& sm = DBP_SPECIALMAPPING(map);
		if (sm.evt <= _DBPET_JOY_AXIS_MAX) DBP_QueueEvent((DBP_Event_Type)sm.evt, port, (dirval < 0 ? 0 : dirval) * sm.meta);
		else if (dirval >= 12000 && dirprevval <  12000) DBP_QueueEvent((DBP_Event_Type)(sm.evt    ), port, sm.meta);
		else if (dirval <  12000 && dirprevval >= 12000) DBP_QueueEvent((DBP_Event_Type)(sm.evt + 1), port, sm.meta);
	}
}

static void DBP_ReportCoreMemoryMaps()
{
	extern const char* RunningProgram;
	const size_t conventional_end = 640 * 1024, memtotal = (MEM_TotalPages() * 4096);

	// Give access to entire memory to frontend (cheat and achievements support)
	// Instead of raw [OS] [GAME] [EXPANDED MEMORY] we switch the order to be
	// [GAME] [OS] [EXPANDED MEMORY] so regardless of the size of the OS environment
	// the game memory (below 640k) is always at the same (virtual) address.

	struct retro_memory_descriptor mdescs[3] = {{0}}, *mdesc_expandedmem;
	if (!DOSBox_Boot)
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
		mdescs[0].start      = 0x00000000;
		mdescs[0].len        = conventional_end;
		mdescs[0].ptr        = MemBase;
		mdesc_expandedmem = &mdescs[1];
	}
	mdesc_expandedmem->flags = RETRO_MEMDESC_SYSTEM_RAM;
	mdesc_expandedmem->start = 0x00200000;
	mdesc_expandedmem->len   = memtotal - conventional_end;
	mdesc_expandedmem->ptr   = MemBase + conventional_end;

	#ifndef NDEBUG
	log_cb(RETRO_LOG_INFO, "[DOSBOX STATUS] ReportCoreMemoryMaps - Program: %s - Booted OS: %d - Program Memory: %d KB\n", RunningProgram, (int)DOSBox_Boot, (mdescs[0].len / 1024));
	#endif

	struct retro_memory_map mmaps = { mdescs, (unsigned)(!DOSBox_Boot ? 3 : 2) };
	environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);
	dbp_refresh_memmaps = false;
}

enum DBP_ThreadCtlMode { TCM_PAUSE_FRAME, TCM_ON_PAUSE_FRAME, TCM_RESUME_FRAME, TCM_FINISH_FRAME, TCM_ON_FINISH_FRAME, TCM_NEXT_FRAME, TCM_SHUTDOWN, TCM_ON_SHUTDOWN };
static void DBP_ThreadControl(DBP_ThreadCtlMode m)
{
	static retro_time_t pausedTimeStart; retro_time_t emuWaitTimeStart;
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
			emuWaitTimeStart = time_cb();
			semDoContinue.Wait();
			dbp_emu_waiting += (Bit32u)(time_cb() - emuWaitTimeStart);
			#ifdef DBP_ENABLE_WAITSTATS
			dbp_wait_paused += (Bit32u)(time_cb() - emuWaitTimeStart);
			#endif
			dbp_paused_midframe = false;
			return;
		case TCM_RESUME_FRAME:
			if (!dbp_frame_pending) return;
			DBP_ASSERT(dbp_pause_events);
			dbp_pause_events = false;
			goto case_TCM_EMULATION_CONTINUES;
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
			emuWaitTimeStart = time_cb();
			semDoContinue.Wait();
			dbp_emu_waiting += (Bit32u)(time_cb() - emuWaitTimeStart);
			#ifdef DBP_ENABLE_WAITSTATS
			dbp_wait_continue += (Bit32u)(time_cb() - emuWaitTimeStart);
			#endif
			return;
		case TCM_NEXT_FRAME:
			DBP_ASSERT(!dbp_frame_pending);
			if (dbp_state == DBPSTATE_EXITED) return;
			dbp_frame_pending = true;
			goto case_TCM_EMULATION_CONTINUES;
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
			dbp_game_running = false;
			semDidPause.Post();
			return;
		case_TCM_EMULATION_PAUSED:
			if (!pausedTimeStart) pausedTimeStart = time_cb();
			if (dbp_refresh_memmaps) DBP_ReportCoreMemoryMaps();
			return;
		case_TCM_EMULATION_CONTINUES:
			if (pausedTimeStart) { dbp_paused_work += (Bit32u)(time_cb() - pausedTimeStart); pausedTimeStart = 0; }
			if (dbp_serializesize && dbp_serializemode != DBPSERIALIZE_REWIND) dbp_serializesize = 0;
			semDoContinue.Post();
			return;
	}
}

static inline Bit32s DBP_CyclesForYear(int year, int year_max = 0x7FFFFFF)
{
	static const Bit32s Cycles1982to1999[1+1999-1982] = { 900, 1500, 2100, 2750, 3800, 4800, 6300, 7800, 14000, 23800, 27000, 44000, 55000, 66800, 93000, 125000, 200000, 350000 };
	return (year > year_max ? DBP_CyclesForYear(year_max, year_max) :
		(year < 1982 ? 315 : // Very early 8086/8088 CPU
		(year > 1999 ? (500000 + ((year - 2000) * 200000)) : // Pentium III, 600 MHz and later
		Cycles1982to1999[year - 1982]))); // Matching speed for year
}

static void DBP_SetCyclesByYear(int year, int year_max)
{
	DBP_ASSERT(year > 1970);
	CPU_CycleMax = DBP_CyclesForYear(year, year_max);
	extern void DBP_CPU_AutoEnableDynamicCore();
	DBP_CPU_AutoEnableDynamicCore();
}

void DBP_SetRealModeCycles()
{
	if (cpu.pmode || CPU_CycleAutoAdjust || !(CPU_AutoDetermineMode & CPU_AUTODETERMINE_CYCLES) || !dbp_game_running || dbp_content_year <= 1970) return;
	const int year = (machine != MCH_PCJR ? dbp_content_year : 1981);
	DBP_SetCyclesByYear(year, 1996);

	// When auto switching to a high-speed CPU, enable auto adjust so low spec hardware is allowed to throttle down
	if (year >= 1995)
		CPU_CycleAutoAdjust = true;
}

static int DBP_NeedFrameSkip(bool in_emulation)
{
	float run_rate = dbp_throttle.rate, emu_rate = render.src.fps;
	if (run_rate == 0)
	{
		if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD) return (in_emulation ? 8 : 0);
		run_rate = (float)av_info.timing.fps;
	}
	else if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION || dbp_throttle.mode == RETRO_THROTTLE_REWINDING)
	{
		emu_rate *= (run_rate / (float)av_info.timing.fps);
	}

	if ((in_emulation ? (run_rate >= emu_rate - 0.001f) : (emu_rate >= run_rate - 0.001f)) || run_rate < 5 || emu_rate < 5 || dbp_throttle.mode == RETRO_THROTTLE_FRAME_STEPPING) return 0;
	static float accum;
	accum += (in_emulation ? (emu_rate - run_rate) : (run_rate - emu_rate));
	if (accum < run_rate) return 0;
	int res = (in_emulation ? (int)(accum / run_rate) : 1);
	#ifdef DBP_ENABLE_FPS_COUNTERS
	(in_emulation ? dbp_fpscount_skip_render : dbp_fpscount_skip_run) += (Bit32u)res;
	#endif
	//log_cb(RETRO_LOG_INFO, "%s %d FRAME(S) AT %u\n", (in_emulation ? "[GFX_EndUpdate] SKIP RENDERING" : "[retro_run] SKIP EMULATING"), res, dbp_framecount);
	accum -= run_rate * res;
	return res;
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
	for (DBP_Image& i : dbp_images) if (i.path == in_path) return (unsigned)(&i - &dbp_images[0]); // known

	struct Local { static bool GetDriveDepth(DOS_Drive* drv, const char* p, int& res)
	{
		res++;
		for (int n = 0;; n++)
			if (DOS_Drive* shadow = drv->GetShadow(n, true)) { if (GetDriveDepth(shadow, p, res)) return true; }
			else return (!n && drv->FileExists(p));
	}};
	int dd = 0;
	if (in_path[0] == '$' && Drives[in_path[1]-'A']) Local::GetDriveDepth(Drives[in_path[1]-'A'], in_path + 4, dd);

	// insert into image list ordered by drive depth and alphabetically
	unsigned insert_index = (unsigned)dbp_images.size();
	if (sorted)
		for (DBP_Image& i : dbp_images)
			if (dd < i.dd || (dd == i.dd && i.path > in_path)) {insert_index = (unsigned)(&i - &dbp_images[0]); break; }

	dbp_images.insert(dbp_images.begin() + insert_index, DBP_Image());
	DBP_Image& i = dbp_images[insert_index];
	i.path = in_path;
	i.dd = dd;

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
	if (out_namelen) *out_namelen = (p_dot_drive ? p_dot_drive : ext) - ((*ext && ext[-1] == '.') ? 1 : 0) - path_file;
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

void DBP_Unmount(char drive)
{
	DBP_ASSERT(drive >= 'A' && drive <= 'Z');
	for (DBP_Image& i : dbp_images)
	{
		if (!i.mounted || i.drive != drive) continue;
		i.mounted = false;
	}
	DOS_Drive *drv = Drives[drive-'A'], *tst;
	if (drv && drv->UnMount() != 0) { DBP_ASSERT(false); return; }
	Drives[drive-'A'] = NULL;
	MSCDEX_RemoveDrive(drive);
	if (drive < 'A'+MAX_DISK_IMAGES)
		if (imageDisk*& dsk = imageDiskList[drive-'A'])
			{ delete dsk; dsk = NULL; }
	IDE_RefreshCDROMs();
	mem_writeb(Real2Phys(dos.tables.mediaid)+(drive-'A')*9,0);

	// Unmount anything that is a subst mirror of the drive that just got unmounted
	for (Bit8u i = 0; drv && i < DOS_DRIVES; i++)
		if ((tst = Drives[i]) != NULL && tst != drv && tst->GetShadow(0, false) == drv)
			DBP_Unmount(i + 'A');
}

enum DBP_SaveFileType { SFT_GAMESAVE, SFT_SAVENAMEREDIRECT, SFT_VIRTUALDISK, SFT_DIFFDISK, _SFT_LAST_SAVE_DIRECTORY, SFT_SYSTEMDIR, SFT_NEWOSIMAGE };
static std::string DBP_GetSaveFile(DBP_SaveFileType type, const char** out_filename = NULL, Bit32u* out_diskhash = NULL)
{
	std::string res;
	char savename[256];
	size_t savenamelen = 0;
	if (type < _SFT_LAST_SAVE_DIRECTORY)
	{
		// Find a file with a .savename extension to use as a save redirect for sharing saves between multiple contents
		if (DOS_Drive* drv = (!dbp_legacy_save ? Drives['C'-'A'] : NULL))
		{
			RealPt save_dta = dos.dta();
			dos.dta(dos.tables.tempdta);
			DOS_DTA dta(dos.dta());
			dta.SetupSearch(255, (Bit8u)(0xffff & ~(DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY)), (char*)"*.sav");
			for (bool more = drv->FindFirst((char*)"", dta); more; more = drv->FindNext(dta))
			{
				char dta_name[DOS_NAMELENGTH_ASCII]; Bit32u dta_size; Bit16u dta_date, dta_time; Bit8u dta_attr;
				dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
				if (drv->GetLongFileName(dta_name, savename) && (savenamelen = strlen(savename)) > 9 && !strncasecmp(savename + savenamelen - 9, ".SAVENAME", 9)) break;
				savenamelen = 0;
			}
			dos.dta(save_dta);
		}
		if (type == SFT_SAVENAMEREDIRECT && !savenamelen) return res;
	}
	const char *env_dir = NULL;
	if (environ_cb((type < _SFT_LAST_SAVE_DIRECTORY ? RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY : RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY), &env_dir) && env_dir)
		res.assign(env_dir) += CROSS_FILESPLIT;
	Cross::MakePathAbsolute(res);
	size_t dir_len = res.size();
	if (type < _SFT_LAST_SAVE_DIRECTORY)
	{
		if (savenamelen) res.append(savename, savenamelen - 9);
		else res.append(dbp_content_name.empty() ? "DOSBox-pure" : dbp_content_name.c_str());
		if (type == SFT_GAMESAVE && !dbp_strict_mode) // strict mode has no support for legacy saves
		{
			if (FILE* fSave = fopen_wrap(res.append(".pure.zip").c_str(), "rb")) { fclose(fSave); } // new save exists!
			else if (FILE* fSave = fopen_wrap(res.replace(res.length()-8, 3, "sav", 3).c_str(), "rb")) { dbp_legacy_save = true; fclose(fSave); }
			else res.replace(res.length()-8, 3, "pur", 3); // use new save
		}
		else if (type <= SFT_SAVENAMEREDIRECT)
		{
			res.append(".pure.zip"); // only use new save
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
			Bit32u hash = (Bit32u)(0x11111111 - 1024) + (Bit32u)atoi(DBP_Option::Get(DBP_Option::bootos_dfreespace));
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
	return res;
}

FILE* DBP_FileOpenContentOrSystem(const char* fname)
{
	std::string tmp;
	const char *p = fname, *content = dbp_content_path.c_str(), *content_fs = strrchr(content, '/'), *content_bs = strrchr(content, '\\');
	const char* content_dir_end = ((content_fs || content_bs) ? (content_fs > content_bs ? content_fs : content_bs) + 1 : content + dbp_content_path.length());
	if (content_dir_end != content) p = tmp.append(content, (content_dir_end - content)).append((size_t)(!content_fs && !content_bs), CROSS_FILESPLIT).append(fname).c_str();
	if (FILE* f = fopen_wrap(p, "rb")) return f;
	return fopen_wrap((tmp = DBP_GetSaveFile(SFT_SYSTEMDIR)).append(fname).c_str(), "rb");
}

static void DBP_SetDriveLabelFromContentPath(DOS_Drive* drive, const char *path, char letter = 'C', const char *path_file = NULL, const char *ext = NULL, bool forceAppendExtension = false)
{
	// Use content filename as drive label, cut off at file extension, the first occurrence of a ( or [ character or right white space.
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

static bool DBP_IsDiskCDISO(imageDisk* id)
{
	// Check if ISO (based on CDROM_Interface_Image::LoadIsoFile/CDROM_Interface_Image::CanReadPVD)
	static const Bit32u pvdoffsets[] = { 32768, 32768+8, 37400, 37400+8, 37648, 37656, 37656+8 };
	for (Bit32u pvdoffset : pvdoffsets)
	{
		Bit8u pvd[8];
		if (id->Read_Raw(pvd, pvdoffset, 8) == 8 && (!memcmp(pvd, "\1CD001\1", 7) || !memcmp(pvd, "\1CDROM\1", 7)))
			return true;
	}
	return false;
}

static DOS_Drive* DBP_Mount(unsigned image_index = 0, bool unmount_existing = true, char remount_letter = 0, const char* boot = NULL)
{
	DBP_Image* dbpimage = (!boot ? &dbp_images[image_index] : NULL);
	const char *path = (dbpimage ? dbpimage->path.c_str() : boot), *path_file, *ext, *fragment; char letter;
	if (!DBP_ExtractPathInfo(path, &path_file, NULL, &ext, &fragment, &letter)) return NULL;
	if (remount_letter) letter = remount_letter;

	std::string path_no_fragment;
	if (dbpimage && dbpimage->imgmount)
	{
		// somewhat dangerous to set ext as not part of path but it is OK for the IMG and ISO code paths below
		if (dbpimage->imfat) ext = "IMG";
		if (dbpimage->imiso) ext = "ISO";
		if (!remount_letter) letter = remount_letter = dbpimage->drive;
	}
	else if (fragment)
	{
		path_no_fragment.assign(path, fragment - path);
		ext       = path_no_fragment.c_str() + (ext       - path);
		path_file = path_no_fragment.c_str() + (path_file - path);
		path      = path_no_fragment.c_str();
	}

	DOS_Drive *drive = NULL;
	imageDisk* disk = NULL;
	CDROM_Interface* cdrom = NULL;
	Bit8u media_byte = 0;
	const char* error_type = "content";
	if (!strcasecmp(ext, "ZIP") || !strcasecmp(ext, "DOSZ") || !strcasecmp(ext, "DOSC"))
	{
		if (!letter) letter = (boot ? 'C' : 'D');
		if (!unmount_existing && Drives[letter-'A']) return NULL;
		std::string* ziperr = NULL;
		if ((ext[3]|0x20) != 'c')
			drive = zipDrive::MountWithDependencies(path, ziperr, dbp_strict_mode, dbp_legacy_save);
		else
		{
			// When loading a DOSC file, load the corresponding DOSZ file, but strip out a [VARIANT] specifier at the end.
			std::string dosz_path(path);
			dosz_path.back() = (ext[3] == 'c' ? 'z' : 'Z'); // swap dosc -> dosz with same capitalization
			if (const char* dosc_variant = (ext[-2] == ']' ? strrchr(path_file, '[') : NULL))
			{
				while (dosc_variant > path_file && dosc_variant[-1] == ' ') dosc_variant--;
				size_t dosc_variant_len = (ext - 1 - dosc_variant);
				dosz_path.erase(dosc_variant - path, dosc_variant_len);
			}
			drive = zipDrive::MountWithDependencies(dosz_path.c_str(), ziperr, dbp_strict_mode, dbp_legacy_save, path);
		}
		if (!drive)
		{
			if (ziperr) { emuthread_notify(0, LOG_ERROR, "%s", ziperr->c_str()); delete ziperr; }
			error_type = "ZIP";
			goto TRY_DIRECTORY;
		}
		DBP_SetDriveLabelFromContentPath(drive, path, letter, path_file, ext);
		if (boot && letter == 'C') return drive;
	}
	else if (!strcasecmp(ext, "IMG") || !strcasecmp(ext, "IMA") || !strcasecmp(ext, "VHD") || !strcasecmp(ext, "JRC"))
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
			if (DBP_IsDiskCDISO(fat->loadedDisk)) goto FAT_TRY_ISO;
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
				updateDPT();
				fat2->loadedDisk = NULL; // don't want it deleted by ~fatDrive
				delete fat2;
				return fat;
			}
		}
		if (!letter) letter = (disk->hardDrive ? (drive ? 'D' : 'E') : 'A');
		media_byte = (disk->hardDrive ? 0xF8 : (disk->active ? disk->GetBiosType() : 0));
	}
	else if (!strcasecmp(ext, "ISO") || !strcasecmp(ext, "CHD") || !strcasecmp(ext, "CUE") || !strcasecmp(ext, "INS"))
	{
		MOUNT_ISO:
		if (letter < 'D' && !remount_letter) letter = 'D';
		if (!unmount_existing && Drives[letter-'A']) return NULL;
		if (DBP_IsMounted(letter)) DBP_Unmount(letter); // needs to be done before constructing isoDrive as it registers itself with MSCDEX overwriting the current drives registration
		int error = -1;
		isoDrive* iso = new isoDrive(letter, path, 0xF8, error);
		if (error)
		{
			delete iso;
			if (DOS_Drive **srcdrv = ((!boot && path[0] == '$' && path[1] >= 'A' && path[1] <= 'Z') ? &Drives[path[1]-'A'] : NULL))
				if (DOS_Drive *mirror = dynamic_cast<mirrorDrive*>(*srcdrv))
					if (DOS_Drive* shadow = mirror->GetShadow(0, false))
					{
						*srcdrv = shadow;
						drive = DBP_Mount(image_index, unmount_existing, remount_letter);
						*srcdrv = mirror;
						return drive;
					}
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
			emuthread_notify(0, LOG_ERROR, "Unable to open %s file: %s%s", error_type, path, "");
			return NULL;
		}
		close_directory(dirp);

		localDrive* localdrive = new localDrive(dir.c_str(), 512, 32, 32765, 16000, 0xF8);
		DBP_SetDriveLabelFromContentPath(localdrive, path, letter, path_file, ext);
		if (!isDir && (ext[2]|0x20) == 'n') dbp_conf_loading = 'o'; // conf loading mode set to 'o'utside will load the requested .conf file
		drive = localdrive;
		if (boot) path = NULL; // don't treat as disk image, but always register with Drives
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

	// Register CDROM with IDE controller (if IDE_SetupControllers was called)
	if (cdrom) IDE_RefreshCDROMs();

	if (path)
	{
		if (boot) dbpimage = &dbp_images[(image_index = DBP_AppendImage(path, false))];
		dbpimage->mounted = true;
		dbpimage->remount = false;
		dbpimage->drive = letter;
		dbp_image_index = image_index;
	}

	// Register with BIOS/CMOS/IDE controller (after DBP_AppendImage)
	if (disk && letter < 'A'+MAX_DISK_IMAGES)
	{
		imageDiskList[letter-'A'] = disk;
		dbpimage->image_disk = true;
		if (letter >= 'C') updateDPT();
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
		if (drive1 < 'A'+MAX_DISK_IMAGES && imageDiskList[drive1-'A'])
		{
			imageDisk*& dsk = imageDiskList[drive1-'A'];
			if (drive2 < 'A'+MAX_DISK_IMAGES) imageDiskList[drive2-'A'] = dsk;
			else if (!dynamic_cast<fatDrive*>(Drives[drive1-'A'])) delete dsk;
			dsk = NULL;
			updateDPT();
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

void DBP_ImgMountLoadDisks(char drive, const std::vector<std::string>& paths, bool fat, bool iso)
{
	for (const std::string& path : paths)
	{
		DBP_Image& i = dbp_images[DBP_AppendImage(path.c_str(), false)];
		i.drive = drive;
		i.imgmount = true;
		i.imfat = fat;
		i.imiso = iso;
	}
	DBP_Mount(DBP_AppendImage(paths[0].c_str(), false));
}

void DBPSerialize_Mounts(DBPArchive& ar)
{
	const char* fname;
	Bit32u mounthash[2] = { 0, 0 }; // remember max 2 mounted images (one floppy and cd)
	if (ar.mode == DBPArchive::MODE_SAVE)
		for (DBP_Image& i : dbp_images)
			if (i.mounted && DBP_ExtractPathInfo(i.longpath.c_str(), &fname))
				mounthash[mounthash[0] ? 1 : 0] = BaseStringToPointerHashMap::Hash(fname);
	ar << mounthash[0] << mounthash[1];
	if (ar.mode == DBPArchive::MODE_LOAD && mounthash[0])
		for (DBP_Image& i : dbp_images)
			if (DBP_ExtractPathInfo(i.longpath.c_str(), &fname) && (mounthash[0] == BaseStringToPointerHashMap::Hash(fname) || (mounthash[1] && mounthash[1] == BaseStringToPointerHashMap::Hash(fname))))
				DBP_Mount((unsigned)(&i - &dbp_images[0]), true);
}

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
		CPU_Cycles = 0; // avoid crash due to PIC_TickIndex returning negative number when CPU_Cycles > CPU_CycleMax
		delete control;
		control = NULL;
	}
	dbp_state = DBPSTATE_SHUTDOWN;
}

void DBP_OnBIOSReboot()
{
	// to be called on the DOSBox thread
	if ((MEM_TotalPages() / 256) == 64 && atoi(DBP_Option::Get(DBP_Option::memory_size)) < 32)
		dbp_reboot_set64mem = true; // avoid another restart via DBP_Run::BootOS
	dbp_biosreboot = true;
	if (first_shell) DBP_DOSBOX_ForceShutdown();
}

static double DBP_GetFPS()
{
	// More accurate then render.src.fps would be (1000.0 / vga.draw.delay.vtotal)
	return (dbp_forcefps ? dbp_forcefps : render.src.fps);
}

void DBP_Crash(const char* msg)
{
	log_cb(RETRO_LOG_WARN, "[DOSBOX] Crash: %s\n", msg);
	dbp_crash_message = msg;
	if (!render.src.fps) { render.src = { 640, 0, 480, 32, 0, 0, 4.0/3, 70 }; } // crash before first frame
	DBP_Buffer& buf = dbp_buffers[buffer_active];
	if (!buf.video) { buf = { (Bit32u*)calloc(640*480, 4), 640, 480, 640*480*4, 0, 0, 0, 4.0f/3 }; } // crash before first draw
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

bool DBP_WantAutoShutDown()
{
	return (dbp_menu_time >= 0 && dbp_menu_time < 99);
}

void DBP_EnableNetwork()
{
	if (dbp_use_network) return;
	dbp_use_network = true;

	bool running_dos_game = (dbp_had_game_running && !DOSBox_Boot);
	if (running_dos_game && DBP_GetTicks() < 10000) { DBP_ForceReset(); return; }
	DBP_Option::SetDisplay(DBP_Option::modem, true);

	bool pauseThread = (dbp_state != DBPSTATE_BOOT && dbp_state != DBPSTATE_SHUTDOWN);
	if (pauseThread) DBP_ThreadControl(TCM_PAUSE_FRAME);
	Section* sec = control->GetSection("ipx");
	sec->ExecuteDestroy(false);
	sec->GetProp("ipx")->SetValue("true");
	sec->ExecuteInit(false);
	sec = control->GetSection("serial");
	sec->ExecuteDestroy(false);
	sec->GetProp("serial1")->SetValue((DBP_Option::Get(DBP_Option::modem)[0] == 'n') ? "libretro null" : "libretro");
	sec->ExecuteInit(false);
	if (pauseThread) DBP_ThreadControl(TCM_RESUME_FRAME);
}

static std::vector<std::string>& DBP_ScanSystem(bool force_midi_scan)
{
	static std::vector<std::string> dynstr;
	const char *system_dir = NULL;
	struct retro_vfs_interface_info vfs = { 3, NULL };
	if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) || !system_dir || !environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) || vfs.required_interface_version < 3 || !vfs.iface)
		return dynstr;

	dynstr.clear();
	dbp_osimages.clear();
	dbp_shellzips.clear();
	std::string path, subdir;
	std::vector<std::string> subdirs;
	subdirs.emplace_back();
	retro_time_t scan_start = time_cb();
	while (subdirs.size())
	{
		subdir.swap(subdirs.back());
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
				std::string subpath(subdir); subpath.append(subdir.length() ? "/" : "").append(entry_name);
				FILE* f = fopen_wrap(path.assign(system_dir).append("/").append(subpath).c_str(), "rb");
				Bit64u fsize = 0; if (f) { fseek_wrap(f, 0, SEEK_END); fsize = (Bit64u)ftell_wrap(f); fclose(f); }
				if (fsize < 1024*1024*7 || (fsize % 512)) continue; // min 7MB hard disk image made up of 512 byte sectors
				dbp_osimages.emplace_back(std::move(subpath));
			}
			else if (ln > 5 && !strcasecmp(entry_name + ln - 5, ".DOSZ"))
			{
				dbp_shellzips.emplace_back(path.assign(subdir).append(subdir.length() ? "/" : "").append(entry_name));
			}
			else if (ln == 23 && !subdir.length() && !force_midi_scan && !strcasecmp(entry_name, "DOSBoxPureMidiCache.txt"))
			{
				std::string content;
				ReadAndClose(rawFile::TryOpen(path.assign(system_dir).append("/").append(entry_name).c_str()), content);
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

	retro_time_t system_scan_time = (time_cb() - scan_start);
	if (force_midi_scan || (system_scan_time > 2000000 && !dbp_system_cached))
	{
		dbp_system_cached = (system_scan_time > 2000000);
		path.assign(system_dir).append("/").append("DOSBoxPureMidiCache.txt");
		if (!dbp_system_cached)
		{
			vfs.iface->remove(path.c_str());
		}
		else if (FILE* f = fopen_wrap(path.c_str(), "w"))
		{
			for (const std::string& s : dynstr) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			for (const std::string& s : dbp_osimages) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			for (const std::string& s : dbp_shellzips) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			fclose(f);
		}
		if (force_midi_scan) DBP_QueueEvent(DBPET_REFRESHSYSTEM, DBP_NO_PORT);
	}
	return dynstr;
}

#include "dosbox_pure_ver.h"
#include "dosbox_pure_pad.h"
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

Bit8u* GFX_GetPixels(Bitu& pitch)
{
	DBP_Buffer& buf = dbp_buffers[(buffer_active + 1) % 3];
	pitch = buf.width * 4;
	return (Bit8u*)(buf.video + (buf.width * buf.pad_y + buf.pad_x));
}

bool GFX_StartUpdate(Bit8u*& pixels, Bitu& pitch)
{
	if (dbp_state == DBPSTATE_BOOT) return false;
	DBP_FPSCOUNT(dbp_fpscount_gfxstart)
	Bit32u w = (Bit32u)render.src.width, h = (Bit32u)render.src.height, pad_x = 0, pad_y = 0, pad_offset = 0;
	if (dbp_doublescan)
	{
		w <<= (Bit32u)render.src.dblw;
		h <<= (Bit32u)render.src.dblh;
	}
	if ((dbp_overscan || dbp_padding) && !voodoo_is_active())
	{
		if (dbp_padding)
		{
			const float ratio = ((Bit32u)render.src.width << (Bit32u)render.src.dblw) / (((Bit32u)render.src.height << (Bit32u)render.src.dblh) * (float)render.src.ratio);
			pad_x += ((ratio < (4.0f / 3.0f)) ? (Bit32u)((w * ((4.0f / 3.0f) / ratio) - w) / 2.0f + 0.4999f) : (Bit32u)0);
			pad_y += ((ratio > (4.0f / 3.0f)) ? (Bit32u)((h * (ratio / (4.0f / 3.0f)) - h) / 2.0f + 0.4999f) : (Bit32u)0);
		}
		// Try to keep the overscan with a consistent size whether or not double-scanning is enabled.
		Bit32u overscan = (Bit32u)render.src.width * dbp_overscan / (160 >> (dbp_doublescan & (render.src.dblw || render.src.dblh)));
		pad_x += overscan; pad_y += overscan;
		w += pad_x * 2; h += pad_y * 2;
		pad_offset = (w * pad_y + pad_x) * 4;
	}

	DBP_Buffer& buf = dbp_buffers[(buffer_active + 1) % 3];
	if ((buf.width != w) | (buf.height != h) | (buf.pad_x != pad_x) | (buf.pad_y != pad_y))
	{
		if (buf.cap < w * h * 4) buf.video = (Bit32u*)realloc(buf.video, (buf.cap = w * h * 4));
		memset(buf.video, 0, w * h * 4); // clear to black
		buf.width = w; buf.height = h;
		buf.pad_x = pad_x; buf.pad_y = pad_y;
		buf.border_color = 0xDEADBEEF; // force refresh
	}
	pixels = (Bit8u*)buf.video + pad_offset;
	pitch = w * 4;
	return true;
}

void GFX_EndUpdate(const Bit16u *changedLines)
{
	if (!changedLines) return;
	if (dbp_state == DBPSTATE_BOOT) return;

	DBP_Buffer& buf = dbp_buffers[(buffer_active + 1) % 3];
	//DBP_ASSERT((Bit8u*)buf.video == render.scale.outWrite - render.scale.outPitch * render.src.height); // this assert can fail after loading a save game
	DBP_ASSERT(render.scale.outWrite >= (Bit8u*)buf.video && render.scale.outWrite <= (Bit8u*)(buf.video + buf.width * buf.height + (buf.width * buf.pad_y + buf.pad_x) * 4));

	const Bit32u dblw = (Bit32u)render.src.dblw, dblh = (Bit32u)render.src.dblh, srcw = (Bit32u)render.src.width, srch = (Bit32u)render.src.height;
	if (render.aspect)
	{
		if (dbp_doublescan && (dblw | dblh))
		{
			const Bit32u pitch = buf.width, trgpitch = pitch<<dblh, padofs = (pitch * buf.pad_y + buf.pad_x);
			for (Bit32u *pVid = buf.video + padofs, *pLine = pVid + (pitch * (srch - 1)), *pTrgRight = pVid + (trgpitch * (srch - 1) + ((srcw - 1) << dblw)); pLine >= pVid; pLine -= pitch, pTrgRight -= trgpitch)
			{
				Bit32u *src = pLine + srcw, *srcEnd = pLine, *trg = pTrgRight;
				if      (!dblw) for (; src != srcEnd; trg -= 1) trg[0] = trg[pitch] = *(--src);
				else if (!dblh) for (; src != srcEnd; trg -= 2) trg[0] = trg[1] = *(--src);
				else            for (; src != srcEnd; trg -= 2) trg[0] = trg[1] = trg[pitch] = trg[pitch+1] = *(--src);
			}
		}
		buf.ratio = (dbp_padding ? (4.0f / 3.0f) : ((srcw<<dblw) / ((srch<<dblh) * (float)render.src.ratio)));
	}
	else
	{
		// Use square pixels, if the correct aspect ratio is far off, we double or halve the aspect ratio
		float sqr_ratio = ((float)buf.width / buf.height), sqr_to_corr = (((srcw<<dblw) / ((srch<<dblh) * (float)render.src.ratio)) / sqr_ratio);
		buf.ratio = sqr_ratio * (sqr_to_corr > 1.66f ? 2.0f : (sqr_to_corr > 0.6f ? 1.0f : 0.5f));
	}

	if (buf.pad_x | buf.pad_y)
	{
		Bit32u border_color = (Bit32u)GFX_GetRGB(vga.dac.rgb[vga.attr.overscan_color].red<<2, vga.dac.rgb[vga.attr.overscan_color].green<<2, vga.dac.rgb[vga.attr.overscan_color].blue<<2);
		if (border_color != buf.border_color)
		{
			buf.border_color = border_color;
			Bit32u px = buf.pad_x, py = buf.pad_y, w = buf.width, wb = (w - px), *v = buf.video, *topEnd = v + w * py, *bottomStart = v + w * (buf.height - py), *vb, *vr, x;
			for (vb = bottomStart; v != topEnd;) *(v++) = *(vb++) = border_color;
			for (vr = v + wb; v != bottomStart; v += wb, vr += wb) { for (x = 0; x != px; x++) *(v++) = *(vr++) = border_color; }
		}
	}

	if (dbp_intercept_next && dbp_intercept_next->usegfx())
	{
		#ifdef DBP_STANDALONE
		DBP_Buffer& osdbf = dbp_osdbuf[(buffer_active + 1) % 3];
		if (!osdbf.video) osdbf.video = (Bit32u*)malloc(DBPS_OSD_WIDTH*DBPS_OSD_HEIGHT*4);
		memset(osdbf.video, 0, DBPS_OSD_WIDTH*DBPS_OSD_HEIGHT*4);
		dbp_intercept_next->gfx(osdbf);
		#else
		if (dbp_opengl_draw && voodoo_ogl_is_showing()) // zero all including alpha because we'll blend the OSD after displaying voodoo
			memset(buf.video, 0, buf.width * buf.height * 4);
		dbp_intercept_next->gfx(buf);
		#endif
		buf.border_color = 0xDEADBEEF; // force redraw
	}

	#ifndef DBP_ENABLE_FPS_COUNTERS
	if (dbp_perf == DBP_PERF_DETAILED && !DBP_Run::autoinput.ptr)
	#endif
	{
		const DBP_Buffer& lbuf = dbp_buffers[buffer_active];
		bool diff = (!voodoo_ogl_is_showing() ? (!lbuf.video || lbuf.width != buf.width || lbuf.height != buf.height || memcmp(buf.video, lbuf.video, buf.width * buf.height * 4)) : voodoo_ogl_have_new_image());
		if (diff) { DBP_FPSCOUNT(dbp_fpscount_gfxend) dbp_perf_uniquedraw++; }
	}
	buffer_active = (buffer_active + 1) % 3;

	// frameskip is best to be modified in this function (otherwise it can be off by one)
	dbp_framecount += 1 + render.frameskip.max;
	render.frameskip.max = DBP_NeedFrameSkip(true);

	// handle frame skipping and CPU speed during fast forwarding
	const float ffrate = (dbp_throttle.mode != RETRO_THROTTLE_FAST_FORWARD ? 0.0f : (dbp_throttle.rate ? dbp_throttle.rate : -1.0f));
	if (dbp_last_fastforward == ffrate) return;
	static Bit32s old_max;
	static bool old_pmode;
	if (ffrate)
	{
		if (!dbp_last_fastforward) { old_max = CPU_CycleMax; old_pmode = cpu.pmode; }
		if (ffrate > 0 && (dbp_state == DBPSTATE_RUNNING || dbp_state == DBPSTATE_FIRST_FRAME))
		{
			// If fast forwarding at a desired rate, apply custom max cycle rules
			CPU_CycleMax = (Bit32s)(old_max / (CPU_CycleAutoAdjust ? ffrate / av_info.timing.fps : 1.0f));
		}
		else
		{
			CPU_CycleMax = (cpu.pmode ? 30000 : 10000);
		}
	}
	else if (old_max)
	{
		// If we switched to protected mode while locked with auto adjust cycles on, choose a reasonable base rate
		CPU_CycleMax = (old_pmode == cpu.pmode || !CPU_CycleAutoAdjust ? old_max : 20000);
		old_max = 0;
		DBP_SetRealModeCycles();
	}
	dbp_last_fastforward = ffrate;
}

static bool GFX_AdvanceFrame(bool force_skip, bool force_no_auto_adjust)
{
	enum { HISTORY_STEP = 4, HISTORY_SIZE = HISTORY_STEP * 2 };
	static struct
	{
		retro_time_t TimeLast, TimeSleepUntil;
		double LastModeHash;
		Bit32u LastFrameCount, FrameTicks, HistoryCycles[HISTORY_SIZE], HistoryEmulator[HISTORY_SIZE], HistoryFrame[HISTORY_SIZE], HistoryCursor;
	} St;

	St.FrameTicks++;
	if (St.LastFrameCount == dbp_framecount)
	{
		if (dbp_pause_events)
			DBP_ThreadControl(TCM_ON_PAUSE_FRAME);
		return false;
	}

	Bit32u finishedframes = (dbp_framecount - St.LastFrameCount);
	Bit32u finishedticks = St.FrameTicks;
	St.LastFrameCount = dbp_framecount;
	St.FrameTicks = 0;

	// With certain keyboard layouts, we can end up here during startup which we don't want to do anything further
	if (dbp_state == DBPSTATE_BOOT)
	{
		return_true:
		CPU_IODelayRemoved = 0;
		dbp_emu_waiting = 0;
		dbp_paused_work = 0;
		return true;
	}

	if (force_skip)
		goto return_true;

	DBP_ThreadControl(TCM_ON_FINISH_FRAME);

	retro_time_t time_last = St.TimeLast, time_after = time_cb();
	St.TimeLast = time_after;

	if (dbp_perf)
	{
		dbp_perf_count += finishedframes;
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
		goto return_true;
	}

	if (!CPU_CycleAutoAdjust || force_no_auto_adjust)
	{
		St.HistoryEmulator[HISTORY_SIZE-1] = 0;
		St.HistoryCursor = 0;
		goto return_true;
	}

	if (dbp_throttle.mode == RETRO_THROTTLE_FRAME_STEPPING || dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION || dbp_throttle.mode == RETRO_THROTTLE_REWINDING)
		goto return_true;

	extern Bit64s CPU_IODelayRemoved;
	Bit32u hc = St.HistoryCursor % HISTORY_SIZE;
	St.HistoryCycles[hc] = (Bit32u)(((Bit64s)CPU_CycleMax * finishedticks - CPU_IODelayRemoved) / finishedticks);
	St.HistoryEmulator[hc] = (Bit32u)(time_after - time_last - dbp_emu_waiting + dbp_paused_work);
	St.HistoryFrame[hc] = (Bit32u)(time_after - time_last);
	St.HistoryCursor++;

	if ((St.HistoryCursor % HISTORY_STEP) == 0)
	{
		float absFrameTime = (1000000.0f / render.src.fps);
		if (dbp_throttle.rate <= render.src.fps - 1)
			absFrameTime *= render.src.fps / (dbp_throttle.rate + 1);

		Bit32u frameTime = (Bit32u)(absFrameTime * dbp_auto_target);

		Bit32u frameThreshold = 0;
		for (Bit32u f : St.HistoryFrame) frameThreshold += f;
		//float frameMiss = (float)frameThreshold / HISTORY_SIZE - absFrameTime;
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

		//// While a frontend is very busy for example with replay buffer calculation this can help a bit with < 100% speed and sound stuttering
		//static float frameOver;
		//if (frameMiss > 200) frameOver = (frameOver * .8f) + (frameMiss - 200) * .2f;
		//else frameOver = (frameOver > 1 ? frameOver * 0.99f : 0.0f);
		//recentEmulator += (Bit32u)frameOver;

		Bit32s ratio = 0;
		Bit64s recentCyclesMaxSum = (Bit64s)CPU_CycleMax * recentCount;
		if (recentCount > HISTORY_STEP/2 && St.HistoryEmulator[HISTORY_SIZE-1] && recentCyclesMaxSum >= recentCyclesSum && recentEmulator)
		{
			// ignore the cycles added due to the IO delay code in order to have smoother auto cycle adjustments
			double ratio_not_removed = 1.0 - ((double)(recentCyclesMaxSum - recentCyclesSum) / recentCyclesMaxSum);

			// scale ratio we are aiming for (1024 is no change)
			ratio = frameTime * 1024 / recentEmulator;
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

			Bit32s limit = 4000000;
			if (CPU_CycleLimit > 0) limit = CPU_CycleLimit;
			else if (!cpu.pmode && dbp_content_year >= 1995) limit = DBP_CyclesForYear(dbp_content_year, 1996); // enforce max from DBP_SetRealModeCycles
			if (limit > (Bit64s)recentEmulator * 280) limit = (Bit32s)(recentEmulator * 280);
			if (CPU_CycleMax > limit) CPU_CycleMax = limit;
			if (CPU_CycleMax < (cpu.pmode ? 10000 : 1000)) CPU_CycleMax = (cpu.pmode ? 10000 : 1000);
		}

		//log_cb(RETRO_LOG_INFO, "[DBPTIMERS%4d] - EMU: %5d - FE: %5d - TARGET: %5d - EffectiveCycles: %6d - Limit: %6d|%6d - CycleMax: %6d - Scale: %5d\n",
		//	St.HistoryCursor, (int)recentEmulator, (int)((recentFrameSum / recentCount) - recentEmulator), frameTime, 
		//	recentCyclesSum / recentCount, (cpu.pmode ? 10000 : 1000), recentEmulator * 280, CPU_CycleMax, ratio);
	}
	goto return_true;
}

void GFX_Events()
{
	// Some configuration modifications (like keyboard layout) can cause this to be called recursively
	static bool GFX_EVENTS_RECURSIVE;
	if (GFX_EVENTS_RECURSIVE) { /*DBP_ASSERT(false);*/ return; } // it probably isn't recursive anymore since config variable changing was moved to the main thread (though it seems during a page fault BIOS_SetKeyboardLEDOverwrite can cause it)
	GFX_EVENTS_RECURSIVE = true;

	DBP_FPSCOUNT(dbp_fpscount_event)

	bool force_skip = false;
	if (DBP_Run::autoinput.ptr)
	{
		DBP_Run::ProcessAutoInput();
		force_skip = !!DBP_Run::autoinput.ptr;
	}
	if (dbp_audio_remain == -1)
	{
		DBP_MIXER_ScrapAudio();
		dbp_audio_remain = 0; // resume audio
	}
	bool wasFrameEnd = GFX_AdvanceFrame(force_skip, false);

	static bool mouse_speed_up, mouse_speed_down;
	static int mouse_joy_x, mouse_joy_y, hatbits;
	while (dbp_event_queue_read_cursor != dbp_event_queue_write_cursor)
	{
		DBP_Event e = dbp_event_queue[dbp_event_queue_read_cursor];
		dbp_event_queue_read_cursor = ((dbp_event_queue_read_cursor + 1) % DBP_EVENT_QUEUE_SIZE);
		//log_cb(RETRO_LOG_INFO, "[DOSBOX EVENT] [%4d@%6d] %s %08x%s\n", dbp_framecount, DBP_GetTicks(), (e.type > _DBPET_MAX ? "SPECIAL" : DBP_Event_Type_Names[(int)e.type]), (unsigned)e.val, (dbp_intercept_next ? " [INTERCEPTED]" : ""));
		bool intercepted = (dbp_intercept_next && dbp_intercept_next->evnt(e.type, e.val, e.val2));
		if (intercepted && !DBP_IS_RELEASE_EVENT(e.type)) continue;
		#if 0
		if (e.type == DBPET_KEYDOWN && e.val == KBD_b) { void DBP_DumpPSPs(); DBP_DumpPSPs(); }
		#endif
		switch (e.type)
		{
			case DBPET_KEYDOWN:
				if (e.port == DBP_KEYBOARD_PORT) BIOS_SetKeyboardLEDOverwrite((KBD_KEYS)e.val, (KBD_LEDS)e.val2);
				KEYBOARD_AddKey((KBD_KEYS)e.val, true);
				break;
			case DBPET_KEYUP: KEYBOARD_AddKey((KBD_KEYS)e.val, false); break;

			case DBPET_TOGGLEOSD: DBP_StartOSD(); break;
			case DBPET_TOGGLEOSDUP: break;

			case DBPET_ACTIONWHEEL: DBP_WheelShiftOSD(e.port, true); break;
			case DBPET_ACTIONWHEELUP: DBP_WheelShiftOSD(e.port, false); break;

			case DBPET_SHIFTPORT: DBP_WheelShiftOSD(e.port, true, (Bit8u)e.val); break;
			case DBPET_SHIFTPORTUP: DBP_WheelShiftOSD(e.port, false, (Bit8u)e.val); break;

			case DBPET_MOUSEMOVE:
			{
				#ifdef DBP_STANDALONE
				if (dbp_mouse_input != 'p') DBPS_GetMouse(dbp_mouse_x, dbp_mouse_y, false);
				#endif
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
			case DBPET_REFRESHSYSTEM: break;
			default: DBP_ASSERT(false); break;
		}
	}

	if (wasFrameEnd)
	{
		if ((mouse_joy_x || mouse_joy_y) && (abs(mouse_joy_x) > 5 || abs(mouse_joy_y) > 5))
		{
			float mx = mouse_joy_x * dbp_joymouse_speed, my = mouse_joy_y * dbp_joymouse_speed;

			if (!mouse_speed_up && !mouse_speed_down) {}
			else if (mouse_speed_up && mouse_speed_down) mx *= 5, my *= 5;
			else if (mouse_speed_up) mx *= 2, my *= 2;
			else if (mouse_speed_down) mx *= .5f, my *= .5f;

			mx *= dbp_mouse_speed * dbp_mouse_speed_x;
			my *= dbp_mouse_speed;
			Mouse_CursorMoved(mx, my, 0, 0, true);
		}
		#ifdef DBP_STANDALONE
		if (dbps_emu_thread_func)
		{
			dbps_emu_thread_func();
			dbps_emu_thread_func = NULL;
		}
		#endif
	}

	GFX_EVENTS_RECURSIVE = false;
}

void GFX_SetTitle(Bit32s cycles, int frameskip, bool paused)
{
	extern const char* RunningProgram;
	bool was_game_running = dbp_game_running;
	dbp_had_game_running |= (dbp_game_running = (strcmp(RunningProgram, "DOSBOX") && strcmp(RunningProgram, "PUREMENU")));
	log_cb(RETRO_LOG_INFO, "[DOSBOX STATUS] Program: %s - Cycles: %d - Frameskip: %d - Paused: %d\n", RunningProgram, cycles, frameskip, paused);
	if (was_game_running != dbp_game_running && DOSBox_Boot) dbp_refresh_memmaps = true;
	if (cpu.pmode && CPU_CycleAutoAdjust && CPU_OldCycleMax == 3000 && CPU_CycleMax == 3000)
	{
		// Choose a reasonable base rate when switching to protected mode
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

void DBP_ShowSlowLoading()
{
	if (!render.src.fps || !render.src.width) return;

	// Force screen refresh without advancing the emulation (call GFX_StartUpdate to make sure buf is allocated)
	dbp_framecount++;
	buffer_active = (buffer_active + (3-1)) % 3; // go back
	Bit8u* pixels; Bitu pitch; GFX_StartUpdate(pixels, pitch);
	buffer_active = (buffer_active + 1) % 3; // advance again
	DBP_BufferDrawing& buf = (DBP_BufferDrawing&)dbp_buffers[buffer_active];

	// Show loading message
	if (DBP_Run::autoinput.ptr) memset(buf.video, 0, buf.width * buf.height * 4); // keep black during auto input
	buf.DrawBox(20, buf.height - 40, buf.width - 40, 20, buf.BGCOL_MENU | 0x80000000, buf.COL_LINEBOX);
	char msg[24];
	strcpy(msg, "Caching ZIP Structure  ");
	static const char charanim[] = { '|', '/', '-', '\\' };
	msg[22] = charanim[dbp_framecount % 4];
	buf.PrintCenteredOutlined(14, 0, buf.width, buf.height - 37, msg, buf.COL_MENUTITLE, 0x80404020);

	dbp_audio_remain = -1; // Prevent main thread from mixing audio because we could be called while inside MixerChannel::Mix
	GFX_AdvanceFrame(false, true); // can't auto adjust CPU_CycleMax because we're not inside GFX_Events
}

bool DBP_UseDirectMouse()
{
	return (dbp_mouse_input == 'd');
}

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
					else { lbl.swap(result); msg = "Label of drive %c: was changed to '%s'\n"; }
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

static void DBP_PureXCopyProgram(Program** make)
{
	// Bare bones xcopy implementation needed by installers of some games (i.e. GTA)
	struct XCopyProgram : Program
	{
		struct Data
		{
			Program& program;
			bool recurse, emptydirs;
			struct { bool ok; char full[DOS_PATHLENGTH]; DOS_Drive* drive; } srcdst[2];
			int srclen;
			std::string str;
		};
		void Run(void)
		{
			Data d = {*this};
			int n = 0;
			for (unsigned int i = 0, iEnd = cmd->GetCount(); i != iEnd; i++)
			{
				cmd->FindCommand(i + 1, d.str); const char* p = d.str.c_str();
				if (*p == '/') { char c = (p[1]|0x20); d.recurse |= (c == 's' || c == 'e'); d.emptydirs |= (c == 'e'); }
				else { if (n == 2) { goto err; } Bit8u j; if ((d.srcdst[n].ok = DOS_MakeName(p, d.srcdst[n].full, &j))) { d.srcdst[n].drive = Drives[j]; } n++; }
			}
			if (n == 1) { Bit8u j; if ((d.srcdst[n].ok = DOS_MakeName(".", d.srcdst[n].full, &j))) d.srcdst[n].drive = Drives[j]; n++; }
			if (!d.srcdst[0].ok || !d.srcdst[1].ok) { err: WriteOut("Usage error\n"); return; }
			d.srclen = (int)strlen(d.srcdst[0].full);
			FileIter(d.srcdst[0].full, true, 0, 0, 0, 0, (Bitu)&d);
			if (d.srclen) d.srclen++; // now also skip backslash
			DriveFileIterator(d.srcdst[0].drive, FileIter, (Bitu)&d, d.srcdst[0].full);
		}
		static void FileIter(const char* path, bool is_dir, Bit32u size, Bit16u , Bit16u, Bit8u attr, Bitu ptr)
		{
			Data& d = *(Data*)ptr;
			if (is_dir && !d.emptydirs) return;
			const char *subpath = path + d.srclen, *lastslash = strrchr(subpath, '\\');
			if (lastslash && !d.recurse) return;

			d.str.assign(d.srcdst[1].full).append(lastslash ? "\\" : "").append(subpath, (lastslash ? lastslash - subpath : 0));
			if (is_dir || !d.emptydirs) { d.srcdst[1].drive->MakeDir((char*)d.str.c_str()); if (is_dir) return; }
			(d.str += '\\').append(lastslash ? lastslash + 1 : subpath);

			DOS_File *dfSrc, *dfDst;
			if (!d.srcdst[0].drive->FileOpen(&dfSrc, (char*)path, 0)) { d.program.WriteOut("Failed to read %s\n", path); return; }
			d.program.WriteOut("Copying %s\n", path);
			dfSrc->AddRef();
			if (d.srcdst[1].drive->FileCreate(&dfDst, (char*)d.str.c_str(), DOS_ATTR_ARCHIVE))
			{
				dfDst->AddRef();
				dfDst->time = dfSrc->time;
				dfDst->date = dfSrc->date;
				dfDst->newtime = true;
				Bit8u buf[4096];
				for (Bit16u read, write; dfSrc->Read(buf, &(read = sizeof(buf))) && read;)
					if (!dfDst->Write(buf, &(write = read)) || write != read)
						{ dfDst->Close();delete dfDst; goto writeerr; } // disk full?
				dfDst->Close();delete dfDst;
			}
			else { writeerr: d.program.WriteOut("Failed to write %s\n", d.str.c_str()); }
			dfSrc->Close();delete dfSrc;
		}
	};
	*make = new XCopyProgram;
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
	info->library_version  = DOSBOX_PURE_VERSION_STR;
	info->need_fullpath    = true;
	info->block_extract    = true;
	info->valid_extensions = "zip|dosz|exe|com|bat|iso|chd|cue|ins|img|ima|vhd|jrc|m3u|m3u8|conf|/";
}

void retro_set_environment(retro_environment_t cb) //#2
{
	environ_cb = cb;
	bool allow_no_game = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &allow_no_game);
}

static void set_variables(bool force_midi_scan = false)
{
	std::vector<std::string>& dynstr = DBP_ScanSystem(force_midi_scan);

	retro_core_option_v2_definition& def = option_defs[DBP_Option::midi];
	size_t i = 0, numfiles = (dynstr.size() > (RETRO_NUM_CORE_OPTION_VALUES_MAX-4)*2 ? (RETRO_NUM_CORE_OPTION_VALUES_MAX-4)*2 : dynstr.size());
	for (size_t f = 0; f != numfiles; f += 2)
		if (((&dynstr[f].back())[-1]|0x20) == 'f') // .SF* extension soundfont
			def.values[i++] = { dynstr[f].c_str(), dynstr[f+1].c_str() };
	for (size_t f = 0; f != numfiles; f += 2)
		if (((&dynstr[f].back())[-1]|0x20) != 'f') // .ROM extension munt rom
			def.values[i++] = { dynstr[f].c_str(), dynstr[f+1].c_str() };
	#ifndef DBP_STANDALONE
	def.values[i++] = { "frontend", "Frontend MIDI driver" };
	#else
	def.values[i++] = { "system", "System MIDI driver" };
	#endif
	def.values[i++] = { "disabled", "Disabled" };
	if (dbp_system_cached)
		def.values[i++] = { "scan", (!strcmp(DBP_Option::Get(DBP_Option::midi), "scan") ? "System directory scan finished" : "Scan System directory for soundfonts (open this menu again after)") };
	def.values[i] = { 0, 0 };
	def.default_value = def.values[0].value;

	unsigned options_ver = 0;
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &options_ver);
	if (options_ver >= 2)
	{
		// Category options support
		static const struct retro_core_options_v2 options = { option_cats, option_defs };
		environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void*)&options);
	}
	else if (options_ver == 1)
	{
		// Convert options to V1 format
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
		// Convert options to legacy format
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
		environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)&v0defs[0]);
	}
}

const char* DBP_Option::Get(DBP_Option::Index idx, bool* was_modified)
{
	retro_core_option_v2_definition& def = option_defs[idx];
	retro_variable var = { def.key };
	const char* v = (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : def.default_value);
	if (!was_modified) return v;
	Bit32u oldhash = (Bit32u)(size_t)(void*)def.values[RETRO_NUM_CORE_OPTION_VALUES_MAX-3].label, newhash = BaseStringToPointerHashMap::Hash(v);
	if (oldhash && oldhash != newhash) *was_modified = true;
	def.values[RETRO_NUM_CORE_OPTION_VALUES_MAX-3].label = (const char*)(void*)(size_t)newhash;
	return v;
}

bool DBP_Option::Apply(Section& section, const char* var_name, const char* new_value, bool disallow_in_game, bool need_restart, bool user_modified)
{
	if (!control) return false;

	Property* prop = section.GetProp(var_name);
	if (prop->IsFixed())
	{
		if (user_modified) retro_notify(0, RETRO_LOG_WARN, "Unable to change setting which was fixed with game configuration");
		return false;
	}

	const Value& propVal = prop->GetValue();
	if (!strcmp(new_value, (propVal.type == Value::V_STRING ? (const char*)propVal : propVal.ToString().c_str()))) return false;

	bool reInitSection = (dbp_state != DBPSTATE_BOOT);
	if ((disallow_in_game && dbp_game_running) || (need_restart && reInitSection))
	{
		if (disallow_in_game && user_modified)
			retro_notify(0, RETRO_LOG_WARN, "Unable to change value while game is running");
		else if ((dbp_game_running || DBP_OSD.ptr._all == NULL || !DBP_FullscreenOSD) && user_modified)
			retro_notify(2000, RETRO_LOG_INFO, "Setting will be applied after restart");
		DBP_Run::startup.reboot = true;
		reInitSection = false;
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
			section.ExecuteDestroy(false);
			sectionExecInit = true;
		}
	}
	bool res = prop->SetValue(new_value);
	DBP_ASSERT(res && prop->GetValue().ToString() == new_value);
	if (sectionExecInit) section.ExecuteInit(false);
	if (reInitSection) DBP_ThreadControl(TCM_RESUME_FRAME);
	return true;
}

bool DBP_Option::GetAndApply(Section& section, const char* var_name, DBP_Option::Index idx, bool disallow_in_game, bool need_restart)
{
	bool user_modified = false;
	const char* new_value = Get(idx, &user_modified);
	return Apply(section, var_name, new_value, disallow_in_game, need_restart, user_modified);
}

void DBP_Option::SetDisplay(DBP_Option::Index idx, bool visible)
{
	retro_core_option_v2_definition& def = option_defs[idx];
	retro_core_option_display disp = { def.key, visible };
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &disp);
	def.values[RETRO_NUM_CORE_OPTION_VALUES_MAX-2].label = (const char*)(void*)(size_t)(!visible);
}

bool DBP_Option::GetHidden(const retro_core_option_v2_definition& d) { return !!(size_t)(void*)d.values[RETRO_NUM_CORE_OPTION_VALUES_MAX-2].label; }

static bool check_variables()
{
	// Depending on this we call set_variables, which needs to be done before any DBP_Option::SetDisplay call
	bool midi_changed = false;
	const char* midi = DBP_Option::Get(DBP_Option::midi, &midi_changed);
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
			if (dbp_state != DBPSTATE_BOOT) set_variables(); // just update label on "scan" option
			dbp_system_scannable = true;
		}
	}

	char buf[32];
	unsigned options_ver = 0;
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &options_ver);
	bool visibility_changed = false;

	#ifdef DBP_STANDALONE
	int interface_crtfilter = atoi(DBP_Option::Get(DBP_Option::interface_crtfilter));
	DBP_Option::SetDisplay(DBP_Option::interface_crtscanline, !!interface_crtfilter);
	DBP_Option::SetDisplay(DBP_Option::interface_crtblur, !!interface_crtfilter);
	DBP_Option::SetDisplay(DBP_Option::interface_crtmask, !!interface_crtfilter);
	DBP_Option::SetDisplay(DBP_Option::interface_crtcurvature, !!interface_crtfilter);
	DBP_Option::SetDisplay(DBP_Option::interface_crtcorner, !!interface_crtfilter);
	#endif

	dbp_actionwheel_inputs = (Bit8u)atoi(DBP_Option::Get(DBP_Option::actionwheel_inputs));
	dbp_auto_mapping_mode = DBP_Option::Get(DBP_Option::auto_mapping)[0];

	bool old_strict_mode = dbp_strict_mode;
	dbp_strict_mode = (DBP_Option::Get(DBP_Option::strict_mode)[0] == 't');
	if (old_strict_mode != dbp_strict_mode && dbp_state != DBPSTATE_BOOT && !dbp_game_running)
		dbp_state = DBPSTATE_REBOOT;

	Section &sec_dosbox   = *control->GetSection("dosbox"),   &sec_dos = *control->GetSection("dos"), &sec_mixer    = *control->GetSection("mixer"), &sec_midi = *control->GetSection("midi"), 
	        &sec_speaker  = *control->GetSection("speaker"),  &sec_cpu = *control->GetSection("cpu"), &sec_render   = *control->GetSection("render"),
	        &sec_sblaster = *control->GetSection("sblaster"), &sec_gus = *control->GetSection("gus"), &sec_joystick = *control->GetSection("joystick");

	bool machine_changed = false;
	const char new_mchar = (dbp_reboot_machine ? dbp_reboot_machine : DBP_Option::Get(DBP_Option::machine, &machine_changed)[0]), *new_machine = "svga_s3";
	switch (new_mchar)
	{
		case 's': new_machine = DBP_Option::Get(DBP_Option::svga, &machine_changed); break;
		case 'v': new_machine = "vgaonly"; break;
		case 'e': new_machine = "ega"; break;
		case 'c': new_machine = "cga"; break;
		case 't': new_machine = "tandy"; break;
		case 'h': new_machine = "hercules"; break;
		case 'p': new_machine = "pcjr"; break;
	}
	visibility_changed |= DBP_Option::Apply(sec_dosbox, "machine", new_machine, false, true, machine_changed);
	DBP_Option::GetAndApply(sec_dosbox, "vmemsize", DBP_Option::svgamem, false, true);
	if (dbp_reboot_machine) dbp_reboot_machine = 0;
	const char cur_mchar = (dbp_state == DBPSTATE_BOOT ? '\0' : (machine == MCH_VGA && svgaCard != SVGA_None) ? 's' : machine == MCH_CGA ? 'c' : machine == MCH_HERC ? 'h' : '\0'); // need only these 3
	const bool show_svga = (new_mchar == 's' || cur_mchar == 's'), show_cga = (new_mchar == 'c' || cur_mchar == 'c'), show_hercules = (new_mchar == 'h' || cur_mchar == 'h');
	const char active_mchar = (dbp_state == DBPSTATE_BOOT ? new_mchar : cur_mchar);

	bool mem_changed = false;
	const char* mem = DBP_Option::Get(DBP_Option::memory_size, &mem_changed);
	if (dbp_reboot_set64mem) mem = "64";
	bool mem_use_extended = (atoi(mem) > 0);
	DBP_Option::Apply(sec_dos, "xms", (mem_use_extended ? "true" : "false"), true);
	DBP_Option::Apply(sec_dos, "ems", (mem_use_extended ? "true" : "false"), true);
	DBP_Option::Apply(sec_dosbox, "memsize", (mem_use_extended ? mem : "16"), false, true, mem_changed);

	bool audiorate_changed = false;
	#ifndef DBP_STANDALONE
	const char* audiorate = DBP_Option::Get(DBP_Option::audiorate, &audiorate_changed);
	#else
	const char* audiorate = "44100";
	#endif
	DBP_Option::Apply(sec_mixer, "rate", audiorate, false, true, audiorate_changed);
	DBP_Option::GetAndApply(sec_mixer, "swapstereo", DBP_Option::swapstereo);
	extern bool dbp_swapstereo;
	dbp_swapstereo = (bool)control->GetProp("mixer", "swapstereo")->GetValue(); // to also get dosbox.conf override

	if (dbp_state == DBPSTATE_BOOT)
	{
		DBP_Option::Apply(sec_sblaster, "oplrate",   audiorate);
		DBP_Option::Apply(sec_speaker,  "pcrate",    audiorate);
		DBP_Option::Apply(sec_speaker,  "tandyrate", audiorate);

		// initiate audio buffer, we don't need SDL specific behavior, so just set a large enough buffer
		DBP_Option::Apply(sec_mixer, "prebuffer", "0");
		DBP_Option::Apply(sec_mixer, "blocksize", "2048");
	}

	// Emulation options
	const char* forcefps = DBP_Option::Get(DBP_Option::forcefps);
	dbp_forcefps = (Bit16s)(forcefps[0] == 'f' ? 0 : forcefps[0] == 't' ? 60 : atoi(forcefps));
	if (dbp_forcefps < 0) dbp_forcefps = 0;

	switch (DBP_Option::Get(DBP_Option::perfstats)[0])
	{
		case 's': dbp_perf = DBP_PERF_SIMPLE; break;
		case 'd': dbp_perf = DBP_PERF_DETAILED; break;
		default:  dbp_perf = DBP_PERF_NONE; break;
	}
	#ifndef DBP_STANDALONE
	switch (DBP_Option::Get(DBP_Option::savestate)[0])
	{
		case 'd': dbp_serializemode = DBPSERIALIZE_DISABLED; break;
		case 'r': dbp_serializemode = DBPSERIALIZE_REWIND; break;
		default: dbp_serializemode = DBPSERIALIZE_STATES; break;
	}
	#endif
	DBPArchive::accomodate_delta_encoding = (dbp_serializemode == DBPSERIALIZE_REWIND);
	dbp_conf_loading = DBP_Option::Get(DBP_Option::conf)[0];
	dbp_menu_time = (char)atoi(DBP_Option::Get(DBP_Option::menu_time));

	bool cycles_changed = false;
	const char* cycles = DBP_Option::Get(DBP_Option::cycles, &cycles_changed);
	bool cycles_numeric = (cycles[0] >= '0' && cycles[0] <= '9');
	int cycles_max = (cycles_numeric ? 0 : atoi(DBP_Option::Get(DBP_Option::cycles_max, &cycles_changed)));
	DBP_Option::SetDisplay(DBP_Option::cycles_max, !cycles_numeric);
	DBP_Option::SetDisplay(DBP_Option::cycles_scale, cycles_numeric || cycles_max > 0);
	DBP_Option::SetDisplay(DBP_Option::cycle_limit, !cycles_numeric);
	if (cycles_numeric)
	{
		snprintf(buf, sizeof(buf), "%d", (int)(atoi(cycles) * (float)atof(DBP_Option::Get(DBP_Option::cycles_scale, &cycles_changed)) + .499));
		cycles = buf;
	}
	else if (cycles_max > 0)
	{
		snprintf(buf, sizeof(buf), "%s limit %d", cycles, (int)(cycles_max * (float)atof(DBP_Option::Get(DBP_Option::cycles_scale, &cycles_changed)) + .499));
		cycles = buf;
	}
	visibility_changed |= DBP_Option::Apply(sec_cpu, "cycles", cycles, false, false, cycles_changed);

	dbp_auto_target = (1.0f * (cycles_numeric ? 1.0f : (float)atof(DBP_Option::Get(DBP_Option::cycle_limit)))) - 0.0075f; // was - 0.01f

	bool cpu_core_changed = false;
	const char* cpu_core = ((DOSBox_Boot && DBP_Option::Get(DBP_Option::bootos_forcenormal, &cpu_core_changed)[0] == 't') ? "normal" : DBP_Option::Get(DBP_Option::cpu_core, &cpu_core_changed));
	DBP_Option::Apply(sec_cpu, "core", cpu_core, false, false, cpu_core_changed);
	DBP_Option::GetAndApply(sec_cpu, "cputype", DBP_Option::cpu_type, true);

	DBP_Option::SetDisplay(DBP_Option::modem, dbp_use_network);
	if (dbp_use_network)
		DBP_Option::Apply(*control->GetSection("serial"), "serial1", ((DBP_Option::Get(DBP_Option::modem)[0] == 'n') ? "libretro null" : "libretro"));

	DBP_Option::SetDisplay(DBP_Option::svga, show_svga);
	DBP_Option::SetDisplay(DBP_Option::svgamem, show_svga);
	DBP_Option::SetDisplay(DBP_Option::voodoo, show_svga);
	DBP_Option::SetDisplay(DBP_Option::voodoo_perf, show_svga);
	DBP_Option::SetDisplay(DBP_Option::voodoo_gamma, show_svga);
	DBP_Option::SetDisplay(DBP_Option::voodoo_scale, show_svga);
	if (active_mchar == 's')
	{
		Section& sec_pci = *control->GetSection("pci");
		DBP_Option::GetAndApply(sec_pci, "voodoo", DBP_Option::voodoo, true, true);
		const char* voodoo_perf = DBP_Option::Get(DBP_Option::voodoo_perf);
		DBP_Option::Apply(sec_pci, "voodoo_perf", ((voodoo_perf[0] == 'a' || voodoo_perf[0] == '4') ? (dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE ? "1" : "4") : voodoo_perf));
		if (dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE && (atoi(voodoo_perf) & 0x4))
			retro_notify(0, RETRO_LOG_WARN, "To enable OpenGL hardware rendering, close and re-open.");
		DBP_Option::GetAndApply(sec_pci, "voodoo_gamma", DBP_Option::voodoo_gamma);
		DBP_Option::GetAndApply(sec_pci, "voodoo_scale", DBP_Option::voodoo_scale);
	}

	DBP_Option::SetDisplay(DBP_Option::cga, show_cga);
	if (active_mchar == 'c')
	{
		const char* cga = DBP_Option::Get(DBP_Option::cga);
		bool cga_new_model = false;
		const char* cga_mode = NULL;
		if (!memcmp(cga, "early_", 6)) { cga_new_model = false; cga_mode = cga + 6; }
		if (!memcmp(cga, "late_",  5)) { cga_new_model = true;  cga_mode = cga + 5; }
		DBP_CGA_SetModelAndComposite(cga_new_model, (!cga_mode || cga_mode[0] == 'a' ? 0 : ((cga_mode[0] == 'o' && cga_mode[1] == 'n') ? 1 : 2)));
	}

	DBP_Option::SetDisplay(DBP_Option::hercules, show_hercules);
	if (active_mchar == 'h')
	{
		const char herc_mode = DBP_Option::Get(DBP_Option::hercules)[0];
		DBP_Hercules_SetPalette(herc_mode == 'a' ? 1 : (herc_mode == 'g' ? 2 : 0));
	}

	const char* dbp_aspectratio = DBP_Option::Get(DBP_Option::aspect_correction);
	DBP_Option::Apply(sec_render, "aspect", (dbp_aspectratio[0] == 'f' ? "false" : "true"));
	dbp_padding = (dbp_aspectratio[0] == 'p');
	dbp_doublescan = (dbp_aspectratio[0] == 'd' || (dbp_padding && !strcmp(dbp_aspectratio, "padded-doublescan")));
	dbp_overscan = (unsigned char)atoi(DBP_Option::Get(DBP_Option::overscan));

	bool sblaster_changed = false;
	const char* sblaster_conf = DBP_Option::Get(DBP_Option::sblaster_conf, &sblaster_changed);
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
		DBP_Option::Apply(sec_sblaster, sb_props[i], buf, false, false, sblaster_changed);
	}

	std::string soundfontpath;
	if (!*midi || !strcmp(midi, "disabled") || !strcasecmp(midi, "none")) midi = "";
	else if (strcmp(midi, "frontend") && strcmp(midi, "scan") && strcmp(midi, "system"))
		midi = (soundfontpath = DBP_GetSaveFile(SFT_SYSTEMDIR)).append(midi).c_str();
	DBP_Option::Apply(sec_midi, "midiconfig", (strcmp(midi, "system") ? midi : ""), false, false, midi_changed);
	DBP_Option::Apply(sec_midi, "mpu401", (*midi ? "intelligent" : "none"), false, false, midi_changed);

	DBP_Option::GetAndApply(sec_sblaster, "sbtype",  DBP_Option::sblaster_type);
	DBP_Option::GetAndApply(sec_sblaster, "oplmode", DBP_Option::sblaster_adlib_mode);
	DBP_Option::GetAndApply(sec_sblaster, "oplemu",  DBP_Option::sblaster_adlib_emu);
	DBP_Option::GetAndApply(sec_gus,      "gus",     DBP_Option::gus);
	DBP_Option::GetAndApply(sec_speaker,  "tandy",   DBP_Option::tandysound);
	DBP_Option::GetAndApply(sec_joystick, "timed",   DBP_Option::joystick_timed);

	// Keyboard layout can't be change in protected mode (extracting keyboard layout doesn't work when EMS/XMS is in use)
	DBP_Option::GetAndApply(sec_dos, "keyboardlayout", DBP_Option::keyboard_layout, true);

	DBP_PadMapping::CheckInputVariables();

	dbp_alphablend_base = (Bit8u)((atoi(DBP_Option::Get(DBP_Option::menu_transparency)) + 30) * 0xFF / 130);
	dbp_joy_analog_deadzone = (int)((float)atoi(DBP_Option::Get(DBP_Option::joystick_analog_deadzone)) * 0.01f * (float)DBP_JOY_ANALOG_RANGE);

	return visibility_changed;
}

static void init_dosbox_load_dosboxconf(const std::string& cfg, Section*& ref_autoexec, bool force_puremenu, bool& skip_c_mount)
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
				if (section == ref_autoexec)
				{
					if (force_puremenu) continue; // don't read autoexec if forcing menu
					ref_autoexec = NULL; // otherwise skip loading the menu with this
				}
				if (!section || !section->HandleInputline(line))
				{
					lowcase(line); // overwrite skipping of c mounting with 'automount = true'
					if (line.find("automount") != std::string::npos && line.find("true") != std::string::npos) skip_c_mount = false;
					continue;
				}
				if ((loc = line.find('=')) == std::string::npos) continue;
				trim(line.erase(loc));
				if (Property* p = section->GetProp(line.c_str())) p->MarkFixed();
		}
	}
}

static void init_dosbox_parse_drives()
{
	DBP_PadMapping::Load(); // if loaded don't show auto map notification

	struct Local { static void FileIter(const char* path, bool is_dir, Bit32u size, Bit16u, Bit16u, Bit8u, Bitu data)
	{
		if (is_dir) return;
		const char* lastslash = strrchr(path, '\\'), *fname = (lastslash ? lastslash + 1 : path);

		// Check mountable disk images on drive C
		const char* fext = (data == ('C'-'A') ? strrchr(fname, '.') : NULL);
		if (fext++)
		{
			bool isFS = (!strcmp(fext, "ISO") || !strcmp(fext, "CHD") || !strcmp(fext, "CUE") || !strcmp(fext, "INS") || !strcmp(fext, "IMG") || !strcmp(fext, "IMA") || !strcmp(fext, "VHD") || !strcmp(fext, "JRC"));
			if (isFS && !strncmp(fext, "IM", 2))
			{
				DOS_File *df;
				if (size < 163840 || ((size % 512) && (size % 2352))) isFS = false; // validate generic image (disk or cd)
				else if (size < 2949120) // validate floppy images
				{
					for (Bit32u i = 0, dgsz;; i++) if ((dgsz = DiskGeometryList[i].ksize * 1024) == size || dgsz + 1024 == size) goto img_is_fs;
					isFS = false;
					img_is_fs:;
				}
				else if (!Drives[data]->FileOpen(&df, (char*)path, OPEN_READ)) { DBP_ASSERT(0); isFS = false; }
				else // validate mountable hard disk / cd
				{
					df->AddRef();
					imageDisk* id = new imageDisk(df, "", (size / 1024), true);
					if (!id->Set_GeometryForHardDisk()) isFS = DBP_IsDiskCDISO(id);
					delete id; // closes and deletes df
				}
			}
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
				retro_notify(0, RETRO_LOG_INFO, "Detected Automatic GamePad mappings for %s", static_title.c_str());
			return;
		}
	}};

	for (int i = 0; i != ('Z'-'A'); i++)
		if (Drives[i])
			DriveFileIterator(Drives[i], Local::FileIter, i);

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

static void init_dosbox(bool forcemenu = false, bool reinit = false, const std::string* dbconf = NULL)
{
	if (reinit)
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
		dbp_audio_remain = 0;
		DBP_SetIntercept(NULL);
		for (size_t i = dbp_images.size(); i--;)
		{
			DBP_Image& img = dbp_images[i];
			if (!dbp_wasloaded || img.imgmount) dbp_images.erase(dbp_images.begin() + i);
			else { img.remount = img.mounted; img.mounted = false; }
		}
	}

	const char* path = (dbp_content_path.empty() ? NULL : dbp_content_path.c_str()), *path_file, *path_ext, *path_fragment; size_t path_namelen;
	if (path && DBP_ExtractPathInfo(path, &path_file, &path_namelen, &path_ext, &path_fragment) && !dbp_wasloaded)
	{
		dbp_content_name = std::string(path_file, path_namelen);
		#ifdef DBP_STANDALONE
		DBPS_OnContentLoad(dbp_content_name.c_str(), path, (path_file > path ? (size_t)(path_file - path - 1) : 0));
		#endif
	}
	const int path_extlen = (path ? (int)((path_fragment ? path_fragment : path + dbp_content_path.length()) - path_ext) : 0);
	const bool newcontent = !dbp_wasloaded, force_puremenu = forcemenu || (dbp_biosreboot && dbp_wasloaded);
	if (newcontent) dbp_biosreboot = dbp_reboot_set64mem = false; // ignore this when switching content
	if (newcontent && !reinit) dbp_auto_mapping = NULL; // re-acquire when switching content

	// Loading a .conf file behaves like regular DOSBox (no union drive mounting, save file, start menu, etc.)
	dbp_skip_c_mount = (path_extlen == 4 && !strncasecmp(path_ext, "conf", 4));
	if (dbp_skip_c_mount && !dbconf)
	{
		std::string confcontent;
		if (ReadAndClose(rawFile::TryOpen(path), confcontent))
			return init_dosbox(forcemenu, reinit, &confcontent);
	}

	control = new Config();
	DOSBOX_Init();
	Section* autoexec = control->GetSection("autoexec");
	if (dbconf) init_dosbox_load_dosboxconf(*dbconf, autoexec, force_puremenu, dbp_skip_c_mount);
	DBP_Run::PreInit(newcontent && !reinit);
	check_variables();
	dbp_boot_time = time_cb();
	control->Init();
	PROGRAMS_MakeFile("PUREMENU.COM", DBP_PureMenuProgram);
	PROGRAMS_MakeFile("LABEL.COM", DBP_PureLabelProgram);
	PROGRAMS_MakeFile("REMOUNT.COM", DBP_PureRemountProgram);
	PROGRAMS_MakeFile("XCOPY.COM", DBP_PureXCopyProgram);

	if (!dbp_skip_c_mount)
	{
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
			else
			{
				Drives['C'-'A'] = union_underlay; // set for DBP_GetSaveFile
				std::string save_name_redirect = DBP_GetSaveFile(SFT_SAVENAMEREDIRECT);
				if (!save_name_redirect.empty()) save_file.swap(save_name_redirect);
			}
			unionDrive* uni = new unionDrive(*union_underlay, (save_file.empty() ? NULL : &save_file[0]), true, dbp_strict_mode);
			Drives['C'-'A'] = uni;
			mem_writeb(Real2Phys(dos.tables.mediaid) + ('C'-'A') * 9, uni->GetMediaByte());
		}
	}

	// Detect content year and auto mapping
	if (newcontent && !reinit)
	{
		init_dosbox_parse_drives();

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

		// Check if DOS.YML needs a reboot (after evaluating dbp_images)
		if (DBP_Run::PostInitFirstTime())
			return init_dosbox(forcemenu, true, dbconf);
	}

	if (DOS_Drive* drive_c = Drives['C'-'A']) // guaranteed not NULL unless dbp_skip_c_mount
	{
		if (dbp_conf_loading != 'f' && !reinit && !dbconf)
		{
			DOS_File* conffile = NULL; std::string strconfpath, confcontent;
			if (dbp_conf_loading == 'i') // load confs 'i'nside content
			{
				if (drive_c->FileExists(("$C:\\DOSBOX.CON")+4)) { conffile = FindAndOpenDosFile("$C:\\DOSBOX.CON"); } //8.3 filename in ZIPs
				else if (drive_c->FileExists(("$C:\\DOSBOX~1.CON")+4)) { conffile = FindAndOpenDosFile("$C:\\DOSBOX~1.CON"); } //8.3 filename in local file systems
			}
			else if (dbp_conf_loading == 'o' && path) // load confs 'o'utside content
			{
				conffile = rawFile::TryOpen(strconfpath.assign(path, path_ext - path).append(path_ext[-1] == '.' ? 0 : 1, '.').append("conf").c_str());
			}
			if (conffile && ReadAndClose(conffile, confcontent))
				return init_dosbox(forcemenu, true, &confcontent);
		}

		// Try to load either DOSBOX.SF2 or a pair of MT32_CONTROL.ROM/MT32_PCM.ROM from the mounted C: drive and use as fixed midi config
		const char* mountedMidi;
		if (drive_c->FileExists((mountedMidi = "$C:\\DOSBOX.SF2")+4) || (drive_c->FileExists(("$C:\\MT32_PCM.ROM")+4) && (drive_c->FileExists((mountedMidi = "$C:\\MT32TROL.ROM")+4) || drive_c->FileExists((mountedMidi = "$C:\\MT32_C~1.ROM")+4))))
		{
			Section* sec = control->GetSection("midi");
			Property* prop = sec->GetProp("midiconfig");
			sec->ExecuteDestroy(false);
			prop->SetValue(mountedMidi);
			prop->MarkFixed();
			sec->ExecuteInit(false);
		}
	}

	// Always start network again when it has been used once (or maybe we're restarting to start it up the first time)
	if (dbp_use_network) { dbp_use_network = false; DBP_EnableNetwork(); }

	// Joysticks are refreshed after control modifications but needs to be done here also to happen on core restart
	DBP_PadMapping::RefreshDosJoysticks();

	// Always switch to the C: drive directly (for puremenu, to run DOSBOX.BAT and to run the autoexec of the dosbox conf)
	// For DBP we modified init_line to always run Z:\AUTOEXEC.BAT and not just any AUTOEXEC.BAT of the current drive/directory
	if (DOS_SetDrive('C'-'A') && autoexec) // SetDrive will fail if dbp_skip_c_mount
	{
		bool auto_mount = true;
		autoexec->ExecuteDestroy();
		if (!force_puremenu && dbp_menu_time != (signed char)-1 && path_extlen == 3 && (!strncasecmp(path_ext, "EXE", 3) || !strncasecmp(path_ext, "COM", 3) || !strncasecmp(path_ext, "BAT", 3)) && !Drives['C'-'A']->FileExists("AUTOBOOT.DBP"))
		{
			((((((static_cast<Section_line*>(autoexec)->data += "echo off") += '\n') += ((path_ext[0]|0x20) == 'b' ? "call " : "")) += path_file) += '\n') += "Z:PUREMENU") += " -FINISH\n";
		}
		else if (!force_puremenu && Drives['C'-'A']->FileExists("DOSBOX.BAT"))
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
			if (!newcontent && (dbp_images[i].path[0] == '$' && !Drives[dbp_images[i].path[1]-'A']))
				dbp_images.erase(dbp_images.begin() + (i--));
			else if (newcontent || dbp_images[i].remount)
				DBP_Mount(i, dbp_images[i].remount);
		if (!newcontent) dbp_image_index = (active_disk_image_index >= dbp_images.size() ? 0 : active_disk_image_index);
	}
	dbp_biosreboot = dbp_reboot_set64mem = false;
	dbp_wasloaded = true;
	DBP_ReportCoreMemoryMaps();

	// Clear any dos errors that could have been set by drive file access until now
	dos.errorcode = DOSERR_NONE;

	struct Local { static Thread::RET_t THREAD_CC ThreadDOSBox(void*)
	{
		control->StartUp();
		DBP_ThreadControl(TCM_ON_SHUTDOWN);
		return 0;
	}};

	// Start DOSBox thread
	dbp_frame_pending = true;
	dbp_state = DBPSTATE_FIRST_FRAME;
	Thread::StartDetached(Local::ThreadDOSBox);
}

// This is called on the main thread
void DBP_ForceReset(bool forcemenu) { init_dosbox(forcemenu); } 

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
		{RETROK_BACKQUOTE, KBD_grave}, {RETROK_OEM_102, KBD_extra_lt_gt}
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
			if (keycode >= RETROK_LAST) return;
			int val = dbp_keymap_retro2dos[keycode];
			if (!val) return;
			if (down && !dbp_keys_down[val])
			{
				int leds = ((key_modifiers & RETROKMOD_NUMLOCK) ? KLED_NUMLOCK : 0) | ((key_modifiers & RETROKMOD_CAPSLOCK) ? KLED_CAPSLOCK : 0) | ((key_modifiers & RETROKMOD_SCROLLOCK) ? KLED_SCROLLLOCK : 0);
				dbp_keys_down[val] |= DBP_DOWN_BY_KEYBOARD;
				DBP_QueueEvent(DBPET_KEYDOWN, DBP_KEYBOARD_PORT, val, leds);
			}
			else if (!down && (dbp_keys_down[val] & DBP_DOWN_BY_KEYBOARD))
			{
				dbp_keys_down[val] = 1;
				DBP_QueueEvent(DBPET_KEYUP, DBP_KEYBOARD_PORT, val);
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
				DBP_Mount(dbp_image_index);
			DBP_SetMountSwappingRequested(); // set swapping_requested flag for CMscdex::GetMediaStatus
			DBP_ThreadControl(TCM_RESUME_FRAME);
			DBP_QueueEvent(DBPET_CHANGEMOUNTS, DBP_NO_PORT);
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
				const char *lastSlash = strrchr(info->path, '/'), *lastBackSlash = strrchr(info->path, '\\');
				dbp_images[index].path = info->path;
				dbp_images[index].longpath.clear();
				dbp_images[index].dirlen = (int)((lastSlash && lastSlash > lastBackSlash ? lastSlash + 1 : (lastBackSlash ? lastBackSlash + 1 : info->path)) - info->path);
				dbp_images[index].dd = 0;
			}
			return true;
		}

		static bool RETRO_CALLCONV add_image_index()
		{
			dbp_images.resize(dbp_images.size() + 1);
			dbp_images.back().dirlen = 0;
			dbp_images.back().dd = 0;
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
	#ifndef DBP_STANDALONE
	enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format))
	{
		retro_notify(0, RETRO_LOG_ERROR, "Frontend does not support XRGB8888.\n");
		return false;
	}

	const char* voodoo_perf = DBP_Option::Get(DBP_Option::voodoo_perf);
	#else
	const char* voodoo_perf = "auto"; // standalone always uses OpenGL rendering
	#endif
	if (voodoo_perf[0] == 'a' || voodoo_perf[0] == '4') // 3dfx wants to use OpenGL, request hardware render context
	{
		static struct sglproc { retro_proc_address_t* ptr; const char* name; bool required; } glprocs[] = { MYGL_FOR_EACH_PROC(MYGL_MAKEPROCARRENTRY) };
		static unsigned prog_dosboxbuffer, vbo, vao, tex, fbo, lastw, lasth;

		static const Bit8u testhwcontexts[] = { RETRO_HW_CONTEXT_OPENGL_CORE, RETRO_HW_CONTEXT_OPENGLES_VERSION, RETRO_HW_CONTEXT_OPENGLES3, RETRO_HW_CONTEXT_OPENGLES2, RETRO_HW_CONTEXT_OPENGL };
		struct HWContext
		{
			static void Reset(void)
			{
				bool missRequired = false;
				for (sglproc& glproc : glprocs)
				{
					*glproc.ptr = dbp_hw_render.get_proc_address(glproc.name);
					if (!*glproc.ptr)
					{
						//GFX_ShowMsg("[DBP:GL] OpenGL Function %s is not available!", glproc.name);
						char buf[256], *arboes = buf + strlen(glproc.name);
						memcpy(buf, glproc.name, (arboes - buf));
						memcpy(arboes, "ARB", 4);
						*glproc.ptr = dbp_hw_render.get_proc_address(buf);
						if (!*glproc.ptr)
						{
							//GFX_ShowMsg("[DBP:GL] OpenGL Function %s is not available!", buf);
							memcpy(arboes, "OES", 4);
							*glproc.ptr = dbp_hw_render.get_proc_address(buf);
							if (!*glproc.ptr)
							{
								GFX_ShowMsg("[DBP:GL] %s OpenGL Function %s is not available!", (glproc.required ? "Required" : "Optional"), glproc.name);
								if (glproc.required) { DBP_ASSERT(0); missRequired = true; }
							}
							else GFX_ShowMsg("[DBP:GL] Using OpenGL extension function %s", buf);
						}
						else GFX_ShowMsg("[DBP:GL] Using OpenGL extension function %s", buf);
					}
				}
				if (missRequired)
				{
					gl_error:
					retro_notify(0, RETRO_LOG_ERROR, "Error during OpenGL initialization. Please switch the '3dfx Voodoo Performance' video core option to 'Software'. Enable logging for details.");
					OnReset(voodoo_ogl_initfailed, true);
					dbp_hw_render.context_type = RETRO_HW_CONTEXT_DUMMY; // signal failed context
					av_info.timing.fps = -1; // force update of av_info in retro_run
					return;
				}

				//GFX_ShowMsg("[DBP:GL] GL Version: %s", myglGetString(/*MYGL_VERSION*/0x1F02));
				//GFX_ShowMsg("[DBP:GL] GL Extensions: %s", myglGetString(/*MYGL_EXTENSIONS*/0x1F03));

				//float pointsizes[2];
				//myglGetFloatv(/*GL_POINT_SIZE_RANGE*/0x0B12, pointsizes);
				//GFX_ShowMsg("[DBP:GL] GL Point Size Range: %f ~ %f", pointsizes[0], pointsizes[1]);

				static const char* vertex_shader_src =
					"in vec2 a_position;"
					"in vec2 a_texcoord;"
					"out vec2 v_texcoord;"
					"void main()"
					"{"
						"v_texcoord = a_texcoord;"
						"gl_Position = vec4(a_position, 0.0, 1.0);"
					"}";

				static const char* fragment_shader_src =
					"uniform sampler2D u_texture;"
					"in vec2 v_texcoord;"
					"void main()"
					"{"
						"fragColor = texture(u_texture, v_texcoord).bgra;"
					"}";

				static const char* bind_attrs[] = { "a_position", "a_texcoord" };
				if (myglGetError()) { DBP_ASSERT(0); goto gl_error; }
				prog_dosboxbuffer = DBP_Build_GL_Program(1, &vertex_shader_src, 1, &fragment_shader_src, 2, bind_attrs);
				if (myglGetError()) { DBP_ASSERT(0); goto gl_error; }

				myglUseProgram(prog_dosboxbuffer);
				myglUniform1i(myglGetUniformLocation(prog_dosboxbuffer, "u_texture"), 0);

				myglGenBuffers(1, &vbo);
				myglGenVertexArrays(1, &vao);

				myglGenTextures(1, &tex);
				myglBindTexture(MYGL_TEXTURE_2D, tex);
				myglTexParameteri(MYGL_TEXTURE_2D, MYGL_TEXTURE_MIN_FILTER, MYGL_NEAREST);
				myglTexParameteri(MYGL_TEXTURE_2D, MYGL_TEXTURE_MAG_FILTER, MYGL_NEAREST);
				myglTexParameteri(MYGL_TEXTURE_2D, MYGL_TEXTURE_WRAP_S, MYGL_CLAMP_TO_EDGE);
				myglTexParameteri(MYGL_TEXTURE_2D, MYGL_TEXTURE_WRAP_T, MYGL_CLAMP_TO_EDGE);
				myglGenFramebuffers(1, &fbo);
				myglBindFramebuffer(MYGL_FRAMEBUFFER, fbo);
				myglFramebufferTexture2D(MYGL_FRAMEBUFFER, MYGL_COLOR_ATTACHMENT0, MYGL_TEXTURE_2D, tex, 0);
				if (myglGetError()) { DBP_ASSERT(0); goto gl_error; }

				lastw = lasth = 0;
				dbp_opengl_draw = Draw;
				if (dbp_state == DBPSTATE_RUNNING || dbp_state == DBPSTATE_FIRST_FRAME) OnReset(voodoo_ogl_resetcontext, false);
			}

			static void Destroy(void)
			{
				if (!dbp_opengl_draw) return; // RetroArch can call destroy even when context is not inited
				myglDeleteFramebuffers(1, &fbo);
				myglDeleteTextures(1, &tex);
				myglDeleteVertexArrays(1, &vao);
				myglDeleteBuffers(1, &vbo);
				myglDeleteProgram(prog_dosboxbuffer);
				OnReset(voodoo_ogl_cleanup, true);
			}

			static void OnReset(void (*func_voodoo_ogl)(), bool context_destroyed)
			{
				const bool pauseThread = (dbp_state != DBPSTATE_BOOT && dbp_state != DBPSTATE_SHUTDOWN);
				if (pauseThread) DBP_ThreadControl(TCM_PAUSE_FRAME);
				func_voodoo_ogl();
				if (pauseThread) DBP_ThreadControl(TCM_RESUME_FRAME);
				if (context_destroyed) { prog_dosboxbuffer = vbo = vao = tex = fbo = lastw = lasth = 0; dbp_opengl_draw = NULL; }
			}

			static void Draw(const DBP_Buffer& buf)
			{
				myglGetError(); // clear any frontend error state

				Bit32u view_width = buf.width, view_height = buf.height;
				if (lastw != view_width || lasth != view_height)
				{
					lastw = view_width;
					lasth = view_height;
					const float vertices[] = { // TODO: Flip view[1]/view[2] in voodoo_ogl_mainthread and use same vertices for both myglDrawArrays calls
						-1.0f, -1.0f,   0.0f,1.0f, // bottom left
						 1.0f, -1.0f,   1.0f,1.0f, // bottom right
						-1.0f,  1.0f,   0.0f,0.0f, // top left
						 1.0f,  1.0f,   1.0f,0.0f, // top right
						-1.0f,  1.0f,   0.0f,1.0f, // bottom left
						 1.0f,  1.0f,   1.0f,1.0f, // bottom right
						-1.0f, -1.0f,   0.0f,0.0f, // top left
						 1.0f, -1.0f,   1.0f,0.0f, // top right
					};
					myglBindVertexArray(vao);
					myglBindBuffer(MYGL_ARRAY_BUFFER, vbo);
					myglBufferData(MYGL_ARRAY_BUFFER, sizeof(vertices), vertices, MYGL_STATIC_DRAW);
					myglEnableVertexAttribArray(0);
					myglEnableVertexAttribArray(1);
					myglVertexAttribPointer(0, 2, MYGL_FLOAT, MYGL_FALSE, 4 * sizeof(float), (void*)0);
					myglVertexAttribPointer(1, 2, MYGL_FLOAT, MYGL_FALSE, 4 * sizeof(float), (void*)(sizeof(float)*2));
					myglBindTexture(MYGL_TEXTURE_2D, tex);
					myglTexImage2D(MYGL_TEXTURE_2D, 0, MYGL_RGBA, view_width, view_height, 0, MYGL_RGBA, MYGL_UNSIGNED_BYTE, NULL);
				}
				if (myglGetError()) { DBP_ASSERT(0); } // clear any error state

				bool is_voodoo_display = voodoo_ogl_display();
				if (is_voodoo_display) { view_width *= voodoo_ogl_scale; view_height *= voodoo_ogl_scale; }

				myglBindFramebuffer(MYGL_FRAMEBUFFER, (unsigned)dbp_hw_render.get_current_framebuffer());
				myglViewport(0, 0, view_width, view_height);
				myglBindVertexArray(vao);
				if (is_voodoo_display)
					myglDrawArrays(MYGL_TRIANGLE_STRIP, 4, 4);

				#ifndef DBP_STANDALONE
				if (!is_voodoo_display || dbp_intercept_next)
				#else
				if (!is_voodoo_display)
				#endif
				{
					myglBindTexture(MYGL_TEXTURE_2D, tex);
					myglTexSubImage2D(MYGL_TEXTURE_2D, 0, 0, 0, buf.width, buf.height, MYGL_RGBA, MYGL_UNSIGNED_BYTE, buf.video);
					if (is_voodoo_display)
					{
						myglEnable(MYGL_BLEND);
						myglBlendFuncSeparate(MYGL_SRC_ALPHA, MYGL_ONE_MINUS_SRC_ALPHA, MYGL_SRC_ALPHA, MYGL_ONE_MINUS_SRC_ALPHA);
					}
					else myglDisable(MYGL_BLEND);
					myglUseProgram(prog_dosboxbuffer);
					myglActiveTexture(MYGL_TEXTURE0);
					myglDrawArrays(MYGL_TRIANGLE_STRIP, 0, 4);
					if (is_voodoo_display) myglDisable(MYGL_BLEND);
				}

				// Unbind buffers and arrays before leaving
				myglBindBuffer(MYGL_ARRAY_BUFFER, 0);
				myglBindVertexArray(0);
				myglBindFramebuffer(MYGL_FRAMEBUFFER, 0);
				if (myglGetError()) { DBP_ASSERT(0); } // clear any error state

				video_cb(RETRO_HW_FRAME_BUFFER_VALID, view_width, view_height, 0);
			}
		};

		for (int test = -1, testmax = (voodoo_perf[0] == 'a' ? 0 : 5); test != testmax; test++)
		{
			if (test < 0)
			{
				unsigned preffered_hw_render = RETRO_HW_CONTEXT_NONE;
				if (!environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preffered_hw_render)) continue;
				if (preffered_hw_render == RETRO_HW_CONTEXT_NONE || preffered_hw_render >= RETRO_HW_CONTEXT_VULKAN) continue;
				dbp_hw_render.context_type = (enum retro_hw_context_type)preffered_hw_render;
				// RetroArch on Android will return RETRO_HW_CONTEXT_OPENGL even when only accepting a OPENGLES context.
				// So we still try all the other tests in that case even when on auto.
				if (preffered_hw_render == RETRO_HW_CONTEXT_OPENGL) testmax = 4;
			}
			else dbp_hw_render.context_type = (enum retro_hw_context_type)testhwcontexts[test];
			dbp_hw_render.version_major = (dbp_hw_render.context_type >= RETRO_HW_CONTEXT_OPENGL_CORE ? 3 : 0);
			dbp_hw_render.version_minor = (dbp_hw_render.context_type >= RETRO_HW_CONTEXT_OPENGL_CORE ? 1 : 0);
			dbp_hw_render.context_reset = HWContext::Reset;
			dbp_hw_render.context_destroy = HWContext::Destroy;
			dbp_hw_render.depth = false;
			dbp_hw_render.stencil = false;
			dbp_hw_render.bottom_left_origin = true;

			if (environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &dbp_hw_render))
			{
				const char* names[] = { "NONE", "OpenGL 2.x", "OpenGL ES 2.0", "OpenGL 3/4", "Open GL ES 3.0", "Open GL ES 3.1+" };
				GFX_ShowMsg("[DBP:GL] Selected HW Renderer: %s : %d.%d", names[dbp_hw_render.context_type], dbp_hw_render.version_major, dbp_hw_render.version_minor);
				break;
			}
			dbp_hw_render.context_type = RETRO_HW_CONTEXT_NONE;
		}
		if (dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE) voodoo_ogl_initfailed(); // enforce software rendering
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
	init_dosbox();

	bool support_achievements = true;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &support_achievements);

	DBP_PadMapping::SetInputDescriptors(true);

	return true;
}

void retro_get_system_av_info(struct retro_system_av_info *info) // #5
{
	DBP_ASSERT(dbp_state != DBPSTATE_BOOT);
	DBP_ThreadControl(TCM_FINISH_FRAME);
	if (dbp_biosreboot || dbp_state == DBPSTATE_EXITED)
	{
		// A reboot can happen during the first frame if puremenu wants to change DOSBox machine config or if autoexec via dosbox.conf ran 'exit'
		DBP_ASSERT(dbp_state == DBPSTATE_EXITED && (dbp_biosreboot || dbp_crash_message.size() || (control && !dbp_game_running)));
		DBP_ForceReset();
		DBP_ThreadControl(TCM_FINISH_FRAME);
		DBP_ASSERT((!dbp_biosreboot && dbp_state == DBPSTATE_FIRST_FRAME) || dbp_crash_message.size() || (control && !dbp_game_running));
	}
	DBP_ASSERT(render.src.fps > 10.0); // validate initialized video mode after first frame
	const DBP_Buffer& buf = dbp_buffers[buffer_active];
	const Bit32u vscale = (Bit32u)atoi(DBP_Option::Get(DBP_Option::voodoo_scale)), vscale2 = (vscale < 16 ? vscale : 1), vw = 640 * vscale2, vh = 480 * vscale2;
	av_info.geometry.max_width = (buf.width > 1024 ? buf.width : 1024);
	av_info.geometry.max_height = (buf.height > 1024 ? buf.height : 1024);
	if (vw > av_info.geometry.max_width) av_info.geometry.max_width = vw;
	if (vh > av_info.geometry.max_height) av_info.geometry.max_height = vh;
	av_info.geometry.base_width = buf.width;
	av_info.geometry.base_height = buf.height;
	av_info.geometry.aspect_ratio = buf.ratio;
	av_info.timing.fps = DBP_GetFPS();
	av_info.timing.sample_rate = DBP_MIXER_GetFrequency();
	if (dbp_perf == DBP_PERF_DETAILED)
		retro_notify(0, RETRO_LOG_INFO, "Startup Resolution: %d x %d @ %4.2f hz", (int)render.src.width, (int)render.src.height, render.src.fps);
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
	init_dosbox(input_state_cb && (
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
			{ down_tick = tick; is_tap = true; down_btn = presses; DBP_QueueEvent(DBPET_MOUSEDOWN, DBP_NO_PORT, down_btn); press_tick = 0; }
		else if (down_tick && (!presses || add_press))
			{ DBP_QueueEvent(DBPET_MOUSEUP, DBP_NO_PORT, down_btn); down_tick = 0; }
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
			DBP_QueueEvent(DBPET_MOUSEMOVE, DBP_NO_PORT, dx / 32, dy / 32);
			is_move = true;
		}
	}
	if (!down_tick && presses && !is_move && press_tick && (tick - press_tick) >= 500)
		{ down_tick = tick; is_tap = false; down_btn = presses - 1; DBP_QueueEvent(DBPET_MOUSEDOWN, DBP_NO_PORT, down_btn); }
	else if (down_tick && is_tap && (tick - down_tick) >= 100)
		{ DBP_QueueEvent(DBPET_MOUSEUP, DBP_NO_PORT, down_btn); down_tick = 0; }
}

void retro_run(void)
{
	#ifdef DBP_ENABLE_FPS_COUNTERS
	DBP_FPSCOUNT(dbp_fpscount_retro)
	uint32_t curTick = DBP_GetTicks();
	if (curTick - dbp_lastfpstick >= 1000 && !dbp_perf)
	{
		double fpsf = 1000.0 / (double)(curTick - dbp_lastfpstick), gfxf = fpsf * (render.frameskip.max < 1 ? 1 : render.frameskip.max);
		log_cb(RETRO_LOG_INFO, "[DBP FPS] RETRO: %3.2f - GFXSTART: %3.2f - GFXEND: %3.2f - EVENT: %5.1f - EMULATED: %3.2f - CyclesMax: %d\n",
			dbp_fpscount_retro * fpsf, dbp_fpscount_gfxstart * gfxf, dbp_fpscount_gfxend * gfxf, dbp_fpscount_event * fpsf, render.src.fps, CPU_CycleMax);
		dbp_lastfpstick = (curTick - dbp_lastfpstick >= 1500 ? curTick : dbp_lastfpstick + 1000);
		dbp_fpscount_retro = dbp_fpscount_gfxstart = dbp_fpscount_gfxend = dbp_fpscount_event = dbp_fpscount_skip_run = dbp_fpscount_skip_render = 0;
	}
	#endif

	if (dbp_message_queue) run_emuthread_notify();

	if (!environ_cb(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &dbp_throttle))
	{
		bool fast_forward = false;
		if (environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast_forward) && fast_forward)
			dbp_throttle = { RETRO_THROTTLE_FAST_FORWARD, 0.0f };
		else
			dbp_throttle = { RETRO_THROTTLE_NONE, (float)av_info.timing.fps };
	}

	static Bit8u fpsboost_count = 0, last_fpsboost = 1;
	Bit8u next_fpsboost = 1, fpsboost = 1;
	extern bool dbp_net_connected;
	if (dbp_net_connected)
	{
		// Increase the rate retro_run is called during multiplayer. This can significantly improve network performance for games running in lock-step by giving the frontend more chances to send and receive packets.
		fpsboost = ((dbp_throttle.mode == RETRO_THROTTLE_NONE && dbp_throttle.rate > (av_info.timing.fps * 1.5)) ? (int)(dbp_throttle.rate / av_info.timing.fps + .499) : 1);
		dbp_throttle.rate /= fpsboost;
		next_fpsboost = 4;
	}

	static retro_throttle_state throttle_last;
	if (dbp_throttle.mode != throttle_last.mode || dbp_throttle.rate != throttle_last.rate)
	{
		static const char* throttle_modes[] = { "NONE", "FRAME_STEPPING", "FAST_FORWARD", "SLOW_MOTION", "REWINDING", "VSYNC", "UNBLOCKED" };
		log_cb(RETRO_LOG_INFO, "[DBP THROTTLE] %s %f -> %s %f\n", throttle_modes[throttle_last.mode], throttle_last.rate, throttle_modes[dbp_throttle.mode], dbp_throttle.rate);
		throttle_last = dbp_throttle;
	}

	bool variable_update = false;
	if (!dbp_optionsupdatecallback && control && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &variable_update) && variable_update)
		check_variables(); // can't do this while DOS has crashed (control is NULL)

	// start input update
	input_poll_cb();
	//input_state_cb(0, RETRO_DEVICE_NONE, 0, 0); // poll keys? 
	//input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_SPACE); // get latest keyboard callbacks?

	// query mouse movement before querying mouse buttons
	if (dbp_mouse_input != 'f')
	{
		int16_t movx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
		int16_t movy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
		int16_t absx = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
		int16_t absy = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
		int16_t prss = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
		//int16_t lgx = input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
		//int16_t lgy = input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
		//int16_t screenmousex = input_state_cb(0, RETRO_DEVICE_MOUSE | 0x10000, 0, RETRO_DEVICE_ID_MOUSE_X);
		//int16_t screenmousey = input_state_cb(0, RETRO_DEVICE_MOUSE | 0x10000, 0, RETRO_DEVICE_ID_MOUSE_Y);
		//int16_t screenpointerx = input_state_cb(0, RETRO_DEVICE_POINTER | 0x10000, 0, RETRO_DEVICE_ID_POINTER_X);
		//int16_t screenpointery = input_state_cb(0, RETRO_DEVICE_POINTER | 0x10000, 0, RETRO_DEVICE_ID_POINTER_Y);
		//log_cb(RETRO_LOG_INFO, "[DOSBOX MOUSE] [%4d@%6d] Rel: %d,%d - Abs: %d,%d - LG: %d,%d - Press: %d - Count: %d - ScreenMouse: %d,%d - ScreenPointer: %d,%d\n", dbp_framecount, DBP_GetTicks(), movx, movy, absx, absy, lgx, lgy, prss, input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_COUNT), screenmousex,screenmousey,screenpointerx,screenpointery);
		bool absvalid = (absx || absy || prss);
		if (dbp_mouse_input == 'p')
			retro_run_touchpad(!!prss, absx, absy);
		else if (movx || movy || (absvalid && (absx != dbp_mouse_x || absy != dbp_mouse_y)))
		{
			//log_cb(RETRO_LOG_INFO, "[DOSBOX MOUSE] [%4d@%6d] Rel: %d,%d - Abs: %d,%d - LG: %d,%d - Press: %d - Count: %d\n", dbp_framecount, DBP_GetTicks(), movx, movy, absx, absy, lgx, lgy, prss, input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_COUNT));
			if (absvalid) dbp_mouse_x = absx, dbp_mouse_y = absy;
			DBP_QueueEvent(DBPET_MOUSEMOVE, DBP_NO_PORT, movx, movy);
		}
	}

	recheck_intercept:
	if (!dbp_intercept)
	{
		// query buttons mapped to analog functions
		if (dbp_analog_buttons)
		{
			for (DBP_InputBind& b : dbp_input_binds)
			{
				if (b.evt > _DBPET_JOY_AXIS_MAX || b.device != RETRO_DEVICE_JOYPAD) continue; // handled below
				Bit16s val = input_state_cb(b.port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, b.id);
				if (!val) val = (input_state_cb(b.port, RETRO_DEVICE_JOYPAD, 0, b.id) ? (Bit16s)32767 : (Bit16s)0); // old frontend fallback
				if (val != b.lastval) b.Update(val, true);
			}
		}

		// query input states and generate input events
		for (DBP_InputBind& b : dbp_input_binds)
		{
			Bit16s val = input_state_cb(b.port, b.device, b.index, b.id);
			if (val != b.lastval) b.Update(val);
		}
	}
	else if (dbp_intercept == dbp_intercept_next)
		dbp_intercept->input();
	else
		{ dbp_intercept->close(); dbp_intercept = dbp_intercept_next; goto recheck_intercept; }

	// This catches sticky keys due to various frontend/driver issues
	// For example ALT key can easily get stuck when using ALT-TAB, menu opening or fast forwarding also can get stuck
	if (dbp_keys_down_count)
		DBP_ReleaseKeyEvents(true);

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
				#ifdef DBP_STANDALONE
				environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
				#else
				// On statically linked platforms shutdown would exit the frontend, so don't do that. Just tint the screen red and sleep
				#ifndef STATIC_LINKING
				if (dbp_menu_time >= 0 && dbp_menu_time < 99) // only auto shut down for users that want auto shut down in general
				{
					environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
				}
				else
				#endif
				{
					for (Bit8u *p = (Bit8u*)buf.video, *pEnd = p + buf.width * buf.height * 4; p < pEnd; p += 56) p[2] = 255;
					retro_sleep(10);
				}
				#endif
			}
			else if (dbp_state == DBPSTATE_SHUTDOWN)
			{
				if (DBP_OSD.ptr._all == NULL) DBP_StartOSD(_DBPOSD_OPEN);
				while (dbp_intercept_next && dbp_event_queue_read_cursor != dbp_event_queue_write_cursor)
				{
					DBP_Event e = dbp_event_queue[dbp_event_queue_read_cursor];
					dbp_event_queue_read_cursor = ((dbp_event_queue_read_cursor + 1) % DBP_EVENT_QUEUE_SIZE);
					dbp_intercept_next->evnt(e.type, e.val, e.val2);
				}
				if (dbp_intercept_next)
				{
					#ifndef DBP_STANDALONE
					dbp_intercept_next->gfx(buf);
					#else
					DBP_Buffer& osdbf = dbp_osdbuf[(buffer_active + 1) % 3];
					if (!osdbf.video) osdbf.video = (Bit32u*)malloc(DBPS_OSD_WIDTH*DBPS_OSD_HEIGHT*4);
					memset(osdbf.video, 0, DBPS_OSD_WIDTH*DBPS_OSD_HEIGHT*4);
					dbp_intercept_next->gfx(osdbf);
					DBPS_SubmitOSDFrame(osdbf.video, osdbf.width, osdbf.height);
					#endif
				}
			}

			// submit last frame
			#ifndef DBP_STANDALONE
			Bit32u numEmptySamples = (Bit32u)(av_info.timing.sample_rate / av_info.timing.fps);
			DBP_Audio& aud = dbp_audio[dbp_audio_active ^= 1];
			if (numEmptySamples > aud.length) { aud.audio = (int16_t*)realloc(aud.audio, numEmptySamples * 4); aud.length = numEmptySamples; }
			memset(aud.audio, 0, numEmptySamples * 4);
			audio_batch_cb(aud.audio, numEmptySamples);
			#endif
			if (dbp_opengl_draw)
				dbp_opengl_draw(buf);
			else
				video_cb(buf.video, buf.width, buf.height, buf.width * 4);
			return;
		}

		DBP_ASSERT(dbp_state == DBPSTATE_FIRST_FRAME);
		DBP_ThreadControl(TCM_FINISH_FRAME);
		DBP_ASSERT(dbp_state == DBPSTATE_FIRST_FRAME || (dbp_state == DBPSTATE_EXITED && (dbp_biosreboot || dbp_crash_message.size())));
		const char* midiarg, *midierr = DBP_MIDI_StartupError(control->GetSection("midi"), midiarg);
		if (midierr) retro_notify(0, RETRO_LOG_ERROR, midierr, midiarg);
		if (dbp_state == DBPSTATE_FIRST_FRAME) dbp_state = DBPSTATE_RUNNING;
		if (dbp_skip_c_mount)
		{
			init_dosbox_parse_drives(); // parse things mounted by autoexec of .conf
			if (dbp_auto_mapping || dbp_custom_mapping.size()) DBP_PadMapping::SetInputDescriptors(true); // refresh if now loaded
		}
	}

	bool skip_emulate = (fpsboost > 1 && (((fpsboost_count++)%fpsboost)!=0)) || DBP_NeedFrameSkip(false);
	DBP_ThreadControl(skip_emulate ? TCM_PAUSE_FRAME : TCM_FINISH_FRAME);

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
		dbp_perf_uniquedraw = dbp_perf_count = dbp_perf_totaltime = 0;
	}

	#ifndef DBP_STANDALONE
	// mix audio
	Bit32u haveSamples = DBP_MIXER_DoneSamplesCount(), mixSamples = 0; double numSamples;
	if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD && dbp_throttle.rate < 1)
		numSamples = haveSamples;
	else if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD || dbp_throttle.mode == RETRO_THROTTLE_SLOW_MOTION || dbp_throttle.rate < 1)
		numSamples = (av_info.timing.sample_rate / av_info.timing.fps) + dbp_audio_remain;
	else
		numSamples = (av_info.timing.sample_rate / dbp_throttle.rate) + dbp_audio_remain;
	if (fpsboost > 1) numSamples /= (fpsboost*.9); // Without *.9 audio can end up skipping
	if (numSamples && haveSamples && dbp_audio_remain != -1) // stretch on underrun (allows frontend to catch up with the emulation)
	{
		mixSamples = (numSamples > haveSamples ? haveSamples : (Bit32u)numSamples);
		dbp_audio_remain = ((numSamples <= mixSamples || numSamples > haveSamples) ? 0.0 : (numSamples - mixSamples));
		DBP_Audio& aud = dbp_audio[dbp_audio_active ^= 1];
		if (mixSamples > aud.length) { aud.audio = (int16_t*)realloc(aud.audio, mixSamples * 4); aud.length = mixSamples; }
		MIXER_CallBack(0, (Bit8u*)aud.audio, mixSamples * 4);
	}
	#endif

	// Read buffer_active before waking up emulation thread
	const DBP_Buffer& buf = dbp_buffers[buffer_active];
	Bit32u view_width = buf.width, view_height = buf.height;

	if (dbp_opengl_draw && voodoo_ogl_mainthread()) { view_width *= voodoo_ogl_scale; view_height *= voodoo_ogl_scale; }

	DBP_ThreadControl(skip_emulate ? TCM_RESUME_FRAME : TCM_NEXT_FRAME);

	#ifndef DBP_STANDALONE
	// submit audio
	//log_cb(RETRO_LOG_INFO, "[retro_run] Submit %d samples (remain %f) - Had: %d - Left: %d\n", mixSamples, dbp_audio_remain, haveSamples, DBP_MIXER_DoneSamplesCount());
	if (mixSamples)
		audio_batch_cb(dbp_audio[dbp_audio_active].audio, mixSamples);
	#endif

	if (tpfActual)
	{
		extern const char* DBP_CPU_GetDecoderName();
		if (dbp_perf == DBP_PERF_DETAILED)
			retro_notify(-1500, RETRO_LOG_INFO, "Speed: %4.1f%%, DOS: %dx%d@%4.2fhz, Actual: %4.2ffps, Drawn: %dfps, Cycles: %u (%s)"
				#ifdef DBP_ENABLE_WAITSTATS
				", Waits: p%u|f%u|z%u|c%u"
				#endif
				#ifdef DBP_ENABLE_FPS_COUNTERS
				"\nRetro: %u, GfxStart: %u, GfxEnd: %u, Event: %u, SkipRun: %u, SkipRender: %u"
				#endif
				, ((float)tpfTarget / (float)tpfActual * 100), (int)render.src.width, (int)render.src.height, render.src.fps, (1000000.f / tpfActual), tpfDraws, CPU_CycleMax, DBP_CPU_GetDecoderName()
				#ifdef DBP_ENABLE_WAITSTATS
				, waitPause, waitFinish, waitPaused, waitContinue
				#endif
				#ifdef DBP_ENABLE_FPS_COUNTERS
				, dbp_fpscount_retro, dbp_fpscount_gfxstart, dbp_fpscount_gfxend, dbp_fpscount_event, dbp_fpscount_skip_run, dbp_fpscount_skip_render
				#endif
				);
		else
			retro_notify(-1500, RETRO_LOG_INFO, "Emulation Speed: %4.1f%%",
				((float)tpfTarget / (float)tpfActual * 100));
		#ifdef DBP_ENABLE_FPS_COUNTERS
		dbp_fpscount_retro = dbp_fpscount_gfxstart = dbp_fpscount_gfxend = dbp_fpscount_event = dbp_fpscount_skip_run = dbp_fpscount_skip_render = 0;
		#endif
	}

	// handle video mode changes
	double targetfps = DBP_GetFPS();
	if (av_info.geometry.base_width != view_width || av_info.geometry.base_height != view_height || av_info.geometry.aspect_ratio != buf.ratio || av_info.timing.fps != targetfps || next_fpsboost != last_fpsboost)
	{
		log_cb(RETRO_LOG_INFO, "[DOSBOX] Resolution changed %ux%u @ %.3fHz AR: %.5f => %ux%u @ %.3fHz AR: %.5f\n",
			av_info.geometry.base_width, av_info.geometry.base_height, av_info.timing.fps, av_info.geometry.aspect_ratio,
			view_width, view_height, av_info.timing.fps, buf.ratio);
		bool newfps = (av_info.timing.fps != targetfps || next_fpsboost != last_fpsboost), newmax = (av_info.geometry.max_width < view_width || av_info.geometry.max_height < view_height);
		if (av_info.geometry.max_width < view_width)   av_info.geometry.max_width = view_width;
		if (av_info.geometry.max_height < view_height) av_info.geometry.max_height = view_height;
		av_info.geometry.base_width = view_width;
		av_info.geometry.base_height = view_height;
		av_info.geometry.aspect_ratio = buf.ratio;
		av_info.timing.fps = targetfps * next_fpsboost;
		last_fpsboost = next_fpsboost;
		if (dbp_hw_render.context_type == RETRO_HW_CONTEXT_DUMMY)
		{
			// To force RetroArch to abandon the hardware context we need to do 2 things, clear hw render, then reinitialize the video driver by changing max size
			memset(&dbp_hw_render, 0, sizeof(dbp_hw_render));
			environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &dbp_hw_render);
			av_info.geometry.max_width++;
			environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
			av_info.geometry.max_width--;
		}
		environ_cb(((newfps || newmax) ? RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO : RETRO_ENVIRONMENT_SET_GEOMETRY), &av_info);
		av_info.timing.fps = targetfps;
	}

	// submit video
	if (skip_emulate)
		video_cb(NULL, view_width, view_height, view_width * 4);
	else if (dbp_opengl_draw)
		dbp_opengl_draw(buf);
	else
		video_cb(buf.video, view_width, view_height, view_width * 4);

	#ifdef DBP_STANDALONE
	if (dbp_intercept && dbp_osdbuf[&buf - dbp_buffers].video)
	{
		DBP_Buffer& osdbf = dbp_osdbuf[&buf - dbp_buffers];
		DBPS_SubmitOSDFrame(osdbf.video, osdbf.width, osdbf.height);
	}
	#endif
}

static bool retro_serialize_all(DBPArchive& ar, bool unlock_thread)
{
	if (dbp_serializemode == DBPSERIALIZE_DISABLED) return false;
	bool pauseThread = (dbp_state != DBPSTATE_BOOT && dbp_state != DBPSTATE_SHUTDOWN);
	if (pauseThread) DBP_ThreadControl(TCM_PAUSE_FRAME);
	DBPSerialize_All(ar, (dbp_state == DBPSTATE_RUNNING || dbp_state == DBPSTATE_FIRST_FRAME), dbp_game_running);
	//log_cb(RETRO_LOG_WARN, "[SERIALIZE] [%d] [%s] %u\n", ((dbp_state == DBPSTATE_RUNNING || dbp_state == DBPSTATE_FIRST_FRAME) && dbp_game_running), (ar.mode == DBPArchive::MODE_LOAD ? "LOAD" : ar.mode == DBPArchive::MODE_SAVE ? "SAVE" : ar.mode == DBPArchive::MODE_SIZE ? "SIZE" : ar.mode == DBPArchive::MODE_MAXSIZE ? "MAXX" : ar.mode == DBPArchive::MODE_ZERO ? "ZERO" : "???????"), (Bit32u)ar.GetOffset());
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
			case DBPArchive::ERR_GAMENOTRUNNING:
				if (ar.mode == DBPArchive::MODE_LOAD)
					retro_notify(0, RETRO_LOG_WARN, "Unable to load a save state while game the isn't running, start it first.");
				else if (dbp_serializemode != DBPSERIALIZE_REWIND)
					retro_notify(0, RETRO_LOG_ERROR, "%sUnable to %s while %s %s not running."
						#ifndef DBP_STANDALONE
						"\nIf using rewind, make sure to modify the related core option."
						#endif
						"", (ar.mode == DBPArchive::MODE_LOAD ? "Load State Error: " : "Save State Error: "),
						(ar.mode == DBPArchive::MODE_LOAD ? "load state made" : "save state"),
						(ar.had_error == DBPArchive::ERR_DOSNOTRUNNING ? "DOS" : "game"),
						(ar.mode == DBPArchive::MODE_LOAD ? "was" : "is"));
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
	if (dbp_serializesize) return dbp_serializesize;
	DBPArchiveCounter ar((dbp_state != DBPSTATE_RUNNING && dbp_state != DBPSTATE_FIRST_FRAME) || dbp_serializemode == DBPSERIALIZE_REWIND);
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
	if ((dbp_state != DBPSTATE_RUNNING && dbp_state != DBPSTATE_FIRST_FRAME) || dbp_game_running) retro_reset();
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
#include <sys/stat.h>

#ifdef WIN32
wchar_t* AllocUTF8ToUTF16(const char *str)
{
	if (!str || !*str) return NULL;
	wchar_t* res;
	if (int len8 = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0))
	{
		if (!(res = (wchar_t*)malloc(len8 * sizeof(wchar_t)))) return NULL;
		if ((MultiByteToWideChar(CP_UTF8, 0, str, -1, res, len8)) < 0) { free(res); return NULL; }
	}
	else if (int lena = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0)) // Fall back to ANSI codepage instead
	{
		if (!(res = (wchar_t*)malloc(lena * sizeof(wchar_t)))) return NULL;
		if ((MultiByteToWideChar(CP_ACP, 0, str, -1, res, lena)) < 0) { free(res); return NULL; }
	}
	return res;
}
#ifndef S_ISDIR
#define S_ISDIR(m) (((m)&S_IFMT)==S_IFDIR)
#endif
#endif

FILE* fopen_wrap(const char* path, const char* mode)
{
	#ifdef WIN32
	for (const char* p = path; *p; p++) { if ((Bit8u)*p > 0x7F) goto needw; }
	#endif
	return fopen(path, mode);
	#ifdef WIN32
	needw:
	wchar_t *wpath = AllocUTF8ToUTF16(path), wmode[20], *pwmode = wmode;
	if (!wpath) return NULL;
	for (const char* p = mode, *pEnd = p + 19; *p && p != pEnd; p++) *(pwmode++) = *p;
	*pwmode = '\0';
	FILE* f = _wfopen(wpath, wmode);
	free(wpath);
	return f;
	#endif
}

static bool exists_utf8(const char* path, bool* out_is_dir)
{
	#ifdef WIN32
	for (const char* p = path; *p; p++) { if ((Bit8u)*p > 0x7F) goto needw; }
	#endif
	struct stat test;
	if (stat(path, &test)) return false;
	if (out_is_dir) *out_is_dir = !!S_ISDIR(test.st_mode);
	return true;
	#ifdef WIN32
	needw:
	wchar_t *wpath = AllocUTF8ToUTF16(path);
	if (!wpath) return NULL;
	struct _stat64i32 wtest;
	bool retval = !_wstat64i32(wpath, &wtest);
	free(wpath);
	if (out_is_dir && retval) *out_is_dir = !!S_ISDIR(wtest.st_mode);
	return true;
	#endif
}

#if !defined(HAVE_LIBNX) // platforms that don't support inlining this compile this as a separate object
#include "libretro-common/features/features_cpu.inl"
#endif

bool fpath_nocase(std::string& pathstr, bool* out_is_dir)
{
	if (!*pathstr.c_str()) return false; // c_str guarantees \0 terminator afterwards
	char* path = &pathstr[0];

	#ifdef WIN32
	// Directories on Windows, for stat (used by exists_utf8) we need to remove trailing slashes except the one after :
	for (char clast; ((clast = pathstr.back()) == '\\' || clast == '/') && pathstr.length() > (path[1] == ':' ? 3u : 1u); path = (char*)pathstr.c_str()) pathstr.pop_back(); 
	// Paths that start with / or \ need to be prefixed with the drive letter from the content path
	if ((path[0] == '/' || path[0] == '\\') && path[1] != '\\' && dbp_content_path.length() > 1 && dbp_content_path[1] == ':') { pathstr.insert(0, &dbp_content_path[0], 2); path = (char*)pathstr.c_str(); }
	// For absolute paths we can just return here because paths are not case sensitive on Windows
	if ((path[1] == ':' && (path[2] == '/' || path[2] == '\\')) || (path[0] == '\\' && path[1] == '\\')) return exists_utf8(path, out_is_dir);
	#else
	const bool is_absolute = (path[0] == '/' || path[0] == '\\');
	if (is_absolute && exists_utf8(path, out_is_dir)) return true; // exists as is with an absolute path
	size_t contentdirlen = 1; // 1 to use root on absolute path
	if (!is_absolute)
	{
		#endif
		// Prefix with directory of content path
		const char *content = dbp_content_path.c_str(), *content_fs = strrchr(content, '/'), *content_bs = strrchr(content, '\\');
		const char* content_dir_end = ((content_fs || content_bs) ? (content_fs > content_bs ? content_fs : content_bs) + 1 : content + dbp_content_path.length());
		if (content_dir_end != content)
		{
			pathstr.insert(0, content, (content_dir_end - content));
			if (!content_fs && !content_bs) pathstr.insert(((content_dir_end++) - content), 1, CROSS_FILESPLIT);
		}
		if (exists_utf8(pathstr.c_str(), out_is_dir)) return true; // exists relative to content as is
		#ifdef WIN32
		return false; // does not exist, even case insensitive
		#else
		contentdirlen = (content_dir_end != content ? content_dir_end - 1 - content : 0);
		path = &pathstr[0];
	}
	if (strchr(path, '\\'))
	{
		strreplace(path, '\\', '/');
		if (exists_utf8(path, out_is_dir)) return true; // exists with native slashes
	}

	struct retro_vfs_interface_info vfs = { 3, NULL };
	if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) || vfs.required_interface_version < 3 || !vfs.iface)
		return false;

	std::string subdir;
	if (contentdirlen) { subdir.assign(path, contentdirlen); path += contentdirlen; }

	bool res = false, wantDir = false;
	while (pathstr.back() == '/') { wantDir = true; pathstr.pop_back(); path = (char*)pathstr.c_str() + contentdirlen; }
	const char* base_dir = (char*)subdir.c_str();
	for (char* psubdir;; *psubdir = CROSS_FILESPLIT, path = psubdir + 1)
	{
		if ((psubdir = strchr(path, '/')) != NULL)
		{
			if (psubdir == path || psubdir[1] == '/') continue;
			*psubdir = '\0';
		}

		// On Android opendir fails for directories the user/app doesn't have access so just assume it exists as is
		if (struct retro_vfs_dir_handle *dir = (base_dir ? vfs.iface->opendir(base_dir, true) : NULL))
		{
			while (vfs.iface->readdir(dir))
			{
				const char* entry_name = vfs.iface->dirent_get_name(dir);
				if (strcasecmp(entry_name, path)) continue;
				strcpy(path, entry_name);
				if (psubdir) break; // not yet done
				bool is_dir = (wantDir || out_is_dir) && vfs.iface->dirent_is_dir(dir);
				res = (!wantDir || is_dir);
				if (out_is_dir) *out_is_dir = is_dir;
				break;
			}
			vfs.iface->closedir(dir);
		}
		if (!psubdir) return res;
		if (subdir.empty() && base_dir) subdir = base_dir;
		if (!subdir.empty() && subdir.back() != '/') subdir += '/';
		base_dir = subdir.append(path).c_str();
	}
	#endif
}

MYGL_FOR_EACH_PROC(MYGL_MAKEFUNCPTR)

static unsigned CreateShaderOfType(int type, int count, const char** shader_src)
{
	unsigned shdr = myglCreateShader(type);
	myglShaderSource(shdr, count, shader_src, NULL);
	myglCompileShader(shdr);
	int compiled = 0;
	myglGetShaderiv(shdr, MYGL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		GFX_ShowMsg("[DBP:GL] %s_shader_src:", (type == MYGL_VERTEX_SHADER ? "vertex" : "fragment"));
		GFX_ShowMsg("------------------------------------------");
		for (int i = 0; i < count; i++) GFX_ShowMsg("%s", shader_src[i]);
		GFX_ShowMsg("------------------------------------------");
		GFX_ShowMsg("[DBP:GL] compiled: %d", compiled);
		int info_len = 0;
		myglGetShaderiv(shdr, MYGL_INFO_LOG_LENGTH, &info_len);
		GFX_ShowMsg("[DBP:GL] info_len: %d", info_len);
		if (info_len > 1)
		{
			char* info_log = (char*)malloc(sizeof(char) * info_len);
			myglGetShaderInfoLog(shdr, info_len, NULL, info_log);
			GFX_ShowMsg("[DBP:GL] Error compiling shader: %s", info_log);
			free(info_log);
		}
		DBP_ASSERT(0);
		myglDeleteShader(shdr);
		shdr = 0;
	}
	return shdr;
}

unsigned DBP_Build_GL_Program(int vertex_shader_srcs_count, const char** vertex_shader_srcs, int fragment_shader_srcs_count, const char** fragment_shader_srcs, int bind_attribs_count, const char** bind_attribs)
{
	const char *tmpvsrcs[2], *tmpfsrcs[2];
	if (vertex_shader_srcs_count   == 1) { tmpvsrcs[0] = NULL; tmpvsrcs[1] = *vertex_shader_srcs;   vertex_shader_srcs_count   = 2; vertex_shader_srcs   = tmpvsrcs; }
	if (fragment_shader_srcs_count == 1) { tmpfsrcs[0] = NULL; tmpfsrcs[1] = *fragment_shader_srcs; fragment_shader_srcs_count = 2; fragment_shader_srcs = tmpfsrcs; }
	DBP_ASSERT(vertex_shader_srcs[0] == NULL && fragment_shader_srcs[0] == NULL); // need slot for header

	if (dbp_hw_render.context_type == RETRO_HW_CONTEXT_OPENGLES2 || dbp_hw_render.context_type == RETRO_HW_CONTEXT_OPENGLES3 || dbp_hw_render.context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION)
	{
		vertex_shader_srcs[0] = "#define in attribute\n#define out varying\nprecision highp float;";
		fragment_shader_srcs[0] = "#define in varying\n#define texture texture2D\n#define fragColor gl_FragColor\nprecision highp float;";
	}
	else if (((dbp_hw_render.version_major << 16) | dbp_hw_render.version_minor) < 0x30001)
	{
		vertex_shader_srcs[0] = "#define in attribute\n#define out varying\n";
		fragment_shader_srcs[0] = "#define in varying\n#define texture texture2D\n#define fragColor gl_FragColor\n";
	}
	else
	{
		vertex_shader_srcs[0] = "#version 140\n";
		fragment_shader_srcs[0] = "#version 140\nout vec4 fragColor;";
	}

	unsigned vert = CreateShaderOfType(MYGL_VERTEX_SHADER, vertex_shader_srcs_count, vertex_shader_srcs);
	unsigned frag = CreateShaderOfType(MYGL_FRAGMENT_SHADER, fragment_shader_srcs_count, fragment_shader_srcs);
	unsigned prog = myglCreateProgram();
	myglAttachShader(prog, vert);
	myglAttachShader(prog, frag);
	for (int i = 0; i < bind_attribs_count; i++) myglBindAttribLocation(prog, i, bind_attribs[i]);

	int linked;
	myglLinkProgram(prog);
	myglDetachShader(prog, vert); myglDeleteShader(vert);
	myglDetachShader(prog, frag); myglDeleteShader(frag);
	myglGetProgramiv(prog, MYGL_LINK_STATUS, &linked);
	if (!linked)
	{
		int info_len = 0;
		myglGetProgramiv(prog, MYGL_INFO_LOG_LENGTH, &info_len);
		if (info_len > 1)
		{
			char* info_log = (char*)malloc(info_len);
			myglGetProgramInfoLog(prog, info_len, NULL, info_log);
			GFX_ShowMsg("[DBP:GL] Error linking program: %s", info_log);
			free(info_log);
		}
		DBP_ASSERT(0);
		myglDeleteProgram(prog);
		prog = 0;
	}
	return prog;
}
