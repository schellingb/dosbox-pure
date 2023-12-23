/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *  Copyright (C) 2023 Bernhard Schelling
 *
 * Parts of this file (NE2000/ethernet code base) are:
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
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
#include "regs.h"
#include "setup.h"
#include "callback.h"
#include "pic.h"
#include "cross.h"
#include "dbp_network.h"
#include "dbp_threads.h"
#include <stdarg.h> /* va_list */
#include <stdlib.h> /* realloc, free */ 

struct DBP_Net
{
	enum { FIRST_MAC_OCTET = 0xde }; // has locally administered address bit (0x02) set
	enum { PKT_IPX, PKT_NE2K, PKT_MODEM };
	std::vector<Bit8u> IncomingIPX, IncomingNe2k, IncomingModem, OutgoingPackets, OutgoingModem;
	Mutex IncomingMtx, OutgoingMtx;
	struct Addr { Bit8u ipxnetworknum[4], mac[6]; };
};

static bool dbp_net_connected;
static DBP_Net::Addr dbp_net_addr;
static DBP_Net* dbp_net;
const char* dbp_net_msg;
static struct DBP_Net_Cleanup { ~DBP_Net_Cleanup() { delete dbp_net; } } _dbp_net_cleanup;

#if 1
#define NET_READ_LE16(p) ((Bit16u)(((const Bit8u *)(p))[0]) | ((Bit16u)(((const Bit8u *)(p))[1]) << 8U))
#define NET_READ_BE16(p) (((Bit16u)(((const Bit8u *)(p))[0]) << 8U) | (Bit16u)(((const Bit8u *)(p))[1]))
#define NET_WRITE_LE16(p,v) { ((Bit8u*)(p))[0] = (Bit8u)((Bit16u)(v) & 0xFF); ((Bit8u*)(p))[1] = (Bit8u)(((Bit16u)(v) >> 8) & 0xFF); }
#define NET_WRITE_BE16(p,v) { ((Bit8u*)(p))[0] = (Bit8u)(((Bit16u)(v) >> 8) & 0xFF); ((Bit8u*)(p))[1] = (Bit8u)((Bit16u)(v) & 0xFF); }
#else
INLINE Bit16u NET_READ_LE16(const void* p) { return ((Bit16u)(((const Bit8u *)(p))[0]) | ((Bit16u)(((const Bit8u *)(p))[1]) << 8U)); }
INLINE Bit16u NET_READ_BE16(const void* p) { return (((Bit16u)(((const Bit8u *)(p))[0]) << 8U) | (Bit16u)(((const Bit8u *)(p))[1])); }
INLINE void NET_WRITE_LE16(void* p, Bit16u v) { ((Bit8u*)(p))[0] = (Bit8u)((Bit16u)(v) & 0xFF); ((Bit8u*)(p))[1] = (Bit8u)(((Bit16u)(v) >> 8) & 0xFF); }
INLINE void NET_WRITE_BE16(void* p, Bit16u v) { ((Bit8u*)(p))[0] = (Bit8u)(((Bit16u)(v) >> 8) & 0xFF); ((Bit8u*)(p))[1] = (Bit8u)((Bit16u)(v) & 0xFF); }
#endif

static void DBP_Net_InitMac(uint16_t client_id = 0)
{
	dbp_net_addr.mac[0] = DBP_Net::FIRST_MAC_OCTET;
	dbp_net_addr.mac[1] = 0xb0;
	dbp_net_addr.mac[2] = 0xc9;
	dbp_net_addr.mac[3] = 0x00;
	NET_WRITE_BE16(&dbp_net_addr.mac[4], client_id);
}

#ifdef C_DBP_ENABLE_LIBRETRO_IPX

#if 0
#define LOG_IPX LOG_MSG
#else
#define LOG_IPX(...)
#endif

static Bit16u ipx_dospage;

static struct IPXECB *ECBList, *ESRList; // Linked list of ECB's,  ECBs waiting to be ESR notified

struct IPXHeader {
	Bit8u checkSum[2], length[2], transportControl, packetType;
	struct transport { Bit8u network[4]; Bit8u mac[6]; Bit8u socket[2]; } dest, src;
};

struct IPXECB
{
	enum
	{
		// In Use Flag codes
		USEFLAG_AVAILABLE = 0x00,
		USEFLAG_AESCOUNT  = 0xfd,
		USEFLAG_LISTENING = 0xfe,
		USEFLAG_SENDING   = 0xff,

		// Completion codes
		COMP_SUCCESS       = 0x00,
		COMP_CANCELLED     = 0xfc,
		COMP_MALFORMED     = 0xfd,
		COMP_UNDELIVERABLE = 0xfe,
		COMP_HARDWAREERROR = 0xff,
	};

	struct fragmentDescriptor { Bit16u offset, segment, size; };

	RealPt ECBAddr;
	bool isInESRList;
	void* databuffer; // received data is stored here until we get called by interrupt
	Bit16u bufres, buflen;
	IPXECB *prevECB, *nextECB; // Linked List
	Bit8u iuflag; // Need to save data since we are not always in
	Bit16u mysocket; // real mode

	IPXECB(Bit16u segment, Bit16u offset) {
		ECBAddr = RealMake(segment, offset);
		isInESRList = false;
		databuffer = NULL;
		bufres = 0;
		prevECB = nextECB = NULL;
		if (ECBList == NULL)
			ECBList = this;
		else {
			// Traverse the list until we hit the end
			IPXECB *useECB = ECBList;
			while (useECB->nextECB) useECB = useECB->nextECB;
			useECB->nextECB = this;
			this->prevECB = useECB;
		}

		iuflag = getInUseFlag();
		mysocket = getSocket();

		#ifdef IPX_DEBUGMSG
		Bitu ECBAmount = 0; for (IPXECB* i = ECBList; i; i = i->nextECB) ECBAmount++;
		LOG_IPX("ECB: created.   Number of ECBs: %3d, ESR %4x:%4x, ECB %4x:%4x",
			ECBAmount, real_readw(RealSeg(ECBAddr), RealOff(ECBAddr)+6), real_readw(RealSeg(ECBAddr), RealOff(ECBAddr)+4),segment,offset);
		#endif
	}

	~IPXECB() {
		#ifdef IPX_DEBUGMSG
		Bitu ECBAmount = 0; for (IPXECB* i = ECBList; i; i = i->nextECB) ECBAmount++;
		LOG_IPX("ECB: destroyed. Remaining ECBs: %3d", ECBAmount-1);
		#endif

		if(isInESRList) {
			// in ESR list, always the first element is deleted.
			ESRList=nextECB;
		} else {
			if(prevECB == NULL) {	// was the first in the list
				ECBList = nextECB;
				if(ECBList != NULL) ECBList->prevECB = NULL;
			} else {	// not the first
				prevECB->nextECB = nextECB;
				if(nextECB != NULL) nextECB->prevECB = prevECB;
			}
		}

		if (databuffer) free(databuffer);
	}

	void writeDataBuffer(Bit8u* buffer, Bit16u length) {
		if (length > bufres) { bufres = length; databuffer = realloc(databuffer, length); }
		memcpy(databuffer, buffer, length);
		buflen = length;
	}

	bool writeData() {
		Bitu length = buflen;
		Bit8u* buffer = (Bit8u*)databuffer;
		fragmentDescriptor tmpFrag;
		setInUseFlag(USEFLAG_AVAILABLE);
		Bit16u fragCount = getFragCount();
		Bitu bufoffset = 0;
		for(Bit16u i = 0;i < fragCount;i++) {
			getFragDesc(i,&tmpFrag);
			for(Bit16u t = 0;t < tmpFrag.size;t++) {
				real_writeb(tmpFrag.segment, tmpFrag.offset + t, buffer[bufoffset]);
				bufoffset++;
				if(bufoffset >= length) {
					setCompletionFlag(COMP_SUCCESS);
					setImmAddress(&buffer[22]);  // Write in source node
					return true;
				}
			}
		}
		if(bufoffset < length) {
			setCompletionFlag(COMP_MALFORMED);
			return false;
		}
		return false;
	}

	Bit16u getSocket(void) {
		Bit16u w = real_readw(RealSeg(ECBAddr), RealOff(ECBAddr) + 0xa);
		return NET_READ_BE16(&w);
	}

	Bit8u getInUseFlag(void) {
		return real_readb(RealSeg(ECBAddr), RealOff(ECBAddr) + 0x8);
	}

	void setInUseFlag(Bit8u flagval) {
		iuflag = flagval;
		real_writeb(RealSeg(ECBAddr), RealOff(ECBAddr) + 0x8, flagval);
	}

	void setCompletionFlag(Bit8u flagval) {
		real_writeb(RealSeg(ECBAddr), RealOff(ECBAddr) + 0x9, flagval);
	}

	Bit16u getFragCount(void) {
		return real_readw(RealSeg(ECBAddr), RealOff(ECBAddr) + 34);
	}

	void getFragDesc(Bit16u descNum, fragmentDescriptor *fragDesc) {
		Bit16u memoff = RealOff(ECBAddr) + 30 + ((descNum+1) * 6);
		fragDesc->offset = real_readw(RealSeg(ECBAddr), memoff);
		memoff += 2;
		fragDesc->segment = real_readw(RealSeg(ECBAddr), memoff);
		memoff += 2;
		fragDesc->size = real_readw(RealSeg(ECBAddr), memoff);
	}

	RealPt getESRAddr(void) {
		return RealMake(real_readw(RealSeg(ECBAddr),
			RealOff(ECBAddr)+6),
			real_readw(RealSeg(ECBAddr),
			RealOff(ECBAddr)+4));
	}

	void NotifyESR(void) {
		Bit32u ESRval = real_readd(RealSeg(ECBAddr), RealOff(ECBAddr)+4);
		if(ESRval || databuffer) { // databuffer: write data at realmode/v86 time
			// LOG_IPX("ECB: to be notified.");
			// take the ECB out of the current list
			if(prevECB == NULL) {	// was the first in the list
				ECBList = nextECB;
				if(ECBList != NULL) ECBList->prevECB = NULL;
			} else {		// not the first
				prevECB->nextECB = nextECB;
				if(nextECB != NULL) nextECB->prevECB = prevECB;
			}

			nextECB = NULL;
			// put it to the notification queue
			if(ESRList==NULL) {
				ESRList = this;
				prevECB = NULL;
			} else  {// put to end of ESR list
				IPXECB* useECB = ESRList;

				while(useECB->nextECB != NULL)
					useECB = useECB->nextECB;

				useECB->nextECB = this;
				prevECB = useECB;
			}
			isInESRList = true;
			PIC_ActivateIRQ(11);
		}
		// this one does not want to be notified, delete it right away
		else delete this;
	}

	void setImmAddress(Bit8u *immAddr) {
		for(Bit8u i=0;i<6;i++)
			real_writeb(RealSeg(ECBAddr), RealOff(ECBAddr)+28+i, immAddr[i]);
	}

	void getImmAddress(Bit8u* immAddr) {
		for(Bit8u i=0;i<6;i++)
			immAddr[i] = real_readb(RealSeg(ECBAddr), RealOff(ECBAddr)+28+i);
	}
};

struct DOSIPX
{
	enum { IPXBUFFERSIZE = 1424, SOCKTABLESIZE = 150 }; // DOS IPX driver was limited to 150 open sockets 

	static DOSIPX* self;
	RealPt ipx_callback;
	Bit16u socketCount, opensockets[SOCKTABLESIZE];
	CALLBACK_HandlerObject callback_ipx, callback_esr, callback_ipxint;
	RealPt old_73_vector;

	DOSIPX()
	{
		DBP_ASSERT(ECBList == NULL && ESRList == NULL);
		old_73_vector = 0;
		socketCount = 0;

		DOS_AddMultiplexHandler(IPX_Multiplex);

		callback_ipx.Install(&IPX_Handler,CB_RETF,"IPX Handler");
		ipx_callback = callback_ipx.Get_RealPointer();

		callback_ipxint.Install(&IPX_IntHandler,CB_IRET,"IPX (int 7a)");
		callback_ipxint.Set_RealVec(0x7a);

		callback_esr.Allocate(&IPX_ESRHandler,"IPX_ESR");
		Bit16u call_ipxesr1 = callback_esr.Get_callback();

		if (!ipx_dospage) ipx_dospage = DOS_GetMemory(2); // can not be freed yet

		PhysPt phyDospage = PhysMake(ipx_dospage,0);

		LOG_IPX("ESR callback address: %x, HandlerID %d", phyDospage,call_ipxesr1);

		// save registers
		phys_writeb(phyDospage+0,(Bit8u)0xFA);    // CLI
		phys_writeb(phyDospage+1,(Bit8u)0x60);    // PUSHA
		phys_writeb(phyDospage+2,(Bit8u)0x1E);    // PUSH DS
		phys_writeb(phyDospage+3,(Bit8u)0x06);    // PUSH ES
		phys_writew(phyDospage+4,(Bit16u)0xA00F); // PUSH FS
		phys_writew(phyDospage+6,(Bit16u)0xA80F); // PUSH GS

		// callback
		phys_writeb(phyDospage+8,(Bit8u)0xFE);  // GRP 4
		phys_writeb(phyDospage+9,(Bit8u)0x38);  // Extra Callback instruction
		phys_writew(phyDospage+10,call_ipxesr1);        // Callback identifier

		// register recreation
		phys_writew(phyDospage+12,(Bit16u)0xA90F); // POP GS
		phys_writew(phyDospage+14,(Bit16u)0xA10F); // POP FS
		phys_writeb(phyDospage+16,(Bit8u)0x07);    // POP ES
		phys_writeb(phyDospage+17,(Bit8u)0x1F);    // POP DS
		phys_writeb(phyDospage+18,(Bit8u)0x61);    // POPA
		phys_writeb(phyDospage+19,(Bit8u)0xCF);    // IRET: restores flags, CS, IP

		RealPt ESRRoutineBase = RealMake(ipx_dospage, 0);

		// Interrupt enabling
		RealSetVec(0x73,ESRRoutineBase,old_73_vector);	// IRQ11
		IO_WriteB(0xa1,IO_ReadB(0xa1)&(~8));			// enable IRQ11

		TIMER_AddTickHandler(&IPX_IncomingLoop);
	}

	~DOSIPX()
	{
		TIMER_DelTickHandler(&IPX_IncomingLoop);
		PIC_RemoveEvents(IPX_AES_EventHandler);
		DOS_DelMultiplexHandler(IPX_Multiplex);

		extern bool DBP_IsShuttingDown();
		RemoveISR(DBP_IsShuttingDown());
	}

	void RemoveISR(bool abandon_dos_mem)
	{
		CloseAllSockets();

		if (old_73_vector) {
			LOG_IPX("IPX: Removing ISR handler from interrupt");
			RealSetVec(0x73,old_73_vector);
			IO_WriteB(0xa1,IO_ReadB(0xa1)|8);	// disable IRQ11
			old_73_vector = 0;
		}

		if (ipx_dospage) {
			/* The ISR was contained in dospage.
			 * One use for this call is that the emulator is booting a guest OS.
			 * In that case it is pointless to hold onto dospage because DOS memory allocation has
			 * no meaning once a guest OS is running. */
			LOG_IPX("IPX: Freeing DOS memory used to hold ISR");
			PhysPt phyDospage = PhysMake(ipx_dospage,0);
			for(PhysPt i = 0;i < 32;i++)
				phys_writeb(phyDospage+i,(Bit8u)0x00);

			if (abandon_dos_mem)
				ipx_dospage = 0;
		}
	}

	static void IPX_IncomingLoop(void)
	{
		if (!dbp_net_connected || !dbp_net->IncomingIPX.size()) return;

		dbp_net->IncomingMtx.Lock();
		Bit16u len;
		for (Bit8u* p = &dbp_net->IncomingIPX[0], *pEnd = p + dbp_net->IncomingIPX.size(); p < pEnd; p += 2 + len)
		{
			//LogPacket("IPX::IncomingLoop", p + 2, NET_READ_LE16(p));
			len = NET_READ_LE16(p);
			self->receivePacket(p + 2, len);
		}
		dbp_net->IncomingIPX.clear();
		dbp_net->IncomingMtx.Unlock();
	}

	int sockInUse(Bit16u sockNum) {
		for (int i = 0; i < socketCount; i++) if (opensockets[i] == sockNum) return i;
		return -1;
	}

