/*
 *  Copyright (C) 2020-2021 Bernhard Schelling
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
#include "src/ints/int10.h"
#include "src/dos/drives.h"
#include "keyb2joypad.h"
#include "libretro-common/include/libretro.h"
#include "libretro-common/include/retro_timers.h"
#include <string>
#include <chrono>

#ifndef DBP_THREADS_CLASSES
#define DBP_STACK_SIZE (2*1024*1024) //2 MB
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define THREAD_CC WINAPI
struct Thread { typedef DWORD RET_t; typedef RET_t (THREAD_CC *FUNC_t)(LPVOID); static void StartDetached(FUNC_t f, void* p = NULL) { HANDLE h = CreateThread(0,DBP_STACK_SIZE,f,p,0,0); CloseHandle(h); } };
struct Mutex { Mutex() : h(CreateMutexA(0,0,0)) {} ~Mutex() { CloseHandle(h); } __inline void Lock() { WaitForSingleObject(h,INFINITE); } __inline void Unlock() { ReleaseMutex(h); } private:HANDLE h;Mutex(const Mutex&);Mutex& operator=(const Mutex&);};
struct Semaphore { Semaphore() : h(CreateSemaphoreA(0,0,32768,0)) {} ~Semaphore() { CloseHandle(h); } __inline void Post() { ReleaseSemaphore(h, 1, 0); } __inline void Wait() { WaitForSingleObject(h,INFINITE); } private:HANDLE h;Semaphore(const Semaphore&);Semaphore& operator=(const Semaphore&);};
#else
#include <pthread.h>
#define THREAD_CC
struct Thread { typedef void* RET_t; typedef RET_t (THREAD_CC *FUNC_t)(void*); static void StartDetached(FUNC_t f, void* p = NULL) { pthread_t h = 0; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, DBP_STACK_SIZE); pthread_create(&h, &a, f, p); pthread_attr_destroy(&a); pthread_detach(h); } };
struct Mutex { Mutex() { pthread_mutex_init(&h,0); } ~Mutex() { pthread_mutex_destroy(&h); } __inline void Lock() { pthread_mutex_lock(&h); } __inline void Unlock() { pthread_mutex_unlock(&h); } private:pthread_mutex_t h;Mutex(const Mutex&);Mutex& operator=(const Mutex&);friend struct Conditional;};
struct Conditional { Conditional() { pthread_cond_init(&h,0); } ~Conditional() { pthread_cond_destroy(&h); } __inline void Broadcast() { pthread_cond_broadcast(&h); } __inline void Wait(Mutex& m) { pthread_cond_wait(&h,&m.h); } private:pthread_cond_t h;Conditional(const Conditional&);Conditional& operator=(const Conditional&);};
struct Semaphore { Semaphore() : v(0) {} __inline void Post() { m.Lock(); v = 1; c.Broadcast(); m.Unlock(); } __inline void Wait() { m.Lock(); while (!v) c.Wait(m); v = 0; m.Unlock(); } private:Mutex m;Conditional c;int v;Semaphore(const Semaphore&);Semaphore& operator=(const Semaphore&);};
#endif
#endif

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
enum DBP_State : Bit8u
{
	DBPSTATE_BOOT, DBPSTATE_EXITED, DBPSTATE_SHUTDOWN, DBPSTATE_FIRST_FRAME,
#ifndef DBP_REMOVE_OLD_TIMING
	DBPSTATE_WAIT_FIRST_EVENTS, DBPSTATE_WAIT_FIRST_RUN,
#endif
	DBPSTATE_RUNNING
};
enum DBP_SerializeMode : Bit8u { DBPSERIALIZE_DISABLED, DBPSERIALIZE_STATES, DBPSERIALIZE_REWIND };
#if !defined(DBP_REMOVE_OLD_TIMING) || !defined(DBP_REMOVE_NEW_TIMING)
bool dbp_new_timing = true;
#endif
static retro_throttle_state dbp_throttle;
#ifndef DBP_REMOVE_NEW_TIMING
static Semaphore semDoContinue, semDidPause;
static bool dbp_pause_events, dbp_paused_midframe, dbp_frame_pending, dbp_lowlatency, dbp_force60fps;
static float dbp_auto_target = .8f;
static Bit32u dbp_perf_uniquedraw, dbp_perf_count, dbp_perf_emutime, dbp_perf_totaltime, dbp_framecount, dbp_serialize_time;
static enum { DBP_PERF_NONE, DBP_PERF_SIMPLE, DBP_PERF_DETAILED } dbp_perf;
//#define DBP_ENABLE_WAITSTATS
#ifdef DBP_ENABLE_WAITSTATS
static Bit32u dbp_wait_pause, dbp_wait_finish, dbp_wait_paused, dbp_wait_continue;
#endif
#endif
static std::string dbp_crash_message;
static std::string dbp_content_path;
static std::string dbp_content_name;
static retro_time_t dbp_boot_time;
static Bit32u dbp_lastmenuticks;
static DBP_State dbp_state;
static DBP_SerializeMode dbp_serializemode;
static size_t dbp_serializesize;
static char dbp_menu_time;
static bool dbp_game_running;
#ifndef DBP_REMOVE_OLD_TIMING
static Mutex dbp_audiomutex;
static Mutex dbp_lockthreadmtx[2];
static Bit32u dbp_last_run;
static Bit32u dbp_min_sleep;
static bool dbp_timing_tamper;
static bool dbp_lockthreadstate;
static void dbp_calculate_min_sleep() { dbp_min_sleep = (uint32_t)(1000 / (render.src.fps > av_info.timing.fps ? render.src.fps : av_info.timing.fps)); }
static Bit32u dbp_retro_activity;
static Bit32u dbp_wait_activity;
static Bit32u dbp_overload_count;
static retro_usec_t dbp_frame_time;
#define old_DBP_DEFAULT_FPS 60.0f
#endif

// DOSBOX AUDIO/VIDEO
struct DBP_Buffer
{
	Bit32u video[SCALER_MAXWIDTH * SCALER_MAXHEIGHT];
	int16_t audio[4096 * 2]; // mixer blocksize * 2 (96khz @ 30 fps max)
	Bit32u width, height, samples; float ratio;
};
static DBP_Buffer dbp_buffers[2];
static Bit8u buffer_active;
static void(*dbp_gfx_intercept)(DBP_Buffer& buf);

// DOSBOX DISC MANAGEMENT
static std::vector<std::string> dbp_images;
static unsigned dbp_disk_image_index;
static bool dbp_disk_eject_state;
static char dbp_disk_mount_letter;

// DOSBOX INPUT
struct DBP_InputBind
{
	uint8_t port, device, index, id;
	const char* desc;
	int16_t evt, meta, lastval;
};
enum DBP_Port_Device
{
	DBP_DEVICE_Disabled                = RETRO_DEVICE_NONE,
	DBP_DEVICE_DefaultJoypad           = RETRO_DEVICE_JOYPAD,
	DBP_DEVICE_BindGenericKeyboard     = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0),
	DBP_DEVICE_MouseLeftAnalog         = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1),
	DBP_DEVICE_MouseRightAnalog        = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 2),
	DBP_DEVICE_GravisGamepad           = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 3),
	DBP_DEVICE_BasicJoystick1          = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 4),
	DBP_DEVICE_BasicJoystick2          = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 5),
	DBP_DEVICE_ThrustMasterFlightStick = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 6),
	DBP_DEVICE_BothDOSJoysticks        = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 7),
	DBP_DEVICE_BindCustomKeyboard      = RETRO_DEVICE_KEYBOARD,
	DBP_DEVICE_KeyboardMouseLeftStick  = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 1),
	DBP_DEVICE_KeyboardMouseRightStick = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 2),
};
enum { DBP_MAX_PORTS = 8, DBP_JOY_ANALOG_RANGE = 0x8000 }; // analog stick range is -0x8000 to 0x8000
static const char* DBP_KBDNAMES[] =
{
	"None","1","2","3","4","5","6","7","8","9","0","Q","W","E","R","T","Y","U","I","O","P","A","S","D","F","G","H","J","K","L","Z","X","C","V","B","N","M",
	"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","Esc","Tab","Backspace","Enter","Space","Left-Alt","Right-Alt","Left-Ctrl","Right-Ctrl","Left-Shift","Right-Shift",
	"Caps-Lock","Scroll-Lock","Num-Lock","Grave","Minus","Equals","Backslash","Left-Bracket","Right-Bracket","Semicolon","Quote","Period","Comma","Slash","Extra-Lt-Gt",
	"Print-Screen","Pause","Insert","Home","Page-Up","Delete","End","Page-Down","Left","Up","Down","Right","NP-1","NP-2","NP-3","NP-4","NP-5","NP-6","NP-7","NP-8","NP-9","NP-0",
	"NP-Divide","NP-Multiply","NP-Minus","NP-Plus","NP-Enter","NP-Period",""
};
static std::vector<DBP_InputBind> dbp_input_binds;
static DBP_Port_Device dbp_port_devices[DBP_MAX_PORTS];
static bool dbp_bind_unused;
static bool dbp_on_screen_keyboard;
static bool dbp_mouse_input;
static bool dbp_optionsupdatecallback;
static bool dbp_last_hideadvanced;
static char dbp_last_machine;
static char dbp_auto_mapping_mode;
static int16_t dbp_bind_mousewheel;
static int dbp_joy_analog_deadzone = (int)(0.15f * (float)DBP_JOY_ANALOG_RANGE);
static float dbp_mouse_speed = 1;
static float dbp_mouse_speed_x = 1;
static Bit8u* dbp_auto_mapping;
static const char* dbp_auto_mapping_names;
static const char* dbp_auto_mapping_title;
#define DBP_GET_JOY_ANALOG_VALUE(V) ((V >= -dbp_joy_analog_deadzone && V <= dbp_joy_analog_deadzone) ? 0.0f : \
	((float)((V > dbp_joy_analog_deadzone) ? (V - dbp_joy_analog_deadzone) : (V + dbp_joy_analog_deadzone)) / (float)(DBP_JOY_ANALOG_RANGE - dbp_joy_analog_deadzone)))

// DOSBOX EVENTS
enum DBP_Event_Type
{
#ifndef DBP_REMOVE_OLD_TIMING
	DBPETOLD_SET_VARIABLE, DBPETOLD_MOUNT, _DBPETOLD_EXT_MAX, DBPETOLD_UNMOUNT, DBPETOLD_SET_FASTFORWARD, DBPETOLD_LOCKTHREAD, DBPETOLD_SHUTDOWN, _DBPETOLD_INPUT_FIRST,
#endif
	DBPET_JOY1X, DBPET_JOY1Y, DBPET_JOY2X, DBPET_JOY2Y, DBPET_JOYMX, DBPET_JOYMY,
	_DBPET_JOY_AXIS_MAX,

	DBPET_MOUSEXY,
	DBPET_MOUSEDOWN, DBPET_MOUSEUP,
	DBPET_MOUSESETSPEED, DBPET_MOUSERESETSPEED, 
	DBPET_JOYHATSETBIT, DBPET_JOYHATUNSETBIT,
	DBPET_JOY1DOWN, DBPET_JOY1UP,
	DBPET_JOY2DOWN, DBPET_JOY2UP,
	DBPET_KEYDOWN, DBPET_KEYUP,

	DBPET_ONSCREENKEYBOARD,
	DBPET_AXIS_TO_KEY,
	DBPET_CHANGEMOUNTS,

	#define DBP_IS_RELEASE_EVENT(EVT) ((EVT) >= DBPET_MOUSEUP && (EVT & 1))
	#define DBP_KEYAXIS_MAKE(KEY1,KEY2) (((KEY1)<<7)|(KEY2))
	#define DBP_KEYAXIS_GET(VAL, META) ((VAL) < 0 ? (int16_t)((META)>>7) : (int16_t)((META)&127))

	_DBPET_MAX
};
static const char* DBP_Event_Type_Names[] = {
#ifndef DBP_REMOVE_OLD_TIMING
	"SET_VARIABLE", "MOUNT", "EXT_MAX", "UNMOUNT", "SET_FASTFORWARD", "LOCKTHREAD", "SHUTDOWN", "INPUT_FIRST",
#endif
	"JOY1X", "JOY1Y", "JOY2X", "JOY2Y", "JOYMX", "JOYMY", "JOY_AXIS_MAX", "MOUSEXY", "MOUSEDOWN", "MOUSEUP", "MOUSESETSPEED", "MOUSERESETSPEED", "JOYHATSETBIT", "JOYHATUNSETBIT",
	"JOY1DOWN", "JOY1UP", "JOY2DOWN", "JOY2UP", "KEYDOWN", "KEYUP", "ONSCREENKEYBOARD", "AXIS_TO_KEY",
#ifndef DBP_REMOVE_NEW_TIMING
	"CHANGEMOUNTS",
#endif
	"MAX" };
struct DBP_Event
{
	DBP_Event_Type type;
#ifndef DBP_REMOVE_OLD_TIMING
	struct Ext { Section* section; std::string cmd; };
	union { struct { int val, val2; }; Ext* ext; };
#else
	int val, val2;
#endif
};
enum { DBP_EVENT_QUEUE_SIZE = 256, DBP_DOWN_BY_KEYBOARD = 128 };
static DBP_Event dbp_event_queue[DBP_EVENT_QUEUE_SIZE];
static int dbp_event_queue_write_cursor;
static int dbp_event_queue_read_cursor;
static int dbp_keys_down_count;
static unsigned char dbp_keys_down[KBD_LAST+1];
static unsigned short dbp_keymap_dos2retro[KBD_LAST];
static unsigned char dbp_keymap_retro2dos[RETROK_LAST];
static void(*dbp_input_intercept)(DBP_Event_Type, int, int);
static void DBP_QueueEvent(DBP_Event& evt)
{
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
				else if (ie.type >= DBPET_JOY1X && ie.type <= _DBPET_JOY_AXIS_MAX) ie.val += je.val;
				else if (ie.type == DBPET_MOUSEXY) { ie.val += je.val; ie.val2 += je.val2; }
#ifndef DBP_REMOVE_OLD_TIMING
				else if (ie.ext != je.ext) continue;
#endif
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
#ifndef DBP_REMOVE_OLD_TIMING
		if (dbp_event_queue[cur].type <= _DBPETOLD_EXT_MAX) { delete dbp_event_queue[cur].ext; dbp_event_queue[cur].ext = NULL; }
#endif
		for (int n = cur; (n = ((n + 1) % DBP_EVENT_QUEUE_SIZE)) != next; cur = n)
			dbp_event_queue[cur] = dbp_event_queue[n];
	}
	dbp_event_queue[cur] = evt;
	dbp_event_queue_write_cursor = next;
}
static void DBP_QueueEvent(DBP_Event_Type type, int val = 0, int val2 = 0)
{
	switch (type)
	{
		case DBPET_KEYDOWN:
			if (!val || ((++dbp_keys_down[val]) & 127) > 1) return;
			dbp_keys_down_count++;
			break;
		case DBPET_KEYUP:
			if (((dbp_keys_down[val]) & 127) == 0 || ((--dbp_keys_down[val]) & 127) > 0) return;
			dbp_keys_down[val] = 0;
			dbp_keys_down_count--;
			break;
		case DBPET_MOUSEDOWN: case DBPET_JOY1DOWN: case DBPET_JOY2DOWN: dbp_keys_down[KBD_LAST] = 1; break;
		case DBPET_MOUSEUP:   case DBPET_JOY1UP:   case DBPET_JOY2UP:   dbp_keys_down[KBD_LAST] = 0; break;
		default:;
	}
	DBP_Event evt = { type, val, val2 };
	DBP_QueueEvent(evt);
}
#ifndef DBP_REMOVE_OLD_TIMING
static void DBPOld_QueueEvent(DBP_Event_Type type, std::string& swappable_cmd, Section* section = NULL)
{
	DBP_Event evt = { type };
	evt.ext = new DBP_Event::Ext();
	evt.ext->section = section;
	std::swap(evt.ext->cmd, swappable_cmd);
	DBP_QueueEvent(evt);
}
#endif

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

// PERF FPS COUNTERS
//#define DBP_ENABLE_FPS_COUNTERS
#ifdef DBP_ENABLE_FPS_COUNTERS
static Bit32u dbp_lastfpstick, dbp_fpscount_retro, dbp_fpscount_gfxstart, dbp_fpscount_gfxend, dbp_fpscount_event;
#define DBP_FPSCOUNT(DBP_FPSCOUNT_VARNAME) DBP_FPSCOUNT_VARNAME++;
#else
#define DBP_FPSCOUNT(DBP_FPSCOUNT_VARNAME)
#endif

static void retro_notify(int duration, retro_log_level lvl, char const* format,...)
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

// ------------------------------------------------------------------------------

static void DBP_StartOnScreenKeyboard();
void DBP_DOSBOX_ForceShutdown(const Bitu = 0);
void DBP_CPU_ModifyCycles(const char* val);
void DBP_KEYBOARD_ReleaseKeys();
void DBP_CGA_SetModelAndComposite(bool new_model, Bitu new_comp_mode);
void DBP_Hercules_SetPalette(Bit8u pal);
Bit32u DBP_MIXER_GetFrequency();
Bit32u DBP_MIXER_DoneSamplesCount();
void MIXER_CallBack(void *userdata, uint8_t *stream, int len);
bool MSCDEX_HasDrive(char driveLetter);
int MSCDEX_AddDrive(char driveLetter, const char* physicalPath, Bit8u& subUnit);
int MSCDEX_RemoveDrive(char driveLetter);
bool MIDI_TSF_SwitchSF2(const char*);
bool MIDI_Retro_HasOutputIssue();

enum DBP_ThreadCtlMode { TCM_PAUSE_FRAME, TCM_FINISH_FRAME, TCM_RESUME_FRAME, TCM_NEXT_FRAME, TCM_SHUTDOWN };
static void DBP_ThreadControl(DBP_ThreadCtlMode m)
{
	switch (m)
	{
		case TCM_PAUSE_FRAME:
			if (!dbp_frame_pending || dbp_pause_events) return;
			dbp_pause_events = true;
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDidPause.Wait(); dbp_wait_pause += (Bit32u)(time_cb() - t); }
			#else
			semDidPause.Wait();
			#endif
			dbp_pause_events = dbp_frame_pending = dbp_paused_midframe;
			return;
		case TCM_FINISH_FRAME:
			if (!dbp_frame_pending) return;
			if (dbp_pause_events) DBP_ThreadControl(TCM_RESUME_FRAME);
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDidPause.Wait(); dbp_wait_finish += (Bit32u)(time_cb() - t); }
			#else
			semDidPause.Wait();
			#endif
			DBP_ASSERT(!dbp_paused_midframe);
			dbp_frame_pending = false;
			return;
		case TCM_RESUME_FRAME:
			if (!dbp_frame_pending) return;
			DBP_ASSERT(dbp_pause_events);
			dbp_pause_events = false;
			semDoContinue.Post();
			return;
		case TCM_NEXT_FRAME:
			DBP_ASSERT(!dbp_frame_pending);
			dbp_frame_pending = true;
			semDoContinue.Post();
			return;
		case TCM_SHUTDOWN:
			dbp_pause_events = true;
			semDidPause.Wait();
			dbp_pause_events = dbp_frame_pending = false;
			DBP_DOSBOX_ForceShutdown();
			do { semDoContinue.Post(); semDidPause.Wait(); } while (dbp_state != DBPSTATE_EXITED);
			return;
	}
}

static void DBP_UnlockSpeed(bool unlock, int start_frame_skip = 0)
{
	render.frameskip.max = (unlock ? start_frame_skip : 0);
	static Bit32s old_max;
	static bool old_pmode;
	if (unlock)
	{
		old_max = CPU_CycleMax;
		old_pmode = cpu.pmode;
		CPU_CycleMax = (cpu.pmode ? 30000 : 10000);
	}
	else if (old_max)
	{
		// If we switched to protected mode while locked (likely at startup) with auto adjust cycles on, choose a reasonable base rate
		CPU_CycleMax = (old_pmode != cpu.pmode && cpu.pmode && CPU_CycleAutoAdjust ? 200000 : old_max);
		old_max = 0;
	}
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		extern bool ticksLocked;
		ticksLocked = unlock;
		CPU_CycleLeft = CPU_Cycles = 0;
		void DBP_DOSBOX_ResetTickTimer();
		DBP_DOSBOX_ResetTickTimer();
	}
#endif
}

static void DBP_AppendImage(const char* entry, bool sorted)
{
	// insert into image list ordered alphabetically, ignore already known images
	size_t insert_index;
	for (insert_index = 0; insert_index != dbp_images.size(); insert_index++)
	{
		if (dbp_images[insert_index] == entry) return;
		if (sorted && dbp_images[insert_index] > entry) { break; }
	}
	dbp_images.insert(dbp_images.begin() + insert_index, entry);
}

static DOS_Drive* DBP_Mount(const char* path, bool is_boot, bool set_content_name)
{
	const char *last_slash = strrchr(path, '/'), *last_bslash = strrchr(path, '\\');
	const char *path_file = (last_slash && last_slash > last_bslash ? last_slash + 1 : (last_bslash ? last_bslash + 1 : path));
	const char *ext = strrchr(path_file, '.');
	if (!ext) return NULL;

	const char *fragment = strrchr(path_file, '#');
	if (fragment && ext > fragment) // check if 'FOO.ZIP#BAR.EXE'
	{
		const char* real_ext = fragment - (fragment - 3 > path_file && fragment[-3] == '.' ? 3 : 4);
		if (real_ext > path_file && *real_ext == '.') ext = real_ext;
		else fragment = NULL;
	}

	// A drive letter can be specified either by naming the mount file '.<letter>.<extension>' or by loading a path with an added '#<letter>' suffix.
	char letter = 0;
	const char *p_fra_drive = (fragment && fragment[1] && !fragment[2] ? fragment + 1 : NULL);
	const char *p_dot_drive = (ext - path > 2 && ext[-2] == '.' ? ext - 1 : NULL);
	if      (p_fra_drive && (*p_fra_drive >= 'A' && *p_fra_drive <= 'Z')) letter = *p_fra_drive;
	else if (p_fra_drive && (*p_fra_drive >= 'a' && *p_fra_drive <= 'z')) letter = *p_fra_drive - 0x20;
	else if (p_dot_drive && (*p_dot_drive >= 'A' && *p_dot_drive <= 'Z')) letter = *p_dot_drive;
	else if (p_dot_drive && (*p_dot_drive >= 'a' && *p_dot_drive <= 'z')) letter = *p_dot_drive - 0x20;
	if (!is_boot && dbp_disk_mount_letter) letter = dbp_disk_mount_letter;
	if (letter && Drives[letter-'A']) { DBP_ASSERT(0); return NULL; }

	if (set_content_name)
	{
		dbp_content_path = path;
		dbp_content_name = std::string(path_file, (p_dot_drive ? p_dot_drive - 1 : ext) - path_file);
	}

	std::string path_no_fragment;
	if (fragment)
	{
		path_no_fragment = std::string(path, fragment - path);
		ext       = path_no_fragment.c_str() + (ext       - path);
		path_file = path_no_fragment.c_str() + (path_file - path);
		path      = path_no_fragment.c_str();
	}

	DOS_Drive* res = NULL;
	Bit8u res_media_byte = 0;
	if (!strcasecmp(ext, ".zip") || !strcasecmp(ext, ".dosz"))
	{
		FILE* zip_file_h = fopen_wrap(path, "rb");
		if (!zip_file_h)
		{
			if (!is_boot) dbp_disk_eject_state = true;
			retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s", "ZIP", path);
			return NULL;
		}
		res = new zipDrive(new rawFile(zip_file_h, false), true);

		// Use zip filename as drive label, cut off at file extension, the first occurence of a ( or [ character or right white space.
		char lbl[11+1], *lblend = lbl + (ext - path_file > 11 ? 11 : ext - path_file);
		memcpy(lbl, path_file, lblend - lbl);
		for (char* c = lblend; c > lbl; c--) { if (c == lblend || *c == '(' || *c == '[' || (*c <= ' ' && !c[1])) *c = '\0'; }
		res->label.SetLabel(lbl, !(is_boot && (!letter || letter == 'C')), true);

		if (is_boot && (!letter || letter == 'C')) return res;
		if (!letter) letter = 'D';
		if (letter > 'C')
		{
			Bit8u subUnit;
			MSCDEX_AddDrive(letter, "", subUnit);
		}
		else if (letter < 'C')
		{
			res_media_byte = 0xF0; //floppy
		}
	}
	else if (!strcasecmp(ext, ".img") || !strcasecmp(ext, ".ima") || !strcasecmp(ext, ".vhd"))
	{
		fatDrive* fat = new fatDrive(path, 512, 63, 16, 0, 0);
		if (!fat->loadedDisk || !fat->created_successfully)
		{
			delete fat;
			goto MOUNT_ISO;
		}
		bool is_hdd = fat->loadedDisk->hardDrive;
		if (is_boot && is_hdd && (!letter || letter == 'C')) return fat;
		if (!letter) letter = (is_hdd ? 'D' : 'A');

		// Copied behavior from IMGMOUNT::Run, force obtaining the label and saving it in label
		RealPt save_dta = dos.dta();
		dos.dta(dos.tables.tempdta);
		DOS_DTA dta(dos.dta());
		dta.SetupSearch(255, DOS_ATTR_VOLUME, (char*)"*.*");
		fat->FindFirst((char*)"", dta);
		dos.dta(save_dta);

		// Register with BIOS/CMOS
		if (letter-'A' < MAX_DISK_IMAGES)
		{
			if (imageDiskList[letter-'A']) { DBP_ASSERT(0); delete imageDiskList[letter-'A']; }
			imageDiskList[letter-'A'] = fat->loadedDisk;
		}

		// Because fatDrive::GetMediaByte is different than all others...
		res_media_byte = (is_hdd ? 0xF8 : 0xF0);
		res = fat;
	}
	else if (!strcasecmp(ext, ".iso") || !strcasecmp(ext, ".cue") || !strcasecmp(ext, ".ins"))
	{
		MOUNT_ISO:
		int error = -1;
		if (!letter) letter = 'D';
		res = new isoDrive(letter, path, 0xF8, error);
		if (error)
		{
			delete res;
			if (!is_boot) dbp_disk_eject_state = true;
			retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s", "image", path);
			return NULL;
		}
	}
	else if (!strcasecmp(ext, ".exe") || !strcasecmp(ext, ".com") || !strcasecmp(ext, ".bat"))
	{
		if (!letter) letter = (is_boot ? 'C' : 'D');
		res = new localDrive(std::string(path, path_file - path).c_str(), 512, 32, 32765, 16000, 0xF8);
		res->label.SetLabel("PURE", false, true);
		path = NULL; // don't treat as disk image
	}
	else if (!strcasecmp(ext, ".m3u") || !strcasecmp(ext, ".m3u8"))
	{
		FILE* m3u_file_h = fopen_wrap(path, "rb");
		if (!m3u_file_h)
		{
			retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s", "M3U", path);
			return NULL;
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

	if (res)
	{
		DBP_ASSERT(!Drives[letter-'A']);
		Drives[letter-'A'] = res;
		mem_writeb(Real2Phys(dos.tables.mediaid) + (letter-'A') * 9, (res_media_byte ? res_media_byte : res->GetMediaByte()));
		if (path)
		{
			if (is_boot) DBP_AppendImage(path, false);
			else dbp_disk_mount_letter = letter;
		}
	}
	return NULL;
}

static void DBP_Shutdown()
{
	if (dbp_state != DBPSTATE_EXITED && dbp_state != DBPSTATE_SHUTDOWN)
	{
		dbp_state = DBPSTATE_RUNNING;
		if (dbp_new_timing)
		{
			DBP_ThreadControl(TCM_SHUTDOWN);
		}
#ifndef DBP_REMOVE_OLD_TIMING
		if (!dbp_new_timing)
		{
			DBP_QueueEvent(DBPETOLD_SHUTDOWN);
			while (dbp_state != DBPSTATE_EXITED) retro_sleep(50);
		}
#endif
	}
	if (!dbp_crash_message.empty())
	{
		retro_notify(0, RETRO_LOG_ERROR, "DOS crashed: %s", dbp_crash_message.c_str());
		dbp_crash_message.clear();
	}
	if (control)
	{
		DBP_ASSERT(!first_shell); //should have been properly cleaned up
		delete control;
		control = NULL;
	}
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		for (DBP_Event& e : dbp_event_queue)
		{
			if (e.type > _DBPETOLD_EXT_MAX) continue;
			delete e.ext;
			e.ext = NULL;
		}
		dbp_event_queue_write_cursor = dbp_event_queue_read_cursor = 0;
	}
#endif
	dbp_state = DBPSTATE_SHUTDOWN;
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

void DBP_DelayTicks(Bit32u ms)
{
	retro_sleep(ms);
}

void DBP_MidiDelay(Bit32u ms)
{
	if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD) return;
	retro_sleep(ms);
}

#ifndef DBP_REMOVE_OLD_TIMING
static void DBP_LockThread(bool lock)
{
	if (lock && !dbp_lockthreadstate)
	{
		dbp_lockthreadstate = true;
		dbp_lockthreadmtx[0].Lock();
		DBP_QueueEvent(DBPETOLD_LOCKTHREAD);
		dbp_lockthreadmtx[1].Lock();
	}
	else if (!lock && dbp_lockthreadstate)
	{
		dbp_lockthreadmtx[0].Unlock();
		dbp_lockthreadmtx[1].Unlock();
		dbp_lockthreadstate = false;
	}
}

void DBP_LockAudio()
{
	dbp_audiomutex.Lock();
}

void DBP_UnlockAudio()
{
	dbp_audiomutex.Unlock();
}
#endif

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
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		dbp_calculate_min_sleep();
	}
#endif
	//const char* VGAModeNames[] { "M_CGA2","M_CGA4","M_EGA","M_VGA","M_LIN4","M_LIN8","M_LIN15","M_LIN16","M_LIN32","M_TEXT","M_HERC_GFX","M_HERC_TEXT","M_CGA16","M_TANDY2","M_TANDY4","M_TANDY16","M_TANDY_TEXT","M_ERROR"};
	//log_cb(RETRO_LOG_INFO, "[DOSBOX SIZE] Width: %u - Height: %u - Ratio: %f (%f) - DBLH: %d - DBLW: %d - BPP: %u - Mode: %s (%d)\n",
	//	(unsigned)width, (unsigned)height, (float)((width * scalex) / (height * scaley)), render.src.ratio, render.src.dblh, render.src.dblw, render.src.bpp, VGAModeNames[vga.mode], vga.mode);
	return GFX_GetBestMode(0);
}

bool GFX_StartUpdate(Bit8u*& pixels, Bitu& pitch)
{
	DBP_FPSCOUNT(dbp_fpscount_gfxstart)
	DBP_Buffer& buf = dbp_buffers[buffer_active^1];
	pixels = (Bit8u*)buf.video;
	pitch = render.src.width * 4;
	if (render.src.width != buf.width || render.src.height != buf.height)
	{
		buf.width = (Bit32u)render.src.width;
		buf.height = (Bit32u)render.src.height;
		buf.ratio = (float)buf.width / buf.height;
		if (buf.ratio < 1) buf.ratio *= 2; //because render.src.dblw is not reliable
		if (buf.ratio > 2) buf.ratio /= 2; //because render.src.dblh is not reliable
	}
	return true;
}

void GFX_EndUpdate(const Bit16u *changedLines)
{
	if (!changedLines) return;

	buffer_active ^= 1;
	DBP_Buffer& buf = dbp_buffers[buffer_active];
	//DBP_ASSERT((Bit8u*)buf.video == render.scale.outWrite - render.scale.outPitch * render.src.height); // this assert can fail after loading a save game
	DBP_ASSERT(render.scale.outWrite >= (Bit8u*)buf.video && render.scale.outWrite <= (Bit8u*)buf.video+ sizeof(buf.video));

	if (
		#ifndef DBP_ENABLE_FPS_COUNTERS
		dbp_perf == DBP_PERF_DETAILED &&
		#endif
		memcmp(dbp_buffers[0].video, dbp_buffers[1].video, buf.width * buf.height * 4))
	{
		DBP_FPSCOUNT(dbp_fpscount_gfxend)
		dbp_perf_uniquedraw++;
	}

	if (dbp_gfx_intercept) dbp_gfx_intercept(buf);

	// Tell dosbox to draw the next frame completely, not just the scanlines that changed (could also issue GFX_CallBackRedraw)
	render.scale.clearCache = true;

	if (dbp_new_timing)
	{
		// frameskip is best to be modified in this function
		dbp_framecount += 1 + render.frameskip.max;

		static unsigned last_throttle_mode;
		if (last_throttle_mode != dbp_throttle.mode)
		{
			if (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD)
				DBP_UnlockSpeed(true, 10);
			else if (last_throttle_mode == RETRO_THROTTLE_FAST_FORWARD)
				DBP_UnlockSpeed(false);
			last_throttle_mode = dbp_throttle.mode;
		}

		if (dbp_state == DBPSTATE_FIRST_FRAME && render.frameskip.max)
			DBP_UnlockSpeed(false);

		render.frameskip.max = 0;
		if (dbp_throttle.rate < render.src.fps - 1 && dbp_throttle.rate > 10 &&
			dbp_throttle.mode != RETRO_THROTTLE_FRAME_STEPPING && dbp_throttle.mode != RETRO_THROTTLE_FAST_FORWARD && dbp_throttle.mode != RETRO_THROTTLE_SLOW_MOTION && dbp_throttle.mode != RETRO_THROTTLE_REWINDING)
		{
			static float accum;
			accum += (render.src.fps - dbp_throttle.rate);
			if (accum >= dbp_throttle.rate)
			{
				//log_cb(RETRO_LOG_INFO, "[GFX_EndUpdate] SKIP 1 AT %u\n", dbp_framecount);
				render.frameskip.max = 1;
				accum -= dbp_throttle.rate;
			}
		}
	}

#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		if (dbp_state == DBPSTATE_FIRST_FRAME)
			dbp_state = DBPSTATE_WAIT_FIRST_EVENTS;
	
		// When pausing the frontend we need to make sure CycleAutoAdjust is only re-activated after normal rendering has resumed
		extern bool CPU_SkipCycleAutoAdjust;
		static Bit8u stall_frames, resume_frames;
		static Bit32u last_retro_activity;
		if (dbp_retro_activity != last_retro_activity)
		{
			last_retro_activity = dbp_retro_activity;
			if (stall_frames) stall_frames = 0;
			if (resume_frames && resume_frames++ > 4) { CPU_SkipCycleAutoAdjust = false; resume_frames = 0; }
		}
		else if ((dbp_timing_tamper || stall_frames++ > 4) && dbp_state == DBPSTATE_RUNNING && !first_shell->exit)
		{
			stall_frames = resume_frames = 1;
			CPU_SkipCycleAutoAdjust = true;
			dbp_wait_activity = last_retro_activity;
		}
	}
#endif
}

static bool GFX_Events_AdvanceFrame()
{
	enum { HISTORY_STEP = 4, HISTORY_SIZE = HISTORY_STEP * 2 };
	static struct
	{
		retro_time_t TimeLast;
		double LastModeHash;
		Bit32u LastFrameCount, FrameTicks, Paused, HistoryCycles[HISTORY_SIZE], HistoryEmulator[HISTORY_SIZE], HistoryFrame[HISTORY_SIZE], HistoryCursor;
	} St;

	St.FrameTicks++;
	if (St.LastFrameCount == dbp_framecount)
	{
		if (dbp_pause_events)
		{
			retro_time_t time_before_pause = time_cb();
			dbp_paused_midframe = true;
			semDidPause.Post();
			#ifdef DBP_ENABLE_WAITSTATS
			{ retro_time_t t = time_cb(); semDoContinue.Wait(); dbp_wait_paused += (Bit32u)(time_cb() - t); }
			#else
			semDoContinue.Wait();
			#endif
			dbp_paused_midframe = false;
			St.Paused += (Bit32u)(time_cb() - time_before_pause);
		}
		return false;
	}

	Bit32u finishedframes = (dbp_framecount - St.LastFrameCount);
	Bit32u finishedticks = St.FrameTicks;
	St.LastFrameCount = dbp_framecount;
	St.FrameTicks = 0;

#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing) return true;
#endif

	// With certain keyboard layouts, we can end up here during startup which we don't want to do anything further
	if (dbp_state == DBPSTATE_BOOT) return true;

	retro_time_t time_before = time_cb() - St.Paused;
	St.Paused = 0;

	DBP_Buffer& buf = dbp_buffers[buffer_active];

	if (dbp_throttle.rate < render.src.fps - 1 && dbp_throttle.rate > 10)
		buf.samples = (Bit32u)(av_info.timing.sample_rate / dbp_throttle.rate + .9999);
	else
		buf.samples = DBP_MIXER_DoneSamplesCount();
	if (buf.samples > 4096) buf.samples = 4096;
	if (buf.samples) MIXER_CallBack(0, (uint8_t*)buf.audio, buf.samples * 4);

	semDidPause.Post();
	#ifdef DBP_ENABLE_WAITSTATS
	{ retro_time_t t = time_cb(); semDoContinue.Wait(); dbp_wait_continue += (Bit32u)(time_cb() - t); }
	#else
	semDoContinue.Wait();
	#endif

	retro_time_t time_after = time_cb();
	retro_time_t time_last = St.TimeLast;
	St.TimeLast = time_after;

	if (dbp_perf)
	{
		dbp_perf_count += finishedframes;
		dbp_perf_emutime += (Bit32u)(time_before - time_last);
		dbp_perf_totaltime += (Bit32u)(time_after - time_last);
	}

	// Skip evaluating the performance of this frame if the display mode has changed
	double modeHash = render.src.ratio * render.src.fps * render.src.width * render.src.height * (double)vga.mode;
	if (modeHash != St.LastModeHash)
	{
		//log_cb(RETRO_LOG_INFO, "[DBPTIMERS@%4d] NEW VIDEO MODE %f|%f|%d|%d|%d|%d|\n", St.HistoryCursor, render.src.ratio, render.src.fps, (int)render.src.width, (int)render.src.height, (int)vga.mode);
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

	bool wasFrameEnd = GFX_Events_AdvanceFrame();

#ifndef DBP_REMOVE_OLD_TIMING
	bool wait_until_activity = !!dbp_wait_activity;
	bool wait_until_run = (dbp_state == DBPSTATE_WAIT_FIRST_EVENTS);

	check_new_events:
#endif

	static bool mouse_speed_up, mouse_speed_down;
	static int mouse_joy_x, mouse_joy_y, hatbits;
	for (;dbp_event_queue_read_cursor != dbp_event_queue_write_cursor; dbp_event_queue_read_cursor = ((dbp_event_queue_read_cursor + 1) % DBP_EVENT_QUEUE_SIZE))
	{
		DBP_Event e = dbp_event_queue[dbp_event_queue_read_cursor];
		//log_cb(RETRO_LOG_INFO, "[DOSBOX EVENT] [@%6d] %s %08x%s\n", DBP_GetTicks(), (e.type > _DBPET_MAX ? "SPECIAL" : DBP_Event_Type_Names[(int)e.type]), (unsigned)e.val, (dbp_input_intercept && e.type >= _DBPETOLD_INPUT_FIRST ? " [INTERCEPTED]" : ""));
		if (dbp_input_intercept
#ifndef DBP_REMOVE_OLD_TIMING
				&& e.type >= _DBPETOLD_INPUT_FIRST
#endif
			)
		{
			dbp_input_intercept(e.type, e.val, e.val2);
			if (!DBP_IS_RELEASE_EVENT(e.type)) continue;
		}
		switch (e.type)
		{
#ifndef DBP_REMOVE_OLD_TIMING
			case DBPETOLD_SET_VARIABLE:
				if (!memcmp(e.ext->cmd.c_str(), "midiconfig=", 11) && MIDI_TSF_SwitchSF2(e.ext->cmd.c_str() + 11))
				{
					// Do the SF2 reload directly (otherwise midi output stops until dos program restart)
					e.ext->section->HandleInputline(e.ext->cmd);
				}
				else if (!memcmp(e.ext->cmd.c_str(), "cycles=", 7))
				{
					// Set cycles value without Destroy/Init (because that can cause FPU overflow crashes)
					DBP_CPU_ModifyCycles(e.ext->cmd.c_str() + 7);
					e.ext->section->HandleInputline(e.ext->cmd);
				}
				else
				{
					e.ext->section->ExecuteDestroy(false);
					e.ext->section->HandleInputline(e.ext->cmd);
					e.ext->section->ExecuteInit(false);
				}
				delete e.ext;
				dbp_event_queue[dbp_event_queue_read_cursor].ext = NULL;
				break;

			case DBPETOLD_MOUNT:
				if (!Drives['A'-'A'] && !Drives['D'-'A'])
					DBP_Mount(e.ext->cmd.c_str(), false, false);
				delete e.ext;
				dbp_event_queue[dbp_event_queue_read_cursor].ext = NULL;
				break;

			case DBPETOLD_UNMOUNT:
				if (dbp_disk_mount_letter && Drives[dbp_disk_mount_letter-'A'] && Drives[dbp_disk_mount_letter-'A']->UnMount() == 0)
				{
					Drives[dbp_disk_mount_letter-'A'] = 0;
					mem_writeb(Real2Phys(dos.tables.mediaid)+(dbp_disk_mount_letter-'A')*9,0);
				}
				break;

			case DBPETOLD_SET_FASTFORWARD: 
				DBP_UnlockSpeed(!!e.val, 10);
				break;

			case DBPETOLD_LOCKTHREAD:
				dbp_lockthreadmtx[1].Unlock();
				dbp_lockthreadmtx[0].Lock();
				dbp_lockthreadmtx[1].Lock();
				dbp_lockthreadmtx[0].Unlock();
				break;

			case DBPETOLD_SHUTDOWN:
				DBP_DOSBOX_ForceShutdown();
				goto abort_gfx_events;
#endif

			case DBPET_KEYDOWN: KEYBOARD_AddKey((KBD_KEYS)e.val, true);  break;
			case DBPET_KEYUP:   KEYBOARD_AddKey((KBD_KEYS)e.val, false); break;

			case DBPET_ONSCREENKEYBOARD:
				DBP_StartOnScreenKeyboard();
				break;

			case DBPET_MOUSEXY:
			{
				float mx = e.val*dbp_mouse_speed*dbp_mouse_speed_x, my = e.val2*dbp_mouse_speed; // good for 320x200?
				Mouse_CursorMoved(mx, my, 0, 0, true);
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
		}
	}

#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		if (wait_until_activity)
		{
			if (dbp_wait_activity == dbp_retro_activity && dbp_state == DBPSTATE_RUNNING && !first_shell->exit)
			{
				retro_sleep(1);
				goto check_new_events;
			}
			dbp_wait_activity = 0;
			void DBP_DOSBOX_ResetTickTimer();
			DBP_DOSBOX_ResetTickTimer();
		}

		if (wait_until_run)
		{
			if (dbp_state == DBPSTATE_WAIT_FIRST_EVENTS) dbp_state = DBPSTATE_WAIT_FIRST_RUN;
			if (dbp_state == DBPSTATE_WAIT_FIRST_RUN && !first_shell->exit)
			{
				retro_sleep(1);
				goto check_new_events;
			}
			DBP_UnlockSpeed(dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD, 10); // also resets tick timer
		}
	}
#endif

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

#ifndef DBP_REMOVE_OLD_TIMING
	abort_gfx_events:
#endif
	GFX_EVENTS_RECURSIVE = false;
}

void GFX_SetTitle(Bit32s cycles, int frameskip, bool paused)
{
	extern const char* RunningProgram;
	dbp_game_running = (strcmp(RunningProgram, "DOSBOX") && strcmp(RunningProgram, "PUREMENU"));
	log_cb(RETRO_LOG_INFO, "[DOSBOX STATUS] Program: %s - Cycles: %d - Frameskip: %d - Paused: %d\n", RunningProgram, cycles, frameskip, paused);
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

static void DBP_PureMenuProgram(Program** make)
{
	static struct Menu* menu;

	struct FakeBatch : BatchFile
	{
		int count;
		std::string exe;
		FakeBatch(std::string& _exe) : BatchFile(first_shell,"Z:\\AUTOEXEC.BAT","",""), count(0) { std::swap(exe, _exe); }
		virtual bool ReadLine(char * line)
		{
			const char *p = exe.c_str(), *f = strrchr(p, '\\') + 1, *fext;
			switch (count++)
			{
				case 0:
					memcpy(line + 0, "@ :\n", 5);
					line[1] = p[0];
					break;
				case 1:
					memcpy(line + 0, "@cd ", 4);
					memcpy(line + 4, p + 2, (f - (f == p + 3 ? 0 : 1) - p - 2));
					memcpy(line + 4 + (f - (f == p + 3 ? 0 : 1) - p - 2), "\n", 2);
					break;
				case 2:
				{
					bool isbat = ((fext = strrchr(f, '.')) && !strcasecmp(fext, ".bat"));
					int call_cmd_len = (isbat ? 5 : 0), flen = (int)strlen(f);
					memcpy(line, "@", 1);
					memcpy(line+1, "call ", call_cmd_len);
					memcpy(line+call_cmd_len+1, f, flen);
					memcpy(line+call_cmd_len+1+flen, "\n", 2);
					break;
				}
				case 3:
					memcpy(line, "@Z:PUREMENU -FINISH\n", 21);
					delete this;
					break;
			}
			return true;
		}
	};

	struct Menu : Program
	{
		Menu() : result(0), sel(0), exe_count(0), fs_count(0), scroll(0), mousex(0), mousey(0), joyx(0), joyy(0), init_autosel(0), init_autoskip(0), autoskip(0),
			have_autoboot(false), use_autoboot(false), multidrive(false), open_ticks(DBP_GetTicks()) { }

		~Menu() {}

		int result, sel, exe_count, fs_count, scroll, mousex, mousey, joyx, joyy, init_autosel, init_autoskip, autoskip;
		bool have_autoboot, use_autoboot, multidrive;
		Bit32u open_ticks;
		std::vector<std::string> list;

		enum
		{
			ATTR_HEADER    = 0x0B, //cyan in color, white in hercules
			ATTR_NORMAL    = 0x0E, //yellow in color, white in hercules
			ATTR_HIGHLIGHT = 0x78, //dark gray on gray in color, white on gray in hercules
			ATTR_WHITE     = 0x0F,

			RESULT_LAUNCH      = 1,
			RESULT_COMMANDLINE = 2,
			RESULT_SHUTDOWN    = 3,
		};

		void RefreshFileList(bool initial_scan)
		{
			list.clear();
			exe_count = fs_count = 0;
			size_t old_images_size = dbp_images.size();
			int old_sel = sel;
			// Scan drive C first, any others after
			sel = ('C'-'A');
			DriveFileIterator(Drives[sel], FileIter, (Bitu)this);
			if (fs_count)
			{
				for (int i = 0; i != fs_count; i++)
				{
					// Filter image files that have the same name as a cue file
					const char *pEntry = list[i].c_str(), *pExt = strrchr(pEntry, '.');
					if (!pExt || (strcasecmp(pExt, ".cue") && strcasecmp(pExt, ".ins"))) continue;
					for (int j = fs_count; j--;)
					{
						if (i == j || strncasecmp(list[j].c_str(), pEntry, pExt - pEntry + 1)) continue;
						list.erase(list.begin() + j);
						if (i > j) i--;
						fs_count--;
					}
				}
				for (int i = 0; i != fs_count; i++)
					DBP_AppendImage(list[i].c_str(), true);
			}
			if (initial_scan && !old_images_size && dbp_images.size())
			{
				dbp_disk_eject_state = false;
				dbp_disk_image_index = 0;
				DBP_Mount(dbp_images[0].c_str(), false, false);
			}
			for (sel = 0; sel != ('Z'-'A'); sel++)
			{
				if (sel == ('C'-'A') || !Drives[sel]) continue;
				DriveFileIterator(Drives[sel], FileIter, (Bitu)this);
				multidrive = true;
			}
			sel = (list.empty() ? 2 : old_sel);
			if (!initial_scan) return;
			char autostr[DOS_PATHLENGTH + 32] = {0,1};
			if (have_autoboot)
			{
				Bit16u autostrlen = (Bit16u)(sizeof(autostr) - 1);
				DOS_File *autobootfile = nullptr;
				Drives['C'-'A']->FileOpen(&autobootfile, (char*)"AUTOBOOT.DBP", OPEN_READ);
				autobootfile->AddRef();
				autobootfile->Read((Bit8u*)autostr, &autostrlen);
				autobootfile->Close();
				delete autobootfile;
				autostr[autostrlen] = '\0';
				char *nameend = strchr(autostr, '\n'), *skip = nameend;
				while (nameend > autostr && *nameend <= ' ') nameend--;
				while (skip && *skip && *skip <= ' ') skip++;
				if (nameend) nameend[1] = '\0';
				if (skip) init_autoskip = autoskip = atoi(skip);
			}
			else if (strrchr(dbp_content_path.c_str(), '#'))
			{
				memcpy(autostr, "C:\\", 3);
				safe_strncpy(autostr + 3, strrchr(dbp_content_path.c_str(), '#') + 1, DOS_PATHLENGTH + 16);
			}
			if (autostr[0])
			{
				for (std::string& name : list)
				{
					if (name != autostr) continue;
					use_autoboot = have_autoboot = true;
					init_autosel = sel = (int)(&name - &list[0]);
					return;
				}
				init_autoskip = autoskip = 0;
			}
			sel = fs_count;
		}

		static void FileIter(const char* path, bool is_dir, Bit32u size, Bit16u, Bit16u, Bit8u, Bitu data)
		{
			if (is_dir) return;
			Menu* m = (Menu*)data;
			if (m->sel == ('C'-'A') && !memcmp(path, "AUTOBOOT.DBP", sizeof("AUTOBOOT.DBP")))
			{
				m->have_autoboot = true;
				return;
			}
			const char* fext = strrchr(path, '.');
			if (!fext) return;
			bool isEXE = (!strcasecmp(fext, ".exe") || !strcasecmp(fext, ".com") || !strcasecmp(fext, ".bat"));
			bool isFS = (!isEXE && m->sel == ('C'-'A') && (!strcasecmp(fext, ".iso") || !strcasecmp(fext, ".cue") || !strcasecmp(fext, ".ins") || !strcasecmp(fext, ".img") || !strcasecmp(fext, ".ima") || !strcasecmp(fext, ".vhd")));
			if (!isEXE && !isFS) return;
			if (isFS && !strncasecmp(fext + 1, "im", 2) && (size < 163840 || (size <= 2949120 && (size % 20480)))) return; //validate floppy images
			if (isFS && !strcasecmp(fext, ".ins"))
			{
				// Make sure this is an actual CUE file with an INS extension
				if (size >= 16384) return;
				Bit8u cmd[6];
				Bit16u cmdlen = (Bit16u)sizeof(cmd);
				DOS_File *insfile = nullptr;
				Drives['C'-'A']->FileOpen(&insfile, (char*)path, OPEN_READ);
				insfile->AddRef();
				insfile->Read(cmd, &cmdlen);
				insfile->Close();
				delete insfile;
				if (cmdlen != sizeof(cmd) || memcmp(cmd, "FILE \"", sizeof(cmd))) return;
			}
			(isEXE ? m->exe_count : m->fs_count)++;

			int insert_index;
			std::string entry;
			entry.reserve(4 + (fext - path) + 4);
			if (isFS) entry += '$';
			entry += ('A' + m->sel);
			entry += ':';
			entry += '\\';
			entry += path;

			// insert into menu list ordered alphabetically
			for (insert_index = 0; insert_index != (int)m->list.size(); insert_index++)
				if (m->list[insert_index] > entry) { break; }
			m->list.insert(m->list.begin() + insert_index, std::string());
			std::swap(m->list[insert_index], entry);
		}

		static void DrawText(Bit16u x, Bit16u y, const char* txt, Bit8u attr)
		{
			for (; *txt; txt++, x++)
			{
				Bit8u page = real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
				extern void WriteChar(Bit16u col,Bit16u row,Bit8u page,Bit8u chr,Bit8u attr,bool useattr);
				WriteChar(x,y,page,*txt,attr,true);
			}
		}

		void RedrawScreen()
		{
			ClearScreen();
			INT10_SetCursorShape(0, 0);
			INT10_SetCursorPos((Bit8u)CurMode->twidth, (Bit8u)CurMode->theight, 0);

			// Horizontal lines
			for (Bit16u x = 0; x != CurMode->twidth; x++)
			{
				DrawText(x, 0, "\xCD", ATTR_HEADER);
				DrawText(x, 2, "\xCD", ATTR_HEADER);
				DrawText(x, (Bit16u)CurMode->theight-2, "\xCD", ATTR_HEADER);
			}

			// Header
			DrawText((int)CurMode->twidth / 2 - 12, 0, " DOSBOX PURE START MENU ", ATTR_HEADER);
			DrawText(((int)CurMode->twidth - (int)dbp_content_name.length()) / 2, 1, dbp_content_name.c_str(), 9);
			if (dbp_content_name.empty())
				DrawText(((int)CurMode->twidth - 18) / 2, 1, "no content loaded!", 9);
			DrawText(0, 0, "\xC9", ATTR_HEADER);
			DrawText(0, 1, "\xBA", ATTR_HEADER);
			DrawText(0, 2, "\xC8", ATTR_HEADER);
			DrawText((int)CurMode->twidth - 1, 0, "\xBB", ATTR_HEADER);
			DrawText((int)CurMode->twidth - 1, 1, "\xBA", ATTR_HEADER);
			DrawText((int)CurMode->twidth - 1, 2, "\xBC", ATTR_HEADER);

			// Footer
			DrawText((int)CurMode->twidth - 40, (Bit16u)CurMode->theight-1, "\xB3 \x18\x19 Scroll \xB3 \x1A\x1B Set Auto Start \xB3 \x7 Run", ATTR_HEADER);
			DrawText((int)CurMode->twidth - 40, (Bit16u)CurMode->theight-2, "\xD1", ATTR_HEADER);
			DrawText((int)CurMode->twidth - 28, (Bit16u)CurMode->theight-2, "\xD1", ATTR_HEADER);
			DrawText((int)CurMode->twidth -  8, (Bit16u)CurMode->theight-2, "\xD1", ATTR_HEADER);

			//// Test all font characters
			//for (int i = 0; i != 256; i++) { char bla[2] { (char)i, 0 }; DrawText(i, 2, bla, ATTR_HEADER); }

			DrawMenu();
		}

		void DrawMenu(int sel_change = 0)
		{
			int mid = (int)CurMode->twidth / 2;
			int maxy = (int)CurMode->theight - 5;
			int count = (int)list.size() + 3, min = (count == 3 ? 1 : 0);
			int starty = (int)(count >= maxy-1 ? 3 : 4 + min);
			if (sel < min) sel = count - 1;
			if (sel >= count) sel = 0;
			if (sel == count-3) sel += (sel_change > 0 ? 1 : -1);
			if (count > maxy)
			{
				if (sel < scroll+     4) scroll = (sel <         4 ?            0 : sel -        4);
				if (sel > scroll+maxy-5) scroll = (sel > count - 5 ? count - maxy : sel - maxy + 5);
			}

			if (count == 3) DrawText(mid - 12, starty - 1, "No executable file found", ATTR_HEADER);

			bool autostart_info = false;
			for (int i = scroll; i != count && i != (scroll + maxy); i++)
			{
				int y = starty + i - scroll;
				for (Bit16u x = 0; x != CurMode->twidth; x++) DrawText(x, y, " ", 0);
				if (i >= count - 3)
				{
					if (i == count - 2) DrawText(mid - 9, y, "Go to command line", (i == sel ? ATTR_HIGHLIGHT : ATTR_NORMAL));
					if (i == count - 1) DrawText(mid - 2, y, "Exit", (i == sel ? ATTR_HIGHLIGHT : ATTR_NORMAL));
					continue;
				}
				int off = (multidrive ? 0 : 3), len = (int)list[i].size() - off;
				const char* line = list[i].c_str(), *lext = + line + list[i].length() - 4;
				if (line[0] == '$') // mountable file system
				{
					bool mounted = (!dbp_disk_eject_state && dbp_images[dbp_disk_image_index] == list[i]);
					int lbllen = (mounted ? sizeof("UNMOUNT") : sizeof("MOUNT"));
					len += lbllen - 1;
					DrawText(mid - len / 2, y, (mounted ? "UNMOUNT " : "MOUNT "), (i == sel ? ATTR_HIGHLIGHT : ATTR_NORMAL));
					DrawText(mid - len / 2 + lbllen, y, line + 1 + off, (i == sel ? ATTR_HIGHLIGHT : ATTR_NORMAL));
				}
				else
				{
					DrawText(mid - len / 2,       y, line + off, (i == sel ? ATTR_HIGHLIGHT : ATTR_NORMAL));
					if (i != sel) continue;
					DrawText(mid - len / 2 - 2,   y, "*", ATTR_WHITE);
					DrawText(mid - len / 2 + len + 1, y, (use_autoboot ? "* [SET AUTO START]" : "*"), ATTR_WHITE);
					autostart_info = use_autoboot;
				}
			}
			if (scroll)
			{
				int y = starty, from = (mid - 10), to = (mid + 10);
				for (Bit16u x = 0; x != CurMode->twidth; x++) DrawText(x, y, (x >= from && x <= to ? "\x1E" : " "), ATTR_NORMAL);
			}
			if (scroll + maxy < count)
			{
				int y = starty + maxy - 1, from = (mid - 10), to = (mid + 10);
				for (Bit16u x = 0; x != CurMode->twidth; x++) DrawText(x, y, (x >= from && x <= to ? "\x1F" : " "), ATTR_NORMAL);
			}

			for (Bit16u x = 0; x != 38; x++) DrawText(x, (Bit16u)CurMode->theight-1, " ", 0);
			if (autostart_info)
			{
				char skiptext[38];
				if (autoskip) snprintf(skiptext, sizeof(skiptext), "Skip showing first %d frames", autoskip);
				else snprintf(skiptext, sizeof(skiptext), "SHIFT/L2/R2 + Restart to come back");
				DrawText((int)1, (Bit16u)CurMode->theight-1, skiptext, ATTR_HEADER);
			}
		}

		static void HandleInput(DBP_Event_Type type, int val, int val2)
		{
			int sel_change = 0, auto_change = 0;
			switch (type)
			{
				case DBPET_KEYDOWN:
					switch ((KBD_KEYS)val)
					{
						case KBD_left:  case KBD_kp4: auto_change--; break;
						case KBD_right: case KBD_kp6: auto_change++; break;
						case KBD_up:    case KBD_kp8: sel_change--; break;
						case KBD_down:  case KBD_kp2: sel_change++; break;
						case KBD_pageup:   sel_change -= 10; break;
						case KBD_pagedown: sel_change += 10; break;
						case KBD_enter: case KBD_kpenter: menu->result = RESULT_LAUNCH;   break;
						case KBD_esc:                     menu->result = RESULT_SHUTDOWN; break;
					}
					break;
				case DBPET_MOUSEXY:
					menu->mousex += val;
					menu->mousey += val2;
					if (abs(menu->mousex) > 1000) { auto_change = (menu->mousex > 0 ? 1 : -1); menu->mousex = menu->mousey = 0; }
					while (menu->mousey < -300) { sel_change--; menu->mousey += 300; menu->mousex = 0; }
					while (menu->mousey >  300) { sel_change++; menu->mousey -= 300; menu->mousex = 0; }
					break;
				case DBPET_MOUSEDOWN:
					if (val == 0) menu->result = RESULT_LAUNCH;   //left
					if (val == 1) menu->result = RESULT_SHUTDOWN; //right
					break;
				case DBPET_JOY1X:
					if (menu->joyy <  16000 && val >=  16000) auto_change++;
					if (menu->joyy > -16000 && val <= -16000) auto_change--;
					menu->joyx = val;
					break;
				case DBPET_JOY1Y:
					if (menu->joyy <  16000 && val >=  16000) sel_change++;
					if (menu->joyy > -16000 && val <= -16000) sel_change--;
					menu->joyy = val;
					break;
				case DBPET_JOY1DOWN:
				case DBPET_JOY2DOWN:
					menu->result = RESULT_LAUNCH;
					break;
				case DBPET_CHANGEMOUNTS:
					menu->RefreshFileList(false);
					menu->RedrawScreen();
					break;
			}
			if (menu->result && (DBP_GetTicks() - menu->open_ticks) < 200U) menu->result = 0; // ignore already pressed buttons when opening
			if (menu->sel >= (int)menu->list.size())
			{
				auto_change = 0;
			}
			else if (menu->result == RESULT_LAUNCH || auto_change)
			{
				const char* line = menu->list[menu->sel].c_str(), *lext = + line + menu->list[menu->sel].length() - 4;
				bool isFS = (line[0] == '$'); // mountable file system
				if (isFS) auto_change = 0;
				if (isFS && menu->result == RESULT_LAUNCH)
				{
					unsigned image_index;
					for (image_index = 0; image_index != (unsigned)dbp_images.size(); image_index++)
						if (dbp_images[image_index] == menu->list[menu->sel])
							break;
					bool was_ejected = dbp_disk_eject_state;
					dbp_disk_eject_state = true;
					if (dbp_disk_mount_letter && Drives[dbp_disk_mount_letter-'A'] && Drives[dbp_disk_mount_letter-'A']->UnMount() == 0)
					{
						Drives[dbp_disk_mount_letter-'A'] = 0;
						mem_writeb(Real2Phys(dos.tables.mediaid)+(dbp_disk_mount_letter-'A')*9,0);
					}
					if (was_ejected || image_index != dbp_disk_image_index)
					{
						dbp_disk_eject_state = false;
						dbp_disk_image_index = image_index;
						DBP_Mount(dbp_images[image_index].c_str(), false, false);
					}
					menu->result = 0;
					menu->RefreshFileList(false);
					menu->RedrawScreen();
				}
			}
			if (!menu->result && (sel_change || auto_change))
			{
				menu->sel += sel_change;
				if (menu->use_autoboot && auto_change > 0) menu->autoskip += (menu->autoskip < 50 ? 10 : (menu->autoskip < 150 ? 25 : (menu->autoskip < 300 ? 50 : 100)));
				if (!menu->use_autoboot && auto_change > 0) menu->use_autoboot = true;
				if (auto_change < 0) menu->autoskip -= (menu->autoskip <= 50 ? 10 : (menu->autoskip <= 150 ? 25 : (menu->autoskip <= 300 ? 50 : 100)));
				if (menu->autoskip < 0) { menu->use_autoboot = false; menu->autoskip = 0; }
				menu->DrawMenu(sel_change);
			}
			if (menu->result == RESULT_LAUNCH)
			{
				if (menu->sel == menu->list.size() + 1) menu->result = RESULT_COMMANDLINE;
				if (menu->sel == menu->list.size() + 2) menu->result = RESULT_SHUTDOWN;
			}
		}

		static void CheckAnyPress(DBP_Event_Type type, int val, int val2)
		{
			switch (type)
			{
				case DBPET_KEYDOWN:
				case DBPET_MOUSEDOWN:
				case DBPET_JOY1DOWN:
				case DBPET_JOY2DOWN:
					if ((DBP_GetTicks() - dbp_lastmenuticks) > 300) menu->result = 1;
					break;
			}
		}

		bool IdleLoop(void(*input_intercept)(DBP_Event_Type, int, int), Bit32u tick_limit = 0)
		{
			DBP_KEYBOARD_ReleaseKeys(); // any unintercepted CALLBACK_* can set a key down
			dbp_gfx_intercept = NULL;
			dbp_input_intercept = input_intercept;
			while (!result && !first_shell->exit)
			{
				CALLBACK_Idle();
				if (tick_limit && DBP_GetTicks() >= tick_limit) first_shell->exit = true;
			}
			dbp_input_intercept = NULL;
			return !first_shell->exit;
		}

		void ClearScreen()
		{
			reg_ax=0x0003;
			CALLBACK_RunRealInt(0x10);
			DBP_KEYBOARD_ReleaseKeys(); // any unintercepted CALLBACK_* can set a key down
		}

		virtual void Run()
		{
			bool on_boot = cmd->FindExist("-BOOT"), on_finish = cmd->FindExist("-FINISH");
			bool always_show_menu = (dbp_menu_time == (char)-1 || (on_finish && (DBP_GetTicks() - dbp_lastmenuticks) < 500));
			dbp_lastmenuticks = DBP_GetTicks();

			RefreshFileList(true);

			#ifndef STATIC_LINKING
			if (on_finish && !always_show_menu && ((exe_count == 1 && fs_count <= 1) || use_autoboot))
			{
				if (dbp_menu_time == 0) { first_shell->exit = true; return; }
				char secs[] = { (char)('0' + dbp_menu_time), '\0' };
				always_show_menu = true;
				dbp_gfx_intercept = NULL;
				dbp_input_intercept = NULL;
				INT10_SetCursorShape(0, 0);
				INT10_SetCursorPos((Bit8u)CurMode->twidth, (Bit8u)CurMode->theight, 0);
				DrawText((Bit16u)(CurMode->twidth / 2 - 33), (Bit16u)(CurMode->theight - 2), "* GAME ENDED - EXITTING IN   SECONDS - PRESS ANY KEY TO CONTINUE *", ATTR_HIGHLIGHT);
				DrawText((Bit16u)(CurMode->twidth / 2 - 33) + 27, (Bit16u)(CurMode->theight - 2), secs, ATTR_HIGHLIGHT);
				if (!IdleLoop(CheckAnyPress, DBP_GetTicks() + (dbp_menu_time * 1000))) return;
				result = 0;
			}
			#endif

			if (on_finish)
			{
				// ran without auto start or only for a very short time (maybe crash), wait for user confirmation
				INT10_SetCursorShape(0, 0);
				INT10_SetCursorPos((Bit8u)CurMode->twidth, (Bit8u)CurMode->theight, 0);
				DrawText((Bit16u)(CurMode->twidth / 2 - 33), (Bit16u)(CurMode->theight - 2), "            * PRESS ANY KEY TO RETURN TO START MENU *             ", ATTR_HIGHLIGHT);
				if (!IdleLoop(CheckAnyPress)) return;
				result = 0;
			}

			if (on_boot && !always_show_menu && ((exe_count == 1 && fs_count <= 1) || use_autoboot))
				result = RESULT_LAUNCH;

			if (on_boot && list.size() == 0 && !Drives['C'-'A'] && !Drives['A'-'A'] && !Drives['D'-'A'])
				result = RESULT_COMMANDLINE;

			if (!result)
			{
				RedrawScreen();
				if (!IdleLoop(HandleInput)) return;
				ClearScreen();
			}

			if (have_autoboot && !use_autoboot)
			{
				Drives['C'-'A']->FileUnlink((char*)"AUTOBOOT.DBP");
			}

			if (result == RESULT_LAUNCH)
			{
				if (use_autoboot && (!have_autoboot || init_autosel != sel || init_autoskip != autoskip))
				{
					char autostr[DOS_PATHLENGTH + 32];
					char* autoend = autostr + snprintf(autostr, sizeof(autostr), "%s", list[sel].c_str());
					if (autoskip) autoend += snprintf(autoend, sizeof(autostr), "\r\n%d", autoskip);
					Bit16u auto_length = (Bit16u)(autoend - autostr);
					DOS_File *autobootfile;
					Drives['C'-'A']->FileCreate(&autobootfile, (char*)"AUTOBOOT.DBP", DOS_ATTR_ARCHIVE);
					autobootfile->AddRef();
					autobootfile->Write((Bit8u*)autostr, &auto_length);
					autobootfile->Close();
					delete autobootfile;
					DBP_ASSERT(auto_length == (Bit16u)(autoend - autostr));
				}

				if (autoskip)
				{
					DBP_UnlockSpeed(true, autoskip);
					render.updating = false; //avoid immediate call to GFX_EndUpdate
					dbp_state = DBPSTATE_FIRST_FRAME;
				}

				if (first_shell->bf) delete first_shell->bf;
				first_shell->bf = new FakeBatch(list[sel]);
			}
			else if (result == RESULT_SHUTDOWN)
				first_shell->exit = true;
			else if (result == RESULT_COMMANDLINE)
			{
				if (Drives['C'-'A']) DOS_SetDrive('C'-'A');
				WriteOut("Type 'PUREMENU' to return to the start menu\n");
			}
			dbp_lastmenuticks = DBP_GetTicks();
		}
	};
	*make = menu = new Menu;
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
			char drive1 = (      p1[0] && p1[1] == ':' ? (p1[0] >= 'A' && p1[0] <= 'Z' ? p1[0] : (p1[0] >= 'a' && p1[0] <= 'z' ? p1[0]-0x20 : 0)) : 0);
			char drive2 = (p2 && p2[1] && p2[2] == ':' ? (p2[1] >= 'A' && p2[1] <= 'Z' ? p2[1] : (p2[1] >= 'a' && p2[1] <= 'z' ? p2[1]-0x20 : 0)) : 0);
			if (!drive1) { WriteOut("Usage: REMOUNT [olddrive:] [newdrive:]\n"); return; }
			if (!drive2) { drive2 = drive1; drive1 = DOS_GetDefaultDrive()+'A'; }
			if (!Drives[drive1-'A']) { WriteOut("Drive %c: does not exist\n", drive1); return; }
			if (Drives[drive2-'A']) { WriteOut("Drive %c: already exists\n", drive2); return; }
			WriteOut("Remounting %c: to %c:\n", drive1, drive2);
			if (drive1 != dbp_disk_mount_letter)
			{
				if (MSCDEX_HasDrive(drive1)) MSCDEX_RemoveDrive(drive1);
				if (imageDiskList[drive1-'A']) imageDiskList[drive1-'A'] = NULL;

				DOS_Drive* drive = Drives[drive1-'A'];
				Drives[drive1-'A'] = NULL;
				Drives[drive2-'A'] = drive;
				mem_writeb(Real2Phys(dos.tables.mediaid) + (drive2-'A') * 9, (drive2 > 'B' ? 0xF8 : 0xF0));

				if (drive2 > 'C')
				{
					Bit8u subUnit;
					MSCDEX_AddDrive(drive2, "", subUnit);
				}
			}
			else if (!dbp_disk_eject_state && dbp_disk_image_index < dbp_images.size())
			{
				Drives[drive1-'A']->UnMount();
				Drives[drive1-'A'] = 0;
				dbp_disk_mount_letter = drive2;
				DBP_Mount(dbp_images[dbp_disk_image_index].c_str(), false, false);
			}
			if (drive1 == DOS_GetDefaultDrive()) DOS_SetDrive(drive2-'A');
			for (std::string& img : dbp_images) if (img[0] == '$' && img[1] == drive1) img[1] = drive2;
		}
	};
	*make = new RemountProgram;
}

static void DBP_StartOnScreenKeyboard()
{
	static struct
	{
		float mx, my, dx, dy, jx, jy, kx, ky, mspeed;
		Bit32u pressed_time;
		KBD_KEYS hovered_key, pressed_key;
		bool held[KBD_LAST];
	} osk;
	struct OSKFunc
	{
		enum { KWR = 10, KWTAB = 15, KWCAPS = 20, KWLS = 17, KWRSHIFT = 33, KWCTRL = 16, KWZERO = 22, KWBS = 28, KWSPACEBAR = 88, KWENTR = 18, KWPLUS };
		enum { KXX = 100+KWR+2, SPACEFF = 109, KSPLIT = 255, KSPLIT1 = 192, KSPLIT2 = 234, KWIDTH = KSPLIT2 + KWR*4 + 2*3 };
		static void gfx(DBP_Buffer& buf)
		{
			static const uint8_t keyboard_rows[6][25] = 
			{
				{ KWR, KXX ,KWR,KWR,KWR,KWR,   SPACEFF,   KWR,KWR,KWR,KWR,   SPACEFF,   KWR,KWR,KWR,KWR , KSPLIT , KWR,KWR,KWR },
				{ KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,     KWBS , KSPLIT , KWR,KWR,KWR , KSPLIT , KWR,KWR,KWR,KWR    },
				{ KWTAB, KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWENTR , KSPLIT , KWR,KWR,KWR , KSPLIT , KWR,KWR,KWR,KWPLUS },
				{ KWCAPS, KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,          KSPLIT        ,        KSPLIT , KWR,KWR,KWR        },
				{ KWLS, KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,       KWRSHIFT , KSPLIT , KXX,KWR,KXX , KSPLIT , KWR,KWR,KWR,KWPLUS },
				{ KWCTRL, KXX, KWCTRL,                  KWSPACEBAR,                 KWCTRL, KXX, KWCTRL , KSPLIT , KWR,KWR,KWR , KSPLIT , KWZERO ,KWR        },
			};
			static const KBD_KEYS keyboard_keys[6][25] =
			{
				{ KBD_esc,KBD_NONE,KBD_f1,KBD_f2,KBD_f3,KBD_f4,KBD_NONE,KBD_f5,KBD_f6,KBD_f7,KBD_f8,KBD_NONE,KBD_f9,KBD_f10,KBD_f11,KBD_f12,KBD_NONE,KBD_printscreen,KBD_scrolllock,KBD_pause },
				{ KBD_grave, KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7, KBD_8, KBD_9, KBD_0, KBD_minus, KBD_equals,    KBD_backspace ,KBD_NONE,KBD_insert,KBD_home,KBD_pageup ,KBD_NONE,KBD_numlock,KBD_kpdivide,KBD_kpmultiply,KBD_kpminus },
				{ KBD_tab,KBD_q,KBD_w,KBD_e,KBD_r,KBD_t,KBD_y,KBD_u,KBD_i,KBD_o,KBD_p,KBD_leftbracket,KBD_rightbracket,          KBD_enter ,KBD_NONE,KBD_delete,KBD_end,KBD_pagedown,KBD_NONE,KBD_kp7,KBD_kp8,KBD_kp9,KBD_kpplus },
				{ KBD_capslock,KBD_a,KBD_s,KBD_d,KBD_f,KBD_g,KBD_h,KBD_j,KBD_k,KBD_l,KBD_semicolon,KBD_quote,KBD_backslash                 ,KBD_NONE               ,                 KBD_NONE,KBD_kp4,KBD_kp5,KBD_kp6 },
				{ KBD_leftshift,KBD_extra_lt_gt,KBD_z,KBD_x,KBD_c,KBD_v,KBD_b,KBD_n,KBD_m,KBD_comma,KBD_period,KBD_slash,KBD_rightshift    ,KBD_NONE,   KBD_NONE,KBD_up,KBD_NONE    ,KBD_NONE,KBD_kp1,KBD_kp2,KBD_kp3,KBD_kpenter },
				{ KBD_leftctrl,KBD_NONE,KBD_leftalt,                        KBD_space,                 KBD_rightalt,KBD_NONE,KBD_rightctrl ,KBD_NONE,  KBD_left,KBD_down,KBD_right  ,KBD_NONE,KBD_kp0,KBD_kpperiod },
			};

			int thickness = (buf.width < (KWIDTH + 10) ? 1 : ((int)buf.width - 10) / KWIDTH);
			float ar = (float)buf.width / buf.height;
			float fx = (buf.width < KWIDTH ? (buf.width - 10) / (float)KWIDTH : (float)thickness);
			float fy = fx * ar * buf.height / buf.width;
			int thicknessy = (int)(thickness * (fy / fx + .4f)); if (thicknessy < 1) thicknessy = 1;
			int oskx = (int)(buf.width / fx / 2) - (KWIDTH / 2);
			int osky = (osk.my && osk.my < (buf.height / fy / 2) ? 3 : (int)(buf.height / fy) - 3 - 65);

			if (!osk.mspeed)
			{
				osk.mx = (float)(KWIDTH/2);
				osk.my = (float)(osky + 32);
				osk.mspeed = 2.f;
			}

			if (osk.dx) { osk.mx += osk.dx; osk.dx = 0; }
			if (osk.dy) { osk.my += osk.dy; osk.dy = 0; }
			osk.mx += (osk.jx + osk.kx) * osk.mspeed;
			osk.my += (osk.jy + osk.ky) * osk.mspeed;
			if (osk.mx < 0)      osk.mx = 0;
			if (osk.mx > KWIDTH) osk.mx = KWIDTH;
			if (osk.my <                     3) osk.my = (float)(                    3);
			if (osk.my > (buf.height / fy) - 3) osk.my = (float)((buf.height / fy) - 3);
			int cX = (int)((oskx+osk.mx)*fx); // mx is related to oskx
			int cY = (int)((     osk.my)*fy); // my is related to screen!

			if (osk.pressed_key && (DBP_GetTicks() - osk.pressed_time) > 500)
			{
				osk.held[osk.pressed_key] = true;
				osk.pressed_key = KBD_NONE;
			}

			// Draw keys and check hovered state
			osk.hovered_key = KBD_NONE;
			for (int row = 0; row != 6; row++)
			{
				int x = 0, y = (row ? 3 + (row * 10) : 0);
				for (const uint8_t *k = keyboard_rows[row], *k_end = k + 25; k != k_end; k++)
				{
					int draww = *k, drawh = 8;
					switch (*k)
					{
						case KWENTR:
							x += 5;
							drawh = 18;
							break;
						case KWPLUS:
							draww = KWR;
							drawh = 18;
							break;
						case KXX:case SPACEFF:
							x += (*k - 100);
							continue;
						case KSPLIT:
							x = (x < KSPLIT1 ? KSPLIT1 : KSPLIT2);
							continue;
						case 0: continue;
						default: break;
					}

					DBP_ASSERT(draww);
					int rl = (int)((oskx + x) * fx), rr = (int)((oskx + x + draww) * fx), rt = (int)((osky + y) * fy), rb = (int)((osky + y + drawh) * fy);
					bool hovered = (cX >= rl && cX < rr && cY >= rt && cY < rb);

					KBD_KEYS kbd_key = keyboard_keys[row][k - keyboard_rows[row]];
					if (hovered) osk.hovered_key = kbd_key;

					Bit32u col = (osk.pressed_key == kbd_key ? 0x808888FF : 
							(osk.held[kbd_key] ? 0x80A0A000 :
							(hovered ? 0x800000FF :
							0x80FF0000)));

					for (int ky = rt; ky != rb; ky++)
						for (int kx = rl; kx != rr; kx++)
							AlphaBlend(buf.video + ky * buf.width + kx, col);

					for (int kx = rl-1; kx <= rr; kx++)
					{
						AlphaBlend(buf.video + (rt-1) * buf.width + kx, 0xA0000000);
						AlphaBlend(buf.video + (rb  ) * buf.width + kx, 0xA0000000);
					}
					for (int ky = rt; ky != rb; ky++)
					{
						AlphaBlend(buf.video + ky * buf.width + (rl-1), 0x80000000);
						AlphaBlend(buf.video + ky * buf.width + (rr  ), 0x80000000);
					}

					x += (draww + 2);
				}
			}

			// Draw key letters
			static Bit32u keyboard_letters[] = { 3154116614U,3773697760U,3285975058U,432266297U,1971812352U,7701880U,235069918U,0,2986344448U,545521665U,304153104U,71320576U,2376196U,139756808U,1375749253U,15335968U,0,9830400U,148945920U,2023662U,471712220U,2013272514U,2239255943U,3789422661U,69122U,0,45568U,33824900U,67112993U,1090790466U,2215116836U,612698196U,42009088U,482U,0,2214592674U,553779744U,1107558416U,608207908U,1417938944U,1344776U,570589442U,1U,3053453312U,545521665U,270590494U,406963200U,1589316U,141854472U,3254809733U,31596257U,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3670272U,125847559U,805798000U,3934080U,14680064U,33555392U,19792160U,2149581128U,9961472U,134234113U,134250568U,2152203264U,9220U,1171071088U,563740792U,1476471297U,44048385U,16802816U,2013724704U,3670912U,125841412U,229412U,1271156960U,31500800U,23593262U,234995728U,268500992U,4196352U,33572868U,604241992U,544210944U,8000605U,572334506U,268519425U,320U,524544U,67125256U,1208025160U,2360320U,1428160512U,704645644U,19010849U,537395528U,2U,117471233U,805535808U,2150629504U,15367U,3588022272U,564789259U,1208009245U,2055U,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3759984128U,3936512U,285339655U,1610875024U,7343872U,14U,14747136U,31457518U,122892U,17835300U,150995985U,2417033280U,9438208U,134221833U,0,705569130U,302055425U,603980064U,285282333U,1074200636U,9439744U,251695108U,524304U,704643072U,19796705U,3758883072U,635699201U,68485456U,4196608U,67145732U,268501136U,2048U,560594944U,2147557906U,16781824U,2418353152U,267520U,67125257U,2416181392U,1048832U,33783816U,304163328U,4194594U,65554U,23076132U,151010314U,1610874944U,6292480U,234909697U,6436992U,3792888166U,201334784U,480U,0,0,0,0,2147483648U,10059U,0,0,1024U,0,0,0,0,1216348160U,34U,0,0,14U,0,0,0,0,575176704U,0,0,67108864U,0,0,0,0,0,2902912U,0,0,201326592U,3758489600U,31459073U,503390236U,65608U,2098176U,0,0,3225157920U,2043805697U,2099463U,33562633U,672137504U,256U,8196U,0,536870912U,2098177U,21627392U,151117833U,3759800800U,1576961U,8193U,64U,0,31457280U,57372U,252148674U,537460992U,18878976U,16787472U,1073741824U,0,0,536936448U,1375732000U,590858U,2099457U,302063634U,536936520U,8388608U,0,0,538968320U,197918721U,31459591U,201334791U,1208746272U,2100992U,32768U,0,0,12590081U,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2152730112U,35666944U,469901336U,4457024U,33554432U,6063584U,16777216U,4194304U,1057021966U,4213282U,604119072U,3223585312U,27650U,537001984U,16900U,229376U,268451840U,774439168U,268443864U,537133376U,54533122U,84U,3185574144U,238U,1984U,3758620736U,1256728832U,2148008000U,35652608U,1140998180U,0,1118109697U,0,1073741825U,16778240U,2152376320U,20972544U,604061732U,2151940672U,8390656U,4367616U,16777216U,4194304U,520159234U,35502U,402792508U,1075576960U,8406018U,3758129152U,98981U,65536U,503332864U,1048800U,0,0,0,0,0,0,0,0,4096U,0,0,0,0,0,0,0,0,15U,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1275592704U,1275068480U,2U,0,0,9408U,16460U,256U,61440U,1480736256U,38928384U,0,0,536870912U,1107296293U,1048664U,8193U,144U,4514313U,479744U,0,0,1965031424U,1155661824U,16783360U,2415919200U,16777216U,17474U,606U,0,0,2482176U,4473344U,4228366588U,9437184U,1107296256U,1375731780U,2U,0,0,9504U,402670658U,6292352U,36864U,1166810880U,206700544U,0,0,536870912U,2348810437U,1048645U,8193U,4194544U,16U };
			for (Bit32u p = 0; p != 59*280; p++)
			{
				if (!(keyboard_letters[p>>5] & (1<<(p&31)))) continue;
				int lx = (int)((oskx + (p%280)) * fx), ly = (int)((osky + 1 + (p/280)) * fy);
				for (int y = ly; y != ly + thicknessy; y++)
					for (int x = lx; x != lx + thickness; x++)
						*(buf.video + y * buf.width + x) = 0xFFFFFFFF;
			}

			// Draw white mouse cursor with black outline
			for (Bit32u i = 0; i != 9; i++)
			{
				Bit32u n = (i < 4 ? i : (i < 8 ? i+1 : 4)), x = (Bit32u)cX + (n%3)-1, y = (Bit32u)cY + (n/3)-1, ccol = (n == 4 ? 0xFFFFFFFF : 0xFF000000);
				for (Bit32u c = 0; c != (Bit32u)(8*fx); c++)
				{
					*(buf.video + (y+c) * buf.width + (x  )) = ccol;
					if (x+c >= buf.width) continue;
					*(buf.video + (y  ) * buf.width + (x+c)) = ccol;
					*(buf.video + (y+c) * buf.width + (x+c)) = ccol;
				}
			}
		}

		static void AlphaBlend(Bit32u* p1, Bit32u p2)
		{
			Bit32u a = (p2 & 0xFF000000) >> 24, na = 255 - a;
			Bit32u rb = ((na * (*p1 & 0x00FF00FF)) + (a * (p2 & 0x00FF00FF))) >> 8;
			Bit32u ag = (na * ((*p1 & 0xFF00FF00) >> 8)) + (a * (0x01000000 | ((p2 & 0x0000FF00) >> 8)));
			*p1 = ((rb & 0x00FF00FF) | (ag & 0xFF00FF00));
		}

		static void input(DBP_Event_Type type, int val, int val2)
		{
			switch (type)
			{
				case DBPET_MOUSEXY: osk.dx += val/2.f; osk.dy += val2/2.f; break;
				case DBPET_MOUSEDOWN: case DBPET_JOY1DOWN: case DBPET_JOY2DOWN: case_ADDKEYDOWN:
					if (osk.pressed_key == KBD_NONE && osk.hovered_key != KBD_NONE)
					{
						if (osk.held[osk.hovered_key])
						{
							osk.held[osk.hovered_key] = false;
							KEYBOARD_AddKey(osk.hovered_key, false);
						}
						else if (osk.hovered_key >= KBD_leftalt && osk.hovered_key <= KBD_rightshift)
						{
							osk.held[osk.hovered_key] = true;
							KEYBOARD_AddKey(osk.hovered_key, true);
						}
						else
						{
							osk.pressed_time = DBP_GetTicks();
							osk.pressed_key = osk.hovered_key;
							KEYBOARD_AddKey(osk.pressed_key, true);
						}
					}
					break;
				case DBPET_MOUSEUP: case DBPET_JOY1UP: case DBPET_JOY2UP: case_ADDKEYUP:
					if (osk.pressed_key != KBD_NONE)
					{
						KEYBOARD_AddKey(osk.pressed_key, false);
						osk.pressed_key = KBD_NONE;
					}
					break;
				case DBPET_KEYDOWN:
					switch ((KBD_KEYS)val)
					{
						case KBD_left:  case KBD_kp4: osk.kx = -1; break;
						case KBD_right: case KBD_kp6: osk.kx =  1; break;
						case KBD_up:    case KBD_kp8: osk.ky = -1; break;
						case KBD_down:  case KBD_kp2: osk.ky =  1; break;
						case KBD_enter: case KBD_kpenter: case KBD_space: goto case_ADDKEYDOWN;
					}
					break;
				case DBPET_KEYUP:
					switch ((KBD_KEYS)val)
					{
						case KBD_left:  case KBD_kp4: case KBD_right: case KBD_kp6: osk.kx = 0; break;
						case KBD_up:    case KBD_kp8: case KBD_down:  case KBD_kp2: osk.ky = 0; break;
						case KBD_enter: case KBD_kpenter: case KBD_space: goto case_ADDKEYUP;
						case KBD_esc: goto case_CLOSEOSK;
					}
				case DBPET_JOY1X: case DBPET_JOY2X: case DBPET_JOYMX: osk.jx = DBP_GET_JOY_ANALOG_VALUE(val); break;
				case DBPET_JOY1Y: case DBPET_JOY2Y: case DBPET_JOYMY: osk.jy = DBP_GET_JOY_ANALOG_VALUE(val); break;
				case DBPET_MOUSESETSPEED: osk.mspeed = (val > 0 ? 4.f : 1.f); break;
				case DBPET_MOUSERESETSPEED: osk.mspeed = 2.f; break;
				case DBPET_ONSCREENKEYBOARD: case_CLOSEOSK:
					DBP_KEYBOARD_ReleaseKeys();
					dbp_gfx_intercept = NULL;
					dbp_input_intercept = NULL;
					break;
			}
		}
	};

	DBP_KEYBOARD_ReleaseKeys();
	//memset(&osk, 0, sizeof(osk));
	dbp_gfx_intercept = OSKFunc::gfx;
	dbp_input_intercept = OSKFunc::input;
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
	info->library_version  = "0.18";
	info->need_fullpath    = true;
	info->block_extract    = true;
	info->valid_extensions = "zip|dosz|exe|com|bat|iso|cue|ins|img|ima|vhd|m3u|m3u8";
}

void retro_set_environment(retro_environment_t cb) //#2
{
	environ_cb = cb;

	bool allow_no_game = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &allow_no_game);
}

static void refresh_input_binds(unsigned refresh_min_port = 0)
{
	if (refresh_min_port < 2)
	{
		dbp_input_binds.clear();
		if (dbp_mouse_input)
		{
			dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   NULL, DBPET_MOUSEDOWN, 0 });
			dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  NULL, DBPET_MOUSEDOWN, 1 });
			dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, NULL, DBPET_MOUSEDOWN, 2 });
			if (dbp_bind_mousewheel)
			{
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,   NULL, DBPET_KEYDOWN, DBP_KEYAXIS_GET(-1, dbp_bind_mousewheel) });
				dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN, NULL, DBPET_KEYDOWN, DBP_KEYAXIS_GET( 1, dbp_bind_mousewheel) });
			}
		}
		refresh_min_port  = 0;
	}
	else
	{
		size_t i;
		for (i = 0; i != dbp_input_binds.size(); i++) if (dbp_input_binds[i].port >= refresh_min_port) break;
		if (i < dbp_input_binds.size()) dbp_input_binds.erase(dbp_input_binds.begin() + i, dbp_input_binds.end());
	}

	static const DBP_InputBind BindsMouseLeftAnalog[] = {
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Mouse Horizontal", DBPET_JOYMX },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse Vertical",   DBPET_JOYMY },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Left Mouse Button",   DBPET_MOUSEDOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Right Mouse Button",  DBPET_MOUSEDOWN, 1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Middle Mouse Button", DBPET_MOUSEDOWN, 2 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Speed Up Mouse",     DBPET_MOUSESETSPEED,  1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Slow Down Mouse",    DBPET_MOUSESETSPEED, -1 },
		{ 0 }};
	static const DBP_InputBind BindsMouseRightAnalog[] = {
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Mouse Horizontal", DBPET_JOYMX },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse Vertical",   DBPET_JOYMY },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,  "Left Mouse Button",   DBPET_MOUSEDOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,  "Right Mouse Button",  DBPET_MOUSEDOWN, 1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,  "Middle Mouse Button", DBPET_MOUSEDOWN, 2 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Speed Up Mouse",      DBPET_MOUSESETSPEED,  1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Slow Down Mouse",     DBPET_MOUSESETSPEED, -1 },
		{ 0 }};
	static const DBP_InputBind BindsGravisGamepad[] = {
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up",    DBPET_JOY1Y, -1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down",  DBPET_JOY1Y,  1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left",  DBPET_JOY1X, -1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right", DBPET_JOY1X,  1 },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Axis Horizontal", DBPET_JOY1X },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Axis Vertical",   DBPET_JOY1Y },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Red Button (1)",    DBPET_JOY1DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Blue Button (2)",   DBPET_JOY1DOWN, 1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Yellow Button (3)", DBPET_JOY2DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Green Button (4)",  DBPET_JOY2DOWN, 1 },
		{ 0 }};
	static const DBP_InputBind BindsBasicJoystick1[] = {
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up",    DBPET_JOY1Y, -1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down",  DBPET_JOY1Y,  1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left",  DBPET_JOY1X, -1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right", DBPET_JOY1X,  1 },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Stick Horizontal", DBPET_JOY1X },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Stick Vertical",   DBPET_JOY1Y },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Button 1", DBPET_JOY1DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Button 2", DBPET_JOY1DOWN, 1 },
		{ 0 }};
	static const DBP_InputBind BindsBasicJoystick2[] = {
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up",    DBPET_JOY2Y, -1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down",  DBPET_JOY2Y,  1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left",  DBPET_JOY2X, -1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right", DBPET_JOY2X,  1 },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Stick Horizontal", DBPET_JOY2X },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Stick Vertical",   DBPET_JOY2Y },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Button 1", DBPET_JOY2DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Button 2", DBPET_JOY2DOWN, 1 },
		{ 0 }};
	static const DBP_InputBind BindsThrustMasterFlightStick[] = {
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up",    DBPET_JOYHATSETBIT, 8 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down",  DBPET_JOYHATSETBIT, 2 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left",  DBPET_JOYHATSETBIT, 1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right", DBPET_JOYHATSETBIT, 4 },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Stick Horizontal", DBPET_JOY1X },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Stick Vertical",   DBPET_JOY1Y },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Rudder",           DBPET_JOY2X },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Button 1", DBPET_JOY1DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Button 2", DBPET_JOY1DOWN, 1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Button 3", DBPET_JOY2DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Button 4", DBPET_JOY2DOWN, 1 },
		{ 0 }};
	static const DBP_InputBind BindsBothDOSJoysticks[] = {
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Stick 1 Horizontal", DBPET_JOY1X },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Stick 1 Vertical",   DBPET_JOY1Y },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Stick 2 Horizontal", DBPET_JOY2X },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Stick 2 Vertical",   DBPET_JOY2Y },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Button 1", DBPET_JOY1DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Button 2", DBPET_JOY1DOWN, 1 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Button 3", DBPET_JOY2DOWN, 0 },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Button 4", DBPET_JOY2DOWN, 1 },
		{ 0 }};

	for (uint8_t port = refresh_min_port; port != DBP_MAX_PORTS; port++)
	{
		const DBP_InputBind* binds = NULL;
		size_t port_bind_begin = dbp_input_binds.size();
		switch (dbp_port_devices[port])
		{
			case DBP_DEVICE_Disabled:
				continue;
			case DBP_DEVICE_MouseLeftAnalog:
			case DBP_DEVICE_KeyboardMouseLeftStick:
				binds = BindsMouseLeftAnalog;
				break;
			case DBP_DEVICE_MouseRightAnalog:
			case DBP_DEVICE_KeyboardMouseRightStick:
				binds = BindsMouseRightAnalog;
				break;
			case DBP_DEVICE_DefaultJoypad:
				if (port == 0)
				{
					if (dbp_auto_mapping)
					{
						const Bit8u count = *dbp_auto_mapping, *p = dbp_auto_mapping + 1;
						static std::vector<std::string> name_buffers;
						name_buffers.resize(count);
						for (Bit8u num = 0; num != count; num++)
						{
							std::string& name = name_buffers[num];
							name.clear();
							const char *desc_key = NULL, *desc_key2 = NULL, *desc_device = NULL;
							size_t first_input_bind = dbp_input_binds.size();

							DBP_InputBind bnd = { 0, RETRO_DEVICE_JOYPAD, 0, *(p++) };
							bool hasActionName = (bnd.id >= 128);
							if (hasActionName)
							{
								bnd.id &= 127;
								Bit32u name_offset = 0;
								do { name_offset = (name_offset<<7)|(*p&127); } while (*(p++) & 128);
								name = dbp_auto_mapping_names + name_offset;
								name += ' '; name += '(';
							}
							DBP_ASSERT(bnd.id <= 19);
							bool isAnalog = (bnd.id >= 16);
							if (isAnalog)
							{
								bnd.device = RETRO_DEVICE_ANALOG;
								bnd.index  = (bnd.id >= 18 ? RETRO_DEVICE_INDEX_ANALOG_RIGHT : RETRO_DEVICE_INDEX_ANALOG_LEFT);
								bnd.id     = (bnd.id & 1 ? RETRO_DEVICE_ID_ANALOG_X : RETRO_DEVICE_ID_ANALOG_Y);
							}
							for (Bit8u more_keys = 1; more_keys;)
							{
								Bit8u key = (*p & 127), key2 = (isAnalog ? (*(++p) & 127) : 0);
								more_keys = (*(p++) & 128);
								if (isAnalog)
								{
									if (key > KBD_LAST)
									{
										bnd.evt = (key == 114 || key == 115 ? DBPET_JOY1Y : DBPET_JOY1X);
										bnd.meta = 0;
										desc_key = (bnd.evt == DBPET_JOY1Y ? "Up/Down" : "Left/Right");
									}
									else
									{
										bnd.evt = DBPET_AXIS_TO_KEY;
										bnd.meta = (int16_t)DBP_KEYAXIS_MAKE(key, key2);
										desc_key = DBP_KBDNAMES[key];
										desc_key2 = DBP_KBDNAMES[key2];
									}
								}
								else switch (key)
								{
									case 110: bnd.evt = DBPET_JOY1DOWN;  bnd.meta =  0; desc_key = "Button 1"; break; //Joy 1 Btn 1
									case 111: bnd.evt = DBPET_JOY1DOWN;  bnd.meta =  1; desc_key = "Button 2"; break; //Joy 1 Btn 2
									case 112: bnd.evt = DBPET_JOY2DOWN;  bnd.meta =  0; desc_key = "Button 3"; break; //Joy 2 Btn 1
									case 113: bnd.evt = DBPET_JOY2DOWN;  bnd.meta =  1; desc_key = "Button 4"; break; //Joy 2 Btn 2
									case 114: bnd.evt = DBPET_JOY1Y;     bnd.meta = -1; desc_key = "Up";       break; //Joy 1 Up
									case 115: bnd.evt = DBPET_JOY1Y;     bnd.meta =  1; desc_key = "Down";     break; //Joy 1 Down
									case 116: bnd.evt = DBPET_JOY1X;     bnd.meta = -1; desc_key = "Left";     break; //Joy 1 Left
									case 117: bnd.evt = DBPET_JOY1X;     bnd.meta =  1; desc_key = "Right";    break; //Joy 1 Right
									case 118: bnd.evt = DBPET_MOUSEDOWN; bnd.meta =  0; desc_key = "Left";     break; //Mouse Left
									case 119: bnd.evt = DBPET_MOUSEDOWN; bnd.meta =  1; desc_key = "Right";    break; //Mouse Right
									case 120: bnd.evt = DBPET_MOUSEDOWN; bnd.meta =  2; desc_key = "Middle";   break; //Mouse Middle
									default:  bnd.evt = DBPET_KEYDOWN; bnd.meta = key; desc_key = DBP_KBDNAMES[key]; break; //Key
								}
								dbp_input_binds.push_back(bnd);

								const char* dev = (bnd.evt == DBPET_KEYDOWN || bnd.evt == DBPET_AXIS_TO_KEY ? "Keyboard" : (bnd.evt == DBPET_MOUSEDOWN ? "Mouse" : "Joystick"));
								if (desc_device != dev) { name += (desc_device = dev); name += ' '; }
								name += desc_key;
								if (desc_key2) { name += '/'; name += desc_key2; }
								if (more_keys) name += '+';
							}
							if (hasActionName) name += ')';
							dbp_input_binds[first_input_bind].desc = name.c_str();
						}
					}
					else
						binds = BindsGravisGamepad;
				}
				else if (port == 1)
					binds = BindsBasicJoystick2;
				else
					continue;
				break;
			case DBP_DEVICE_GravisGamepad:
				binds = BindsGravisGamepad;
				break;
			case DBP_DEVICE_BasicJoystick1:
				binds = BindsBasicJoystick1;
				break;
			case DBP_DEVICE_BasicJoystick2:
				binds = BindsBasicJoystick2;
				break;
			case DBP_DEVICE_ThrustMasterFlightStick:
				binds = BindsThrustMasterFlightStick;
				break;
			case DBP_DEVICE_BothDOSJoysticks:
				binds = BindsBothDOSJoysticks;
				break;
			case DBP_DEVICE_BindCustomKeyboard:
			case DBP_DEVICE_BindGenericKeyboard:
			default:
				break;
		}

		for (; binds && binds->port; binds++)
		{
			dbp_input_binds.push_back(*binds);
			dbp_input_binds.back().port = port;
		}

		if (dbp_on_screen_keyboard && port == 0)
		{
			dbp_input_binds.push_back({ port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "On Screen Keyboard", DBPET_ONSCREENKEYBOARD });
		}

		if ((dbp_port_devices[port] & RETRO_DEVICE_MASK) == RETRO_DEVICE_KEYBOARD)
			continue;

		if (port == 0 && dbp_auto_mapping && dbp_port_devices[0] == DBP_DEVICE_DefaultJoypad)
			continue;

		if (!dbp_bind_unused && dbp_port_devices[port] != DBP_DEVICE_BindGenericKeyboard)
			continue;

		int abp = (port % 4); //auto bind series
		static const struct { uint8_t id; struct { int16_t meta; const char* name; } ports[4]; } auto_buttons[] =
		{
			#define ABK(key, name) {(int16_t)KBD_##key, "Keyboard " #name}
			#define ABKP(key, name) {(int16_t)KBD_kp##key, "Numpad " #name}
			{ RETRO_DEVICE_ID_JOYPAD_UP,     { ABK(up       ,Up        ), ABKP(8,       8       ), ABK(q,Q), ABK(backspace,   Backspace    ) } },
			{ RETRO_DEVICE_ID_JOYPAD_DOWN,   { ABK(down     ,Down      ), ABKP(2,       2       ), ABK(a,A), ABK(backslash,   Backslash    ) } },
			{ RETRO_DEVICE_ID_JOYPAD_LEFT,   { ABK(left     ,Left      ), ABKP(4,       4       ), ABK(z,Z), ABK(semicolon,   Semicolon    ) } },
			{ RETRO_DEVICE_ID_JOYPAD_RIGHT,  { ABK(right    ,Right     ), ABKP(6,       6       ), ABK(x,X), ABK(quote,       Quote        ) } },
			{ RETRO_DEVICE_ID_JOYPAD_SELECT, { ABK(esc      ,Escape    ), ABKP(period,  Period  ), ABK(g,G), ABK(o,           O            ) } },
			{ RETRO_DEVICE_ID_JOYPAD_START,  { ABK(enter    ,Enter     ), ABKP(enter,   Enter   ), ABK(h,H), ABK(p,           P            ) } },
			{ RETRO_DEVICE_ID_JOYPAD_X,      { ABK(space    ,Space     ), ABKP(5,       5       ), ABK(d,D), ABK(slash,       Slash        ) } },
			{ RETRO_DEVICE_ID_JOYPAD_Y,      { ABK(leftshift,Left Shift), ABKP(1,       1       ), ABK(f,F), ABK(rightshift,  Right Shift  ) } },
			{ RETRO_DEVICE_ID_JOYPAD_B,      { ABK(leftctrl ,Left Ctrl ), ABKP(0,       0       ), ABK(c,C), ABK(rightctrl,   Right Ctrl   ) } },
			{ RETRO_DEVICE_ID_JOYPAD_A,      { ABK(leftalt  ,Left Alt  ), ABKP(3,       3       ), ABK(s,S), ABK(rightalt,    Right Alt    ) } },
			{ RETRO_DEVICE_ID_JOYPAD_L,      { ABK(1        ,1         ), ABKP(7,       7       ), ABK(w,W), ABK(leftbracket, Left Bracket ) } },
			{ RETRO_DEVICE_ID_JOYPAD_R,      { ABK(2        ,2         ), ABKP(9,       9       ), ABK(e,E), ABK(rightbracket,Right Bracket) } },
			{ RETRO_DEVICE_ID_JOYPAD_L2,     { ABK(3        ,3         ), ABKP(minus,   Minus   ), ABK(r,R), ABK(comma,       Comma        ) } },
			{ RETRO_DEVICE_ID_JOYPAD_R2,     { ABK(4        ,4         ), ABKP(plus,    Plus    ), ABK(t,T), ABK(period,      Period       ) } },
			{ RETRO_DEVICE_ID_JOYPAD_L3,     { ABK(f1       ,F1        ), ABKP(divide,  Divide  ), ABK(v,V), ABK(minus,       Minus        ) } },
			{ RETRO_DEVICE_ID_JOYPAD_R3,     { ABK(f2       ,F2        ), ABKP(multiply,Multiply), ABK(b,B), ABK(equals,      Equals       ) } },
			#undef ABK
			#undef ABKP
		};
		static const struct { uint8_t index, id; struct { int16_t meta; const char* name; } ports[4]; } auto_analogs[] =
		{
			#define ABK(key1, key2, name) {(int16_t)DBP_KEYAXIS_MAKE(KBD_##key1, KBD_##key2), "Keyboard " #name}
			#define ABKP(key1, key2, name) {(int16_t)DBP_KEYAXIS_MAKE(KBD_kp##key1, KBD_kp##key2), "Numpad " #name}
			{ RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, { ABK(left,  right,   Left/Right), ABKP(4,     6,       4/6            ), ABK(z,x,Z/X), ABK(semicolon,  quote,        Semicolon/Quote    ) } },
			{ RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, { ABK(up,    down,    Up/Down   ), ABKP(8,     2,       8/2            ), ABK(q,a,Q/A), ABK(backspace,  backslash,    Backspace/Backslash) } },
			{ RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, { ABK(home,  end,     Home/End  ), ABKP(minus, plus,    Minus/Plus     ), ABK(j,l,J/L), ABK(leftbracket,rightbracket, Left/Right Bracket ) } },
			{ RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, { ABK(pageup,pagedown,PgUp/PgDn ), ABKP(divide,multiply,Divide/Multiply), ABK(i,k,I/K), ABK(minus,      equals,       Minus/Equals       ) } },
			#undef ABK
			#undef ABKP
		};

		bool bound_buttons[RETRO_DEVICE_ID_JOYPAD_R3+1] = {false};
		bool bound_analogs[RETRO_DEVICE_INDEX_ANALOG_RIGHT+1] = {false};
		size_t port_bind_count = dbp_input_binds.size() - port_bind_begin;
		for (DBP_InputBind *b = (port_bind_count ? &dbp_input_binds[0] + port_bind_begin : NULL), *bEnd = b + port_bind_count; b != bEnd; b++)
			if (b->device == RETRO_DEVICE_JOYPAD && b->id <= RETRO_DEVICE_ID_JOYPAD_R3) bound_buttons[b->id] = true;
			else if (b->device == RETRO_DEVICE_ANALOG && b->index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT) bound_analogs[b->index] = true;

		for (int i = 0, j = 0; j != (sizeof(auto_buttons)/sizeof(*auto_buttons)); j++)
		{
			if (bound_buttons[auto_buttons[j].id]) { if (j < 4) i++; continue; }
			dbp_input_binds.push_back({ port, RETRO_DEVICE_JOYPAD, 0, auto_buttons[j].id, auto_buttons[i].ports[abp].name, DBPET_KEYDOWN, auto_buttons[i].ports[abp].meta });
			i++;
		}

		for (int i = 0, j = 0; j != (sizeof(auto_analogs)/sizeof(*auto_analogs)); j++)
		{
			if (bound_analogs[auto_analogs[j].index]) continue;
			dbp_input_binds.push_back({ port, RETRO_DEVICE_ANALOG, auto_analogs[j].index, auto_analogs[j].id, auto_analogs[i].ports[abp].name, DBPET_AXIS_TO_KEY, auto_analogs[i].ports[abp].meta });
			i++;
		}
	}

	bool useJoy1 = false, useJoy2 = false;
	std::vector<retro_input_descriptor> input_descriptor;
	for (DBP_InputBind *b = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *bEnd = b + dbp_input_binds.size(), *prev = NULL; b != bEnd; prev = b++)
	{
		useJoy1 |= (b->evt == DBPET_JOY1X || b->evt == DBPET_JOY1Y || b->evt == DBPET_JOY1DOWN);
		useJoy2 |= (b->evt == DBPET_JOY2X || b->evt == DBPET_JOY2Y || b->evt == DBPET_JOY2DOWN || b->evt == DBPET_JOYHATSETBIT);
		if (b->device != RETRO_DEVICE_MOUSE && b->desc)
			if (!prev || prev->port != b->port || prev->device != b->device || prev->index != b->index || prev->id != b->id)
				input_descriptor.push_back( { b->port, b->device, b->index, b->id, b->desc } );
	}
	input_descriptor.push_back( { 0 } );
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &input_descriptor[0]);

	JOYSTICK_Enable(0, useJoy1);
	JOYSTICK_Enable(1, useJoy2);
}

static void set_variables(bool force_midi_scan)
{
	static std::vector<std::string> dynstr;
	dynstr.clear();
	bool use_midi_cache = false;
	std::string path, subdir;
	const char *system_dir = NULL;
	struct retro_vfs_interface_info vfs = { 3, NULL };
	retro_time_t scan_start = time_cb();
	if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir && environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) && vfs.required_interface_version >= 3 && vfs.iface)
	{
		std::vector<std::string> subdirs;
		subdirs.push_back(std::string());
		while (subdirs.size())
		{
			std::swap(subdir, subdirs.back());
			subdirs.pop_back();
			struct retro_vfs_dir_handle *dir = vfs.iface->opendir(path.assign(system_dir).append(subdir.length() ? "/" : "").append(subdir).c_str(), false);
			if (!dir) continue;
			while (vfs.iface->readdir(dir))
			{
				const char* entry_name = vfs.iface->dirent_get_name(dir);
				size_t entry_len = strlen(entry_name);
				if (vfs.iface->dirent_is_dir(dir) && strcmp(entry_name, ".") && strcmp(entry_name, ".."))
					subdirs.push_back(path.assign(subdir).append(subdir.length() ? "/" : "").append(entry_name));
				else if ((entry_len > 4 && !strcasecmp(entry_name + entry_len - 4, ".sf2"))
						|| (entry_len > 12 && !strcasecmp(entry_name + entry_len - 12, "_CONTROL.ROM")))
				{
					dynstr.push_back(path.assign(subdir).append(subdir.length() ? "/" : "").append(entry_name));
					dynstr.push_back(entry_name[entry_len-1] == '2' ? "General MIDI SoundFont" : "Roland MT-32/CM-32L");
					dynstr.back().append(": ").append(path, 0, path.size() - (entry_name[entry_len-1] == '2' ? 4 : 12));
				}
				else if (entry_len == 23 && !subdir.length() && !force_midi_scan && !strcasecmp(entry_name, "DOSBoxPureMidiCache.txt"))
				{
					FILE* f = fopen_wrap(path.assign(system_dir).append("/").append(entry_name).c_str(), "rb");
					if (!f) continue;
					fseek(f, 0, SEEK_END);
					size_t fsize = ftell(f);
					fseek(f, 0, SEEK_SET);
					char* cache = new char[fsize + 1];
					if (!fread(cache, fsize, 1, f)) { DBP_ASSERT(0); fsize = 0;}
					fclose(f);
					cache[fsize] = '\0';
					dynstr.clear();
					for (char *pLine = cache, *pEnd = cache + fsize + 1, *p = cache; p != pEnd; p++)
					{
						if (*p >= ' ') continue;
						if (p == pLine) { pLine++; continue; }
						dynstr.push_back(path.assign(pLine, p - pLine));
						pLine = p + 1;
					}
					delete [] cache;
					if (dynstr.size() & 1) dynstr.pop_back();
					use_midi_cache = true;
					subdirs.clear();
					break;
				}
			}
			vfs.iface->closedir(dir);
		}
	}
	if (force_midi_scan || (!use_midi_cache && time_cb() - scan_start > 2000000 && system_dir))
	{
		FILE* f = fopen_wrap(path.assign(system_dir).append("/").append("DOSBoxPureMidiCache.txt").c_str(), "w");
		if (f)
		{
			for (const std::string& s : dynstr) { fwrite(s.c_str(), s.length(), 1, f); fwrite("\n", 1, 1, f); }
			fclose(f);
		}
	}

	#include "core_options.h"
	for (retro_core_option_v2_definition& def : option_defs)
	{
		if (!def.key || strcmp(def.key, "dosbox_pure_midi")) continue;
		size_t i = 0, numfiles = (dynstr.size() > (RETRO_NUM_CORE_OPTION_VALUES_MAX-4)*2 ? (RETRO_NUM_CORE_OPTION_VALUES_MAX-4)*2 : dynstr.size());
		for (size_t f = 0; f != numfiles; f += 2)
			if (dynstr[f].back() == '2')
				def.values[i++] = { dynstr[f].c_str(), dynstr[f+1].c_str() };
		for (size_t f = 0; f != numfiles; f += 2)
			if (dynstr[f].back() != '2')
				def.values[i++] = { dynstr[f].c_str(), dynstr[f+1].c_str() };
		def.values[i++] = { "disabled", "Disabled" };
		def.values[i++] = { "frontend", "Frontend MIDI driver" };
		if (use_midi_cache)
			def.values[i++] = { "scan", "Scan System directory for soundfonts (open this menu again after)" };
		else if (force_midi_scan)
			def.values[i++] = { "scan", "System directory scan finished" };
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
				dynstr.push_back(v2def.category_key);
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

static bool check_variables()
{
	struct Variables
	{
		static bool DosBoxSet(const char* section_name, const char* var_name, const char* new_value, bool disallow_in_game = false, bool need_restart = false)
		{
			if (!control) return false;

			Section* section = control->GetSection(section_name);
			DBP_ASSERT(section);
			std::string str = var_name;
			std::string old_val = section->GetPropValue(str);
			DBP_ASSERT(old_val != "PROP_NOT_EXIST");
			if (!section || old_val == new_value) return false;

			bool reInitSection = (dbp_state != DBPSTATE_BOOT);
			if (disallow_in_game && dbp_game_running)
			{
				retro_notify(0, RETRO_LOG_WARN, "Unable to change value while game is running");
				reInitSection = false;
			}
			if (need_restart && reInitSection)
			{
				retro_notify(2000, RETRO_LOG_INFO, "Setting will be applied after restart");
				reInitSection = false;
			}

			//log_cb(RETRO_LOG_INFO, "[DOSBOX] variable %s::%s updated from %s to %s\n", section_name, var_name, old_val.c_str(), new_value);
			str += '=';
			str += new_value;
			if (reInitSection)
			{
				if (dbp_new_timing)
				{
					DBP_ThreadControl(TCM_PAUSE_FRAME);
					if (!memcmp(str.c_str(), "midiconfig=", 11) && MIDI_TSF_SwitchSF2(str.c_str() + 11))
					{
						// Do the SF2 reload directly (otherwise midi output stops until dos program restart)
						section->HandleInputline(str);
					}
					else if (!memcmp(str.c_str(), "cycles=", 7))
					{
						// Set cycles value without Destroy/Init (because that can cause FPU overflow crashes)
						DBP_CPU_ModifyCycles(str.c_str() + 7);
						section->HandleInputline(str);
					}
					else
					{
						section->ExecuteDestroy(false);
						section->HandleInputline(str);
						section->ExecuteInit(false);
					}
					DBP_ThreadControl(TCM_RESUME_FRAME);
				}
#ifndef DBP_REMOVE_OLD_TIMING
				if (!dbp_new_timing)
				{
					DBPOld_QueueEvent(DBPETOLD_SET_VARIABLE, str, section);
				}
#endif
			}
			else
			{
				section->HandleInputline(str);
			}
			return true;
		}

		static const char* RetroGet(const char* key, const char* default_value)
		{
			retro_variable var;
			var.key = key;
			return (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : default_value);
		}

		static void RetroVisibility(const char* key, bool visible)
		{
			retro_core_option_display disp;
			disp.key = key;
			disp.visible = visible;
			if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &disp);
		}
	};

	char buf[16];
	unsigned options_ver = 0;
	if (environ_cb) environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &options_ver);
	bool show_advanced = (options_ver != 1 || Variables::RetroGet("dosbox_pure_advanced", "false")[0] != 'f');
	bool visibility_changed = false;

	if (dbp_last_hideadvanced == show_advanced)
	{
		static const char* advanced_options[] =
		{
			"dosbox_pure_mouse_speed_factor_x",
			"dosbox_pure_mouse_input",
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
		};
		for (const char* i : advanced_options) Variables::RetroVisibility(i, show_advanced);
		dbp_last_hideadvanced = !show_advanced;
		visibility_changed = true;
	}

	dbp_auto_mapping_mode = Variables::RetroGet("dosbox_pure_auto_mapping", "true")[0];

	const char* machine = Variables::RetroGet("dosbox_pure_machine", "svga");
	if (!strcmp(machine, "svga")) machine = Variables::RetroGet("dosbox_pure_svga", "svga_s3");
	else if (!strcmp(machine, "vga")) machine = "vgaonly";
	Variables::DosBoxSet("dosbox", "machine", machine, false, true);

	const char* mem = Variables::RetroGet("dosbox_pure_memory_size", "16");
	bool mem_use_extended = (atoi(mem) > 0);
	Variables::DosBoxSet("dos", "xms", (mem_use_extended ? "true" : "false"), true);
	Variables::DosBoxSet("dos", "ems", (mem_use_extended ? "true" : "false"), true);
	Variables::DosBoxSet("dosbox", "memsize", (mem_use_extended ? mem : "16"), false, true);

	const char* audiorate = Variables::RetroGet("dosbox_pure_audiorate", DBP_DEFAULT_SAMPLERATE_STRING);
	Variables::DosBoxSet("mixer", "rate", audiorate, false, true);

	if (dbp_state == DBPSTATE_BOOT)
	{
		dbp_last_machine = machine[0];

		Variables::DosBoxSet("sblaster", "oplrate",   audiorate);
		Variables::DosBoxSet("speaker",  "pcrate",    audiorate);
		Variables::DosBoxSet("speaker",  "tandyrate", audiorate);

		// initiate audio buffer, we don't need SDL specific behavior, so just set a large enough buffer
		Variables::DosBoxSet("mixer", "prebuffer", "0");
		Variables::DosBoxSet("mixer", "blocksize", "2048");
	}

	// Emulation options
#ifndef DBP_REMOVE_OLD_TIMING
	bool experimental_new_timing = (Variables::RetroGet("dosbox_pure_experimental_timing_mode", "legacy")[0] == 'n'); static bool lastent; if (experimental_new_timing != lastent) { retro_notify(2000, RETRO_LOG_INFO, "Setting will be applied after restart"); lastent^=1; }
	Variables::RetroVisibility("dosbox_pure_force60fps", experimental_new_timing);
	Variables::RetroVisibility("dosbox_pure_latency", experimental_new_timing);
	Variables::RetroVisibility("dosbox_pure_auto_target", experimental_new_timing);
	Variables::RetroVisibility("dosbox_pure_perfstats", experimental_new_timing);
#endif
	dbp_force60fps = (Variables::RetroGet("dosbox_pure_force60fps", "default")[0] == 't');
	dbp_lowlatency = (Variables::RetroGet("dosbox_pure_latency", "default")[0] == 'l');
	Variables::RetroVisibility("dosbox_pure_auto_target", dbp_lowlatency);
	dbp_auto_target = (dbp_lowlatency ? (float)atof(Variables::RetroGet("dosbox_pure_auto_target", "1.0")) : 1.0f);
	switch (Variables::RetroGet("dosbox_pure_perfstats", "none")[0])
	{
		case 's': dbp_perf = DBP_PERF_SIMPLE; break;
		case 'd': dbp_perf = DBP_PERF_DETAILED; break;
		default:  dbp_perf = DBP_PERF_NONE; break;
	}
	switch (Variables::RetroGet("dosbox_pure_savestate", "on")[0])
	{
		case 'd': dbp_serializemode = DBPSERIALIZE_DISABLED; break;
		case 'r': dbp_serializemode = DBPSERIALIZE_REWIND; break;
		default: dbp_serializemode = DBPSERIALIZE_STATES; break;
	}
	DBPArchive::accomodate_delta_encoding = (dbp_serializemode == DBPSERIALIZE_REWIND);
	dbp_menu_time = (char)atoi(Variables::RetroGet("dosbox_pure_menu_time", "5"));

	const char* cycles = Variables::RetroGet("dosbox_pure_cycles", "auto");
	bool cycles_numeric = (cycles[0] >= '0' && cycles[0] <= '9');
	Variables::RetroVisibility("dosbox_pure_cycles_scale", cycles_numeric);
	if (cycles_numeric)
	{
		snprintf(buf, sizeof(buf), "%d", (int)(atoi(cycles) * (float)atof(Variables::RetroGet("dosbox_pure_cycles_scale", "1.0")) + .499));
		cycles = buf;
	}
	visibility_changed |= Variables::DosBoxSet("cpu", "cycles", cycles);

	Variables::DosBoxSet("cpu", "core",    Variables::RetroGet("dosbox_pure_cpu_core", "auto"), false, true);
	Variables::DosBoxSet("cpu", "cputype", Variables::RetroGet("dosbox_pure_cpu_type", "auto"), false, true);

	if (dbp_last_machine != machine[0])
	{
		dbp_last_machine = machine[0];
		visibility_changed = true;
	}

	bool machine_is_svga = !strcmp(machine, "svga");
	Variables::RetroVisibility("dosbox_pure_svga", machine_is_svga);

	bool machine_is_cga = !strcmp(machine, "cga");
	Variables::RetroVisibility("dosbox_pure_cga", machine_is_cga);
	if (machine_is_cga)
	{
		const char* cga = Variables::RetroGet("dosbox_pure_cga", "early_auto");
		bool cga_new_model = false;
		const char* cga_mode = NULL;
		if (!memcmp(cga, "early_", 6)) { cga_new_model = false; cga_mode = cga + 6; }
		if (!memcmp(cga, "late_",  5)) { cga_new_model = true;  cga_mode = cga + 5; }
		DBP_CGA_SetModelAndComposite(cga_new_model, (!cga_mode || cga_mode[0] == 'a' ? 0 : ((cga_mode[0] == 'o' && cga_mode[1] == 'n') ? 1 : 2)));
	}

	bool machine_is_hercules = !strcmp(machine, "hercules");
	Variables::RetroVisibility("dosbox_pure_hercules", machine_is_hercules);
	if (machine_is_hercules)
	{
		const char herc_mode = Variables::RetroGet("dosbox_pure_hercules", "white")[0];
		DBP_Hercules_SetPalette(herc_mode == 'a' ? 1 : (herc_mode == 'g' ? 2 : 0));
	}

	Variables::DosBoxSet("render", "aspect",  Variables::RetroGet("dosbox_pure_aspect_correction", "false"));

	const char* sblaster_conf = Variables::RetroGet("dosbox_pure_sblaster_conf", "A220 I7 D1 H5");
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
	const char* midi = Variables::RetroGet("dosbox_pure_midi", "");
	if (!strcmp(midi, "scan"))
	{
		set_variables(true);
		midi = Variables::RetroGet("dosbox_pure_midi", "");
	}
	if (!*midi) midi = Variables::RetroGet("dosbox_pure_soundfont", ""); // old option name
	if (!strcmp(midi, "disabled") || !strcasecmp(midi, "none")) midi = "";
	else if (*midi && strcmp(midi, "frontend") && strcmp(midi, "scan"))
	{
		const char *system_dir = NULL;
		environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir);
		midi = soundfontpath.assign(system_dir).append("/").append(midi).c_str();
	}
	Variables::DosBoxSet("midi", "midiconfig", midi);

	Variables::DosBoxSet("sblaster", "sbtype", Variables::RetroGet("dosbox_pure_sblaster_type", "sb16"));
	Variables::DosBoxSet("sblaster", "oplmode", Variables::RetroGet("dosbox_pure_sblaster_adlib_mode", "auto"));
	Variables::DosBoxSet("sblaster", "oplemu", Variables::RetroGet("dosbox_pure_sblaster_adlib_emu", "default"));
	Variables::DosBoxSet("gus", "gus", Variables::RetroGet("dosbox_pure_gus", "false"));

	Variables::DosBoxSet("joystick", "timed", Variables::RetroGet("dosbox_pure_joystick_timed", "true"));

	// Keyboard layout can't be change in protected mode (extracting keyboard layout doesn't work when EMS/XMS is in use)
	Variables::DosBoxSet("dos", "keyboardlayout", Variables::RetroGet("dosbox_pure_keyboard_layout", "us"), true);

	const char* mouse_wheel = Variables::RetroGet("dosbox_pure_mouse_wheel", "67/68");
	const char* mouse_wheel2 = (mouse_wheel ? strchr(mouse_wheel, '/') : NULL);
	int wkey1 = (mouse_wheel ? atoi(mouse_wheel) : 0);
	int wkey2 = (mouse_wheel2 ? atoi(mouse_wheel2 + 1) : 0);
	Bit16u bind_mousewheel (wkey1 > KBD_NONE && wkey1 < KBD_LAST && wkey2 > KBD_NONE && wkey2 < KBD_LAST ? DBP_KEYAXIS_MAKE(wkey1, wkey2) : 0);

	bool bind_unused = (Variables::RetroGet("dosbox_pure_bind_unused", "true")[0] != 'f');
	bool on_screen_keyboard = (Variables::RetroGet("dosbox_pure_on_screen_keyboard", "true")[0] != 'f');
	bool mouse_input = (Variables::RetroGet("dosbox_pure_mouse_input", "true")[0] != 'f');
	if (bind_unused != dbp_bind_unused || on_screen_keyboard != dbp_on_screen_keyboard || mouse_input != dbp_mouse_input || bind_mousewheel != dbp_bind_mousewheel)
	{
		dbp_bind_unused = bind_unused;
		dbp_on_screen_keyboard = on_screen_keyboard;
		dbp_mouse_input = mouse_input;
		dbp_bind_mousewheel = bind_mousewheel;
		if (dbp_state > DBPSTATE_SHUTDOWN) refresh_input_binds();
	}

	dbp_mouse_speed = (float)atof(Variables::RetroGet("dosbox_pure_mouse_speed_factor", "1.0"));
	dbp_mouse_speed_x = (float)atof(Variables::RetroGet("dosbox_pure_mouse_speed_factor_x", "1.0"));

	dbp_joy_analog_deadzone = (int)((float)atoi(Variables::RetroGet("dosbox_pure_joystick_analog_deadzone", "15")) * 0.01f * (float)DBP_JOY_ANALOG_RANGE);

	return visibility_changed;
}

static bool init_dosbox(const char* path, bool firsttime)
{
	DBP_ASSERT(dbp_state == DBPSTATE_BOOT);
	control = new Config();
	DOSBOX_Init();
	check_variables();
	dbp_boot_time = time_cb();
	control->Init();
	PROGRAMS_MakeFile("PUREMENU.COM", DBP_PureMenuProgram);
	PROGRAMS_MakeFile("LABEL.COM", DBP_PureLabelProgram);
	PROGRAMS_MakeFile("REMOUNT.COM", DBP_PureRemountProgram);

	DOS_Drive* union_underlay = (path ? DBP_Mount(path, true, true) : NULL);

	if (!dbp_disk_eject_state && dbp_disk_image_index < dbp_images.size() && !Drives['A'-'A'] && !Drives['D'-'A'])
		DBP_Mount(dbp_images[dbp_disk_image_index].c_str(), false, false);

	if (!Drives['C'-'A'])
	{
		if (!union_underlay) union_underlay = new memoryDrive();

		std::string save_file;
		const char *env_save_dir = NULL;
		environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &env_save_dir);
		if (env_save_dir)
		{
			save_file = env_save_dir;
			save_file += '/';
			save_file += (dbp_content_name.empty() ? "DOSBox-pure" : dbp_content_name.c_str());
			save_file += ".save.zip";
		}

		unionDrive* uni = new unionDrive(*union_underlay, (save_file.empty() ? NULL : &save_file[0]), true);
		Drives['C'-'A'] = uni;

		// Set the media byte and the hard drive label and switch to it
		mem_writeb(Real2Phys(dos.tables.mediaid) + ('C'-'A') * 9, uni->GetMediaByte());
	}

	// Detect auto mapping
	if (firsttime && dbp_auto_mapping_mode != 'f')
	{
		struct Local
		{
			static void FileIter(const char* path, bool is_dir, Bit32u size, Bit16u, Bit16u, Bit8u, Bitu data)
			{
				if (is_dir || dbp_auto_mapping) return;
				const char* lastslash = strrchr(path, '\\');

				Bit32u hash = 0x811c9dc5;
				for (const char* p = (lastslash ? lastslash + 1 : path); *p; p++)
					hash = ((hash * 0x01000193) ^ (Bit8u)*p);
				hash ^= (size<<3);

				for (Bit32u idx = hash;; idx++)
				{
					if (!map_keys[idx &= (MAP_TABLE_SIZE-1)]) break;
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

					static_title = "Detected Automatic Key Mapping: ";
					static_title += map_title;
					dbp_auto_mapping_title = static_title.c_str();

					static_buf.resize(mappings_bk.mappings_size_uncompressed);
					buf = &static_buf[0];
					zipDrive::Uncompress(mappings_bk.mappings_compressed, mappings_bk.mappings_size_compressed, buf, mappings_bk.mappings_size_uncompressed);

					dbp_auto_mapping = buf + map_offset;
					dbp_auto_mapping_names = (char*)buf + mappings_bk.mappings_action_offset;

					if (dbp_auto_mapping_mode == 'n') //notify
						retro_notify(0, RETRO_LOG_INFO, dbp_auto_mapping_title);
					return;
				}
			}
		};
		for (int i = 0; i != ('Z'-'A'); i++)
			if (Drives[i] && !dbp_auto_mapping)
				DriveFileIterator(Drives[i], Local::FileIter);
	}

	// Always launch puremenu, to tell us the shell was fully started
	control->GetSection("autoexec")->ExecuteDestroy();
	static_cast<Section_line*>(control->GetSection("autoexec"))->data += "@PUREMENU -BOOT\n";
	control->GetSection("autoexec")->ExecuteInit();

	char org_menu_time = dbp_menu_time;
	bool force_start_menu = (!input_state_cb ? false : (
		input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LSHIFT) ||
		input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RSHIFT) ||
		input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2) ||
		input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2)));
	if (force_start_menu) dbp_menu_time = (char)-1;

	retro_variable var;
	var.key = "dosbox_pure_experimental_timing_mode";
	environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
	dbp_new_timing = (var.value && var.value[0] == 'n');
#ifdef DBP_REMOVE_OLD_TIMING
	dbp_new_timing = true;
#endif

	struct Local
	{
		static Thread::RET_t THREAD_CC ThreadDOSBox(void*)
		{
			control->StartUp();
			dbp_state = DBPSTATE_EXITED;
			semDidPause.Post();
			return 0;
		}
#ifndef DBP_REMOVE_OLD_TIMING
		static Thread::RET_t THREAD_CC ThreadDOSBoxOld(void*)
		{
			dbp_lockthreadmtx[1].Lock();
			control->StartUp();
			dbp_lockthreadmtx[1].Unlock();
			dbp_state = DBPSTATE_EXITED;
			return 0;
		}
#endif
	};

	// Start DOSBox and wait until the shell has fully started
	DBP_ASSERT(DOS_GetDefaultDrive() == ('Z'-'A')); // Shell must start with Z:\AUTOEXEC.BAT
	dbp_lastmenuticks = (Bit32u)-1;
	if (dbp_new_timing)
	{
		dbp_frame_pending = true;
		Thread::StartDetached(Local::ThreadDOSBox);
	}
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		Thread::StartDetached(Local::ThreadDOSBoxOld);
	}
#endif
	while (dbp_lastmenuticks == (Bit32u)-1)
	{
		if (dbp_state == DBPSTATE_EXITED) { DBP_Shutdown(); return false; }
		retro_sleep(1);
	}
	dbp_state = DBPSTATE_FIRST_FRAME;
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		dbp_last_run = DBP_GetTicks();
		dbp_retro_activity = 1;
	}
#endif
	dbp_menu_time = org_menu_time;
	return true;
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

#ifndef DBP_REMOVE_OLD_TIMING
		static void RETRO_CALLCONV retro_frame_time(retro_usec_t usec)
		{
			if (dbp_new_timing) return;
			dbp_frame_time = usec;
			dbp_timing_tamper = (usec == 0 && dbp_state == DBPSTATE_RUNNING);
			bool variable_update = false;
			// because usec is > 0 when the menu is active even though retro_run is paused we always check variables here, too
			if (/*dbp_timing_tamper && */environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &variable_update) && variable_update)
				check_variables();
		}
