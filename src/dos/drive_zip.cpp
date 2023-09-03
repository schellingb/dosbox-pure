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

#include "dosbox.h"
#include "dos_inc.h"
#include "drives.h"
#include "inout.h"

#include <vector>

struct miniz
{
	// BASED ON MINIZ
	// miniz.c v1.15 - public domain deflate
	// Rich Geldreich <richgel99@gmail.com>, last updated Oct. 13, 2013

	typedef unsigned char mz_uint8;
	typedef signed short mz_int16;
	typedef unsigned short mz_uint16;
	typedef unsigned int mz_uint32;
	typedef unsigned int mz_uint;
	#if defined(_MSC_VER)
	typedef unsigned __int64 mz_uint64;
	#else
	typedef unsigned long long mz_uint64;
	#endif

	#define MZ_MAX(a,b) (((a)>(b))?(a):(b))
	#define MZ_MIN(a,b) (((a)<(b))?(a):(b))
	#define MZ_CLEAR_OBJ(obj) memset(&(obj), 0, sizeof(obj))

	#define MZ_READ_LE16(p) ((miniz::mz_uint16)(((const miniz::mz_uint8 *)(p))[0]) | ((miniz::mz_uint16)(((const miniz::mz_uint8 *)(p))[1]) << 8U))
	#define MZ_READ_LE32(p) ((miniz::mz_uint32)(((const miniz::mz_uint8 *)(p))[0]) | ((miniz::mz_uint32)(((const miniz::mz_uint8 *)(p))[1]) << 8U) | ((miniz::mz_uint32)(((const miniz::mz_uint8 *)(p))[2]) << 16U) | ((miniz::mz_uint32)(((const miniz::mz_uint8 *)(p))[3]) << 24U))
	#define MZ_READ_LE64(p) ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[0]) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[1]) << 8U) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[2]) << 16U) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[3]) << 24U) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[4]) << 32U) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[5]) << 40U) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[6]) << 48U) | ((miniz::mz_uint64)(((const miniz::mz_uint8 *)(p))[7]) << 56U))

	// Set MINIZ_HAS_64BIT_REGISTERS to 1 if operations on 64-bit integers are reasonably fast (and don't involve compiler generated calls to helper functions).
	#if defined(_M_X64) || defined(_WIN64) || defined(__MINGW64__) || defined(_LP64) || defined(__LP64__) || defined(__ia64__) || defined(__x86_64__)
	#define MINIZ_HAS_64BIT_REGISTERS 1
	#else
	#define MINIZ_HAS_64BIT_REGISTERS 0
	#endif

	enum
	{
		// Decompression flags used by tinfl_decompress().
		TINFL_FLAG_HAS_MORE_INPUT = 2,                // If set, there are more input bytes available beyond the end of the supplied input buffer. If clear, the input buffer contains all remaining input.
		TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4, // If set, the output buffer is large enough to hold the entire decompressed stream. If clear, the output buffer is at least the size of the dictionary (typically 32KB).

		// Max size of read buffer.
		MZ_ZIP_MAX_IO_BUF_SIZE = 16*1024, // Was 64*1024 originally (though max size readable through DOS_File would be 0xFFFF).

		// Max size of LZ dictionary (output buffer).
		TINFL_LZ_DICT_SIZE = 32*1024, // fixed for zip

		// Internal/private bits follow.
		TINFL_MAX_HUFF_TABLES = 3, TINFL_MAX_HUFF_SYMBOLS_0 = 288, TINFL_MAX_HUFF_SYMBOLS_1 = 32, TINFL_MAX_HUFF_SYMBOLS_2 = 19,
		TINFL_FAST_LOOKUP_BITS = 10, TINFL_FAST_LOOKUP_SIZE = 1 << TINFL_FAST_LOOKUP_BITS,

		// Number coroutine states consecutively
		TINFL_STATE_INDEX_BLOCK_BOUNDRY = 1,
		TINFL_STATE_3 , TINFL_STATE_5 , TINFL_STATE_6 , TINFL_STATE_7 , TINFL_STATE_51, TINFL_STATE_52, 
		TINFL_STATE_9 , TINFL_STATE_38, TINFL_STATE_11, TINFL_STATE_14, TINFL_STATE_16, TINFL_STATE_18,
		TINFL_STATE_23, TINFL_STATE_24, TINFL_STATE_25, TINFL_STATE_26, TINFL_STATE_27, TINFL_STATE_53,
		TINFL_STATE_END
	};

	// Return status.
	enum tinfl_status
	{
		TINFL_STATUS_BAD_PARAM = -3,
		TINFL_STATUS_FAILED = -1,
		TINFL_STATUS_DONE = 0,
		TINFL_STATUS_NEEDS_MORE_INPUT = 1,
		TINFL_STATUS_HAS_MORE_OUTPUT = 2,
	};

	#if MINIZ_HAS_64BIT_REGISTERS
	typedef mz_uint64 tinfl_bit_buf_t;
	#else
	typedef mz_uint32 tinfl_bit_buf_t;
	#endif

	struct tinfl_huff_table
	{
		mz_int16 m_look_up[TINFL_FAST_LOOKUP_SIZE];
		mz_int16 m_tree[TINFL_MAX_HUFF_SYMBOLS_0 * 2];
		mz_uint8 m_code_size[TINFL_MAX_HUFF_SYMBOLS_0];
	};

	struct tinfl_decompressor
	{
		tinfl_huff_table m_tables[TINFL_MAX_HUFF_TABLES];
		mz_uint32 m_state, m_num_bits, m_final, m_type, m_dist, m_counter, m_num_extra, m_table_sizes[TINFL_MAX_HUFF_TABLES];
		tinfl_bit_buf_t m_bit_buf;
		size_t m_dist_from_out_buf_start;
		mz_uint8 m_raw_header[4], m_len_codes[TINFL_MAX_HUFF_SYMBOLS_0 + TINFL_MAX_HUFF_SYMBOLS_1 + 137];
	};

	// Initializes the decompressor to its initial state.
	static void tinfl_init(tinfl_decompressor *r) { r->m_state = 0; }

	// Main low-level decompressor coroutine function. This is the only function actually needed for decompression. All the other functions are just high-level helpers for improved usability.
	// This is a universal API, i.e. it can be used as a building block to build any desired higher level decompression API. In the limit case, it can be called once per every byte input or output.
	static tinfl_status tinfl_decompress(tinfl_decompressor *r, const mz_uint8 *pIn_buf_next, mz_uint32 *pIn_buf_size, mz_uint8 *pOut_buf_start, mz_uint8 *pOut_buf_next, mz_uint32 *pOut_buf_size, const mz_uint32 decomp_flags)
	{
		// An attempt to work around MSVC's spammy "warning C4127: conditional expression is constant" message.
		#ifdef _MSC_VER
		#define TINFL_MACRO_END while (0, 0)
		#else
		#define TINFL_MACRO_END while (0)
		#endif

		#define TINFL_MEMCPY(d, s, l) memcpy(d, s, l)
		#define TINFL_MEMSET(p, c, l) memset(p, c, l)

		#define TINFL_CR_BEGIN switch(r->m_state) { case 0:
		#define TINFL_CR_RETURN(state_index, result) do { status = result; r->m_state = state_index; goto common_exit; case state_index:; } TINFL_MACRO_END
		#define TINFL_CR_RETURN_FOREVER(state_index, result) do { status = result; r->m_state = TINFL_STATE_END; goto common_exit; } TINFL_MACRO_END
		#define TINFL_CR_FINISH }

		// TODO: If the caller has indicated that there's no more input, and we attempt to read beyond the input buf, then something is wrong with the input because the inflator never
		// reads ahead more than it needs to. Currently TINFL_GET_BYTE() pads the end of the stream with 0's in this scenario.
		#define TINFL_GET_BYTE(state_index, c) do { \
			if (pIn_buf_cur >= pIn_buf_end) { \
				for ( ; ; ) { \
					if (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) { \
						TINFL_CR_RETURN(state_index, TINFL_STATUS_NEEDS_MORE_INPUT); \
						if (pIn_buf_cur < pIn_buf_end) { \
							c = *pIn_buf_cur++; \
							break; \
						} \
					} else { \
						c = 0; \
						break; \
					} \
				} \
			} else c = *pIn_buf_cur++; } TINFL_MACRO_END

		#define TINFL_NEED_BITS(state_index, n) do { mz_uint c; TINFL_GET_BYTE(state_index, c); bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); num_bits += 8; } while (num_bits < (mz_uint)(n))
		#define TINFL_SKIP_BITS(state_index, n) do { if (num_bits < (mz_uint)(n)) { TINFL_NEED_BITS(state_index, n); } bit_buf >>= (n); num_bits -= (n); } TINFL_MACRO_END
		#define TINFL_GET_BITS(state_index, b, n) do { if (num_bits < (mz_uint)(n)) { TINFL_NEED_BITS(state_index, n); } b = bit_buf & ((1 << (n)) - 1); bit_buf >>= (n); num_bits -= (n); } TINFL_MACRO_END

