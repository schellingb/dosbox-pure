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


#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <limits.h> //GCC 2.95
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include "cdrom.h"
#include "drives.h"
#include "support.h"
#include "setup.h"

#if !defined(WIN32)
#include <libgen.h>
#else
#include <string.h>
#endif

using namespace std;

#define MAX_LINE_LENGTH 512
#define MAX_FILENAME_LENGTH 256

#ifdef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE
CDROM_Interface_Image::TrackFile::TrackFile(const char *filename, bool &error, const char *relative_to) : dos_file(NULL), dos_end(0), dos_ofs(0)
{
	dos_file = FindAndOpenDosFile(filename, NULL, NULL, relative_to);
	if (!dos_file) { error = true; return; }
	bool can_seek = dos_file->Seek(&dos_end, DOS_SEEK_END);
	dos_ofs = dos_end;
	DBP_ASSERT(can_seek);
	error = false;
}

CDROM_Interface_Image::TrackFile::~TrackFile()
{
	if (!dos_file) return;
	if (dos_file->IsOpen()) dos_file->Close();
	if (dos_file->RemoveRef() <= 0) delete dos_file;
}

bool CDROM_Interface_Image::TrackFile::read(Bit8u *buffer, int seek, int count)
{
	int wanted_count = count;
	if ((Bit32u)seek >= dos_end) count = 0;
	else if (dos_end - (Bit32u)seek < (Bit32u)count) count = (int)(dos_end - (Bit32u)seek);
	if ((Bit32u)seek != dos_ofs)
	{
		dos_ofs = (Bit32u)seek;
		dos_file->Seek(&dos_ofs, DOS_SEEK_SET);
	}
	for (Bit32u remain = (Bit32u)count; remain;)
	{
		Bit16u sz = (remain > 0xFFFF ? 0xFFFF : (Bit16u)remain);
		if (!dos_file->Read(buffer, &sz) || !sz) { count -= (int)remain; break; }
		remain -= sz;
		buffer += sz;
	}
	dos_ofs += (Bit32u)count;
	return (wanted_count == count);
}

int CDROM_Interface_Image::TrackFile::getLength()
{
	return (int)dos_end;
}
#else
CDROM_Interface_Image::BinaryFile::BinaryFile(const char *filename, bool &error)
{
	file = new ifstream(filename, ios::in | ios::binary);
	error = (file == NULL) || (file->fail());
}

CDROM_Interface_Image::BinaryFile::~BinaryFile()
{
	delete file;
	file = NULL;
}

bool CDROM_Interface_Image::BinaryFile::read(Bit8u *buffer, int seek, int count)
{
	file->seekg(seek, ios::beg);
	file->read((char*)buffer, count);
	return !(file->fail());
}

int CDROM_Interface_Image::BinaryFile::getLength()
{
	file->seekg(0, ios::end);
	int length = (int)file->tellg();
	if (file->fail()) return -1;
	return length;
}
#endif /* C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE */

#ifdef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE

#include "stb_vorbis.inl"

CDROM_Interface_Image::AudioFile::AudioFile(const char *filename, bool &error, const char *relative_to) : TrackFile(filename, error, relative_to), last_seek(0), vorb(NULL)
{
	if (error) return;

	Bit8u header[64];
	Bit16u sz;
	dos_file->Seek(&(dos_ofs = 0), DOS_SEEK_SET);
	dos_file->Read(header, &(sz = (Bit16u)sizeof(header)));
	if (sz >= 54 && !memcmp(&header[0], "RIFF", 4) && !memcmp(&header[8], "WAVE", 4))
	{
		struct { Bit32u chunkId, chunkSize; Bit16u wFormatTag, nChannels; Bit32u nSamplesPerSec, nAvgBytesPerSec; Bit16u nBlockAlign, wBitsPerSample, cbSize, wValidBitsPerSample; Bit32u dwChannelMask; } chnk;
		Bit32u seek = 12;
		for (bool haveFmt = false;;)
		{
			dos_file->Seek(&seek, DOS_SEEK_SET);
			dos_file->Read((Bit8u*)&chnk, &(sz = (Bit16u)(sizeof(chnk))));
			if (sz >= 8 && memcmp(&chnk.chunkId, (haveFmt ? "data" : "fmt "), 4)) { seek += 8 + ((chnk.chunkSize+1)&~1); continue; }
			if (sz >= 8 && haveFmt) break;
			if (sz < (Bit16u)(sizeof(chnk))
				|| chnk.wFormatTag != 1 //only PCM WAV files are supported.
				|| chnk.nChannels < 1 || chnk.nChannels > 2 //only mono or stereo supported
				|| chnk.wBitsPerSample != 16 //only 16 bits per sample supported.
				|| chnk.nBlockAlign != chnk.nChannels * 2 //implementation error
				) { LOG_MSG("ERROR: CD audio WAV file '%s' is not a valid PCM file", filename); error = true; return; }
			haveFmt = true;
			audio_factor = (chnk.nSamplesPerSec * chnk.nChannels) / 88200.0f;
			if (chnk.nChannels != 2 || chnk.nSamplesPerSec != 44100) { LOG_MSG("WARNING: CD audio WAV file '%s' has %d channels and a rate of %d hz (playback quality might suffer if it's not 2 channels and a rate of 44100 hz)", filename, (int)chnk.nChannels, (int)chnk.nSamplesPerSec); }
		}
		wave_start = seek + 8;
		if (wave_start + chnk.chunkSize < dos_end) dos_end = wave_start + chnk.chunkSize;
		audio_length = (dos_end - wave_start);
	}
	else if (sz >= 54 && !memcmp(header, "OggS", 4))
	{
		dos_file->Seek(&(dos_ofs = 0), DOS_SEEK_SET);
		struct VorbisFuncs
		{
			static bool trkread(CDROM_Interface_Image::AudioFile* trk, Bit8u *buffer, int count)
			{
				return trk->TrackFile::read(buffer, trk->dos_ofs, count);
			}
			static bool trkseek(CDROM_Interface_Image::AudioFile* trk, int pos, int dos_seek_mode)
			{
				return trk->dos_file->Seek(&(trk->dos_ofs = pos), dos_seek_mode);
			}
			static Bit32u trktell(CDROM_Interface_Image::AudioFile* trk)
			{
				return trk->dos_ofs;
			}
		};
		vorb = stb_vorbis_open_trackfile(this, (bool(*)(void*,Bit8u*,int))&VorbisFuncs::trkread, (bool (*)(void*,int,int))&VorbisFuncs::trkseek, (Bit32u(*)(void*))&VorbisFuncs::trktell, dos_end);
		if (!vorb) { LOG_MSG("ERROR: CD audio OGG file '%s' is invalid", filename); error = true; return; }
		stb_vorbis_info p = stb_vorbis_get_info(vorb);
		if (p.sample_rate != 44100) { LOG_MSG("WARNING: CD audio OGG file '%s' has a rate of %d hz (playback quality might suffer if it's not a rate of 44100 hz)", filename, (int)p.sample_rate); }
		audio_factor = p.sample_rate / 44100.0f;
		audio_length = stb_vorbis_stream_length_in_samples(vorb) * 4;
	}
	else { LOG_MSG("ERROR: CD audio file '%s' uses unsupported audio compression", filename); error = true; return; }

	if (audio_factor != 1.0) buffer_temp.resize((size_t)(16 + RAW_SECTOR_SIZE * audio_factor)); // alloc temp buffer for resampling
	audio_length = (Bit32u)(audio_length / audio_factor / (double)(RAW_SECTOR_SIZE) + .4999) * (Bit32u)(RAW_SECTOR_SIZE); // fix and round to RAW_SECTOR_SIZE
	error = false;
}

