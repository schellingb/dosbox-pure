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

#include <dbp_serialize.h>
#include <stdio.h>
#include <string.h> /* memset, memcpy */
#include <stdarg.h> /* va_list */

// Discard should only be called for MODE_LOAD archives which need to override this function
DBPArchive& DBPArchive::Discard(size_t sz) { DBP_ASSERT(0); return *this; }

void DBPArchive::SerializePointers(void** ptrs, size_t num_ptrs, bool ignore_unknown, size_t num_luts, ...)
{
	if (mode == MODE_SIZE || mode == MODE_MAXSIZE) { SerializeBytes(NULL, num_ptrs); return; }
	Bit8u lutnum = 0, n;
	void* lut[256];
	lut[lutnum++] = NULL;
	va_list ap;
	for (va_start(ap, num_luts); num_luts; num_luts--)
	{
		void** sublut = va_arg(ap, void**);
		for (size_t j = 0; sublut[j]; j++)
		{
			DBP_ASSERT(lutnum != 255);
			lut[lutnum++] = sublut[j];
		}
	}
	va_end(ap);
	for (size_t i = 0; i != num_ptrs; i++)
	{
		if (mode == MODE_SAVE)
		{
			for (n = 0; n != lutnum; n++)
				if (ptrs[i] == lut[n])
					break;
			DBP_ASSERT(ignore_unknown || n != lutnum);
		}
		SerializeByte(&n);
		if (mode != MODE_SAVE && n != lutnum)
			ptrs[i] = lut[n];
	}
}

void DBPArchive::DoExceptionList(void* p, size_t sz, size_t num_exceptions, ...)
{
	va_list ap;
	char *pLast = (char*)p, *pEnd = pLast + sz;
	for (va_start(ap, num_exceptions); num_exceptions; num_exceptions--)
	{
		char* pEx = va_arg(ap, char*);
		size_t szEx = va_arg(ap, size_t);
		DBP_ASSERT(pEx >= pLast && pEx + szEx <= pEnd);
		SerializeBytes(pLast, pEx - pLast);
		pLast = pEx + szEx;
	}
	SerializeBytes(pLast, pEnd - pLast);
}

#if !defined(__SSE2__) && (_M_IX86_FP == 2 || (defined(_M_AMD64) || defined(_M_X64)))
#define __SSE2__ 1
#endif

#if defined(__SSE2__) && __SSE2__
#include <emmintrin.h>
#define DBP_SERIALIZE_SPARSE_TYPE __m128i
#define DBP_SERIALIZE_SPARSE_INIT() __m128i m128izero = _mm_setzero_si128()
#define DBP_SERIALIZE_SPARSE_TEST(x) (_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_load_si128(x), m128izero)) != 0xFFFF)
//// No performance difference with 128-bit neon registers vs. 64-bit data, so disable this for now
//#elif defined(__ARM_NEON__)
//#include <arm_neon.h>
//#define DBP_SERIALIZE_SPARSE_TYPE uint32x4_t
//#define DBP_SERIALIZE_SPARSE_INIT() uint32x4_t a; uint32x2_t b
//#define DBP_SERIALIZE_SPARSE_TEST(x) (a = vld1q_u32((uint32_t*)(x)), b = vorr_u32(vget_low_u32(a), vget_high_u32(a)), vget_lane_u32(vpmax_u32(b, b), 0))
#else
#define DBP_SERIALIZE_SPARSE_TYPE Bit64u
#define DBP_SERIALIZE_SPARSE_INIT()
#define DBP_SERIALIZE_SPARSE_TEST(x) *(x)
#endif

bool DBPArchive::accomodate_delta_encoding;

