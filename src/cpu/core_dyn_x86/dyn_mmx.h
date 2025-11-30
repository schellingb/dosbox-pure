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

#include "mmx.h"

extern Bit32u * lookupRMEAregd[256];

static MMX_reg mmxtmp;

static void MMX_LOAD_32(PhysPt addr)
{
	mmxtmp.ud.d0 = mem_readd_inline(addr);
}

static void MMX_STORE_32(PhysPt addr)
{
	mem_writed_inline(addr, mmxtmp.ud.d0);
}

static void MMX_LOAD_64(PhysPt addr)
{
	mmxtmp.q = mem_readq_inline(addr);
}

static void MMX_STORE_64(PhysPt addr)
{
	mem_writeq_inline(addr, mmxtmp.q);
}

// add simple instruction (that operates only with mm regs)
static void dyn_mmx_simple(Bitu op, Bit8u modrm)
{
	cache_addw((Bit16u)(0x0F | (op << 8)));
	cache_addb(modrm);
}

// same but with imm8 also
static void dyn_mmx_simple_imm8(Bit8u op, Bit8u modrm, Bit8u imm)
{
	cache_addw(0x0F | (op << 8));
	cache_addb(modrm);
	cache_addb(imm);
}

static void dyn_mmx_mem(Bitu op, Bitu reg = decode.modrm.reg, void* mem = &mmxtmp)
{
#if C_TARGETCPU == X86
	cache_addw(0x0F | (op << 8));
	cache_addb(0x05 | (reg << 3));
	cache_addd((Bit32u)(mem));
#else // X86_64
	opcode((int)reg).setabsaddr(mem).Emit16((Bit16u)(0x0F | (op << 8)));
#endif
}

static void dyn_mmx_op(Bitu op)
{
	// Bitu imm = 0;
	dyn_get_modrm();

	if (decode.modrm.mod < 3) {
		dyn_fill_ea();
		gen_call_function((void*)&MMX_LOAD_64, "%Drd", DREG(EA));
		dyn_mmx_mem(op);
	} else {
		dyn_mmx_simple(op, decode.modrm.val);
	}
}

// mmx SHIFT mm, imm8 template
static void dyn_mmx_shift_imm8(Bit8u op)
{
	dyn_get_modrm();
	Bitu imm;
	decode_fetchb_imm(imm);

	dyn_mmx_simple_imm8(op, (Bit8u)decode.modrm.val, (Bit8u)imm);
}

// 0x6E - MOVD mm, r/m32
static void dyn_mmx_movd_pqed()
{
	dyn_get_modrm();

	if (decode.modrm.mod < 3) {
		// movd mm, m32 - resolve EA and load data
		dyn_fill_ea();
		// generate call to mmxtmp load
		gen_call_function((void*)&MMX_LOAD_32, "%Drd", DREG(EA));
		// mmxtmp contains loaded value - finish by loading it to mm
		dyn_mmx_mem(0x6E); // movd  mm, m32
	} else {
		// movd mm, r32 - r32->mmxtmp->mm
		gen_save_host((void*)&mmxtmp, &DynRegs[decode.modrm.rm], 4);
		// mmxtmp contains loaded value - finish by loading it to mm
		dyn_mmx_mem(0x6E); // movd  mm, m32
	}
}

// 0x6F - MOVQ mm, mm/m64
static void dyn_mmx_movq_pqqq()
{
	dyn_get_modrm();

	if (decode.modrm.mod < 3) {
		// movq mm, m64 - resolve EA and load data
		dyn_fill_ea();
		// generate call to mmxtmp load
		gen_call_function((void*)&MMX_LOAD_64, "%Drd", DREG(EA));
		// mmxtmp contains loaded value - finish by loading it to mm
		dyn_mmx_mem(0x6F); // movq  mm, m64
	} else {
		// movq mm, mm
		dyn_mmx_simple(0x6F, (Bit8u)decode.modrm.val);
	}
}