#endif

		static bool RETRO_CALLCONV set_eject_state(bool ejected)
		{
			if (dbp_images.size() == 0) { dbp_disk_eject_state = true; return ejected; }
			if (dbp_disk_eject_state == ejected) return true;
			if (dbp_new_timing)
			{
				DBP_ThreadControl(TCM_PAUSE_FRAME);
				if (ejected)
				{
					if (dbp_disk_mount_letter && Drives[dbp_disk_mount_letter-'A'] && Drives[dbp_disk_mount_letter-'A']->UnMount() == 0)
					{
						Drives[dbp_disk_mount_letter-'A'] = 0;
						mem_writeb(Real2Phys(dos.tables.mediaid)+(dbp_disk_mount_letter-'A')*9,0);
					}
				}
				else
				{
					if (!Drives['A'-'A'] && !Drives['D'-'A'])
					{
						DBP_Mount(dbp_images[dbp_disk_image_index].c_str(), false, false);
					}
				}
				DBP_ThreadControl(TCM_RESUME_FRAME);
			}
#ifndef DBP_REMOVE_OLD_TIMING
			if (!dbp_new_timing)
			{
				if (ejected)
					DBP_QueueEvent(DBPETOLD_UNMOUNT);
				else
				{
					std::string swappable = dbp_images[dbp_disk_image_index];
					DBPOld_QueueEvent(DBPETOLD_MOUNT, swappable);
				}
			}
#endif
			DBP_QueueEvent(DBPET_CHANGEMOUNTS);
			dbp_disk_eject_state = ejected;
			return true;
		}

		static bool RETRO_CALLCONV get_eject_state()
		{
			if (dbp_images.size() == 0) dbp_disk_eject_state = true;
			return dbp_disk_eject_state;
		}

		static unsigned RETRO_CALLCONV get_image_index()
		{
			return dbp_disk_image_index;
		}

		static bool RETRO_CALLCONV set_image_index(unsigned index)
		{
			if (index >= dbp_images.size()) return false;
			dbp_disk_image_index = index;
			return true;
		}

		static unsigned RETRO_CALLCONV get_num_images()
		{
			return (unsigned)dbp_images.size();
		}

		static bool RETRO_CALLCONV replace_image_index(unsigned index, const struct retro_game_info *info)
		{
			if (index >= dbp_images.size()) return false;
			if (info == NULL)
			{
				if (dbp_disk_image_index > index) dbp_disk_image_index--;
				dbp_images.erase(dbp_images.begin() + index);
				if (dbp_disk_image_index == dbp_images.size()) dbp_disk_image_index--;
			}
			else
			{
				dbp_images[index] = info->path;
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
			safe_strncpy(path, dbp_images[index].c_str(), len);
			return true;
		}

		static bool RETRO_CALLCONV get_image_label(unsigned index, char *label, size_t len)
		{
			if (index >= dbp_images.size()) return false;
			const char *lastSlash = strrchr(dbp_images[index].c_str(), '/'), *lastBackSlash = strrchr(dbp_images[index].c_str(), '\\');
			const char *basePath = (lastSlash && lastSlash > lastBackSlash ? lastSlash + 1 : (lastBackSlash ? lastBackSlash + 1 : dbp_images[index].c_str()));
			safe_strncpy(label, basePath, len);
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

#ifndef DBP_REMOVE_OLD_TIMING
	static const struct retro_frame_time_callback rftc = { CallBacks::retro_frame_time, 0 };
	environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, (void*)&rftc);
#endif

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

	struct retro_perf_callback perf;
	if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf) && perf.get_time_usec) time_cb = perf.get_time_usec;

	// Set default ports
	dbp_port_devices[0] = (DBP_Port_Device)DBP_DEVICE_DefaultJoypad;
	dbp_port_devices[1] = (DBP_Port_Device)DBP_DEVICE_DefaultJoypad;

	set_variables(false);
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

	const char* path = (info ? info->path : NULL);

	if (!init_dosbox(path, true)) return false;

	DOS_PSP psp(dos.psp());
	DOS_MCB env_mcb(psp.GetEnvironment() - 1);
	PhysPt env_end = PhysMake(psp.GetEnvironment() + env_mcb.GetSize(), 0);

	// Give access to entire memory to frontend (cheat and achievements support)
	// Instead of raw [ENVIRONMENT] [GAME] [EXTENDED MEMORY] we switch the order to be
	// [GAME] [ENVIRONMENT] [EXTENDED MEMORY] so irregardless of the size of the OS environment
	// the game memory (below 640k) is always at the same (virtual) address.
	struct retro_memory_descriptor mdescs[3] = { 0 };
	mdescs[0].flags = RETRO_MEMDESC_SYSTEM_RAM;
	mdescs[0].start = 0;
	mdescs[0].ptr   = MemBase + env_end;
	mdescs[0].len   = (640 * 1024) - env_end;
	mdescs[1].flags = RETRO_MEMDESC_SYSTEM_RAM;
	mdescs[1].start = mdescs[0].start + mdescs[0].len;
	mdescs[1].ptr   = MemBase;
	mdescs[1].len   = env_end;
	mdescs[2].flags = RETRO_MEMDESC_SYSTEM_RAM;
	mdescs[2].start = mdescs[1].start + mdescs[1].len;
	mdescs[2].ptr   = MemBase + (640 * 1024);
	mdescs[2].len   = (MEM_TotalPages() * 4096) - (640 * 1024);
	struct retro_memory_map mmaps = { mdescs, 3 };
	environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);

	bool support_achievements = true;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &support_achievements);

	unsigned port, port_first_cd[3];
	struct retro_controller_info ports[DBP_MAX_PORTS+1] = {{0}};
	static std::vector<retro_controller_description> controller_descriptions; // descriptions must be static data
	controller_descriptions.clear();
	for (port = 0; port != 3; port++)
	{
		const DBP_Port_Device gravis_device = (port != 0 || dbp_auto_mapping ? DBP_DEVICE_GravisGamepad : DBP_DEVICE_DefaultJoypad);
		const DBP_Port_Device secondjoystick_device = (port != 1 ? DBP_DEVICE_BasicJoystick2 : DBP_DEVICE_DefaultJoypad);
		port_first_cd[port] = (unsigned)controller_descriptions.size();
		controller_descriptions.push_back({ "Disabled",                                             (unsigned)DBP_DEVICE_Disabled                });
		if (port == 0 && dbp_auto_mapping)
			controller_descriptions.push_back({ dbp_auto_mapping_title,                             (unsigned)DBP_DEVICE_DefaultJoypad           });
		controller_descriptions.push_back({ "Generic Keyboard Bindings",                            (unsigned)DBP_DEVICE_BindGenericKeyboard     });
		controller_descriptions.push_back({ "Mouse with Left Analog Stick",                         (unsigned)DBP_DEVICE_MouseLeftAnalog         });
		controller_descriptions.push_back({ "Mouse with Right Analog Stick",                        (unsigned)DBP_DEVICE_MouseRightAnalog        });
		controller_descriptions.push_back({ "Gravis GamePad (1 D-Pad, 4 Buttons)",                  (unsigned)gravis_device                      });
		controller_descriptions.push_back({ "First DOS joystick (2 Axes, 2 Buttons)",               (unsigned)DBP_DEVICE_BasicJoystick1          });
		controller_descriptions.push_back({ "Second DOS joystick (2 Axes, 2 Buttons)",              (unsigned)secondjoystick_device              });
		controller_descriptions.push_back({ "ThrustMaster Flight Stick (3 axes, 4 buttons, 1 hat)", (unsigned)DBP_DEVICE_ThrustMasterFlightStick });
		controller_descriptions.push_back({ "Control both DOS joysticks (4 axes, 4 buttons)",       (unsigned)DBP_DEVICE_BothDOSJoysticks        });
		controller_descriptions.push_back({ "Custom Keyboard Bindings",                             (unsigned)DBP_DEVICE_BindCustomKeyboard      });
		controller_descriptions.push_back({ "Custom Keyboard + Mouse on Left Stick and B/A/X",      (unsigned)DBP_DEVICE_KeyboardMouseLeftStick  });
		controller_descriptions.push_back({ "Custom Keyboard + Mouse on Right Stick and L/R/X",     (unsigned)DBP_DEVICE_KeyboardMouseRightStick });
		ports[port].num_types = (unsigned)controller_descriptions.size() - port_first_cd[port];
	}
	for (port = 0; port != 3; port++) ports[port].types = &controller_descriptions[port_first_cd[port]];
	for (; port != DBP_MAX_PORTS; port++) ports[port] = ports[2];
	environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

	refresh_input_binds();

	return true;
}