void DBPArchive::SerializeSparse(void* ptr, size_t sz)
{
	// Performance wise it is faster to read over a large range of memory to find non-zero blocks and only serialize them out
	// than to write out the entire block. We support two modes here 'minimized' which serializes multiple blocks that contain
	// non-zero bytes and 'accomodate for delta encoding' which tries to make sure multiple successive save states share the
	// same data layout to better suit a delta encoded rewind buffer.
	DBP_ASSERT(sz <= 0xFFFFFFFF);
	if (mode == MODE_MAXSIZE)
	{
		SerializeBytes(ptr, sz + 16);
	}
	else if (mode == MODE_SAVE || mode == MODE_SIZE)
	{
		//size_t start_offset = GetOffset();
		Bit8u *ptr_begin = (Bit8u*)ptr, *ptr_end = (Bit8u*)ptr_begin + sz, *from, *to = ptr_begin;
		enum { T_SIZE = sizeof(DBP_SERIALIZE_SPARSE_TYPE), T_MASK = T_SIZE - 1 };
		DBP_SERIALIZE_SPARSE_INIT();
		DBP_SERIALIZE_SPARSE_TYPE *p_start = (DBP_SERIALIZE_SPARSE_TYPE*)(((uintptr_t)ptr + T_MASK) & ~T_MASK);
		DBP_SERIALIZE_SPARSE_TYPE *p_end = p_start + ((ptr_end - (Bit8u*)p_start) / T_SIZE);
		if (accomodate_delta_encoding)
		{
			for (Bit8u *p = ptr_end - 1; p >= (Bit8u *)p_end; p--)
				if (*p) { to = p + 1; goto found_end; }
			for (DBP_SERIALIZE_SPARSE_TYPE *p = p_end - 1; p >= p_start; p--)
				if (DBP_SERIALIZE_SPARSE_TEST(p)) { to = (Bit8u*)(p + 1); goto found_end; }
			for (Bit8u *p = (Bit8u*)p_start - 1; p >= (Bit8u *)ptr_begin; p--)
				if (*p) { to = p + 1; goto found_end; }
			if (to != ptr_begin)
			{
				found_end:
				Bit32u skip = 0, len = (Bit32u)(to - ptr_begin);
				Serialize(skip).Serialize(len).SerializeBytes(ptr_begin, len);
			}
		}
		else
		{
			bool align_hasdata = false;
			for (Bit8u *hdr = ptr_begin; hdr != (Bit8u*)p_start; hdr++) { if (*hdr) { align_hasdata = true; break; } }
			DBP_SERIALIZE_SPARSE_TYPE *p = p_start;
			if (align_hasdata) { from = ptr_begin; goto find_data_end; }

			while (p != p_end)
			{
				for (;;)
				{
					if (DBP_SERIALIZE_SPARSE_TEST(p)) break;
					if (++p == p_end) goto done;
				}
				from = (Bit8u*)p;
				find_data_end:
				Bit32u skip = (Bit32u)(from - to);
				for (;;)
				{
					sameblock:
					if (p == p_end) { to = (Bit8u*)p; break; }
					if (DBP_SERIALIZE_SPARSE_TEST(p++)) continue;
					to = (Bit8u*)p;

					for (Bit32u i = 0; i != 512 && p != p_end; i++)
						if (DBP_SERIALIZE_SPARSE_TEST(p++))
							goto sameblock;
					break;
				}
				Bit32u len = (Bit32u)(to - from);
				Serialize(skip).Serialize(len).SerializeBytes(from, len);
			}
			done:
			from = (Bit8u*)p;
			Bit32u footer_len = (Bit32u)(ptr_end - from);
			bool footer_hasdata = false;
			for (Bit32u i = 0; i != footer_len; i++) { if (from[i]) { footer_hasdata = true; break; } }
			if (footer_hasdata)
			{
				Bit32u skip = (Bit32u)(from - to);
				Serialize(skip).Serialize(footer_len).SerializeBytes(from, footer_len);
			}
		}
		Bit32u zero = 0;
		Serialize(zero).Serialize(zero);
		//printf("    Sparse saved %f MB into %f%%\n", sz/1024./1024., (GetOffset()-start_offset) / (double)sz);
	}
	else
	{
		Bit32u skip, len;
		Bit8u* p = (Bit8u*)ptr;
		for (Serialize(skip).Serialize(len); len; Serialize(skip).Serialize(len))
		{
			memset(p, 0, skip);
			p += skip;
			SerializeBytes(p, len);
			p += len;
			if (had_error) return;
		}
		memset(p, 0, (Bit8u*)ptr + sz - p);
	}
}

