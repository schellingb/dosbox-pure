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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <algorithm>

#ifdef C_DBP_USE_SDL
#include "SDL.h"
#endif

#include "dosbox.h"
#include "midi.h"
#include "cross.h"
#include "support.h"
#include "setup.h"
#include "mapper.h"
#include "pic.h"
#include "hardware.h"
#include "timer.h"

#define RAWBUF	1024

Bit8u MIDI_evt_len[256] = {
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x00
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x10
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x20
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x30
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x40
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x50
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x60
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x70

  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x80
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x90
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xa0
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xb0

  2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xc0
  2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xd0

  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xe0

  0,2,3,2, 0,0,1,0, 1,0,1,1, 1,0,1,0   // 0xf0
};

MidiHandler * handler_list = 0;

MidiHandler::MidiHandler(){
	next = handler_list;
	handler_list = this;
};


#ifdef C_DBP_SUPPORT_MIDI_TSF
#include "midi_tsf.h"
#endif

#ifdef C_DBP_SUPPORT_MIDI_MT32
#include "midi_mt32.h"
#endif

#ifdef C_DBP_SUPPORT_MIDI_RETRO
#include "midi_retro.h"
#endif

#ifdef C_DBP_SUPPORT_MIDI_ADLIB
#include "midi_opl.h"
#endif

#if !defined(C_DBP_SUPPORT_MIDI_TSF) && !defined(C_DBP_SUPPORT_MIDI_RETRO)
MidiHandler Midi_none;
#endif

#ifdef C_DBP_NATIVE_MIDI
/* Include different midi drivers, lowest ones get checked first for default.
   Each header provides an independent midi interface. */

#if defined(MACOSX)

#if defined(C_SUPPORTS_COREMIDI)
#include "midi_coremidi.h"
#endif

#if defined(C_SUPPORTS_COREAUDIO)
#include "midi_coreaudio.h"
#endif

#elif defined (WIN32)

#include "midi_win32.h"

#else

#include "midi_oss.h"

#endif

#if defined (HAVE_ALSA)

#include "midi_alsa.h"

#endif
#endif /* C_DBP_NATIVE_MIDI */


DB_Midi midi;