void retro_get_system_av_info(struct retro_system_av_info *info) // #5
{
	DBP_ASSERT(dbp_state != DBPSTATE_BOOT);
	av_info.geometry.max_width = SCALER_MAXWIDTH;
	av_info.geometry.max_height = SCALER_MAXHEIGHT;
	if (dbp_new_timing)
	{
		// More accurate then render.src.fps would be (1000.0 / vga.draw.delay.vtotal)
		DBP_ThreadControl(TCM_FINISH_FRAME);
		DBP_ASSERT(render.src.fps > 10.0); // validate initialized video mode after first frame
		const DBP_Buffer& buf = dbp_buffers[buffer_active];
		av_info.geometry.base_width = buf.width;
		av_info.geometry.base_height = buf.height;
		av_info.geometry.aspect_ratio = buf.ratio;
		av_info.timing.fps = (dbp_force60fps ? 60.0 : render.src.fps);
	}
	av_info.timing.sample_rate = DBP_MIXER_GetFrequency();
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		av_info.geometry.base_width = 320;
		av_info.geometry.base_height = 200;
		av_info.geometry.aspect_ratio = 4.0f / 3.0f;
		av_info.timing.fps = old_DBP_DEFAULT_FPS;
		if (environ_cb)
		{
			float refresh_rate = (float)old_DBP_DEFAULT_FPS;
			if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &refresh_rate))
				av_info.timing.fps = refresh_rate;
		}
		dbp_calculate_min_sleep();
	}
