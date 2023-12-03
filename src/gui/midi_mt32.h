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
#include "support.h"
#include "cross.h"
#include "mt32emu.h"
#include "../dos/drives.h"

static void MIDI_MT32_CallBack(Bitu len);

struct MidiHandler_mt32 : public MidiHandler
{
	MidiHandler_mt32() : MidiHandler(), chan(NULL), mo(NULL), f_control(NULL), f_pcm(NULL), syn(NULL) {}
	MixerChannel*   chan;
	MixerObject*    mo;
	DOS_File*       f_control;
	DOS_File*       f_pcm;
	MT32Emu::Synth* syn;

	const char * GetName(void) { return "mt32"; };

	struct RomFile : public MT32Emu::File
	{
		RomFile(DOS_File* f) : data(NULL), size(0)
		{
			if (!f) return;
			Bit32u begin = 0;
			f->Seek(&size, SEEK_END);
			f->Seek(&begin, SEEK_SET);
			data = new Bit8u[size];
			for (Bit32u sz = size, p = 0; sz;) { Bit16u read = (Bit16u)(sz > 0xFFFF ? 0xFFFF : sz); if (!f->Read(data+p, &read)) break; sz -= read; p += read; }
			f->Close();
			delete f;

			SHA1_CTX ctx;
			SHA1_CTX::SHA1Process(&ctx, (const unsigned char*)data, size);
			unsigned char finalcount[8];
			for (unsigned i = 0; i < 8; i++)  finalcount[i] = (unsigned char)((ctx.count[(i >= 4 ? 0 : 1)] >> ((3-(i & 3)) * 8) ) & 255);
			unsigned char c = 0200;
			SHA1_CTX::SHA1Process(&ctx, &c, 1);
			while ((ctx.count[0] & 504) != 448) { c = 0000; SHA1_CTX::SHA1Process(&ctx, &c, 1); }
			SHA1_CTX::SHA1Process(&ctx, finalcount, 8);
			for (unsigned j = 0; j < 20; j++)
			{
				Bit8u byte = (Bit8u)((ctx.state[j>>2] >> ((3-(j & 3)) * 8)) & 255), nib0 = (byte >> 4), nib1 = (byte & 15);
				sha1Digest[j*2+0] = (nib0 < 10 ? '0' : ('a' - 10)) + nib0;
				sha1Digest[j*2+1] = (nib1 < 10 ? '0' : ('a' - 10)) + nib1;
			}
			sha1Digest[40] = '\0';
		}

		~RomFile() { if (data) delete[] data; }
		void close() {}
		size_t getSize() { return size; }
		const Bit8u *getData() { return data; }
		const SHA1Digest &getSHA1() { return sha1Digest; }

		char sha1Digest[41];
		Bit8u *data;
		Bit32u size;

		struct SHA1_CTX
		{
			SHA1_CTX()
			{
				// Initialize new context with initialization constants
				count[0] = count[1] = 0;
				state[0] = 0x67452301;
				state[1] = 0xEFCDAB89;
				state[2] = 0x98BADCFE;
				state[3] = 0x10325476;
				state[4] = 0xC3D2E1F0;
			}