CDROM_Interface_Image::AudioFile::~AudioFile()
{
	if (vorb)
		stb_vorbis_close(vorb);
}

bool CDROM_Interface_Image::AudioFile::read(Bit8u *buffer, int seek, int count)
{
	DBP_ASSERT(count == RAW_SECTOR_SIZE);
	int count_org = count;
	Bit8u* buffer_org = buffer;
	seek = (int)(seek / sizeof(short) * audio_factor) * sizeof(short);
	count = (int)(count / sizeof(short) * audio_factor) * sizeof(short);
	if (count != count_org) buffer = &buffer_temp[0];

	int seek_off = ((int)last_seek - seek);
	bool seek_jump = ((seek_off < 0 ? -seek_off : seek_off) > count / 3);
	if (!seek_jump) seek = last_seek;
	last_seek = seek + count;

	Bit32u got;
	if (vorb)
	{
		if (seek_jump && !stb_vorbis_seek(vorb, seek / 4)) got = 0;
		else got = stb_vorbis_get_samples_short_interleaved(vorb, 2, (short*)buffer, count / 2) * 4;
	}
	else
	{
		got = dos_end - (wave_start + seek);
		TrackFile::read(buffer, wave_start + seek, count);
	}

	if (got < count)
		memset(buffer + got, 0, count - got);

	if (count != count_org)
	{
		// extremely low quality resampling (better than nothing, output warning in constructor if this gets used)
		short *pOut = (short*)buffer_org, *pIn = (short*)buffer;
		for (int i = 0, iEnd = count_org/sizeof(short); i != iEnd; i++)
			pOut[i] = pIn[(int)(i * audio_factor)];
	}
	return true;
}

int CDROM_Interface_Image::AudioFile::getLength()
{
	return (int)audio_length;
}
#elif defined(C_SDL_SOUND)
CDROM_Interface_Image::AudioFile::AudioFile(const char *filename, bool &error)
{
	Sound_AudioInfo desired = {AUDIO_S16, 2, 44100};
	sample = Sound_NewSampleFromFile(filename, &desired, RAW_SECTOR_SIZE);
	lastCount = RAW_SECTOR_SIZE;
	lastSeek = 0;
	error = (sample == NULL);
}

CDROM_Interface_Image::AudioFile::~AudioFile()
{
	Sound_FreeSample(sample);
}

bool CDROM_Interface_Image::AudioFile::read(Bit8u *buffer, int seek, int count)
{
	if (lastCount != count) {
		int success = Sound_SetBufferSize(sample, count);
		if (!success) return false;
	}
	if (lastSeek != (seek - count)) {
		int success = Sound_Seek(sample, (int)((double)(seek) / 176.4f));
		if (!success) return false;
	}
	lastSeek = seek;
	int bytes = Sound_Decode(sample);
	if (bytes < count) {
		memcpy(buffer, sample->buffer, bytes);
		memset(buffer + bytes, 0, count - bytes);
	} else {
		memcpy(buffer, sample->buffer, count);
	}
	
	return !(sample->flags & SOUND_SAMPLEFLAG_ERROR);
}

int CDROM_Interface_Image::AudioFile::getLength()
{
	int time = 1;
	int shift = 0;
	if (!(sample->flags & SOUND_SAMPLEFLAG_CANSEEK)) return -1;
	
	while (true) {
		int success = Sound_Seek(sample, (unsigned int)(shift + time));
		if (!success) {
			if (time == 1) return lround((double)shift * 176.4f);
			shift += time >> 1;
			time = 1;
		} else {
			if (time > ((numeric_limits<int>::max() - shift) / 2)) return -1;
			time = time << 1;
		}
	}
}
#endif

// initialize static members
int CDROM_Interface_Image::refCount = 0;
CDROM_Interface_Image* CDROM_Interface_Image::images[26] = {};
CDROM_Interface_Image::imagePlayer CDROM_Interface_Image::player = {
	NULL, NULL,
#ifdef C_DBP_USE_SDL
	NULL, //mutex
#endif
	{0}, 0, 0, 0, false, false, false, { {0,0,0,0},{0,0,0,0} } };

	
CDROM_Interface_Image::CDROM_Interface_Image(Bit8u subUnit)
                      :subUnit(subUnit)
{
	images[subUnit] = this;
	if (refCount == 0) {
#ifdef C_DBP_USE_SDL
		player.mutex = SDL_CreateMutex();
#endif
		if (!player.channel) {
			player.channel = MIXER_AddChannel(&CDAudioCallBack, 44100, "CDAUDIO");
		}
		player.channel->Enable(true);
	}
	refCount++;
}

CDROM_Interface_Image::~CDROM_Interface_Image()
{
	refCount--;
	if (player.cd == this) player.cd = NULL;
	ClearTracks();
	if (refCount == 0) {
#ifdef C_DBP_USE_SDL
		SDL_DestroyMutex(player.mutex);
#endif
		player.channel->Enable(false);
	}
}

void CDROM_Interface_Image::InitNewMedia()
{
}

bool CDROM_Interface_Image::SetDevice(char* path, int /*forceCD*/)
{
	if (LoadCueSheet(path)) return true;
	if (LoadIsoFile(path)) return true;
#ifdef C_DBP_SUPPORT_CDROM_CHD_IMAGE
	if (LoadChdFile(path)) return true;
#endif
	
	// print error message on dosbox console
	char buf[MAX_LINE_LENGTH];
	snprintf(buf, MAX_LINE_LENGTH, "Could not load image file: %s\r\n", path);
	Bit16u size = (Bit16u)strlen(buf);
	DOS_WriteFile(STDOUT, (Bit8u*)buf, &size);
	return false;
}

bool CDROM_Interface_Image::GetUPC(unsigned char& attr, char* upc)
{
	attr = 0;
	strcpy(upc, this->mcn.c_str());
	return true;
}

bool CDROM_Interface_Image::GetAudioTracks(int& stTrack, int& end, TMSF& leadOut)
{
	stTrack = 1;
	end = (int)(tracks.size() - 1);
	FRAMES_TO_MSF(tracks[tracks.size() - 1].start + 150, &leadOut.min, &leadOut.sec, &leadOut.fr);
	return true;
}

bool CDROM_Interface_Image::GetAudioTrackInfo(int track, TMSF& start, unsigned char& attr)
{
	if (track < 1 || track > (int)tracks.size()) return false;
	FRAMES_TO_MSF(tracks[track - 1].start + 150, &start.min, &start.sec, &start.fr);
	attr = tracks[track - 1].attr;
	return true;
}

