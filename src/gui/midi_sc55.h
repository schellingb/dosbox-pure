/*
 *  Copyright (C) 2026 Bernhard Schelling
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
#include "../dos/drives.h"

static void MIDI_SC55_CallBack(Bitu len);

struct MidiHandler_sc55 : public MidiHandler
{
	enum { NROM1, NROM2, NROMSM, NROMWAVE1, NROMWAVE2, NROMWAVE3, MAXROMS };
	enum : Bit32u { ROM1_SIZE = 32768, ROM2MK2_SIZE = 524288, ROM2_SIZE = 262144, WAVEROM8MBIT_SIZE = 1048576, WAVEROM16MBIT_SIZE = 2097152, ROMSM_SIZE = 4096 };
	enum : Bit8u { ROM1_BYTE67_MK2_OR_ST = 0x12, ROM1_BYTE67_SC55_OR_SC155 = 0x50, ROM1_BYTE67_CM300_OR_SCC1 = 0x74, ROM1_BYTE67_JV880 = 0x7B };
	MidiHandler_sc55() : MidiHandler(), chan(NULL), mo(NULL), d_zip(NULL), loaded(false) { memset(roms, 0, sizeof(roms)); }
	MixerChannel* chan;
	MixerObject*  mo;
	DOS_File*     roms[MAXROMS];
	DOS_Drive*    d_zip;
	bool loaded, is_mk1, is_cm300, is_jv880, is_scb55, is_sc155;

	const char * GetName(void) { return "sc55"; };

	DOS_File** GetROMFile(const char* path, Bit32u size)
	{
		if      ((size == ROM1_SIZE)                         && !roms[NROM1] ) return &roms[NROM1];
		else if ((size == ROM2_SIZE || size == ROM2MK2_SIZE) && !roms[NROM2] ) return &roms[NROM2];
		else if ((size == ROMSM_SIZE)                        && !roms[NROMSM]) return &roms[NROMSM];
		else if (size != WAVEROM8MBIT_SIZE && size != WAVEROM16MBIT_SIZE) return NULL;
		for (const char* p = path + strlen(path) - 1; p >= path && *p != '/' && *p != '\\'; p--)
			{ if (int n = (*p == '1') ? NROMWAVE1 : (*p == '2') ? NROMWAVE2 : (*p == '3') ? NROMWAVE3 : 0) { if (!roms[n]) { return &roms[n]; } } }
		return NULL;
	}

	static void IterateZip(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
	{
		MidiHandler_sc55& self = *(MidiHandler_sc55*)data;
		if (DOS_File** pf = self.GetROMFile(path, size))
			if (self.d_zip->FileOpen(pf, (char*)path, OPEN_READ))
				(*pf)->AddRef();
	}

	bool Open(const char * conf)
	{
		if (!conf || !*conf) return false;
		const int conf_len = (int)strlen(conf);
		if (conf[0] == '^' && conf[1] == 'S') // a path to a ZIP on the host file system
		{
			FILE* DBP_FileOpenContentOrSystem(const char* fname);
			FILE* zip_file_h = DBP_FileOpenContentOrSystem(conf + 2);
			if (!zip_file_h) return false;
			d_zip = new zipDrive(new rawFile(zip_file_h, false));
			DriveFileIterator(d_zip, IterateZip, (Bitu)this);
		}
		else
		{
			if (conf_len < 8 || strcasecmp(conf + conf_len - 8, "ROM1.BIN")) return false; // path is to ROM1.BIN

			Bit32u rom1_size;
			roms[NROM1] = FindAndOpenDosFile(conf, &rom1_size);
			if (!roms[NROM1] || rom1_size != ROM1_SIZE) return false;

			std::string pathstr;
			const int rPos = conf_len - 8;
			const char rCap = (conf[rPos] & 0x20), oCap = (conf[rPos+1] & 0x20);
			const char* romfns[] = { "ROM2", "ROM2_ST", "ROM_SM", "WAVEROM1", "WAVEROM2", "WAVEROM3" };
			for (const char* romfn : romfns)
			{
				// Try to open matching file using the same capitalization used for ROM1.BIN
				char* path = &pathstr.assign(conf, rPos).append(romfn).append(conf + conf_len - 4)[0];
				for (int i = 0; romfn[i]; i++) { if (romfn[i] >= 'A' && romfn[i] <= 'Z') { path[rPos+i] |= (i ? oCap : rCap); } }

				Bit32u romsize;
				DOS_File* f = FindAndOpenDosFile(path, &romsize);
				if (!f) continue;
				DOS_File** pf = GetROMFile(path, romsize);
				if (!pf) { f->Close(); delete f; continue; }
				*pf = f;
			}
		}
		if (!roms[NROM1] || !roms[NROM2]) { Close(); return false; }

		Bit32u spos, romsizes[MAXROMS];
		Bit16u rsize;
		Bit8u rom1byte67 = 0;
		roms[NROM1]->Seek(&(spos = 67), DOS_SEEK_SET);
		roms[NROM1]->Read(&rom1byte67, &(rsize = 1));
		for (int i = 1; i != MAXROMS; i++) { if (!roms[i] || !roms[i]->Seek(&(romsizes[i] = 0), DOS_SEEK_END)) { romsizes[i] = 0; } }

		// Find the name of the directory (if conf references ROM1.BIN) or the ZIP package (if conf starts with ^)
		const char* pConfName = conf + conf_len;
		while (pConfName > conf && pConfName[-1] != '\\' && pConfName[-1] != '/') pConfName--;
		if (conf[0] != '^' && pConfName > conf) { pConfName--; while (pConfName > conf && pConfName[-1] != '\\' && pConfName[-1] != '/') { pConfName--; } }

		bool nameHas155 = (strstr(pConfName, "155") != NULL), invalid = false;
		is_mk1 = is_cm300 = is_jv880 = is_scb55 = is_sc155 = false;
		switch (rom1byte67)
		{
			case ROM1_BYTE67_MK2_OR_ST:
				is_sc155 = nameHas155;
				invalid = (romsizes[NROMSM] != ROMSM_SIZE || romsizes[NROMWAVE1] != WAVEROM16MBIT_SIZE || romsizes[NROMWAVE2] != WAVEROM8MBIT_SIZE);
				break;
			case ROM1_BYTE67_SC55_OR_SC155:
				is_mk1 = true;
				is_sc155 = nameHas155;
				invalid = (romsizes[NROM2] != ROM2_SIZE || romsizes[NROMWAVE1] != WAVEROM8MBIT_SIZE || romsizes[NROMWAVE2] != WAVEROM8MBIT_SIZE || romsizes[NROMWAVE3] != WAVEROM8MBIT_SIZE);
				break;
			case ROM1_BYTE67_CM300_OR_SCC1:
				is_mk1 = is_cm300 = true;
				invalid = (romsizes[NROM2] != ROM2_SIZE || romsizes[NROMWAVE1] != WAVEROM8MBIT_SIZE || romsizes[NROMWAVE2] != WAVEROM8MBIT_SIZE || romsizes[NROMWAVE3] != WAVEROM8MBIT_SIZE);
				break;
			case ROM1_BYTE67_JV880:
				is_jv880 = true;
				invalid = (romsizes[NROM2] != ROM2_SIZE || romsizes[NROMWAVE1] != WAVEROM16MBIT_SIZE || romsizes[NROMWAVE2] != WAVEROM16MBIT_SIZE);
				break;
			default: // SCB-55 (aka RLP-3194) or RLP-3237
				is_scb55 = true;
				invalid = (romsizes[NROMWAVE1] != WAVEROM16MBIT_SIZE);
				break;
		}
		if (invalid) { DBP_ASSERT(false); Close(); return false; }

		DBP_ASSERT(!mo && !chan);
		mo = new MixerObject;
		chan = mo->Install(&MIDI_SC55_CallBack, ((is_mk1 || is_jv880) ? 64000 : 66207) / 2, "SC55");
		return true;
	}

	void Close(void)
	{
		for (DOS_File*& f : roms) { if (f) { f->Close(); delete f; f = NULL; } }
		if (d_zip)  { delete d_zip;        d_zip = NULL; }
		if (chan)   { chan->Enable(false); chan  = NULL; }
		if (mo)     { delete mo;           mo    = NULL; } // also deletes chan!
		if (loaded) { extern void NUKEDSC55_Shutdown(); NUKEDSC55_Shutdown(); loaded = false; }
	}

	bool LoadSynth()
	{
		struct ReadRom
		{
			ReadRom(DOS_File*& f) : data(NULL), size(0)
			{
				if (!f) return;
				Bit32u begin = 0;
				f->Seek(&size, SEEK_END);
				f->Seek(&begin, SEEK_SET);
				data = new Bit8u[size];
				for (Bit32u sz = size, p = 0; sz;) { Bit16u read = (Bit16u)(sz > 0xFFFF ? 0xFFFF : sz); if (!f->Read(data+p, &read)) break; sz -= read; p += read; }
				f->Close();
				delete f;
				f = NULL;
			}
			~ReadRom() { delete[] data; }
			Bit8u *data;
			Bit32u size;
		};

		ReadRom rom1(roms[NROM1]), rom2(roms[NROM2]), romsm(roms[NROMSM]), romwave1(roms[NROMWAVE1]), romwave2(roms[NROMWAVE2]), romwave3(roms[NROMWAVE3]);
		extern unsigned NUKEDSC55_Init(bool _mk1, bool _cm300, bool _jv880, bool _scb55, bool _sc155, uint8_t* pRom1, uint8_t* pRom2, int rom2size, uint8_t* pRomSM, uint8_t* pRomWave1, uint8_t* pRomWave2, uint8_t* pRomWave3);
		chan->SetFreq(NUKEDSC55_Init(is_mk1, is_cm300, is_jv880, is_scb55, is_sc155, rom1.data, rom2.data, (int)rom2.size, romsm.data, romwave1.data, romwave2.data, romwave3.data));
		chan->Enable(true);
		loaded = true;
		return true;
	}

	void PlayMsg(Bit8u * msg)
	{
		if (!loaded && (!roms[NROM1] || !LoadSynth())) return;
		extern void MCU_PostUART(uint8_t data);
		const Bit8u b1 = msg[0];
		switch (b1 & 0xf0)
		{
			case 0x80:case 0x90:case 0xa0:case 0xb0:case 0xe0:
				MCU_PostUART(b1);
				MCU_PostUART(msg[1]);
				MCU_PostUART(msg[2]);
				break;
			case 0xc0:case 0xd0:
				MCU_PostUART(b1);
				MCU_PostUART(msg[1]);
				break;
		}
	}

	void PlaySysex(Bit8u * sysex,Bitu len)
	{
		if (!loaded && (!roms[NROM1] || !LoadSynth())) return;
		extern void MCU_PostUART(uint8_t data);
		while (len--) MCU_PostUART(*(sysex++));
	}
};

static MidiHandler_sc55 Midi_sc55;

static void MIDI_SC55_CallBack(Bitu len)
{
	DBP_ASSERT(len <= (MIXER_BUFSIZE/4));
	if (len > (MIXER_BUFSIZE/4)) len = (MIXER_BUFSIZE/4);
	extern void NUKEDSC55_Render(short* buf, uint32_t len);
	NUKEDSC55_Render((short*)MixTemp, len*2);
	Midi_sc55.chan->AddSamples_s16(len, (Bit16s*)MixTemp);
}