			static void SHA1Transform(unsigned int* state, const void* buffer)
			{
				// Hash a single 512-bit block. This is the core of the algorithm
				unsigned int block[16];
				memcpy(block, buffer, 64);
				unsigned int a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
				// BLK0() and BLK() perform the initial expand
				// (R0+R1), R2, R3, R4 are the different operations used in SHA1
				#define SHA1ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
				#define SHA1BLK0(i) (block[i] = (SHA1ROL(block[i],24)&0xFF00FF00)|(SHA1ROL(block[i],8)&0x00FF00FF))
				#define SHA1BLK(i) (block[i&15] = SHA1ROL(block[(i+13)&15]^block[(i+8)&15]^block[(i+2)&15]^block[i&15],1))
				#define SHA1R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+SHA1BLK0(i)+0x5A827999+SHA1ROL(v,5);w=SHA1ROL(w,30);
				#define SHA1R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+SHA1BLK(i)+0x5A827999+SHA1ROL(v,5);w=SHA1ROL(w,30);
				#define SHA1R2(v,w,x,y,z,i) z+=(w^x^y)+SHA1BLK(i)+0x6ED9EBA1+SHA1ROL(v,5);w=SHA1ROL(w,30);
				#define SHA1R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+SHA1BLK(i)+0x8F1BBCDC+SHA1ROL(v,5);w=SHA1ROL(w,30);
				#define SHA1R4(v,w,x,y,z,i) z+=(w^x^y)+SHA1BLK(i)+0xCA62C1D6+SHA1ROL(v,5);w=SHA1ROL(w,30);
				// 4 rounds of 20 operations each. Loop unrolled.
				SHA1R0(a,b,c,d,e, 0); SHA1R0(e,a,b,c,d, 1); SHA1R0(d,e,a,b,c, 2); SHA1R0(c,d,e,a,b, 3);
				SHA1R0(b,c,d,e,a, 4); SHA1R0(a,b,c,d,e, 5); SHA1R0(e,a,b,c,d, 6); SHA1R0(d,e,a,b,c, 7);
				SHA1R0(c,d,e,a,b, 8); SHA1R0(b,c,d,e,a, 9); SHA1R0(a,b,c,d,e,10); SHA1R0(e,a,b,c,d,11);
				SHA1R0(d,e,a,b,c,12); SHA1R0(c,d,e,a,b,13); SHA1R0(b,c,d,e,a,14); SHA1R0(a,b,c,d,e,15);
				SHA1R1(e,a,b,c,d,16); SHA1R1(d,e,a,b,c,17); SHA1R1(c,d,e,a,b,18); SHA1R1(b,c,d,e,a,19);
				SHA1R2(a,b,c,d,e,20); SHA1R2(e,a,b,c,d,21); SHA1R2(d,e,a,b,c,22); SHA1R2(c,d,e,a,b,23);
				SHA1R2(b,c,d,e,a,24); SHA1R2(a,b,c,d,e,25); SHA1R2(e,a,b,c,d,26); SHA1R2(d,e,a,b,c,27);
				SHA1R2(c,d,e,a,b,28); SHA1R2(b,c,d,e,a,29); SHA1R2(a,b,c,d,e,30); SHA1R2(e,a,b,c,d,31);
				SHA1R2(d,e,a,b,c,32); SHA1R2(c,d,e,a,b,33); SHA1R2(b,c,d,e,a,34); SHA1R2(a,b,c,d,e,35);
				SHA1R2(e,a,b,c,d,36); SHA1R2(d,e,a,b,c,37); SHA1R2(c,d,e,a,b,38); SHA1R2(b,c,d,e,a,39);
				SHA1R3(a,b,c,d,e,40); SHA1R3(e,a,b,c,d,41); SHA1R3(d,e,a,b,c,42); SHA1R3(c,d,e,a,b,43);
				SHA1R3(b,c,d,e,a,44); SHA1R3(a,b,c,d,e,45); SHA1R3(e,a,b,c,d,46); SHA1R3(d,e,a,b,c,47);
				SHA1R3(c,d,e,a,b,48); SHA1R3(b,c,d,e,a,49); SHA1R3(a,b,c,d,e,50); SHA1R3(e,a,b,c,d,51);
				SHA1R3(d,e,a,b,c,52); SHA1R3(c,d,e,a,b,53); SHA1R3(b,c,d,e,a,54); SHA1R3(a,b,c,d,e,55);
				SHA1R3(e,a,b,c,d,56); SHA1R3(d,e,a,b,c,57); SHA1R3(c,d,e,a,b,58); SHA1R3(b,c,d,e,a,59);
				SHA1R4(a,b,c,d,e,60); SHA1R4(e,a,b,c,d,61); SHA1R4(d,e,a,b,c,62); SHA1R4(c,d,e,a,b,63);
				SHA1R4(b,c,d,e,a,64); SHA1R4(a,b,c,d,e,65); SHA1R4(e,a,b,c,d,66); SHA1R4(d,e,a,b,c,67);
				SHA1R4(c,d,e,a,b,68); SHA1R4(b,c,d,e,a,69); SHA1R4(a,b,c,d,e,70); SHA1R4(e,a,b,c,d,71);
				SHA1R4(d,e,a,b,c,72); SHA1R4(c,d,e,a,b,73); SHA1R4(b,c,d,e,a,74); SHA1R4(a,b,c,d,e,75);
				SHA1R4(e,a,b,c,d,76); SHA1R4(d,e,a,b,c,77); SHA1R4(c,d,e,a,b,78); SHA1R4(b,c,d,e,a,79);
				// Add the working vars back into context.state[]
				state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
			}

