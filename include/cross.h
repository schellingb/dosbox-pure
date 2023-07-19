/*
 *  Copyright (C) 2002-2021  The DOSBox Team
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


#ifndef DOSBOX_CROSS_H
#define DOSBOX_CROSS_H

#ifndef DOSBOX_DOSBOX_H
#include "dosbox.h"
#endif

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>

#if defined (_MSC_VER)						/* MS Visual C++ */
#include <direct.h>
#include <io.h>
#define LONGTYPE(a) a##i64
#if _MSC_VER >= 1900 // Pre-MSVC 2015 compilers don't implement snprintf, vsnprintf in a cross-platform manner
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#else
#if _MSC_VER <= 1310
#error _vscprintf and _vsnprintf_s not available
#endif
#define snprintf c99_snprintf
#define vsnprintf c99_vsnprintf
static inline int c99_vsnprintf(char *s, size_t sz, const char *fmt, va_list ap)
{
	if (!sz) return _vscprintf(fmt, ap);
	int count = _vsnprintf_s(s, sz, sz - 1, fmt, ap);
	return (count > -1 ? count : _vscprintf(fmt, ap));
}
static inline int c99_snprintf(char *s, size_t len, const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt); int count = c99_vsnprintf(s, len, fmt, ap); va_end(ap); return count;
}
#endif
#else										/* LINUX / GCC */
#include <dirent.h>
#include <unistd.h>
#define LONGTYPE(a) a##LL
#endif

#define CROSS_LEN 512						/* Maximum filename size */


#if defined (WIN32) || defined (OS2)				/* Win 32 & OS/2*/
#define CROSS_FILENAME(blah) 
#define CROSS_FILESPLIT '\\'
#ifdef C_DBP_USE_SDL
#define F_OK 0
#else
#ifndef F_OK
#define F_OK 0
#endif
#endif
#else
#define	CROSS_FILENAME(blah) strreplace(blah,'\\','/')
#define CROSS_FILESPLIT '/'
#endif

#define CROSS_NONE	0
#define CROSS_FILE	1
#define CROSS_DIR	2
#if defined (WIN32)
// DBP: Fix warning when building with MSYS2
#ifdef ftruncate
#undef ftruncate
#endif
#define ftruncate(blah,blah2) chsize(blah,blah2)
#endif

//Solaris maybe others
#if defined (DB_HAVE_NO_POWF)
#include <math.h>
static inline float powf (float x, float y) { return (float) pow (x,y); }
#endif

#ifdef GEKKO
/* With Wii the file/dir is considered always accessible if it exists */
static int wii_access (const char *pathname, int mode)
{
	struct stat st;

	if (stat(pathname, &st) < 0)
		return -1;
	return 0;
}
#define access wii_access
#endif

class Cross {
public:
#if defined(C_DBP_ENABLE_CONFIG_PROGRAM) || defined(C_DBP_ENABLE_CAPTURE) || defined(C_OPENGL)
	static void GetPlatformConfigDir(std::string& in);
	static void GetPlatformConfigName(std::string& in);
	static void CreatePlatformConfigDir(std::string& in);
#endif
	static void ResolveHomedir(std::string & temp_line);
#ifdef C_DBP_ENABLE_CAPTURE
	static void CreateDir(std::string const& temp);
#endif
	static bool IsPathAbsolute(std::string const& in);
};


#if defined (WIN32)

#define WIN32_LEAN_AND_MEAN        // Exclude rarely-used stuff from 
#include <windows.h>

typedef struct dir_struct {
	HANDLE          handle;
	char            base_path[MAX_PATH+4];
	WIN32_FIND_DATA search_data;
} dir_information;

#else

//#include <sys/types.h> //Included above
#include <dirent.h>

typedef struct dir_struct { 
	DIR*  dir;
	char base_path[CROSS_LEN];
} dir_information;

#endif

dir_information* open_directory(const char* dirname);
bool read_directory_first(dir_information* dirp, char* entry_name, bool& is_directory);
bool read_directory_next(dir_information* dirp, char* entry_name, bool& is_directory);
void close_directory(dir_information* dirp);

FILE *fopen_wrap(const char *path, const char *mode);

#ifdef C_DBP_HAVE_FPATH_NOCASE
// Check if path exists, will fix case in path
// Returns true if file exists, otherwise path can be partially modified
bool fpath_nocase(char* path);
#endif

//DBP: Use 64-bit fseek and ftell (based on libretro-common/vfs/vfs_implementation.c)
#if defined(_MSC_VER) && _MSC_VER >= 1400 // VC2005 and up have a special 64-bit fseek
#define fseek_wrap(fp, offset, whence) _fseeki64(fp, (__int64)offset, whence)
#define ftell_wrap(fp) _ftelli64(fp)
#elif defined(HAVE_64BIT_OFFSETS) || (defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE - 0) >= 200112) || (defined(__POSIX_VISIBLE) && __POSIX_VISIBLE >= 200112) || (defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112) || __USE_LARGEFILE || (defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64)
#define fseek_wrap(fp, offset, whence) fseeko(fp, (off_t)offset, whence)
#define ftell_wrap(fp) ftello(fp)
#else
#define fseek_wrap(fp, offset, whence) fseek(fp, (long)offset, whence)
#define ftell_wrap(fp) ftell(fp)
#endif

#endif
