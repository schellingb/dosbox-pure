/* Copyright  (C) 2010-2020 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (features_cpu.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
All symbols functions have been prefixed to make compilation of the core be best compatible
with an environment or platform where the core gets statically linked into a frontend.
*/

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "../include/libretro.h"
#include "../include/retro_timers.h"

#if defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#endif

#ifdef __PSL1GHT__
#include <lv2/systime.h>
#endif

#if defined(_XBOX360)
#include <PPCIntrinsics.h>
#elif !defined(__MACH__) && (defined(__POWERPC__) || defined(__powerpc__) || defined(__ppc__) || defined(__PPC64__) || defined(__powerpc64__))
#ifndef _PPU_INTRINSICS_H
#include <ppu_intrinsics.h>
#endif
#elif defined(_POSIX_MONOTONIC_CLOCK) || defined(ANDROID) || defined(__QNX__) || defined(DJGPP)
/* POSIX_MONOTONIC_CLOCK is not being defined in Android headers despite support being present. */
#include <time.h>
#endif

#if defined(__QNX__) && !defined(CLOCK_MONOTONIC)
#define CLOCK_MONOTONIC 2
#endif

#if defined(PSP)
#include <pspkernel.h>
#endif

#if defined(PSP) || defined(__PSL1GHT__)
#include <sys/time.h>
#endif

#if defined(PSP)
#include <psprtc.h>
#endif

#if defined(VITA)
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#endif

#if defined(ORBIS)
#include <orbis/libkernel.h>
#endif

#if defined(PS2)
#include <ps2sdkapi.h>
#endif

#if !defined(__PSL1GHT__) && defined(__PS3__)
#include <sys/sys_time.h>
#endif

#ifdef GEKKO
#include <ogc/lwp_watchdog.h>
#endif

#ifdef WIIU
/* FROM #include <wiiu/os/time.h> */
#ifdef __cplusplus
extern "C" {
#endif
#define ticks_to_us(ticks)       (((uint64_t)(ticks) * (2*2 * 2*2*2)) / (17 * 13 * 3*3))
typedef int64_t OSTime;
OSTime OSGetSystemTime();
#ifdef __cplusplus
}
#endif
#endif

#if defined(HAVE_LIBNX)
#include <switch.h>
#elif defined(SWITCH)
#include <libtransistor/types.h>
#include <libtransistor/svc.h>
#endif

#if defined(_3DS)
#include <3ds/svc.h>
#include <3ds/os.h>
#include <3ds/services/cfgu.h>
#endif

/* iOS/OSX specific. Lacks clock_gettime(), so implement it. */
#ifdef __MACH__
#include <sys/time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

/**
 * TODO/FIXME: clock_gettime function is part of iOS 10 now
 **/
static int dbp_ra_clock_gettime(int clk_ik, struct timespec *t)
{
   struct timeval now;
   int rv     = gettimeofday(&now, NULL);
   if (rv)
      return rv;
   t->tv_sec  = now.tv_sec;
   t->tv_nsec = now.tv_usec * 1000;
   return 0;
}
#endif

#if defined(__MACH__) && __IPHONE_OS_VERSION_MIN_REQUIRED < 100000
#else
#define dbp_ra_clock_gettime clock_gettime
#endif

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#if defined(BSD) || defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include <string.h>

retro_time_t dbp_cpu_features_get_time_usec(void)
{
#if defined(_WIN32)
   static LARGE_INTEGER freq;
   LARGE_INTEGER count;

   /* Frequency is guaranteed to not change. */
   if (!freq.QuadPart && !QueryPerformanceFrequency(&freq))
      return 0;

   if (!QueryPerformanceCounter(&count))
      return 0;
   return (count.QuadPart / freq.QuadPart * 1000000) + (count.QuadPart % freq.QuadPart * 1000000 / freq.QuadPart);
#elif defined(__PSL1GHT__)
   return sysGetSystemTime();
#elif !defined(__PSL1GHT__) && defined(__PS3__)
   return sys_time_get_system_time();
#elif defined(GEKKO)
   return ticks_to_microsecs(gettime());
#elif defined(WIIU)
   return ticks_to_us(OSGetSystemTime());
#elif defined(SWITCH) || defined(HAVE_LIBNX)
   return (svcGetSystemTick() * 10) / 192;
#elif defined(_3DS)
   return osGetTime() * 1000;
#elif defined(_POSIX_MONOTONIC_CLOCK) || defined(__QNX__) || defined(ANDROID) || defined(__MACH__)
   struct timespec tv;
   if (dbp_ra_clock_gettime(CLOCK_MONOTONIC, &tv) < 0)
      return 0;
   return tv.tv_sec * INT64_C(1000000) + (tv.tv_nsec + 500) / 1000;
#elif defined(EMSCRIPTEN)
   return emscripten_get_now() * 1000;
#elif defined(PS2)
   return ps2_clock() / PS2_CLOCKS_PER_MSEC * 1000;
#elif defined(VITA) || defined(PSP)
   return sceKernelGetSystemTimeWide();
#elif defined(DJGPP)
   return uclock() * 1000000LL / UCLOCKS_PER_SEC;
#elif defined(ORBIS)
   return sceKernelGetProcessTime();
#else
#error "Your platform does not have a timer function implemented in cpu_features_get_time_usec(). Cannot continue."
#endif
}