bool CDROM_Interface_Image::GetAudioSub(unsigned char& attr, unsigned char& track, unsigned char& index, TMSF& relPos, TMSF& absPos)
{
	int cur_track = GetTrack(player.currFrame);
	if (cur_track < 1) return false;
	track = (unsigned char)cur_track;
	attr = tracks[track - 1].attr;
	index = 1;
	FRAMES_TO_MSF(player.currFrame + 150, &absPos.min, &absPos.sec, &absPos.fr);
	FRAMES_TO_MSF(player.currFrame - tracks[track - 1].start, &relPos.min, &relPos.sec, &relPos.fr);
	return true;
}

bool CDROM_Interface_Image::GetAudioStatus(bool& playing, bool& pause)
{
	playing = player.isPlaying;
	pause = player.isPaused;
	return true;
}

bool CDROM_Interface_Image::GetMediaTrayStatus(bool& mediaPresent, bool& mediaChanged, bool& trayOpen)
{
	mediaPresent = true;
	mediaChanged = false;
	trayOpen = false;
	return true;
}

bool CDROM_Interface_Image::PlayAudioSector(unsigned long start,unsigned long len)
{
	// We might want to do some more checks. E.g valid start and length
#ifdef C_DBP_USE_SDL
	SDL_mutexP(player.mutex);
#endif
	player.cd = this;
	player.bufLen = 0;
	player.currFrame = start;
	player.targetFrame = start + len;
	int track = GetTrack(start) - 1;
	if(track >= 0 && tracks[track].attr == 0x40) {
		LOG(LOG_MISC,LOG_WARN)("Game tries to play the data track. Not doing this");
		player.isPlaying = false;
		//Unclear wether return false should be here. 
		//specs say that this function returns at once and games should check the status wether the audio is actually playing
		//Real drives either fail or succeed as well
	} else player.isPlaying = true;
	player.isPaused = false;
#ifdef C_DBP_USE_SDL
	SDL_mutexV(player.mutex);
#endif
	return true;
}

bool CDROM_Interface_Image::PauseAudio(bool resume)
{
	player.isPaused = !resume;
	return true;
}

bool CDROM_Interface_Image::StopAudio(void)
{
	player.isPlaying = false;
	player.isPaused = false;
	return true;
}

void CDROM_Interface_Image::ChannelControl(TCtrl ctrl)
{
	player.ctrlUsed = (ctrl.out[0]!=0 || ctrl.out[1]!=1 || ctrl.vol[0]<0xfe || ctrl.vol[1]<0xfe);
	player.ctrlData = ctrl;
}

bool CDROM_Interface_Image::ReadSectors(PhysPt buffer, bool raw, unsigned long sector, unsigned long num)
{
	int sectorSize = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;
	//DBP: Removed memory allocation
	//Bitu buflen = num * sectorSize;
	//Bit8u* buf = new Bit8u[buflen];
	
	bool success = true; //Gobliiins reads 0 sectors
	for(unsigned long i = 0; i < num; i++) {
		//success = ReadSector(&buf[i * sectorSize], raw, sector + i);
		Bit8u buf[RAW_SECTOR_SIZE];
		success = ReadSector(buf, raw, sector + i);
		MEM_BlockWrite(buffer, buf, sectorSize);
		buffer += sectorSize;
		if (!success) break;
	}

	//MEM_BlockWrite(buffer, buf, buflen);
	//delete[] buf;

	return success;
}

#ifdef C_DBP_ENABLE_IDE
CDROM_Interface::atapi_res CDROM_Interface_Image::ReadSectorsAtapi(void* buffer, Bitu bufferSize, Bitu sector, Bitu num, Bit8u readSectorType, Bitu readLength)
{
	Bit8u *buf = (Bit8u*)buffer, *bufEnd = buf + bufferSize;

	int track_num = GetTrack((int)sector);
	if (track_num <= 0) return CDROM_Interface::ATAPI_ILLEGAL_MODE; // illegal request - illegal mode for this track
	Track *t = &tracks[track_num - 1], *lastt = &tracks.back();

	for (Bitu i = 0; i != num; i++, sector++, buf += readLength)
	{
		for (; sector >= t->start + t->length; t++)
			if (t == lastt || (t + 1)->attr != t->attr)
				return CDROM_Interface::ATAPI_ILLEGAL_MODE; // illegal request - illegal mode for this track

		int raw_off = 0;
		bool t_is_raw = (t->sectorSize >= RAW_SECTOR_SIZE), can_read = true;
		switch (readSectorType)
		{
			case 0: break; /* All types */
			case 1: /* CD-DA */
				can_read = (t->attr != 0x40 && t_is_raw);
				break;
			case 2: /* Mode 1 */
				raw_off = (16 - (readLength&31));
				can_read = (!t->mode2 && t->attr == 0x40 && (t_is_raw || raw_off == 16));
				break;
			case 3: /* Mode 2 Formless */
			case 4: /* Mode 2 Form 1 */
			case 5: /* Mode 2 Form 2 */
				raw_off = (readLength < 2324 ? (24 - (readLength&31)) : (readLength < 2332 ? 24 : (readLength < 2340 ? 16 : (readLength < 2348 ? 12 : 0))));
				can_read = (t->mode2 && t->attr == 0x40 && (t_is_raw || raw_off == 24));
				break;
			case 8: /* Special, non-CD-DA, user data only */
				raw_off = (t->mode2 ? 24 : 16);
				can_read = (t->attr == 0x40);
				break;
			default:
				DBP_ASSERT(false);
				return CDROM_Interface::ATAPI_ILLEGAL_MODE; // illegal request - illegal mode for this track
		}

		int off = (t_is_raw ? raw_off : 0), seek = t->skip + (sector - t->start) * t->sectorSize + off;
		if (!can_read || raw_off < 0 || readLength + off > t->sectorSize || buf + RAW_SECTOR_SIZE > bufEnd)
			return CDROM_Interface::ATAPI_ILLEGAL_MODE; // illegal request - illegal mode for this track

		if (t_is_raw && !t->mode2)
		{
			if (!t->file->read(buf, seek, RAW_SECTOR_SIZE - off)) { DBP_ASSERT(false); return CDROM_Interface::ATAPI_ILLEGAL_MODE; } // illegal request - illegal mode for this track

			#ifdef CDROM_VALIDATE_SECTOR_CRC // (slow + unoptimized) validation of CRC
			if (!off)
			{
				// validate crc
				Bit32u invacc = 0, top = 0;
				for (Bit32u i = 0; i < 2064 * 8 + 32; i++) {
					top = invacc & 0x80000000;
					invacc = (invacc << 1);
					if (i < 2064 * 8)
						invacc |= ((buf[i / 8] >> (i % 8)) & 1);
					if (top)
						invacc ^= 0x8001801b;
				}

				Bit32u acc = 0;
				for (Bit32u i = 0; i < 32; i++)
					if (invacc & (1 << i))
						acc |= 1 << (31 - i);

				Bit8u chksum[] = { (Bit8u)((acc) & 0xFF), (Bit8u)((acc >> 8) & 0xFF), (Bit8u)((acc >> 16) & 0xFF), (Bit8u)((acc >> 24) & 0xFF) };
				if (buf[2064] != chksum[0] || buf[2065] != chksum[1] || buf[2066] != chksum[2] || buf[2067] != chksum[3])
				{
					DBP_ASSERT(buf[2068 - off]); // on failed checksum, this normally zero byte should also have some garbage in it
					return CDROM_Interface::ATAPI_READ_ERROR; // medium error - unrecoverable read error
				}
			}
			#endif

			if (buf[2068 - off])
			{
				// ECMA-130: The Intermediate field shall consist of 8 (00)-bytes recorded in positions 2068 to 2075
				// We report a non-zero value as a sector read error. This is to satisfy copy protection checks which expect certain sectors to be bad.
				// Some raw CD image formats represent bad sectors on the original media by filling up the entire sector beyond the header with a dummy byte like 0x55.
				return CDROM_Interface::ATAPI_READ_ERROR; // medium error - unrecoverable read error
			}
		}
		else
		{
			if (!t->file->read(buf, seek, readLength)) { DBP_ASSERT(false); return CDROM_Interface::ATAPI_ILLEGAL_MODE; } // illegal request - illegal mode for this track
		}
	}
	return CDROM_Interface::ATAPI_OK;
}
#endif

