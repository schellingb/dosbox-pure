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

// Based on CSoftModem and CNullModem
struct CModemBuf { enum { BUFSIZE = 1024 }; Bit8u buf[BUFSIZE]; Bit16u p_in, p_out; }; // If queue size is too high you overflow terminal clients buffers i think
struct CLibretroDualModem : public CSerial
{
	CLibretroDualModem(Bitu id, CommandLine* cmd);
	virtual void handleUpperEvent(Bit16u type);
	virtual void updateMSR();
	virtual void setRTSDTR(bool rts, bool dtr);
	virtual void setRTS(bool val);
	virtual void setDTR(bool val);
	virtual void transmitByte(Bit8u val, bool first);
	virtual void setBreak(bool value);
	virtual void updatePortConfig(Bit16u divider, Bit8u lcr);

	void SM_SetState(bool set_connected, bool do_reset = false);
	void SM_SendRes(int response);
	void SM_DoCommand();
	void SM_Poll();
	void NM_DoSend(Bit8u val, bool is_escape);
	bool NM_DoReceive();

	enum { MODE_DETECT, MODE_SOFTMODEM, MODE_NULLMODEM } mode;

	CModemBuf rbuf, tbuf;
	Bit8u tmpbuf[CModemBuf::BUFSIZE];

	enum { SREGS = 100 };
	struct {
		char cmdbuf[128];
		bool commandmode, echo, numericresponse, ringing, connected;
		Bit8u reg[SREGS], doresponse, plusinc, waiting_tx_character, cmdpos, flowcontrol;
		Bit16u cmdpause, ringtimer;
	} sm;
	struct {
		Bit8u rx_state;
		bool transparent;    // if true, don't send 0xff 0xXX to toggle DSR/CTS.
		Bit16u rx_retry;     // counter of retries
		Bit16u rx_retry_max; // how many POLL_EVENTS to wait before causing a overrun error.
		//bool tx_block;       // true while the SERIAL_TX_REDUCTION event is pending
		//Bit16u tx_gather;    // how long to gather tx data before sending all of them [milliseconds]
	} nm;
};

typedef CLibretroDualModem CLibretroModem;
#endif

#endif
