/*
 *  Copyright (C) 2020-2021 Bernhard Schelling
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
	MidiHandler_retro() : MidiHandler(), boot_buf(NULL) { midi_interface.write = NULL; }
	retro_midi_interface midi_interface;
	std::vector<Bit8u>* boot_buf;

	const char * GetName(void) { return "retro"; };

	static bool RETRO_CALLCONV dummy_flush(void) { return true; }

	bool Open(const char * conf)
	{
		midi_interface.write = NULL;
		extern bool DBP_GetRetroMidiInterface(retro_midi_interface* res);
		if (!conf || strcasecmp(conf, "frontend") || !DBP_GetRetroMidiInterface(&midi_interface) || !midi_interface.write)
			return false;

		if (!midi_interface.flush) midi_interface.flush = &dummy_flush;
		boot_buf = new std::vector<Bit8u>();
		return true;
	};

	void Close(void)
	{
		if (boot_buf)
		{
			delete boot_buf;
			boot_buf = NULL;
		}
		else if (midi_interface.write)
		{
			Bit8u resetchan[] = { 0xB0, 0x7B, 0x00, 0xB0, 0x78, 0x00, 0xFF };
			for (Bit8u i = 0; i != 32; i++)
			{
				midi_interface.write((0xB0 | (i / 2)), 0);
				midi_interface.write(((i & 1) ? 0x78 : 0x7B), 0);
				midi_interface.write(0x00, 0);
				midi_interface.flush();
			}
			midi_interface.write(0xFF, 0); // system reset
			midi_interface.flush();
		}
		midi_interface.write = NULL;
	};

	void PlayMsg(Bit8u * msg)
	{
		if (!midi_interface.write) return;
		static const Bit8u msg_lengths[] = { 3, 3, 3, 3, 2, 2, 3 };
		static const Bit8u ctrl_lengths[] = {  0, 2, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
		Bit8u b = *msg, len = (b < 0x80 ? 0 :  (b >= 0xF0 ? ctrl_lengths[b-0xF0] : msg_lengths[(b>>4)&7]));
		Write(msg, len);
	};

	void PlaySysex(Bit8u * sysex,Bitu len)
	{
		if (!midi_interface.write) return;
		Write(sysex, len);
	}

	void Write(Bit8u* data, Bitu len)
	{
		if (boot_buf)
		{
			if (!boot_buf->empty())
			{
				const Bit8u* buf = &boot_buf->operator[](0);
				if (!midi_interface.write(buf[0], 0)) goto push_to_buf;
				for (size_t i = 1; i != boot_buf->size(); i++)
				{
					midi_interface.write(buf[i], 0);
					midi_interface.flush();
				}
			}
			else if (midi_interface.write(*data, 0))
			{
				len--;
				data++;
			}
			else
			{
				push_to_buf:
				while (len--) boot_buf->push_back(*(data++));
				return;
			}
			delete boot_buf;
			boot_buf = NULL;
		}
		while (len--) midi_interface.write(*(data++), 0);
		midi_interface.flush();
	}
};

static MidiHandler_retro Midi_retro;

bool MIDI_Retro_HasOutputIssue()
{
	return (midi.handler == &Midi_retro && (!Midi_retro.midi_interface.output_enabled || !Midi_retro.midi_interface.output_enabled()));
}