		// TINFL_HUFF_BITBUF_FILL() is only used rarely, when the number of bytes remaining in the input buffer falls below 2.
		// It reads just enough bytes from the input stream that are needed to decode the next Huffman code (and absolutely no more). It works by trying to fully decode a
		// Huffman code by using whatever bits are currently present in the bit buffer. If this fails, it reads another byte, and tries again until it succeeds or until the
		// bit buffer contains >=15 bits (deflate's max. Huffman code size).
		#define TINFL_HUFF_BITBUF_FILL(state_index, pHuff) \
			do { \
				temp = (pHuff)->m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]; \
				if (temp >= 0) { \
					code_len = temp >> 9; \
					if ((code_len) && (num_bits >= code_len)) \
					break; \
				} else if (num_bits > TINFL_FAST_LOOKUP_BITS) { \
					 code_len = TINFL_FAST_LOOKUP_BITS; \
					 do { \
							temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; \
					 } while ((temp < 0) && (num_bits >= (code_len + 1))); if (temp >= 0) break; \
				} TINFL_GET_BYTE(state_index, c); bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); num_bits += 8; \
			} while (num_bits < 15);

		// TINFL_HUFF_DECODE() decodes the next Huffman coded symbol. It's more complex than you would initially expect because the zlib API expects the decompressor to never read
		// beyond the final byte of the deflate stream. (In other words, when this macro wants to read another byte from the input, it REALLY needs another byte in order to fully
		// decode the next Huffman code.) Handling this properly is particularly important on raw deflate (non-zlib) streams, which aren't followed by a byte aligned adler-32.
		// The slow path is only executed at the very end of the input buffer.
		#define TINFL_HUFF_DECODE(state_index, sym, pHuff) do { \
			int temp; mz_uint code_len, c; \
			if (num_bits < 15) { \
				if ((pIn_buf_end - pIn_buf_cur) < 2) { \
					 TINFL_HUFF_BITBUF_FILL(state_index, pHuff); \
				} else { \
					 bit_buf |= (((tinfl_bit_buf_t)pIn_buf_cur[0]) << num_bits) | (((tinfl_bit_buf_t)pIn_buf_cur[1]) << (num_bits + 8)); pIn_buf_cur += 2; num_bits += 16; \
				} \
			} \
			if ((temp = (pHuff)->m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0) \
				code_len = temp >> 9, temp &= 511; \
			else { \
				code_len = TINFL_FAST_LOOKUP_BITS; do { temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; } while (temp < 0); \
			} sym = temp; bit_buf >>= code_len; num_bits -= code_len; } TINFL_MACRO_END

		static const int s_length_base[31] = { 3,4,5,6,7,8,9,10,11,13, 15,17,19,23,27,31,35,43,51,59, 67,83,99,115,131,163,195,227,258,0,0 };
		static const int s_length_extra[31]= { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };
		static const int s_dist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193, 257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};
		static const int s_dist_extra[32] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
		static const mz_uint8 s_length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
		static const int s_min_table_sizes[3] = { 257, 1, 4 };

		tinfl_status status = TINFL_STATUS_FAILED; mz_uint32 num_bits, dist, counter, num_extra; tinfl_bit_buf_t bit_buf;
		const mz_uint8 *pIn_buf_cur = pIn_buf_next, *const pIn_buf_end = pIn_buf_next + *pIn_buf_size, *const pIn_buf_end_m_4 = pIn_buf_end - 4;
		mz_uint8 *pOut_buf_cur = pOut_buf_next, *const pOut_buf_end = pOut_buf_next + *pOut_buf_size, *const pOut_buf_end_m_2 = pOut_buf_end - 2;
		size_t out_buf_size_mask = (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ? (size_t)-1 : ((pOut_buf_next - pOut_buf_start) + *pOut_buf_size) - 1, dist_from_out_buf_start;

		mz_int16* r_tables_0_look_up = r->m_tables[0].m_look_up;

		// Ensure the output buffer's size is a power of 2, unless the output buffer is large enough to hold the entire output file (in which case it doesn't matter).
		if (((out_buf_size_mask + 1) & out_buf_size_mask) || (pOut_buf_next < pOut_buf_start)) { *pIn_buf_size = *pOut_buf_size = 0; return TINFL_STATUS_BAD_PARAM; }

		num_bits = r->m_num_bits; bit_buf = r->m_bit_buf; dist = r->m_dist; counter = r->m_counter; num_extra = r->m_num_extra; dist_from_out_buf_start = r->m_dist_from_out_buf_start;
		TINFL_CR_BEGIN

		bit_buf = num_bits = dist = counter = num_extra = 0;

		do
		{
			if (pIn_buf_cur - pIn_buf_next) { TINFL_CR_RETURN(TINFL_STATE_INDEX_BLOCK_BOUNDRY, TINFL_STATUS_HAS_MORE_OUTPUT); }
			TINFL_GET_BITS(TINFL_STATE_3, r->m_final, 3); r->m_type = r->m_final >> 1;
			if (r->m_type == 0)
			{
				TINFL_SKIP_BITS(TINFL_STATE_5, num_bits & 7);
				for (counter = 0; counter < 4; ++counter) { if (num_bits) TINFL_GET_BITS(TINFL_STATE_6, r->m_raw_header[counter], 8); else TINFL_GET_BYTE(TINFL_STATE_7, r->m_raw_header[counter]); }
				if ((counter = (r->m_raw_header[0] | (r->m_raw_header[1] << 8))) != (mz_uint)(0xFFFF ^ (r->m_raw_header[2] | (r->m_raw_header[3] << 8)))) { TINFL_CR_RETURN_FOREVER(39, TINFL_STATUS_FAILED); }
				while ((counter) && (num_bits))
				{
					TINFL_GET_BITS(TINFL_STATE_51, dist, 8);
					while (pOut_buf_cur >= pOut_buf_end) { TINFL_CR_RETURN(TINFL_STATE_52, TINFL_STATUS_HAS_MORE_OUTPUT); }
					*pOut_buf_cur++ = (mz_uint8)dist;
					counter--;
				}
				while (counter)
				{
					size_t n; while (pOut_buf_cur >= pOut_buf_end) { TINFL_CR_RETURN(TINFL_STATE_9, TINFL_STATUS_HAS_MORE_OUTPUT); }
					while (pIn_buf_cur >= pIn_buf_end)
					{
						if (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT)
						{
							TINFL_CR_RETURN(TINFL_STATE_38, TINFL_STATUS_NEEDS_MORE_INPUT);
						}
						else
						{
							TINFL_CR_RETURN_FOREVER(40, TINFL_STATUS_FAILED);
						}
					}
					n = MZ_MIN(MZ_MIN((size_t)(pOut_buf_end - pOut_buf_cur), (size_t)(pIn_buf_end - pIn_buf_cur)), counter);
					TINFL_MEMCPY(pOut_buf_cur, pIn_buf_cur, n); pIn_buf_cur += n; pOut_buf_cur += n; counter -= (mz_uint)n;
				}
			}
			else if (r->m_type == 3)
			{
				TINFL_CR_RETURN_FOREVER(10, TINFL_STATUS_FAILED);
			}
			else
			{
				if (r->m_type == 1)
				{
					mz_uint8 *p = r->m_tables[0].m_code_size; mz_uint i;
					r->m_table_sizes[0] = 288; r->m_table_sizes[1] = 32; TINFL_MEMSET(r->m_tables[1].m_code_size, 5, 32);
					for ( i = 0; i <= 143; ++i) *p++ = 8; for ( ; i <= 255; ++i) *p++ = 9; for ( ; i <= 279; ++i) *p++ = 7; for ( ; i <= 287; ++i) *p++ = 8;
				}
				else
				{
					for (counter = 0; counter < 3; counter++) { TINFL_GET_BITS(TINFL_STATE_11, r->m_table_sizes[counter], "\05\05\04"[counter]); r->m_table_sizes[counter] += s_min_table_sizes[counter]; }
					MZ_CLEAR_OBJ(r->m_tables[2].m_code_size); for (counter = 0; counter < r->m_table_sizes[2]; counter++) { mz_uint s; TINFL_GET_BITS(TINFL_STATE_14, s, 3); r->m_tables[2].m_code_size[s_length_dezigzag[counter]] = (mz_uint8)s; }
					r->m_table_sizes[2] = 19;
				}
				for ( ; (int)r->m_type >= 0; r->m_type--)
				{
					int tree_next, tree_cur; tinfl_huff_table *pTable;
					mz_uint i, j, used_syms, total, sym_index, next_code[17], total_syms[16]; pTable = &r->m_tables[r->m_type]; MZ_CLEAR_OBJ(total_syms); MZ_CLEAR_OBJ(pTable->m_look_up); MZ_CLEAR_OBJ(pTable->m_tree);
					for (i = 0; i < r->m_table_sizes[r->m_type]; ++i) total_syms[pTable->m_code_size[i]]++;
					used_syms = 0, total = 0; next_code[0] = next_code[1] = 0;
					for (i = 1; i <= 15; ++i) { used_syms += total_syms[i]; next_code[i + 1] = (total = ((total + total_syms[i]) << 1)); }
					if ((65536 != total) && (used_syms > 1))
					{
						TINFL_CR_RETURN_FOREVER(35, TINFL_STATUS_FAILED);
					}
					for (tree_next = -1, sym_index = 0; sym_index < r->m_table_sizes[r->m_type]; ++sym_index)
					{
						mz_uint rev_code = 0, l, cur_code, code_size = pTable->m_code_size[sym_index]; if (!code_size) continue;
						cur_code = next_code[code_size]++; for (l = code_size; l > 0; l--, cur_code >>= 1) rev_code = (rev_code << 1) | (cur_code & 1);
						if (code_size <= TINFL_FAST_LOOKUP_BITS) { mz_int16 k = (mz_int16)((code_size << 9) | sym_index); while (rev_code < TINFL_FAST_LOOKUP_SIZE) { pTable->m_look_up[rev_code] = k; rev_code += (1 << code_size); } continue; }
						if (0 == (tree_cur = pTable->m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)])) { pTable->m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)] = (mz_int16)tree_next; tree_cur = tree_next; tree_next -= 2; }
						rev_code >>= (TINFL_FAST_LOOKUP_BITS - 1);
						for (j = code_size; j > (TINFL_FAST_LOOKUP_BITS + 1); j--)
						{
							tree_cur -= ((rev_code >>= 1) & 1);
							if (!pTable->m_tree[-tree_cur - 1]) { pTable->m_tree[-tree_cur - 1] = (mz_int16)tree_next; tree_cur = tree_next; tree_next -= 2; } else tree_cur = pTable->m_tree[-tree_cur - 1];
						}
						tree_cur -= ((rev_code >>= 1) & 1); pTable->m_tree[-tree_cur - 1] = (mz_int16)sym_index;
					}
					if (r->m_type == 2)
					{
						for (counter = 0; counter < (r->m_table_sizes[0] + r->m_table_sizes[1]); )
						{
							mz_uint s; TINFL_HUFF_DECODE(TINFL_STATE_16, dist, &r->m_tables[2]); if (dist < 16) { r->m_len_codes[counter++] = (mz_uint8)dist; continue; }
							if ((dist == 16) && (!counter))
							{
								TINFL_CR_RETURN_FOREVER(17, TINFL_STATUS_FAILED);
							}
							num_extra = "\02\03\07"[dist - 16]; TINFL_GET_BITS(TINFL_STATE_18, s, num_extra); s += "\03\03\013"[dist - 16];
							TINFL_MEMSET(r->m_len_codes + counter, (dist == 16) ? r->m_len_codes[counter - 1] : 0, s); counter += s;
						}
						if ((r->m_table_sizes[0] + r->m_table_sizes[1]) != counter)
						{
							TINFL_CR_RETURN_FOREVER(21, TINFL_STATUS_FAILED);
						}
						TINFL_MEMCPY(r->m_tables[0].m_code_size, r->m_len_codes, r->m_table_sizes[0]); TINFL_MEMCPY(r->m_tables[1].m_code_size, r->m_len_codes + r->m_table_sizes[0], r->m_table_sizes[1]);
					}
				}
				for ( ; ; )
				{
					mz_uint8 *pSrc;
					for ( ; ; )
					{
						if (GCC_UNLIKELY(((pIn_buf_end_m_4 < pIn_buf_cur)) || ((pOut_buf_end_m_2 < pOut_buf_cur))))
						{
							TINFL_HUFF_DECODE(TINFL_STATE_23, counter, &r->m_tables[0]);
							if (counter >= 256)
								break;
							while (pOut_buf_cur >= pOut_buf_end) { TINFL_CR_RETURN(TINFL_STATE_24, TINFL_STATUS_HAS_MORE_OUTPUT); }
							*pOut_buf_cur++ = (mz_uint8)counter;
						}
						else
						{
							int sym2; mz_uint code_len;
							#if MINIZ_HAS_64BIT_REGISTERS
							if (num_bits < 30) { bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE32(pIn_buf_cur)) << num_bits); pIn_buf_cur += 4; num_bits += 32; }
							#else
							if (num_bits < 15) { bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits); pIn_buf_cur += 2; num_bits += 16; }
							#endif

							sym2 = r_tables_0_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)];
							if (GCC_LIKELY(sym2 < 0))
							{
								code_len = TINFL_FAST_LOOKUP_BITS;
								do { sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
							}
							else
								code_len = sym2 >> 9;
							counter = sym2;
							bit_buf >>= code_len;
							num_bits -= code_len;
							if (counter & 256)
								break;

							#if !MINIZ_HAS_64BIT_REGISTERS
							if (num_bits < 15) { bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits); pIn_buf_cur += 2; num_bits += 16; }
							#endif

							sym2 = r_tables_0_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)];
							if (GCC_LIKELY(sym2 >= 0))
								code_len = sym2 >> 9;
							else
							{
								code_len = TINFL_FAST_LOOKUP_BITS;
								do { sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
							}
							bit_buf >>= code_len;
							num_bits -= code_len;

							pOut_buf_cur[0] = (mz_uint8)counter;
							if (sym2 & 256)
							{
								pOut_buf_cur++;
								counter = sym2;
								break;
							}
							pOut_buf_cur[1] = (mz_uint8)sym2;
							pOut_buf_cur += 2;
						}
					}
					if ((counter &= 511) == 256) break;

					num_extra = s_length_extra[counter - 257]; counter = s_length_base[counter - 257];
					if (num_extra) { mz_uint extra_bits; TINFL_GET_BITS(TINFL_STATE_25, extra_bits, num_extra); counter += extra_bits; }

					TINFL_HUFF_DECODE(TINFL_STATE_26, dist, &r->m_tables[1]);
					num_extra = s_dist_extra[dist]; dist = s_dist_base[dist];
					if (num_extra) { mz_uint extra_bits; TINFL_GET_BITS(TINFL_STATE_27, extra_bits, num_extra); dist += extra_bits; }

					dist_from_out_buf_start = pOut_buf_cur - pOut_buf_start;
					if ((dist > dist_from_out_buf_start) && (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
					{
						TINFL_CR_RETURN_FOREVER(37, TINFL_STATUS_FAILED);
					}

					pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);

					if (GCC_LIKELY((MZ_MAX(pOut_buf_cur, pSrc) + counter) <= pOut_buf_end))
					{
						do
						{
							pOut_buf_cur[0] = pSrc[0];
							pOut_buf_cur[1] = pSrc[1];
							pOut_buf_cur[2] = pSrc[2];
							pOut_buf_cur += 3; pSrc += 3;
						} while ((int)(counter -= 3) > 2);
						if (GCC_LIKELY((int)counter > 0))
						{
							*(pOut_buf_cur++) = pSrc[0];
							if (counter == 2)
								*(pOut_buf_cur++) = pSrc[1];
						}
					}
					else
					{
						while (counter--)
						{
							while (pOut_buf_cur >= pOut_buf_end) { TINFL_CR_RETURN(TINFL_STATE_53, TINFL_STATUS_HAS_MORE_OUTPUT); }
							*pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
						}
					}
				}
			}
		} while (!(r->m_final & 1));
		TINFL_CR_RETURN_FOREVER(34, TINFL_STATUS_DONE);
		TINFL_CR_FINISH

		common_exit:
		r->m_num_bits = num_bits; r->m_bit_buf = bit_buf; r->m_dist = dist; r->m_counter = counter; r->m_num_extra = num_extra; r->m_dist_from_out_buf_start = dist_from_out_buf_start;
		*pIn_buf_size = (mz_uint32)(pIn_buf_cur - pIn_buf_next); *pOut_buf_size = (mz_uint32)(pOut_buf_cur - pOut_buf_next);
		return status;

		#undef TINFL_MACRO_END
		#undef TINFL_MEMCPY
		#undef TINFL_MEMSET
		#undef TINFL_CR_BEGIN
		#undef TINFL_CR_RETURN
		#undef TINFL_CR_RETURN_FOREVER
		#undef TINFL_CR_FINISH
		#undef TINFL_GET_BYTE
		#undef TINFL_NEED_BITS
		#undef TINFL_SKIP_BITS
		#undef TINFL_GET_BITS
		#undef TINFL_HUFF_BITBUF_FILL
		#undef TINFL_HUFF_DECODE
	}
};

struct sdefl
{
	// BASED ON SMALL DEFLATE
	// Small Deflate (public domain)
	// By Micha Mettke - https://gist.github.com/vurtun/760a6a2a198b706a7b1a6197aa5ac747

	enum { WIN_SIZ = (1 << 15), HASH_BITS = 19, HASH_SIZ = (1 << HASH_BITS) };
	int bits, cnt, tbl[HASH_SIZ], prv[WIN_SIZ];