#endif
	*info = av_info;
}

void retro_unload_game(void)
{
	DBP_Shutdown();
}

void retro_set_controller_port_device(unsigned port, unsigned device) //#5
{
	if (port >= DBP_MAX_PORTS || dbp_port_devices[port] == (DBP_Port_Device)device) return;
	//log_cb(RETRO_LOG_INFO, "[DOSBOX] Plugging device %u into port %u.\n", device, port);
	dbp_port_devices[port] = (DBP_Port_Device)device;
	if (dbp_state > DBPSTATE_SHUTDOWN) refresh_input_binds(port);
}

void retro_reset(void)
{
	DBP_Shutdown();
	DBPArchiveZeroer ar;
	DBPSerialize_All(ar);
	extern const char* RunningProgram;
	RunningProgram = "DOSBOX";
	dbp_crash_message.clear();
	dbp_state = DBPSTATE_BOOT;
	dbp_throttle = { RETRO_THROTTLE_NONE };
	dbp_game_running = false;
	dbp_serializesize = 0;
	dbp_disk_mount_letter = 0;
	dbp_gfx_intercept = NULL;
	dbp_input_intercept = NULL;
	for (size_t i = dbp_images.size(); i--;)
		if (dbp_images[i][0] == '$')
			dbp_images.erase(dbp_images.begin() + i);
	init_dosbox((dbp_content_path.empty() ? NULL : dbp_content_path.c_str()), false);
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

#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		dbp_retro_activity++;

		// serialize_size got called but was never followed by serialize or unserialize
		if (dbp_lockthreadstate)
			DBP_LockThread(false);
	}