			static void SHA1Process(SHA1_CTX* context, const unsigned char* data, size_t len)
			{
				size_t i, j = context->count[0];
				if ((context->count[0] += (len << 3)) < j) context->count[1]++;
				context->count[1] += (len>>29);
				j = (j >> 3) & 63;
				if ((j + len) > 63)
				{
					memcpy(&context->buffer[j], data, (i = 64-j));
					SHA1Transform(context->state, context->buffer);
					for (; i + 63 < len; i += 64) SHA1Transform(context->state, &data[i]);
					j = 0;
				}
				else i = 0;
				memcpy(&context->buffer[j], &data[i], len - i);
			}

			size_t count[2];
			unsigned int state[5];
			unsigned char buffer[64];
		};
	};

	bool Open(const char * conf)
	{
		if (!conf || !*conf) return false;
		size_t conf_len = strlen(conf);
		if (conf_len <= 12 || strcasecmp(conf + conf_len - 4, ".ROM")) return false;

		DBP_ASSERT(!f_control);
		f_control = FindAndOpenDosFile(conf);
		if (!f_control) return false;

		// When using the $ prefix, we are loading a fixed file name from the mounted C drive via FindAndOpenDosFile
		const char* pcmpath = "$C:\\MT32_PCM.ROM";
		std::string pcmpathstr;
		if (*conf != '$')
		{
			// Try to open the matching _PCM file using the same capitalization used for _CONTROL
			char charC = conf[conf_len - 12 + 1], charO = conf[conf_len - 12 + 2];
			pcmpath = pcmpathstr.assign(conf, conf_len - 12 + 1).append(charC == 'C' ? "P" : "p").append(charO == 'O' ? "CM" : "cm").append(conf + conf_len - 4).c_str();
		}

		DBP_ASSERT(!f_pcm);
		f_pcm = FindAndOpenDosFile(pcmpath);
		if (!f_pcm) { f_control->Close(); delete f_control; f_control = NULL; return false; }

		DBP_ASSERT(!mo && !chan);
		mo = new MixerObject;
		chan = mo->Install(&MIDI_MT32_CallBack, MT32Emu::SAMPLE_RATE, "MT32");
		return true;
	};

	void Close(void)
	{
		if (f_control) { f_control->Close(); delete f_control; f_control = NULL; }
		if (f_pcm)     { f_pcm->Close(); delete f_pcm;         f_pcm     = NULL; }
		if (syn)       { syn->close(); delete syn;             syn       = NULL; }
		if (chan)      { chan->Enable(false);                  chan      = NULL; }
		if (mo)        { delete mo;                            mo        = NULL; } // also deletes chan!
	};

	bool LoadSynth()
	{
		if (syn) return true;
		if (!f_control || !f_pcm) return false;

		RomFile control_rom_file(f_control); f_control = NULL;
		RomFile pcm_rom_file(f_pcm);         f_pcm     = NULL;

		syn = new MT32Emu::Synth();
		const MT32Emu::ROMImage *control = MT32Emu::ROMImage::makeROMImage(&control_rom_file), *pcm = MT32Emu::ROMImage::makeROMImage(&pcm_rom_file);
		syn->open(*control, *pcm, MT32Emu::DEFAULT_MAX_PARTIALS, MT32Emu::AnalogOutputMode_ACCURATE);
		MT32Emu::ROMImage::freeROMImage(control);
		MT32Emu::ROMImage::freeROMImage(pcm);

		if (!syn->isOpen())
		{
			delete syn;
			syn = NULL;
			return false;
		}
		chan->SetFreq(syn->getStereoOutputSampleRate());
		chan->Enable(true);
		return true;
	}

	void PlayMsg(Bit8u * msg)
	{
		if (!syn && (!f_control || !LoadSynth())) return;
		Bit32u msg32 = ((Bit32u)(msg[0]) | ((Bit32u)(msg[1]) << 8U) | ((Bit32u)(msg[2]) << 16U) | ((Bit32u)(msg[3]) << 24U));
		syn->playMsg(msg32);
	};

	void PlaySysex(Bit8u * sysex,Bitu len)
	{
		if (!syn && (!f_control || !LoadSynth())) return;
		syn->playSysex(sysex, (Bit32u)len);
	}
};

static MidiHandler_mt32 Midi_mt32;

static void MIDI_MT32_CallBack(Bitu len)
{
	DBP_ASSERT(len <= (MIXER_BUFSIZE/4));
	if (len > (MIXER_BUFSIZE/4)) len = (MIXER_BUFSIZE/4);
	Midi_mt32.syn->render((Bit16s*)MixTemp, (Bit32u)len);
	Midi_mt32.chan->AddSamples_s16(len, (Bit16s*)MixTemp);
}