	void OpenSocket(void) {
		Bit16u sockNum = NET_READ_BE16(&reg_dx);

		if(socketCount >= SOCKTABLESIZE) {
			reg_al = 0xfe; // Socket table full
			return;
		}

		if(sockNum == 0x0000) {
			// Dynamic socket allocation
			Bit16u sockAlloc = 0x4002;
			while (sockInUse(sockAlloc) >= 0 && (sockAlloc < 0x7fff)) sockAlloc++;
			if(sockAlloc > 0x7fff) {
				// I have no idea how this could happen if the IPX driver
				// is limited to 150 open sockets at a time
				LOG_MSG("IPX: Out of dynamic sockets");
			}
			sockNum = sockAlloc;
		} else {
			if(sockInUse(sockNum) >= 0) {
				reg_al = 0xff; // Socket already open
				return;
			}
		}

		opensockets[socketCount] = sockNum;
		socketCount++;

		reg_al = 0x00; // Success
		NET_WRITE_BE16(&reg_dx, sockNum); // Convert back to big-endian
	}

	void CloseSocket(void) {
		Bit16u sockNum = NET_READ_BE16(&reg_dx);
		int socki = sockInUse(sockNum);
		if (socki < 0) return;
		socketCount--;
		for (int i = socki; i != socketCount; i++) // Realign list of open sockets
			opensockets[i] = opensockets[i + 1];

		// delete all ECBs of that socket
		for (IPXECB *tmpECB = ECBList, *tmp2ECB; tmpECB; tmpECB = tmp2ECB) {
			tmp2ECB = tmpECB->nextECB;
			if (tmpECB->getSocket() != sockNum) continue;
			tmpECB->setCompletionFlag(IPXECB::COMP_CANCELLED);
			tmpECB->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
			delete tmpECB;
		}
	}

	void CloseAllSockets(void) {
		/* This can be called when booting a guest OS.
		 * Managing IPX socket state in memory is not a good idea once a guest OS is running. */
		LOG_IPX("IPX: Closing all active sockets");
		while (ECBList) {
			ECBList->setCompletionFlag(IPXECB::COMP_CANCELLED);
			ECBList->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
			delete ECBList; // updates ECBList
		}
		while (ESRList) {
			ESRList->setCompletionFlag(IPXECB::COMP_CANCELLED);
			ESRList->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
			delete ESRList; // updates ESRList
		}
	}

	static bool IPX_Multiplex(void) {
		if(reg_ax != 0x7a00) return false;
		reg_al = 0xff;
		SegSet16(es,RealSeg(self->ipx_callback));
		reg_di = RealOff(self->ipx_callback);
		return true;
	}

	static void IPX_AES_EventHandler(Bitu param) {
		for (IPXECB *tmpECB = ECBList, *tmp2ECB; tmpECB; tmpECB = tmp2ECB) {
			tmp2ECB = tmpECB->nextECB;
			if (tmpECB->iuflag != IPXECB::USEFLAG_AESCOUNT || param != (Bitu)tmpECB->ECBAddr) continue;
			tmpECB->setCompletionFlag(IPXECB::COMP_SUCCESS);
			tmpECB->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
			tmpECB->NotifyESR();
			// LOG_IPX("AES Notification: ECB");
			return;
		}
		LOG_MSG("!!!! Rogue AES !!!!" );
	}

	void handleIpxRequest(void) {
		IPXECB *tmpECB;
		switch (reg_bx) {
			case 0x0000:	// Open socket
				OpenSocket();
				LOG_IPX("IPX: Open socket %4x", NET_READ_BE16(&reg_dx));
				break;
			case 0x0001:	// Close socket
				LOG_IPX("IPX: Close socket %4x", NET_READ_BE16(&reg_dx));
				CloseSocket();
				break;
			case 0x0002:	// get local target [es:si] Currently no support for multiple networks
				for(Bit8u i = 0; i < 6; i++)
					real_writeb(SegValue(es),reg_di+i,real_readb(SegValue(es),reg_si+i+4));
				reg_cx=1;		// time ticks expected
				reg_al=0x00;	//success
				break;
			case 0x0003:		// Send packet
				tmpECB = new IPXECB(SegValue(es),reg_si);
				if (!true) { // not connected but we're always conntected //if(!connected) {
					tmpECB->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
					tmpECB->setCompletionFlag(IPXECB::COMP_UNDELIVERABLE);
					delete tmpECB;	// not notify?
					reg_al = 0xff; // Failure
				} else {
					tmpECB->setInUseFlag(IPXECB::USEFLAG_SENDING);
					//LOG_IPX("IPX: Sending packet on %4x", tmpECB->getSocket());
					reg_al = 0x00; // Success
					sendPacket(tmpECB);
				}
				break;
			case 0x0004: // Listen for packet
				tmpECB = new IPXECB(SegValue(es),reg_si);
				//LOG_IPX("ECB: RECEIVE.");
				if (sockInUse(tmpECB->getSocket()) < 0) {  // Socket is not open
					reg_al = 0xff;
					tmpECB->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
					tmpECB->setCompletionFlag(IPXECB::COMP_HARDWAREERROR);
					delete tmpECB;
				} else {
					reg_al = 0x00;  // Success
					tmpECB->setInUseFlag(IPXECB::USEFLAG_LISTENING);
					//LOG_IPX("IPX: Listen for packet on 0x%4x - ESR address %4x:%4x", tmpECB->getSocket(), RealSeg(tmpECB->getESRAddr()), RealOff(tmpECB->getESRAddr()));
				}
				break;
			case 0x0005:	// SCHEDULE IPX EVENT
			case 0x0007:	// SCHEDULE SPECIAL IPX EVENT
			{
				tmpECB = new IPXECB(SegValue(es),reg_si);
				//LOG_IPX("ECB: AES. T=%fms.", (1000.0f/(1193182.0f/65536.0f))*(float)reg_ax);
				PIC_AddEvent(IPX_AES_EventHandler,
					(1000.0f/(1193182.0f/65536.0f))*(float)reg_ax,(Bitu)tmpECB->ECBAddr);
				tmpECB->setInUseFlag(IPXECB::USEFLAG_AESCOUNT);
				break;
			}
			case 0x0006:	// cancel operation
			{
				RealPt ecbaddress = RealMake(SegValue(es),reg_si);
				IPXECB* tmpECB= ECBList;
				IPXECB* tmp2ECB;
				while(tmpECB) {
					tmp2ECB=tmpECB->nextECB;
					if(tmpECB->ECBAddr == ecbaddress) {
						if(tmpECB->getInUseFlag()==IPXECB::USEFLAG_AESCOUNT)
							PIC_RemoveSpecificEvents(IPX_AES_EventHandler,(Bitu)ecbaddress);
						tmpECB->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
						tmpECB->setCompletionFlag(IPXECB::COMP_CANCELLED);
						delete tmpECB;
						reg_al=0;	// Success
						LOG_IPX("IPX: ECB canceled.");
						return;
					}
					tmpECB=tmp2ECB;
				}
				reg_al=0xff;	// Fail
				break;
			}
			case 0x0008:		// Get interval marker
				reg_ax = mem_readw(0x46c); // BIOS_TIMER
				break;
			case 0x0009:		// Get internetwork address
			{
				LOG_IPX("IPX: Get internetwork address %2x:%2x:%2x:%2x:%2x:%2x",
					dbp_net_addr.mac[5], dbp_net_addr.mac[4],
					dbp_net_addr.mac[3], dbp_net_addr.mac[2],
					dbp_net_addr.mac[1], dbp_net_addr.mac[0]);

				Bit8u * addrptr = (Bit8u *)&dbp_net_addr;
				for(Bit16u i=0;i<10;i++)
					real_writeb(SegValue(es),reg_si+i,addrptr[i]);
				break;
			}
			case 0x000a:		// Relinquish control
				break;			// Idle thingy
			case 0x000b:		// Disconnect from Target
				break;			// We don't even connect
			case 0x000d:		// get packet size
				reg_cx=0;		// retry count
				reg_ax=1024;	// real implementation returns 1024
				break;
			case 0x0010:		// SPX install check
				reg_al=0;		// SPX not installed
				break;
			case 0x001a:		// get driver maximum packet size
				reg_cx=0;		// retry count
				reg_ax=IPXBUFFERSIZE;	// max packet size: something near the ethernet packet size
				break;
			default:
				LOG_MSG("Unhandled IPX function: %4x", reg_bx);
				break;
		}
	}

	// Entrypoint handler
	static Bitu IPX_Handler(void) {
		self->handleIpxRequest();
		return CBRET_NONE;
	}

	// INT 7A handler
	static Bitu IPX_IntHandler(void) {
		self->handleIpxRequest();
		return CBRET_NONE;
	}

	//static void LogPacket(const char* info, const Bit8u *p, size_t len)
	//{
	//	const IPXHeader* h = (const IPXHeader*)p;
	//	if (len < (sizeof(IPXHeader))) { DBP_ASSERT(false); return; }
	//	printf("[DOSBOXNET] %s - Len: %d, ChkSum: %04x, IpxLen: %04x, TC: %d, Typ: %d,  Src: %04x%04x_%02x:%02x:%02x:%02x:%02x:%02x@#%d, Dest: %04x%04x_%02x:%02x:%02x:%02x:%02x:%02x@#%d\n", info, len,
	//		NET_READ_BE16(h->checkSum), NET_READ_BE16(h->length), h->transportControl, h->packetType,
	//		NET_READ_BE16(h->src.network), NET_READ_BE16(h->src.network+2), h->src.mac[0], h->src.mac[1], h->src.mac[2], h->src.mac[3], h->src.mac[4], h->src.mac[5], NET_READ_BE16(h->src.socket),
	//		NET_READ_BE16(h->dest.network), NET_READ_BE16(h->dest.network+2), h->dest.mac[0], h->dest.mac[1], h->dest.mac[2], h->dest.mac[3], h->dest.mac[4], h->dest.mac[5], NET_READ_BE16(h->dest.socket));
	//	printf("            ");
	//	while (len--) printf("%02x ", *(p++));
	//	printf("\n");
	//}

	void receivePacket(Bit8u *buffer, Bit16u bufSize) {
		IPXECB *useECB;
		IPXECB *nextECB;
		Bit16u useSocket = NET_READ_BE16(&buffer[16]);

		useECB = ECBList;
		while(useECB != NULL)
		{
			nextECB = useECB->nextECB;
			if(useECB->iuflag == IPXECB::USEFLAG_LISTENING && useECB->mysocket == useSocket) {
				useECB->writeDataBuffer(buffer, bufSize);
				useECB->NotifyESR();
				return;
			}
			useECB = nextECB;
		}
		LOG_IPX("IPX: RX Packet loss!");
	}

	void sendPacket(IPXECB* sendecb) {
		Bit8u outbuffer[IPXBUFFERSIZE];
		IPXECB::fragmentDescriptor tmpFrag;
		Bit16u i, fragCount,t;
		Bit16u packetsize;

		sendecb->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
		packetsize = 0;
		fragCount = sendecb->getFragCount();
		for(i=0;i<fragCount;i++) {
			sendecb->getFragDesc(i,&tmpFrag);
			if(i==0) {
				// Fragment containing IPX header
				// Must put source address into header
				Bit8u * addrptr;

				// source netnum
				addrptr = (Bit8u *)&dbp_net_addr.ipxnetworknum;
				for(Bit16u m=0;m<4;m++) {
					real_writeb(tmpFrag.segment,tmpFrag.offset+m+18,addrptr[m]);
				}
				// source node number
				addrptr = (Bit8u *)&dbp_net_addr.mac;
				for(Bit16u m=0;m<6;m++) {
					real_writeb(tmpFrag.segment,tmpFrag.offset+m+22,addrptr[m]);
				}
				// Source socket
				Bit16u socket = sendecb->getSocket();
				real_writew(tmpFrag.segment,tmpFrag.offset+28, NET_READ_BE16(&socket));

				// blank checksum
				real_writew(tmpFrag.segment,tmpFrag.offset, 0xffff);
			}

			for(t=0;t<tmpFrag.size;t++) {
				outbuffer[packetsize] = real_readb(tmpFrag.segment, tmpFrag.offset + t);
				packetsize++;
				if (packetsize >= IPXBUFFERSIZE) {
					LOG_MSG("IPX: Packet size to be sent greater than %d bytes.", IPXBUFFERSIZE);
					sendecb->setCompletionFlag(IPXECB::COMP_UNDELIVERABLE);
					sendecb->NotifyESR();
					return;
				}
			}
		}

		// Add length and source socket to IPX header
		// Blank CRC
		DBP_ASSERT(NET_READ_BE16(outbuffer+0) == 0xffff);
		// Length
		NET_WRITE_BE16(outbuffer+2, packetsize);
		// Source socket
		DBP_ASSERT(NET_READ_BE16(outbuffer+28) == sendecb->getSocket());

		sendecb->getFragDesc(0,&tmpFrag);
		real_writew(tmpFrag.segment,tmpFrag.offset+2, NET_READ_BE16(&packetsize));

		Bit8u immedAddr[6];
		sendecb->getImmAddress(immedAddr);
		// filter out broadcasts and local loopbacks
		// Real implementation uses the ImmedAddr to check whether this is a broadcast

		bool isloopback=true;
		Bit8u * addrptr;
		addrptr = (Bit8u *)&dbp_net_addr.ipxnetworknum;
		for(Bitu m=0;m<4;m++) {
			if(addrptr[m]!=outbuffer[m+0x6])isloopback=false;
		}

		bool islocalbroadcast=true;
		addrptr = (Bit8u *)&dbp_net_addr.mac;
		for(Bitu m=0;m<6;m++) {
			if(addrptr[m]!=outbuffer[m+0xa])isloopback=false;
			if(immedAddr[m]!=0xff) islocalbroadcast=false;
			DBP_ASSERT(outbuffer[m+0xa] == immedAddr[m]);
		}
		bool isgarbage = (!islocalbroadcast && !isloopback && outbuffer[0+0xa] != DBP_Net::FIRST_MAC_OCTET); // should check full MAC address?

		LOG_IPX("SEND isloopback: %d - islocalbroadcast: %d - isgarbage: %d - size: %d",(int)isloopback, (int)islocalbroadcast, (int)isgarbage, (int)packetsize);
		if (isgarbage) {
			// Descent sends packets to mac address 00:00:00:00:00:00 which we need to ignore here or otherwise a recipient would be confused
		}
		else if (!isloopback) {
			if (!dbp_net_connected) {
				sendecb->setCompletionFlag(IPXECB::COMP_HARDWAREERROR);
				sendecb->NotifyESR();
				return;
			}
			sendecb->setCompletionFlag(IPXECB::COMP_SUCCESS);

			//LogPacket("IPX::sendPacket", outbuffer, packetsize);
			LOG_IPX("Packet sent: size: %d", packetsize);
			dbp_net->OutgoingMtx.Lock();
			dbp_net->OutgoingPackets.resize(dbp_net->OutgoingPackets.size() + 3 + packetsize);
			Bit8u* p = &dbp_net->OutgoingPackets[dbp_net->OutgoingPackets.size() - 3 - packetsize];
			NET_WRITE_LE16(p, packetsize);
			p[2] = DBP_Net::PKT_IPX;
			memcpy(p + 3, outbuffer, packetsize);
			dbp_net->OutgoingMtx.Unlock();
		}
		else sendecb->setCompletionFlag(IPXECB::COMP_SUCCESS);

		if (isloopback || islocalbroadcast) {
			// Send packet back to ourselves.
			receivePacket(outbuffer, packetsize);
			LOG_IPX("Packet back: loopback:%d, broadcast:%d",isloopback,islocalbroadcast);
		}
		sendecb->NotifyESR();
	}

	static Bitu IPX_ESRHandler(void) {
		LOG_IPX("ESR: >>>>>>>>>>>>>>>" );
		while (ESRList) {
			//LOG_IPX("ECB: notified.");
			if(ESRList->databuffer) ESRList->writeData();
			if(ESRList->getESRAddr()) {
				// setup registers
				SegSet16(es, RealSeg(ESRList->ECBAddr));
				reg_si = RealOff(ESRList->ECBAddr);
				reg_al = 0xff;
				CALLBACK_RunRealFar(RealSeg(ESRList->getESRAddr()), RealOff(ESRList->getESRAddr()));
			}
			delete ESRList; //Destructor updates this pointer to the next value or NULL
		}
		IO_WriteB(0xa0,0x63);	//EOI11
		IO_WriteB(0x20,0x62);	//EOI2
		LOG_IPX("ESR: <<<<<<<<<<<<<<<");
		return CBRET_NONE;
	}
};
DOSIPX* DOSIPX::self;