#endif

	if (dbp_state < DBPSTATE_RUNNING)
	{
		if (dbp_state == DBPSTATE_EXITED || dbp_state == DBPSTATE_SHUTDOWN)
		{
			DBP_Buffer& buf = dbp_buffers[buffer_active];
			if (!dbp_crash_message.empty()) // unexpected shutdown
				DBP_Shutdown();
			else if (dbp_state == DBPSTATE_EXITED) // expected shutdown
			{
				#ifndef STATIC_LINKING
				environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
				#else
				// On statically linked platforms shutdown would exit the frontend, so don't do that. Just tint the screen red and sleep.
				for (Bit8u *p = (uint8_t*)buf.video, *pEnd = p + sizeof(buf.video); p < pEnd; p += 56) p[2] = 255;
				retro_sleep(10);
				#endif
			}

			// submit last frame
			memset(buf.audio, 0, buf.samples * 4);
			video_cb(buf.video, buf.width, buf.height, buf.width * 4);
			audio_batch_cb(buf.audio, buf.samples);
			return;
		}

		if (dbp_new_timing)
		{
			DBP_ASSERT(dbp_state == DBPSTATE_FIRST_FRAME);
			DBP_ThreadControl(TCM_FINISH_FRAME);
			if (MIDI_Retro_HasOutputIssue())
				retro_notify(0, RETRO_LOG_WARN, "The frontend MIDI output is not set up correctly");
			dbp_state = DBPSTATE_RUNNING;
		}
#ifndef DBP_REMOVE_OLD_TIMING
		if (!dbp_new_timing)
		{
			if (MIDI_Retro_HasOutputIssue())
				retro_notify(0, RETRO_LOG_WARN, "The frontend MIDI output is not set up correctly");

			float refresh_rate = (float)av_info.timing.fps;
			if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &refresh_rate) && refresh_rate != (float)av_info.timing.fps)
			{
				av_info.timing.fps = refresh_rate;
				environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
				dbp_calculate_min_sleep();
			}

			// first frame
			DBP_ASSERT(dbp_state != DBPSTATE_BOOT);
			for (int n = 0; n++ != 5000 && dbp_state != DBPSTATE_WAIT_FIRST_RUN;) retro_sleep(1);
			if (dbp_state == DBPSTATE_WAIT_FIRST_RUN) dbp_state = DBPSTATE_RUNNING;
		}
