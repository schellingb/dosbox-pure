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

enum STRING_OP {
	R_OUTSB,R_OUTSW,R_OUTSD,
	R_INSB,R_INSW,R_INSD,
	R_MOVSB,R_MOVSW,R_MOVSD,
	R_LODSB,R_LODSW,R_LODSD,
	R_STOSB,R_STOSW,R_STOSD,
	R_SCASB,R_SCASW,R_SCASD,
	R_CMPSB,R_CMPSW,R_CMPSD
};

#define LoadD(_BLAH) _BLAH

static void DoString(STRING_OP type) {
	PhysPt  si_base,di_base;
	Bitu	si_index,di_index;
	Bitu	add_mask;
	Bitu	count,count_left;
	Bits	add_index;
	
	si_base=BaseDS;
	di_base=SegBase(es);
	add_mask=AddrMaskTable[core.prefixes & PREFIX_ADDR];
	si_index=reg_esi & add_mask;
	di_index=reg_edi & add_mask;
	count=reg_ecx & add_mask;
	add_index=cpu.direction;
	if (!TEST_PREFIX_REP) {
		count=1;
	}
	//DBP: Added implementation of accuracy improvements and page fault handling from DOSBox-X by Jonathan Campbell
	//     Source: https://github.com/joncampbell123/dosbox-x/commit/59bcdd3
	if (count) { PAGE_FAULT_CLEANUP_TRY() switch (type) {
	case R_OUTSB:
		do {
			IO_WriteB(reg_dx,LoadMb(si_base+si_index));
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_OUTSW:
		add_index<<=1;
		do {
			IO_WriteW(reg_dx,LoadMw(si_base+si_index));
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_OUTSD:
		add_index<<=2;
		do {
			IO_WriteD(reg_dx,LoadMd(si_base+si_index));
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_INSB:
		do {
			SaveMb(di_base+di_index,IO_ReadB(reg_dx));
			di_index=(di_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_INSW:
		add_index<<=1;
		do {
			SaveMw(di_base+di_index,IO_ReadW(reg_dx));
			di_index=(di_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_INSD:
		add_index<<=2;
		do {
			SaveMd(di_base+di_index,IO_ReadD(reg_dx));
			di_index=(di_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_STOSB:
		do {
			SaveMb(di_base+di_index,reg_al);
			di_index=(di_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_STOSW:
		add_index<<=1;
		do {
			SaveMw(di_base+di_index,reg_ax);
			di_index=(di_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_STOSD:
		add_index<<=2;
		do {
			SaveMd(di_base+di_index,reg_eax);
			di_index=(di_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_MOVSB:
		do {
			SaveMb(di_base+di_index,LoadMb(si_base+si_index));
			di_index=(di_index+add_index) & add_mask;
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_MOVSW:
		add_index<<=1;
		do {
			SaveMw(di_base+di_index,LoadMw(si_base+si_index));
			di_index=(di_index+add_index) & add_mask;
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_MOVSD:
		add_index<<=2;
		do {
			SaveMd(di_base+di_index,LoadMd(si_base+si_index));
			di_index=(di_index+add_index) & add_mask;
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_LODSB:
		do {
			reg_al=LoadMb(si_base+si_index);
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_LODSW:
		add_index<<=1;
		do {
			reg_ax=LoadMw(si_base+si_index);
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_LODSD:
		add_index<<=2;
		do {
			reg_eax=LoadMd(si_base+si_index);
			si_index=(si_index+add_index) & add_mask;
			count--;
		} while (--CPU_Cycles > 0 && count != 0); break;
	case R_SCASB:
		{
			Bit8u val2;
			do {
				val2=LoadMb(di_base+di_index);
				di_index=(di_index+add_index) & add_mask;
				count--;
			} while (--CPU_Cycles > 0 && (reg_al==val2)==core.rep_zero && count != 0);
			CMPB(reg_al,val2,LoadD,0);
		}
		break;
	case R_SCASW:
		{
			add_index<<=1;Bit16u val2;
			do {
				val2=LoadMw(di_base+di_index);
				di_index=(di_index+add_index) & add_mask;
				count--;
			} while (--CPU_Cycles > 0 && (reg_ax==val2)==core.rep_zero && count != 0);
			CMPW(reg_ax,val2,LoadD,0);
		}
		break;
	case R_SCASD:
		{
			add_index<<=2;Bit32u val2;
			do {
				val2=LoadMd(di_base+di_index);
				di_index=(di_index+add_index) & add_mask;
				count--;
			} while (--CPU_Cycles > 0 && (reg_eax==val2)==core.rep_zero && count != 0);
			CMPD(reg_eax,val2,LoadD,0);
		}
		break;
	case R_CMPSB:
		{
			Bit8u val1,val2;
			do {
				val1=LoadMb(si_base+si_index);
				val2=LoadMb(di_base+di_index);
				si_index=(si_index+add_index) & add_mask;
				di_index=(di_index+add_index) & add_mask;
				count--;
			} while (--CPU_Cycles > 0 && (val1==val2)==core.rep_zero && count != 0);
			CMPB(val1,val2,LoadD,0);
		}
		break;
	case R_CMPSW:
		{
			add_index<<=1;Bit16u val1,val2;
			do {
				val1=LoadMw(si_base+si_index);
				val2=LoadMw(di_base+di_index);
				si_index=(si_index+add_index) & add_mask;
				di_index=(di_index+add_index) & add_mask;
				count--;
			} while (--CPU_Cycles > 0 && (val1==val2)==core.rep_zero && count != 0);
			CMPW(val1,val2,LoadD,0);
		}
		break;
	case R_CMPSD:
		{
			add_index<<=2;Bit32u val1,val2;
			do {
				val1=LoadMd(si_base+si_index);
				val2=LoadMd(di_base+di_index);
				si_index=(si_index+add_index) & add_mask;
				di_index=(di_index+add_index) & add_mask;
				count--;
			} while (--CPU_Cycles > 0 && (val1==val2)==core.rep_zero && count != 0);
			CMPD(val1,val2,LoadD,0);
		}
		break;
	default:
		LOG(LOG_CPU,LOG_ERROR)("Unhandled string op %d",type);
	}
	/* Clean up after certain amount of instructions */
	reg_esi&=(~add_mask);
	reg_esi|=(si_index & add_mask);
	reg_edi&=(~add_mask);
	reg_edi|=(di_index & add_mask);
	if (TEST_PREFIX_REP) {
		reg_ecx&=(~add_mask);
		reg_ecx|=(count & add_mask);

		/* if the count is still nonzero, then there is still work to do and the
		 * instruction has not finished executing---it needs to be restarted.
		 * if the string op was REP CMPSB or REP SCASB then it also matters
		 * whether the ZF flag matches the REP condition on whether or not we
		 * restart the instruction. */
		if (count != 0) {
			if (type < R_SCASB) {
				/* if count != 0 then restart the instruction */
				LOADIP;
			}
			else {
				/* if ZF matches the REP condition, restart the instruction */
				if ((get_ZF()?1:0) == (core.rep_zero?1:0)) {
					LOADIP;
				}
			}
		}
	} PAGE_FAULT_CLEANUP_CATCH({
		/* Clean up after certain amount of instructions */
		reg_esi&=(~add_mask);
		reg_esi|=(si_index & add_mask);
		reg_edi&=(~add_mask);
		reg_edi|=(di_index & add_mask);
		if (TEST_PREFIX_REP) {
			reg_ecx&=(~add_mask);
			reg_ecx|=(count & add_mask);
		}

		/* rethrow the exception.
		 * NOTE: this means the normal core has no chance to execute SAVEIP, therefore
		 *       when the guest OS has finished handling the page fault the instruction
		 *       pointer will come right back to the string op that caused the fault
		 *       and the string op will restart where it left off. */
	})}
}