void MIDI_RawOutByte(Bit8u data) {
	if (midi.sysex.start) {
		Bit32u passed_ticks = GetTicks() - midi.sysex.start;
		if (passed_ticks < midi.sysex.delay) {
#ifdef C_DBP_USE_SDL
			SDL_Delay(midi.sysex.delay - passed_ticks);
#else
			void DBP_MidiDelay(Bit32u ms);
			DBP_MidiDelay((Bit32u)(midi.sysex.delay - passed_ticks));
#endif
		}
	}

	/* Test for a realtime MIDI message */
	if (data>=0xf8) {
		midi.rt_buf[0]=data;
		midi.handler->PlayMsg(midi.rt_buf);
		return;
	}
	/* Test for a active sysex tranfer */
	if (midi.status==0xf0) {
		if (!(data&0x80)) {
			if (midi.sysex.used<(SYSEX_SIZE-1)) midi.sysex.buf[midi.sysex.used++] = data;
			return;
		} else {
			midi.sysex.buf[midi.sysex.used++] = 0xf7;

			if ((midi.sysex.start) && (midi.sysex.used >= 4) && (midi.sysex.used <= 9) && (midi.sysex.buf[1] == 0x41) && (midi.sysex.buf[3] == 0x16)) {
				LOG(LOG_ALL,LOG_ERROR)("MIDI:Skipping invalid MT-32 SysEx midi message (too short to contain a checksum)");
			} else {
//				LOG(LOG_ALL,LOG_NORMAL)("Play sysex; address:%02X %02X %02X, length:%4d, delay:%3d", midi.sysex.buf[5], midi.sysex.buf[6], midi.sysex.buf[7], midi.sysex.used, midi.sysex.delay);
				midi.handler->PlaySysex(midi.sysex.buf, midi.sysex.used);
				if (midi.sysex.start) {
					if (midi.sysex.buf[5] == 0x7F) {
						midi.sysex.delay = 290; // All Parameters reset
					} else if (midi.sysex.buf[5] == 0x10 && midi.sysex.buf[6] == 0x00 && midi.sysex.buf[7] == 0x04) {
						midi.sysex.delay = 145; // Viking Child
					} else if (midi.sysex.buf[5] == 0x10 && midi.sysex.buf[6] == 0x00 && midi.sysex.buf[7] == 0x01) {
						midi.sysex.delay = 30; // Dark Sun 1
					} else midi.sysex.delay = (Bitu)(((float)(midi.sysex.used) * 1.25f) * 1000.0f / 3125.0f) + 2;
					midi.sysex.start = GetTicks();
				}
			}

			LOG(LOG_ALL,LOG_NORMAL)("Sysex message size %d", static_cast<int>(midi.sysex.used));
#ifdef C_DBP_ENABLE_CAPTURE
			if (CaptureState & CAPTURE_MIDI) {
				CAPTURE_AddMidi( true, midi.sysex.used-1, &midi.sysex.buf[1]);
			}
#endif
		}
	}
	if (data&0x80) {
		midi.status=data;
		midi.cmd_pos=0;
		midi.cmd_len=MIDI_evt_len[data];
		if (midi.status==0xf0) {
			midi.sysex.buf[0]=0xf0;
			midi.sysex.used=1;
		}
	}
	if (midi.cmd_len) {
		midi.cmd_buf[midi.cmd_pos++]=data;
		if (midi.cmd_pos >= midi.cmd_len) {
#ifdef C_DBP_ENABLE_CAPTURE
			if (CaptureState & CAPTURE_MIDI) {
				CAPTURE_AddMidi(false, midi.cmd_len, midi.cmd_buf);
			}
#endif
			//DBP: Update MIDI cache to restore channels on deserialization
			Bit8u channel = (midi.cmd_buf[0] & 0x0f), data1 = midi.cmd_buf[1], rpn;
			switch (midi.cmd_buf[0] & 0xf0)
			{
				case 0xC0: //channel program (preset) change (special handling for 10th MIDI channel with drums)
					midi.cache[channel].preset_bank[0] = midi.cache[channel].control[0];
					midi.cache[channel].preset_bank[1] = midi.cache[channel].control[32];
					midi.cache[channel].preset = 1 + data1;
					break;
				case 0xE0: //pitch wheel modification
					memcpy(midi.cache[channel].pitch_tuning, midi.cache[channel].rpn_data, sizeof(midi.cache[0].pitch_tuning));
					midi.cache[channel].pitch[0] = 1 + data1;
					midi.cache[channel].pitch[1] = midi.cmd_buf[2];
					break;
				case 0xB0: //MIDI controller messages
					if (data1 < (sizeof(midi.cache[0].control)/sizeof(midi.cache[0].control[0])))
						midi.cache[channel].control[data1] = 1 + midi.cmd_buf[2];
					switch (data1)
					{
						case 0: //bank select MSB
							midi.cache[channel].control[32] = 0; // Bank select MSB clears LSB!
							break;
						case 6: case 38: // data entry MSB / LSB
							rpn = (midi.cache[channel].rpn[0] > 1 ? 0xFF : 0) | (midi.cache[channel].rpn[1] ? (midi.cache[channel].rpn[1] - 1) : (midi.cache[channel].rpn[0] == 1 ? 0 : 0xFF));
							if (rpn > 2) break;
							midi.cache[channel].rpn_data[rpn][0] = midi.cache[channel].control[6];
							midi.cache[channel].rpn_data[rpn][1] = midi.cache[channel].control[38];
							break;
						case 100: // registered parameter number LSB
							midi.cache[channel].rpn[1] = 1 + midi.cmd_buf[2];
							break;
						case 101: // registered parameter number MSB
							midi.cache[channel].rpn[0] = 1 + midi.cmd_buf[2];
							break;
						case 98: case 99: // NRPM clears RPN
							midi.cache[channel].rpn[0] = midi.cache[channel].rpn[1] = 0;
							break;
						case 121: // all controls off
							memset(midi.cache[channel].control, 0, sizeof(midi.cache[0].control));
							memset(midi.cache[channel].rpn, 0, sizeof(midi.cache[0].rpn));
							memset(midi.cache[channel].rpn_data, 0, sizeof(midi.cache[0].rpn_data));
							break;
					}
					break;
			}
			if (!midi.ever_used) midi.ever_used=true; /* DBP: for serialization */
			midi.handler->PlayMsg(midi.cmd_buf);
			midi.cmd_pos=1;		//Use Running status
		}
	}
}

bool MIDI_Available(void)  {
	return midi.available;
}