bool CDROM_Interface_Image::LoadUnloadMedia(bool /*unload*/)
{
	return true;
}

int CDROM_Interface_Image::GetTrack(int sector)
{
	vector<Track>::iterator i = tracks.begin();
	vector<Track>::iterator end = tracks.end() - 1;
	
	while(i != end) {
		Track &curr = *i;
		Track &next = *(i + 1);
		if (curr.start <= sector && sector < next.start) return curr.number;
		i++;
	}
	return -1;
}

bool CDROM_Interface_Image::ReadSector(Bit8u *buffer, bool raw, unsigned long sector)
{
	/*
	Mode 1:        12 sync bytes, 4 header bytes, 2048 bytes cooked user data, 288 bytes EDC/ECC
	Mode 2 Form 2: 12 sync bytes, 4 header bytes, 2336 bytes user data (with 8 bytes subheader before cooked data)

	ISO
		attr: 0x40 - sectorSize: 2048 (COOKED_SECTOR_SIZE) - mode2: false - cooked seek:  0  (CanReadPVD(file, COOKED_SECTOR_SIZE, false))
		attr: 0x40 - sectorSize: 2352 (RAW_SECTOR_SIZE)    - mode2: false - cooked seek: 16  (CanReadPVD(file, RAW_SECTOR_SIZE, false))
		attr: 0x40 - sectorSize: 2448 (CHD_SECTOR_SIZE)    - mode2: false - cooked seek: 16  (CanReadPVD(file, 2448, false))
		attr: 0x40 - sectorSize: 2336 (MODE2_DATA_SIZE)    - mode2: true  - cooked seek:  8  (CanReadPVD(file, 2336, true))
		attr: 0x40 - sectorSize: 2352 (RAW_SECTOR_SIZE)    - mode2: true  - cooked seek: 24  (CanReadPVD(file, RAW_SECTOR_SIZE, true))

	CUE
		attr: 0    - sectorSize: 2352 (RAW_SECTOR_SIZE)    - mode2: false - cooked seek: 16  (type == "AUDIO")
		attr: 0x40 - sectorSize: 2352 (RAW_SECTOR_SIZE)    - mode2: false - cooked seek: 16  (type == "MODE1/2352")
		attr: 0x40 - sectorSize: 2048 (COOKED_SECTOR_SIZE) - mode2: false - cooked seek:  0  (type == "MODE1/2048")
		attr: 0x40 - sectorSize: 2048 (COOKED_SECTOR_SIZE) - mode2: false - cooked seek:  0  (type == "MODE2/2048")
		attr: 0x40 - sectorSize: 2336 (MODE2_DATA_SIZE)    - mode2: true  - cooked seek:  8  (type == "MODE2/2336")
		attr: 0x40 - sectorSize: 2352 (RAW_SECTOR_SIZE)    - mode2: true  - cooked seek: 24  (type == "MODE2/2352")

	CHD (sectorSize is always 2448, datasize is from chdman source, total seek needs to be to start of cooked data)
		attr: 0    - datasize:   2352 (RAW_SECTOR_SIZE)    - mode2: false - cooked seek: 16  (!strcmp(Type, "AUDIO")) (total seek: 16)
		attr: 0x40 - datasize:   2352 (RAW_SECTOR_SIZE)    - mode2: false - cooked seek: 16  (!strcmp(Type, "MODE1_RAW")) (total seek: 16)
		attr: 0x40 - datasize:   2048 (COOKED_SECTOR_SIZE) - mode2: false - cooked seek: 16  (!strcmp(Type, "MODE1")) (cooked_sector_shift: -16 - total seek: 0)
		attr: 0x40 - datasize:   2048 (COOKED_SECTOR_SIZE) - mode2: false - cooked seek: 16  (!strcmp(Type, "MODE2_FORM1")) (cooked_sector_shift: -16 - total seek: 0)
		attr: 0x40 - datasize:   2324 (FORM2_DATA_SIZE)    - mode2: true  - cooked seek: 24  (!strcmp(Type, "MODE2_FORM2")) (cooked_sector_shift = -24 - total seek: 0)
		attr: 0x40 - datasize:   2336 (MODE2_DATA_SIZE)    - mode2: true  - cooked seek: 24  (!strcmp(Type, "MODE2")) (cooked_sector_shift = -16 - total seek: 8)
		attr: 0x40 - datasize:   2336 (MODE2_DATA_SIZE)    - mode2: true  - cooked seek: 24  (!strcmp(Type, "MODE2_FORM_MIX")) (cooked_sector_shift = -16 - total seek: 8)
		attr: 0x40 - datasize:   2352 (RAW_SECTOR_SIZE)    - mode2: true  - cooked seek: 24  (!strcmp(Type, "MODE2_RAW")) (total seek: 24)
	*/

	int track = GetTrack(sector) - 1;
	if (track < 0) return false;
	
	int seek = tracks[track].skip + (sector - tracks[track].start) * tracks[track].sectorSize;
	int length = (raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE);
#ifdef C_DBP_SUPPORT_CDROM_CHD_IMAGE
	if (tracks[track].sectorSize < RAW_SECTOR_SIZE) { if (raw) return false; }
	else { if (!tracks[track].mode2 && !raw) seek += 16; }
	if (tracks[track].mode2 && !raw) seek += (tracks[track].sectorSize >= RAW_SECTOR_SIZE ? 24 : 8);
#else
	if (tracks[track].sectorSize != RAW_SECTOR_SIZE && raw) return false;
	if (tracks[track].sectorSize == RAW_SECTOR_SIZE && !tracks[track].mode2 && !raw) seek += 16;
	if (tracks[track].mode2 && !raw) seek += 24;
#endif

	return tracks[track].file->read(buffer, seek, length);
}

//DBP: for restart
void CDROM_Interface_Image::ShutDown()
{
	if (CDROM_Interface_Image::player.channel)
		MIXER_DelChannel(CDROM_Interface_Image::player.channel);
	memset(&CDROM_Interface_Image::player, 0, sizeof(CDROM_Interface_Image::player));
}