#endif // C_DBP_ENABLE_LIBRETRO_IPX

#ifdef C_DBP_ENABLE_LIBRETRO_MODEM

INLINE static void ModemBufClear(CModemBuf& b) { b.p_in = b.p_out = 0; }
INLINE static Bitu ModemBufIsEmpty(const CModemBuf& b) { return b.p_in == b.p_out; }
INLINE static Bitu ModemBufHave(const CModemBuf& b) { return ((b.p_out + CModemBuf::BUFSIZE - b.p_in) % CModemBuf::BUFSIZE); }
INLINE static Bitu ModemBufUnused(const CModemBuf& b) { return (b.p_in <= b.p_out) * CModemBuf::BUFSIZE + b.p_in - b.p_out; }
INLINE static Bit8u ModemBufGetB(CModemBuf& b) { Bit8u r = b.buf[b.p_in]; if (b.p_in == b.p_out) { DBP_ASSERT(false); } else if (++b.p_in == CModemBuf::BUFSIZE) b.p_in = 0; return r; }
INLINE static void ModemBufAddB(CModemBuf& b, Bit8u _val) { b.buf[b.p_out++] = _val; if (b.p_out == CModemBuf::BUFSIZE) b.p_out = 0; DBP_ASSERT(b.p_out != b.p_in); }
static void ModemBufAddS(CModemBuf& b, const char* fmt, ...) {
	char m[128]; va_list v; va_start(v, fmt); int len = vsnprintf(m, sizeof(m), fmt, v); va_end(v);
	for (char* p = m; len-- > 0; p++) ModemBufAddB(b, *p);
}

enum { N_RX_IDLE, N_RX_WAIT, N_RX_BLOCKED, N_RX_FASTWAIT };
enum { MREG_AUTOANSWER_COUNT, MREG_RING_COUNT, MREG_ESCAPE_CHAR, MREG_CR_CHAR, MREG_LF_CHAR, MREG_BACKSPACE_CHAR, MREG_GUARD_TIME = 12 };
enum { MRES_OK, MRES_ERROR, MRES_CONNECT, MRES_RING, MRES_NOCARRIER };
//enum { NULLMODEM_TX_PACKETDELAY = SERIAL_BASE_EVENT_COUNT+1 };

CLibretroDualModem::CLibretroDualModem(Bitu id, CommandLine* cmd) : CSerial(id, cmd)
{
	InstallationSuccessful = true; // never fails
	ModemBufClear(rbuf);
	ModemBufClear(tbuf);

	// Read null modem configuration
	Bitu val = 0;
	// transparent: don't add additional handshake control.
	nm.transparent = false;
	if (CSerial::getBituSubstring("transparent:", &val, cmd)) nm.transparent = (val == 1);
	// rxdelay: How many milliseconds to wait before causing an overflow when the application is unresponsive.
	nm.rx_retry_max = 20;
	if (CSerial::getBituSubstring("rxdelay:", &val, cmd)) { nm.rx_retry_max = (val > 10000 ? 50 : (Bit16u)val); }
	// txdelay: How many milliseconds to wait before sending data. This reduces network overhead quite a lot.
	//nm.tx_gather = 12;
	//if (CSerial::getBituSubstring("txdelay:", &val, cmd)) { nm.tx_gather = (val > 500 ? 12 : (Bit16u)val); }
	
	sm.connected = false; // because Init_Registers calls setRTSDTR
	CSerial::Init_Registers();

	//// Start out as soft modem in detect mode
	//mode = MODE_DETECT;

	// Start out with requested mode
	mode = (cmd->FindExist("null", false) ? MODE_NULLMODEM : MODE_SOFTMODEM);

	if (mode == MODE_NULLMODEM)
	{
		// We are connnected from startup
		CSerial::setCTS(nm.transparent);
		CSerial::setDSR(nm.transparent);
		CSerial::setRI(false);

		// transmit the line status
		setRTSDTR(getRTS(), getDTR());

		nm.rx_state = N_RX_IDLE;
	}
	else
	{
		SM_SetState(false, true);
	}
	setEvent(SERIAL_POLLING_EVENT, 1);
}

void CLibretroDualModem::handleUpperEvent(Bit16u type)
{
	if (mode != MODE_NULLMODEM)
	{
		switch (type)
		{
			case SERIAL_TX_EVENT:
				if (ModemBufUnused(tbuf)) {
					ModemBufAddB(tbuf, (Bit8u)sm.waiting_tx_character);
					if (ModemBufUnused(tbuf) < 2) CSerial::setCTS(false);
				} else { DBP_ASSERT(false); }
				ByteTransmitted();
				break;

			case SERIAL_RX_EVENT:
				// check for bytes to be sent to port
				if (CSerial::CanReceiveByte() && !ModemBufIsEmpty(rbuf) && (CSerial::getRTS( ) || sm.flowcontrol != 3)) {
					Bit8u rbyte = ModemBufGetB(rbuf);
					//LOG_MSG("[DOSBOX] [SOFTMODEM] Byte %c (%2x) to be received", ((rbyte >= 32 && rbyte < 127) ? rbyte : ' '), rbyte);
					CSerial::receiveByteEx(rbyte, 0);
				}
				if (CSerial::CanReceiveByte())
					setEvent(SERIAL_RX_EVENT, bytetime*0.98f);
				break;

			case SERIAL_POLLING_EVENT:
				if (!ModemBufIsEmpty(rbuf)) {
					removeEvent(SERIAL_RX_EVENT);
					setEvent(SERIAL_RX_EVENT, (float)0.01);
				}
				setEvent(SERIAL_POLLING_EVENT, 1);
				SM_Poll();
				break;
		}
	}
	else //(mode == MODE_NULLMODEM)
	{
		switch(type)
		{
			case SERIAL_TX_EVENT:
				// Maybe echo cirquit works a bit better this way
				if (nm.rx_state == N_RX_IDLE && CanReceiveByte()) {
					if (NM_DoReceive()) {
						// a byte was received
						nm.rx_state = N_RX_WAIT;
						setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
					}
				}
				ByteTransmitted();
				break;

			case SERIAL_RX_EVENT:
				switch(nm.rx_state)
				{
					case N_RX_IDLE: DBP_ASSERT(false); LOG_MSG("internal error in nullmodem");break;

					case N_RX_BLOCKED: // try to receive
					case N_RX_WAIT:
					case N_RX_FASTWAIT:
						if (CanReceiveByte()) {
							// just works or unblocked
							if (NM_DoReceive()) {
								nm.rx_retry = 0; // not waiting anymore
								if (nm.rx_state == N_RX_WAIT)
									setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
								else {
									// maybe unblocked
									nm.rx_state = N_RX_FASTWAIT;
									setEvent(SERIAL_RX_EVENT, bytetime*0.65f);
								}
							} else {
								// didn't receive anything
								nm.rx_retry = 0;
								nm.rx_state = N_RX_IDLE;
							}
						} else {
							// blocking now or still blocked
							#if SERIAL_DEBUG
							log_ser(dbg_aux,"Nullmodem: %s (retry=%d)", ((nm.rx_state == N_RX_BLOCKED) ? "rx still blocked" : "block on continued rx"), nm.rx_retry);
							#endif
							setEvent(SERIAL_RX_EVENT, bytetime*0.65f);
							nm.rx_state = N_RX_BLOCKED;
						}
						break;
				}
				break;

			case SERIAL_POLLING_EVENT:
				// periodically check if new data arrived, disconnect if required. Add it back.
				setEvent(SERIAL_POLLING_EVENT, 1.0f);
				switch(nm.rx_state)
				{
					case N_RX_IDLE:
						if (CanReceiveByte()) {
							if (NM_DoReceive()) {
								// a byte was received
								nm.rx_state = N_RX_WAIT;
								setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
							} // else still idle
						} else {
							#if SERIAL_DEBUG
							log_ser(dbg_aux,"Nullmodem: block on polling.");
							#endif
							nm.rx_state = N_RX_BLOCKED;
							// have both delays (1ms + bytetime)
							setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
						}
						break;

					case N_RX_BLOCKED:
						// one timeout tick
						if (!CanReceiveByte()) {
							if (++nm.rx_retry >= nm.rx_retry_max) {
								// it has timed out:
								nm.rx_retry = 0;
								removeEvent(SERIAL_RX_EVENT);
								if (NM_DoReceive()) {
									// read away everything
									while (NM_DoReceive());
									nm.rx_state = N_RX_WAIT;
									setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
								} else {
									// much trouble about nothing
									nm.rx_state = N_RX_IDLE;
									#if SERIAL_DEBUG
									log_ser(dbg_aux,"Nullmodem: unblock due to no more data", nm.rx_retry);
									#endif
								}
							} // else wait further
						} else {
							// good: we can receive again
							removeEvent(SERIAL_RX_EVENT);
							nm.rx_retry = 0;
							if (NM_DoReceive()) {
								nm.rx_state = N_RX_FASTWAIT;
								setEvent(SERIAL_RX_EVENT, bytetime*0.65f);
							} else {
								// much trouble about nothing
								nm.rx_state = N_RX_IDLE;
							}
						}
						break;
				}
				break;

			case SERIAL_THR_EVENT:
				ByteTransmitting();
				// actually send it
				setEvent(SERIAL_TX_EVENT, bytetime + 0.01f);
				break;

			// Unused because we send in NetCallBack::poll
			//case NULLMODEM_TX_PACKETDELAY:
			//	// TODO: Flush the data in the transmitting buffer.
			//	//if (clientsocket) clientsocket->FlushBuffer();
			//	nm.tx_block = false;
			//	break;
		}
	}
}

void CLibretroDualModem::transmitByte(Bit8u val, bool first)
{
	//LOG_MSG("[DOSBOX] [%sMODEM] Byte %c (%2x) to be transmitted", (mode != MODE_NULLMODEM ? "SOFT" : "NULL"), ((val >= 32 && val < 127) ? val : ' '), val);
	if (mode != MODE_NULLMODEM)
	{
		sm.waiting_tx_character = val;
		setEvent(SERIAL_TX_EVENT, bytetime); // TX event
		if (first) ByteTransmitting();
	}
	else //(mode == MODE_NULLMODEM)
	{
		// transmit it later in THR_Event
		if (first) setEvent(SERIAL_THR_EVENT, bytetime/8);
		else       setEvent(SERIAL_TX_EVENT, bytetime);

		if (dbp_net_connected)
			NM_DoSend(val, (!nm.transparent && val == 0xff)); // transparent mode disables 0xff escaping

		//if (!nm.tx_block)
		//{
		//	setEvent(NULLMODEM_TX_PACKETDELAY, (float)nm.tx_gather);
		//	nm.tx_block = true;
		//}
	}
}

void CLibretroDualModem::setRTSDTR(bool rts, bool dtr)
{
	//LOG_MSG("[DOSBOX] [MODEM] setRTSDTR - DTR %d - RTS %d - RI %d - CD %d - DSR %d - CTS %d - IER %x - LCR %x - MCR %x - SPR %x", (int)getDTR(), (int)getRTS(), (int)getRI(), (int)getCD(), (int)getDSR(), (int)getCTS(), (int)Read_IER(), (int)Read_LCR(), (int)Read_MCR(), (int)Read_SPR());
	if (mode != MODE_NULLMODEM)
	{
		// If DTR goes low, hang up.
		if (dtr || !sm.connected) return;
		SM_SendRes(MRES_NOCARRIER);
		SM_SetState(false);
		LOG_MSG("Modem: Hang up due to dropped DTR.");
	}
	else //(mode == MODE_NULLMODEM)
	{
		if (nm.transparent || !dbp_net_connected) return;
		NM_DoSend((Bit8u)((rts ? 1 : 0) | (dtr ? 2 : 0) | ((LCR&LCR_BREAK_MASK) ? 4 : 0)), true);
	}
}

void CLibretroDualModem::setRTS(bool val) { setRTSDTR(     val, getDTR()); }
void CLibretroDualModem::setDTR(bool val) { setRTSDTR(getRTS(),      val); }
void CLibretroDualModem::setBreak(bool)   { setRTSDTR(getRTS(), getDTR()); }
void CLibretroDualModem::updateMSR() { } // think it is not needed
void CLibretroDualModem::updatePortConfig(Bit16u, Bit8u) { } // dummy

void CLibretroDualModem::SM_SetState(bool set_connected, bool do_reset)
{
	sm.commandmode = !set_connected;
	sm.connected = set_connected;
	sm.ringing = false;
	CSerial::setCD(set_connected);
	CSerial::setRI(false);
	if (set_connected) {
		SM_SendRes(MRES_CONNECT);
		return;
	}
	CSerial::setDSR(true);
	CSerial::setCTS(true);
	ModemBufClear(tbuf);
	if (!do_reset) return;
	sm.cmdpos = 0;
	sm.cmdbuf[0] = 0;
	sm.flowcontrol = 0;
	sm.plusinc = 0;
	memset(&sm.reg, 0, sizeof(sm.reg));
	sm.reg[MREG_AUTOANSWER_COUNT] = 0; // no autoanswer
	sm.reg[MREG_RING_COUNT] = 1;
	sm.reg[MREG_ESCAPE_CHAR] = '+';
	sm.reg[MREG_CR_CHAR] = '\r';
	sm.reg[MREG_LF_CHAR] = '\n';
	sm.reg[MREG_BACKSPACE_CHAR] = '\b';
	sm.reg[MREG_GUARD_TIME] = 50;
	sm.cmdpause = 0;
	sm.echo = true;
	sm.doresponse = 0;
	sm.numericresponse = false;
}

void CLibretroDualModem::SM_SendRes(int response) {
	char const * str; int code;
	switch (response) {
		case MRES_OK:        str = "OK";            code = 0; break;
		case MRES_ERROR:     str = "ERROR";         code = 4; break;
		case MRES_RING:      str = "RING";          code = 2; break;
		case MRES_NOCARRIER: str = "NO CARRIER" ;   code = 3; break;
		case MRES_CONNECT:   str = "CONNECT 57600"; code = 1; break;
	}
	if (sm.doresponse != 1) {
		if (sm.doresponse == 2 && (response == MRES_RING || response == MRES_CONNECT || response == MRES_NOCARRIER)) return;
		if (sm.numericresponse) ModemBufAddS(rbuf, "\xd\xa%d\xd\xa", code);
		else ModemBufAddS(rbuf, "\xd\xa%s\xd\xa", str);
		LOG_MSG("Modem response: %s", str);
	}
}

