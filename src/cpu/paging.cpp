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


#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "dosbox.h"
#include "mem.h"
#include "paging.h"
#include "regs.h"
#include "lazyflags.h"
#include "cpu.h"
#include "debug.h"
#include "setup.h"

/*
  DBP: Added improved paging implementation with C++ exception page faults from Taewoong's Daum branch
       It is used when running with the normal core to increase compatibility.
       Because it does not support the dynamic core, an enhanced version of the original paging behavior is used for that.
*/

#define LINK_TOTAL		(64*1024)

#define USERWRITE_PROHIBITED			((cpu.cpl&cpu.mpl)==3)

#define CPU_HAS_WP_FLAG					(cpu.cr0&CR0_WRITEPROTECT)

PagingBlock paging;

//static Bit32u logcnt;

Bitu PageHandler::readb(PhysPt addr) {
	E_Exit("No byte handler for read from %d",addr);	
	return 0;
}
Bitu PageHandler::readw(PhysPt addr) {
	Bitu ret = (readb(addr+0) << 0);
	ret     |= (readb(addr+1) << 8);
	return ret;
}
Bitu PageHandler::readd(PhysPt addr) {
	Bitu ret = (readb(addr+0) << 0);
	ret     |= (readb(addr+1) << 8);
	ret     |= (readb(addr+2) << 16);
	ret     |= (readb(addr+3) << 24);
	return ret;
}

void PageHandler::writeb(PhysPt addr,Bitu /*val*/) {
	E_Exit("No byte handler for write to %d",addr);	
}

void PageHandler::writew(PhysPt addr,Bitu val) {
	writeb(addr+0,(Bit8u) (val >> 0));
	writeb(addr+1,(Bit8u) (val >> 8));
}
void PageHandler::writed(PhysPt addr,Bitu val) {
	writeb(addr+0,(Bit8u) (val >> 0));
	writeb(addr+1,(Bit8u) (val >> 8));
	writeb(addr+2,(Bit8u) (val >> 16));
	writeb(addr+3,(Bit8u) (val >> 24));
}

HostPt PageHandler::GetHostReadPt(Bitu /*phys_page*/) {
	return 0;
}

HostPt PageHandler::GetHostWritePt(Bitu /*phys_page*/) {
	return 0;
}

bool PageHandler::readb_checked(PhysPt addr, Bit8u * val) {
	*val=(Bit8u)readb(addr);	return false;
}
bool PageHandler::readw_checked(PhysPt addr, Bit16u * val) {
	*val=(Bit16u)readw(addr);	return false;
}
bool PageHandler::readd_checked(PhysPt addr, Bit32u * val) {
	*val=(Bit32u)readd(addr);	return false;
}
bool PageHandler::writeb_checked(PhysPt addr,Bitu val) {
	writeb(addr,val);	return false;
}
bool PageHandler::writew_checked(PhysPt addr,Bitu val) {
	writew(addr,val);	return false;
}
bool PageHandler::writed_checked(PhysPt addr,Bitu val) {
	writed(addr,val);	return false;
}



struct PF_Entry {
	Bitu cs;
	Bitu eip;
	Bitu page_addr;
	Bitu mpl;
};

#ifdef C_DBP_LIBRETRO
#define PF_QUEUESIZE 80
#else
#define PF_QUEUESIZE 16
#endif
static struct {
	Bitu used;
	PF_Entry entries[PF_QUEUESIZE];
} pf_queue;

#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
extern void DOSBOX_WipePageFaultQueue();
extern void DOSBOX_ResetCPUDecoder();
extern bool DOSBOX_IsWipingPageFaultQueue;
static Bit32u DBP_PageFaultCycles;
#endif

#define ACCESS_KR  0
#define ACCESS_KRW 1
#define ACCESS_UR  2
#define ACCESS_URW 3
#define ACCESS_TABLEFAULT 4
//const char* const mtr[] = {"KR ","KRW","UR ","URW","PFL"};

// bit0 entry write
// bit1 entry access
// bit2 table write
// bit3 table access
// These arrays define how the access bits in the page table and entry
// result in access rights.
// The used array is switched depending on the CPU under emulation.

// Intel says the lowest numeric value wins for both 386 and 486+
// There's something strange about KR with WP=1 though
static const Bit8u translate_array[] = {
	ACCESS_KR,		// 00 00
	ACCESS_KR,		// 00 01
	ACCESS_KR,		// 00 10
	ACCESS_KR,		// 00 11
	ACCESS_KR,		// 01 00
	ACCESS_KRW,		// 01 01
	ACCESS_KR, //	ACCESS_KRW,		// 01 10
	ACCESS_KRW,		// 01 11
	ACCESS_KR,		// 10 00
	ACCESS_KR, //	ACCESS_KRW,		// 10 01
	ACCESS_UR,		// 10 10
	ACCESS_UR,		// 10 11
	ACCESS_KR,		// 11 00
	ACCESS_KRW,		// 11 01
	ACCESS_UR,		// 11 10
	ACCESS_URW		// 11 11
};

// This array defines how a page is mapped depending on 
// page access right, cpl==3, and WP.
// R = map handler as read, W = map handler as write, E = map exception handler
#define ACMAP_RW 0
#define ACMAP_RE 1
#define ACMAP_EE 2

//static const char* const lnm[] = {"RW ","RE ","EE "}; // debug stuff

// bit0-1 ACCESS_ type
// bit2   1=user mode
// bit3   WP on

static const Bit8u xlat_mapping[] = {
//  KR        KRW       UR        URW
	// index 0-3   kernel, wp 0
	ACMAP_RW, ACMAP_RW, ACMAP_RW, ACMAP_RW,
	// index 4-7   user,   wp 0
	ACMAP_EE, ACMAP_EE, ACMAP_RE, ACMAP_RW,
	// index 8-11  kernel, wp 1
	ACMAP_RE, ACMAP_RW, ACMAP_RE, ACMAP_RW,
	// index 11-15 user,   wp 1 (same as user, wp 0)
	ACMAP_EE, ACMAP_EE, ACMAP_RE, ACMAP_RW,
};

// This table can figure out if we are going to fault right now in the init handler
// (1=fault) 
// bit0-1 ACCESS_ type
// bit2   1=user mode
// bit3   1=writing
// bit4   wp

static const Bit8u fault_table[] = {
//	KR	KRW	UR	URW
	// WP 0
	// index 0-3   kernel, reading
	0,	0,	0,	0,
	// index 4-7   user,   reading
	1,	1,	0,	0,
	// index 8-11  kernel, writing
	0,	0,	0,	0,
	// index 11-15 user,   writing
	1,	1,	1,	0,
	// WP 1
	// index 0-3   kernel, reading
	0,	0,	0,	0,
	// index 4-7   user,   reading
	1,	1,	0,	0,
	// index 8-11  kernel, writing
	1,	0,	1,	0,
	// index 11-15 user,   writing
	1,	1,	1,	0,
};

#define PHYSPAGE_DITRY 0x10000000
#define PHYSPAGE_ADDR  0x000FFFFF

// helper functions for calculating table entry addresses
static inline PhysPt GetPageDirectoryEntryAddr(PhysPt lin_addr) {
	return paging.base.addr | ((lin_addr >> 22) << 2);
}
static inline PhysPt GetPageTableEntryAddr(PhysPt lin_addr, X86PageEntry& dir_entry) {
	return (dir_entry.block.base<<12) | ((lin_addr >> 10) & 0xffc);
}

/*
void PrintPageInfo(const char* string, PhysPt lin_addr, bool writing, bool prepare_only) {

	Bitu lin_page=lin_addr >> 12;

	X86PageEntry dir_entry, table_entry;
	bool isUser = (((cpu.cpl & cpu.mpl)==3)? true:false);

	PhysPt dirEntryAddr = GetPageDirectoryEntryAddr(lin_addr);
	PhysPt tableEntryAddr = 0;
	dir_entry.load=phys_readd(dirEntryAddr);
	Bitu result = 4;
	bool dirty = false;
	Bitu ft_index = 0;

	if (dir_entry.block.p) {
		tableEntryAddr = GetPageTableEntryAddr(lin_addr, dir_entry);
		table_entry.load=phys_readd(tableEntryAddr);
		if (table_entry.block.p) {
			result =
				translate_array[((dir_entry.load<<1)&0xc) | ((table_entry.load>>1)&0x3)];

			ft_index = result | (writing? 8:0) | (isUser? 4:0) |
				(paging.wp? 16:0);

			dirty = table_entry.block.d? true:false;
		}
	}
	LOG_MSG("%s %s LIN% 8x PHYS% 5x wr%x ch%x wp%x d%x c%x m%x f%x a%x [%x/%x/%x]",
		string, mtr[result], lin_addr, table_entry.block.base,
		writing, prepare_only, paging.wp,
		dirty, cpu.cpl, cpu.mpl, fault_table[ft_index],
		((dir_entry.load<<1)&0xc) | ((table_entry.load>>1)&0x3),
		dirEntryAddr, tableEntryAddr, table_entry.load);
}
*/

