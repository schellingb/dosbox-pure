/*
 *  Copyright (C) 2020 Bernhard Schelling
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

#include "../../libretro-common/include/libretro.h"

struct MidiHandler_retro : public MidiHandler
{
	MidiHandler_retro() : MidiHandler() { midi_interface.write = NULL; }
	retro_midi_interface midi_interface;

	const char * GetName(void) { return "retro"; };

	bool Open(const char * conf)
	{
		if (conf && !strcasecmp(conf, "frontend"))
		{
			midi_interface.output_enabled = NULL;
			extern void DBP_GetRetroMidiInterface(retro_midi_interface* res);
			DBP_GetRetroMidiInterface(&midi_interface);
			if (midi_interface.output_enabled && midi_interface.output_enabled())
				return true;
		}
		midi_interface.write = NULL;
		return false;
	};

	void Close(void)
	{
		if (!midi_interface.write) return;
		Bit8u resetchan[] = { 0xB0, 0x7B, 0x00, 0xB0, 0x78, 0x00, 0xFF };
		for (Bit8u chan = 0; chan != 16; chan++)
		{
			resetchan[0] = resetchan[3] = 0xB0 | chan;
			for (Bit8u b : resetchan) midi_interface.write(b, 0);
		}
		midi_interface.write(0xFF, 0); // system reset
		midi_interface.flush();
		midi_interface.write = NULL;
	};

	void PlayMsg(Bit8u * msg)
	{
		if (!midi_interface.write) return;
		static const Bit8u msg_lengths[] = { 3, 3, 3, 3, 2, 2, 3 };
		static const Bit8u ctrl_lengths[] = {  0, 2, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
		Bit8u b = *msg, len = (b < 0x80 ? 0 :  (b >= 0xF0 ? ctrl_lengths[b-0xF0] : msg_lengths[(b>>4)&7]));
		while (len--) midi_interface.write(*(msg++), 0);
		midi_interface.flush();
	};

	void PlaySysex(Bit8u * sysex,Bitu len)
	{
		if (!midi_interface.write) return;
		while (len--) midi_interface.write(*(sysex++), 0);
		midi_interface.flush();
	}
};

static MidiHandler_retro Midi_retro;

bool MIDI_Retro_IsActiveHandler()
{
	return (midi.handler == &Midi_retro);
}