void CLibretroDualModem::SM_DoCommand() {
	sm.cmdbuf[sm.cmdpos] = 0;
	sm.cmdpos = 0; //Reset for next command
	upcase(sm.cmdbuf);
	LOG_MSG("Command sent to modem: ->%s<-\n", sm.cmdbuf);
	// Check for empty line, stops dialing and autoanswer
	if (!sm.cmdbuf[0]) { sm.reg[0]=0; return; } // autoanswer off
	// AT command set interpretation
	if (sm.cmdbuf[0] != 'A' || sm.cmdbuf[1] != 'T') { SM_SendRes(MRES_ERROR); return; }

	for (char * scanbuf = &sm.cmdbuf[2];;)
	{
		struct Local { static Bitu ScanNumber(char * & scan) { for (Bitu ret = 0;; scan++) { char c = *scan; if (c < '0' || c > '9') return ret; ret = ret * 10 + (c-'0'); } } };
		char chr = *(scanbuf++), cmdchar; Bitu val;
		switch (chr)
		{
			case 'D': // Dial
				if (!dbp_net_connected) {
					dbp_net_msg = "Not connected to host, modem call failed";
					SM_SendRes(MRES_NOCARRIER);
					SM_SetState(false);
				} else {
					dbp_net_msg = "Modem calling ...";
					dbp_net->OutgoingMtx.Lock();
					if (!dbp_net->OutgoingModem.size()) dbp_net->OutgoingModem.push_back(0); // leave space for packet id
					dbp_net->OutgoingModem.push_back(0); // send a byte as ring command
					dbp_net->OutgoingMtx.Unlock();
					SM_SetState(true);
				}
				return;
			case 'I': // Some strings about firmware
				switch (Local::ScanNumber(scanbuf)) {
					case 3: ModemBufAddS(rbuf, "\xd\xaDOSBox Emulated Modem Firmware V1.00\xd\xa"); break;
					case 4: ModemBufAddS(rbuf, "\xd\xaModem for DOSBox " VERSION "\xd\xa"); break;
				}
				break;
			case 'E': // Echo on/off
				switch (Local::ScanNumber(scanbuf)) {
					case 0: sm.echo = false; break;
					case 1: sm.echo = true; break;
				}
				break;
			case 'V':
				switch (Local::ScanNumber(scanbuf)) {
					case 0: sm.numericresponse = true; break;
					case 1: sm.numericresponse = false; break;
				}
				break;
			case 'H': // Hang up
				if (Local::ScanNumber(scanbuf) == 0 && sm.connected) {
					SM_SendRes(MRES_NOCARRIER);
					SM_SetState(false);
					return;
				}
				break;
			case 'O': // Switch to data mode
				if (Local::ScanNumber(scanbuf) == 0) {
					if (dbp_net_connected) {
						sm.commandmode = false;
						return;
					} else {
						SM_SendRes(MRES_ERROR);
						return;
					}
				}
				break;
			case 'T': // Tone Dial
			case 'P': // Pulse Dial
				break;
			case 'M': // Monitor
			case 'L': // Volume
				Local::ScanNumber(scanbuf);
				break;
			case 'A': // Answer call
				if (dbp_net_connected && sm.ringing) {
					SM_SetState(true); // answer call
				} else {
					dbp_net_msg = "Modem tried to answer a call but there was none";
					SM_SendRes(MRES_ERROR);
				}
				return;
			case 'Z': // Reset and load profiles
				// scan the number away, if any
				Local::ScanNumber(scanbuf);
				if (sm.connected) SM_SendRes(MRES_NOCARRIER);
				SM_SetState(false, true);
				break;
			case ' ': // skip space
				break;
			case 'Q':
				// Response options
				// 0 = all on, 1 = all off, 2 = no ring and no connect/carrier in answermode
				if ((val = Local::ScanNumber(scanbuf)) > 2) { SM_SendRes(MRES_ERROR); return; }
				sm.doresponse = (Bit8u)val;
				break;
			case 'S': // Registers
				if ((val = Local::ScanNumber(scanbuf)) >= SREGS) {
					SM_SendRes(MRES_ERROR);
					return; //goto ret_none;
				}
				while (scanbuf[0] == ' ') scanbuf++; // skip spaces
				if (scanbuf[0] == '=') { // set register
					scanbuf++;
					while (scanbuf[0] == ' ') scanbuf++; // skip spaces
					sm.reg[val] = (Bit8u)Local::ScanNumber(scanbuf);
					break;
				}
				else if (scanbuf[0] == '?') { // get register
					ModemBufAddS(rbuf, "\xd\xa%d\xd\xa", sm.reg[val]);
					scanbuf++;
					break;
				}
				//else LOG_MSG("print reg %d with %d", val, reg[val]);
				break;
			case '&': // & escaped commands
				switch (cmdchar = *(scanbuf++)) {
					case 'K':
						val = Local::ScanNumber(scanbuf);
						if (val >= 5) { SM_SendRes(MRES_ERROR); return; }
						sm.flowcontrol = (Bit8u)val;
						break;
					case '\0':
						// end of string
						SM_SendRes(MRES_ERROR);
						return;
					default:
						LOG_MSG("Modem: Unhandled command: &%c%d", cmdchar, (int)Local::ScanNumber(scanbuf));
						break;
				}
				break;

			case '\\': // \ escaped commands
				switch (cmdchar = *(scanbuf++)) {
					case 'N':
						// error correction stuff - not emulated
						if (Local::ScanNumber(scanbuf) > 5) { SM_SendRes(MRES_ERROR); return; }
						break;
					case '\0':
						// end of string
						SM_SendRes(MRES_ERROR);
						return;
					default:
						LOG_MSG("Modem: Unhandled command: \\%c%" sBitfs(d), cmdchar, Local::ScanNumber(scanbuf));
						break;
				}
				break;

			case '\0':
				SM_SendRes(MRES_OK);
				return;

			default:
				LOG_MSG("Modem: Unhandled command: %c%" sBitfs(d), chr, Local::ScanNumber(scanbuf));
				break;
		}
	}
}

void CLibretroDualModem::SM_Poll(void)
{
	// Check for eventual break command
	if (!sm.commandmode && ++sm.cmdpause > (20 * sm.reg[MREG_GUARD_TIME])) {
		if (sm.plusinc == 0)
			sm.plusinc = 1;
		else if (sm.plusinc == 4) {
			LOG_MSG("Modem: Returning to command mode (escape sequence)");
			sm.commandmode = true;
			SM_SendRes(MRES_OK);
			sm.plusinc = 0;
		}
	}

	// Handle incoming data from serial port, read as much as available
	CSerial::setCTS(true); // buffer will get 'emptier', new data can be received 
	
	size_t txbuffersize = 0;
	while (!ModemBufIsEmpty(tbuf)) {
		Bit8u txval = ModemBufGetB(tbuf);
		if (sm.commandmode) {
			//if (mode == MODE_DETECT && txval != 0xa && txval != 0xd && txval != 0x8 && txval != '+')
			//{
			//	if (txval != (sm.cmdpos ? 'T' : 'A')) { NM_SwitchToNullModem(); return; }
			//	else if (sm.cmdpos) mode = MODE_SOFTMODEM; // is soft modem
			//}
			if (sm.echo) ModemBufAddB(rbuf, txval); //LOG_MSG("Echo back to queue: %x",txval);
			if      (txval == 0xa) continue; //Real modem doesn't seem to skip this?
			else if (txval == 0x1b) continue; //ESC as sent by Duke Nukem 3D before sending command
			else if (txval == 0xd) SM_DoCommand(); // newline
			else if (txval == 0x8 && sm.cmdpos > 0) --sm.cmdpos; // backspace
			else if (txval != '+' && sm.cmdpos < 99) sm.cmdbuf[sm.cmdpos++] = txval;
			//if (!sm.echo && txval == 0xd && mode == MODE_SOFTMODEM && !strcmp(sm.cmdbuf, "ATE") && tbuf.p_out < 5 && ModemBufIsEmpty(tbuf))
			//{
			//	// If the very first command was just "ATE", go back to detect mode once again
			//	// Battle Chess does this and expects communication to be up after this.
			//	mode = MODE_DETECT;
			//}
		}
		else {
			sm.plusinc = ((sm.plusinc >= 1 && sm.plusinc <= 3 && txval == sm.reg[MREG_ESCAPE_CHAR]) ? sm.plusinc + 1 : 0);
			sm.cmdpause = 0;
			tmpbuf[txbuffersize++] = txval;
		}
	}

	if (dbp_net_connected && txbuffersize) {
		dbp_net->OutgoingMtx.Lock();
		if (!dbp_net->OutgoingModem.size()) dbp_net->OutgoingModem.push_back(0); // leave space for packet id
		dbp_net->OutgoingModem.insert(dbp_net->OutgoingModem.end(), tmpbuf, tmpbuf + txbuffersize);
		dbp_net->OutgoingMtx.Unlock();
	}

	#if 0 // disabled aggressive latency reduction
	if (sm.connected)
	{
		// If the core is running in the "low latency" mode we can flush the network packets now because
		// we know the main thread is stopped in retro_run waiting for DOSBox to finish a frame.
		// This can improve performance of games that very much depend on minimal latency as long as both players do it.
		extern bool DBP_IsLowLatency();
		if (DBP_IsLowLatency()) { extern void DBP_Network_Flush(); DBP_Network_Flush(); }
	}
	#endif

	// Handle incoming to the serial port
	if (dbp_net_connected && dbp_net->IncomingModem.size())
	{
		//if (mode == MODE_DETECT) { NM_SwitchToNullModem(); return; }
		if ((!sm.commandmode || mode == MODE_DETECT) && ModemBufUnused(rbuf)) {
			Bitu recv_num = ModemBufUnused(rbuf);
			if (recv_num > 16) recv_num = 16;

			dbp_net->IncomingMtx.Lock();
			if (recv_num > dbp_net->IncomingModem.size()) recv_num = dbp_net->IncomingModem.size();
			memcpy(tmpbuf, &dbp_net->IncomingModem[0], recv_num);
			dbp_net->IncomingModem.erase(dbp_net->IncomingModem.begin(), dbp_net->IncomingModem.begin() + recv_num);
			dbp_net->IncomingMtx.Unlock();

			for (Bit8u* p = tmpbuf; recv_num--;) ModemBufAddB(rbuf, *(p++));
		}
		else if (sm.commandmode)
		{
			// have incoming data in command mode, use as incoming call
			dbp_net->IncomingMtx.Lock();
			dbp_net->IncomingModem.clear();
			dbp_net->IncomingMtx.Unlock();

			// ignore incoming data before it is clear we're a soft modem
			if (mode == MODE_SOFTMODEM) {
				if (!CSerial::getDTR()) {
					// accept no calls with DTR off; TODO: AT &Dn
					SM_SetState(false);
				} else if (!sm.ringing) {
					sm.ringing = true;
					dbp_net_msg = "Modem Incoming Call";
					SM_SendRes(MRES_RING);
					CSerial::setRI(!CSerial::getRI());
					sm.ringtimer = 3000;
					sm.reg[1] = 0; //Reset ring counter reg
				}
			}
		}
	}

	if (sm.ringing && sm.ringtimer-- <= 0) {
		sm.reg[1]++;
		if ((sm.reg[0] > 0) && (sm.reg[0] >= sm.reg[1])) {
			SM_SetState(dbp_net_connected); // answer call
			return;
		}
		SM_SendRes(MRES_RING);
		CSerial::setRI(!CSerial::getRI());
		sm.ringtimer = 2999;
	}
}

void CLibretroDualModem::NM_DoSend(Bit8u val, bool is_escape)
{
	dbp_net->OutgoingMtx.Lock();
	if (!dbp_net->OutgoingModem.size()) dbp_net->OutgoingModem.push_back(0); // leave space for packet id
	if (is_escape) dbp_net->OutgoingModem.push_back(0xff);
	dbp_net->OutgoingModem.push_back(val);
	dbp_net->OutgoingMtx.Unlock();
}

bool CLibretroDualModem::NM_DoReceive()
{
	Bit8u rxchar[2], rxcount = 0;
	grab_next_char:
	if (ModemBufIsEmpty(rbuf) && (!dbp_net_connected || !dbp_net->IncomingModem.size())) return false;

	if (ModemBufIsEmpty(rbuf))
	{
		ModemBufClear(rbuf);
		dbp_net->IncomingMtx.Lock();
		rbuf.p_out = (dbp_net->IncomingModem.size() > CModemBuf::BUFSIZE ? (Bit16u)CModemBuf::BUFSIZE : (Bit16u)dbp_net->IncomingModem.size());
		memcpy(rbuf.buf, &dbp_net->IncomingModem[0], rbuf.p_out);
		dbp_net->IncomingModem.erase(dbp_net->IncomingModem.begin(), dbp_net->IncomingModem.begin() + rbuf.p_out);
		dbp_net->IncomingMtx.Unlock();
	}

	rxchar[rxcount] = ModemBufGetB(rbuf);

	if (rxchar[0] == 0xff && !nm.transparent)
	{
		// escape char
		if (!rxcount) { rxcount = 1; goto grab_next_char; }
		if (rxchar[1] != 0xff) // 0xff 0xff -> 0xff was meant
		{
			setCTS(!!(rxchar[1] & 0x1));
			setDSR(!!(rxchar[1] & 0x2));
			if (rxchar[1] & 0x4) receiveByteEx(0x0, 0x10);
			rxcount = 0;
			goto grab_next_char; // no "payload" received
		}
	}

	//LOG_MSG("[DOSBOX] [NULLMODEM] Byte %c (%2x) to be received", ((rxchar[0] >= 32 && rxchar[0] < 127) ? rxchar[0] : ' '), rxchar[0]);
	receiveByteEx(rxchar[0], 0);
	return true;
}

#endif // C_DBP_ENABLE_LIBRETRO_MODEM

#ifdef C_DBP_ENABLE_LIBRETRO_NE2K

// Peter Grehan (grehan@iprg.nokia.com) coded all of this NE2000/ether stuff.
//
// An implementation of an ne2000 ISA ethernet adapter. This part uses
// a National Semiconductor DS-8390 ethernet MAC chip, with some h/w
// to provide a windowed memory region for the chip and a MAC address.
//

#if 0
#define BX_INFO(...)  do { LOG_MSG("[NE2K BX_INFO] " __VA_ARGS__);  } while (0)
#define BX_DEBUG(...) do { LOG_MSG("[NE2K BX_DEBUG] " __VA_ARGS__); } while (0)
#define BX_ERROR(...) do { LOG_MSG("[NE2K BX_ERROR] " __VA_ARGS__); } while (0)
#define BX_PANIC(...) do { LOG_MSG("[NE2K BX_PANIC] " __VA_ARGS__); } while (0)
#else
#define BX_INFO(...)  {}
#define BX_DEBUG(...) {}
#define BX_ERROR(...) {}
#define BX_PANIC(...) {}
#endif

struct EthernetHeader { Bit8u dest_mac[6], src_mac[6], type_and_length[2]; };