static Bits PageFaultCore(void) {
	CPU_CycleLeft+=CPU_Cycles;
	CPU_Cycles=1;
	Bits ret=CPU_Core_Full_Run();
	CPU_CycleLeft+=CPU_Cycles;
	if (ret<0) E_Exit("Got a dosbox close machine in pagefault core?");
	if (ret) 
		return ret;
#ifndef C_DBP_PAGE_FAULT_QUEUE_WIPE
	if (!pf_queue.used) E_Exit("PF Core without PF");
	PF_Entry * entry=&pf_queue.entries[pf_queue.used-1];
#else // support loading save state into a pagefault
	if (!pf_queue.used) DOSBOX_ResetCPUDecoder();
	PF_Entry * entry=&pf_queue.entries[pf_queue.used?pf_queue.used-1:0];
#endif
	X86PageEntry pentry;
	pentry.load=phys_readd(entry->page_addr);
	if (pentry.block.p && entry->cs == SegValue(cs) && entry->eip==reg_eip) {
		cpu.mpl=entry->mpl;
		return -1;
	}
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
	if (DOSBOX_IsWipingPageFaultQueue) return -1;
	if ((pf_queue.used >= 2 && DBP_PageFaultCycles++ > 5000000) || pf_queue.used > 50)
	{
		LOG(LOG_PAGING,LOG_NORMAL)("Wiping page fault queue after %d queueups", pf_queue.used);
		DOSBOX_WipePageFaultQueue();
		DBP_PageFaultCycles = 0;
		return -1;
	}
#endif
	return 0;
}

#if C_DEBUG
Bitu DEBUG_EnableDebugger(void);
#endif

void PAGING_PageFault(PhysPt lin_addr,Bitu page_addr,Bitu faultcode) {
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
	if (pf_queue.used > 60)
	{
		LOG(LOG_PAGING,LOG_NORMAL)("Emergency wiping page fault queue after %d queueups", pf_queue.used);
		DOSBOX_WipePageFaultQueue();
		return;
	}
	if (DOSBOX_IsWipingPageFaultQueue) return;
	DBP_PageFaultCycles = 0;
#endif
	/* Save the state of the cpu cores */
	LazyFlags old_lflags;
	memcpy(&old_lflags,&lflags,sizeof(LazyFlags));
	CPU_Decoder * old_cpudecoder;
	old_cpudecoder=cpudecoder;
	cpudecoder=&PageFaultCore;
	paging.cr2=lin_addr;
	DBP_ASSERT(pf_queue.used < PF_QUEUESIZE);
	PF_Entry * entry=&pf_queue.entries[pf_queue.used++];
	//LOG(LOG_PAGING,LOG_NORMAL)("PageFault at %X type [%x] queue %d (paging_prevent_exception_jump: %d)",lin_addr,faultcode,pf_queue.used, (int)paging_prevent_exception_jump);
	//LOG(LOG_PAGING,LOG_NORMAL)("    EAX:%04X ECX:%04X EDX:%04X EBX:%04X",reg_eax,reg_ecx,reg_edx,reg_ebx);
	//LOG(LOG_PAGING,LOG_NORMAL)("    CS:%04X EIP:%08X SS:%04x SP:%08X",SegValue(cs),reg_eip,SegValue(ss),reg_esp);
//	LOG_MSG("EAX:%04X ECX:%04X EDX:%04X EBX:%04X",reg_eax,reg_ecx,reg_edx,reg_ebx);
//	LOG_MSG("CS:%04X EIP:%08X SS:%04x SP:%08X",SegValue(cs),reg_eip,SegValue(ss),reg_esp);
	entry->cs=SegValue(cs);
	entry->eip=reg_eip;
	entry->page_addr=page_addr;
	entry->mpl=cpu.mpl;
	cpu.mpl=3;

	CPU_Exception(EXCEPTION_PF,faultcode);
#if C_DEBUG
//	DEBUG_EnableDebugger();
#endif
	DOSBOX_RunMachine();
	pf_queue.used--;
	//LOG(LOG_PAGING,LOG_NORMAL)("Left PageFault for %x queue %d",lin_addr,pf_queue.used);
	memcpy(&lflags,&old_lflags,sizeof(LazyFlags));
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE //DBP: Added this check to support page fault queue wiping
	if (DOSBOX_IsWipingPageFaultQueue) return;
#endif
	cpudecoder=old_cpudecoder;
	//LOG_MSG("SS:%04x SP:%08X",SegValue(ss),reg_esp);
}

static INLINE void InitPageCheckPresence(PhysPt lin_addr,bool writing,X86PageEntry& table,X86PageEntry& entry) {
	Bitu lin_page=lin_addr >> 12;
	Bitu d_index=lin_page >> 10;
	Bitu t_index=lin_page & 0x3ff;
	Bitu table_addr=(paging.base.page<<12)+d_index*4;
	table.load=phys_readd(table_addr);
	if (!table.block.p) {
		LOG(LOG_PAGING,LOG_NORMAL)("NP Table");
		PAGING_PageFault(lin_addr,table_addr,
			(writing?0x02:0x00) | (((cpu.cpl&cpu.mpl)==0)?0x00:0x04));
		table.load=phys_readd(table_addr);
		if (GCC_UNLIKELY(!table.block.p))
		{
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
			if (DOSBOX_IsWipingPageFaultQueue) return;
#else // avoid calling E_Exit after DBP_DOSBOX_ForceShutdown has been called
			E_Exit("Pagefault didn't correct table");
#endif
		}
	}
	Bitu entry_addr=(table.block.base<<12)+t_index*4;
	entry.load=phys_readd(entry_addr);
	if (!entry.block.p) {
//		LOG(LOG_PAGING,LOG_NORMAL)("NP Page");
		PAGING_PageFault(lin_addr,entry_addr,
			(writing?0x02:0x00) | (((cpu.cpl&cpu.mpl)==0)?0x00:0x04));
		entry.load=phys_readd(entry_addr);
		if (GCC_UNLIKELY(!entry.block.p))
		{
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
			if (DOSBOX_IsWipingPageFaultQueue) return;
#else // avoid calling E_Exit after DBP_DOSBOX_ForceShutdown has been called
			E_Exit("Pagefault didn't correct page");
#endif
		}
	}
}

bool paging_prevent_exception_jump;	/* when set, do recursive page fault mode (when not executing instruction) */

// PAGING_NewPageFault
// lin_addr, page_addr: the linear and page address the fault happened at
// prepare_only: true in case the calling core handles the fault, else the PageFaultCore does
static void PAGING_NewPageFault(PhysPt lin_addr, Bitu page_addr, bool prepare_only, Bitu faultcode) {
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
	if (DOSBOX_IsWipingPageFaultQueue) return;
#endif
	paging.cr2=lin_addr;
	//LOG_MSG("FAULT q%d, code %x",  pf_queue.used, faultcode);
	//PrintPageInfo("FA+",lin_addr,faultcode, prepare_only);
	if (prepare_only) {
		cpu.exception.which = EXCEPTION_PF;
		cpu.exception.error = faultcode;
		//LOG_MSG("[%8u] [@%8d] PageFault at %X type [%x] PREPARE",logcnt++, CPU_Cycles,lin_addr,cpu.exception.error);
	} else if (!paging_prevent_exception_jump) {
		//LOG_MSG("[%8u] [@%8d] PageFault at %X type [%x] queue 1",logcnt++, CPU_Cycles,lin_addr,faultcode);
		THROW_PAGE_FAULT(faultcode);
	} else {
		PAGING_PageFault(lin_addr,page_addr,faultcode);
	}
}

static void PAGING_LinkPageNew(Bitu lin_page, Bitu phys_page, Bitu linkmode, bool dirty);

class PageFoilHandler : public PageHandler {
private:
	void work(PhysPt addr) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;

		// set the page dirty in the tlb
		paging.tlb.phys_page[lin_page] |= PHYSPAGE_DITRY;

		// mark the page table entry dirty
		X86PageEntry dir_entry, table_entry;

		PhysPt dirEntryAddr = GetPageDirectoryEntryAddr(addr);
		dir_entry.load=phys_readd(dirEntryAddr);
		//if (!dir_entry.block.p) E_Exit("Undesired situation 1 in page foiler.");

		PhysPt tableEntryAddr = GetPageTableEntryAddr(addr, dir_entry);
		table_entry.load=phys_readd(tableEntryAddr);
		//if (!table_entry.block.p) E_Exit("Undesired situation 2 in page foiler.");

		//// for debugging...
		//if (table_entry.block.base != phys_page)
		//	E_Exit("Undesired situation 3 in page foiler.");

		// map the real write handler in our place
		PageHandler* handler = MEM_GetPageHandler(phys_page);

		// debug
		//LOG_MSG("FOIL            LIN% 8x PHYS% 8x [%x/%x/%x] WRP % 8x", addr, phys_page,
		//	dirEntryAddr, tableEntryAddr, table_entry.load, wtest);

		// this can happen when the same page table is used at two different
		// page directory entries / linear locations (WfW311)
		// if (table_entry.block.d) E_Exit("% 8x Already dirty!!",table_entry.load);

		// set the dirty bit
		table_entry.block.d=1;
		phys_writed(tableEntryAddr,table_entry.load);