	Bit32u Run(unsigned char *out, const unsigned char *in, int in_len, int lvl = 9)
	{
		enum { WIN_MSK = WIN_SIZ-1, MIN_MATCH = 4, MAX_MATCH = 258, HASH_MSK = HASH_SIZ-1, NIL = -1, LVL_MIN = 0, LVL_DEF = 5, LVL_MAX = 8 };
		#define R2(n) n, n + 128, n + 64, n + 192
		#define R4(n) R2(n), R2(n + 32), R2(n + 16), R2(n + 48)
		#define R6(n) R4(n), R4(n +  8), R4(n +  4), R4(n + 12)
		static const unsigned char sdefl_mirror[256] = { R6(0), R6(2), R6(1), R6(3) };
		#undef R6
		#undef R4
		#undef R2

		struct defl
		{
			static unsigned char* put(unsigned char *dst, struct sdefl *s, int code, int bitcnt)
			{
				s->bits |= (code << s->cnt);
				s->cnt += bitcnt;
				while (s->cnt >= 8) { *dst++ = (unsigned char)(s->bits & 0xFF); s->bits >>= 8; s->cnt -= 8; }
				return dst;
			}
			static int ilog2(int n)
			{
				#define it(n) n,n,n,n, n,n,n,n, n,n,n,n ,n,n,n,n
				static const signed char tbl[256] = {-1,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,it(4),it(5),it(5),it(6),it(6),it(6),it(6),it(7),it(7),it(7),it(7),it(7),it(7),it(7),it(7)};
				int tt, t;
				return (((tt = (n >> 16))) ? ((t = (tt >> 8)) ? 24+tbl[t]: 16+tbl[tt]) : ((t = (n >> 8)) ? 8+tbl[t]: tbl[n]));
				#undef it
			}
			static int npow2(int n)
			{
				n--; n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16; return (int)++n;
			}
			static unsigned uload32(const void *p)
			{
				unsigned int n = 0;
				memcpy(&n, p, sizeof(n));
				return n;
			}
			static unsigned hash32(const void *p)
			{
				unsigned n = uload32(p);
				return (n*0x9E377989)>>(32-HASH_BITS);
			}
		};

		int p = 0, max_chain = (lvl < 8) ? (1<<(lvl+1)): (1<<13);
		unsigned char *q = out;

		bits = cnt = 0;
		for (p = 0; p < HASH_SIZ; ++p) tbl[p] = NIL;

		p = 0;
		q = defl::put(q, this, 0x01, 1); /* block */
		q = defl::put(q, this, 0x01, 2); /* static huffman */
		while (p < in_len){
			int run, best_len = 0, dist = 0;
			int max_match = ((in_len-p)>MAX_MATCH) ? MAX_MATCH:(in_len-p);
			if (max_match > MIN_MATCH){
				int limit = ((p-WIN_SIZ)<NIL)?NIL:(p-WIN_SIZ);
				int chain_len = max_chain;
				int i = tbl[defl::hash32(&in[p])];
				while (i > limit) {
					if (in[i+best_len] == in[p+best_len] && (defl::uload32(&in[i]) == defl::uload32(&in[p]))){
						int n = MIN_MATCH;
						while (n < max_match && in[i+n] == in[p+n]) n++;
						if (n > best_len) {
							best_len = n;
							dist = p - i;
							if (n == max_match) break;
						}
					}
					if (!(--chain_len)) break;
					i = prv[i&WIN_MSK];
				}
			}
			if (lvl >= 5 && best_len >= MIN_MATCH && best_len < max_match){
				const int x = p + 1;
				int tar_len = best_len + 1;
				int limit = ((x-WIN_SIZ)<NIL)?NIL:(x-WIN_SIZ);
				int chain_len = max_chain;
				int i = tbl[defl::hash32(&in[p])];
				while (i > limit) {
					if (in[i+best_len] == in[x+best_len] && (defl::uload32(&in[i]) == defl::uload32(&in[x]))){
						int n = MIN_MATCH;
						while (n < tar_len && in[i+n] == in[x+n]) n++;
						if (n == tar_len) {
							best_len = 0;
							break;
						}
					}
					if (!(--chain_len)) break;
					i = prv[i&WIN_MSK];
				}
			}
			if (best_len >= MIN_MATCH) {
				static const short lxmin[] = {0,11,19,35,67,131};
				static const short dxmax[] = {0,6,12,24,48,96,192,384,768,1536,3072,6144,12288,24576};
				static const short lmin[] = {11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227};
				static const short dmin[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};

				/* length encoding */
				int lc = best_len;
				int lx = defl::ilog2(best_len - 3) - 2;
				if (!(lx = (lx < 0) ? 0: lx)) lc += 254;
				else if (best_len >= 258) lx = 0, lc = 285;
				else lc = ((lx-1) << 2) + 265 + ((best_len - lxmin[lx]) >> lx);

				if (lc <= 279) q = defl::put(q, this, sdefl_mirror[(lc - 256) << 1], 7);
				else q = defl::put(q, this, sdefl_mirror[0xc0 - 280 + lc], 8);
				if (lx) q = defl::put(q, this, best_len - lmin[lc - 265], lx);

				/* distance encoding */
				int dc = dist - 1;
				int dx = defl::ilog2(defl::npow2(dist) >> 2);
				if ((dx = (dx < 0) ? 0: dx))
					dc = ((dx + 1) << 1) + (dist > dxmax[dx]);
				q = defl::put(q, this, sdefl_mirror[dc << 3], 5);
				if (dx) q = defl::put(q, this, dist - dmin[dc], dx);
				run = best_len;
			} else {
				int c = in[p];
				if (c <= 143) q = defl::put(q, this, sdefl_mirror[0x30+c], 8);
				else q = defl::put(q, this, 1 + 2 * sdefl_mirror[0x90 - 144 + c], 9);
				run = 1;
			}
			while (run-- != 0) {
				unsigned h = defl::hash32(&in[p]);
				prv[p&WIN_MSK] = tbl[h];
				tbl[h] = p++;
			}
		}
		/* zlib partial flush */
		q = defl::put(q, this, 0, 7);
		q = defl::put(q, this, 2, 10);
		q = defl::put(q, this, 2, 3);
		return (Bit32u)(q - out);
	}
};

struct oz_unshrink
{
	// BASED ON OZUNSHRINK
	// Ozunshrink / Old ZIP Unshrink (ozunshrink.h) (public domain)
	// By Jason Summers - https://github.com/jsummers/oldunzip

	enum
	{
		OZ_ERRCODE_OK                  = 0,
		OZ_ERRCODE_GENERIC_ERROR       = 1,
		OZ_ERRCODE_BAD_CDATA           = 2,
		OZ_ERRCODE_READ_FAILED         = 6,
		OZ_ERRCODE_WRITE_FAILED        = 7,
		OZ_ERRCODE_INSUFFICIENT_CDATA  = 8,
	};

	typedef unsigned char oz_uint8;
	typedef unsigned short oz_uint16;
	typedef unsigned int oz_uint32;
	typedef unsigned short oz_code;

	oz_uint8 *out_start, *out_cur, *out_end;
	oz_uint8 *in_start, *in_cur, *in_end;

	// The code table (implements a dictionary)
	enum { OZ_VALBUFSIZE = 7936, OZ_NUM_CODES = 8192 };
	oz_uint8 valbuf[OZ_VALBUFSIZE]; // Max possible chain length (8192 - 257 + 1 = 7936)
	struct { oz_code parent; oz_uint8 value; oz_uint8 flags; } ct[OZ_NUM_CODES];

	static int Run(oz_unshrink *oz)
	{
		enum { OZ_INITIAL_CODE_SIZE = 9, OZ_MAX_CODE_SIZE = 13, OZ_INVALID_CODE = 256 };
		oz_uint32 oz_bitreader_buf = 0;
		oz_uint8  oz_bitreader_nbits_in_buf = 0;
		oz_uint8  oz_curr_code_size = OZ_INITIAL_CODE_SIZE;
		oz_code   oz_oldcode = 0;
		oz_code   oz_last_code_added = 0;
		oz_code   oz_highest_code_ever_used = 0;
		oz_code   oz_free_code_search_start = 257;
		oz_uint8  oz_last_value = 0;
		bool      oz_have_oldcode = false;
		bool      oz_was_clear = false;

		memset(oz->ct, 0, sizeof(oz->ct));
		for (oz_code i = 0; i < 256; i++)
		{
			// For entries <=256, .parent is always set to OZ_INVALID_CODE.
			oz->ct[i].parent = OZ_INVALID_CODE;
			oz->ct[i].value = (oz_uint8)i;
		}
		for (oz_code i = 256; i < OZ_NUM_CODES; i++)
		{
			// For entries >256, .parent==OZ_INVALID_CODE means code is unused
			oz->ct[i].parent = OZ_INVALID_CODE;
		}

		for (;;)
		{
			while (oz_bitreader_nbits_in_buf < oz_curr_code_size)
			{
				if (oz->in_cur >= oz->in_end) return OZ_ERRCODE_INSUFFICIENT_CDATA;
				oz_uint8 b = *(oz->in_cur++);
				oz_bitreader_buf |= ((oz_uint32)b) << oz_bitreader_nbits_in_buf;
				oz_bitreader_nbits_in_buf += 8;
			}

			oz_code code = (oz_code)(oz_bitreader_buf & ((1U << oz_curr_code_size) - 1U));
			oz_bitreader_buf >>= oz_curr_code_size;
			oz_bitreader_nbits_in_buf -= oz_curr_code_size;

			if (code == 256)
			{
				oz_was_clear = true;
				continue;
			}

			if (oz_was_clear)
			{
				oz_was_clear = false;

				if (code == 1 && (oz_curr_code_size < OZ_MAX_CODE_SIZE))
				{
					oz_curr_code_size++;
					continue;
				}
				if (code != 2) return OZ_ERRCODE_BAD_CDATA;

				// partial clear
				oz_code i;
				for (i = 257; i <= oz_highest_code_ever_used; i++)
				{
					if (oz->ct[i].parent != OZ_INVALID_CODE)
					{
						oz->ct[oz->ct[i].parent].flags = 1; // Mark codes that have a child
					}
				}

				for (i = 257; i <= oz_highest_code_ever_used; i++)
				{
					if (oz->ct[i].flags == 0)
					{
						oz->ct[i].parent = OZ_INVALID_CODE; // Clear this code
						oz->ct[i].value = 0;
					}
					else
					{
						oz->ct[i].flags = 0; // Leave all flags at 0, for next time.
					}
				}

				oz_free_code_search_start = 257;
				continue;
			}

			// Process a single (nonspecial) LZW code that was read from the input stream.
			if (code >= OZ_NUM_CODES) return OZ_ERRCODE_GENERIC_ERROR;

			bool late_add, code_is_in_table = (code < 256 || oz->ct[code].parent != OZ_INVALID_CODE);
			if      (!oz_have_oldcode) { late_add = false; goto OZ_EMIT_CODE;         } //emit only
			else if (code_is_in_table) { late_add =  true; goto OZ_EMIT_CODE;         } //emit, then add
			else                       { late_add = false; goto OZ_ADD_TO_DICTIONARY; } //add, then emit

			// Add a code to the dictionary. Sets oz_last_code_added to the position where it was added.
			OZ_ADD_TO_DICTIONARY:
			oz_code newpos;
			for (newpos = oz_free_code_search_start; ; newpos++)
			{
				if (newpos >= OZ_NUM_CODES) return OZ_ERRCODE_BAD_CDATA;
				if (oz->ct[newpos].parent == OZ_INVALID_CODE) break;
			}
			oz->ct[newpos].parent = oz_oldcode;
			oz->ct[newpos].value = oz_last_value;
			oz_last_code_added = newpos;
			oz_free_code_search_start = newpos + 1;
			if (newpos > oz_highest_code_ever_used)
			{
				oz_highest_code_ever_used = newpos;
			}
			if (late_add) goto OZ_FINISH_PROCESS_CODE; 

			// Decode an LZW code to one or more values, and write the values. Updates oz_last_value.
			OZ_EMIT_CODE:
			for (oz_code emit_code = code, valbuf_pos = OZ_VALBUFSIZE;;) // = First entry that's used
			{
				if (emit_code >= OZ_NUM_CODES) return OZ_ERRCODE_GENERIC_ERROR;

				// Check if infinite loop (probably an internal error).
				if (valbuf_pos == 0) return OZ_ERRCODE_GENERIC_ERROR;

				// valbuf is a stack, essentially. We fill it in the reverse direction, to make it simpler to write the final byte sequence.
				valbuf_pos--;

				if (emit_code >= 257 && oz->ct[emit_code].parent == OZ_INVALID_CODE)
				{
					oz->valbuf[valbuf_pos] = oz_last_value;
					emit_code = oz_oldcode;
					continue;
				}

				oz->valbuf[valbuf_pos] = oz->ct[emit_code].value;

				if (emit_code < 257)
				{
					oz_last_value = oz->ct[emit_code].value;

					// Write out the collected values.
					size_t n = OZ_VALBUFSIZE - valbuf_pos;
					if (oz->out_cur + n > oz->out_end) return OZ_ERRCODE_WRITE_FAILED;
					memcpy(oz->out_cur, &oz->valbuf[valbuf_pos], n);
					oz->out_cur += n;
					if (oz->out_cur == oz->out_end) return OZ_ERRCODE_OK;

					break;
				}

				// Traverse the tree, back toward the root codes.
				emit_code = oz->ct[emit_code].parent;
			}
			if (late_add) goto OZ_ADD_TO_DICTIONARY;

			if (!oz_have_oldcode)
			{
				oz_have_oldcode = true;
				oz_last_value = (oz_uint8)code;
			}

			OZ_FINISH_PROCESS_CODE:
			oz_oldcode = code;
		}
	}
};

struct unz_explode
{
	// BASED ON INFO-ZIP UNZIP
	// Info-ZIP UnZip v5.4 (explode.c and inflate.c)
	// Put in the public domain by Mark Adler

	enum
	{
		UNZ_ERRCODE_OK                  = 0,
		UNZ_ERRCODE_INCOMPLETE_SET      = 1,
		UNZ_ERRCODE_INVALID_TABLE_INPUT = 2,
		UNZ_ERRCODE_OUTOFMEMORY         = 3,
		UNZ_ERRCODE_INVALID_TREE_INPUT  = 4,
		UNZ_ERRCODE_INTERNAL_ERROR      = 5,
		UNZ_ERRCODE_OUTPUT_ERROR        = 6,
	};

