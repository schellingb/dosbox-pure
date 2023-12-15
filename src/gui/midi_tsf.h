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

#include "mixer.h"

#define STB_VORBIS_HEADER_ONLY
#include "../dos/stb_vorbis.inl"
#include "../dos/drives.h"

#define TSF_IMPLEMENTATION
#define TSF_STATIC
#define TSF_NO_STDIO
#include "tsf.h"

static void MIDI_TSF_CallBack(Bitu len);

struct MidiHandler_tsf : public MidiHandler
{
	MidiHandler_tsf() : MidiHandler(), chan(NULL), mo(NULL), f(NULL), sf(NULL) {}
	MixerChannel* chan;
	MixerObject*  mo;
	DOS_File*     f;
	tsf*          sf;

	const char * GetName(void) { return "tsf"; };

	bool Open(const char * conf)
	{
		if (!conf || !*conf) return false;
		size_t conf_len = strlen(conf);
		if (conf_len <= 4 || strncasecmp(conf + conf_len - 4, ".sf", 3)) return false;

		DBP_ASSERT(!f);
		f = FindAndOpenDosFile(conf);
		if (!f) return false;

		DBP_ASSERT(!chan);
		mo = new MixerObject;
		extern Bit32u DBP_MIXER_GetFrequency();
		chan = mo->Install(&MIDI_TSF_CallBack, DBP_MIXER_GetFrequency(), "TSF");

		return true;
	};

	void Close(void)
	{
		if (f)      { f->Close();delete f; f      = NULL; }
		if (sf)     { tsf_close(sf);       sf     = NULL; }
		if (chan)   { chan->Enable(false); chan   = NULL; }
		if (mo)     { delete mo;           mo     = NULL; } // also deletes chan!
	};

	static int tsf_stream_dosfile_skip(DOS_File* f, unsigned int count) { return !!f->Seek(&count, DOS_SEEK_CUR); }
	static int tsf_stream_dosfile_read(DOS_File* f, Bit8u* p, unsigned int sz)
	{
		for (Bit16u read; sz; sz -= read, p += read) { read = (Bit16u)(sz > 0xFFFF ? 0xFFFF : sz); if (!f->Read(p, &read) || !read) return false; }
		return true;
	}

	bool LoadFont()
	{
		if (sf) return true;
		if (!f) return false;
		struct tsf_stream stream = { f, (int(*)(void*,void*,unsigned int))&tsf_stream_dosfile_read, (int(*)(void*,unsigned int))&tsf_stream_dosfile_skip };
		sf = tsf_load(&stream);
		f->Close();
		delete f;
		f = NULL;
		if (!sf) return false;

		extern Bit32u DBP_MIXER_GetFrequency();
		tsf_set_output(sf, TSF_STEREO_INTERLEAVED, (int)DBP_MIXER_GetFrequency(), 0.0);
		chan->Enable(true);
		return true;
	}

	void PlayMsg(Bit8u * msg)
	{
		if (!sf && (!f || !LoadFont())) return;

		Bit8u channel = (msg[0] & 0x0f);
//		if (channel == 2 || channel == 3 || channel == 4)
		switch (msg[0] & 0xf0)
		{
			case 0xC0: //channel program (preset) change (special handling for 10th MIDI channel with drums)
//				printf("[MIDI] Channel %2d PRESET %3d\n", channel, msg[1]);
				tsf_channel_set_presetnumber(sf, channel, msg[1], (channel == 9));
				break;
			case 0x90: //play a note
//				printf("[MIDI] Channel %2d NOTE %3d AT VEL %3d\n", channel, msg[1], msg[2]);
				tsf_channel_note_on(sf, channel, msg[1], msg[2] / 127.0f); //1);//TSF_POWF(msg[2] / 127.0f, .5f));
				break;
			case 0x80: //stop a note
				tsf_channel_note_off(sf, channel, msg[1]);
				break;
			case 0xE0: //pitch wheel modification
				tsf_channel_set_pitchwheel(sf, channel, ((msg[2] & 0x7f) << 7) | msg[1]);
				break;
			case 0xB0: //MIDI controller messages
//				printf("[MIDI] Channel %2d CONTROLLER %3d - %3d\n", channel, msg[1], msg[2]);
				tsf_channel_midi_control(sf, channel, msg[1], msg[2]);
				break;
		}
	};

	void PlaySysex(Bit8u * sysex,Bitu len)
	{
		// Some samples:
		// F0 41 10 42 12 40 00 7F 00 41 F7 // GS RESET
		// F0 41 10 16 12 7F 01 F7 // RESET
		// F0 43 10 4C 00 00 7E 00 F7 //XG RESET
		// F0 7E 7F 09 01 F7 //GM RESET
		// 00 00 00 00 00 00 // DOOM reset?
//		fprintf(stderr, "[SYSEX]");
//		for (Bitu i = 0; i != len; i++) fprintf(stderr, " %02X", sysex[len]);
//		fprintf(stderr, "\n");
	}
};

static MidiHandler_tsf Midi_tsf;

static void MIDI_TSF_CallBack(Bitu len)
{
	DBP_ASSERT(len <= (MIXER_BUFSIZE/4));
	if (len > (MIXER_BUFSIZE/4)) len = (MIXER_BUFSIZE/4);
	tsf_render_short(Midi_tsf.sf, (Bit16s*)MixTemp, (int)len, 0);
	Midi_tsf.chan->AddSamples_s16(len, (Bit16s*)MixTemp);
}

bool MIDI_TSF_SwitchSF(const char* path)
{
	if (midi.handler != &Midi_tsf) return false;

	Midi_tsf.Close();
	if (!Midi_tsf.Open(path)) return false;

	void DBP_MIDI_ReplayCache();
	DBP_MIDI_ReplayCache();

	return true;
}
