/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *  Copyright (C) 2023 Bernhard Schelling
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

#ifndef DOSBOX_DBP_NETWORK_H
#define DOSBOX_DBP_NETWORK_H

#include "config.h"

#ifdef C_DBP_ENABLE_LIBRETRO_MODEM
#include "serialport.h"

// Based on CNullModem
struct CLibretroModem : public CSerial
{
	CLibretroModem(Bitu id, CommandLine* cmd);
	virtual ~CLibretroModem();
	virtual void handleUpperEvent(Bit16u type);
	virtual void updateMSR();
	virtual void setRTSDTR(bool rts, bool dtr);
	virtual void setRTS(bool val);
	virtual void setDTR(bool val);
	virtual void transmitByte(Bit8u val, bool first);
	virtual void setBreak(bool value);
	virtual void updatePortConfig(Bit16u divider, Bit8u lcr);
	void doSend(Bit8u val, bool is_escape);
	bool doReceive();

	Bit8u recvbuf[128], recvbufofs, recvbuflen;
	Bit8u rx_state;
	bool tx_block;		// true while the SERIAL_TX_REDUCTION event is pending
	Bitu rx_retry;		// counter of retries
	Bitu rx_retry_max;	// how many POLL_EVENTS to wait before causing a overrun error.
	Bitu tx_gather;		// how long to gather tx data before sending all of them [milliseconds]
	bool transparent;	// if true, don't send 0xff 0xXX to toggle DSR/CTS.
};
#endif

#endif