#endif
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

#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		static bool last_fast_forward;
		if (last_fast_forward != (dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD))
			DBP_QueueEvent(DBPETOLD_SET_FASTFORWARD, (int)(last_fast_forward ^= 1));

		extern bool DBP_CPUOverload;
		if (DBP_CPUOverload)
		{
			static Bit32u first_overload, last_overload_msg;
			if (!dbp_overload_count) first_overload = DBP_GetTicks();
			if (dbp_retro_activity < 10 || dbp_timing_tamper || last_fast_forward) dbp_overload_count = 0;
			else if (dbp_overload_count++ >= 200)
			{
				Bit32u ticks = DBP_GetTicks();
				if ((ticks - first_overload) < 10000 && (!last_overload_msg || (ticks - last_overload_msg) >= 60000))
				{
					retro_notify(0, RETRO_LOG_WARN, "Emulated CPU is overloaded, try reducing the emulated performance in the core options");
					last_overload_msg = ticks;
				}
				dbp_overload_count = 0;
			}
		}
	}
#endif

	// Use fixed mappings using only port 0 to use in the menu and the on-screen keyboard
	static DBP_InputBind intercept_binds[] =
	{
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   NULL, DBPET_MOUSEDOWN, 0 },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  NULL, DBPET_MOUSEDOWN, 1 },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, NULL, DBPET_MOUSEDOWN, 2 },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,   NULL, DBPET_KEYDOWN, KBD_up   },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN, NULL, DBPET_KEYDOWN, KBD_down },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, NULL, DBPET_ONSCREENKEYBOARD },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    NULL, DBPET_JOY1Y, -1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  NULL, DBPET_JOY1Y,  1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  NULL, DBPET_JOY1X, -1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, NULL, DBPET_JOY1X,  1 },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, NULL, DBPET_JOY1X },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, NULL, DBPET_JOY1Y },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, NULL, DBPET_JOY2X },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, NULL, DBPET_JOY2Y },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, NULL, DBPET_JOY1DOWN, 0 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, NULL, DBPET_JOY1DOWN, 1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, NULL, DBPET_JOY2DOWN, 0 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, NULL, DBPET_JOY2DOWN, 1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, NULL, DBPET_MOUSEDOWN, 0 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, NULL, DBPET_MOUSEDOWN, 1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, NULL, DBPET_KEYDOWN, KBD_esc },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  NULL, DBPET_KEYDOWN, KBD_enter },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, NULL, DBPET_MOUSESETSPEED,  1 },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, NULL, DBPET_MOUSESETSPEED, -1 },
	};
	DBP_InputBind *binds = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]);
	DBP_InputBind *binds_end = binds + dbp_input_binds.size();
	input_poll_cb();
	static bool use_input_intercept;
	bool toggled_intercept = (use_input_intercept != !!dbp_input_intercept);
	if (toggled_intercept)
	{
		use_input_intercept ^= 1;
		if (!use_input_intercept) for (DBP_InputBind* b = intercept_binds; b != &intercept_binds[sizeof(intercept_binds)/sizeof(*intercept_binds)]; b++)
		{
			// Release all pressed events when leaving intercepted screen
			DBP_ASSERT(b->evt != DBPET_AXIS_TO_KEY);
			if (!b->lastval) continue;
			if (b->evt <= _DBPET_JOY_AXIS_MAX) DBP_QueueEvent((DBP_Event_Type)b->evt, 0);
			else DBP_QueueEvent((DBP_Event_Type)(b->evt + 1), b->meta);
			b->lastval = 0;
		}
	}
	if (use_input_intercept)
	{
		//input_state_cb(0, RETRO_DEVICE_NONE, 0, 0); // poll keys? 
		//input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_SPACE); // get latest keyboard callbacks?
		if (toggled_intercept)
			for (DBP_InputBind* b = intercept_binds; b != &intercept_binds[sizeof(intercept_binds)/sizeof(*intercept_binds)]; b++)
				b->lastval = input_state_cb(b->port, b->device, b->index, b->id);
		binds = intercept_binds + (dbp_mouse_input ? 0 : 5);
		binds_end = &intercept_binds[sizeof(intercept_binds)/sizeof(*intercept_binds)];

		static bool warned_game_focus;
		if (!dbp_gfx_intercept && !warned_game_focus && dbp_port_devices[0] != DBP_DEVICE_BindCustomKeyboard && input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK))
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
	}

	// query input states and generate input events
	for (DBP_InputBind *b = binds; b != binds_end; b++)
	{
		int16_t val = input_state_cb(b->port, b->device, b->index, b->id);
		if (val == b->lastval) continue;
		if (b->evt == DBPET_AXIS_TO_KEY)
		{
			int cur = (val < -12000 ? -1 : (val > 12000 ? 1 : 0)), last = (b->lastval < -12000 ? -1 : (b->lastval > 12000 ? 1 : 0));
			if (cur == last) {}
			else if (cur && last)
			{
				DBP_QueueEvent(DBPET_KEYUP, DBP_KEYAXIS_GET(last, b->meta));
				DBP_QueueEvent(DBPET_KEYDOWN, DBP_KEYAXIS_GET(cur, b->meta));
			}
			else
			{
				DBP_QueueEvent((cur ? DBPET_KEYDOWN : DBPET_KEYUP), DBP_KEYAXIS_GET(cur+last, b->meta));
			}
		}
		else if (b->evt <= _DBPET_JOY_AXIS_MAX)
		{
			// if meta is 1 or -1, this is a digital input for an axis
			DBP_QueueEvent((DBP_Event_Type)b->evt, (b->meta ? (val ? 32767 : 0) * b->meta : val));
		}
		else
		{
			// if button is pressed, send the _DOWN, otherwise send _UP
			DBP_QueueEvent((DBP_Event_Type)(val ? b->evt : b->evt + 1), b->meta);
		}
		b->lastval = val;
	}
	int16_t mousex = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	int16_t mousey = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	if ((mousex || mousey) && dbp_mouse_input)
	{
		DBP_QueueEvent(DBPET_MOUSEXY, mousex, mousey);
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

	if (dbp_new_timing)
	{
		DBP_ThreadControl(TCM_FINISH_FRAME);

		if (dbp_lowlatency)
		{
			DBP_ThreadControl(TCM_NEXT_FRAME);
			DBP_ThreadControl(TCM_FINISH_FRAME);
		}
	}

	const DBP_Buffer& buf = dbp_buffers[buffer_active];

	if (dbp_new_timing)
	{
		double targetfps = (dbp_force60fps ? 60.0 : render.src.fps);
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

		if (!dbp_lowlatency)
		{
			DBP_ThreadControl(TCM_NEXT_FRAME);
		}

		if (tpfActual)
		{
			if (dbp_perf == DBP_PERF_DETAILED)
				retro_notify(-1500, RETRO_LOG_INFO, "Speed: %4.1f%%, DOS: %dx%d@%4.2ffps, Actual: %4.2ffps, Drawn: %dfps, Cycles: %u"
					#ifdef DBP_ENABLE_WAITSTATS
					", Waits: p%u|f%u|z%u|c%u"
					#endif
					, ((float)tpfTarget / (float)tpfActual * 100), (int)render.src.width, (int)render.src.height, render.src.fps, (1000000.f / tpfActual), tpfDraws, CPU_CycleMax
					#ifdef DBP_ENABLE_WAITSTATS
					, waitPause, waitFinish, waitPaused, waitContinue
					#endif
					);
			else
				retro_notify(-1500, RETRO_LOG_INFO, "Emulation Speed: %4.1f%%",
					((float)tpfTarget / (float)tpfActual * 100));
		}

		// handle timing changes
		if (av_info.timing.fps != targetfps)
		{
			av_info.timing.fps = targetfps;
			environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
		}
	}

	// handle audio/video mode changes
	if (av_info.geometry.base_width != buf.width || av_info.geometry.base_height != buf.height || av_info.geometry.aspect_ratio != buf.ratio)
	{
		log_cb(RETRO_LOG_INFO, "[DOSBOX] Resolution changed %ux%u @ %.3fHz AR: %.5f => %ux%u @ %.3fHz AR: %.5f\n",
			av_info.geometry.base_width, av_info.geometry.base_height, av_info.timing.fps, av_info.geometry.aspect_ratio,
			buf.width, buf.height, av_info.timing.fps, buf.ratio);

		av_info.geometry.base_width = buf.width;
		av_info.geometry.base_height = buf.height;
		av_info.geometry.aspect_ratio = buf.ratio;

		if (!environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av_info)) log_cb(RETRO_LOG_WARN, "[DOSBOX] SET_GEOMETRY failed\n");
	}

	if (dbp_new_timing)
	{
		// submit video and audio
		video_cb(buf.video, buf.width, buf.height, buf.width * 4);
		audio_batch_cb(buf.audio, buf.samples);
	}
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing)
	{
		// submit video
		video_cb(buf.video, buf.width, buf.height, buf.width * 4);

		// process and submit audio
		static Bit32u mix_missed;
		DBP_Buffer& audbuf = (DBP_Buffer&)buf;
		if (!dbp_frame_time) dbp_frame_time = (retro_usec_t)(150000.0 + 500000.0 / render.src.fps);
		audbuf.samples = (uint32_t)(av_info.timing.sample_rate / 1000000.0 * dbp_frame_time + .499999);
		if (audbuf.samples || mix_missed)
		{
			Bit32u mix_samples_need = audbuf.samples;
			audbuf.samples += mix_missed;
			if (audbuf.samples > sizeof(audbuf.audio)/4) audbuf.samples = sizeof(audbuf.audio) / 4;
			if (audbuf.samples > DBP_MIXER_DoneSamplesCount()) audbuf.samples = DBP_MIXER_DoneSamplesCount();
			if (mix_samples_need != audbuf.samples)
			{
				if (dbp_retro_activity < 10 || dbp_timing_tamper || dbp_throttle.mode == RETRO_THROTTLE_FAST_FORWARD) mix_missed = 0;
				else mix_missed += (mix_samples_need - audbuf.samples);
			}
			if (audbuf.samples)
			{
				dbp_audiomutex.Lock();
				MIXER_CallBack(0, (Bit8u*)audbuf.audio, audbuf.samples * 4);
				dbp_audiomutex.Unlock();
				audio_batch_cb(audbuf.audio, audbuf.samples);
			}
		}

		// keep frontend UI thread from running at 100% cpu
		uint32_t this_run = DBP_GetTicks(), run_sleep = dbp_min_sleep - (this_run - dbp_last_run);
		if (run_sleep < dbp_min_sleep) { retro_sleep(run_sleep); this_run += run_sleep; }
		dbp_last_run = this_run;
	}