struct NE2K
{
	enum { BX_NE2K_MEMSIZ = 32*1024, BX_NE2K_MEMSTART = 16*1024, BX_NE2K_MEMEND = BX_NE2K_MEMSTART + BX_NE2K_MEMSIZ };
	static NE2K* self;
	typedef Bit8u bx_bool;
	struct bx_ne2k_t
	{
		//
		// ne2k register state
		//
		// Page 0
		//
		//  Command Register - 00h read/write
		struct CR_t {
			bx_bool  stop;		// STP - Software Reset command
			bx_bool  start;		// START - start the NIC
			bx_bool  tx_packet;	// TXP - initiate packet transmission
			Bit8u    rdma_cmd;	// RD0,RD1,RD2 - Remote DMA command
			Bit8u    pgsel;		// PS0,PS1 - Page select
		} CR;
		// Interrupt Status Register - 07h read/write
		struct ISR_t {
			bx_bool  pkt_rx;	// PRX - packet received with no errors
			bx_bool  pkt_tx;	// PTX - packet transmitted with no errors
			bx_bool  rx_err;	// RXE - packet received with 1 or more errors
			bx_bool  tx_err;	// TXE - packet tx'd       "  " "    "    "
			bx_bool  overwrite;	// OVW - rx buffer resources exhausted
			bx_bool  cnt_oflow;	// CNT - network tally counter MSB's set
			bx_bool  rdma_done;	// RDC - remote DMA complete
			bx_bool  reset;		// RST - reset status
		} ISR;
		// Interrupt Mask Register - 0fh write
		struct IMR_t {
			bx_bool  rx_inte;		// PRXE - packet rx interrupt enable
			bx_bool  tx_inte;		// PTXE - packet tx interrput enable
			bx_bool  rxerr_inte;	// RXEE - rx error interrupt enable
			bx_bool  txerr_inte;	// TXEE - tx error interrupt enable
			bx_bool  overw_inte;	// OVWE - overwrite warn int enable
			bx_bool  cofl_inte;		// CNTE - counter o'flow int enable
			bx_bool  rdma_inte;		// RDCE - remote DMA complete int enable
			bx_bool  reserved;		//  D7 - reserved
		} IMR;
		// Data Configuration Register - 0eh write
		struct DCR_t {
			bx_bool  wdsize;	// WTS - 8/16-bit select
			bx_bool  endian;	// BOS - byte-order select
			bx_bool  longaddr;	// LAS - long-address select
			bx_bool  loop;		// LS  - loopback select
			bx_bool  auto_rx;	// AR  - auto-remove rx packets with remote DMA
			Bit8u    fifo_size;	// FT0,FT1 - fifo threshold
		} DCR;
		// Transmit Configuration Register - 0dh write
		struct TCR_t {
			bx_bool  crc_disable;	// CRC - inhibit tx CRC
			Bit8u    loop_cntl;		// LB0,LB1 - loopback control
			bx_bool  ext_stoptx;    // ATD - allow tx disable by external mcast
			bx_bool  coll_prio;		// OFST - backoff algorithm select
			Bit8u    reserved;		//  D5,D6,D7 - reserved
		} TCR;
		// Transmit Status Register - 04h read
		struct TSR_t {
			bx_bool  tx_ok;			// PTX - tx complete without error
			bx_bool  reserved;		//  D1 - reserved
			bx_bool  collided;		// COL - tx collided >= 1 times
			bx_bool  aborted;		// ABT - aborted due to excessive collisions
			bx_bool  no_carrier;	// CRS - carrier-sense lost
			bx_bool  fifo_ur;		// FU  - FIFO underrun
			bx_bool  cd_hbeat;		// CDH - no tx cd-heartbeat from transceiver
			bx_bool  ow_coll;		// OWC - out-of-window collision
		} TSR;
		// Receive Configuration Register - 0ch write
		struct RCR_t {
			bx_bool  errors_ok;	// SEP - accept pkts with rx errors
			bx_bool  runts_ok;	// AR  - accept < 64-byte runts
			bx_bool  broadcast;	// AB  - accept eth broadcast address
			bx_bool  multicast;	// AM  - check mcast hash array
			bx_bool  promisc;	// PRO - accept all packets
			bx_bool  monitor;	// MON - check pkts, but don't rx
			Bit8u    reserved;	//  D6,D7 - reserved
		} RCR;
		// Receive Status Register - 0ch read
		struct RSR_t {
			bx_bool  rx_ok;			// PRX - rx complete without error
			bx_bool  bad_crc;		// CRC - Bad CRC detected
			bx_bool  bad_falign;	// FAE - frame alignment error
			bx_bool  fifo_or;		// FO  - FIFO overrun
			bx_bool  rx_missed;		// MPA - missed packet error
			bx_bool  rx_mbit;		// PHY - unicast or mcast/bcast address match
			bx_bool  rx_disabled;   // DIS - set when in monitor mode
			bx_bool  deferred;		// DFR - collision active
		} RSR;
		Bit16u local_dma;		// 01,02h read ; current local DMA addr
		Bit8u  page_start;	// 01h write ; page start register
		Bit8u  page_stop;		// 02h write ; page stop register
		Bit8u  bound_ptr;		// 03h read/write ; boundary pointer
		Bit8u  tx_page_start;	// 04h write ; transmit page start register
		Bit8u  num_coll;		// 05h read  ; number-of-collisions register
		Bit16u tx_bytes;		// 05,06h write ; transmit byte-count register
		Bit8u  fifo;			// 06h read  ; FIFO
		Bit16u remote_dma;	// 08,09h read ; current remote DMA addr
		Bit16u remote_start;  // 08,09h write ; remote start address register
		Bit16u remote_bytes;  // 0a,0bh write ; remote byte-count register
		Bit8u  tallycnt_0;	// 0dh read  ; tally counter 0 (frame align errors)
		Bit8u  tallycnt_1;	// 0eh read  ; tally counter 1 (CRC errors)
		Bit8u  tallycnt_2;	// 0fh read  ; tally counter 2 (missed pkt errors)
		//
		// Page 1
		//
		// Command Register 00h (repeated)
		Bit8u  physaddr[6];	// 01-06h read/write ; MAC address
		Bit8u  curr_page;	// 07h read/write ; current page register
		Bit8u  mchash[8];	// 08-0fh read/write ; multicast hash array
		//
		// Page 2  - diagnostic use only
		//
		//   Command Register 00h (repeated)
		//
		//   Page Start Register 01h read  (repeated)
		//   Page Stop Register  02h read  (repeated)
		//   Current Local DMA Address 01,02h write (repeated)
		//   Transmit Page start address 04h read (repeated)
		//   Receive Configuration Register 0ch read (repeated)
		//   Transmit Configuration Register 0dh read (repeated)
		//   Data Configuration Register 0eh read (repeated)
		//   Interrupt Mask Register 0fh read (repeated)
		//
		Bit8u  rempkt_ptr;		// 03h read/write ; remote next-packet pointer
		Bit8u  localpkt_ptr;	// 05h read/write ; local next-packet pointer
		Bit16u address_cnt;		// 06,07h read/write ; address counter
		//
		// Page 3  - should never be modified.
		//
		// Novell ASIC state
		Bit8u  macaddr[32];			// ASIC ROM'd MAC address, even bytes
		Bit8u  mem[BX_NE2K_MEMSIZ];	// on-chip packet memory
		// ne2k internal state
		Bit32u base_address;
		int base_irq, tx_timer_active;
	} s;

	IO_ReadHandleObject ReadHandler;
	IO_WriteHandleObject WriteHandler;

	NE2K()
	{
		s.base_address = (Bit32u)0x300;
		s.base_irq = (int)10;
		init_mac();

		//BX_DEBUG(("Init $Id: ne2k.cc,v 1.56.2.1 2004/02/02 22:37:22 cbothamy Exp $"));
		BX_INFO("port 0x%x/32 irq %d mac %02x:%02x:%02x:%02x:%02x:%02x",
				(unsigned int)(s.base_address), (int)(s.base_irq),
				s.physaddr[0], s.physaddr[1], s.physaddr[2], s.physaddr[3], s.physaddr[4], s.physaddr[5]);

		// Bring the register state into power-up state
		reset();

		// install I/O-handlers and timer
		ReadHandler.Install(s.base_address, dosbox_read_handler, IO_MB|IO_MW, 0x20);
		WriteHandler.Install(s.base_address, dosbox_write_handler, IO_MB|IO_MW, 0x20);
		TIMER_AddTickHandler(dosbox_tick_handler);
	}

	~NE2K() {
		TIMER_DelTickHandler(dosbox_tick_handler);
		PIC_RemoveEvents(dosbox_tx_event);
	}

	void init_mac()
	{
		if (dbp_net_addr.mac[0] != DBP_Net::FIRST_MAC_OCTET) DBP_Net_InitMac(); // force init, though might change later (which would probably confuse an OS)

		memcpy(s.physaddr, dbp_net_addr.mac, 6);

		// Initialise the mac address area by doubling the physical address
		for (Bitu i = 0; i < 12; i++)
			s.macaddr[i] = s.physaddr[i>>1];

		// ne2k signature
		for (Bitu i = 12; i < 32; i++)
			s.macaddr[i] = 0x57;
	}

	// reset - restore state to power-up, cancelling all i/o
	void reset()
	{
		BX_DEBUG("reset");
		// Zero out registers and memory
		memset(&s.CR,  0, sizeof(s.CR) );
		memset(&s.ISR, 0, sizeof(s.ISR));
		memset(&s.IMR, 0, sizeof(s.IMR));
		memset(&s.DCR, 0, sizeof(s.DCR));
		memset(&s.TCR, 0, sizeof(s.TCR));
		memset(&s.TSR, 0, sizeof(s.TSR));
		//memset(&s.RCR, 0, sizeof(s.RCR));
		memset(&s.RSR, 0, sizeof(s.RSR));
		s.tx_timer_active = 0;
		s.local_dma  = 0;
		s.page_start = 0;
		s.page_stop  = 0;
		s.bound_ptr  = 0;
		s.tx_page_start = 0;
		s.num_coll   = 0;
		s.tx_bytes   = 0;
		s.fifo       = 0;
		s.remote_dma = 0;
		s.remote_start = 0;
		s.remote_bytes = 0;
		s.tallycnt_0 = 0;
		s.tallycnt_1 = 0;
		s.tallycnt_2 = 0;

		//memset(&s.physaddr, 0, sizeof(s.physaddr));
		//memset(&s.mchash, 0, sizeof(s.mchash));
		s.curr_page = 0;

		s.rempkt_ptr   = 0;
		s.localpkt_ptr = 0;
		s.address_cnt  = 0;
		memset(&s.mem, 0, sizeof(s.mem));

		// Set power-up conditions
		s.CR.stop      = 1;
		s.CR.rdma_cmd  = 4;
		s.ISR.reset    = 1;
		s.DCR.longaddr = 1;
		PIC_DeActivateIRQ((unsigned int)s.base_irq);
	}

	// read_cr/write_cr - utility routines for handling reads/writes to the Command Register
	Bit32u read_cr(void)
	{
		Bit32u val =
			(((unsigned int)(s.CR.pgsel    & 0x03u) << 6u) |
			 ((unsigned int)(s.CR.rdma_cmd & 0x07u) << 3u) |
			  (unsigned int)(s.CR.tx_packet << 2u) |
			  (unsigned int)(s.CR.start     << 1u) |
			  (unsigned int)(s.CR.stop));
		BX_DEBUG("read CR returns 0x%08x", val);
		return val;
	}
	void write_cr(Bit32u value)
	{
		BX_DEBUG("wrote 0x%02x to CR", value);

		// Validate remote-DMA
		if ((value & 0x38) == 0x00) {
			BX_DEBUG("CR write - invalid rDMA value 0");
			value |= 0x20; /* dma_cmd == 4 is a safe default */
			//value = 0x22; /* dma_cmd == 4 is a safe default */
		}

		// Check for s/w reset
		if (value & 0x01) {
			s.ISR.reset = 1;
			s.CR.stop   = 1;
		} else {
			s.CR.stop = 0;
		}

		s.CR.rdma_cmd = (value & 0x38) >> 3;

		// If start command issued, the RST bit in the ISR must be cleared
		if ((value & 0x02) && !s.CR.start)
			s.ISR.reset = 0;

		s.CR.start = ((value & 0x02) == 0x02);
		s.CR.pgsel = (value & 0xc0) >> 6;

		// Check for send-packet command
		if (s.CR.rdma_cmd == 3) {
			// Set up DMA read from receive ring
			s.remote_start =s.remote_dma = s.bound_ptr * 256;
			s.remote_bytes = *((Bit16u*) & s.mem[s.bound_ptr * 256 + 2 - BX_NE2K_MEMSTART]);
			BX_INFO("Sending buffer #x%x length %d", s.remote_start, s.remote_bytes);
		}

		// Check for start-tx
		if ((value & 0x04) && s.TCR.loop_cntl)
		{
			// loopback mode
			if (s.TCR.loop_cntl != 1) {
				BX_INFO("Loop mode %d not supported.",s.TCR.loop_cntl);
			} else {
				rx_frame (&s.mem[s.tx_page_start*256 - BX_NE2K_MEMSTART], s.tx_bytes);

				// do a TX interrupt
				// Generate an interrupt if not masked and not one in progress
				if (s.IMR.tx_inte && !s.ISR.pkt_tx) {
					//LOG_MSG("tx complete interrupt");
					PIC_ActivateIRQ((unsigned int)s.base_irq);
				}
				s.ISR.pkt_tx = 1;
			}
		}
		else if (value & 0x04)
		{
			// start-tx and no loopback
			if (s.CR.stop || !s.CR.start)
				{ BX_PANIC("CR write - tx start, dev in reset"); }

			if (s.tx_bytes == 0)
				{ BX_PANIC("CR write - tx start, tx bytes == 0"); }

			#if 0 // debug stuff
			printf("packet tx (%d bytes):\t",s.tx_bytes);
			for (int i = 0; i <s.tx_bytes; i++) {
				printf("%02x ",s.mem[s.tx_page_start*256 - BX_NE2K_MEMSTART + i]);
				if (i && (((i+1) % 16) == 0)) printf("\t");
			}
			printf("");
			#endif

			// Send the packet to the system driver
			if (dbp_net_connected) {
				BX_INFO("Packet sent: size: %d", s.tx_bytes);
				dbp_net->OutgoingMtx.Lock();
				dbp_net->OutgoingPackets.resize(dbp_net->OutgoingPackets.size() + 3 + s.tx_bytes);
				Bit8u* p = &dbp_net->OutgoingPackets[dbp_net->OutgoingPackets.size() - 3 - s.tx_bytes];
				NET_WRITE_LE16(p, s.tx_bytes);
				p[2] = DBP_Net::PKT_NE2K;
				memcpy(p + 3, &s.mem[s.tx_page_start*256 - BX_NE2K_MEMSTART], s.tx_bytes);
				dbp_net->OutgoingMtx.Unlock();
			}

			// Trigger any pending timers
			if (s.tx_timer_active) {
				dosbox_tx_event(0);
				PIC_RemoveEvents(dosbox_tx_event);
			}

			//LOG_MSG("send packet command");
			//s.tx_timer_index = (64 + 96 + 4*8 +s.tx_bytes*8)/10;
			s.tx_timer_active = 1;
			PIC_AddEvent(dosbox_tx_event,(float)((64 + 96 + 4*8 +s.tx_bytes*8)/20000.0),0);

			// Schedule a timer to trigger a tx-complete interrupt
			// The number of microseconds is the bit-time / 10.
			// The bit-time is the preamble+sfd (64 bits), the
			// inter-frame gap (96 bits), the CRC (4 bytes), and the
			// the number of bits in the frame (s.tx_bytes * 8).
			/* TODO: Code transmit timer */
			//bx_pc_system.activate_timer(s.tx_timer_index, (64 + 96 + 4*8 +s.tx_bytes*8)/10, 0); // not continuous

			// Simplistic detection of DHCP discover/request packets and respond with an offer/ack containing an IP address generated from the client id (enough for Win 9x)
			struct DHCPPacket
			{
				Bit8u eth_dest_mac[6], eth_src_mac[6], eth_type_and_length[2],
					ip_ihl_ver, ip_tos, ip_len[2], ip_ident[2], ip_offset[2], ip_ttl, ip_protocol, ip_hdrchksum[2], ip_source[4], ip_dest[4],
					udp_srcport[2], udp_destport[2], udp_len[2], udp_chksum[2],
					dhcp_bootmsgtype, dhcp_hwtype, dhcp_adrlen, dhcp_hops, dhcp_txid[4], dhcp_time[2], dhcp_flags[2], dhcp_curip[4], dhcp_newip[4], dhcp_srvip[4], dhcp_relay[4],
					dhcp_clientmac[6], dhcp_clientmacpadding[10], dhcp_srvname[64], dhcp_bootfile[128], dhcp_cookie[4], dhcp_opt1[3];
			};
			const DHCPPacket& inp = *(const DHCPPacket*)&s.mem[s.tx_page_start*256 - BX_NE2K_MEMSTART];
			if (s.tx_bytes >= sizeof(DHCPPacket)
				&& inp.dhcp_cookie[0] == 0x63 && inp.dhcp_cookie[1] == 0x82 && inp.dhcp_cookie[2] == 0x53 && inp.dhcp_cookie[3] == 0x63 && inp.dhcp_bootmsgtype == 1
				&& inp.eth_dest_mac[0] == 0xFF && inp.eth_type_and_length[0] == 0x8 && inp.ip_protocol == 17 && inp.udp_srcport[1] == 68 && inp.udp_destport[1] == 67
				&& s.DCR.loop != 0 && s.TCR.loop_cntl == 0) // don't receive in loopback modes
			{
				struct Local { static void WriteChkSum(const Bit8u* p, const Bit8u* pEnd, Bit8u* res, Bit32u sum = 0)
				{
					Bit8u x = (Bit8u)(res - p) & 1; sum -= res[x^1] + (res[x] << 8); // negate existing checksum
					for (Bit8u f = 255; p != pEnd; f = ~f) sum += (*p++) * ((int)f + 1); // sum bytes
					sum = ((sum >> 16) + (sum & 0x0000ffff)); sum += (sum >> 16); sum = ~sum; // ones-complement
					NET_WRITE_BE16(res, sum);
				}};

				Bit8u outbuf[590] = {0};
				DHCPPacket& outp = *(DHCPPacket*)outbuf;
				memcpy(&outp, &inp, sizeof(DHCPPacket));

				// Validate check sum code with incoming checksums
				DBP_ASSERT((Local::WriteChkSum(&inp.ip_ihl_ver, inp.udp_srcport,               (Bit8u*)inp.ip_hdrchksum                                                          ), outp.ip_hdrchksum[0] == inp.ip_hdrchksum[0] && outp.ip_hdrchksum[1] == inp.ip_hdrchksum[1]));
				DBP_ASSERT((Local::WriteChkSum(inp.ip_source,   inp.eth_dest_mac + s.tx_bytes, (Bit8u*)inp.udp_chksum,    17 + s.tx_bytes - (int)(inp.udp_srcport - (Bit8u*)&inp)), outp.udp_chksum[0]   == inp.udp_chksum[0]   && outp.udp_chksum[1]   == inp.udp_chksum[1]  ));

				// Write response IP header fields
				outp.eth_src_mac[4] = outp.eth_src_mac[5] = 0xFE; // fake DHCP servers mac address
				NET_WRITE_BE16(outp.ip_len, (sizeof(outbuf) - (&outp.ip_ihl_ver - (Bit8u*)&outp)));
				outp.ip_ident[0] = outp.ip_ident[1] = 0;
				memcpy(outp.ip_source, "\xc0\xa8\xfe\xfe", 4); // 192.168.254.254
				Local::WriteChkSum(&outp.ip_ihl_ver, outp.udp_srcport, (Bit8u*)outp.ip_hdrchksum);

				// Write response UDP header fields
				outp.udp_srcport[1] = 67;
				outp.udp_destport[1] = 68;
				NET_WRITE_BE16(outp.udp_len, (sizeof(outbuf) - (outp.udp_srcport - (Bit8u*)&outp)));

				// Write response DHCP fields
				DBP_ASSERT(inp.dhcp_opt1[0] == 53 && inp.dhcp_opt1[1] == 1 && (inp.dhcp_opt1[2] == 1 || inp.dhcp_opt1[2] == 3)); // expect DHCP Message Type Discover or Request
				Bit16u client_id = NET_READ_BE16(&dbp_net_addr.mac[4]);
				outp.dhcp_bootmsgtype = 2; // Boot Reply
				memcpy(outp.dhcp_srvip, "\xc0\xa8\xfe\xfe", 4); // 192.168.254.254
				memcpy(outp.dhcp_newip, "\xc0\xa8\xfe\xfe", 2); // 192.168.
				outp.dhcp_newip[2] = client_id / 253; outp.dhcp_newip[3] = 1 + (client_id % 253); // fill in last two digits
				outp.dhcp_opt1[2] = (inp.dhcp_opt1[2] == 1 ? 2 : 5); // Discover => Offer, Request => Ack (DORA)
				memcpy(&outp + 1, // fixed remaining options
					"\x36\x04\xc0\xa8\xfe\xfe" // Option: (54) DHCP Server Identifier (192.168.254.254)
					"\x33\x04\x7f\xff\xff\xff" // Option: (51) IP Address Lease Time
					"\x01\x04\xff\xff\x00\x00" // Option: (1) Subnet Mask (255.255.0.0)
					"\x03\x04\xc0\xa8\xfe\xfe" // Option: (3) Router
					"\x06\x04\xc0\xa8\xfe\xfe" // Option: (6) Domain Name Server
					"\xff", 31);               // Option: (255) End

				// Calculate UDP checksum and directly receive DHCP response packet
				Local::WriteChkSum(outp.ip_source, outp.eth_dest_mac + sizeof(outbuf), outp.udp_chksum, 17 + sizeof(outbuf) - (int)(inp.udp_srcport - (Bit8u*)&inp));
				rx_frame(outbuf, sizeof(outbuf));
			}
		} // end transmit-start branch

		// Linux probes for an interrupt by setting up a remote-DMA read
		// of 0 bytes with remote-DMA completion interrupts enabled.
		// Detect this here
		if (s.CR.rdma_cmd == 0x01 && s.CR.start && s.remote_bytes == 0) {
			s.ISR.rdma_done = 1;
			if (s.IMR.rdma_inte) {
				PIC_ActivateIRQ((unsigned int)s.base_irq);
			}
		}
	}