void CDROM_Interface_Image::CDAudioCallBack(Bitu len)
{
	len *= 4;       // 16 bit, stereo
	if (!len) return;
	if (!player.isPlaying || player.isPaused) {
		player.channel->AddSilence();
		return;
	}
	
#ifdef C_DBP_USE_SDL
	SDL_mutexP(player.mutex);
#endif
	while (player.bufLen < (Bits)len) {
		bool success;
		if (player.targetFrame > player.currFrame)
			success = player.cd->ReadSector(&player.buffer[player.bufLen], true, player.currFrame);
		else success = false;
		
		if (success) {
			player.currFrame++;
			player.bufLen += RAW_SECTOR_SIZE;
		} else {
			memset(&player.buffer[player.bufLen], 0, len - player.bufLen);
			player.bufLen = len;
			player.isPlaying = false;
		}
	}
	if (player.ctrlUsed) {
		Bit16s sample0,sample1;
		Bit16s * samples=(Bit16s *)&player.buffer;
		for (Bitu pos=0;pos<len/4;pos++) {
#if defined(WORDS_BIGENDIAN)
			sample0=(Bit16s)host_readw((HostPt)&samples[pos*2+player.ctrlData.out[0]]);
			sample1=(Bit16s)host_readw((HostPt)&samples[pos*2+player.ctrlData.out[1]]);
#else
			sample0=samples[pos*2+player.ctrlData.out[0]];
			sample1=samples[pos*2+player.ctrlData.out[1]];
#endif
			samples[pos*2+0]=(Bit16s)(sample0*player.ctrlData.vol[0]/255.0);
			samples[pos*2+1]=(Bit16s)(sample1*player.ctrlData.vol[1]/255.0);
		}
#if defined(WORDS_BIGENDIAN)
		player.channel->AddSamples_s16(len/4,(Bit16s *)player.buffer);
	} else	player.channel->AddSamples_s16_nonnative(len/4,(Bit16s *)player.buffer);
#else
	}
	player.channel->AddSamples_s16(len/4,(Bit16s *)player.buffer);
#endif
	memmove(player.buffer, &player.buffer[len], player.bufLen - len);
	player.bufLen -= len;
#ifdef C_DBP_USE_SDL
	SDL_mutexV(player.mutex);
#endif
}

bool CDROM_Interface_Image::LoadIsoFile(char* filename)
{
	//DBP: Call ClearTracks here which actually clears the tracks correctly (the call to LoadCueSheet can actually leave tracks that need clearing after an error)
	ClearTracks(); //tracks.clear();
	
	// data track
	Track track = {0, 0, 0, 0, 0, 0, false, NULL};
	bool error;
	track.file = new BinaryFile(filename, error);
	if (error) {
		delete track.file;
		track.file = NULL;
		return false;
	}
	track.number = 1;
	track.attr = 0x40;//data
	
	// try to detect iso type
	if (CanReadPVD(track.file, COOKED_SECTOR_SIZE, false)) {
		track.sectorSize = COOKED_SECTOR_SIZE;
		track.mode2 = false;
	} else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, false)) {
		track.sectorSize = RAW_SECTOR_SIZE;
		track.mode2 = false;		
	} else if (CanReadPVD(track.file, 2336, true)) {
		track.sectorSize = 2336;
		track.mode2 = true;		
	} else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, true)) {
		track.sectorSize = RAW_SECTOR_SIZE;
		track.mode2 = true;		
#ifdef C_DBP_SUPPORT_CDROM_CHD_IMAGE // added detection for 2448 sector size
	} else if (CanReadPVD(track.file, 2448, false)) {
		track.sectorSize = 2448;
		track.mode2 = false;
#endif
	} else {
		//DBP: Added cleanup
		delete track.file;
		return false;
	}
	
	track.length = track.file->getLength() / track.sectorSize;
	tracks.push_back(track);
	
	// leadout track
	track.number = 2;
	track.attr = 0;
	track.start = track.length;
	track.length = 0;
	track.file = NULL;
	tracks.push_back(track);

	return true;
}