#endif
}

static bool retro_serialize_all(DBPArchive& ar, bool unlock_thread)
{
	if (dbp_serializemode == DBPSERIALIZE_DISABLED) return false;
	if (dbp_new_timing) DBP_ThreadControl(TCM_PAUSE_FRAME);
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing) DBP_LockThread(true);
#endif
	retro_time_t timeStart = time_cb();
	DBPSerialize_All(ar, (dbp_state == DBPSTATE_RUNNING), dbp_game_running);
	dbp_serialize_time += (Bit32u)(time_cb() - timeStart);
	//log_cb(RETRO_LOG_WARN, "[SERIALIZE] [%d] [%s] %u\n", (dbp_state == DBPSTATE_RUNNING && dbp_game_running), (ar.mode == DBPArchive::MODE_LOAD ? "LOAD" : ar.mode == DBPArchive::MODE_SAVE ? "SAVE" : ar.mode == DBPArchive::MODE_SIZE ? "SIZE" : ar.mode == DBPArchive::MODE_MAXSIZE ? "MAXX" : ar.mode == DBPArchive::MODE_ZERO ? "ZERO" : "???????"), (Bit32u)ar.GetOffset());
	if (dbp_game_running && ar.mode == DBPArchive::MODE_LOAD) dbp_lastmenuticks = DBP_GetTicks(); // force show menu on immediate emulation crash
	if (dbp_new_timing && unlock_thread) DBP_ThreadControl(TCM_RESUME_FRAME);