		// replace this handler with the real thing
		if (handler->flags & PFLAG_WRITEABLE)
			paging.tlb.write[lin_page] = handler->GetHostWritePt(phys_page) - (lin_page << 12);
		else paging.tlb.write[lin_page]=0;
		paging.tlb.writehandler[lin_page]=handler;
	}

	void read() {
		E_Exit("The page foiler shouldn't be read.");
	}
public:
	PageFoilHandler() { flags = PFLAG_INIT|PFLAG_NOCODE; }
	Bitu readb(PhysPt addr) {read();return 0;}
	Bitu readw(PhysPt addr) {read();return 0;}
	Bitu readd(PhysPt addr) {read();return 0;}

	void writeb(PhysPt addr,Bitu val) {
		work(addr);
		// execute the write:
		// no need to care about mpl because we won't be entered
		// if write isn't allowed
		mem_writeb(addr,val);
	}
	void writew(PhysPt addr,Bitu val) {
		work(addr);
		mem_writew(addr,val);
	}
	void writed(PhysPt addr,Bitu val) {
		work(addr);
		mem_writed(addr,val);
	}

	bool readb_checked(PhysPt addr, Bit8u * val) {read();return true;}
	bool readw_checked(PhysPt addr, Bit16u * val) {read();return true;}
	bool readd_checked(PhysPt addr, Bit32u * val) {read();return true;}

	bool writeb_checked(PhysPt addr,Bitu val) {
		work(addr);
		mem_writeb(addr,val);
		return false;
	}
	bool writew_checked(PhysPt addr,Bitu val) {
		work(addr);
		mem_writew(addr,val);
		return false;
	}
	bool writed_checked(PhysPt addr,Bitu val) {
		work(addr);
		mem_writed(addr,val);
		return false;
	}
};

class ExceptionPageHandler : public PageHandler {
private:
	PageHandler* getHandler(PhysPt addr) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		return handler;
	}

	bool hack_check(PhysPt addr) {
		// First Encounters
		// They change the page attributes without clearing the TLB.
		// On a real 486 they get away with it because its TLB has only 32 or so 
		// elements. The changed page attribs get overwritten and re-read before
		// the exception happens. Here we have gazillions of TLB entries so the
		// exception occurs if we don't check for it.

		Bitu old_attirbs = paging.tlb.phys_page[addr>>12] >> 30;
		X86PageEntry dir_entry, table_entry;
		
		dir_entry.load = phys_readd(GetPageDirectoryEntryAddr(addr));
		if (!dir_entry.block.p) return false;
		table_entry.load = phys_readd(GetPageTableEntryAddr(addr, dir_entry));
		if (!table_entry.block.p) return false;
		Bitu result =
		translate_array[((dir_entry.load<<1)&0xc) | ((table_entry.load>>1)&0x3)];
		if (result != old_attirbs) return true;
		return false;
	}

	void Exception(PhysPt addr, bool writing, bool checked) {
		//PrintPageInfo("XCEPT",addr,writing, checked);
		//LOG_MSG("XCEPT LIN% 8x wr%d, ch%d, cpl%d, mpl%d",addr, writing, checked, cpu.cpl, cpu.mpl);
		PhysPt tableaddr = 0;
		if (!checked) {
			X86PageEntry dir_entry;
			dir_entry.load = phys_readd(GetPageDirectoryEntryAddr(addr));
			if (!dir_entry.block.p) E_Exit("Undesired situation 1 in exception handler.");
			
			// page table entry
			tableaddr = GetPageTableEntryAddr(addr, dir_entry);
			//Bitu d_index=(addr >> 12) >> 10;
			//tableaddr=(paging.base.page<<12) | (d_index<<2);
		} 
		PAGING_NewPageFault(addr, tableaddr, checked,
			1 | (writing? 2:0) | (((cpu.cpl&cpu.mpl)==3)? 4:0));
		
		PAGING_ClearTLB(); // TODO got a better idea?
	}

	Bitu readb_through(PhysPt addr) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		if (handler->flags & PFLAG_READABLE) {
			return host_readb(handler->GetHostReadPt(phys_page) + (addr&0xfff));
		}
		else return handler->readb(addr);
	}
	Bitu readw_through(PhysPt addr) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		if (handler->flags & PFLAG_READABLE) {
			return host_readw(handler->GetHostReadPt(phys_page) + (addr&0xfff));
		}
		else return handler->readw(addr);
	}
	Bitu readd_through(PhysPt addr) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		if (handler->flags & PFLAG_READABLE) {
			return host_readd(handler->GetHostReadPt(phys_page) + (addr&0xfff));
		}
		else return handler->readd(addr);
	}
	void writeb_through(PhysPt addr, Bitu val) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		if (handler->flags & PFLAG_WRITEABLE) {
			return host_writeb(handler->GetHostWritePt(phys_page) + (addr&0xfff), (Bit8u)val);
		}
		else return handler->writeb(addr, val);
	}
	void writew_through(PhysPt addr, Bitu val) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		if (handler->flags & PFLAG_WRITEABLE) {
			return host_writew(handler->GetHostWritePt(phys_page) + (addr&0xfff), (Bit16u)val);
		}
		else return handler->writew(addr, val);
	}
	void writed_through(PhysPt addr, Bitu val) {
		Bitu lin_page = addr >> 12;
		Bit32u phys_page = paging.tlb.phys_page[lin_page] & PHYSPAGE_ADDR;
		PageHandler* handler = MEM_GetPageHandler(phys_page);
		if (handler->flags & PFLAG_WRITEABLE) {
			return host_writed(handler->GetHostWritePt(phys_page) + (addr&0xfff), val);
		}
		else return handler->writed(addr, val);
	}

public:
	ExceptionPageHandler() { flags = PFLAG_INIT|PFLAG_NOCODE; }
	Bitu readb(PhysPt addr) {
		if (!cpu.mpl) return readb_through(addr);

		Exception(addr, false, false);
		return mem_readb(addr); // read the updated page (unlikely to happen?)
	}
	Bitu readw(PhysPt addr) {
		// access type is supervisor mode (temporary)
		// we are always allowed to read in superuser mode
		// so get the handler and address and read it
		if (!cpu.mpl) return readw_through(addr);

		Exception(addr, false, false);
		return mem_readw(addr);
	}
	Bitu readd(PhysPt addr) {
		if (!cpu.mpl) return readd_through(addr);

		Exception(addr, false, false);
		return mem_readd(addr);
	}
	void writeb(PhysPt addr,Bitu val) {
		if (!cpu.mpl) {
			writeb_through(addr, val);
			return;
		}
		Exception(addr, true, false);
		mem_writeb(addr, val);
	}
	void writew(PhysPt addr,Bitu val) {
		if (!cpu.mpl) {
			// TODO Exception on a KR-page?
			writew_through(addr, val);
			return;
		}
		if (hack_check(addr)) {
			LOG_MSG("Page attributes modified without clear");
			PAGING_ClearTLB();
			mem_writew(addr,val);
			return;
		}
		// firstenc here
		Exception(addr, true, false);
		mem_writew(addr, val);
	}
	void writed(PhysPt addr,Bitu val) {
		if (!cpu.mpl) {
			writed_through(addr, val);
			return;
		}
		Exception(addr, true, false);
		mem_writed(addr, val);
	}
	// returning true means an exception was triggered for these _checked functions
	bool readb_checked(PhysPt addr, Bit8u * val) {
		Exception(addr, false, true);
		return true;
	}
	bool readw_checked(PhysPt addr, Bit16u * val) {
		Exception(addr, false, true);
		return true;
	}
	bool readd_checked(PhysPt addr, Bit32u * val) {
		Exception(addr, false, true);
		return true;
	}
	bool writeb_checked(PhysPt addr,Bitu val) {
		Exception(addr, true, true);
		return true;
	}
	bool writew_checked(PhysPt addr,Bitu val) {
		if (hack_check(addr)) {
			LOG_MSG("Page attributes modified without clear");
			PAGING_ClearTLB();
			mem_writew(addr,val); // TODO this makes us recursive again?
			return false;
		}
		Exception(addr, true, true);
		return true;
	}
	bool writed_checked(PhysPt addr,Bitu val) {
		Exception(addr, true, true);
		return true;
	}
};