// 0x7E - MOVD r/m32, mm
static void dyn_mmx_movd_edpq()
{
	dyn_get_modrm();

	if (decode.modrm.mod < 3) {
		// movd m32, mm - resolve EA and load data
		dyn_fill_ea();
		// fill mmxtmp
		dyn_mmx_mem(0x7E); // movd  mm, m32
		// generate call to mmxtmp store
		gen_call_function((void*)&MMX_STORE_32, "%Drd", DREG(EA));
	} else {
		// movd r32, mm - mm->mmxtmp->r32
		// fill mmxtmp
		dyn_mmx_mem(0x7E); // movd  mm, m32
		// move from mmxtmp to genreg
		gen_load_host((void*)&mmxtmp, &DynRegs[decode.modrm.rm], 4);
	}
}

// 0x7F - MOVQ mm/m64, mm
static void dyn_mmx_movq_qqpq()
{
	dyn_get_modrm();

	if (decode.modrm.mod < 3) {
		// movq m64, mm - resolve EA and load data
		dyn_fill_ea();
		// fill mmxtmp
		dyn_mmx_mem(0x7F); // movq  mm, m64
		// generate call to mmxtmp store
		gen_call_function((void*)&MMX_STORE_64, "%Drd", DREG(EA));
	} else {
		// movq mm, mm
		dyn_mmx_simple(0x7F, (Bit8u)decode.modrm.val);
	}
}

// 0x77 - EMMS
static void dyn_mmx_emms() {
	// gen_call_function((void*)&setFPUTagEmpty, "");
	cache_addw(0x770F);
}

#define dyn_x86_mmx_check() \
	if (CPU_ArchitectureType<CPU_ARCHTYPE_PENTIUM_MMX) goto illegalopcode; \
	if ((dyn_dh_fpu.dh_fpu_enabled) && (!fpu_used)) {dh_fpu_startup();}

//-------------------------------------------------------------------------
#define dyn_x86_mmx_ops                                                   \
	/* OP mm, mm/m64 */                                                   \
	/* pack/unpacks, compares */                                          \
	case 0x60:case 0x61:case 0x62:case 0x63:case 0x64:case 0x65:          \
	case 0x66:case 0x67:case 0x68:case 0x69:case 0x6a:case 0x6b:          \
	case 0x74:case 0x75:case 0x76:                                        \
	/* mm-directed shifts, add/sub, bitwise, multiplies */                \
	case 0xd1:case 0xd2:case 0xd3:case 0xd4:case 0xd5:case 0xd8:          \
	case 0xd9:case 0xdb:case 0xdc:case 0xdd:case 0xdf:case 0xe1:          \
	case 0xe2:case 0xe5:case 0xe8:case 0xe9:case 0xeb:case 0xec:          \
	case 0xed:case 0xef:case 0xf1:case 0xf2:case 0xf3:case 0xf5:          \
	case 0xf8:case 0xf9:case 0xfa:case 0xfc:case 0xfd:case 0xfe:          \
		dyn_x86_mmx_check(); dyn_mmx_op(dual_code); break;                \
	/* SHIFT mm, imm8*/                                                   \
	case 0x71:case 0x72:case 0x73:                                        \
		dyn_x86_mmx_check(); dyn_mmx_shift_imm8((Bit8u)dual_code); break; \
	/* MOVD mm, r/m32 */                                                  \
	case 0x6e: dyn_x86_mmx_check(); dyn_mmx_movd_pqed(); break;           \
	/* MOVQ mm, mm/m64 */                                                 \
	case 0x6f: dyn_x86_mmx_check(); dyn_mmx_movq_pqqq(); break;           \
	/* MOVD r/m32, mm */                                                  \
	case 0x7e: dyn_x86_mmx_check(); dyn_mmx_movd_edpq(); break;           \
	/* MOVQ mm/m64, mm */                                                 \
	case 0x7f: dyn_x86_mmx_check(); dyn_mmx_movq_qqpq(); break;           \
	/* EMMS */                                                            \
	case 0x77: dyn_x86_mmx_check(); dyn_mmx_emms(); break;                \
//-------------------------------------------------------------------------
