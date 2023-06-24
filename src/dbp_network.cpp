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
#include "dbp_network.h"
#include "dbp_threads.h"

struct DBP_Net
{
	enum { PKT_IPX, PKT_NE2K, PKT_MODEM };
	std::vector<Bit8u> IncomingIPX, IncomingNe2k, IncomingModem, OutgoingPackets, OutgoingModem;
	Mutex IncomingMtx, OutgoingMtx;
	union Addr { Bit8u raw[10]; struct { Bit8u ipxnum[4], mac[6]; }; };
};

static bool dbp_net_connected;
static Bit16u dbp_net_client_id;
static DBP_Net::Addr dbp_net_addr;
static DBP_Net* dbp_net;
static struct DBP_Net_Cleanup { ~DBP_Net_Cleanup() { delete dbp_net; } } _dbp_net_cleanup;

#define NET_READ_LE16(p) ((Bit16u)(((const Bit8u *)(p))[0]) | ((Bit16u)(((const Bit8u *)(p))[1]) << 8U))
#define NET_WRITE_LE16(p,v) { ((Bit8u*)(p))[0] = (Bit8u)((Bit16u)(v) & 0xFF); ((Bit8u*)(p))[1] = (Bit8u)(((Bit16u)(v) >> 8) & 0xFF); }
#define NET_READ_BE16(p) (((Bit16u)(((const Bit8u *)(p))[0]) << 8U) | (Bit16u)(((const Bit8u *)(p))[1]))
#define NET_WRITE_BE16(p,v) { ((Bit8u*)(p))[0] = (Bit8u)(((Bit16u)(v) >> 8) & 0xFF); ((Bit8u*)(p))[1] = (Bit8u)((Bit16u)(v) & 0xFF); }
static inline Bit16u NET_SWAP_BIT16U(Bit16u sockNum) { return (((sockNum>> 8)) | (sockNum << 8)); }

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
		return NET_SWAP_BIT16U(real_readw(RealSeg(ECBAddr), RealOff(ECBAddr) + 0xa));
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
		Bit16u sockNum = NET_SWAP_BIT16U(reg_dx);

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
		reg_dx = NET_SWAP_BIT16U(sockNum);  // Convert back to big-endian
	}

	void CloseSocket(void) {
		Bit16u sockNum = NET_SWAP_BIT16U(reg_dx);
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
				LOG_IPX("IPX: Open socket %4x", NET_SWAP_BIT16U(reg_dx));
				break;
			case 0x0001:	// Close socket
				LOG_IPX("IPX: Close socket %4x", NET_SWAP_BIT16U(reg_dx));
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
				// LOG_IPX("ECB: RECEIVE.");
				if (sockInUse(tmpECB->getSocket()) < 0) {  // Socket is not open
					reg_al = 0xff;
					tmpECB->setInUseFlag(IPXECB::USEFLAG_AVAILABLE);
					tmpECB->setCompletionFlag(IPXECB::COMP_HARDWAREERROR);
					delete tmpECB;
				} else {
					reg_al = 0x00;  // Success
					tmpECB->setInUseFlag(IPXECB::USEFLAG_LISTENING);
					/*LOG_IPX("IPX: Listen for packet on 0x%4x - ESR address %4x:%4x",
						tmpECB->getSocket(),
						RealSeg(tmpECB->getESRAddr()),
						RealOff(tmpECB->getESRAddr()));*/
				}
				break;
			case 0x0005:	// SCHEDULE IPX EVENT
			case 0x0007:	// SCHEDULE SPECIAL IPX EVENT
			{
				tmpECB = new IPXECB(SegValue(es),reg_si);
				// LOG_IPX("ECB: AES. T=%fms.", (1000.0f/(1193182.0f/65536.0f))*(float)reg_ax);
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

				Bit8u * addrptr = (Bit8u *)&dbp_net_addr.raw;
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

	void receivePacket(Bit8u *buffer, Bit16u bufSize) {
		IPXECB *useECB;
		IPXECB *nextECB;
		Bit16u *bufword = (Bit16u *)buffer;
		Bit16u useSocket = NET_SWAP_BIT16U(bufword[8]);

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
		Bit16u *wordptr;

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
				addrptr = (Bit8u *)&dbp_net_addr.ipxnum;
				for(Bit16u m=0;m<4;m++) {
					real_writeb(tmpFrag.segment,tmpFrag.offset+m+18,addrptr[m]);
				}
				// source node number
				addrptr = (Bit8u *)&dbp_net_addr.mac;
				for(Bit16u m=0;m<6;m++) {
					real_writeb(tmpFrag.segment,tmpFrag.offset+m+22,addrptr[m]);
				}
				// Source socket
				real_writew(tmpFrag.segment,tmpFrag.offset+28, NET_SWAP_BIT16U(sendecb->getSocket()));

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
		wordptr = (Bit16u *)&outbuffer[0];
		// Blank CRC
		//wordptr[0] = 0xffff;
		// Length
		wordptr[1] = NET_SWAP_BIT16U(packetsize);
		// Source socket
		//wordptr[14] = swapByte(sendecb->getSocket());

		sendecb->getFragDesc(0,&tmpFrag);
		real_writew(tmpFrag.segment,tmpFrag.offset+2, NET_SWAP_BIT16U(packetsize));

		Bit8u immedAddr[6];
		sendecb->getImmAddress(immedAddr);
		// filter out broadcasts and local loopbacks
		// Real implementation uses the ImmedAddr to check whether this is a broadcast

		bool islocalbroadcast=true;
		bool isloopback=true;

		Bit8u * addrptr;
		addrptr = (Bit8u *)&dbp_net_addr.ipxnum;
		for(Bitu m=0;m<4;m++) {
			if(addrptr[m]!=outbuffer[m+0x6])isloopback=false;
		}
		addrptr = (Bit8u *)&dbp_net_addr.mac;
		for(Bitu m=0;m<6;m++) {
			if(addrptr[m]!=outbuffer[m+0xa])isloopback=false;
			if(immedAddr[m]!=0xff) islocalbroadcast=false;
		}
		//LOG_IPX("SEND crc:%2x",packetCRC(&outbuffer[0], packetsize));

		if (!isloopback) {
			if (!dbp_net_connected)
			{
				sendecb->setCompletionFlag(IPXECB::COMP_HARDWAREERROR);
				sendecb->NotifyESR();
				return;
			}
			sendecb->setCompletionFlag(IPXECB::COMP_SUCCESS);

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
			// LOG_IPX("ECB: notified.");
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

void IPX_ShutDown(Section* sec) {
	if (DOSIPX::self) { delete DOSIPX::self; DOSIPX::self = NULL; }
	void NET_ShutdownNetworkCard();
	NET_ShutdownNetworkCard();
}

void IPX_Init(Section* sec) {
	if (static_cast<Section_prop *>(sec)->Get_bool("ipx")) DOSIPX::self = new DOSIPX();
	sec->AddDestroyFunction(&IPX_ShutDown,true);
}

#endif // C_DBP_ENABLE_LIBRETRO_IPX

#ifdef C_DBP_ENABLE_LIBRETRO_MODEM

enum { SERIAL_TX_REDUCTION = SERIAL_BASE_EVENT_COUNT+1, SERIAL_NULLMODEM_EVENT_COUNT = SERIAL_BASE_EVENT_COUNT+1 };
enum { N_RX_IDLE, N_RX_WAIT, N_RX_BLOCKED, N_RX_FASTWAIT, N_RX_DISC };

CLibretroModem::CLibretroModem(Bitu id, CommandLine* cmd) : CSerial(id, cmd)
{
	InstallationSuccessful = false;
	rx_retry = 0;
	rx_retry_max = 20;
	rx_state = N_RX_DISC;
	tx_gather = 12;
	tx_block = false;
	transparent = false;
	recvbuflen = recvbufofs = 0;
	Bitu bool_temp = 0;

	// transparent: don't add additional handshake control.
	if (CSerial::getBituSubstring("transparent:", &bool_temp, cmd)) {
		if (bool_temp==1) transparent=true;
		else transparent=false;
	}
	// rxdelay: How many milliseconds to wait before causing an
	// overflow when the application is unresponsive.
	if (CSerial::getBituSubstring("rxdelay:", &rx_retry_max, cmd)) {
		if (!(rx_retry_max<=10000)) {
			rx_retry_max=50;
		}
	}
	// txdelay: How many milliseconds to wait before sending data.
	// This reduces network overhead quite a lot.
	if (CSerial::getBituSubstring("txdelay:", &tx_gather, cmd)) {
		if (!(tx_gather<=500)) {
			tx_gather=12;
		}
	}

	CSerial::Init_Registers();
	InstallationSuccessful = true;

	// We are connnected from startup
	setCTS(transparent);
	setDSR(transparent);
	setRI(false);

	// transmit the line status
	setRTSDTR(getRTS(), getDTR());
	bool is_client = (dbp_net_client_id != 0); // not host
	if (is_client || transparent) setCD(true);

	rx_state = N_RX_IDLE;
	setEvent(SERIAL_POLLING_EVENT, 1);
}

CLibretroModem::~CLibretroModem()
{
	// remove events
	for(Bit16u i = SERIAL_BASE_EVENT_COUNT+1; i <= SERIAL_NULLMODEM_EVENT_COUNT; i++)
		removeEvent(i);
}

void CLibretroModem::handleUpperEvent(Bit16u type)
{
	switch(type) {
		case SERIAL_POLLING_EVENT: {
			// periodically check if new data arrived, disconnect if required. Add it back.
			setEvent(SERIAL_POLLING_EVENT, 1.0f);
			// update Modem input line states
			updateMSR();
			switch(rx_state) {
				case N_RX_IDLE:
					if (CanReceiveByte()) {
						if (doReceive()) {
							// a byte was received
							rx_state=N_RX_WAIT;
							setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
						} // else still idle
					} else {
						#if SERIAL_DEBUG
						log_ser(dbg_aux,"Nullmodem: block on polling.");
						#endif
						rx_state=N_RX_BLOCKED;
						// have both delays (1ms + bytetime)
						setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
					}
					break;
				case N_RX_BLOCKED:
					// one timeout tick
					if (!CanReceiveByte()) {
						rx_retry++;
						if (rx_retry>=rx_retry_max) {
							// it has timed out:
							rx_retry=0;
							removeEvent(SERIAL_RX_EVENT);
							if (doReceive()) {
								// read away everything
								while(doReceive());
								rx_state=N_RX_WAIT;
								setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
							} else {
								// much trouble about nothing
								rx_state=N_RX_IDLE;
								#if SERIAL_DEBUG
								log_ser(dbg_aux,"Nullmodem: unblock due to no more data",rx_retry);
								#endif
							}
						} // else wait further
					} else {
						// good: we can receive again
						removeEvent(SERIAL_RX_EVENT);
						rx_retry=0;
						if (doReceive()) {
							rx_state=N_RX_FASTWAIT;
							setEvent(SERIAL_RX_EVENT, bytetime*0.65f);
						} else {
							// much trouble about nothing
							rx_state=N_RX_IDLE;
						}
					}
					break;

				case N_RX_WAIT:
				case N_RX_FASTWAIT:
					break;
			}
			break;
		}
		case SERIAL_RX_EVENT: {
			switch(rx_state) {
				case N_RX_IDLE:
					LOG_MSG("internal error in nullmodem");
					break;

				case N_RX_BLOCKED: // try to receive
				case N_RX_WAIT:
				case N_RX_FASTWAIT:
					if (CanReceiveByte()) {
						// just works or unblocked
						if (doReceive()) {
							rx_retry=0; // not waiting anymore
							if (rx_state==N_RX_WAIT) setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
							else {
								// maybe unblocked
								rx_state=N_RX_FASTWAIT;
								setEvent(SERIAL_RX_EVENT, bytetime*0.65f);
							}
						} else {
							// didn't receive anything
							rx_retry=0;
							rx_state=N_RX_IDLE;
						}
					} else {
						// blocking now or still blocked
						#if SERIAL_DEBUG
						if (rx_state==N_RX_BLOCKED)
							log_ser(dbg_aux,"Nullmodem: rx still blocked (retry=%d)",rx_retry);
						else log_ser(dbg_aux,"Nullmodem: block on continued rx (retry=%d).",rx_retry);
						#endif
						setEvent(SERIAL_RX_EVENT, bytetime*0.65f);
						rx_state=N_RX_BLOCKED;
					}
					break;
			}
			break;
		}
		case SERIAL_TX_EVENT: {
			// Maybe echo cirquit works a bit better this way
			if (rx_state==N_RX_IDLE && CanReceiveByte() /*&& clientsocket*/) {
				if (doReceive()) {
					// a byte was received
					rx_state=N_RX_WAIT;
					setEvent(SERIAL_RX_EVENT, bytetime*0.9f);
				}
			}
			ByteTransmitted();
			break;
		}
		case SERIAL_THR_EVENT: {
			ByteTransmitting();
			// actually send it
			setEvent(SERIAL_TX_EVENT,bytetime+0.01f);
			break;
		}
		case SERIAL_TX_REDUCTION: {
			// TODO: Flush the data in the transmitting buffer.
			//if (clientsocket) clientsocket->FlushBuffer();
			tx_block=false;
			break;
		}
	}
}

void CLibretroModem::doSend(Bit8u val, bool is_escape)
{
	dbp_net->OutgoingMtx.Lock();
	if (!dbp_net->OutgoingModem.size()) dbp_net->OutgoingModem.push_back(0); // leave space for packet id
	if (is_escape) dbp_net->OutgoingModem.push_back(0xff);
	dbp_net->OutgoingModem.push_back(val);
	dbp_net->OutgoingMtx.Unlock();
}

void CLibretroModem::setRTSDTR(bool rts, bool dtr)
{
	if (transparent || !dbp_net_connected) return;
	doSend((Bit8u)((rts ? 1 : 0) | (dtr ? 2 : 0) | ((LCR&LCR_BREAK_MASK) ? 4 : 0)), true);
}

void CLibretroModem::transmitByte(Bit8u val, bool first)
{
 	// transmit it later in THR_Event
	if (first) setEvent(SERIAL_THR_EVENT, bytetime/8);
	else setEvent(SERIAL_TX_EVENT, bytetime);

	if (dbp_net_connected)
	{
		// disable 0xff escaping when transparent mode is enabled
		doSend(val, (!transparent && val == 0xff));
	}

	if (!tx_block) {
		//LOG_MSG("setevreduct");
		setEvent(SERIAL_TX_REDUCTION, (float)tx_gather);
		tx_block=true;
	}
}

bool CLibretroModem::doReceive()
{
	Bit8u rxchar[2], rxcount = 0;
	grab_next_char:
	if (recvbuflen == recvbufofs && (!dbp_net_connected || !dbp_net->IncomingModem.size())) return false;

	if (recvbuflen == recvbufofs)
	{
		dbp_net->IncomingMtx.Lock();
		recvbufofs = 0, recvbuflen = (dbp_net->IncomingModem.size() > sizeof(recvbuf) ? (Bit8u)sizeof(recvbuf) : (Bit8u)dbp_net->IncomingModem.size());
		memcpy(recvbuf, &dbp_net->IncomingModem[0], recvbuflen);
		dbp_net->IncomingModem.erase(dbp_net->IncomingModem.begin(), dbp_net->IncomingModem.begin() + recvbuflen);
		dbp_net->IncomingMtx.Unlock();
	}

	rxchar[rxcount] = recvbuf[recvbufofs++];

	if (rxchar[0] == 0xff && !transparent)
	{
		// escape char
		if (!rxcount) { rxcount = 1; goto grab_next_char; }
		if (rxchar[1] != 0xff) // 0xff 0xff -> 0xff was meant
		{
			setCTS(!!(rxchar[1] & 0x1));
			setDSR(!!(rxchar[1] & 0x2));
			if (rxchar[1] & 0x4) receiveByteEx(0x0, 0x10);
			return 0; // no "payload" received
		}
	}
	receiveByteEx(rxchar[0],0);
	return true;
}

//void CLibretroModem::Disconnect() {
//	removeEvent(SERIAL_POLLING_EVENT);
//	removeEvent(SERIAL_RX_EVENT);
//	LOG_MSG("Serial%d: Disconnected.",COMNUMBER);
//	setDSR(false);
//	setCTS(false);
//	setCD(false);
//}

void CLibretroModem::setBreak(bool /*val*/) { setRTSDTR(getRTS(), getDTR()); }
void CLibretroModem::setRTS(bool val)       { setRTSDTR(     val, getDTR()); }
void CLibretroModem::setDTR(bool val)       { setRTSDTR(getRTS(),      val); }
void CLibretroModem::updateMSR() { } // does nothing on a null modem
void CLibretroModem::updatePortConfig(Bit16u divider, Bit8u lcr) { } // dummy

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

struct EthernetHeader {
	Bit8u dest_mac[6], src_mac[6], type_and_length[2];
};

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

	IO_ReadHandleObject ReadHandler8[0x20];
	IO_WriteHandleObject WriteHandler8[0x20];
	IO_ReadHandleObject ReadHandler16[0x10];
	IO_WriteHandleObject WriteHandler16[0x10];

	NE2K()
	{
		memcpy(s.physaddr, dbp_net_addr.mac, 6);
		s.base_address = (Bit32u)0x300;
		s.base_irq = (int)10;
		
		//BX_DEBUG(("Init $Id: ne2k.cc,v 1.56.2.1 2004/02/02 22:37:22 cbothamy Exp $"));
		BX_INFO("port 0x%x/32 irq %d mac %02x:%02x:%02x:%02x:%02x:%02x",
				(unsigned int)(s.base_address), (int)(s.base_irq),
				s.physaddr[0], s.physaddr[1], s.physaddr[2], s.physaddr[3], s.physaddr[4], s.physaddr[5]);

		// Initialise the mac address area by doubling the physical address
		s.macaddr[0]  = s.physaddr[0];
		s.macaddr[1]  = s.physaddr[0];
		s.macaddr[2]  = s.physaddr[1];
		s.macaddr[3]  = s.physaddr[1];
		s.macaddr[4]  = s.physaddr[2];
		s.macaddr[5]  = s.physaddr[2];
		s.macaddr[6]  = s.physaddr[3];
		s.macaddr[7]  = s.physaddr[3];
		s.macaddr[8]  = s.physaddr[4];
		s.macaddr[9]  = s.physaddr[4];
		s.macaddr[10] = s.physaddr[5];
		s.macaddr[11] = s.physaddr[5];

		// ne2k signature
		for (Bitu i = 12; i < 32; i++)
			s.macaddr[i] = 0x57;

		// Bring the register state into power-up state
		reset();

		// install I/O-handlers and timer
		for(Bitu i = 0; i < 0x20; i++) {
			ReadHandler8[i].Install((i+s.base_address), dosbox_read_handler, IO_MB|IO_MW);
			WriteHandler8[i].Install((i+s.base_address), dosbox_write_handler, IO_MB|IO_MW);
		}
		TIMER_AddTickHandler(dosbox_tick_handler);
	}

	~NE2K() {
		TIMER_DelTickHandler(dosbox_tick_handler);
		PIC_RemoveEvents(dosbox_tx_event);
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
		} else if (value & 0x04) {
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
			PIC_AddEvent(dosbox_tx_event,(float)((64 + 96 + 4*8 +s.tx_bytes*8)/10000.0),0);

			// Schedule a timer to trigger a tx-complete interrupt
			// The number of microseconds is the bit-time / 10.
			// The bit-time is the preamble+sfd (64 bits), the
			// inter-frame gap (96 bits), the CRC (4 bytes), and the
			// the number of bits in the frame (s.tx_bytes * 8).
			/* TODO: Code transmit timer */
			//bx_pc_system.activate_timer(s.tx_timer_index, (64 + 96 + 4*8 +s.tx_bytes*8)/10, 0); // not continuous
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

void NET_SetupNetworkCard()
{
	if (DOSIPX::self) { delete DOSIPX::self; DOSIPX::self = NULL; }
	DBP_ASSERT(!NE2K::self);
	NE2K::self = new NE2K();
}

void NET_ShutdownNetworkCard()
{
	delete NE2K::self;
	NE2K::self = NULL;
}

#endif // C_DBP_ENABLE_LIBRETRO_NE2K

#include "../libretro-common/include/libretro.h"
static retro_netpacket_send_t dbp_net_send_fn;

struct NetCallBacks
{
	enum { FIRST_MAC_OCTET = 0xde }; // has locally administered address bit (0x02) set
	static void RETRO_CALLCONV start(uint16_t client_id, retro_netpacket_send_t send_fn)
	{
		LOG_MSG("[DOSBOXNET] Multiplayer session ready! ClientId: %d", client_id);
		if (!dbp_net) dbp_net = new DBP_Net; else cleanup();
		dbp_net_client_id = client_id;
		dbp_net_addr.ipxnum[0] = 0x0;
		dbp_net_addr.ipxnum[1] = 0x0;
		dbp_net_addr.ipxnum[2] = (Bit8u)(client_id >> 7);
		dbp_net_addr.ipxnum[3] = (Bit8u)(0x1 + (client_id & 127));
		dbp_net_addr.mac[0] = FIRST_MAC_OCTET;
		dbp_net_addr.mac[1] = 0xb0;
		dbp_net_addr.mac[2] = 0xc9;
		dbp_net_addr.mac[3] = 0x00;
		dbp_net_addr.mac[4] = (Bit8u)(client_id >> 8);
		dbp_net_addr.mac[5] = (Bit8u)(client_id & 255);
		dbp_net_connected = true;
		dbp_net_send_fn = send_fn;
		void DBP_EnableNetwork();
		DBP_EnableNetwork();
	}

	static inline Bit16u client_id_from_mac(const Bit8u* mac)
	{
		// Consider unknown first octet to be a multicast of some sort
		return (mac[0] == FIRST_MAC_OCTET ? (Bit16u)((mac[4] << 8) | mac[5]) : (Bit16u)0xFFFF);
	}


	static void RETRO_CALLCONV receive(const void* pkt, size_t pktlen, uint16_t client_id)
	{
		DBP_ASSERT(dbp_net_client_id != client_id); // can't be from myself
		if (pktlen < 1 || pktlen > 65535) { DBP_ASSERT(false); return; }

		//static size_t _sumlen, _sumnum; _sumlen += pktlen; _sumnum++; Bit32u DBP_GetTicks(); static Bit32u lastreport; Bit32u tick = DBP_GetTicks();
		//if (tick - lastreport >= 1000)
		//{
		//	printf("[DOSBOXNET] Received %d bytes in %d packets in 1 second\n", (int)_sumlen, (int)_sumnum);
		//	_sumlen = _sumnum = 0;
		//	lastreport = ((tick - lastreport < 2000) ? (lastreport + 1000) : tick);
		//}

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
				uint16_t src_client_id = client_id_from_mac(src_mac), dest_client_id = client_id_from_mac(dest_mac);
				//printf("[DOSBOXNET] Outgoing Packet - Len: %d - Src: %02x:%02x:%02x:%02x:%02x:%02x (#%d) - Dest: %02x:%02x:%02x:%02x:%02x:%02x (#%d)\n", len, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], src_client_id, dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], dest_client_id);
				if (src_client_id == dest_client_id) { DBP_ASSERT(false); continue; }
				dbp_net_send_fn(RETRO_NETPACKET_RELIABLE, p + 2, len, dest_client_id, (dest_client_id == 0xFFFF));
			}
			dbp_net->OutgoingPackets.clear();
		}
		if (dbp_net->OutgoingModem.size())
		{
			for (Bit8u* p = &dbp_net->OutgoingModem[0], *pEnd = (p++) + dbp_net->OutgoingModem.size(); p < pEnd; p += 1023)
			{
				size_t len = (pEnd - p > 1023 ? (Bit32u)1023 : (Bit32u)(pEnd - p));
				p[-1] = DBP_Net::PKT_MODEM; // prefix with packet type (space reserved by CLibretroModem::transmitByte)
				dbp_net_send_fn(RETRO_NETPACKET_RELIABLE, p-1, len + 1, 0xFFFF, true);
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

const retro_netpacket_callback* DBP_Network_GetCallbacks(void)
{
	static const retro_netpacket_callback packet_callback =
	{
		NetCallBacks::start, NetCallBacks::receive,
		NetCallBacks::stop, NetCallBacks::poll,
		//NetCallBacks::connected, NetCallBacks::connected,
	};
	return &packet_callback;
}