class NewInitPageHandler : public PageHandler {
public:
	NewInitPageHandler() { flags = PFLAG_INIT|PFLAG_NOCODE; }
	Bitu readb(PhysPt addr) {
		if (InitPage(addr, false, false)) return 0;
		return mem_readb(addr);
	}
	Bitu readw(PhysPt addr) {
		if (InitPage(addr, false, false)) return 0;
		return mem_readw(addr);
	}
	Bitu readd(PhysPt addr) {
		if (InitPage(addr, false, false)) return 0;
		return mem_readd(addr);
	}
	void writeb(PhysPt addr,Bitu val) {
		if (InitPage(addr, true, false)) return;
		mem_writeb(addr,val);
	}
	void writew(PhysPt addr,Bitu val) {
		if (InitPage(addr, true, false)) return;
		mem_writew(addr,val);
	}
	void writed(PhysPt addr,Bitu val) {
		if (InitPage(addr, true, false)) return;
		mem_writed(addr,val);
	}
	bool readb_checked(PhysPt addr, Bit8u * val) {
		if (InitPage(addr, false, true)) return true;
		*val=mem_readb(addr);
		return false;
	}
	bool readw_checked(PhysPt addr, Bit16u * val) {
		if (InitPage(addr, false, true)) return true;
		*val=mem_readw(addr);
		return false;
	}
	bool readd_checked(PhysPt addr, Bit32u * val) {
		if (InitPage(addr, false, true)) return true;
		*val=mem_readd(addr);
		return false;
	}
	bool writeb_checked(PhysPt addr,Bitu val) {
		if (InitPage(addr, true, true)) return true;
		mem_writeb(addr,val);
		return false;
	}
	bool writew_checked(PhysPt addr,Bitu val) {
		if (InitPage(addr, true, true)) return true;
		mem_writew(addr,val);
		return false;
	}
	bool writed_checked(PhysPt addr,Bitu val) {
		if (InitPage(addr, true, true)) return true;
		mem_writed(addr,val);
		return false;
	}
	bool InitPage(PhysPt lin_addr, bool writing, bool prepare_only) {
		Bitu lin_page=lin_addr >> 12;
		Bitu phys_page;
		if (paging.enabled) {
			initpage_retry:
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
			if (DOSBOX_IsWipingPageFaultQueue) return true;
#endif
			X86PageEntry dir_entry, table_entry;
			bool isUser = (((cpu.cpl & cpu.mpl)==3)? true:false);

			// Read the paging stuff, throw not present exceptions if needed
			// and find out how the page should be mapped
			PhysPt dirEntryAddr = GetPageDirectoryEntryAddr(lin_addr);
			// Range check to avoid emulator segfault: phys_readd() reads from MemBase+addr and does NOT range check.
			// Needed to avoid segfault when running 1999 demo "Void Main" in a bootable Windows 95 image in pure DOS mode.
			if ((dirEntryAddr+4) <= (MEM_TotalPages()<<12u)) {
				dir_entry.load=phys_readd(dirEntryAddr);
			}
			else {
				LOG(LOG_CPU,LOG_WARN)("Page directory access beyond end of memory, page %08x >= %08x",(unsigned int)(dirEntryAddr>>12u),(unsigned int)MEM_TotalPages());
				dir_entry.load=0xFFFFFFFF;
			}

			if (!dir_entry.block.p) {
				// table pointer is not present, do a page fault
				PAGING_NewPageFault(lin_addr, dirEntryAddr, prepare_only,
					(writing? 2:0) | (isUser? 4:0));

				if (prepare_only) return true;
				else goto initpage_retry; // TODO maybe E_Exit after a few loops
			}
			PhysPt tableEntryAddr = GetPageTableEntryAddr(lin_addr, dir_entry);
			// Range check to avoid emulator segfault: phys_readd() reads from MemBase+addr and does NOT range check.
			if ((tableEntryAddr+4) <= (MEM_TotalPages()<<12u)) {
				table_entry.load=phys_readd(tableEntryAddr);
			}
			else {
				LOG(LOG_CPU,LOG_WARN)("Page table entry access beyond end of memory, page %08x >= %08x",(unsigned int)(tableEntryAddr>>12u),(unsigned int)MEM_TotalPages());
				table_entry.load=0xFFFFFFFF;
			}
			table_entry.load=phys_readd(tableEntryAddr);

			// set page table accessed (IA manual: A is set whenever the entry is 
			// used in a page translation)
			if (!dir_entry.block.a) {
				dir_entry.block.a = 1;		
				phys_writed(dirEntryAddr, dir_entry.load);
			}

			if (!table_entry.block.p) {
				// physpage pointer is not present, do a page fault
				PAGING_NewPageFault(lin_addr, tableEntryAddr, prepare_only,
					 (writing? 2:0) | (isUser? 4:0));

				if (prepare_only) return true;
				else goto initpage_retry;
			}
			//PrintPageInfo("INI",lin_addr,writing,prepare_only);

			Bitu result =
				translate_array[((dir_entry.load<<1)&0xc) | ((table_entry.load>>1)&0x3)];
			
			// If a page access right exception occurs we shouldn't change a or d
			// I'd prefer running into the prepared exception handler but we'd need
			// an additional handler that sets the 'a' bit - idea - foiler read?
			Bitu ft_index = result | (writing? 8:0) | (isUser? 4:0) | (CPU_HAS_WP_FLAG? 16:0);
			
			if (GCC_UNLIKELY(fault_table[ft_index])) {
				// exception error code format: 
				// bit0 - protection violation, bit1 - writing, bit2 - user mode
				PAGING_NewPageFault(lin_addr, tableEntryAddr, prepare_only,
					1 | (writing? 2:0) | (isUser? 4:0));

				if (prepare_only) return true;
				else goto initpage_retry; // unlikely to happen?
			}
			// save load to see if it changed later
			Bit32u table_load = table_entry.load;

			// if we are writing we can set it right here to save some CPU
			if (writing) table_entry.block.d = 1;

			// set page accessed
			table_entry.block.a = 1;
			
			// update if needed
			if (table_load != table_entry.load)
				phys_writed(tableEntryAddr, table_entry.load);

			// if the page isn't dirty and we are reading we need to map the foiler
			// (dirty = false)
			bool dirty = table_entry.block.d? true:false;
			/*
			LOG_MSG("INIT  %s LIN% 8x PHYS% 5x wr%x ch%x wp%x d%x c%x m%x a%x [%x/%x/%x]",
				mtr[result], lin_addr, table_entry.block.base,
				writing, prepare_only, paging.wp,
				dirty, cpu.cpl, cpu.mpl,
				((dir_entry.load<<1)&0xc) | ((table_entry.load>>1)&0x3),
				dirEntryAddr, tableEntryAddr, table_entry.load);
			*/
			// finally install the new page
			PAGING_LinkPageNew(lin_page, table_entry.block.base, result, dirty);

		} else { // paging off
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
			PAGING_LinkPage(lin_page,phys_page);
		}
		return false;
	}
	void InitPageForced(Bitu lin_addr) {
		Bitu lin_page=lin_addr >> 12;
		Bitu phys_page;
		if (paging.enabled) {
			X86PageEntry table;
			X86PageEntry entry;
			InitPageCheckPresence((PhysPt)lin_addr,false,table,entry);

			if (!table.block.a) {
				table.block.a=1;		//Set access
				phys_writed((PhysPt)((paging.base.page<<12)+(lin_page >> 10)*4),table.load);
			}
			if (!entry.block.a) {
				entry.block.a=1;					//Set access
				phys_writed((table.block.base<<12)+(lin_page & 0x3ff)*4,entry.load);
			}
			phys_page=entry.block.base;
			// maybe use read-only page here if possible
		} else {
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
		}
		PAGING_LinkPage(lin_page,phys_page);
	}
};

static INLINE bool InitPageCheckPresence_CheckOnly(PhysPt lin_addr,bool writing,X86PageEntry& table,X86PageEntry& entry) {
	Bitu lin_page=lin_addr >> 12;
	Bitu d_index=lin_page >> 10;
	Bitu t_index=lin_page & 0x3ff;
	Bitu table_addr=(paging.base.page<<12)+d_index*4;
	table.load=phys_readd(table_addr);
	if (!table.block.p) {
		paging.cr2=lin_addr;
		cpu.exception.which=EXCEPTION_PF;
		cpu.exception.error=(writing?0x02:0x00) | (((cpu.cpl&cpu.mpl)==0)?0x00:0x04);
		//LOG(LOG_PAGING,LOG_NORMAL)("PageFault at %X type [%x] PREPARE",lin_addr,cpu.exception.error);
		//LOG(LOG_PAGING,LOG_NORMAL)("    EAX:%04X ECX:%04X EDX:%04X EBX:%04X",reg_eax,reg_ecx,reg_edx,reg_ebx);
		//LOG(LOG_PAGING,LOG_NORMAL)("    CS:%04X EIP:%08X SS:%04x SP:%08X",SegValue(cs),reg_eip,SegValue(ss),reg_esp);
		return false;
	}
	Bitu entry_addr=(table.block.base<<12)+t_index*4;
	entry.load=phys_readd(entry_addr);
	if (!entry.block.p) {
		paging.cr2=lin_addr;
		cpu.exception.which=EXCEPTION_PF;
		cpu.exception.error=(writing?0x02:0x00) | (((cpu.cpl&cpu.mpl)==0)?0x00:0x04);
		//LOG(LOG_PAGING,LOG_NORMAL)("PageFault at %X type [%x] PREPARE",lin_addr,cpu.exception.error);
		//LOG(LOG_PAGING,LOG_NORMAL)("    EAX:%04X ECX:%04X EDX:%04X EBX:%04X",reg_eax,reg_ecx,reg_edx,reg_ebx);
		//LOG(LOG_PAGING,LOG_NORMAL)("    CS:%04X EIP:%08X SS:%04x SP:%08X",SegValue(cs),reg_eip,SegValue(ss),reg_esp);
		return false;
	}
	return true;
}