bool CDROM_Interface_Image::CanReadPVD(TrackFile *file, int sectorSize, bool mode2)
{
	Bit8u pvd[COOKED_SECTOR_SIZE];
	int seek = 16 * sectorSize;	// first vd is located at sector 16
	if (sectorSize == RAW_SECTOR_SIZE && !mode2) seek += 16;
	if (mode2) seek += 24;
	file->read(pvd, seek, COOKED_SECTOR_SIZE);
	// pvd[0] = descriptor type, pvd[1..5] = standard identifier, pvd[6] = iso version (+8 for High Sierra)
	return ((pvd[0] == 1 && !strncmp((char*)(&pvd[1]), "CD001", 5) && pvd[6] == 1) ||
			(pvd[8] == 1 && !strncmp((char*)(&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}

#if defined(WIN32) || defined(HAVE_LIBNX) || defined(WIIU) || defined (GEKKO) || defined (_CTR) || defined(_3DS) || defined(VITA) || defined(PSP)
static string FAKEdirname(char * file) {
	char * sep = strrchr(file, '\\');
	if (sep == NULL)
		sep = strrchr(file, '/');
	if (sep == NULL)
		return "";
	else {
		int len = (int)(sep - file);
		char tmp[MAX_FILENAME_LENGTH];
		safe_strncpy(tmp, file, len+1);
		return tmp;
	}
}
#define dirname FAKEdirname
#endif

bool CDROM_Interface_Image::LoadCueSheet(char *cuefile)
{
	Track track = {0, 0, 0, 0, 0, 0, false, NULL};
	//DBP: Call ClearTracks here which actually clears the tracks correctly
	ClearTracks(); //tracks.clear();
	int shift = 0;
	int currPregap = 0;
	int totalPregap = 0;
	int prestart = 0;
	bool success;
	bool canAddTrack = false;
#ifdef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE
	std::string dosfilebuf;
	if (!ReadAndClose(FindAndOpenDosFile(cuefile), dosfilebuf)) return false;
	istringstream inString(dosfilebuf);
	istream& in = (istream&)inString;
#else
	char tmp[MAX_FILENAME_LENGTH];	// dirname can change its argument
	safe_strncpy(tmp, cuefile, MAX_FILENAME_LENGTH);
	string pathname(dirname(tmp));
	ifstream in;
	in.open(cuefile, ios::in);
#endif
	if (in.fail()) return false;
	
	while(!in.eof()) {
		// get next line
		char buf[MAX_LINE_LENGTH];
		in.getline(buf, MAX_LINE_LENGTH);
		if (in.fail() && !in.eof()) return false;  // probably a binary file
		istringstream line(buf);
		
		string command;
		GetCueKeyword(command, line);
		
		if (command == "TRACK") {
			if (canAddTrack) success = AddTrack(track, shift, prestart, totalPregap, currPregap);
			else success = true;
			
			track.start = 0;
			track.skip = 0;
			currPregap = 0;
			prestart = 0;
	
			line >> track.number;
			string type;
			GetCueKeyword(type, line);
			
			if (type == "AUDIO") {
				track.sectorSize = RAW_SECTOR_SIZE;
				track.attr = 0;
				track.mode2 = false;
			} else if (type == "MODE1/2048") {
				track.sectorSize = COOKED_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = false;
			} else if (type == "MODE1/2352") {
				track.sectorSize = RAW_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = false;
#ifdef C_DBP_SUPPORT_CDROM_CHD_IMAGE // added Mode 2 form 1 detection, which is equivalent to MODE1/2048
			} else if (type == "MODE2/2048") { 
				track.sectorSize = COOKED_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = false;
#endif
			} else if (type == "MODE2/2336") {
				track.sectorSize = 2336;
				track.attr = 0x40;
				track.mode2 = true;
			} else if (type == "MODE2/2352") {
				track.sectorSize = RAW_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = true;
			} else success = false;
			
			canAddTrack = true;
		}
		else if (command == "INDEX") {
			int index;
			line >> index;
			int frame;
			success = GetCueFrame(frame, line);
			
			if (index == 1) track.start = frame;
			else if (index == 0) prestart = frame;
			// ignore other indices
		}
		else if (command == "FILE") {
			if (canAddTrack) success = AddTrack(track, shift, prestart, totalPregap, currPregap);
			else success = true;
			canAddTrack = false;
			
			string filename;
			GetCueString(filename, line);
#ifndef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE
			GetRealFileName(filename, pathname);
#endif
			string type;
			GetCueKeyword(type, line);

			track.file = NULL;
			bool error = true;
			if (type == "BINARY") {
#ifdef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE
				track.file = new BinaryFile(filename.c_str(), error, cuefile);
#else
				track.file = new BinaryFile(filename.c_str(), error);
#endif
			}
			//The next if has been surpassed by the else, but leaving it in as not 
			//to break existing cue sheets that depend on this.(mine with OGG tracks specifying MP3 as type)
			else if (type == "WAVE" || type == "AIFF" || type == "MP3") {
#ifdef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE
				track.file = new AudioFile(filename.c_str(), error, cuefile);
#else
				track.file = new AudioFile(filename.c_str(), error);
#endif
			} else { 
#if defined(C_SDL_SOUND)
				const Sound_DecoderInfo **i;
				for (i = Sound_AvailableDecoders(); *i != NULL; i++) {
					if (*(*i)->extensions == type) {
						track.file = new AudioFile(filename.c_str(), error);
						break;
					}
				}
#endif
			}
			if (error) {
				delete track.file;
				track.file = NULL;
				success = false;
			}
		}
		else if (command == "PREGAP") success = GetCueFrame(currPregap, line);
		else if (command == "CATALOG") success = GetCueString(mcn, line);
		// ignored commands
		else if (command == "CDTEXTFILE" || command == "FLAGS" || command == "ISRC"
			|| command == "PERFORMER" || command == "POSTGAP" || command == "REM"
			|| command == "SONGWRITER" || command == "TITLE" || command == "") success = true;
		// failure
		else success = false;

		if (!success) return false;
	}
	// add last track
	if (!AddTrack(track, shift, prestart, totalPregap, currPregap)) return false;
	
	// add leadout track
	track.number++;
	track.attr = 0;//sync with load iso
	track.start = 0;
	track.length = 0;
	track.file = NULL;
	if(!AddTrack(track, shift, 0, totalPregap, 0)) return false;

	return true;
}

bool CDROM_Interface_Image::AddTrack(Track &curr, int &shift, int prestart, int &totalPregap, int currPregap)
{
	// frames between index 0(prestart) and 1(curr.start) must be skipped
	int skip;
	if (prestart > 0) {
		if (prestart > curr.start) return false;
		skip = curr.start - prestart;
	} else skip = 0;
	
	// first track (track number must be 1)
	if (tracks.empty()) {
		if (curr.number != 1) return false;
		curr.skip = skip * curr.sectorSize;
		curr.start += currPregap;
		totalPregap = currPregap;
		tracks.push_back(curr);
		return true;
	}
	
	Track &prev = *(tracks.end() - 1);
	
	// current track consumes data from the same file as the previous
	if (prev.file == curr.file) {
		curr.start += shift;
		prev.length = curr.start + totalPregap - prev.start - skip;
		curr.skip += prev.skip + prev.length * prev.sectorSize + skip * curr.sectorSize;		
		totalPregap += currPregap;
		curr.start += totalPregap;
	// current track uses a different file as the previous track
	} else {
		int tmp = prev.file->getLength() - prev.skip;
		prev.length = tmp / prev.sectorSize;
		if (tmp % prev.sectorSize != 0) prev.length++; // padding
		
		curr.start += prev.start + prev.length + currPregap;
		curr.skip = skip * curr.sectorSize;
		shift += prev.start + prev.length;
		totalPregap = currPregap;
	}
	
	// error checks
	if (curr.number <= 1) return false;
	if (prev.number + 1 != curr.number) return false;
	if (curr.start < prev.start + prev.length) return false;
	if (curr.length < 0) return false;
	
	tracks.push_back(curr);
	return true;
}

bool CDROM_Interface_Image::HasDataTrack(void)
{
	//Data track has attribute 0x40
	for(track_it it = tracks.begin(); it != tracks.end(); it++) {
		if ((*it).attr == 0x40) return true;
	}
	return false;
}

#ifndef C_DBP_SUPPORT_CDROM_MOUNT_DOSFILE
bool CDROM_Interface_Image::GetRealFileName(string &filename, string &pathname)
{
	// check if file exists
	struct stat test;
#ifndef C_DBP_HAVE_FPATH_NOCASE
	if (stat(filename.c_str(), &test) == 0) return true;
	
	// check if file with path relative to cue file exists
	string tmpstr(pathname + "/" + filename);
	if (stat(tmpstr.c_str(), &test) == 0) {
		filename = tmpstr;
		return true;
	}
#else
	if (fpath_nocase(filename.c_str())) return true;
	
	// check if file with path relative to cue file exists
	string tmpstr(pathname + "/" + filename);
	if (fpath_nocase(tmpstr.c_str())) {
		filename = tmpstr;
		return true;
	}
#endif

	// finally check if file is in a dosbox local drive
	char fullname[CROSS_LEN];
	char tmp[CROSS_LEN];
	safe_strncpy(tmp, filename.c_str(), CROSS_LEN);
	Bit8u drive;
	if (!DOS_MakeName(tmp, fullname, &drive)) return false;
	
	localDrive *ldp = dynamic_cast<localDrive*>(Drives[drive]);
	if (ldp) {
		ldp->GetSystemFilename(tmp, fullname);
		if (stat(tmp, &test) == 0) {
			filename = tmp;
			return true;
		}
	}
#if defined (WIN32) || defined(OS2)
	//Nothing
#else
	//Consider the possibility that the filename has a windows directory seperator (inside the CUE file) 
	//which is common for some commercial rereleases of DOS games using DOSBox

	string copy = filename;
	size_t l = copy.size();
	for (size_t i = 0; i < l;i++) {
		if(copy[i] == '\\') copy[i] = '/';
	}

	if (stat(copy.c_str(), &test) == 0) {
		filename = copy;
		return true;
	}

	tmpstr = pathname + "/" + copy;
	if (stat(tmpstr.c_str(), &test) == 0) {
		filename = tmpstr;
		return true;
	}

#endif
	return false;
}
#endif

bool CDROM_Interface_Image::GetCueKeyword(string &keyword, istream &in)
{
	in >> keyword;
	for(Bitu i = 0; i < keyword.size(); i++) keyword[i] = toupper(keyword[i]);
	
	return true;
}

bool CDROM_Interface_Image::GetCueFrame(int &frames, istream &in)
{
	string msf;
	in >> msf;
	int min, sec, fr;
	bool success = sscanf(msf.c_str(), "%d:%d:%d", &min, &sec, &fr) == 3;
	frames = MSF_TO_FRAMES(min, sec, fr);
	
	return success;
}

bool CDROM_Interface_Image::GetCueString(string &str, istream &in)
{
	int pos = (int)in.tellg();
	in >> str;
	if (str[0] == '\"') {
		if (str[str.size() - 1] == '\"') {
			str.assign(str, 1, str.size() - 2);
		} else {
			in.seekg(pos, ios::beg);
			char buffer[MAX_FILENAME_LENGTH];
			in.getline(buffer, MAX_FILENAME_LENGTH, '\"');	// skip
			in.getline(buffer, MAX_FILENAME_LENGTH, '\"');
			str = buffer;
		}
	}
	return true;
}

void CDROM_Interface_Image::ClearTracks()
{
	vector<Track>::iterator i = tracks.begin();
	vector<Track>::iterator end = tracks.end();

	TrackFile* last = NULL;	
	while(i != end) {
		Track &curr = *i;
		if (curr.file != last) {
			delete curr.file;
			last = curr.file;
		}
		i++;
	}
	tracks.clear();
}

#ifdef C_DBP_SUPPORT_CDROM_CHD_IMAGE
bool CDROM_Interface_Image::LoadChdFile(char* filename)
{
	//DBP: Call ClearTracks here which actually clears the tracks correctly (the call to LoadCueSheet can actually leave tracks that need clearing after an error)
	ClearTracks(); //tracks.clear();

	enum { CHD_V5_HEADER_SIZE = 124, CHD_V5_UNCOMPMAPENTRYBYTES = 4, CD_MAX_SECTOR_DATA = 2352, CD_MAX_SUBCODE_DATA = 96, CD_FRAME_SIZE = CD_MAX_SECTOR_DATA + CD_MAX_SUBCODE_DATA };
	enum { METADATA_HEADER_SIZE = 16, CDROM_TRACK_METADATA_TAG = 1128813650, CDROM_TRACK_METADATA2_TAG = 1128813618, CD_TRACK_PADDING = 4 };

	struct ChdFile : public BinaryFile
	{
		ChdFile(const char *filename, bool &error) : BinaryFile(filename, error), memory(NULL), cooked_sector_shift(0) { }
		virtual ~ChdFile() { free(memory); }
		Bit8u *memory, *sector_to_track;
		Bit32u *hunkmap, *paddings;
		int hunkbytes, cooked_sector_shift;

		static Bit32u get_bigendian_uint32(const Bit8u *base) { return (base[0] << 24) | (base[1] << 16) | (base[2] << 8) | base[3]; }
		static Bit64u get_bigendian_uint64(const Bit8u *base) { return ((Bit64u)base[0] << 56) | ((Bit64u)base[1] << 48) | ((Bit64u)base[2] << 40) | ((Bit64u)base[3] << 32) | ((Bit64u)base[4] << 24) | ((Bit64u)base[5] << 16) | ((Bit64u)base[6] << 8) | (Bit64u)base[7]; }

		virtual bool read(Bit8u *buffer, int seek, int count)
		{
			DBP_ASSERT((seek / CD_FRAME_SIZE) == ((seek + count) / CD_FRAME_SIZE)); // read only inside one sector
			int track = sector_to_track[seek / CD_FRAME_SIZE];
			seek += paddings[track];
			const int hunk = (seek / hunkbytes), hunk_ofs = (seek % hunkbytes), hunk_pos = (int)hunkmap[hunk];
			if (!hunk_pos) { memset(buffer, 0, count); return true; }
			if (!BinaryFile::read(buffer, hunk_pos + hunk_ofs + (count == COOKED_SECTOR_SIZE ? cooked_sector_shift : 0), count)) return false;
			if (track) // CHD audio endian swap
				for (Bit8u *p = buffer + (seek & 1), *pEnd = buffer + count, tmp; p < pEnd; p += 2)
					{ tmp = p[0]; p[0] = p[1]; p[1] = tmp; }
			return true;
		}
	};

	bool not_chd;
	ChdFile* chd = new ChdFile(filename, not_chd);
	if (not_chd)
	{
		err:
		tracks.clear();
		delete chd;
		if (!not_chd) GFX_ShowMsg("Invalid or sunsupported CHD file, must be an uncompressed version 5 CD image");
		return false;
	}

	// Read CHD header and check signature
	Bit8u rawheader[CHD_V5_HEADER_SIZE];
	if (!chd->BinaryFile::read(rawheader, 0, CHD_V5_HEADER_SIZE) || memcmp(rawheader, "MComprHD", 8)) { not_chd = true; goto err; }

	// Check supported version, flags and compression
	Bit32u hdr_length = ChdFile::get_bigendian_uint32(&rawheader[8]);
	Bit32u hdr_version = ChdFile::get_bigendian_uint32(&rawheader[12]);
	if (hdr_version != 5 || hdr_length != CHD_V5_HEADER_SIZE) goto err; // only ver 5 is supported
	if (ChdFile::get_bigendian_uint32(&rawheader[16])) goto err; // compression is not supported

	// Make sure it's a CD image
	DBP_STATIC_ASSERT(CD_MAX_SECTOR_DATA == RAW_SECTOR_SIZE);
	Bit32u unitsize = ChdFile::get_bigendian_uint32(&rawheader[60]);
	chd->hunkbytes = (int)ChdFile::get_bigendian_uint32(&rawheader[56]);
	if (unitsize != CD_FRAME_SIZE || (chd->hunkbytes % CD_FRAME_SIZE) || !chd->hunkbytes) goto err; // not CD sector size

	// Read file offsets for hunk mapping and track meta data
	Bit64u filelen = (Bit64u)chd->BinaryFile::getLength();
	Bit64u logicalbytes = ChdFile::get_bigendian_uint64(&rawheader[32]);
	Bit64u mapoffset = ChdFile::get_bigendian_uint64(&rawheader[40]);
	Bit64u metaoffset = ChdFile::get_bigendian_uint64(&rawheader[48]);
	if (mapoffset < CHD_V5_HEADER_SIZE || mapoffset >= filelen || metaoffset < CHD_V5_HEADER_SIZE || metaoffset >= filelen || !logicalbytes) goto err;

	// Read track meta data
	Track empty_track = { 0, 0, 0, 0, 0, CD_FRAME_SIZE, false, chd };
	for (Bit64u metaentry_offset = metaoffset, metaentry_prev = 0, metaentry_next; metaentry_offset != 0; metaentry_prev = metaentry_offset, metaentry_offset = metaentry_next)
	{
		char meta[256], mt_type[32], mt_subtype[32];
		Bit8u raw_meta_header[METADATA_HEADER_SIZE];
		if (!chd->BinaryFile::read(raw_meta_header, (int)metaentry_offset, sizeof(raw_meta_header))) goto err;
		Bit32u metaentry_metatag = ChdFile::get_bigendian_uint32(&raw_meta_header[0]);
		Bit32u metaentry_length = (ChdFile::get_bigendian_uint32(&raw_meta_header[4]) & 0x00ffffff);
		metaentry_next = ChdFile::get_bigendian_uint64(&raw_meta_header[8]);
		if (metaentry_metatag != CDROM_TRACK_METADATA_TAG && metaentry_metatag != CDROM_TRACK_METADATA2_TAG) continue;
		if (!chd->BinaryFile::read((Bit8u*)meta, (int)(metaentry_offset + METADATA_HEADER_SIZE), (int)(metaentry_length > sizeof(meta) ? sizeof(meta) : metaentry_length))) goto err;
		printf("%.*s\n", metaentry_length, meta);

		int mt_track_no = 0, mt_frames = 0, mt_pregap = 0;
		if (sscanf(meta,
			(metaentry_metatag == CDROM_TRACK_METADATA2_TAG ? "TRACK:%d TYPE:%30s SUBTYPE:%30s FRAMES:%d PREGAP:%d" : "TRACK:%d TYPE:%30s SUBTYPE:%30s FRAMES:%d"),
			&mt_track_no, mt_type, mt_subtype, &mt_frames, &mt_pregap) < 4) continue;

		// Add CHD tracks without using AddTrack because it's much simpler, we also support an incoming unsorted track list
		while (tracks.size() < (size_t)(mt_track_no)) { empty_track.number++; tracks.push_back(empty_track); }
		bool isAudio = !strcmp(mt_type, "AUDIO"), isMode2Form1 = (!isAudio && !strcmp(mt_type, "MODE2_FORM1")); // treated equivalent to MODE1
		Track& track = tracks[mt_track_no - 1];
		track.attr = (isAudio ? 0 : 0x40);
		track.start = mt_pregap;
		track.length = mt_frames - mt_pregap;
		track.mode2 = (mt_type[4] == '2' && !isMode2Form1);
		if (isAudio) continue;

		// Negate offset done in CDROM_Interface_Image::ReadSector (see table in that function for how to handle all track types)
		if (isMode2Form1 || !strcmp(mt_type, "MODE1") || !strcmp(mt_type, "MODE2") || !strcmp(mt_type, "MODE2_FORM_MIX")) chd->cooked_sector_shift = -16;
		else if (!strcmp(mt_type, "MODE2_FORM2")) chd->cooked_sector_shift = -24;
	}

	Bit32u trackcount = (Bit32u)tracks.size();
	if (!trackcount || trackcount > 127) goto err; // no tracks found

	// Add leadout track for chd, just calculate it manually and skip AddTrack (which would call TrackFile::getLength).
	// AddTrack would wrongfully change the length of the last track. By doing this manually we don't need ChdFile::getLength.
	empty_track.number++;
	empty_track.file = NULL;
	tracks.push_back(empty_track);

	DBP_STATIC_ASSERT(CHD_V5_UNCOMPMAPENTRYBYTES == sizeof(Bit32u));
	Bit32u hunkcount = ((logicalbytes + chd->hunkbytes - 1) / chd->hunkbytes), sectorcount = (logicalbytes / CD_FRAME_SIZE);
	Bit32u allocate_bytes = (hunkcount * CHD_V5_UNCOMPMAPENTRYBYTES) + (trackcount * sizeof(Bit32u)) + (sectorcount);
	chd->memory = (Bit8u*)malloc(allocate_bytes);
	chd->hunkmap = (Bit32u*)chd->memory;
	chd->paddings = (Bit32u*)(chd->hunkmap + hunkcount);
	chd->sector_to_track = (Bit8u*)(chd->paddings + trackcount);

	// Read hunk mapping and convert to file offsets
	if (!chd->BinaryFile::read((Bit8u*)chd->hunkmap, (int)mapoffset, hunkcount * CHD_V5_UNCOMPMAPENTRYBYTES)) goto err;
	for (Bit32u i = 0; i != hunkcount; i++) chd->hunkmap[i] = ChdFile::get_bigendian_uint32((Bit8u*)&chd->hunkmap[i]) * chd->hunkbytes;

	// Now set physical start offsets for tracks and calculate CHD paddings. In CHD files tracks are padded to a to a 4-sector boundary.
	// Thus we need to give ChdFile::read a means to figure out the padding that applies to the physical sector number it is reading.
	for (Bit32u sector = 0, total_chd_padding = 0, i = 0;; i++)
	{
		int physical_sector = (i ? tracks[i - 1].start + tracks[i - 1].length : 0); // without CHD padding
		tracks[i].start += physical_sector; // += to add to mt_pregap
		if (i == trackcount) break; // leadout only needs start
		tracks[i].skip = tracks[i].start * CD_FRAME_SIZE; // physical address

		Bit32u sector_end = (tracks[i].start + tracks[i].length);
		memset(chd->sector_to_track + sector, (int)i, (sector_end - sector));
		sector = sector_end;
		total_chd_padding += ((CD_TRACK_PADDING - ((physical_sector + total_chd_padding) % CD_TRACK_PADDING)) % CD_TRACK_PADDING);
		chd->paddings[i] = (total_chd_padding * CD_FRAME_SIZE);
	}
	return true;
}
#endif

void CDROM_Image_Destroy(Section*) {
#if defined(C_SDL_SOUND)
	Sound_Quit();
#endif
}

void CDROM_Image_Init(Section* section) {
#if defined(C_SDL_SOUND)
	Sound_Init();
	section->AddDestroyFunction(CDROM_Image_Destroy, false);
#endif
}

#include <dbp_serialize.h>

void DBPSerialize_CDPlayer(DBPArchive& ar_outer)
{
	DBPArchiveOptional ar(ar_outer, CDROM_Interface_Image::player.channel, (CDROM_Interface_Image::player.cd != NULL));
	if (ar.IsSkip()) return;

	ar << CDROM_Interface_Image::player.currFrame << CDROM_Interface_Image::player.targetFrame
		<< CDROM_Interface_Image::player.isPlaying << CDROM_Interface_Image::player.isPaused
		<< CDROM_Interface_Image::player.ctrlUsed;
	ar.Serialize(CDROM_Interface_Image::player.ctrlData);

	if (ar.mode == DBPArchive::MODE_LOAD && CDROM_Interface_Image::player.isPlaying && !CDROM_Interface_Image::player.cd)
	{
		CDROM_Interface_Image* img = NULL;
		for (int i = 2; i != 10; i++)
			if ((img = (Drives[i] && dynamic_cast<isoDrive*>(Drives[i]) ? dynamic_cast<CDROM_Interface_Image*>(((isoDrive*)Drives[i])->GetInterface()) : NULL)) != NULL)
				break;
		CDROM_Interface_Image::player.cd = img;
		if (!img) CDROM_Interface_Image::player.isPlaying = false;
	}
}
