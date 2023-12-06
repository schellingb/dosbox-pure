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

#ifndef DOSBOX_DBP_SERIALIZE_H
#define DOSBOX_DBP_SERIALIZE_H

#include "config.h"
#include <stddef.h> /* size_t */

// Save state support
// Based on patches from ZenJu & tikalat with additional improvements by ykhwong and bruenor41
// Using a coding concept borrowed from Unreal Engine 4 serialization (both loading and saving same code path)

struct DBPArchive
{
	enum EMode : Bit8u
	{
		MODE_LOAD,
		MODE_SAVE,
		MODE_SIZE,
		MODE_MAXSIZE,
		MODE_ZERO,
	};

	DBPArchive(EMode _mode) : mode(_mode), flags(FLAG_NONE), had_error(ERR_NONE), warnings(WARN_NONE) {}
	virtual DBPArchive& SerializeByte(void* p) = 0;
	virtual DBPArchive& SerializeBytes(void* p, size_t sz) = 0;
	virtual DBPArchive& Discard(size_t sz);
	virtual size_t GetOffset() = 0;
	template <typename T> DBPArchive& Serialize(T* v); // undefined, can't serialize pointer
	template <typename T> INLINE DBPArchive& Serialize(T& v) { return SerializeBytes(&v, sizeof(v)); }
	template <typename T, size_t N> INLINE DBPArchive& SerializeArray(T(& v)[N]) { return SerializeBytes(v, sizeof(v)); }
	void SerializeSparse(void* p, size_t sz);
	void SerializePointers(void** ptrs, size_t num_ptrs, bool ignore_unknown, size_t num_luts, ...);
	void DoExceptionList(void* p, size_t sz, size_t num_exceptions, ...);
	template <typename T, typename X1> INLINE DBPArchive& SerializeExcept(T& v, X1& x1) { DoExceptionList(&v, sizeof(v), 1, &x1, sizeof(x1)); return *this; }
	template <typename T, typename X1, typename X2> INLINE DBPArchive& SerializeExcept(T& v, X1& x1, X2& x2) { DoExceptionList(&v, sizeof(v), 2, &x1, sizeof(x1), &x2, sizeof(x2)); return *this; }
	template <typename T, typename X1, typename X2, typename X3> INLINE DBPArchive& SerializeExcept(T& v, X1& x1, X2& x2, X3& x3) { DoExceptionList(&v, sizeof(v), 3, &x1, sizeof(x1), &x2, sizeof(x2), &x3, sizeof(x3)); return *this; }

	enum EError : Bit8u
	{
		ERR_NONE,
		ERR_LAYOUT,
		ERR_VERSION,
		ERR_DOSNOTRUNNING,
		ERR_GAMENOTRUNNING,
		ERR_WRONGMACHINECONFIG,
		ERR_WRONGMEMORYCONFIG,
		ERR_WRONGVGAMEMCONFIG,
	};
	enum EWarning : Bit8u
	{
		WARN_NONE         = 0,
		WARN_WRONGDRIVES  = 1<<0,
		WARN_WRONGDEVICES = 1<<1,
		WARN_WRONGPROGRAM = 1<<2,
	};
	enum EFlag : Bit8u
	{
		FLAG_NONE            = 0,
		FLAG_NORESETINPUT = 1<<0,
	};

	Bit8u mode, version, flags, had_error, warnings, error_info;

	// If this is set to true, the serializer will attempt have the same
	// output size and data layout on successively stored states.
	static bool accomodate_delta_encoding;
};

// We could use an endian swapping serialize function but it is unlikely this code will ever run on big endian
// And even if, the serialized save states will not be compatible between different machines using different cpu cores
// Handling endianness would mean also needing to handle struct bit fields and unions properly

INLINE DBPArchive& operator<<(DBPArchive& ar, bool&   i) { return ar.SerializeByte(&i); }
INLINE DBPArchive& operator<<(DBPArchive& ar, Bit8u&  i) { return ar.SerializeByte(&i); }
INLINE DBPArchive& operator<<(DBPArchive& ar, Bit8s&  i) { return ar.SerializeByte(&i); }
INLINE DBPArchive& operator<<(DBPArchive& ar, double& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar, float&  i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar, Bit16u& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar, Bit16s& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar,   signed       int& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar, unsigned       int& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar,   signed      long& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar, unsigned      long& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar,   signed long long& i) { return ar.SerializeBytes(&i, sizeof(i)); }
INLINE DBPArchive& operator<<(DBPArchive& ar, unsigned long long& i) { return ar.SerializeBytes(&i, sizeof(i)); }