	// chipmem_read/chipmem_write - access the 64K private RAM. The ne2000 memory is accessed through the data port of
	// the asic (offset 0) after setting up a remote-DMA transfer. Both byte and word accesses are allowed.
	// The first 16 bytes contains the MAC address at even locations, and there is 16K of buffer memory starting at 16K
	Bit32u chipmem_read(Bit32u address, unsigned int io_len)
	{
		Bit32u retval = 0;

		if ((io_len == 2) && (address & 0x1))
			{ BX_PANIC("unaligned chipmem word read"); }

		// ROM'd MAC address
		if (/*(address >=0) && */address <= 31) {
		retval =s.macaddr[address];
			if ((io_len == 2u) || (io_len == 4u)) {
				retval |= (unsigned int)(s.macaddr[address + 1u] << 8u);
				if (io_len == 4u) {
					retval |= (unsigned int)(s.macaddr[address + 2u] << 16u);
					retval |= (unsigned int)(s.macaddr[address + 3u] << 24u);
				}
			}
			return (retval);
		}

		if ((address >= BX_NE2K_MEMSTART) && (address < BX_NE2K_MEMEND)) {
			retval =s.mem[address - BX_NE2K_MEMSTART];
			if ((io_len == 2u) || (io_len == 4u)) {
				retval |= (unsigned int)(s.mem[address - BX_NE2K_MEMSTART + 1] << 8u);
			}
			if (io_len == 4u) {
				retval |= (unsigned int)(s.mem[address - BX_NE2K_MEMSTART + 2] << 16u);
				retval |= (unsigned int)(s.mem[address - BX_NE2K_MEMSTART + 3] << 24u);
			}
			return (retval);
		}

		BX_DEBUG("out-of-bounds chipmem read, %04X", address);

		return (0xff);
	}

	void chipmem_write(Bit32u address, Bit32u value, unsigned io_len)
	{
		if ((io_len == 2) && (address & 0x1))
			{ BX_PANIC("unaligned chipmem word write"); }

		if ((address >= BX_NE2K_MEMSTART) && (address < BX_NE2K_MEMEND)) {
			s.mem[address - BX_NE2K_MEMSTART] = value & 0xff;
			if (io_len == 2)
				s.mem[address - BX_NE2K_MEMSTART + 1] = value >> 8;
		} else { BX_DEBUG("out-of-bounds chipmem write, %04X", address); }
	}

	// asic_read/asic_write - This is the high 16 bytes of i/o space (the lower 16 bytes is for the DS8390). Only two locations
	// are used: offset 0, which is used for data transfer, and offset 0xf, which is used to reset the device.
	// The data transfer port is used to as 'external' DMA to the DS8390. The chip has to have the DMA registers set up, and
	// after that, insw/outsw instructions can be used to move the appropriate number of bytes to/from the device.
	Bit32u asic_read(Bit32u offset, unsigned int io_len)
	{
		Bit32u retval = 0;
		switch (offset) {
			case 0x0: // Data register
				// A read remote-DMA command must have been issued, and the source-address and length registers must have been initialised.
				if (io_len >s.remote_bytes) {
					BX_ERROR("ne2K: dma read underrun iolen=%d remote_bytes=%d",io_len,s.remote_bytes);
					//return 0;
				}

				//BX_INFO(("ne2k read DMA: addr=%4x remote_bytes=%d",s.remote_dma,s.remote_bytes));
				retval = chipmem_read(s.remote_dma, io_len);
				//
				// The 8390 bumps the address and decreases the byte count
				// by the selected word size after every access, not by
				// the amount of data requested by the host (io_len).
				//
				s.remote_dma += (s.DCR.wdsize + 1);
				if (s.remote_dma ==s.page_stop << 8) {
					s.remote_dma =s.page_start << 8;
				}
				// keep s.remote_bytes from underflowing
				if (s.remote_bytes > 1)
					s.remote_bytes -= (s.DCR.wdsize + 1);
				else
					s.remote_bytes = 0;

				// If all bytes have been written, signal remote-DMA complete
				if (s.remote_bytes == 0) {
					s.ISR.rdma_done = 1;
					if (s.IMR.rdma_inte) {
						PIC_ActivateIRQ((unsigned int)s.base_irq);
					}
				}
				break;

			case 0xf: // Reset register
				reset();
				//retval=0x1;
				break;

			default:
				BX_INFO("asic read invalid address %04x", (unsigned) offset);
				break;
		}
		return (retval);
	}

	void asic_write(Bit32u offset, Bit32u value, unsigned io_len)
	{
		BX_DEBUG("asic write addr=0x%02x, value=0x%04x", (unsigned) offset, (unsigned) value);
		switch (offset) {
			case 0x0: // Data register - see asic_read for a description
				if ((io_len == 2) && (s.DCR.wdsize == 0)) {
					BX_PANIC("dma write length 2 on byte mode operation");
					break;
				}

				if (s.remote_bytes == 0)
					{ BX_PANIC("ne2K: dma write, byte count 0"); }

				chipmem_write(s.remote_dma, value, io_len);
				// is this right ??? asic_read uses DCR.wordsize
				s.remote_dma   += io_len;
				if (s.remote_dma == s.page_stop << 8)
					s.remote_dma = s.page_start << 8;

				s.remote_bytes -= io_len;
				if (s.remote_bytes > BX_NE2K_MEMSIZ)
					s.remote_bytes = 0;

				// If all bytes have been written, signal remote-DMA complete
				if (s.remote_bytes == 0) {
					s.ISR.rdma_done = 1;
					if (s.IMR.rdma_inte) {
						PIC_ActivateIRQ((unsigned int)s.base_irq);
					}
				}
				break;

			case 0xf: // Reset register
				reset();
				break;

			default: // this is invalid, but happens under win95 device detection
				BX_INFO("asic write invalid address %04x, ignoring", (unsigned) offset);
				break;
		}
	}