DBPArchiveOptional::DBPArchiveOptional(DBPArchive& ar, void* objptr, bool active) : DBPArchive((DBPArchive::EMode)ar.mode), outer(&ar)
{
	version = ar.version, had_error = ar.had_error, warnings = ar.warnings;
	bool state = (objptr && active);
	ar.Serialize(state);
	if (mode == MODE_MAXSIZE) optionality = OPTIONAL_SERIALIZE;
	else if (state) optionality = (objptr ? OPTIONAL_SERIALIZE : OPTIONAL_DISCARD);
	else optionality = ((!objptr || !active) ? OPTIONAL_SKIP : OPTIONAL_RESET);
}

DBPArchiveOptional::~DBPArchiveOptional()
{
	outer->had_error = had_error, outer->warnings |= warnings;
}

DBPArchive& DBPArchiveOptional::SerializeBytes(void* p, size_t sz)
{
	switch (optionality)
	{
		case OPTIONAL_SERIALIZE: outer->SerializeBytes(p, sz); break;
		case OPTIONAL_RESET:     memset(p, 0, sz);             break;
		case OPTIONAL_DISCARD:   outer->Discard(sz);           break;
		case OPTIONAL_SKIP:      DBP_ASSERT(0);                break;
	}
	return *this;
}

DBPArchive& DBPArchiveOptional::SerializeByte(void* p)
{
	switch (optionality)
	{
		case OPTIONAL_SERIALIZE: outer->SerializeByte(p); break;
		case OPTIONAL_RESET:     *(Bit8u*)p = 0;          break;
		case OPTIONAL_DISCARD:   outer->Discard(1);       break;
		case OPTIONAL_SKIP:      DBP_ASSERT(0);           break;
	}
	return *this;
}

size_t DBPArchiveOptional::GetOffset() { return outer->GetOffset(); }

DBPArchive& DBPArchiveReader::SerializeBytes(void* p, size_t sz)
{
	if (ptr + sz <= end) memcpy(p, ptr, sz); else had_error |= ERR_LAYOUT; ptr += sz; return *this;
}

DBPArchive& DBPArchiveWriter::SerializeBytes(void* p, size_t sz)
{
	if (ptr + sz <= end) memcpy(ptr, p, sz); else had_error |= ERR_LAYOUT; ptr += sz; return *this;
}

DBPArchive& DBPArchiveZeroer::SerializeBytes(void* p, size_t sz)
{
	memset(p, 0, sz); return *this;
}

//#define DBP_SERIALIZE_PERF_TEST
#ifdef DBP_SERIALIZE_PERF_TEST
#ifdef _MSC_VER
#include <intrin.h>
#elif defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP == 2)
static Bit64s __rdtsc() { unsigned long lo, hi; asm( "rdtsc" : "=a" (lo), "=d" (hi) );  return( lo | (hi << 32) ); }
#else
#include <sys/time.h>
static Bit64s __rdtsc() { struct timeval tv; gettimeofday(&tv, NULL); return ((Bit64u)tv.tv_sec * 1000000 + tv.tv_usec)<<8; }
#endif
#endif

#include <dosbox.h>
#include <vga.h>
#include <paging.h>