	typedef unsigned char  unz_uint8;
	typedef unsigned short unz_uint16;
	typedef unsigned int   unz_uint32;

	unz_uint8 *out_start, *out_cur, *out_end;
	unz_uint8 *in_start, *in_cur, *in_end;

	enum { WSIZE = 0x8000 }; // window size--must be a power of two
	unz_uint8 slide[WSIZE];

	static unz_uint8 GetByte(unz_explode* exploder)
	{
		return (exploder->in_cur < exploder->in_end ? *(exploder->in_cur++) : 0);
	}

	struct huft
	{
		// number of extra bits or operation, number of bits in this code or subcode
		unz_uint8 e, b;
		// literal, length base, or distance base || pointer to next level of table
		union { unz_uint16 n; huft *t; } v;
	};

	static void huft_free(huft *t)
	{
		for (huft *p = t, *q; p != (huft *)NULL; p = q)
		{
			q = (--p)->v.t;
			free(p);
		}
	}

	static int get_tree_build_huft(unz_explode* exploder, unz_uint32 *b, unz_uint32 n, unz_uint32 s, const unz_uint16 *d, const unz_uint16 *e, huft **t, int *m)
	{
		// Get the bit lengths for a code representation from the compressed stream.
		// If get_tree() returns 4, then there is an error in the data
		unz_uint32 bytes_remain;    // bytes remaining in list
		unz_uint32 lengths_entered; // lengths entered
		unz_uint32 ncodes;  // number of codes
		unz_uint32 bitlen; // bit length for those codes

		// get bit lengths
		bytes_remain = (unz_uint32)GetByte(exploder) + 1; // length/count pairs to read
		lengths_entered = 0; // next code
		do
		{
			bitlen = ((ncodes = (unz_uint32)GetByte(exploder)) & 0xf) + 1; //bits in code (1..16)
			ncodes = ((ncodes & 0xf0) >> 4) + 1; /* codes with those bits (1..16) */
			if (lengths_entered + ncodes > n) return UNZ_ERRCODE_INVALID_TREE_INPUT; // don't overflow bit_lengths
			do
			{
				b[lengths_entered++] = bitlen;
			} while (--ncodes);
		} while (--bytes_remain);
		if (lengths_entered != n) return UNZ_ERRCODE_INVALID_TREE_INPUT;

		// Mystery code, the original (huft_build function) wasn't much more readable IMHO (see inflate.c)
		// Given a list of code lengths and a maximum table size, make a set of tables to decode that set of codes.  Return zero on success, one if
		// the given code set is incomplete (the tables are still built in this case), two if the input is invalid (all zero length codes or an
		// oversubscribed set of lengths), and three if not enough memory.
		enum { BMAX = 16, N_MAX = 288 }; unz_uint32 a, c[BMAX + 1], f, i, j, *p, v[N_MAX], x[BMAX + 1], *xp, z; int g, h, k, l, w, y; huft *q, r, *u[BMAX];
		memset(c, 0, sizeof(c)); p = b; i = n; do { c[*p++]++; } while (--i); if (c[0] == n) { *t = (huft *)NULL; *m = 0; return UNZ_ERRCODE_OK; }
		l = *m; for (j = 1; j <= BMAX; j++) if (c[j]) break; k = j; if ((unz_uint32)l < j) l = j; for (i = BMAX; i; i--) if (c[i]) break;
		g = i; if ((unz_uint32)l > i) l = i; *m = l; for (y = 1 << j; j < i; j++, y <<= 1) if ((y -= c[j]) < 0) return UNZ_ERRCODE_INVALID_TABLE_INPUT;
		if ((y -= c[i]) < 0) return UNZ_ERRCODE_INVALID_TABLE_INPUT; c[i] += y; x[1] = j = 0; p = c + 1; xp = x + 2; while (--i) { *xp++ = (j += *p++); }
		p = b; i = 0; do { if ((j = *p++) != 0) v[x[j]++] = i; } while (++i < n); x[0] = i = 0; p = v; h = -1; w = -l; 
		u[0] = (huft *)NULL; q = (huft *)NULL; z = 0; for (; k <= g; k++) { a = c[k]; while (a--) { while (k > w + l)
		{ h++; w += l; z = (z = g - w) > (unz_uint32)l ? l : z; if ((f = 1 << (j = k - w)) > a + 1) { f -= a + 1; xp = c + k; while (++j < z)
		{ if ((f <<= 1) <= *++xp) break; f -= *xp; } } z = 1 << j; if ((q = (huft *)malloc((z + 1)*sizeof(huft))) == (huft *)NULL)
		{ if (h) huft_free(u[0]); return UNZ_ERRCODE_OUTOFMEMORY; } *t = q + 1; *(t = &(q->v.t)) = (huft *)NULL; u[h] = ++q; if (h)
		{ x[h] = i; r.b = (unz_uint8)l; r.e = (unz_uint8)(16 + j); r.v.t = q; j = i >> (w - l); u[h - 1][j] = r; } } r.b = (unz_uint8)(k - w); if (p >= v + n) r.e = 99; else if (*p < s)
		{ r.e = (unz_uint8)(*p < 256 ? 16 : 15); r.v.n = *p++; } else
		{ r.e = (unz_uint8)e[*p - s]; r.v.n = d[*p++ - s]; } f = 1 << (k - w); for (j = i >> w; j < z; j += f) q[j] = r; for (j = 1 << (k - 1);
		i & j; j >>= 1) i ^= j; i ^= j; while ((i & ((1 << w) - 1)) != x[h]) { h--; w -= l; } } }
		return (y == 0 || g == 1 ? UNZ_ERRCODE_OK : UNZ_ERRCODE_INCOMPLETE_SET);
	}

	static int flush(unz_explode* exploder, unz_uint32 w)
	{
		unz_uint8 *out_w = exploder->out_cur + w;
		int ret = (out_w > exploder->out_end ? 1 : 0);
		if (ret) out_w = exploder->out_end;
		memcpy(exploder->out_cur, exploder->slide, (out_w - exploder->out_cur));
		exploder->out_cur = out_w;
		return ret;
	}

	static int Run(unz_explode* exploder, unz_uint16 zip_bit_flag)
	{
		/* Tables for length and distance */
		static const unz_uint16 cplen2[]  = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65 };
		static const unz_uint16 cplen3[]  = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66 };
		static const unz_uint16 extra[]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8 };
		static const unz_uint16 cpdist4[] = { 1, 65, 129, 193, 257, 321, 385, 449, 513, 577, 641, 705, 769, 833, 897, 961, 1025, 1089, 1153, 1217, 1281, 1345, 1409, 1473, 1537, 1601, 1665, 1729, 1793, 1857, 1921, 1985, 2049, 2113, 2177, 2241, 2305, 2369, 2433, 2497, 2561, 2625, 2689, 2753, 2817, 2881, 2945, 3009, 3073, 3137, 3201, 3265, 3329, 3393, 3457, 3521, 3585, 3649, 3713, 3777, 3841, 3905, 3969, 4033 };
		static const unz_uint16 cpdist8[] = { 1, 129, 257, 385, 513, 641, 769, 897, 1025, 1153, 1281, 1409, 1537, 1665, 1793, 1921, 2049, 2177, 2305, 2433, 2561, 2689, 2817, 2945, 3073, 3201, 3329, 3457, 3585, 3713, 3841, 3969, 4097, 4225, 4353, 4481, 4609, 4737, 4865, 4993, 5121, 5249, 5377, 5505, 5633, 5761, 5889, 6017, 6145, 6273, 
			1, 6529, 6657, 6785, 6913, 7041, 7169, 7297, 7425, 7553, 7681, 7809, 7937, 8065 };
		static const unz_uint16 mask_bits[] = { 0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff, 0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff };

		huft *tb = NULL, *tl = NULL, *td = NULL; // literal code, length code, distance code tables
		unz_uint32 l[256]; // bit lengths for codes
		bool is8k  = ((zip_bit_flag & 2) == 2), islit = ((zip_bit_flag & 4) == 4);
		int bb = (islit ? 9 : 0), bl = 7, bd = ((exploder->in_end - exploder->in_start)  > 200000 ? 8 : 7); // bits for tb, tl, td
		unz_uint32 numbits = (is8k ? 7 : 6);

		int r;
		if (islit && (r = get_tree_build_huft(exploder, l, 256, 256, NULL, NULL, &tb, &bb)) != 0) goto done;
		if ((r = get_tree_build_huft(exploder, l, 64, 0, (islit ? cplen3 : cplen2), extra, &tl, &bl)) != 0) goto done;
		if ((r = get_tree_build_huft(exploder, l, 64, 0, (is8k ? cpdist8 : cpdist4), extra, &td, &bd)) != 0) goto done;

		// The implode algorithm uses a sliding 4K or 8K byte window on the uncompressed stream to find repeated byte strings.
		// This is implemented here as a circular buffer. The index is updated simply by incrementing and then and'ing with 0x0fff (4K-1) or 0x1fff (8K-1).
		// Here, the 32K buffer of inflate is used, and it works just as well to always have a 32K circular buffer, so the index is anded with 0x7fff.
		// This is done to allow the window to also be used as the output buffer.
		unz_uint32 s;          // bytes to decompress
		unz_uint32 e;          // table entry flag/number of extra bits
		unz_uint32 n, d;       // length and index for copy
		unz_uint32 w;          // current window position
		unz_uint32 mb, ml, md; // masks for bb (if lit), bl and bd bits
		unz_uint32 b;          // bit buffer
		unz_uint32 k;          // number of bits in bit buffer
		unz_uint32 u;          // true if unflushed
		huft *t;               // pointer to table entry

		#define UNZ_NEEDBITS(n) do {while(k<(n)){b|=((unz_uint32)GetByte(exploder))<<k;k+=8;}} while(0)
		#define UNZ_DUMPBITS(n) do {b>>=(n);k-=(n);} while(0)

		// explode the coded data
		b = k = w = 0; // initialize bit buffer, window
		u = 1;         // buffer unflushed

		// precompute masks for speed
		mb = mask_bits[bb];
		ml = mask_bits[bl];
		md = mask_bits[bd];
		s = (unz_uint32)(exploder->out_end - exploder->out_start);
		while (s > 0) // do until ucsize bytes uncompressed
		{
			UNZ_NEEDBITS(1);
			if (b & 1) // then literal
			{
				UNZ_DUMPBITS(1);
				s--;
				if (tb)
				{
					// LIT: Decompress the imploded data using coded literals and an 8K sliding window.
					UNZ_NEEDBITS((unz_uint32)bb); // get coded literal
					if ((e = (t = tb + ((~(unz_uint32)b) & mb))->e) > 16)
					{
						do
						{
							if (e == 99) { r = UNZ_ERRCODE_INTERNAL_ERROR; goto done; }
							UNZ_DUMPBITS(t->b);
							e -= 16;
							UNZ_NEEDBITS(e);
						} while ((e = (t = t->v.t + ((~(unz_uint32)b) & mask_bits[e]))->e) > 16);
					}
					UNZ_DUMPBITS(t->b);
					exploder->slide[w++] = (unz_uint8)t->v.n;
					if (w == WSIZE) { if (flush(exploder, w)) { r = UNZ_ERRCODE_OUTPUT_ERROR; goto done; } w = u = 0; }
				}
				else
				{
					// UNLIT: Decompress the imploded data using uncoded literals and an 8K sliding window.
					UNZ_NEEDBITS(8);
					exploder->slide[w++] = (unz_uint8)b;
					if (w == WSIZE) { if (flush(exploder, w)) { r = UNZ_ERRCODE_OUTPUT_ERROR; goto done; } w = u = 0; }
					UNZ_DUMPBITS(8);
				}
			}
			else // else distance/length
			{
				UNZ_DUMPBITS(1);
				UNZ_NEEDBITS(numbits); // get distance low bits
				d = (unz_uint32)b & ((1 << numbits) - 1);
				UNZ_DUMPBITS(numbits);
				UNZ_NEEDBITS((unz_uint32)bd); // get coded distance high bits
				if ((e = (t = td + ((~(unz_uint32)b) & md))->e) > 16)
				{
					do
					{
						if (e == 99) { r = UNZ_ERRCODE_INTERNAL_ERROR; goto done; }
						UNZ_DUMPBITS(t->b);
						e -= 16;
						UNZ_NEEDBITS(e);
					} while ((e = (t = t->v.t + ((~(unz_uint32)b) & mask_bits[e]))->e) > 16);
				}
				UNZ_DUMPBITS(t->b);
				d = w - d - t->v.n; // construct offset
				UNZ_NEEDBITS((unz_uint32)bl); // get coded length
				if ((e = (t = tl + ((~(unz_uint32)b) & ml))->e) > 16)
				{
					do
					{
						if (e == 99) { r = UNZ_ERRCODE_INTERNAL_ERROR; goto done; }
						UNZ_DUMPBITS(t->b);
						e -= 16;
						UNZ_NEEDBITS(e);
					} while ((e = (t = t->v.t + ((~(unz_uint32)b) & mask_bits[e]))->e) > 16);
				}
				UNZ_DUMPBITS(t->b);
				n = t->v.n;
				if (e) // get length extra bits
				{
					UNZ_NEEDBITS(8);
					n += (unz_uint32)b & 0xff;
					UNZ_DUMPBITS(8);
				}

				// do the copy
				s -= n;
				do
				{
					n -= (e = (e = WSIZE - ((d &= WSIZE - 1) > w ? d : w)) > n ? n : e);
					if (u && w <= d)
					{
						memset(exploder->slide + w, 0, e);
						w += e;
						d += e;
					}
					else if (w - d >= e) // (this test assumes unsigned comparison)
					{
						memcpy(exploder->slide + w, exploder->slide + d, e);
						w += e;
						d += e;
					}
					else // do it slow to avoid memcpy() overlap
					{
						do {
							exploder->slide[w++] = exploder->slide[d++];
						} while (--e);
					}
					if (w == WSIZE)
					{
						if (flush(exploder, w)) { r = UNZ_ERRCODE_OUTPUT_ERROR; goto done; }
						w = u = 0;
					}
				} while (n);
			}
		}

		#undef UNZ_NEEDBITS
		#undef UNZ_DUMPBITS

		/* flush out slide */
		if (flush(exploder, w)) { r = UNZ_ERRCODE_OUTPUT_ERROR; goto done; }

		done:
		huft_free(td);
		huft_free(tl);
		huft_free(tb);
		return r;
	}
};