#ifndef DBP_REMOVE_OLD_TIMING
	if (!dbp_new_timing && unlock_thread) DBP_LockThread(false);
#endif

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
				if (dbp_serializemode != DBPSERIALIZE_REWIND)
					retro_notify(0, RETRO_LOG_ERROR, "%sUnable to %s not running.\nIf using rewind, make sure to modify the related core option.", (ar.mode == DBPArchive::MODE_LOAD ? "Load State Error: " : "Save State Error: "), (ar.mode == DBPArchive::MODE_LOAD ? "load state made while DOS was" : "save state while DOS is"));
				break;
			case DBPArchive::ERR_GAMENOTRUNNING:
				if (dbp_serializemode != DBPSERIALIZE_REWIND)
					retro_notify(0, RETRO_LOG_ERROR, "%sUnable to %s not running.\nIf using rewind, make sure to modify the related core option.", (ar.mode == DBPArchive::MODE_LOAD ? "Load State Error: " : "Save State Error: "), (ar.mode == DBPArchive::MODE_LOAD ? "load state made while game was" : "save state while game is"));
				break;
			case DBPArchive::ERR_WRONGMACHINECONFIG:
				retro_notify(0, RETRO_LOG_ERROR, "%sWrong graphics chip configuration (%s instead of %s)", "Load State Error: ",
					(machine <= MCH_VGA ? machine_names[machine] : "UNKNOWN"), (ar.error_info <= MCH_VGA ? machine_names[ar.error_info] : "UNKNOWN"));
				break;
			case DBPArchive::ERR_WRONGMEMORYCONFIG:
				retro_notify(0, RETRO_LOG_ERROR, "%sWrong memory size configuration (%d MB instead of %d MB)", "Load State Error: ",
					(Bit8u)(MEM_TotalPages() / 256), ar.error_info);
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
	if ((rewind
#ifndef DBP_REMOVE_OLD_TIMING
		|| dbp_lockthreadstate
#endif
	) && dbp_serializesize) return dbp_serializesize;
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
#ifndef DBP_REMOVE_OLD_TIMING
	dbp_overload_count = 0; // don't show overload warning immediately after loading
#endif
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
FILE *fopen_wrap(const char *path, const char *mode) { return (FILE*)fopen_utf8(path, mode); }
#ifndef STATIC_LINKING
#include "libretro-common/compat/fopen_utf8.c"
#include "libretro-common/compat/compat_strl.c"
#include "libretro-common/encodings/encoding_utf.c"
#endif

bool fpath_nocase(char* path, const char* base_dir)
{
	if (!path || !*path) return false;

	// Because we have no idea what the current directory of the frontend is, refuse relative paths
	const char* test = (base_dir && *base_dir ? base_dir : path);
	#ifdef WIN32
	if (test[0] < 'A' || test[0] > 'z' || test[1] != ':' || (test[2] && test[2] != '/' && test[2] != '\\')) return false;
	enum { PATH_ROOTLEN = 2 };
	#else
	if (*test != '/') return false;
	enum { PATH_ROOTLEN = 1 };
	#endif

	std::string subdir;
	if (!base_dir || !*base_dir)
	{
		if (!path[PATH_ROOTLEN]) return false;
		base_dir = subdir.append(path, PATH_ROOTLEN).c_str();
		path += PATH_ROOTLEN;
	}
	struct retro_vfs_interface_info vfs = { 3, NULL };
	if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) || vfs.required_interface_version < 3 || !vfs.iface)
	{
		struct stat test;
		if (subdir.empty() && base_dir) subdir = base_dir;
		if (!subdir.empty() && subdir.back() != '/' && subdir.back() != '\\' && *path != '/' && *path != '\\') subdir += '/';
		return (stat(subdir.append(path).c_str(), &test) == 0);
	}
	for (char* psubdir;; *psubdir = '/', path = psubdir + 1)
	{
		char *next_slash = strchr(path, '/'), *next_bslash = strchr(path, '\\');
		psubdir = (next_slash && (!next_bslash || next_slash < next_bslash) ? next_slash : next_bslash);
		if (psubdir == path) continue;
		if (psubdir) *psubdir = '\0';

		bool found = false;
		struct retro_vfs_dir_handle *dir = vfs.iface->opendir(base_dir, true);
		while (dir && vfs.iface->readdir(dir))
		{
			const char* entry_name = vfs.iface->dirent_get_name(dir);
			if (strcasecmp(entry_name, path)) continue;
			memcpy(path, entry_name, strlen(entry_name));
			found = true;
			break;
		}
		vfs.iface->closedir(dir);
		if (!found || !psubdir) { if (psubdir) *psubdir = '/'; return found; }

		if (subdir.empty()) subdir = base_dir;
		if (subdir.back() != '/') subdir += '/';
		base_dir = subdir.append(path).c_str();
	}
}