	// page0_read/page0_write - These routines handle reads/writes to
	// the 'zeroth' page of the DS8390 register file
	Bit32u page0_read(Bit32u offset, unsigned int io_len)
	{
		BX_DEBUG("page 0 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len);
		if (io_len > 1) {
			BX_ERROR("bad length! page 0 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len); /* encountered with win98 hardware probe */
			return 0;
		}

		switch (offset) {
			case 0x1: // CLDA0
				return (s.local_dma & 0xff);

			case 0x2: // CLDA1
				return (unsigned int)(s.local_dma >> 8u);

			case 0x3: // BNRY
				return (s.bound_ptr);

			case 0x4: // TSR
				return
					((unsigned int)(s.TSR.ow_coll    << 7u) |
					 (unsigned int)(s.TSR.cd_hbeat   << 6u) |
					 (unsigned int)(s.TSR.fifo_ur    << 5u) |
					 (unsigned int)(s.TSR.no_carrier << 4u) |
					 (unsigned int)(s.TSR.aborted    << 3u) |
					 (unsigned int)(s.TSR.collided   << 2u) |
					 (unsigned int)(s.TSR.tx_ok));

			case 0x5: // NCR
				return (s.num_coll);

			case 0x6: // FIFO
				// reading FIFO is only valid in loopback mode
				BX_ERROR("reading FIFO not supported yet");
				return (s.fifo);

			case 0x7: // ISR
				return
					((unsigned int)(s.ISR.reset     << 7u) |
					(unsigned int)(s.ISR.rdma_done << 6u) |
					(unsigned int)(s.ISR.cnt_oflow << 5u) |
					(unsigned int)(s.ISR.overwrite << 4u) |
					(unsigned int)(s.ISR.tx_err    << 3u) |
					(unsigned int)(s.ISR.rx_err    << 2u) |
					(unsigned int)(s.ISR.pkt_tx    << 1u) |
					(unsigned int)(s.ISR.pkt_rx));

			case 0x8: // CRDA0
				return (s.remote_dma & 0xff);

			case 0x9: // CRDA1
				return (unsigned int)(s.remote_dma >> 8u);

			case 0xa: // reserved
				BX_INFO("reserved read - page 0, 0xa");
				return (0xff);

			case 0xb: // reserved
				BX_INFO("reserved read - page 0, 0xb");
				return (0xff);

			case 0xc: // RSR
				return
					((unsigned int)(s.RSR.deferred    << 7u) |
					(unsigned int)(s.RSR.rx_disabled << 6u) |
					(unsigned int)(s.RSR.rx_mbit     << 5u) |
					(unsigned int)(s.RSR.rx_missed   << 4u) |
					(unsigned int)(s.RSR.fifo_or     << 3u) |
					(unsigned int)(s.RSR.bad_falign  << 2u) |
					(unsigned int)(s.RSR.bad_crc     << 1u) |
					(unsigned int)(s.RSR.rx_ok));

			case 0xd: // CNTR0
				return (s.tallycnt_0);

			case 0xe: // CNTR1
				return (s.tallycnt_1);

			case 0xf: // CNTR2
				return (s.tallycnt_2);

			default:
				BX_PANIC("page 0 offset %04x out of range", (unsigned) offset);
		}
		return(0);
	}

	void page0_write(Bit32u offset, Bit32u value, unsigned io_len)
	{
		BX_DEBUG("page 0 write to port %04x, len=%u", (unsigned) offset, (unsigned) io_len);

		// It appears to be a common practice to use outw on page0 regs...
		// break up outw into two outb's
		if (io_len == 2) {
			page0_write(offset, (value & 0xff), 1);
			page0_write(offset + 1, ((value >> 8) & 0xff), 1);
			return;
		}

		switch (offset) {
			case 0x1: // PSTART
				s.page_start = value;
				break;

			case 0x2: // PSTOP
				// BX_INFO(("Writing to PSTOP: %02x", value));
				s.page_stop = value;
				break;

			case 0x3: // BNRY
				s.bound_ptr = value;
				break;

			case 0x4: // TPSR
				s.tx_page_start = value;
				break;

			case 0x5: // TBCR0
				// Clear out low byte and re-insert
				s.tx_bytes &= 0xff00;
				s.tx_bytes |= (value & 0xff);
				break;

			case 0x6: // TBCR1
				// Clear out high byte and re-insert
				s.tx_bytes &= 0x00ff;
				s.tx_bytes |= ((value & 0xff) << 8);
				break;

			case 0x7: // ISR
				value &= 0x7f;  // clear RST bit - status-only bit
				// All other values are cleared iff the ISR bit is 1
				s.ISR.pkt_rx    &= ~((bx_bool)((value & 0x01) == 0x01));
				s.ISR.pkt_tx    &= ~((bx_bool)((value & 0x02) == 0x02));
				s.ISR.rx_err    &= ~((bx_bool)((value & 0x04) == 0x04));
				s.ISR.tx_err    &= ~((bx_bool)((value & 0x08) == 0x08));
				s.ISR.overwrite &= ~((bx_bool)((value & 0x10) == 0x10));
				s.ISR.cnt_oflow &= ~((bx_bool)((value & 0x20) == 0x20));
				s.ISR.rdma_done &= ~((bx_bool)((value & 0x40) == 0x40));
				value = ((unsigned int)(s.ISR.rdma_done << 6u) |
					(unsigned int)(s.ISR.cnt_oflow << 5u) |
					(unsigned int)(s.ISR.overwrite << 4u) |
					(unsigned int)(s.ISR.tx_err    << 3u) |
					(unsigned int)(s.ISR.rx_err    << 2u) |
					(unsigned int)(s.ISR.pkt_tx    << 1u) |
					(unsigned int)(s.ISR.pkt_rx));
				value &= ((unsigned int)(s.IMR.rdma_inte  << 6u) |
					(unsigned int)(s.IMR.cofl_inte  << 5u) |
					(unsigned int)(s.IMR.overw_inte << 4u) |
					(unsigned int)(s.IMR.txerr_inte << 3u) |
					(unsigned int)(s.IMR.rxerr_inte << 2u) |
					(unsigned int)(s.IMR.tx_inte    << 1u) |
					(unsigned int)(s.IMR.rx_inte));
				if (value == 0) {
					PIC_DeActivateIRQ((unsigned int)s.base_irq);
				}
				break;

			case 0x8: // RSAR0
				// Clear out low byte and re-insert
				s.remote_start &= 0xff00u;
				s.remote_start |= (value & 0xffu);
				s.remote_dma =s.remote_start;
				break;

			case 0x9: // RSAR1
				// Clear out high byte and re-insert
				s.remote_start &= 0x00ffu;
				s.remote_start |= ((value & 0xffu) << 8u);
				s.remote_dma =s.remote_start;
				break;

			case 0xa: // RBCR0
				// Clear out low byte and re-insert
				s.remote_bytes &= 0xff00u;
				s.remote_bytes |= (value & 0xffu);
				break;

			case 0xb: // RBCR1
				// Clear out high byte and re-insert
				s.remote_bytes &= 0x00ffu;
				s.remote_bytes |= ((value & 0xffu) << 8u);
				break;

			case 0xc: // RCR
				// Check if the reserved bits are set
				if (value & 0xc0)
					{ BX_INFO("RCR write, reserved bits set"); }

				// Set all other bit-fields
				s.RCR.errors_ok = ((value & 0x01u) == 0x01u);
				s.RCR.runts_ok  = ((value & 0x02u) == 0x02u);
				s.RCR.broadcast = ((value & 0x04u) == 0x04u);
				s.RCR.multicast = ((value & 0x08u) == 0x08u);
				s.RCR.promisc   = ((value & 0x10u) == 0x10u);
				s.RCR.monitor   = ((value & 0x20u) == 0x20u);

				// Monitor bit is a little suspicious...
				if (value & 0x20)
					{ BX_INFO("RCR write, monitor bit set!"); }
				break;

			case 0xd: // TCR
				// Check reserved bits
				if (value & 0xe0)
					{ BX_ERROR("TCR write, reserved bits set"); }

				// Test loop mode (not supported)
				if (value & 0x06) {
					s.TCR.loop_cntl = (value & 0x6) >> 1;
					BX_INFO("TCR write, loop mode %d not supported",s.TCR.loop_cntl);
				} else {
					s.TCR.loop_cntl = 0;
				}

				// Inhibit-CRC not supported.
				if (value & 0x01)
					{ BX_PANIC("TCR write, inhibit-CRC not supported"); }

				// Auto-transmit disable very suspicious
				if (value & 0x08)
					{ BX_PANIC("TCR write, auto transmit disable not supported"); }

				// Allow collision-offset to be set, although not used
				s.TCR.coll_prio = ((value & 0x08) == 0x08);
				break;

			case 0xe: // DCR
				// the loopback mode is not supported yet
				if (!(value & 0x08)) {
					BX_ERROR("DCR write, loopback mode selected");
				}
				// It is questionable to set longaddr and auto_rx, since they
				// aren't supported on the ne2000. Print a warning and continue
				if (value & 0x04)
					{ BX_INFO("DCR write - LAS set ???"); }
				if (value & 0x10)
					{ BX_INFO("DCR write - AR set ???"); }

				// Set other values.
				s.DCR.wdsize   = ((value & 0x01) == 0x01);
				s.DCR.endian   = ((value & 0x02) == 0x02);
				s.DCR.longaddr = ((value & 0x04) == 0x04); // illegal ?
				s.DCR.loop     = ((value & 0x08) == 0x08);
				s.DCR.auto_rx  = ((value & 0x10) == 0x10); // also illegal ?
				s.DCR.fifo_size = (value & 0x50) >> 5;
				break;

			case 0xf: // IMR
				// Check for reserved bit
				if (value & 0x80)
					{ BX_ERROR("IMR write, reserved bit set"); }

				// Set other values
				s.IMR.rx_inte    = ((value & 0x01) == 0x01);
				s.IMR.tx_inte    = ((value & 0x02) == 0x02);
				s.IMR.rxerr_inte = ((value & 0x04) == 0x04);
				s.IMR.txerr_inte = ((value & 0x08) == 0x08);
				s.IMR.overw_inte = ((value & 0x10) == 0x10);
				s.IMR.cofl_inte  = ((value & 0x20) == 0x20);
				s.IMR.rdma_inte  = ((value & 0x40) == 0x40);
				if(s.ISR.pkt_tx &&s.IMR.tx_inte) {
					LOG_MSG("tx irq retrigger");
					PIC_ActivateIRQ((unsigned int)s.base_irq);
				}
				break;

			default:
				BX_PANIC("page 0 write, bad offset %0x", offset);
		}
	}

	// page1_read/page1_write - These routines handle reads/writes to
	// the first page of the DS8390 register file
	Bit32u page1_read(Bit32u offset, unsigned int io_len)
	{
		BX_DEBUG("page 1 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len);
		if (io_len > 1)
			{ BX_PANIC("bad length! page 1 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len); }

		switch (offset) {
			case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: // PAR0-5
				return (s.physaddr[offset - 1]);
				break;

			case 0x7: // CURR
				BX_DEBUG("returning current page: %02x", (s.curr_page));
				return (s.curr_page);

			case 0x8: case 0x9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: // MAR0-7
				return (s.mchash[offset - 8]);

			default:
				BX_PANIC("page 1 r offset %04x out of range", (unsigned) offset);
		}
		return (0);
	}

	void page1_write(Bit32u offset, Bit32u value, unsigned io_len)
	{
		(void)io_len;//UNUSED
		BX_DEBUG("page 1 w offset %04x", (unsigned) offset);
		switch (offset) {
			case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: // PAR0-5
				s.physaddr[offset - 1] = value;
				break;

			case 0x7: // CURR
				s.curr_page = value;
				break;

			case 0x8: case 0x9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: // MAR0-7
				s.mchash[offset - 8] = value;
				break;

			default:
				BX_PANIC("page 1 w offset %04x out of range", (unsigned) offset);
		}
	}

	// page2_read/page2_write - These routines handle reads/writes to
	// the second page of the DS8390 register file
	Bit32u page2_read(Bit32u offset, unsigned int io_len)
	{
		BX_DEBUG("page 2 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len);

		if (io_len > 1)
			{ BX_PANIC("bad length!  page 2 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len); }

		switch (offset) {
			case 0x1: // PSTART
				return (s.page_start);

			case 0x2: // PSTOP
				return (s.page_stop);

			case 0x3: // Remote Next-packet pointer
				return (s.rempkt_ptr);

			case 0x4: // TPSR
				return (s.tx_page_start);

			case 0x5: // Local Next-packet pointer
				return (s.localpkt_ptr);

			case 0x6: // Address counter (upper)
				return (unsigned int)(s.address_cnt >> 8u);

			case 0x7: // Address counter (lower)
				return (unsigned int)(s.address_cnt & 0xff);

			case 0x8: case 0x9: case 0xa: case 0xb: // Reserved
				BX_ERROR("reserved read - page 2, 0x%02x", (unsigned) offset);
				return (0xff);

			case 0xc: // RCR
				return
					((unsigned int)(s.RCR.monitor   << 5u) |
					 (unsigned int)(s.RCR.promisc   << 4u) |
					 (unsigned int)(s.RCR.multicast << 3u) |
					 (unsigned int)(s.RCR.broadcast << 2u) |
					 (unsigned int)(s.RCR.runts_ok  << 1u) |
					 (unsigned int)(s.RCR.errors_ok));

			case 0xd: // TCR
				return
					((unsigned int)(s.TCR.coll_prio         << 4u) |
					 (unsigned int)(s.TCR.ext_stoptx        << 3u) |
					((unsigned int)(s.TCR.loop_cntl & 0x3u) << 1u) |
					 (unsigned int)(s.TCR.crc_disable));

			case 0xe: // DCR
				return
					(((unsigned int)(s.DCR.fifo_size & 0x3) << 5u) |
					  (unsigned int)(s.DCR.auto_rx          << 4u) |
					  (unsigned int)(s.DCR.loop             << 3u) |
					  (unsigned int)(s.DCR.longaddr         << 2u) |
					  (unsigned int)(s.DCR.endian           << 1u) |
					  (unsigned int)(s.DCR.wdsize));

			case 0xf: // IMR
				return
					((unsigned int)(s.IMR.rdma_inte  << 6u) |
					 (unsigned int)(s.IMR.cofl_inte  << 5u) |
					 (unsigned int)(s.IMR.overw_inte << 4u) |
					 (unsigned int)(s.IMR.txerr_inte << 3u) |
					 (unsigned int)(s.IMR.rxerr_inte << 2u) |
					 (unsigned int)(s.IMR.tx_inte    << 1u) |
					 (unsigned int) (s.IMR.rx_inte));

			default:
				BX_PANIC("page 2 offset %04x out of range", (unsigned) offset);
		}

		return (0);
	}

	void page2_write(Bit32u offset, Bit32u value, unsigned io_len)
	{
		(void)io_len;//UNUSED
		// Maybe all writes here should be BX_PANIC()'d, since they
		// affect internal operation, but let them through for now
		// and print a warning.
		if (offset != 0)
			{ BX_ERROR("page 2 write ?"); }

		switch (offset) {
			case 0x1: // CLDA0
				// Clear out low byte and re-insert
				s.local_dma &= 0xff00;
				s.local_dma |= (value & 0xff);
				break;

			case 0x2: // CLDA1
				// Clear out high byte and re-insert
				s.local_dma &= 0x00ff;
				s.local_dma |= ((value & 0xff) << 8u);
				break;

			case 0x3: // Remote Next-pkt pointer
				s.rempkt_ptr = value;
				break;

			case 0x4:
				BX_PANIC("page 2 write to reserved offset 4");
				break;

			case 0x5: // Local Next-packet pointer
				s.localpkt_ptr = value;
				break;

			case 0x6: // Address counter (upper)
				// Clear out high byte and re-insert
				s.address_cnt &= 0x00ff;
				s.address_cnt |= ((value & 0xff) << 8);
				break;

			case 0x7: // Address counter (lower)
				// Clear out low byte and re-insert
				s.address_cnt &= 0xff00;
				s.address_cnt |= (value & 0xff);
				break;

			case 0x8: case 0x9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf:
				BX_PANIC("page 2 write to reserved offset %0x", offset);
				break;

			default:
				BX_PANIC("page 2 write, illegal offset %0x", offset);
				break;
		}
	}

	// page3_read/page3_write - writes to this page are illegal
	Bit32u page3_read(Bit32u offset, unsigned int io_len)
	{
		(void)offset;//UNUSED
		(void)io_len;//UNUSED
		BX_PANIC("page 3 read attempted");
		return (0);
	}

	void page3_write(Bit32u offset, Bit32u value, unsigned io_len)
	{
		(void)value;//UNUSED
		(void)offset;//UNUSED
		(void)io_len;//UNUSED
		BX_PANIC("page 3 write attempted");
	}

	void tx_timer(void)
	{
		BX_DEBUG("tx_timer");
		s.TSR.tx_ok = 1;
		// Generate an interrupt if not masked and not one in progress
		if (s.IMR.tx_inte && !s.ISR.pkt_tx) {
			//LOG_MSG("tx complete interrupt");
			PIC_ActivateIRQ((unsigned int)s.base_irq);
		} //else LOG_MSG("no tx complete interrupt");
		s.ISR.pkt_tx = 1;
		s.tx_timer_active = 0;
	}

	// rx_frame() - called by the platform-specific code when an ethernet frame has been received. The destination address
	// is tested to see if it should be accepted, and if the rx ring has enough room, it is copied into it and
	// the receive process is updated
	void rx_frame(const void *buf, unsigned io_len)
	{
		int pages;
		int avail;
		unsigned idx;
		int nextpage;
		unsigned char pkthdr[4];
		unsigned char *pktbuf = (unsigned char *) buf;
		unsigned char *startptr;
		static unsigned char bcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

		if (io_len != 60) { BX_DEBUG("rx_frame with length %d", io_len); }

		//LOG_MSG("stop=%d, pagestart=%x, dcr_loop=%x, tcr_loopcntl=%x", s.CR.stop,s.page_start, s.DCR.loop,s.TCR.loop_cntl);
		if ((s.CR.stop != 0) || (s.page_start == 0) /*|| ((s.DCR.loop == 0) && (s.TCR.loop_cntl != 0))*/)
			return;

		// Add the pkt header + CRC to the length, and work out how many 256-byte pages the frame would occupy
		pages = (int)((io_len + 4u + 4u + 255u)/256u);

		if (s.curr_page <s.bound_ptr) {
			avail =s.bound_ptr -s.curr_page;
		} else {
			avail = (s.page_stop -s.page_start) - (s.curr_page -s.bound_ptr);
		}

		// Avoid getting into a buffer overflow condition by not attempting to do partial receives. The emulation to handle this condition
		// seems particularly painful.
		if ((avail < pages)

			//Never completely fill the ne2k ring so that we never hit the unclear completely full buffer condition.
			|| (avail == pages)

			) {
			BX_DEBUG("no space");
			return;
		}

		if ((io_len < 40/*60*/) && !s.RCR.runts_ok) {
			BX_DEBUG("rejected small packet, length %d", io_len);
			return;
		}

		// some computers don't care...
		if (io_len < 60) io_len = 60;

		// Do address filtering if not in promiscuous mode
		if (!s.RCR.promisc) {
			if (!memcmp(buf, bcast_addr, 6)) {
				if (!s.RCR.broadcast)
					return;
			} else if (pktbuf[0] & 0x01) {
				if (!s.RCR.multicast)
					return;
				idx = mcast_index(buf);
				if (!(s.mchash[idx >> 3] & (1 << (idx & 0x7))))
					return;
			} else if (0 != memcmp(buf,s.physaddr, 6)) {
				return;
			}
		} else {
			BX_DEBUG("rx_frame promiscuous receive");
		}

		BX_INFO("rx_frame %d to %x:%x:%x:%x:%x:%x from %x:%x:%x:%x:%x:%x", io_len,
			pktbuf[0], pktbuf[1], pktbuf[2], pktbuf[3], pktbuf[4], pktbuf[5],
			pktbuf[6], pktbuf[7], pktbuf[8], pktbuf[9], pktbuf[10], pktbuf[11]);

		nextpage =s.curr_page + pages;
		if (nextpage >=s.page_stop) {
			nextpage -=s.page_stop -s.page_start;
		}

		// Setup packet header
		pkthdr[0] = 0; // rx status - old behavior
		pkthdr[0] = 1; // Probably better to set it all the time rather than set it to 0, which is clearly wrong.
		if (pktbuf[0] & 0x01) {
			pkthdr[0] |= 0x20; // rx status += multicast packet
		}
		pkthdr[1] = nextpage; // ptr to next packet
		pkthdr[2] = (io_len + 4) & 0xff; // length-low
		pkthdr[3] = (io_len + 4) >> 8; // length-hi

		// copy into buffer, update curpage, and signal interrupt if config'd
		startptr = &s.mem[s.curr_page * 256 - BX_NE2K_MEMSTART];
		if ((nextpage >s.curr_page) || ((s.curr_page + pages) ==s.page_stop)) {
			memcpy(startptr, pkthdr, 4);
			memcpy(startptr + 4, buf, io_len);
			s.curr_page = nextpage;
		} else {
			unsigned int endbytes = (unsigned int)(s.page_stop -s.curr_page) * 256u;
			memcpy(startptr, pkthdr, 4);
			memcpy(startptr + 4, buf, (size_t)(endbytes - 4u));
			startptr = &s.mem[s.page_start * 256u - BX_NE2K_MEMSTART];
			memcpy(startptr, (void *)(pktbuf + endbytes - 4u), (size_t)(io_len - endbytes + 8u));
			s.curr_page = nextpage;
		}

		s.RSR.rx_ok = 1;
		if (pktbuf[0] & 0x80) {
			s.RSR.rx_mbit = 1;
		}

		s.ISR.pkt_rx = 1;

		if (s.IMR.rx_inte) {
			//LOG_MSG("packet rx interrupt");
			PIC_ActivateIRQ((unsigned int)s.base_irq);
		} //else LOG_MSG("no packet rx interrupt");
	}

	// mcast_index() - return the 6-bit index into the multicast table. Stolen unashamedly from FreeBSD's if_ed.c
	unsigned mcast_index(const void *dst)
	{
		unsigned long crc = 0xffffffffL;
		int carry, i, j;
		unsigned char b, *ep = (unsigned char *)dst;
		for (i = 6; --i >= 0;) {
			b = *ep++;
			for (j = 8; --j >= 0;) {
				carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
				crc <<= 1;
				b >>= 1;
				if (carry)
					crc = ((crc ^ 0x04c11db6) | (unsigned int)carry);
			}
		}
		return (Bit32u)((crc & 0xfffffffful) >> 26ul); /* WARNING: Caller directly uses our 6-bit return as index. If not truncated, will cause a segfault */
	}

	Bit32u read(Bit32u address, unsigned io_len)
	{
		BX_DEBUG("read addr %x, len %d", address, io_len);
		Bit32u retval = 0;
		unsigned int offset = (unsigned int)address - (unsigned int)(s.base_address);
		if (offset >= 0x10) {
			retval = asic_read(offset - 0x10, io_len);
		} else if (offset == 0x00) {
			retval = read_cr();
		} else {
			switch (s.CR.pgsel) {
				case 0x00:
					retval = page0_read(offset, io_len);
					break;

				case 0x01:
					retval = page1_read(offset, io_len);
					break;

				case 0x02:
					retval = page2_read(offset, io_len);
					break;

				case 0x03:
					retval = page3_read(offset, io_len);
					break;

				default:
					BX_PANIC("ne2K: unknown value of pgsel in read - %d", s.CR.pgsel);
			}
		}
		return (retval);
	}

	void write(Bit32u address, Bit32u value, unsigned io_len)
	{
		BX_DEBUG("write with length %d", io_len);
		unsigned int offset = (unsigned int)address - (unsigned int)(s.base_address);

		// The high 16 bytes of i/o space are for the ne2000 asic - the low 16 bytes are for the DS8390, with the current
		//  page being selected by the PS0,PS1 registers in the command register
		if (offset >= 0x10) {
			asic_write(offset - 0x10, value, io_len);
		} else if (offset == 0x00) {
			write_cr(value);
		} else {
			switch (s.CR.pgsel) {
				case 0x00:
					page0_write(offset, value, io_len);
					break;

				case 0x01:
					page1_write(offset, value, io_len);
					break;

				case 0x02:
					page2_write(offset, value, io_len);
					break;

				case 0x03:
					page3_write(offset, value, io_len);
					break;

				default:
					BX_PANIC("ne2K: unknown value of pgsel in write - %d", s.CR.pgsel);
			}
		}
	}
	
	static void dosbox_tx_event(Bitu val)
	{
		(void)val;//UNUSED
		self->tx_timer();
	}

	static void dosbox_tick_handler(void)
	{
		if (!dbp_net_connected || !dbp_net->IncomingNe2k.size()) return;

		dbp_net->IncomingMtx.Lock();
		Bit16u len;
		for (Bit8u* p = &dbp_net->IncomingNe2k[0], *pEnd = p + dbp_net->IncomingNe2k.size(); p < pEnd; p += 2 + len)
		{
			len = NET_READ_LE16(p);
			//LOG_MSG("NE2000: Received %d bytes", len);
			// don't receive in loopback modes
			if ((self->s.DCR.loop != 0) && (self->s.TCR.loop_cntl == 0))
				self->rx_frame(p + 2, len);
		}
		dbp_net->IncomingNe2k.clear();
		dbp_net->IncomingMtx.Unlock();
	}

	// read_handler - i/o 'catcher' function called from DOSBox when the CPU attempts a read in the i/o space registered by this ne2000 instance
	static Bitu dosbox_read_handler(Bitu port, Bitu len)
	{
		Bitu retval = self->read((Bit32u)port,(unsigned int)len);
		//LOG_MSG("ne2k rd port %x val %4x len %d page %d, CS:IP %8x:%8x",
		//	port, retval, len, self->s.CR.pgsel,SegValue(cs),reg_eip);
		return retval;
	}

	// write_handler - i/o 'catcher' function called from DOSBox when the CPU attempts a write in the i/o space registered by this ne2000 instance
	static void dosbox_write_handler(Bitu port, Bitu val, Bitu len)
	{
		//LOG_MSG("ne2k wr port %x val %4x len %d page %d, CS:IP %8x:%8x",
		//	port, val, len,self->s.CR.pgsel,SegValue(cs),reg_eip);
		self->write((Bit32u)port, (Bit32u)val, (unsigned int)len);
	}
};
NE2K* NE2K::self;

void NET_SetupEthernet()
{
	if (DOSIPX::self) { delete DOSIPX::self; DOSIPX::self = NULL; }
	DBP_ASSERT(!NE2K::self);
	NE2K::self = new NE2K();
}

void NET_ShutdownEthernet()
{
	delete NE2K::self;
	NE2K::self = NULL;
}

#endif // C_DBP_ENABLE_LIBRETRO_NE2K

#ifdef C_DBP_ENABLE_LIBRETRO_IPX

void IPX_ShutDown(Section* sec) {
	if (DOSIPX::self) { delete DOSIPX::self; DOSIPX::self = NULL; }
	extern bool DBP_IsShuttingDown();
	if (DBP_IsShuttingDown()) NET_ShutdownEthernet(); // also call this here because NE2K doesn't have its own Section to register a shutdown function
}

void IPX_Init(Section* sec) {
	if (!NE2K::self && static_cast<Section_prop *>(sec)->Get_bool("ipx")) DOSIPX::self = new DOSIPX();
	sec->AddDestroyFunction(&IPX_ShutDown,true);
}

#endif // C_DBP_ENABLE_LIBRETRO_IPX

#include "../libretro-common/include/libretro.h"
static retro_netpacket_send_t dbp_net_send_fn;

struct NetCallBacks
{
	static void RETRO_CALLCONV start(uint16_t client_id, retro_netpacket_send_t send_fn, retro_netpacket_poll_receive_t poll_receive_fn)
	{
		LOG_MSG("[DOSBOXNET] Multiplayer session ready! ClientId: %d", client_id);
		if (!dbp_net) dbp_net = new DBP_Net; else cleanup();
		DBP_Net_InitMac(client_id);
		if (NE2K::self) NE2K::self->init_mac();
		dbp_net_connected = true;
		dbp_net_send_fn = send_fn;
		void DBP_EnableNetwork();
		DBP_EnableNetwork();
	}

	static INLINE Bit16u client_id_from_mac(const Bit8u* mac)
	{
		// Consider unknown first octet to be a multicast of some sort
		return (mac[0] == DBP_Net::FIRST_MAC_OCTET ? NET_READ_BE16(&mac[4]) : (Bit16u)RETRO_NETPACKET_BROADCAST);
	}

	static void RETRO_CALLCONV receive(const void* pkt, size_t pktlen, uint16_t client_id)
	{
		DBP_ASSERT(NET_READ_BE16(&dbp_net_addr.mac[4]) != client_id); // can't be from myself
		if (pktlen < 1 || pktlen > 65535) { DBP_ASSERT(false); return; }

		#if 0 // log traffic summary
		static size_t _sumlen, _sumnum; _sumlen += pktlen; _sumnum++; extern Bit32u DBP_GetTicks(); static Bit32u lastreport; Bit32u tick = DBP_GetTicks();
		if (tick - lastreport >= 1000)
		{
			printf("[DOSBOXNET] Received %d bytes in %d packets\n", (int)_sumlen, (int)_sumnum);
			_sumlen = _sumnum = 0;
			lastreport = ((tick - lastreport < 2000) ? (lastreport + 1000) : tick);
		}
		#endif

		//printf("[DOSBOXNET] Received Packet - From: %d, Type: %d, Len: %d\n", client_id, *(Bit8u*)pkt, pktlen);
		switch (*(const Bit8u*)pkt)
		{
			case DBP_Net::PKT_IPX:
			case DBP_Net::PKT_NE2K:
			{
				std::vector<Bit8u>& incoming = (*(const Bit8u*)pkt == DBP_Net::PKT_IPX ? dbp_net->IncomingIPX : dbp_net->IncomingNe2k);
				dbp_net->IncomingMtx.Lock();
				size_t incomingsz = incoming.size(), datalen = pktlen - 1; // reduce by packet type
				incoming.resize(incomingsz + 2 + datalen);
				Bit8u *pStore = &incoming[incomingsz];
				NET_WRITE_LE16(pStore, (Bit16u)datalen);
				memcpy(pStore + 2, (const Bit8u*)pkt + 1, datalen);
				dbp_net->IncomingMtx.Unlock();
				break;
			}
			case DBP_Net::PKT_MODEM:
			{
				dbp_net->IncomingMtx.Lock();
				size_t incomingsz = dbp_net->IncomingModem.size(), datalen = pktlen - 1; // reduce by packet type
				dbp_net->IncomingModem.resize(incomingsz + datalen);
				memcpy(&dbp_net->IncomingModem[incomingsz], (const Bit8u*)pkt + 1, datalen);
				dbp_net->IncomingMtx.Unlock();
				break;
			}
		}
	}

	static void RETRO_CALLCONV stop(void)
	{
		LOG_MSG("[DOSBOXNET] Multiplayer session ended");
		if (dbp_net) cleanup();
		dbp_net_connected = false;
		dbp_net_send_fn = NULL;
	}

	static void RETRO_CALLCONV poll(void)
	{
		if (dbp_net_msg)
		{
			extern void retro_notify(int duration, retro_log_level lvl, char const* format,...);
			retro_notify(2000, RETRO_LOG_INFO, dbp_net_msg);
			dbp_net_msg = NULL;
		}

		#if 0 // log traffic summary
		static size_t _sumlen, _sumnum; _sumlen += (dbp_net->OutgoingPackets.size() + dbp_net->OutgoingModem.size()); _sumnum++; extern Bit32u DBP_GetTicks(); static Bit32u lastreport; Bit32u tick = DBP_GetTicks();
		if (tick - lastreport >= 1000)
		{
			printf("[DOSBOXNET] Sent %d bytes (%d polls) - [BUF] IPX: %d, NE2K: %d, MODEM: %d\n", (int)_sumlen, (int)_sumnum, (int)dbp_net->IncomingIPX.size(), (int)dbp_net->IncomingNe2k.size(), (int)dbp_net->IncomingModem.size());
			_sumlen = _sumnum = 0;
			lastreport = ((tick - lastreport < 2000) ? (lastreport + 1000) : tick);
		}
		#endif

		if (!dbp_net->OutgoingPackets.size() && !dbp_net->OutgoingModem.size()) return;

		//printf("[DOSBOXNET] Sending Packet(s) - Total Len: %d\n", (int)(dbp_net->OutgoingPackets.size() + dbp_net->OutgoingModem.size()));
		dbp_net->OutgoingMtx.Lock();
		if (dbp_net->OutgoingPackets.size())
		{
			size_t len;
			for (Bit8u* p = &dbp_net->OutgoingPackets[0], *pEnd = p + dbp_net->OutgoingPackets.size(); p < pEnd; p += 2 + len)
			{
				len = NET_READ_LE16(p) + 1; // add packet type byte
				const Bit8u *data = p + 3, *src_mac, *dest_mac;
				if (p[2] == DBP_Net::PKT_IPX)
				{
					if (len < (sizeof(IPXHeader) + 1)) { DBP_ASSERT(false); return; }
					src_mac = ((const IPXHeader*)data)->src.mac, dest_mac = ((const IPXHeader*)data)->dest.mac;
				}
				else // DBP_Net::PKT_NE2K
				{
					if (len < (sizeof(EthernetHeader) + 1)) { DBP_ASSERT(false); return; }
					src_mac = ((const EthernetHeader*)data)->src_mac, dest_mac = ((const EthernetHeader*)data)->dest_mac;
				}
				DBP_ASSERT(memcmp(dest_mac, dbp_net_addr.mac, 6)); // maybe? maybe not? probably...
				//DBP_ASSERT(!memcmp(src_mac, dbp_net_addr.mac, 6)); // can fail during startup of Win9x, so allow it
				//DBP_ASSERT(client_id_from_mac(src_mac) != client_id_from_mac(dest_mac)); // can fail during startup of Win9x, so allow it
				// Currently we alwyas send everything NE2K wants to (IPX filters itself in DOSIPX::sendPacket)
				// For example, TCP/IP might use multicasts (01-00-5e mac addresses) that we just broadcast to everyone and have the endpoint figure it out
				// IPX on the other hand we cannot send to the wrong target as (some implementations?) think every packet is destined for them
				//if (dest_mac[0] != DBP_Net::FIRST_MAC_OCTET && dest_mac[0] != 0xFF)
				//{
				//	printf("[DOSBOXNET] IGNORING Packet - Len: %d - Src: %02x:%02x:%02x:%02x:%02x:%02x (#%d) - Dest: %02x:%02x:%02x:%02x:%02x:%02x (#%d)\n", len, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], client_id_from_mac(src_mac), dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], client_id_from_mac(dest_mac));
				//	continue;
				//}
				//printf("[DOSBOXNET] Outgoing Packet - Len: %d - Src: %02x:%02x:%02x:%02x:%02x:%02x (#%d) - Dest: %02x:%02x:%02x:%02x:%02x:%02x (#%d)\n", len, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], client_id_from_mac(src_mac), dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], client_id_from_mac(dest_mac));
				dbp_net_send_fn(RETRO_NETPACKET_RELIABLE, p + 2, len, client_id_from_mac(dest_mac));
			}
			dbp_net->OutgoingPackets.clear();
		}
		if (dbp_net->OutgoingModem.size())
		{
			for (Bit8u* p = &dbp_net->OutgoingModem[0], *pEnd = (p++) + dbp_net->OutgoingModem.size(); p < pEnd; p += 1023)
			{
				size_t len = (pEnd - p > 1023 ? (Bit32u)1023 : (Bit32u)(pEnd - p));
				p[-1] = DBP_Net::PKT_MODEM; // prefix with packet type (space reserved by CLibretroModem::transmitByte)
				dbp_net_send_fn(RETRO_NETPACKET_RELIABLE, p-1, len + 1, RETRO_NETPACKET_BROADCAST);
			}
			dbp_net->OutgoingModem.clear();
		}
		dbp_net->OutgoingMtx.Unlock();
	}

	//static void RETRO_CALLCONV connected(uint16_t client_id) { LOG_MSG("[DOSBOXNET] Client Connected: %d", client_id); }
	//static void RETRO_CALLCONV disconnected(uint16_t client_id) { LOG_MSG("[DOSBOXNET] Client Disconnected: %d", client_id); }

	static void cleanup()
	{
		dbp_net->OutgoingMtx.Lock();
		dbp_net->OutgoingPackets.clear();
		dbp_net->OutgoingModem.clear();
		dbp_net->OutgoingMtx.Unlock();
		dbp_net->IncomingMtx.Lock();
		dbp_net->IncomingIPX.clear();
		dbp_net->IncomingNe2k.clear();
		dbp_net->IncomingModem.clear();
		dbp_net->IncomingMtx.Unlock();
	}
};

#if 0 // disabled aggressive latency reduction
void DBP_Network_Flush()
{
	if (dbp_net_poll_receive_fn) dbp_net_poll_receive_fn();
	if (dbp_net_send_fn) NetCallBacks::SendOutgoing(RETRO_NETPACKET_RELIABLE|RETRO_NETPACKET_FLUSH_HINT);
}
#endif

void DBP_Network_SetCallbacks(retro_environment_t envcb)
{
	static const retro_netpacket_callback packet_callback =
	{
		NetCallBacks::start, NetCallBacks::receive,
		NetCallBacks::stop, NetCallBacks::poll,
		//NetCallBacks::connected, NetCallBacks::disconnected,
	};
	envcb(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, (void*)&packet_callback);

#if 0 // This was disabled due to a bug in RetroArch 1.16 (fixed for 1.17 in https://github.com/libretro/RetroArch/pull/16019)
	// We provide backwards compatibility with the deprecated environment call 76
	#define RETRO_ENVIRONMENT_SET_NETPACKET76_INTERFACE 76
	typedef void (RETRO_CALLCONV *retro_netpacket76_send_t)(int flags, const void* buf, size_t len, uint16_t client_id, bool broadcast);
	typedef void (RETRO_CALLCONV *retro_netpacket76_start_t)(uint16_t client_id, retro_netpacket76_send_t send_fn);
	struct retro_netpacket76_callback { retro_netpacket76_start_t start; retro_netpacket_receive_t receive; retro_netpacket_stop_t stop; retro_netpacket_poll_t poll; retro_netpacket_connected_t connected; retro_netpacket_disconnected_t disconnected; };
	static retro_netpacket76_send_t dbp_net76_send_fn;
	struct NetCallBacks76
	{
		static void RETRO_CALLCONV wrapsend76(int flags, const void* buf, size_t len, uint16_t client_id)
		{
			dbp_net76_send_fn(flags, buf, len, client_id, (client_id == RETRO_NETPACKET_BROADCAST));
		}
		static void RETRO_CALLCONV start76(uint16_t client_id, retro_netpacket76_send_t send_fn)
		{
			NetCallBacks::start(client_id, NULL, NULL);
			dbp_net76_send_fn = send_fn;
			dbp_net_send_fn = wrapsend76;
		}
	};
	static const retro_netpacket76_callback packet76_callback = { NetCallBacks76::start76, NetCallBacks::receive, NetCallBacks::stop, NetCallBacks::poll };
	envcb(RETRO_ENVIRONMENT_SET_NETPACKET76_INTERFACE, (void*)&packet76_callback);
#endif
}