struct Zip_Archive
{
	DOS_File* zip;
	Bit64u ofs;
	Bit64u size;

	Zip_Archive(DOS_File* _zip) : zip(_zip)
	{
		zip->AddRef();
		size = 0;
		bool can_seek = zip->Seek64(&size, DOS_SEEK_END);
		ofs = size;
		DBP_ASSERT(can_seek);
	}
	
	~Zip_Archive()
	{
		if (!zip) return;
		if (zip->IsOpen()) zip->Close();
		if (zip->RemoveRef() <= 0) delete zip;
	}

	Bit32u Read(Bit64u seek_ofs, void *pBuf, Bit32u n)
	{
		if (seek_ofs >= size) n = 0;
		else if ((Bit64u)n > (size - seek_ofs)) n = (Bit32u)(size - seek_ofs);
		if (seek_ofs != ofs)
		{
			zip->Seek64(&seek_ofs, DOS_SEEK_SET);
			ofs = seek_ofs;
		}
		Bit8u* pOut = (Bit8u*)pBuf;
		for (Bit32u remain = n; remain;)
		{
			Bit16u sz = (remain > 0xFFFF ? 0xFFFF : (Bit16u)remain);
			if (!zip->Read(pOut, &sz) || !sz) { n -= remain; break; }
			remain -= sz;
			pOut += sz;
		}
		ofs += n;
		return n;
	}
};

struct ZIP_Unpacker
{
	virtual ~ZIP_Unpacker() {}
	virtual Bit32u Read(const struct Zip_File& f, Bit32u seek_ofs, void *res_buf, Bit32u res_n) = 0;
	enum { METHOD_STORED = 0, METHOD_SHRUNK = 1, METHOD_IMPLODED = 6, METHOD_DEFLATED = 8 };
	static bool MethodSupported(Bit32u method) { return (method == METHOD_DEFLATED || method == METHOD_STORED || method == METHOD_SHRUNK || method == METHOD_IMPLODED); }
};

struct Zip_Entry
{
protected:
	Zip_Entry(Bit16u _attr, const char* _name, Bit16u _date = 0, Bit16u _time = 0) : date(_date), time(_time), attr(_attr)
	{
		size_t namesize = strlen(_name) + 1;
		if (namesize > sizeof(name)) { DBP_ASSERT(false); namesize = sizeof(name); }
		memcpy(name, _name, namesize);
	}
	~Zip_Entry() {}

public:
	inline bool IsFile()      { return ((attr & DOS_ATTR_DIRECTORY) == 0); }
	inline bool IsDirectory() { return ((attr & DOS_ATTR_DIRECTORY) != 0); }
	inline struct Zip_File*      AsFile()      { return (Zip_File*)this;      }
	inline struct Zip_Directory* AsDirectory() { return (Zip_Directory*)this; }

	Bit16u date;
	Bit16u time;
	Bit16u attr;
	char name[DOS_NAMELENGTH_ASCII];
};

struct Zip_File : Zip_Entry
{
	Bit64u data_ofs;
	Bit32u comp_size, uncomp_size;
	Bit32u refs;
	Bit16u ofs_past_header;
	Bit8u bit_flags;
	Bit8u method;
	ZIP_Unpacker* unpacker;

	Zip_File(Bit16u _attr, const char* filename, Bit16u _date, Bit16u _time, Bit64u _data_ofs, Bit32u _comp_size, Bit32u _uncomp_size, Bit8u _bit_flags, Bit8u _method)
		: Zip_Entry(_attr, filename, _date, _time), data_ofs(_data_ofs), comp_size(_comp_size), uncomp_size(_uncomp_size), refs(0), bit_flags(_bit_flags), method(_method), ofs_past_header(0), unpacker(NULL) {}

	~Zip_File()
	{
		DBP_ASSERT(!refs);
		delete unpacker;
	}
};

struct Zip_StoredUnpacker : ZIP_Unpacker
{
	Zip_Archive& archive;

	Zip_StoredUnpacker(Zip_Archive& _archive) : archive(_archive) { }

	Bit32u Read(const Zip_File& f, Bit32u seek_ofs, void *res_buf, Bit32u res_n)
	{
		return archive.Read(f.data_ofs + seek_ofs, res_buf, res_n);
	}
};

struct Zip_MemoryUnpacker : ZIP_Unpacker
{
	std::vector<Bit8u> mem_data;

	Bit32u Read(const Zip_File& f, Bit32u seek_ofs, void *res_buf, Bit32u res_n)
	{
		if ((size_t)seek_ofs > mem_data.size()) seek_ofs = (Bit32u)mem_data.size();
		if ((size_t)seek_ofs + res_n > mem_data.size()) res_n = (Bit32u)mem_data.size() - seek_ofs;
		memcpy(res_buf, &mem_data[0] + seek_ofs, res_n);
		return res_n;
	}
};

struct Zip_ShrinkUnpacker : Zip_MemoryUnpacker
{
	Zip_ShrinkUnpacker(Zip_Archive& archive, const Zip_File& f)
	{
		oz_unshrink *unshrink = (oz_unshrink*)malloc(sizeof(oz_unshrink) + f.comp_size);
		Bit8u* in_buf = (Bit8u*)(unshrink + 1);
		if (archive.Read(f.data_ofs, in_buf, f.comp_size) == f.comp_size)
		{
			mem_data.resize(f.uncomp_size);
			unshrink->in_start = unshrink->in_cur = in_buf;
			unshrink->in_end = in_buf + f.comp_size;
			unshrink->out_start = unshrink->out_cur = &mem_data[0];
			unshrink->out_end = unshrink->out_start + f.uncomp_size;
			int res = oz_unshrink::Run(unshrink);
			DBP_ASSERT(res == 0);
		}
		free(unshrink);
	}
};

struct Zip_ImplodeUnpacker : Zip_MemoryUnpacker
{
	Zip_ImplodeUnpacker(Zip_Archive& archive, const Zip_File& f)
	{
		unz_explode *explode = (unz_explode*)malloc(sizeof(unz_explode) + f.comp_size);
		Bit8u* in_buf = (Bit8u*)(explode + 1);
		if (archive.Read(f.data_ofs, in_buf, f.comp_size) == f.comp_size)
		{
			mem_data.resize(f.uncomp_size);
			explode->in_start = explode->in_cur = in_buf;
			explode->in_end = in_buf + f.comp_size;
			explode->out_start = explode->out_cur = &mem_data[0];
			explode->out_end = explode->out_start + f.uncomp_size;
			int res = unz_explode::Run(explode, f.bit_flags);
			DBP_ASSERT(res == 0);
		}
		free(explode);
	}
};

struct Zip_DeflateMemoryUnpacker : Zip_MemoryUnpacker
{
	Zip_DeflateMemoryUnpacker(Zip_Archive& archive, const Zip_File& f)
	{
		DBP_ASSERT(f.ofs_past_header);
		mem_data.resize(f.uncomp_size);

		miniz::tinfl_decompressor inflator;
		Bit64u ofs = f.data_ofs, ofs_last_read = 0;
		Bit32u out_buf_ofs = 0, read_buf_avail = 0, read_buf_ofs = 0, comp_remaining = f.comp_size;
		Bit8u read_buf[miniz::MZ_ZIP_MAX_IO_BUF_SIZE], *out_data = &mem_data[0];
		miniz::tinfl_init(&inflator);

		for (miniz::tinfl_status status = miniz::TINFL_STATUS_NEEDS_MORE_INPUT; status == miniz::TINFL_STATUS_NEEDS_MORE_INPUT || status == miniz::TINFL_STATUS_HAS_MORE_OUTPUT;)
		{
			if (!read_buf_avail)
			{
				read_buf_avail = (comp_remaining < miniz::MZ_ZIP_MAX_IO_BUF_SIZE ? comp_remaining : miniz::MZ_ZIP_MAX_IO_BUF_SIZE);
				if (archive.Read(ofs, read_buf, read_buf_avail) != read_buf_avail)
					break;
				ofs_last_read = ofs;
				ofs += read_buf_avail;
				comp_remaining -= read_buf_avail;
				read_buf_ofs = 0;
			}
			Bit32u out_buf_size = f.uncomp_size - out_buf_ofs;
			Bit8u *pWrite_buf_cur = out_data + out_buf_ofs;
			Bit32u in_buf_size = read_buf_avail;
			status = miniz::tinfl_decompress(&inflator, read_buf + read_buf_ofs, &in_buf_size, out_data, pWrite_buf_cur, &out_buf_size, miniz::TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (comp_remaining ? miniz::TINFL_FLAG_HAS_MORE_INPUT : 0));
			read_buf_avail -= in_buf_size;
			read_buf_ofs += in_buf_size;
			out_buf_ofs += out_buf_size;
			DBP_ASSERT(!out_buf_size || out_buf_ofs <= f.uncomp_size);
			DBP_ASSERT(status == miniz::TINFL_STATUS_NEEDS_MORE_INPUT || status == miniz::TINFL_STATUS_HAS_MORE_OUTPUT || status == miniz::TINFL_STATUS_DONE);
		}
	}
};

struct Zip_DeflateUnpacker : ZIP_Unpacker
{
	Zip_Archive& archive;
	miniz::tinfl_decompressor inflator;
	Bit64u ofs;
	Bit64u ofs_last_read;
	Bit32u out_buf_ofs;
	Bit32u read_buf_avail;
	Bit32u read_buf_ofs;
	Bit32u comp_remaining;

	enum { READ_BLOCK = miniz::MZ_ZIP_MAX_IO_BUF_SIZE, WRITE_BLOCK = miniz::TINFL_LZ_DICT_SIZE };
	Bit8u read_buf[READ_BLOCK];
	Bit8u write_buf[WRITE_BLOCK];

	struct SeekCursor
	{
		Bit64u cursor_in;
		Bit32u cursor_out;
		miniz::mz_uint32 m_num_bits;
		miniz::tinfl_bit_buf_t m_bit_buf;
		miniz::mz_uint32 m_dist;
		miniz::mz_uint32 m_counter;
		miniz::mz_uint32 m_num_extra;
		size_t m_dist_from_out_buf_start;
		Bit8u write_buf[WRITE_BLOCK];
	};

	Bit32u cursor_block;
	SeekCursor* cursors;

	enum { SEEK_CURSOR_MAX_DEFL = 128 + (sizeof(SeekCursor) + 9) / 10 * 11, SEEK_CACHE_CURSOR_STEPS = 20 };
	struct SeekCache { zipDrive* drv; std::string path; Bit32u cache_count; } * seek_cache;

	Zip_DeflateUnpacker(Zip_Archive& _archive, const Zip_File& f, zipDrive* drv, const char* path) : archive(_archive), seek_cache(NULL)
	{
		//printf("[%s] OPENED FILE!\n", f.name);
		DBP_ASSERT(f.ofs_past_header);
		cursor_block =
			  f.uncomp_size > (50*1024*1024) ? (1024*1024)  // 50~   MB, 50~   cursors
			: f.uncomp_size > (30*1024*1024) ? ( 768*1024)  // 30~50 MB, 40~77 cursors
			: f.uncomp_size > (12*1024*1024) ? ( 384*1024)  // 12~30 MB, 32~80 cursors
			:                                  ( 256*1024); //  0~12 MB,  2~48 cursors
		Bit32u cursor_count = (f.uncomp_size + (cursor_block - 1)) / cursor_block;
		cursors = (SeekCursor*)calloc(cursor_count, sizeof(SeekCursor));
		Reset(f);

		// Read seek cache file for larger files
		Bit8u drive_idx;
		if (cursor_count > 50 && (drive_idx = DriveGetIndex(drv)) != DOS_DRIVES)
		{
			seek_cache = new SeekCache;
			seek_cache->drv = drv;
			seek_cache->path = path;
			seek_cache->cache_count = 0;
			std::string::size_type separator = seek_cache->path.rfind('.');
			int extlen = (separator == std::string::npos ? 9 : (int)(seek_cache->path.length() - separator));
			if (extlen <= 4) seek_cache->path.resize(seek_cache->path.length() - extlen);
			seek_cache->path.append(".SKC"); // seek cache extension

			DOS_File *df;
			if (Drives[drive_idx]->FileOpen(&df, (char*)seek_cache->path.c_str(), OPEN_READ))
			{
				DBP_STATIC_ASSERT(sizeof(SeekCursor) < 0xFFFF); // for DOS_File max read/write length
				df->AddRef();
				Bit8u* compbuf = new Bit8u[sizeof(SeekCursor)];
				Bit16u hdrin[4], hdrtest[4] = { (Bit16u)0x5344, (Bit16u)sizeof(SeekCursor), (Bit16u)(f.comp_size>>16), (Bit16u)f.comp_size }, sz;
				bool valid = (df->Read((Bit8u*)hdrin, &(sz = (Bit16u)sizeof(hdrin))) && !memcmp(hdrin, hdrtest, sizeof(hdrin)));
				for (Bit16u idx_complen[2]; valid; seek_cache->cache_count++)
				{
					if (!df->Read((Bit8u*)idx_complen, &(sz = sizeof(idx_complen))) || sz != sizeof(idx_complen) || idx_complen[0] >= cursor_count || idx_complen[1] >= sizeof(SeekCursor)) break;
					if (idx_complen[1])
					{
						if (!df->Read(compbuf, &(sz = idx_complen[1])) || sz != idx_complen[1]) valid = false;
						else zipDrive::Uncompress(compbuf, idx_complen[1], (Bit8u*)&cursors[idx_complen[0]], sizeof(SeekCursor));
					}
					else if (!df->Read((Bit8u*)&cursors[idx_complen[0]], &(sz = sizeof(SeekCursor))) || sz != sizeof(SeekCursor)) valid = false;
				}
				df->Close();
				delete df;
				delete[] compbuf;
				if (!valid)
				{
					Drives[drive_idx]->FileUnlink((char*)seek_cache->path.c_str());
					seek_cache->cache_count = 0;
				}
			}
		}
	}