// check if a user-level memory access would trigger a privilege page fault
static INLINE bool InitPage_CheckUseraccess(Bitu u1,Bitu u2) {
	switch (CPU_ArchitectureType) {
	case CPU_ARCHTYPE_MIXED:
	case CPU_ARCHTYPE_386SLOW:
	case CPU_ARCHTYPE_386FAST:
	default:
		return ((u1)==0) && ((u2)==0);
	case CPU_ARCHTYPE_486OLDSLOW:
	case CPU_ARCHTYPE_486NEWSLOW:
	case CPU_ARCHTYPE_PENTIUMSLOW:
		return ((u1)==0) || ((u2)==0);
	}
}


class InitPageHandler : public PageHandler {
public:
	InitPageHandler() {
		flags=PFLAG_INIT|PFLAG_NOCODE;
	}
	Bitu readb(PhysPt addr) {
		Bitu needs_reset=InitPage(addr,false);
		Bit8u val=mem_readb(addr);
		InitPageUpdateLink(needs_reset,addr);
		return val;
	}
	Bitu readw(PhysPt addr) {
		Bitu needs_reset=InitPage(addr,false);
		Bit16u val=mem_readw(addr);
		InitPageUpdateLink(needs_reset,addr);
		return val;
	}
	Bitu readd(PhysPt addr) {
		Bitu needs_reset=InitPage(addr,false);
		Bit32u val=mem_readd(addr);
		InitPageUpdateLink(needs_reset,addr);
		return val;
	}
	void writeb(PhysPt addr,Bitu val) {
		Bitu needs_reset=InitPage(addr,true);
		mem_writeb(addr,val);
		InitPageUpdateLink(needs_reset,addr);
	}
	void writew(PhysPt addr,Bitu val) {
		Bitu needs_reset=InitPage(addr,true);
		mem_writew(addr,val);
		InitPageUpdateLink(needs_reset,addr);
	}
	void writed(PhysPt addr,Bitu val) {
		Bitu needs_reset=InitPage(addr,true);
		mem_writed(addr,val);
		InitPageUpdateLink(needs_reset,addr);
	}
	bool readb_checked(PhysPt addr, Bit8u * val) {
		if (InitPageCheckOnly(addr,false)) {
			*val=mem_readb(addr);
			return false;
		} else return true;
	}
	bool readw_checked(PhysPt addr, Bit16u * val) {
		if (InitPageCheckOnly(addr,false)){
			*val=mem_readw(addr);
			return false;
		} else return true;
	}
	bool readd_checked(PhysPt addr, Bit32u * val) {
		if (InitPageCheckOnly(addr,false)) {
			*val=mem_readd(addr);
			return false;
		} else return true;
	}
	bool writeb_checked(PhysPt addr,Bitu val) {
		if (InitPageCheckOnly(addr,true)) {
			mem_writeb(addr,val);
			return false;
		} else return true;
	}
	bool writew_checked(PhysPt addr,Bitu val) {
		if (InitPageCheckOnly(addr,true)) {
			mem_writew(addr,val);
			return false;
		} else return true;
	}
	bool writed_checked(PhysPt addr,Bitu val) {
		if (InitPageCheckOnly(addr,true)) {
			mem_writed(addr,val);
			return false;
		} else return true;
	}
	Bitu InitPage(Bitu lin_addr,bool writing) {
		Bitu lin_page=lin_addr >> 12;
		Bitu phys_page;
		if (paging.enabled) {
			X86PageEntry table;
			X86PageEntry entry;
			InitPageCheckPresence(lin_addr,writing,table,entry);

			// 0: no action
			// 1: can (but currently does not) fail a user-level access privilege check
			// 2: can (but currently does not) fail a write privilege check
			// 3: fails a privilege check
			Bitu priv_check=0;
			if (InitPage_CheckUseraccess(entry.block.us,table.block.us)) {
				if ((cpu.cpl&cpu.mpl)==3) priv_check=3;
				else {
					switch (CPU_ArchitectureType) {
					case CPU_ARCHTYPE_MIXED:
					case CPU_ARCHTYPE_386FAST:
					default:
//						priv_check=0;	// default
						break;
					case CPU_ARCHTYPE_386SLOW:
					case CPU_ARCHTYPE_486OLDSLOW:
					case CPU_ARCHTYPE_486NEWSLOW:
					case CPU_ARCHTYPE_PENTIUMSLOW:
						priv_check=1;
						break;
					}
				}
			}
			if ((entry.block.wr==0) || (table.block.wr==0)) {
				// page is write-protected for user mode
				if (priv_check==0) {
					switch (CPU_ArchitectureType) {
					case CPU_ARCHTYPE_MIXED:
					case CPU_ARCHTYPE_386FAST:
					default:
//						priv_check=0;	// default
						break;
					case CPU_ARCHTYPE_386SLOW:
					case CPU_ARCHTYPE_486OLDSLOW:
					case CPU_ARCHTYPE_486NEWSLOW:
					case CPU_ARCHTYPE_PENTIUMSLOW:
						priv_check=2;
						break;
					}
				}
				// check if actually failing the write-protected check
				if (writing && USERWRITE_PROHIBITED) priv_check=3;
			}
			if (priv_check==3) {
				LOG(LOG_PAGING,LOG_NORMAL)("Page access denied: cpl=%i, %x:%x:%x:%x",
					cpu.cpl,entry.block.us,table.block.us,entry.block.wr,table.block.wr);
				PAGING_PageFault(lin_addr,(table.block.base<<12)+(lin_page & 0x3ff)*4,0x05 | (writing?0x02:0x00));
				priv_check=0;
			}

			if (!table.block.a) {
				table.block.a=1;		// set page table accessed
				phys_writed((paging.base.page<<12)+(lin_page >> 10)*4,table.load);
			}
			if ((!entry.block.a) || (!entry.block.d)) {
				entry.block.a=1;		// set page accessed

				// page is dirty if we're writing to it, or if we're reading but the
				// page will be fully linked so we can't track later writes
				if (writing || (priv_check==0)) entry.block.d=1;		// mark page as dirty

				phys_writed((table.block.base<<12)+(lin_page & 0x3ff)*4,entry.load);
			}

			phys_page=entry.block.base;
			
			// now see how the page should be linked best, if we need to catch privilege
			// checks later on it should be linked as read-only page
			if (priv_check==0) {
				// if reading we could link the page as read-only to later cacth writes,
				// will slow down pretty much but allows catching all dirty events
				PAGING_LinkPage(lin_page,phys_page);
			} else {
				if (priv_check==1) {
					PAGING_LinkPage(lin_page,phys_page);
					return 1;
				} else if (writing) {
					PageHandler * handler=MEM_GetPageHandler(phys_page);
					PAGING_LinkPage(lin_page,phys_page);
					if (!(handler->flags & PFLAG_READABLE)) return 1;
					if (!(handler->flags & PFLAG_WRITEABLE)) return 1;
					if (get_tlb_read(lin_addr)!=get_tlb_write(lin_addr)) return 1;
					if (phys_page>1) return phys_page;
					else return 1;
				} else {
					PAGING_LinkPage_ReadOnly(lin_page,phys_page);
				}
			}
		} else {
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
			PAGING_LinkPage(lin_page,phys_page);
		}
		return 0;
	}
	bool InitPageCheckOnly(Bitu lin_addr,bool writing) {
		Bitu lin_page=lin_addr >> 12;
		if (paging.enabled) {
			X86PageEntry table;
			X86PageEntry entry;
			if (!InitPageCheckPresence_CheckOnly(lin_addr,writing,table,entry)) return false;

			if (!USERWRITE_PROHIBITED) return true;

			if (InitPage_CheckUseraccess(entry.block.us,table.block.us) ||
					(((entry.block.wr==0) || (table.block.wr==0)) && writing)) {
				LOG(LOG_PAGING,LOG_NORMAL)("Page access denied: cpl=%i, %x:%x:%x:%x",
					cpu.cpl,entry.block.us,table.block.us,entry.block.wr,table.block.wr);
				paging.cr2=lin_addr;
				cpu.exception.which=EXCEPTION_PF;
				cpu.exception.error=0x05 | (writing?0x02:0x00);
				LOG(LOG_PAGING,LOG_NORMAL)("PageFault at %X type [%x] PREPARE",lin_addr,cpu.exception.error);
				//LOG(LOG_PAGING,LOG_NORMAL)("    EAX:%04X ECX:%04X EDX:%04X EBX:%04X",reg_eax,reg_ecx,reg_edx,reg_ebx);
				//LOG(LOG_PAGING,LOG_NORMAL)("    CS:%04X EIP:%08X SS:%04x SP:%08X",SegValue(cs),reg_eip,SegValue(ss),reg_esp);
				return false;
			}
		} else {
			Bitu phys_page;
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
			PAGING_LinkPage(lin_page,phys_page);
		}
		return true;
	}
	void InitPageForced(Bitu lin_addr) {
		Bitu lin_page=lin_addr >> 12;
		Bitu phys_page;
		if (paging.enabled) {
			X86PageEntry table;
			X86PageEntry entry;
			InitPageCheckPresence(lin_addr,false,table,entry);

			if (!table.block.a) {
				table.block.a=1;		//Set access
				phys_writed((paging.base.page<<12)+(lin_page >> 10)*4,table.load);
			}
			if (!entry.block.a) {
				entry.block.a=1;					//Set access
				phys_writed((table.block.base<<12)+(lin_page & 0x3ff)*4,entry.load);
			}
			phys_page=entry.block.base;
			// maybe use read-only page here if possible
		} else {
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
		}
		PAGING_LinkPage(lin_page,phys_page);
	}
	static INLINE void InitPageUpdateLink(Bitu relink,PhysPt addr) {
		if (relink==0) return;
		if (paging.links.used) {
			if (paging.links.entries[paging.links.used-1]==(addr>>12)) {
				paging.links.used--;
				PAGING_UnlinkPages(addr>>12,1);
			}
		}
		if (relink>1) PAGING_LinkPage_ReadOnly(addr>>12,relink);
	}
};