#define DBP_SERIALIZE_SET_POINTER_LIST(TYPE, MODULE, ...) TYPE DBPSerialize##TYPE##MODULE##Ptrs[] = { __VA_ARGS__, (TYPE)0 }
#define DBP_SERIALIZE_GET_POINTER_LIST(TYPE, MODULE) DBPSerialize##TYPE##MODULE##Ptrs
#define DBP_SERIALIZE_STATIC_POINTER_LIST(TYPE, MODULE, ...) static TYPE DBPSerialize##TYPE##MODULE##Ptrs[] = { __VA_ARGS__, (TYPE)0 }
#define DBP_SERIALIZE_EXTERN_POINTER_LIST(TYPE, MODULE) extern TYPE DBPSerialize##TYPE##MODULE##Ptrs[]

struct DBPArchiveOptional : public DBPArchive
{
	DBPArchiveOptional(DBPArchive& ar, void* objptr, bool active = true);
	DBPArchiveOptional(DBPArchive& ar_outer, class MixerChannel* chan);
	~DBPArchiveOptional();
	virtual DBPArchive& SerializeByte(void* p);
	virtual DBPArchive& SerializeBytes(void* p, size_t sz);
	virtual size_t GetOffset();
	INLINE bool IsSkip() { return optionality == OPTIONAL_SKIP; }
	INLINE bool IsReset() { return optionality == OPTIONAL_RESET; }
	INLINE bool IsDiscard() { return optionality == OPTIONAL_DISCARD; }
	private: DBPArchive* outer; enum { OPTIONAL_SERIALIZE, OPTIONAL_RESET, OPTIONAL_DISCARD, OPTIONAL_SKIP } optionality;
};

struct DBPArchiveReader : DBPArchive
{
	DBPArchiveReader(const void* _ptr, size_t _sz) : DBPArchive(DBPArchive::MODE_LOAD), start((const Bit8u*)_ptr), end(start+_sz), ptr(start) {}
	virtual DBPArchive& SerializeByte(void* p) { if (ptr < end) *(Bit8u*)p = *(ptr++); else had_error |= ERR_LAYOUT; return *this; }
	virtual DBPArchive& SerializeBytes(void* p, size_t sz);
	virtual DBPArchive& Discard(size_t sz) { if (ptr + sz > end) had_error |= ERR_LAYOUT; ptr += sz; return *this; }
	virtual size_t GetOffset() { return (ptr - start); }
	const Bit8u *start, *end, *ptr;
};

struct DBPArchiveWriter : DBPArchive
{
	DBPArchiveWriter(void* _ptr, size_t _sz) : DBPArchive(DBPArchive::MODE_SAVE), start((Bit8u*)_ptr), end(start+_sz), ptr(start) {}
	virtual DBPArchive& SerializeByte(void* p) { if (ptr < end) *(ptr++) = *(Bit8u*)p; else had_error |= ERR_LAYOUT; return *this; }
	virtual DBPArchive& SerializeBytes(void* p, size_t sz);
	virtual size_t GetOffset() { return (ptr - start); }
	Bit8u *start, *end, *ptr;
};

struct DBPArchiveCounter : DBPArchive
{
	DBPArchiveCounter(bool count_maxsize = false) : DBPArchive(count_maxsize ? DBPArchive::MODE_MAXSIZE : DBPArchive::MODE_SIZE), count(0) {}
	virtual DBPArchive& SerializeByte(void* p) { count++; return *this; }
	virtual DBPArchive& SerializeBytes(void* p, size_t sz) { count += sz; return *this; }
	virtual size_t GetOffset() { return count; }
	size_t count;
};

struct DBPArchiveZeroer : DBPArchive
{
	DBPArchiveZeroer() : DBPArchive(DBPArchive::MODE_ZERO) {}
	virtual DBPArchive& SerializeByte(void* p) { *(Bit8u*)p = 0; return *this; }
	virtual DBPArchive& SerializeBytes(void* p, size_t sz);
	virtual size_t GetOffset() { return 0; }
};

void DBPSerialize_All(DBPArchive& ar, bool dos_running = true, bool game_running = true);

#endif