	~Zip_DeflateUnpacker()
	{
		free(cursors);
		if (seek_cache) delete seek_cache;
	}

	void Reset(const Zip_File& f)
	{
		miniz::tinfl_init(&inflator);
		ofs = f.data_ofs;
		out_buf_ofs = read_buf_avail = 0;
		comp_remaining = f.comp_size;
	}

	Bit32u Read(const Zip_File& f, Bit32u seek_ofs, void *res_buf, Bit32u res_n)
	{
		Bit32u want_from = seek_ofs, want_to = seek_ofs + res_n;
		DBP_ASSERT(want_to <= f.uncomp_size);
		Bit8u* p_res = (Bit8u*)res_buf;

		Bit32u have_from = ((out_buf_ofs ? out_buf_ofs - 1 : 0) & ~(WRITE_BLOCK-1));
		if (want_from < have_from || want_from > out_buf_ofs)
		{
			for (Bit32u idx = (want_from / cursor_block);; idx--)
			{
				if (!idx && (!cursors[idx].cursor_out || cursors[idx].cursor_out > want_from)) break;
				if (!cursors[idx].cursor_out || cursors[idx].cursor_out > want_from) continue;
				if (want_from > out_buf_ofs && cursors[idx].cursor_out <= out_buf_ofs) break;
				//printf("[%s] JUMP SEEKING FROM %u TO %u (WANT DATA FROM %u)\n", f.name, out_buf_ofs, cursors[idx].cursor_out, want_from);
				ofs = cursors[idx].cursor_in;
				have_from = out_buf_ofs = cursors[idx].cursor_out;
				read_buf_avail = 0;
				inflator.m_num_bits                = cursors[idx].m_num_bits;
				inflator.m_bit_buf                 = cursors[idx].m_bit_buf;
				inflator.m_dist                    = cursors[idx].m_dist;
				inflator.m_counter                 = cursors[idx].m_counter;
				inflator.m_num_extra               = cursors[idx].m_num_extra;
				inflator.m_dist_from_out_buf_start = cursors[idx].m_dist_from_out_buf_start;
				inflator.m_state = miniz::TINFL_STATE_INDEX_BLOCK_BOUNDRY;
				comp_remaining = f.comp_size - (Bit32u)(ofs - f.data_ofs);
				memcpy(write_buf, cursors[idx].write_buf, sizeof(write_buf));
				break;
			}
			if (want_from < have_from)
			{
				//printf("[%s] JUMP SEEKING FROM %u TO 0 (WANT DATA FROM %u)\n", f.name, out_buf_ofs, 0, want_from);
				Reset(f);
			}
		}

		for (miniz::tinfl_status status = miniz::TINFL_STATUS_NEEDS_MORE_INPUT; status == miniz::TINFL_STATUS_NEEDS_MORE_INPUT || status == miniz::TINFL_STATUS_HAS_MORE_OUTPUT || status == miniz::TINFL_STATUS_DONE;)
		{
			if (out_buf_ofs > want_from)
			{
				DBP_ASSERT(out_buf_ofs - want_from <= WRITE_BLOCK);
				Bit32u have_to = (out_buf_ofs > want_to ? want_to : out_buf_ofs);
				Bit32u have_size = have_to - want_from;
				memcpy(p_res, write_buf + (want_from & (WRITE_BLOCK-1)), have_size);
				if (have_to == want_to)
					return res_n;
				p_res += have_size;
				want_from = have_to;
			}
			DBP_ASSERT(out_buf_ofs != want_to && status != miniz::TINFL_STATUS_DONE);

			if (!read_buf_avail)
			{
				read_buf_avail = (comp_remaining < READ_BLOCK ? comp_remaining : READ_BLOCK);
				if (archive.Read(ofs, read_buf, read_buf_avail) != read_buf_avail)
					break;
				ofs_last_read = ofs;
				ofs += read_buf_avail;
				comp_remaining -= read_buf_avail;
				read_buf_ofs = 0;
			}

			Bit32u out_buf_size = WRITE_BLOCK - (out_buf_ofs & (WRITE_BLOCK-1));
			Bit8u *pWrite_buf_cur = write_buf + (out_buf_ofs & (WRITE_BLOCK-1));
			Bit32u in_buf_size = read_buf_avail;

			status = miniz::tinfl_decompress(&inflator, read_buf + read_buf_ofs, &in_buf_size, write_buf, pWrite_buf_cur, &out_buf_size, (comp_remaining ? miniz::TINFL_FLAG_HAS_MORE_INPUT : 0));

			read_buf_avail -= in_buf_size;
			read_buf_ofs += in_buf_size;
			out_buf_ofs += out_buf_size;
			if (out_buf_ofs > f.uncomp_size) { DBP_ASSERT(0); break; }

			if (inflator.m_state == miniz::TINFL_STATE_INDEX_BLOCK_BOUNDRY)
			{
				// Gear cursors toward the middle of the block to accomodate forward and backward seeking as well as possible
				Bit32u idx = (out_buf_ofs / cursor_block);
				if (!cursors[idx].cursor_out || (out_buf_ofs > cursors[idx].cursor_out + 120*1024 && out_buf_ofs < idx*cursor_block + cursor_block/2 + 70*1024))
				{
					//printf("[%s] STORE SEEK CURSOR #%u AT %u\n", f.name, idx, out_buf_ofs);
					cursors[idx].cursor_in = ofs_last_read + read_buf_ofs;
					cursors[idx].cursor_out = out_buf_ofs;
					cursors[idx].m_num_bits                = inflator.m_num_bits;
					cursors[idx].m_bit_buf                 = inflator.m_bit_buf;
					cursors[idx].m_dist                    = inflator.m_dist;
					cursors[idx].m_counter                 = inflator.m_counter;
					cursors[idx].m_num_extra               = inflator.m_num_extra;
					cursors[idx].m_dist_from_out_buf_start = inflator.m_dist_from_out_buf_start;
					memcpy(cursors[idx].write_buf, write_buf, sizeof(write_buf));

					// Write a seek cache next to the compressed file for larger files
					if (seek_cache && idx > 50 && (idx % SEEK_CACHE_CURSOR_STEPS) == 0)
					{
						Bit32u cursor_count = (f.uncomp_size + (cursor_block - 1)) / cursor_block, cursor_got = 0;
						for (Bit32u ii = 0; ii < cursor_count; ii += SEEK_CACHE_CURSOR_STEPS)
							if (cursors[ii].cursor_out)
								cursor_got++;
						//printf("[%s] CURSORS FOR SEEK CACHE: %d / %d\n", f.name, cursor_got, (cursor_count+(SEEK_CACHE_CURSOR_STEPS-1))/SEEK_CACHE_CURSOR_STEPS);
						if (cursor_got > cursor_count / (SEEK_CACHE_CURSOR_STEPS*2) && cursor_got > seek_cache->cache_count && (cursor_got >= seek_cache->cache_count + 5 || cursor_got == (cursor_count+(SEEK_CACHE_CURSOR_STEPS-1))/SEEK_CACHE_CURSOR_STEPS) && cursor_count <= 0xFFFF)
						{
							DOS_File *df;
							Bit8u drive_idx = DriveGetIndex(seek_cache->drv);
							if (drive_idx != DOS_DRIVES && Drives[drive_idx]->FileCreate(&df, (char*)seek_cache->path.c_str(), DOS_ATTR_ARCHIVE))
							{
								df->AddRef();
								sdefl* compressor = new sdefl;
								Bit8u* compbuf = new Bit8u[SEEK_CURSOR_MAX_DEFL];
								Bit16u hdr[4] = { (Bit16u)0x5344, (Bit16u)sizeof(SeekCursor), (Bit16u)(f.comp_size>>16), (Bit16u)f.comp_size }, idx_complen[2], sz;
								df->Write((Bit8u*)hdr, &(sz = (Bit16u)sizeof(hdr)));
								for (idx_complen[0] = 0; idx_complen[0] < cursor_count; idx_complen[0] += SEEK_CACHE_CURSOR_STEPS)
								{
									if (!cursors[idx_complen[0]].cursor_out) continue;
									Bit32u complen = compressor->Run(compbuf, (const unsigned char*)&cursors[idx_complen[0]], sizeof(SeekCursor));
									DBP_ASSERT(complen < SEEK_CURSOR_MAX_DEFL);
									idx_complen[1] = (complen < (sizeof(SeekCursor)-10) ? (Bit16u)complen : (Bit16u)0); // store compressed only when beneficial
									df->Write((Bit8u*)idx_complen, &(sz = (Bit16u)sizeof(idx_complen)));
									if (idx_complen[1]) df->Write((Bit8u*)compbuf, &idx_complen[1]);
									else df->Write((Bit8u*)&cursors[idx_complen[0]], &(sz = (Bit16u)sizeof(SeekCursor)));
								}
								df->Close();
								delete df;
								delete[] compbuf;
								delete compressor;
							}
							seek_cache->cache_count = cursor_got;
						}
					}
				}
			}
		}
		DBP_ASSERT(false);
		return (Bit32u)(p_res - (Bit8u*)res_buf);
	}
};

struct Zip_Handle : public DOS_File
{
	Bit32u ofs;
	Zip_File* src;

	Zip_Handle(Zip_Archive& archive, Zip_File* _src, Bit32u _flags, zipDrive* drv, const char* path) : ofs(0), src(_src)
	{
		_src->refs++;
		date = _src->date;
		time = _src->time;
		attr = _src->attr;
		flags = _flags;
		if (!_src->unpacker)
		{
			if (!_src->uncomp_size) _src->unpacker = nullptr;
			else if (_src->method == ZIP_Unpacker::METHOD_DEFLATED)
			{
				enum { MINIMAL_SIZE = (sizeof(Zip_DeflateUnpacker) + sizeof(Zip_DeflateUnpacker::SeekCursor)) };
				if (_src->uncomp_size > MINIMAL_SIZE) _src->unpacker = new Zip_DeflateUnpacker(archive, *_src, drv, path);
				else                                  _src->unpacker = new Zip_DeflateMemoryUnpacker(archive, *_src);
			}
			else if (_src->method == ZIP_Unpacker::METHOD_STORED)   _src->unpacker = new Zip_StoredUnpacker(archive);
			else if (_src->method == ZIP_Unpacker::METHOD_SHRUNK)   _src->unpacker = new Zip_ShrinkUnpacker(archive, *_src);
			else if (_src->method == ZIP_Unpacker::METHOD_IMPLODED) _src->unpacker = new Zip_ImplodeUnpacker(archive, *_src);
			else { DBP_ASSERT(0); _src->unpacker = nullptr; }
		}
		SetName(path);
		open = true;
	}

	~Zip_Handle()
	{
		DBP_ASSERT(!open && !src);
	}

	virtual bool Close()
	{
		//printf("[] [%s] CLOSING (handle references: %d - zip decompresssor references: %d)\n", name, refCtr, (src ? src->refs : -1));
		if (refCtr == 1)
		{
			src->refs--;
			src = NULL;
			open = false;
		}
		return true;
	}

	virtual bool Read(Bit8u* data, Bit16u* size)
	{
		//printf("[] [%s] READING %d BYTES\n", name, *size);
		if (!OPEN_IS_READING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (!src->unpacker) return FALSE_SET_DOSERR(INVALID_HANDLE);
		if (!*size) return true;
		if (ofs >= (Bit32u)src->uncomp_size) { *size = 0; return true; }
		Bit32u left = (src->uncomp_size - ofs), want = (left < *size ? left : *size);
		Bit32u read = src->unpacker->Read(*src, ofs, data, want);
		ofs += read;
		*size = (Bit16u)read;
		if (!read && want) return FALSE_SET_DOSERR(INVALID_DRIVE);

		// TODO ....
		//	/* Fake harddrive motion. Inspector Gadget with soundblaster compatible */
		//	/* Same for Igor */
		//	/* hardrive motion => unmask irq 2. Only do it when it's masked as unmasking is realitively heavy to emulate */
		//	Bit8u mask = IO_Read(0x21);
		//	if(mask & 0x4 ) IO_Write(0x21,mask&0xfb);

		//printf("[] [%s]     GOT %d BYTES [ %02x %02x %02x %02x ...]\n", name, *size, data[0], data[1], data[2], data[3]);
		return true;
	}

	virtual bool Write(Bit8u* data, Bit16u* size)
	{
		return FALSE_SET_DOSERR(ACCESS_DENIED);
	}

	virtual bool Seek(Bit32u* pos, Bit32u type)
	{
		//printf("[] [%s] SEEKING %d (type: %d)\n", name, *pos, type);
		Bit32s seekto;
		switch(type)
		{
			case DOS_SEEK_SET: seekto = (Bit32s)*pos; break;
			case DOS_SEEK_CUR: seekto = (Bit32s)*pos + (Bit32s)ofs; break;
			case DOS_SEEK_END: seekto = (Bit32s)src->uncomp_size + (Bit32s)*pos; break;
			default: return FALSE_SET_DOSERR(FUNCTION_NUMBER_INVALID);
		}
		if (seekto < 0) seekto = 0;
		*pos = ofs = (Bit32u)seekto;
		//printf("[] [%s]    SEEKED TO %d\n", name, *pos, type);
		return true;
	}

	virtual Bit16u GetInformation(void)
	{
		return 0x40; // read-only drive
	}
};

struct Zip_Directory: Zip_Entry
{
	StringToPointerHashMap<Zip_Entry> entries;
	Bit64u ofs;