class InitPageUserROHandler : public PageHandler {
public:
	InitPageUserROHandler() {
		flags=PFLAG_INIT|PFLAG_NOCODE;
	}
	void writeb(PhysPt addr,Bitu val) {
		InitPage(addr,(Bit8u)(val&0xff));
		host_writeb(get_tlb_read(addr)+addr,(Bit8u)(val&0xff));
	}
	void writew(PhysPt addr,Bitu val) {
		InitPage(addr,(Bit16u)(val&0xffff));
		host_writew(get_tlb_read(addr)+addr,(Bit16u)(val&0xffff));
	}
	void writed(PhysPt addr,Bitu val) {
		InitPage(addr,(Bit32u)val);
		host_writed(get_tlb_read(addr)+addr,(Bit32u)val);
	}
	bool writeb_checked(PhysPt addr,Bitu val) {
		Bitu writecode=InitPageCheckOnly(addr,(Bit8u)(val&0xff));
		if (writecode) {
			HostPt tlb_addr;
			if (writecode>1) tlb_addr=get_tlb_read(addr);
			else tlb_addr=get_tlb_write(addr);
			host_writeb(tlb_addr+addr,(Bit8u)(val&0xff));
			return false;
		}
		return true;
	}
	bool writew_checked(PhysPt addr,Bitu val) {
		Bitu writecode=InitPageCheckOnly(addr,(Bit16u)(val&0xffff));
		if (writecode) {
			HostPt tlb_addr;
			if (writecode>1) tlb_addr=get_tlb_read(addr);
			else tlb_addr=get_tlb_write(addr);
			host_writew(tlb_addr+addr,(Bit16u)(val&0xffff));
			return false;
		}
		return true;
	}
	bool writed_checked(PhysPt addr,Bitu val) {
		Bitu writecode=InitPageCheckOnly(addr,(Bit32u)val);
		if (writecode) {
			HostPt tlb_addr;
			if (writecode>1) tlb_addr=get_tlb_read(addr);
			else tlb_addr=get_tlb_write(addr);
			host_writed(tlb_addr+addr,(Bit32u)val);
			return false;
		}
		return true;
	}
	void InitPage(Bitu lin_addr,Bitu val) {
		Bitu lin_page=lin_addr >> 12;
		Bitu phys_page;
		if (paging.enabled) {
			if (!USERWRITE_PROHIBITED) return;

			X86PageEntry table;
			X86PageEntry entry;
			InitPageCheckPresence(lin_addr,true,table,entry);

			LOG(LOG_PAGING,LOG_NORMAL)("Page access denied: cpl=%i, %x:%x:%x:%x",
				cpu.cpl,entry.block.us,table.block.us,entry.block.wr,table.block.wr);
			PAGING_PageFault(lin_addr,(table.block.base<<12)+(lin_page & 0x3ff)*4,0x07);

			if (!table.block.a) {
				table.block.a=1;		//Set access
				phys_writed((paging.base.page<<12)+(lin_page >> 10)*4,table.load);
			}
			if ((!entry.block.a) || (!entry.block.d)) {
				entry.block.a=1;	//Set access
				entry.block.d=1;	//Set dirty
				phys_writed((table.block.base<<12)+(lin_page & 0x3ff)*4,entry.load);
			}
			phys_page=entry.block.base;
			PAGING_LinkPage(lin_page,phys_page);
		} else {
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
			PAGING_LinkPage(lin_page,phys_page);
		}
	}
	Bitu InitPageCheckOnly(Bitu lin_addr,Bitu val) {
		Bitu lin_page=lin_addr >> 12;
		if (paging.enabled) {
			if (!USERWRITE_PROHIBITED) return 2;

			X86PageEntry table;
			X86PageEntry entry;
			if (!InitPageCheckPresence_CheckOnly(lin_addr,true,table,entry)) return 0;

			if (InitPage_CheckUseraccess(entry.block.us,table.block.us) || (((entry.block.wr==0) || (table.block.wr==0)))) {
				LOG(LOG_PAGING,LOG_NORMAL)("Page access denied: cpl=%i, %x:%x:%x:%x",
					cpu.cpl,entry.block.us,table.block.us,entry.block.wr,table.block.wr);
				paging.cr2=lin_addr;
				cpu.exception.which=EXCEPTION_PF;
				cpu.exception.error=0x07;
				//LOG(LOG_PAGING,LOG_NORMAL)("PageFault at %X type [%x] PREPARE RO",lin_addr,cpu.exception.error);
				//LOG(LOG_PAGING,LOG_NORMAL)("    EAX:%04X ECX:%04X EDX:%04X EBX:%04X",reg_eax,reg_ecx,reg_edx,reg_ebx);
				//LOG(LOG_PAGING,LOG_NORMAL)("    CS:%04X EIP:%08X SS:%04x SP:%08X",SegValue(cs),reg_eip,SegValue(ss),reg_esp);
				return 0;
			}
			PAGING_LinkPage(lin_page,entry.block.base);
		} else {
			Bitu phys_page;
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
			PAGING_LinkPage(lin_page,phys_page);
		}
		return 1;
	}
	void InitPageForced(Bitu lin_addr) {
		Bitu lin_page=lin_addr >> 12;
		Bitu phys_page;
		if (paging.enabled) {
			X86PageEntry table;
			X86PageEntry entry;
			InitPageCheckPresence(lin_addr,true,table,entry);

			if (!table.block.a) {
				table.block.a=1;		//Set access
				phys_writed((paging.base.page<<12)+(lin_page >> 10)*4,table.load);
			}
			if (!entry.block.a) {
				entry.block.a=1;	//Set access
				phys_writed((table.block.base<<12)+(lin_page & 0x3ff)*4,entry.load);
			}
			phys_page=entry.block.base;
		} else {
			if (lin_page<LINK_START) phys_page=paging.firstmb[lin_page];
			else phys_page=lin_page;
		}
		PAGING_LinkPage(lin_page,phys_page);
	}
};


bool PAGING_MakePhysPage(Bitu & page) {
	// page is the linear address on entry
	if (paging.enabled) {
		// check the page directory entry for this address
		X86PageEntry dir_entry;
		dir_entry.load = phys_readd(GetPageDirectoryEntryAddr(page<<12));
		if (!dir_entry.block.p) return false;

		// check the page table entry
		X86PageEntry tbl_entry;
		tbl_entry.load = phys_readd(GetPageTableEntryAddr(page<<12, dir_entry));
		if (!tbl_entry.block.p) return false;

		// return it
		page = tbl_entry.block.base;
	} else {
		if (page<LINK_START) page=paging.firstmb[page];
		//Else keep it the same
	}
	return true;
}

static InitPageHandler dyncore_init_page_handler;
static InitPageUserROHandler dyncore_init_page_handler_userro;
static NewInitPageHandler normalcore_init_page_handler;
static ExceptionPageHandler normalcore_exception_handler;
static PageFoilHandler normalcore_foiling_handler;
static PageHandler* init_page_handler;

Bitu PAGING_GetDirBase(void) {
	return paging.cr3;
}

bool PAGING_ForcePageInit(Bitu lin_addr) {
	PageHandler * handler=get_tlb_readhandler(lin_addr);
	if (handler==&dyncore_init_page_handler) {
		dyncore_init_page_handler.InitPageForced(lin_addr);
		return true;
	} else if (handler==&dyncore_init_page_handler_userro) {
		PAGING_UnlinkPages(lin_addr>>12,1);
		dyncore_init_page_handler_userro.InitPageForced(lin_addr);
		return true;
	}
	return false;
}

#if defined(USE_FULL_TLB)
void PAGING_InitTLB(void) {
	//DBP: Performance improvement
	memset(paging.tlb.read, 0, sizeof(paging.tlb.read));
	memset(paging.tlb.write, 0, sizeof(paging.tlb.write));
	for (Bitu i=0;i<TLB_SIZE;i++) paging.tlb.readhandler[i]=init_page_handler;
	for (Bitu i=0;i<TLB_SIZE;i++) paging.tlb.writehandler[i]=init_page_handler;
	paging.ur_links.used=0;
	paging.krw_links.used=0;
	paging.kr_links.used=0;
	paging.links.used=0;
}