void DBPSerialize_All(DBPArchive& ar, bool dos_running, bool game_running)
{
	#ifdef DBP_SERIALIZE_PERF_TEST
	static Bit64s sum_ticks, avg_ticks, sum_count, avg_size;
	Bit64s from = __rdtsc();
	#endif

	ar.version = 6;
	if (ar.mode != DBPArchive::MODE_ZERO)
	{
		Bit32u magic = 0xD05B5747;
		Bit8u invalid_state = (dos_running ? 0 : 1) | (game_running ? 0 : 2);
		ar << magic << ar.version << invalid_state;
		if (magic != 0xD05B5747) { ar.had_error = DBPArchive::ERR_LAYOUT; return; }
		if (ar.version < 1 || ar.version > 6) { DBP_ASSERT(false); ar.had_error = DBPArchive::ERR_VERSION; return; }
		if (ar.mode == DBPArchive::MODE_LOAD || ar.mode == DBPArchive::MODE_SAVE)
		{
			if (!dos_running  || (invalid_state & 1)) { ar.had_error = DBPArchive::ERR_DOSNOTRUNNING; return; }
			if (!game_running || (invalid_state & 2)) { ar.had_error = DBPArchive::ERR_GAMENOTRUNNING; return; }
		}
	}

	Bitu memory_mb = MEM_TotalPages() / ((1024*1024)/MEM_PAGE_SIZE);
	Bit8u serialized_machine = (Bit8u)machine, current_machine = serialized_machine;
	Bit8u serialized_memory = (Bit8u)(memory_mb < 225 ? memory_mb : (223+memory_mb/128)), current_memory = serialized_memory;
	Bit8u serialized_vgamem = (Bit8u)(vga.vmemsize / (1024*128)), current_vgamem = serialized_vgamem;
	DBP_ASSERT(MEM_TotalPages() == (current_memory < 225 ? current_memory : (current_memory-223)*128)*256);
	ar << serialized_machine << serialized_memory << serialized_vgamem;
	if (ar.mode == DBPArchive::MODE_LOAD)
	{
		if (ar.version < 5 && serialized_memory == 63) serialized_memory = 64; // will be patched in DBPSerialize_Memory
		if (serialized_machine != current_machine) { ar.had_error = DBPArchive::ERR_WRONGMACHINECONFIG; ar.error_info = serialized_machine; return; }
		if (serialized_memory  != current_memory)  { ar.had_error = DBPArchive::ERR_WRONGMEMORYCONFIG;  ar.error_info = serialized_memory;  return; }
		if (serialized_vgamem  != current_vgamem)  { ar.had_error = DBPArchive::ERR_WRONGVGAMEMCONFIG;  ar.error_info = serialized_vgamem;  return; }
	}

	// The switch with __LINE__ cases is a fun way to have all the serialize functions in a list that can easily be reordered in code
	// Small things that have an easily varying size should be put at the end to simplify a delta encoded rewind buffer
	void (*func)(DBPArchive& ar); //const char* func_name;
	#define DBPSERIALIZE_GET_FUNC(FUNC) void FUNC(DBPArchive& ar); func = FUNC; //func_name = #FUNC
	#define DBPSERIALIZE_GET_FVER(FUNC,VER_CHECK) if (!(ar.version VER_CHECK)) continue; DBPSERIALIZE_GET_FUNC(FUNC)
	for (unsigned ln = __LINE__;; ln++)
	{
		switch (ln)
		{
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_DOS         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_CPU         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_EMS         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_XMS         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Timer       ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_CMOS        ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_DMA         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_FPU         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_INT10       ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_IO          ); break;
			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_Mouse,   ==1); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Joystick    ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_VGA         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_VGA_Paradise); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_VGA_Tseng   ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_VGA_XGA     ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Render      ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_SBLASTER    ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_OPL         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_TANDYSOUND  ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_CMS         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_DISNEY      ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Paging      ); break; // irregular size from here on
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Memory      ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_VGA_Draw    ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_VGA_Memory  ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_PIC         ); break; // must be before Keyboard
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Keyboard    ); break; // must be after PIC
			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_Mouse,   >=2); break; // must be after PIC
			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_Drives,  >=3); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Files       ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_GUS         ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_MPU401      ); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_PCSPEAKER   ); break;
			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_Voodoo,  >=5); break;
			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_CDPlayer,>=6); break;
			case __LINE__: goto done; /*return;*/ default: continue;
		}
		size_t old_off = ar.GetOffset();
		func(ar);
		if (ar.had_error) return;
		size_t off = ar.GetOffset(), offcheck = off;
		ar << off;
		//printf("%s: %d\n", func_name, (int)(off - old_off));
		if (ar.mode == DBPArchive::MODE_LOAD && off != offcheck)
		{
			ar.had_error = DBPArchive::ERR_LAYOUT;
			return;
		}
	}
	done:;
	#ifdef DBP_SERIALIZE_PERF_TEST
	if (ar.mode == DBPArchive::MODE_SAVE)
	{
		Bit64s to = __rdtsc();
		sum_ticks += ((to - from) >> 14);
		if (++sum_count > 2) { avg_ticks += sum_ticks; avg_size += ar.GetOffset(); }
		if (((sum_count)%32) == 0) printf("[SAVE] in %u - avg: %u (cur size: %u - avg: %u)\n", (unsigned)sum_ticks, (unsigned)(avg_ticks / (sum_count < 3 ? 1 : sum_count - 2)), (unsigned)ar.GetOffset(), (unsigned)(avg_size / (sum_count < 3 ? 1 : sum_count - 2)));
		sum_ticks = 0;
	}
	#endif
}