#if defined(__x86_64__) || defined(__i386__) || defined(__i486__) || defined(__i686__) || (defined(_M_X64) && _MSC_VER > 1310) || (defined(_M_IX86) && _MSC_VER > 1310)
#define CPU_X86
#endif

#if defined(_MSC_VER) && !defined(_XBOX)
#if (_MSC_VER > 1310)
#include <intrin.h>
#endif
#endif

#if defined(__ARM_NEON__)
#if defined(__arm__)
static void dbp_arm_enable_runfast_mode(void)
{
   /* RunFast mode. Enables flush-to-zero and some
    * floating point optimizations. */
   static const unsigned x = 0x04086060;
   static const unsigned y = 0x03000000;
   int r;
   __asm__ volatile(
         "fmrx	%0, fpscr   \n\t" /* r0 = FPSCR */
         "and	%0, %0, %1  \n\t" /* r0 = r0 & 0x04086060 */
         "orr	%0, %0, %2  \n\t" /* r0 = r0 | 0x03000000 */
         "fmxr	fpscr, %0   \n\t" /* FPSCR = r0 */
         : "=r"(r)
         : "r"(x), "r"(y)
        );
}
#endif
#endif

unsigned dbp_cpu_features_get_core_amount(void)
{
#if defined(_WIN32) && !defined(_XBOX)
   /* Win32 */
   SYSTEM_INFO sysinfo;
#if defined(__WINRT__) || defined(WINAPI_FAMILY) && WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
   GetNativeSystemInfo(&sysinfo);
#else
   GetSystemInfo(&sysinfo);
#endif
   return sysinfo.dwNumberOfProcessors;
#elif defined(GEKKO)
   return 1;
#elif defined(PSP) || defined(PS2)
   return 1;
#elif defined(__PSL1GHT__) || !defined(__PSL1GHT__) && defined(__PS3__)
   return 1; /* Only one PPU, SPUs don't really count */
#elif defined(VITA)
   return 4;
#elif defined(HAVE_LIBNX) || defined(SWITCH)
   return 4;
#elif defined(_3DS)
   u8 device_model = 0xFF;
   CFGU_GetSystemModel(&device_model);/*(0 = O3DS, 1 = O3DSXL, 2 = N3DS, 3 = 2DS, 4 = N3DSXL, 5 = N2DSXL)*/
   switch (device_model)
   {
		case 0:
		case 1:
		case 3:
			/*Old 3/2DS*/
			return 2;

		case 2:
		case 4:
		case 5:
			/*New 3/2DS*/
			return 4;

		default:
			/*Unknown Device Or Check Failed*/
			break;
   }
   return 1;
#elif defined(WIIU)
   return 3;
#elif defined(_SC_NPROCESSORS_ONLN)
   /* Linux, most UNIX-likes. */
   long ret = sysconf(_SC_NPROCESSORS_ONLN);
   if (ret <= 0)
      return (unsigned)1;
   return (unsigned)ret;
#elif defined(BSD) || defined(__APPLE__)
   /* BSD */
   /* Copypasta from stackoverflow, dunno if it works. */
   int num_cpu = 0;
   int mib[4];
   size_t len = sizeof(num_cpu);

   mib[0] = CTL_HW;
   mib[1] = HW_AVAILCPU;
   sysctl(mib, 2, &num_cpu, &len, NULL, 0);
   if (num_cpu < 1)
   {
      mib[1] = HW_NCPU;
      sysctl(mib, 2, &num_cpu, &len, NULL, 0);
      if (num_cpu < 1)
         num_cpu = 1;
   }
   return num_cpu;
#elif defined(_XBOX360)
   return 3;
#else
   /* No idea, assume single core. */
   return 1;
#endif
}