void PAGING_ClearTLB(void) {
	//LOG_MSG("[%8u] [@%8d] [CLEARTLB] m% 4u, kr%d, krw%d, ur%d", logcnt++, CPU_Cycles,
	//	paging.links.used, paging.kr_links.used, paging.krw_links.used, paging.ur_links.used);
	Bit32u * entries=&paging.links.entries[0];
	for (;paging.links.used>0;paging.links.used--) {
		Bitu page=*entries++;
		paging.tlb.read[page]=0;
		paging.tlb.write[page]=0;
		paging.tlb.readhandler[page]=init_page_handler;
		paging.tlb.writehandler[page]=init_page_handler;
	}
	paging.ur_links.used=0;
	paging.krw_links.used=0;
	paging.kr_links.used=0;
	paging.links.used=0;
}

void PAGING_UnlinkPages(Bitu lin_page,Bitu pages) {
	for (;pages>0;pages--) {
		paging.tlb.read[lin_page]=0;
		paging.tlb.write[lin_page]=0;
		paging.tlb.readhandler[lin_page]=init_page_handler;
		paging.tlb.writehandler[lin_page]=init_page_handler;
		lin_page++;
	}
}

void PAGING_MapPage(Bitu lin_page,Bitu phys_page) {
	//LOG_MSG("[%8u] [@%8d] [MAPPAGE] Page: %x - Phys: %x", logcnt++, CPU_Cycles, lin_page, phys_page);
	if (lin_page<LINK_START) {
		paging.firstmb[lin_page]=phys_page;
		paging.tlb.read[lin_page]=0;
		paging.tlb.write[lin_page]=0;
		paging.tlb.readhandler[lin_page]=init_page_handler;
		paging.tlb.writehandler[lin_page]=init_page_handler;
	} else {
		PAGING_LinkPage(lin_page,phys_page);
	}
}

static void PAGING_LinkPageNew(Bitu lin_page, Bitu phys_page, Bitu linkmode, bool dirty) {
	Bitu xlat_index = linkmode | (CPU_HAS_WP_FLAG? 8:0) | ((cpu.cpl==3)? 4:0);
	Bit8u outcome = xlat_mapping[xlat_index];

	// get the physpage handler we are going to map 
	PageHandler * handler=MEM_GetPageHandler(phys_page);
	Bitu lin_base=lin_page << 12;

	//static const char* const lnm[] = {"RW ","RE ","EE "}; // debug stuff
	//LOG_MSG("[%8u] [@%8d] [LINKPAGE] Page: %x - Phys: %x - Dirty: %d - Outcome: %s", logcnt++, CPU_Cycles, lin_page, phys_page, dirty, lnm[outcome]);
	//LOG_MSG("MAPPG %s",lnm[outcome]);

	if (GCC_UNLIKELY(lin_page>=TLB_SIZE || phys_page>=TLB_SIZE)) 
		E_Exit("Illegal page");
	if (GCC_UNLIKELY(paging.links.used>=PAGING_LINKS)) {
		LOG(LOG_PAGING,LOG_NORMAL)("Not enough paging links, resetting cache");
		PAGING_ClearTLB();
	}
	// re-use some of the unused bits in the phys_page variable
	// needed in the exception handler and foiler so they can replace themselves appropriately
	// bit31-30 ACMAP_
	// bit29	dirty
	// these bits are shifted off at the places paging.tlb.phys_page is read
	paging.tlb.phys_page[lin_page]= phys_page | (linkmode<< 30) | (dirty? PHYSPAGE_DITRY:0);
	switch(outcome) {
	case ACMAP_RW:
		// read
		if (handler->flags & PFLAG_READABLE) paging.tlb.read[lin_page] = 
			handler->GetHostReadPt(phys_page)-lin_base;
		else paging.tlb.read[lin_page]=0;
		paging.tlb.readhandler[lin_page]=handler;

		// write
		if (dirty) { // in case it is already dirty we don't need to check
			if (handler->flags & PFLAG_WRITEABLE) paging.tlb.write[lin_page] = 
				handler->GetHostWritePt(phys_page)-lin_base;
			else paging.tlb.write[lin_page]=0;
			paging.tlb.writehandler[lin_page]=handler;
		} else {
			paging.tlb.writehandler[lin_page]= &normalcore_foiling_handler;
			paging.tlb.write[lin_page]=0;
		}
		break;
	case ACMAP_RE:
		// read
		if (handler->flags & PFLAG_READABLE) paging.tlb.read[lin_page] = 
			handler->GetHostReadPt(phys_page)-lin_base;
		else paging.tlb.read[lin_page]=0;
		paging.tlb.readhandler[lin_page]=handler;
		// exception
		paging.tlb.writehandler[lin_page]= &normalcore_exception_handler;
		paging.tlb.write[lin_page]=0;
		break;
	case ACMAP_EE:
		paging.tlb.readhandler[lin_page]= &normalcore_exception_handler;
		paging.tlb.writehandler[lin_page]= &normalcore_exception_handler;
		paging.tlb.read[lin_page]=0;
		paging.tlb.write[lin_page]=0;
		break;
	}

	switch(linkmode) {
	case ACCESS_KR:
		paging.kr_links.entries[paging.kr_links.used++]=lin_page;
		break;
	case ACCESS_KRW:
		paging.krw_links.entries[paging.krw_links.used++]=lin_page;
		break;
	case ACCESS_UR:
		paging.ur_links.entries[paging.ur_links.used++]=lin_page;
		break;
	case ACCESS_URW:	// with this access right everything is possible
						// thus no need to modify it on a us <-> sv switch
		break;
	}
	paging.links.entries[paging.links.used++]=lin_page; // "master table"
}

void PAGING_LinkPage(Bitu lin_page,Bitu phys_page) {
	//LOG_MSG("[%8u] [@%8d] [LINKPAGE] Page: %x - Phys: %x", logcnt++, CPU_Cycles, lin_page, phys_page);
	PageHandler * handler=MEM_GetPageHandler(phys_page);
	Bitu lin_base=lin_page << 12;
	if (lin_page>=TLB_SIZE || phys_page>=TLB_SIZE) 
		E_Exit("Illegal page");

	if (paging.links.used>=PAGING_LINKS) {
		LOG(LOG_PAGING,LOG_NORMAL)("Not enough paging links, resetting cache");
		PAGING_ClearTLB();
	}

	paging.tlb.phys_page[lin_page]=phys_page;
	if (handler->flags & PFLAG_READABLE) paging.tlb.read[lin_page]=handler->GetHostReadPt(phys_page)-lin_base;
	else paging.tlb.read[lin_page]=0;
	if (handler->flags & PFLAG_WRITEABLE) paging.tlb.write[lin_page]=handler->GetHostWritePt(phys_page)-lin_base;
	else paging.tlb.write[lin_page]=0;

	paging.links.entries[paging.links.used++]=lin_page;
	paging.tlb.readhandler[lin_page]=handler;
	paging.tlb.writehandler[lin_page]=handler;
}

void PAGING_LinkPage_ReadOnly(Bitu lin_page,Bitu phys_page) {
	//LOG_MSG("[%8u] [@%8d] [LINKPAGERO] Page: %x - Phys: %x", logcnt++, CPU_Cycles, lin_page, phys_page);
	PageHandler * handler=MEM_GetPageHandler(phys_page);
	Bitu lin_base=lin_page << 12;
	if (lin_page>=TLB_SIZE || phys_page>=TLB_SIZE) 
		E_Exit("Illegal page");

	if (paging.links.used>=PAGING_LINKS) {
		LOG(LOG_PAGING,LOG_NORMAL)("Not enough paging links, resetting cache");
		PAGING_ClearTLB();
	}

	paging.tlb.phys_page[lin_page]=phys_page;
	if (handler->flags & PFLAG_READABLE) paging.tlb.read[lin_page]=handler->GetHostReadPt(phys_page)-lin_base;
	else paging.tlb.read[lin_page]=0;
	paging.tlb.write[lin_page]=0;

	paging.links.entries[paging.links.used++]=lin_page;
	paging.tlb.readhandler[lin_page]=handler;
	paging.tlb.writehandler[lin_page]=&dyncore_init_page_handler_userro;
}

#else
#error Partial TLB not supported
#endif

void PAGING_SetDirBase(Bitu cr3) {
	paging.cr3=cr3;
	paging.base.page=cr3 >> 12;
	paging.base.addr=cr3 & ~0xFFF;
	//LOG(LOG_PAGING,LOG_NORMAL)("CR3:%X Base %X",cr3,paging.base.page);
	if (paging.enabled) {
		PAGING_ClearTLB();
	}
}

void PAGING_ChangedWP(void) {
	if (paging.enabled)
		PAGING_ClearTLB();
}