class MIDI:public Module_base{
public:
	MIDI(Section* configuration):Module_base(configuration){
		Section_prop * section=static_cast<Section_prop *>(configuration);
		const char * dev=section->Get_string("mididevice");
		std::string fullconf=section->Get_string("midiconfig");
		/* If device = "default" go for first handler that works */
		MidiHandler * handler;
//		MAPPER_AddHandler(MIDI_SaveRawEvent,MK_f8,MMOD1|MMOD2,"caprawmidi","Cap MIDI");
		midi.sysex.delay = 0;
		midi.sysex.start = 0;
		if (fullconf.find("delaysysex") != std::string::npos) {
			midi.sysex.start = GetTicks();
			fullconf.erase(fullconf.find("delaysysex"));
			LOG_MSG("MIDI: Using delayed SysEx processing");
		}
		trim(fullconf);
		const char * conf = fullconf.c_str();
		midi.status=0x00;
		midi.cmd_pos=0;
		midi.cmd_len=0;
		if (!strcasecmp(dev,"default")) goto getdefault;
		handler=handler_list;
		while (handler) {
			if (!strcasecmp(dev,handler->GetName())) {
				if (!handler->Open(conf)) {
					LOG_MSG("MIDI: Can't open device:%s with config:%s.",dev,conf);
					goto getdefault;
				}
				midi.handler=handler;
				midi.available=true;
				LOG_MSG("MIDI: Opened device:%s",handler->GetName());
				return;
			}
			handler=handler->next;
		}
		LOG_MSG("MIDI: Can't find device:%s, finding default handler.",dev);
getdefault:
		handler=handler_list;
		while (handler) {
			if (handler->Open(conf)) {
				midi.available=true;
				midi.handler=handler;
				LOG_MSG("MIDI: Opened device:%s",handler->GetName());
				return;
			}
			handler=handler->next;
		}
		/* This shouldn't be possible */
	}
	~MIDI(){
		if(midi.available) midi.handler->Close();
		midi.available = false;
		midi.handler = 0;
		//DBP: Added for serialization
		extern bool DBP_IsShuttingDown();
		if (DBP_IsShuttingDown()) midi.ever_used = false;
	}
};


static MIDI* test;
void MIDI_Destroy(Section* /*sec*/){
	delete test;
}
void MIDI_Init(Section * sec) {
	test = new MIDI(sec);
	sec->AddDestroyFunction(&MIDI_Destroy,true);
	//DBP: Added support for switching MIDI at runtime
	if (midi.ever_used) {
		extern void DBP_MIDI_ReplayCache();
		DBP_MIDI_ReplayCache();
	}
}

void DBP_MIDI_ReplayCache()
{
	if (!midi.handler) return;
	struct Local
	{
		static void PlayControl(Bit8u ch, Bit8u ctrl, Bit8u cache_val)
		{
			if (!cache_val) return;
			Bit8u cmd[3] = { (Bit8u)(0xB0 | ch), ctrl, (Bit8u)(cache_val - 1) };
			midi.handler->PlayMsg(cmd);
		}
		static void PlayRPN(Bit8u ch, Bit8u cache_rpn_data[3][2])
		{
			for (Bit8u rpn = 0; rpn != 3; rpn++)
			{
				if (!cache_rpn_data[rpn][0] && !cache_rpn_data[rpn][1]) continue;
				PlayControl(ch, 101, 1); // registered parameter number MSB
				PlayControl(ch, 100, 1 + rpn); // registered parameter number LSB
				PlayControl(ch,  6, cache_rpn_data[rpn][0]); // data entry MSB
				PlayControl(ch, 38, cache_rpn_data[rpn][1]); // data entry LSB
			}
		}
	};
	for (Bit8u ch = 0; ch != 16; ch++)
	{
		Local::PlayControl(ch, 123, 1); // ALL_NOTES_OFF
		Local::PlayControl(ch, 120, 1); // ALL_SOUND_OFF
		Local::PlayControl(ch, 121, 1); // ALL_CTRL_OFF
		if (midi.cache[ch].preset)
		{
			Local::PlayControl(ch,  0, midi.cache[ch].preset_bank[0]); // BANK MSB
			Local::PlayControl(ch, 32, midi.cache[ch].preset_bank[1]); // BANK LSB
			Bit8u cmd[2] = { (Bit8u)(0xC0 | ch), (Bit8u)(midi.cache[ch].preset - 1) };
			midi.handler->PlayMsg(cmd);
		}
		if (midi.cache[ch].pitch[0])
		{
			Local::PlayRPN(ch, midi.cache[ch].pitch_tuning);
			Bit8u cmd[3] = { (Bit8u)(0xE0 | ch), (Bit8u)(midi.cache[ch].pitch[0] - 1), midi.cache[ch].pitch[1] };
			midi.handler->PlayMsg(cmd);
		}
		Local::PlayRPN(ch, midi.cache[ch].rpn_data);
		Local::PlayControl(ch, 101, midi.cache[ch].rpn[0]);
		Local::PlayControl(ch, 100, midi.cache[ch].rpn[1]);
		for (Bit8u ctrl = 0; ctrl != (sizeof(midi.cache[0].control)/sizeof(midi.cache[0].control[0])); ctrl++)
			Local::PlayControl(ch, ctrl, midi.cache[ch].control[ctrl]);
	}
}