	Zip_Directory(Bit16u _attr, const char* dirname, Bit16u _date, Bit16u _time, Bit64u _ofs) : Zip_Entry(_attr, dirname, _date, _time), ofs(_ofs) {}

	~Zip_Directory()
	{
		for (Zip_Entry* e : entries)
		{
			if (e->IsDirectory()) delete e->AsDirectory();
			else delete e->AsFile();
		}
	}
};

struct Zip_Search
{
	Zip_Directory* dir;
	Bit32u index;
};

struct zipDriveImpl
{
	Zip_Archive archive;
	Zip_Directory root;
	StringToPointerHashMap<Zip_Directory> directories;
	std::vector<Zip_Search> searches;
	std::vector<Bit16u> free_search_ids;
	Bit64u total_decomp_size;

	// Various ZIP archive enums. To completely avoid cross platform compiler alignment and platform endian issues, miniz.c doesn't use structs for any of this stuff.
	enum
	{
		MZ_METHOD_DEFLATED = 8,
		// ZIP archive identifiers and record sizes
		MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG = 0x06054b50, MZ_ZIP_CENTRAL_DIR_HEADER_SIG = 0x02014b50, MZ_ZIP_LOCAL_DIR_HEADER_SIG = 0x04034b50,
		MZ_ZIP_LOCAL_DIR_HEADER_SIZE = 30, MZ_ZIP_CENTRAL_DIR_HEADER_SIZE = 46, MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE = 22,
		MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIG = 0x06064b50, MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE = 56,
		MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIG = 0x07064b50, MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE = 20,
		// End of central directory offsets
		MZ_ZIP_ECDH_NUM_THIS_DISK_OFS = 4, MZ_ZIP_ECDH_NUM_DISK_CDIR_OFS = 6, MZ_ZIP_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS = 8,
		MZ_ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS = 10, MZ_ZIP_ECDH_CDIR_SIZE_OFS = 12, MZ_ZIP_ECDH_CDIR_OFS_OFS = 16, MZ_ZIP_ECDH_COMMENT_SIZE_OFS = 20,
		MZ_ZIP64_ECDL_ECDH_OFS_OFS = 8, MZ_ZIP64_ECDH_SIZE = 4, MZ_ZIP64_ECDH_NUM_THIS_DISK_OFS = 16, MZ_ZIP64_ECDH_NUM_DISK_CDIR_OFS = 20,
		MZ_ZIP64_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS = 24, MZ_ZIP64_ECDH_CDIR_TOTAL_ENTRIES_OFS = 32, MZ_ZIP64_ECDH_CDIR_SIZE_OFS = 40, MZ_ZIP64_ECDH_CDIR_OFS_OFS = 48,
		// Central directory header record offsets
		MZ_ZIP_CDH_VERSION_MADE_BY_OFS = 4, MZ_ZIP_CDH_VERSION_NEEDED_OFS = 6, MZ_ZIP_CDH_BIT_FLAG_OFS = 8,
		MZ_ZIP_CDH_METHOD_OFS = 10, MZ_ZIP_CDH_FILE_TIME_OFS = 12, MZ_ZIP_CDH_FILE_DATE_OFS = 14, MZ_ZIP_CDH_CRC32_OFS = 16,
		MZ_ZIP_CDH_COMPRESSED_SIZE_OFS = 20, MZ_ZIP_CDH_DECOMPRESSED_SIZE_OFS = 24, MZ_ZIP_CDH_FILENAME_LEN_OFS = 28, MZ_ZIP_CDH_EXTRA_LEN_OFS = 30,
		MZ_ZIP_CDH_COMMENT_LEN_OFS = 32, MZ_ZIP_CDH_DISK_START_OFS = 34, MZ_ZIP_CDH_INTERNAL_ATTR_OFS = 36, MZ_ZIP_CDH_EXTERNAL_ATTR_OFS = 38, MZ_ZIP_CDH_LOCAL_HEADER_OFS = 42,
		// Local directory header offsets
		MZ_ZIP_LDH_SIG_OFS = 0, MZ_ZIP_LDH_VERSION_NEEDED_OFS = 4, MZ_ZIP_LDH_BIT_FLAG_OFS = 6, MZ_ZIP_LDH_METHOD_OFS = 8, MZ_ZIP_LDH_FILE_TIME_OFS = 10,
		MZ_ZIP_LDH_FILE_DATE_OFS = 12, MZ_ZIP_LDH_CRC32_OFS = 14, MZ_ZIP_LDH_COMPRESSED_SIZE_OFS = 18, MZ_ZIP_LDH_DECOMPRESSED_SIZE_OFS = 22,
		MZ_ZIP_LDH_FILENAME_LEN_OFS = 26, MZ_ZIP_LDH_EXTRA_LEN_OFS = 28,
	};

	zipDriveImpl(DOS_File* _zip, bool enter_solo_root_dir) : root(DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY, "", 0xFFFF, 0xFFFF, 0), archive(_zip), total_decomp_size(0)
	{
		// Basic sanity checks - reject files which are too small.
		if (archive.size < MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)
			return;

		// Find the end of central directory record by scanning the file from the end towards the beginning.
		Bit8u buf[4096];
		Bit64u ecdh_ofs = (archive.size < sizeof(buf) ? 0 : archive.size - sizeof(buf));
		for (;; ecdh_ofs = MZ_MAX(ecdh_ofs - (sizeof(buf) - 3), 0))
		{
			Bit32s i, n = (Bit32s)MZ_MIN(sizeof(buf), archive.size - ecdh_ofs);
			if (archive.Read(ecdh_ofs, buf, (Bit32s)n) != (Bit32s)n) return;
			for (i = n - 4; i >= 0; --i) { if (MZ_READ_LE32(buf + i) == MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG) break; }
			if (i >= 0) { ecdh_ofs += i; break; }
			if (!ecdh_ofs || (archive.size - ecdh_ofs) >= (0xFFFF + MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)) return;
		}

		// Read and verify the end of central directory record.
		if (archive.Read(ecdh_ofs, buf, MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) != MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)
			return;

		Bit64u total_files = MZ_READ_LE16(buf + MZ_ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS);
		Bit64u cdir_size   = MZ_READ_LE32(buf + MZ_ZIP_ECDH_CDIR_SIZE_OFS);
		Bit64u cdir_ofs    = MZ_READ_LE32(buf + MZ_ZIP_ECDH_CDIR_OFS_OFS);

		if ((cdir_ofs == 0xFFFFFFFF || cdir_size == 0xFFFFFFFF || total_files == 0xFFFF)
			&& ecdh_ofs >= (MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE + MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE)
			&& archive.Read(ecdh_ofs - MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE, buf, MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE) == MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE
			&& MZ_READ_LE32(buf) == MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIG)
		{
			Bit64u ecdh64_ofs = MZ_READ_LE64(buf + MZ_ZIP64_ECDL_ECDH_OFS_OFS);
			if (ecdh64_ofs <= (archive.size - MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE)
				&& archive.Read(ecdh64_ofs, buf, MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE) == MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE
				&& MZ_READ_LE32(buf) == MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIG)
			{
				total_files = MZ_READ_LE64(buf + MZ_ZIP64_ECDH_CDIR_TOTAL_ENTRIES_OFS);
				cdir_size   = MZ_READ_LE64(buf + MZ_ZIP64_ECDH_CDIR_SIZE_OFS);
				cdir_ofs    = MZ_READ_LE64(buf + MZ_ZIP64_ECDH_CDIR_OFS_OFS);
			}
		}

		if (!total_files
			|| (cdir_size >= 0x10000000) // limit to 256MB content directory
			|| (cdir_size < total_files * MZ_ZIP_CENTRAL_DIR_HEADER_SIZE)
			|| ((cdir_ofs + cdir_size) > archive.size)
			) return;

		void* m_central_dir = malloc((size_t)cdir_size);
		if (archive.Read(cdir_ofs, m_central_dir, (Bit32u)cdir_size) != cdir_size)
		{
			free(m_central_dir);
			return;
		}
		const Bit8u *cdir_start = (const Bit8u*)m_central_dir, *cdir_end = cdir_start + cdir_size, *p = cdir_start;

		Bit32u skip_root_dir_len = 0;
		if (enter_solo_root_dir)
		{
			// Find out if everything is inside a single root directory to skip that part of the tree
			for (Bit32u i = 0, header_len; i < total_files && p >= cdir_start && p < cdir_end && MZ_READ_LE32(p) == MZ_ZIP_CENTRAL_DIR_HEADER_SIG; i++, p += header_len)
			{
				Bit32u filename_len = MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);
				const char *name = (const char*)(p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE), *name_end = name + filename_len, *first_slash = name + 1;
				while (first_slash != name_end && *first_slash != '/' && *first_slash != '\\') first_slash++;
				if (first_slash == name_end && !(MZ_READ_LE32(p + MZ_ZIP_CDH_EXTERNAL_ATTR_OFS) & 0x10)) { skip_root_dir_len = 0; break; }
				Bit32u root_dir_len = (Bit32u)(first_slash - name);
				if (skip_root_dir_len && skip_root_dir_len != root_dir_len) { skip_root_dir_len = 0; break; }
				if (skip_root_dir_len && memcmp(name, p - header_len + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE, root_dir_len)) { skip_root_dir_len = 0; break; }
				skip_root_dir_len = root_dir_len;
				header_len = MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_len + MZ_READ_LE16(p + MZ_ZIP_CDH_EXTRA_LEN_OFS) + MZ_READ_LE16(p + MZ_ZIP_CDH_COMMENT_LEN_OFS);
			}
		}

		// Now create an index into the central directory file records, do some basic sanity checking on each record, and check for zip64 entries (which are not yet supported).
		p = cdir_start;
		for (Bit32u i = 0, total_header_size; i < total_files && p >= cdir_start && p < cdir_end && MZ_READ_LE32(p) == MZ_ZIP_CENTRAL_DIR_HEADER_SIG; i++, p += total_header_size)
		{
			Bit32u bit_flag         = MZ_READ_LE16(p + MZ_ZIP_CDH_BIT_FLAG_OFS);
			Bit32u method           = MZ_READ_LE16(p + MZ_ZIP_CDH_METHOD_OFS);
			Bit16u file_time        = MZ_READ_LE16(p + MZ_ZIP_CDH_FILE_TIME_OFS);
			Bit16u file_date        = MZ_READ_LE16(p + MZ_ZIP_CDH_FILE_DATE_OFS);
			Bit64u comp_size        = MZ_READ_LE32(p + MZ_ZIP_CDH_COMPRESSED_SIZE_OFS);
			Bit64u decomp_size      = MZ_READ_LE32(p + MZ_ZIP_CDH_DECOMPRESSED_SIZE_OFS);
			Bit32u filename_len     = MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);
			Bit32s extra_len        = MZ_READ_LE16(p + MZ_ZIP_CDH_EXTRA_LEN_OFS);
			Bit64u local_header_ofs = MZ_READ_LE32(p + MZ_ZIP_CDH_LOCAL_HEADER_OFS);
			total_header_size = MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_len + extra_len + MZ_READ_LE16(p + MZ_ZIP_CDH_COMMENT_LEN_OFS);

			if (!ZIP_Unpacker::MethodSupported(method)
				|| (p + total_header_size > cdir_end)
				|| (bit_flag & (1 | 32)) // Encryption and patch files are not supported.
				) { invalid_cdh: continue; }

			if (decomp_size == 0xFFFFFFFF || comp_size == 0xFFFFFFFF || local_header_ofs == 0xFFFFFFFF)
			{
				for (const Bit8u *x = p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_len, *xEnd = x + extra_len; (x + (sizeof(Bit16u) * 2)) < xEnd;)
				{
					const Bit8u *field = x + (sizeof(Bit16u) * 2), *fieldEnd = field + MZ_READ_LE16(x + 2);
					if (MZ_READ_LE16(x) != 0x0001 || fieldEnd > xEnd) { x = fieldEnd; continue; } // Not Zip64 extended information extra field
					if (decomp_size == 0xFFFFFFFF)
					{
						if (fieldEnd - field < sizeof(Bit64u)) goto invalid_cdh;
						decomp_size = MZ_READ_LE64(field);
						field += sizeof(Bit64u);
					}
					if (comp_size == 0xFFFFFFFF)
					{
						if (fieldEnd - field < sizeof(Bit64u)) goto invalid_cdh;
						comp_size = MZ_READ_LE64(field);
						field += sizeof(Bit64u);
					}
					if (local_header_ofs == 0xFFFFFFFF)
					{
						if (fieldEnd - field < sizeof(Bit64u)) goto invalid_cdh;
						local_header_ofs = MZ_READ_LE64(field);
						field += sizeof(Bit64u);
					}
					break;
				}
			}