// parameter is the new cpl mode
void PAGING_SwitchCPL(bool isUser) {
	if (!paging.krw_links.used && !paging.kr_links.used && !paging.ur_links.used) return;
	//LOG_MSG("SWCPL u%d kr%d, krw%d, ur%d",
	//	isUser, paging.kr_links.used, paging.krw_links.used, paging.ur_links.used);
	
	// this function is worth optimizing
	// some of this cold be pre-stored?

	// krw - same for WP1 and WP0
	if (isUser) {
		// sv -> us: rw -> ee 
		for(Bitu i = 0; i < paging.krw_links.used; i++) {
			Bitu tlb_index = paging.krw_links.entries[i];
			paging.tlb.readhandler[tlb_index] = &normalcore_exception_handler;
			paging.tlb.writehandler[tlb_index] = &normalcore_exception_handler;
			paging.tlb.read[tlb_index] = 0;
			paging.tlb.write[tlb_index] = 0;
		}
	} else {
		// us -> sv: ee -> rw
		for(Bitu i = 0; i < paging.krw_links.used; i++) {
			Bitu tlb_index = paging.krw_links.entries[i];
			Bitu phys_page = paging.tlb.phys_page[tlb_index];
			Bitu lin_base = tlb_index << 12;
			bool dirty = (phys_page & PHYSPAGE_DITRY)? true:false;
			phys_page &= PHYSPAGE_ADDR;
			PageHandler* handler = MEM_GetPageHandler(phys_page);
			
			// map read handler
			paging.tlb.readhandler[tlb_index] = handler;
			if (handler->flags&PFLAG_READABLE)
				paging.tlb.read[tlb_index] = handler->GetHostReadPt(phys_page)-lin_base;
			else paging.tlb.read[tlb_index] = 0;
			
			// map write handler
			if (dirty) {
				paging.tlb.writehandler[tlb_index] = handler;
				if (handler->flags&PFLAG_WRITEABLE)
					paging.tlb.write[tlb_index] = handler->GetHostWritePt(phys_page)-lin_base;
				else paging.tlb.write[tlb_index] = 0;
			} else {
				paging.tlb.writehandler[tlb_index] = &normalcore_foiling_handler;
				paging.tlb.write[tlb_index] = 0;
			}
		}
	}
	
	if (GCC_UNLIKELY(CPU_HAS_WP_FLAG)) {
		// ur: no change with WP=1
		// kr
		if (isUser) {
			// sv -> us: re -> ee 
			for(Bitu i = 0; i < paging.kr_links.used; i++) {
				Bitu tlb_index = paging.kr_links.entries[i];
				paging.tlb.readhandler[tlb_index] = &normalcore_exception_handler;
				paging.tlb.read[tlb_index] = 0;
			}
		} else {
			// us -> sv: ee -> re
			for(Bitu i = 0; i < paging.kr_links.used; i++) {
				Bitu tlb_index = paging.kr_links.entries[i];
				Bitu lin_base = tlb_index << 12;
				Bitu phys_page = paging.tlb.phys_page[tlb_index] & PHYSPAGE_ADDR;
				PageHandler* handler = MEM_GetPageHandler(phys_page);

				paging.tlb.readhandler[tlb_index] = handler;
				if (handler->flags&PFLAG_READABLE)
					paging.tlb.read[tlb_index] = handler->GetHostReadPt(phys_page)-lin_base;
				else paging.tlb.read[tlb_index] = 0;
			}
		}
	} else { // WP=0
		// ur
		if (isUser) {
			// sv -> us: rw -> re 
			for(Bitu i = 0; i < paging.ur_links.used; i++) {
				Bitu tlb_index = paging.ur_links.entries[i];
				paging.tlb.writehandler[tlb_index] = &normalcore_exception_handler;
				paging.tlb.write[tlb_index] = 0;
			}
		} else {
			// us -> sv: re -> rw
			for(Bitu i = 0; i < paging.ur_links.used; i++) {
				Bitu tlb_index = paging.ur_links.entries[i];
				Bitu phys_page = paging.tlb.phys_page[tlb_index];
				bool dirty = (phys_page & PHYSPAGE_DITRY)? true:false;
				phys_page &= PHYSPAGE_ADDR;
				PageHandler* handler = MEM_GetPageHandler(phys_page);

				if (dirty) {
					Bitu lin_base = tlb_index << 12;
					paging.tlb.writehandler[tlb_index] = handler;
					if (handler->flags&PFLAG_WRITEABLE)
						paging.tlb.write[tlb_index] = handler->GetHostWritePt(phys_page)-lin_base;
					else paging.tlb.write[tlb_index] = 0;
				} else {
					paging.tlb.writehandler[tlb_index] = &normalcore_foiling_handler;
					paging.tlb.write[tlb_index] = 0;
				}
			}
		}
	}
}

void PAGING_Enable(bool enabled) {
	/* If paging is disabled, we work from a default paging table */
	if (paging.enabled==enabled) return;
	paging.enabled=enabled;
	if (enabled) {
		if (GCC_UNLIKELY(cpudecoder==CPU_Core_Simple_Run)) {
			//LOG_MSG("CPU core simple won't run this game,switching to normal");
			cpudecoder=CPU_Core_Normal_Run;
			CPU_CycleLeft+=CPU_Cycles;
			CPU_Cycles=0;
		}
		//LOG(LOG_PAGING,LOG_NORMAL)("Enabled");
		PAGING_SetDirBase(paging.cr3);
	}
	PAGING_ClearTLB();
}

bool PAGING_Enabled(void) {
	return paging.enabled;
}

static void PAGING_ShutDown(Section* /*sec*/) {
	init_page_handler = NULL;
	paging_prevent_exception_jump = false;
}

void PAGING_Init(Section * sec) {
	//logcnt = 0;
	sec->AddDestroyFunction(&PAGING_ShutDown);

	Bitu i;

	// log
	LOG(LOG_PAGING,LOG_NORMAL)("Initializing paging system (CPU linear -> physical mapping system)");

	PAGING_OnChangeCore();
	//paging_prevent_exception_jump = false;
	//const char* core = static_cast<Section_prop *>(control->GetSection("cpu"))->Get_string("core");
	//init_page_handler = ((core[0] == 'a' || core[0] == 'd') ? (PageHandler*)&dyncore_init_page_handler : (PageHandler*)&normalcore_init_page_handler);
	//dosbox_allow_nonrecursive_page_fault = !(core[0] == 'a' || core[0] == 'd');

	/* Setup default Page Directory, force it to update */
	paging.enabled=false;
	PAGING_InitTLB();
	for (i=0;i<LINK_START;i++) paging.firstmb[i]=i;
	pf_queue.used=0;
}

#include "control.h"
void PAGING_OnChangeCore(void) {
	// Use dynamic core compatible init page handler when core is set to 'dynamic' or 'auto'
	const char* core = static_cast<Section_prop *>(control->GetSection("cpu"))->Get_string("core");
	PageHandler* next_init_page_handler = ((core[0] == 'a' || core[0] == 'd') ? (PageHandler*)&dyncore_init_page_handler : (PageHandler*)&normalcore_init_page_handler);
	PageHandler* prev_init_page_handler = init_page_handler;
	if (prev_init_page_handler == next_init_page_handler) return;

	if (prev_init_page_handler)
	{
		for (Bitu i=0;i<TLB_SIZE;i++)
		{
			if (paging.tlb.readhandler[i]==prev_init_page_handler) paging.tlb.readhandler[i]=next_init_page_handler;
			if (paging.tlb.writehandler[i]==prev_init_page_handler) paging.tlb.writehandler[i]=next_init_page_handler;
		}
	}
	init_page_handler = next_init_page_handler;
}

#include <dbp_serialize.h>

typedef CPU_Decoder* CPU_DecoderPtr;
DBP_SERIALIZE_SET_POINTER_LIST(CPU_DecoderPtr, Paging, PageFaultCore);

void DBPSerialize_Paging(DBPArchive& ar)
{
	ar.Serialize(paging.cr3);
	ar.Serialize(paging.cr2);
	ar.Serialize(paging.base);
	ar.SerializeSparse(paging.tlb.phys_page, sizeof(paging.tlb.phys_page));
	if (ar.version < 5)
		ar.SerializeSparse(paging.links.entries, sizeof(paging.links.entries));
	ar.SerializeArray(paging.firstmb);
	ar.Serialize(paging.enabled);
	if (ar.version >= 5)
		ar.Serialize(pf_queue.entries[pf_queue.used?pf_queue.used-1:0]);
	else if (ar.version == 4)
		{ Bitu oldused; ar.Serialize(oldused).Discard(oldused * sizeof(pf_queue.entries[0])); }
	else // ar.version <= 3
		ar.Discard(((Bit8u*)&pf_queue.entries[16] - (Bit8u*)&pf_queue));

	if (ar.mode == DBPArchive::MODE_LOAD)
	{
		//PAGING_InitTLB();
		memset(paging.tlb.read, 0, sizeof(paging.tlb.read));
		memset(paging.tlb.write, 0, sizeof(paging.tlb.write));
		for (Bitu i = 0; i != LINK_START; i++) {
			paging.tlb.readhandler[i]=init_page_handler;
			paging.tlb.writehandler[i]=init_page_handler;
		}
		PAGING_ClearTLB();
	}
	if (ar.mode == DBPArchive::MODE_ZERO)
		pf_queue.used = 0;
}