			if (((!method) && (decomp_size != comp_size)) || (decomp_size && !comp_size)
				|| (decomp_size > 0xFFFFFFFF) || (comp_size > 0xFFFFFFFF) // not supported on DOS file systems
				|| ((local_header_ofs + MZ_ZIP_LOCAL_DIR_HEADER_SIZE + comp_size) > archive.size)
				) continue;

			total_decomp_size += decomp_size;
			if (file_date < root.date) root.date = file_date;
			if (file_time < root.time) root.time = file_time;

			const char *name = (const char*)(p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE);
			char dos_path[DOS_PATHLENGTH + 1], *p_dos = dos_path;
			bool is_dir = (name[filename_len-1] == '/' || name[filename_len-1] == '\\' || (MZ_READ_LE32(p + MZ_ZIP_CDH_EXTERNAL_ATTR_OFS) & 0x10));
			if (skip_root_dir_len) { name += skip_root_dir_len; filename_len -= skip_root_dir_len; }

			Zip_Directory *parent = &root;
			for (const char *n = name, *nDir = n, *nEnd = n + filename_len; n != nEnd + 1 && p_dos - dos_path != DOS_PATHLENGTH; n++)
			{
				if (n != nEnd && *n != '/' && *n != '\\') continue;
				if (n == nDir) { nDir++; continue; }

				// Create a 8.3 filename from a 4 char prefix and a suffix if filename is too long
				Bit32u dos_len = DBP_Make8dot3FileName(p_dos, (Bit32u)(dos_path + DOS_PATHLENGTH - p_dos), nDir, (Bit32u)(n - nDir));
				p_dos[dos_len] = '\0';

				if (n == nEnd && !is_dir)
				{
					Zip_File* zfile;
					while (parent->entries.Get(p_dos))
					{
						// A file or directory already exists with the same name try changing some characters until it's unique
						const char* p_dos_dot = strchr(p_dos, '.');
						Bit32u baseLen = (p_dos_dot ? (Bit32u)(p_dos_dot - p_dos) : dos_len), j = (baseLen > 8 ? 4 : baseLen / 2);
						if      (baseLen >= 1 && p_dos[j  ] && p_dos[j  ] < '~') p_dos[j  ]++;
						else if (baseLen >= 3 && p_dos[j+1] && p_dos[j+1] < '~') p_dos[j+1]++;
						else if (baseLen >= 5 && p_dos[j+2] && p_dos[j+2] < '~') p_dos[j+2]++;
						else goto skip_zip_entry;
					}
					zfile = new Zip_File(DOS_ATTR_ARCHIVE, p_dos, file_date, file_time, local_header_ofs, (Bit32u)comp_size, (Bit32u)decomp_size, (Bit8u)bit_flag, (Bit8u)method);
					parent->entries.Put(p_dos, zfile);
					skip_zip_entry:
					break;
				}
				Zip_Directory* zdir = directories.Get(dos_path);
				if (!zdir)
				{
					if (parent->entries.Get(p_dos)) break; // Skip if directory (or a file) already exists with the same name
					zdir = new Zip_Directory(DOS_ATTR_DIRECTORY, p_dos, file_date, file_time, local_header_ofs);
					parent->entries.Put(p_dos, zdir);
					directories.Put(dos_path, zdir);
				}
				if (n + 1 >= nEnd) break;
				parent = zdir;
				p_dos += dos_len;
				*(p_dos++) = '\\';
				nDir = n + 1;
			}
		}
		free(m_central_dir);
		if (root.time == 0xFFFF) root.time = root.date = 0;
	}

	bool SetOfsPastHeader(Zip_File& f)
	{
		char local_header[MZ_ZIP_LOCAL_DIR_HEADER_SIZE];
		if (archive.Read(f.data_ofs, local_header, MZ_ZIP_LOCAL_DIR_HEADER_SIZE) != MZ_ZIP_LOCAL_DIR_HEADER_SIZE)
			return false;
		if (MZ_READ_LE32(local_header) != MZ_ZIP_LOCAL_DIR_HEADER_SIG)
			return false;
		unsigned ofs = MZ_ZIP_LOCAL_DIR_HEADER_SIZE + MZ_READ_LE16(local_header + MZ_ZIP_LDH_FILENAME_LEN_OFS) + MZ_READ_LE16(local_header + MZ_ZIP_LDH_EXTRA_LEN_OFS);
		f.data_ofs += ofs;
		if ((f.data_ofs + f.comp_size) > archive.size)
			return false;
		f.ofs_past_header = (ofs > 0xFFFF ? (Bit16u)0xFFFF : (Bit16u)ofs);
		return true;
	}

	Zip_Entry* Get(const char* path)
	{
		const char *lastslash = strrchr(path, '\\');
		if (!lastslash) return root.entries.Get(path);
		Zip_Directory* dir = directories.Get(path, (Bit16u)(lastslash - path));
		return (dir ? dir->entries.Get(lastslash + 1) : NULL);
	}
};

zipDrive::zipDrive(DOS_File* zip, bool enter_solo_root_dir) : impl(new zipDriveImpl(zip, enter_solo_root_dir))
{
	label.SetLabel("ZIP", false, true);
}

zipDrive::~zipDrive()
{
	ForceCloseAll();
	delete impl;
}

bool zipDrive::FileOpen(DOS_File * * file, char * name, Bit32u flags)
{
	if (!OPEN_CHECK_ACCESS_CODE(flags)) return FALSE_SET_DOSERR(ACCESS_CODE_INVALID);
	if (OPEN_IS_WRITING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);

	DOSPATH_REMOVE_ENDINGDOTS(name);
	Zip_Entry* e = impl->Get(name);
	if (!e || e->IsDirectory()) return FALSE_SET_DOSERR(FILE_NOT_FOUND);

	if (!e->AsFile()->ofs_past_header && !impl->SetOfsPastHeader(*e->AsFile()))
		return FALSE_SET_DOSERR(DATA_INVALID); //ZIP error

	*file = new Zip_Handle(impl->archive, e->AsFile(), flags, this, name);
	return true;
}

bool zipDrive::FileCreate(DOS_File * * file, char * path, Bit16u attributes)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool zipDrive::Rename(char * oldpath, char * newpath)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool zipDrive::FileUnlink(char * path)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool zipDrive::FileExists(const char* name)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Zip_Entry* p = impl->Get(name);
	return (p && p->IsFile());
}

bool zipDrive::RemoveDir(char* dir_path)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool zipDrive::MakeDir(char* dir_path)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool zipDrive::TestDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	return (!dir_path[0] || impl->directories.Get(dir_path));
}

bool zipDrive::FindFirst(char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	Zip_Directory* dir = (!dir_path[0] ? &impl->root : impl->directories.Get(dir_path));
	if (!dir) return FALSE_SET_DOSERR(PATH_NOT_FOUND);

	Zip_Search s = { dir, 0 };
	if (impl->free_search_ids.empty())
	{
		dta.SetDirID((Bit16u)impl->searches.size());
		impl->searches.push_back(s);
	}
	else
	{
		dta.SetDirID(impl->free_search_ids.back());
		impl->free_search_ids.pop_back();
		impl->searches[dta.GetDirID()] = s;
	}

	if (DriveFindDriveVolume(this, dir_path, dta, fcb_findfirst)) return true;
	return FindNext(dta);
}

bool zipDrive::FindNext(DOS_DTA & dta)
{
	if (dta.GetDirID() >= impl->searches.size()) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Zip_Search& s = impl->searches[dta.GetDirID()];
	if (!s.dir) return FALSE_SET_DOSERR(NO_MORE_FILES);
	Bit8u attr;char pattern[DOS_NAMELENGTH_ASCII];
	dta.GetSearchParams(attr,pattern);
	while (s.index < 2)
	{
		const char* dotted = (s.index++ ? ".." : ".");
		if (!WildFileCmp(dotted, pattern) || (s.dir->attr & DOS_ATTR_VOLUME)) continue;
		if (~attr & (Bit8u)s.dir->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
		dta.SetResult(dotted, 0, s.dir->date, s.dir->time, (Bit8u)s.dir->attr);
		return true;
	}
	for (Bit32u i = (s.index++ - 2), end = s.dir->entries.Capacity(); i != end; i++, s.index++)
	{
		Zip_Entry* e = s.dir->entries.GetAtIndex(i);
		if (!e || !WildFileCmp(e->name, pattern)) continue;
		if (~attr & (Bit8u)e->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
		dta.SetResult(e->name, (e->IsFile() ? e->AsFile()->uncomp_size : 0), e->date, e->time, (Bit8u)e->attr);
		return true;
	}
	s.dir = NULL;
	impl->free_search_ids.push_back(dta.GetDirID());
	return FALSE_SET_DOSERR(NO_MORE_FILES);
}

bool zipDrive::FileStat(const char* name, FileStat_Block * const stat_block)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Zip_Entry* p = impl->Get(name);
	if (!p) return false;
	stat_block->attr = p->attr;
	stat_block->size = (p->IsFile() ? p->AsFile()->uncomp_size : 0);
	stat_block->date = p->date;
	stat_block->time = p->time;
	return true;
}

bool zipDrive::GetFileAttr(char * name, Bit16u * attr)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Zip_Entry* p = impl->Get(name);
	if (!p) return false;
	*attr = p->attr;
	return true;
}

bool zipDrive::GetLongFileName(const char* path, char longname[256])
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	Zip_Entry* e = impl->Get(path);
	if (!e || !*path || (e->IsFile() && e->AsFile()->ofs_past_header == 0xFFFF)) return false;

	Bit64u ldh_ofs = (e->IsFile() ? e->AsFile()->data_ofs - e->AsFile()->ofs_past_header : e->AsDirectory()->ofs);
	char ldh[zipDriveImpl::MZ_ZIP_LOCAL_DIR_HEADER_SIZE + CROSS_LEN * 2];
	if (impl->archive.Read(ldh_ofs, ldh, sizeof(ldh)) <= zipDriveImpl::MZ_ZIP_LOCAL_DIR_HEADER_SIZE || MZ_READ_LE32(ldh) != zipDriveImpl::MZ_ZIP_LOCAL_DIR_HEADER_SIG)
		return false;

	char *ldh_path = ldh + zipDriveImpl::MZ_ZIP_LOCAL_DIR_HEADER_SIZE, *ldh_path_end = ldh_path + MZ_READ_LE16(ldh + zipDriveImpl::MZ_ZIP_LDH_FILENAME_LEN_OFS), *ldh_fname = ldh_path;
	if (ldh_path_end <= ldh_path || ldh_path_end > ldh + sizeof(ldh)) return false;
	if (ldh_path_end[-1] == '/' || ldh_path_end[-1] == '\\') ldh_path_end--;
	if (!e->IsFile()) // cut off directory name in full file path
	{
		int slashes = 0;
		for (const char *p = path; *p; p++) if (*p == '\\') slashes++;
		for (char *p = ldh_path; p != ldh_path_end; p++) { if ((*p == '/' || *p == '\\') && !slashes--) { ldh_path_end = p; break; } }
	}
	for (char *p = ldh_path; p < ldh_path_end - 1; p++) if (*p == '/' || *p == '\\') ldh_fname = p + 1;

	size_t p_name_len = strlen(e->name), ldh_fname_len = ldh_path_end - ldh_fname;
	if (ldh_fname_len > (256 - 1) || (ldh_fname_len == p_name_len && !memcmp(e->name, ldh_fname, p_name_len))) return false;

	memcpy(longname, ldh_fname, ldh_fname_len);
	longname[ldh_fname_len] = '\0';
	return true;
}

bool zipDrive::AllocationInfo(Bit16u * _bytes_sector, Bit8u * _sectors_cluster, Bit16u * _total_clusters, Bit16u * _free_clusters)
{
	// return dummy numbers (up to almost 4 GB of total decomp size)
	Bit32u show_size = (impl->total_decomp_size > (0xffffffff-(512*224-1)) ? (0xffffffff-(512*224-1)) : (Bit32u)impl->total_decomp_size);
	Bit8u sectors = (Bit8u)(show_size > (32<<24) ? (show_size>>29<<5): 32);
	*_bytes_sector = 512;
	*_sectors_cluster = sectors;
	*_total_clusters = (Bit16u)(((Bit64u)show_size + (512 * sectors - 1)) / (512 * sectors));
	*_free_clusters = 0;
	return true;
}

Bit8u zipDrive::GetMediaByte(void) { return 0xF8;  } //Hard Disk
bool zipDrive::isRemote(void) { return false; }
bool zipDrive::isRemovable(void) { return false; }
Bits zipDrive::UnMount(void) { delete this; return 0;  }

void zipDrive::Uncompress(const Bit8u* src, Bit32u src_len, Bit8u* trg, Bit32u trg_len)
{
	miniz::tinfl_decompressor inflator;
	miniz::tinfl_init(&inflator);
	const Bit8u *src_end = src + src_len, *trg_start = trg, *trg_end = trg + trg_len;
	for (miniz::tinfl_status status = miniz::TINFL_STATUS_HAS_MORE_OUTPUT; status == miniz::TINFL_STATUS_HAS_MORE_OUTPUT;)
	{
		Bit32u in_size = (Bit32u)(src_end - src), out_size = (Bit32u)(trg_end - trg);
		status = miniz::tinfl_decompress(&inflator, src, &in_size, (Bit8u*)trg_start, trg, &out_size, miniz::TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
		src += in_size;
		trg += out_size;
		DBP_ASSERT(status == miniz::TINFL_STATUS_HAS_MORE_OUTPUT || status == miniz::TINFL_STATUS_DONE);
	}
}
