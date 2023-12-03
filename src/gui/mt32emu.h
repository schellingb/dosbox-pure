/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h> /* memset, memcpy, strcmp */
#include <stdarg.h> /* va_list, va_start, va_end */
#include <stdlib.h> /* rand */
#include <stdio.h> /* printf, vprintf, sprintf */
#include <math.h> /* pow, exp, log, log10, fmod, sin, cos */

#ifndef MT32EMU_GLOBALS_H
#define MT32EMU_GLOBALS_H

#define MT32EMU_EXPORT

/* Helpers for compile-time version checks */

/* Encodes the given version components to a single integer value to simplify further checks. */
#define MT32EMU_VERSION_INT(major, minor, patch) ((major << 16) | (minor << 8) | patch)

/* The version of this library build, as an integer. */
#define MT32EMU_CURRENT_VERSION_INT MT32EMU_VERSION_INT(MT32EMU_VERSION_MAJOR, MT32EMU_VERSION_MINOR, MT32EMU_VERSION_PATCH)

/* Useful constants */

/* Sample rate to use in mixing. With the progress of development, we've found way too many thing dependent.
 * In order to achieve further advance in emulation accuracy, sample rate made fixed throughout the emulator,
 * except the emulation of analogue path.
 * The output from the synth is supposed to be resampled externally in order to convert to the desired sample rate.
 */
#define MT32EMU_SAMPLE_RATE 32000

/* The default value for the maximum number of partials playing simultaneously. */
#define MT32EMU_DEFAULT_MAX_PARTIALS 32

/* The higher this number, the more memory will be used, but the more samples can be processed in one run -
 * various parts of sample generation can be processed more efficiently in a single run.
 * A run's maximum length is that given to Synth::render(), so giving a value here higher than render() is ever
 * called with will give no gain (but simply waste the memory).
 * Note that this value does *not* in any way impose limitations on the length given to render(), and has no effect
 * on the generated audio.
 * This value must be >= 1.
 */
#define MT32EMU_MAX_SAMPLES_PER_RUN 4096

/* The default size of the internal MIDI event queue.
 * It holds the incoming MIDI events before the rendering engine actually processes them.
 * The main goal is to fairly emulate the real hardware behaviour which obviously
 * uses an internal MIDI event queue to gather incoming data as well as the delays
 * introduced by transferring data via the MIDI interface.
 * This also facilitates building of an external rendering loop
 * as the queue stores timestamped MIDI events.
 */
#define MT32EMU_DEFAULT_MIDI_EVENT_QUEUE_SIZE 1024

/* Maximum allowed size of MIDI parser input stream buffer.
 * Should suffice for any reasonable bulk dump SysEx, as the h/w units have only 32K of RAM onboard.
 */
#define MT32EMU_MAX_STREAM_BUFFER_SIZE 32768

/* This should correspond to the MIDI buffer size used in real h/w devices.
 * CM-32L control ROM is using 1000 bytes, and MT-32 GEN0 is using only 240 bytes (semi-confirmed by now).
 */
#define MT32EMU_SYSEX_BUFFER_SIZE 1000

namespace MT32Emu
{
const unsigned int SAMPLE_RATE = MT32EMU_SAMPLE_RATE;
#undef MT32EMU_SAMPLE_RATE

const unsigned int DEFAULT_MAX_PARTIALS = MT32EMU_DEFAULT_MAX_PARTIALS;
#undef MT32EMU_DEFAULT_MAX_PARTIALS

const unsigned int MAX_SAMPLES_PER_RUN = MT32EMU_MAX_SAMPLES_PER_RUN;
#undef MT32EMU_MAX_SAMPLES_PER_RUN

const unsigned int DEFAULT_MIDI_EVENT_QUEUE_SIZE = MT32EMU_DEFAULT_MIDI_EVENT_QUEUE_SIZE;
#undef MT32EMU_DEFAULT_MIDI_EVENT_QUEUE_SIZE

const unsigned int MAX_STREAM_BUFFER_SIZE = MT32EMU_MAX_STREAM_BUFFER_SIZE;
#undef MT32EMU_MAX_STREAM_BUFFER_SIZE

const unsigned int SYSEX_BUFFER_SIZE = MT32EMU_SYSEX_BUFFER_SIZE;
#undef MT32EMU_SYSEX_BUFFER_SIZE
}

#endif /* #ifndef MT32EMU_GLOBALS_H */

#ifndef MT32EMU_TYPES_H
#define MT32EMU_TYPES_H

namespace MT32Emu {

typedef unsigned int       Bit32u;
typedef   signed int       Bit32s;
typedef unsigned short int Bit16u;
typedef   signed short int Bit16s;
typedef unsigned char      Bit8u;
typedef   signed char      Bit8s;

}

#endif

#ifndef MT32EMU_FILE_H
#define MT32EMU_FILE_H

namespace MT32Emu {

class MT32EMU_EXPORT File {
public:
	// Includes terminator char.
	typedef char SHA1Digest[41];

	virtual ~File() {}
	virtual size_t getSize() = 0;
	virtual const Bit8u *getData() = 0;
	virtual const SHA1Digest &getSHA1() = 0;

	virtual void close() = 0;
};

class MT32EMU_EXPORT AbstractFile : public File {
public:
	const SHA1Digest &getSHA1();

protected:
	AbstractFile();
	AbstractFile(const SHA1Digest &sha1Digest);

private:
	bool sha1DigestCalculated;
	SHA1Digest sha1Digest;

	// Binary compatibility helper.
	void *reserved;
};

class MT32EMU_EXPORT ArrayFile : public AbstractFile {
public:
	ArrayFile(const Bit8u *data, size_t size);
	ArrayFile(const Bit8u *data, size_t size, const SHA1Digest &sha1Digest);

	size_t getSize();
	const Bit8u *getData();
	void close() {}

private:
	const Bit8u *data;
	size_t size;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_FILE_H

#ifndef MT32EMU_ROMINFO_H
#define MT32EMU_ROMINFO_H

namespace MT32Emu {

// Defines vital info about ROM file to be used by synth and applications

struct ROMInfo {
public:
	size_t fileSize;
	const File::SHA1Digest &sha1Digest;
	enum Type {PCM, Control, Reverb} type;
	const char *shortName;
	const char *description;
	enum PairType {
		// Complete ROM image ready to use with Synth.
		Full,
		////DBP: Removed unused partial rom
		//// ROM image contains data that occupies lower addresses. Needs pairing before use.
		//FirstHalf,
		//// ROM image contains data that occupies higher addresses. Needs pairing before use.
		//SecondHalf,
		//// ROM image contains data that occupies even addresses. Needs pairing before use.
		//Mux0,
		//// ROM image contains data that occupies odd addresses. Needs pairing before use.
		//Mux1
	} pairType;
	////DBP: Removed unused partial rom
	//// NULL for Full images or a pointer to the corresponding other image for pairing.
	//const ROMInfo *pairROMInfo;

	// Returns a ROMInfo struct by inspecting the size and the SHA1 hash of the file
	// among all the known ROMInfos.
	MT32EMU_EXPORT static const ROMInfo* getROMInfo(File *file);

	// Returns a ROMInfo struct by inspecting the size and the SHA1 hash of the file
	// among the ROMInfos listed in the NULL-terminated list romInfos.
	MT32EMU_EXPORT static const ROMInfo *getROMInfo(File *file, const ROMInfo * const *romInfos);

	// Currently no-op
	MT32EMU_EXPORT static void freeROMInfo(const ROMInfo *romInfo);

	// Allows retrieving a NULL-terminated list of ROMInfos for a range of types and pairTypes
	// (specified by bitmasks)
	// Useful for GUI/console app to output information on what ROMs it supports
	// The caller must free the returned list with freeROMInfoList when finished.
	MT32EMU_EXPORT static const ROMInfo** getROMInfoList(Bit32u types, Bit32u pairTypes);

	// Frees the list of ROMInfos given that has been created by getROMInfoList.
	MT32EMU_EXPORT static void freeROMInfoList(const ROMInfo **romInfos);
};

// Synth::open() requires a full control ROMImage and a compatible full PCM ROMImage to work

class ROMImage {
public:
	// Creates a ROMImage object given a ROMInfo and a File. Keeps a reference
	// to the File and ROMInfo given, which must be freed separately by the user
	// after the ROMImage is freed.
	// CAVEAT: This method always prefers full ROM images over partial ones.
	// Because the lower half of CM-32L/CM-64/LAPC-I PCM ROM is essentially the full
	// MT-32 PCM ROM, it is therefore aliased. In this case a partial image can only be
	// created by the overridden method makeROMImage(File *, const ROMInfo * const *).
	MT32EMU_EXPORT static const ROMImage* makeROMImage(File *file);

	// Same as the method above but only permits creation of a ROMImage if the file content
	// matches one of the ROMs described in a NULL-terminated list romInfos. This list can be
	// created using e.g. method ROMInfo::getROMInfoList.
	MT32EMU_EXPORT static const ROMImage *makeROMImage(File *file, const ROMInfo * const *romInfos);

	////DBP: Removed unused rom merging
	//MT32EMU_EXPORT_V(2.5) static const ROMImage *makeROMImage(File *file1, File *file2);

	// Must only be done after all Synths using the ROMImage are deleted
	MT32EMU_EXPORT static void freeROMImage(const ROMImage *romImage);

	////DBP: Removed unused rom merging
	//MT32EMU_EXPORT_V(2.5) static const ROMImage *mergeROMImages(const ROMImage *romImage1, const ROMImage *romImage2);

	MT32EMU_EXPORT File *getFile() const;

	// Returns true in case this ROMImage is built with a user provided File that has to be deallocated separately.
	// For a ROMImage created via merging two partial ROMImages, this method returns false.
	MT32EMU_EXPORT bool isFileUserProvided() const;
	MT32EMU_EXPORT const ROMInfo *getROMInfo() const;

private:
	////DBP: Removed unused rom merging
	//static const ROMImage *makeFullROMImage(Bit8u *data, size_t dataSize);
	//static const ROMImage *appendImages(const ROMImage *romImageLow, const ROMImage *romImageHigh);
	//static const ROMImage *interleaveImages(const ROMImage *romImageEven, const ROMImage *romImageOdd);

	File * const file;
	const bool ownFile;
	const ROMInfo * const romInfo;

	ROMImage(File *file, bool ownFile, const ROMInfo * const *romInfos);
	~ROMImage();

	// Make ROMIMage an identity class.
	ROMImage(const ROMImage &);
	ROMImage &operator=(const ROMImage &);
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_ROMINFO_H

#ifndef MT32EMU_ENUMERATIONS_H
#define MT32EMU_ENUMERATIONS_H

#define MT32EMU_DAC_INPUT_MODE_NAME DACInputMode
#define MT32EMU_DAC_INPUT_MODE(ident) DACInputMode_##ident

#define MT32EMU_MIDI_DELAY_MODE_NAME MIDIDelayMode
#define MT32EMU_MIDI_DELAY_MODE(ident) MIDIDelayMode_##ident

#define MT32EMU_ANALOG_OUTPUT_MODE_NAME AnalogOutputMode
#define MT32EMU_ANALOG_OUTPUT_MODE(ident) AnalogOutputMode_##ident

#define MT32EMU_PARTIAL_STATE_NAME PartialState
#define MT32EMU_PARTIAL_STATE(ident) PartialState_##ident

#define MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME SamplerateConversionQuality
#define MT32EMU_SAMPLERATE_CONVERSION_QUALITY(ident) SamplerateConversionQuality_##ident

#define MT32EMU_RENDERER_TYPE_NAME RendererType
#define MT32EMU_RENDERER_TYPE(ident) RendererType_##ident

namespace MT32Emu {

/**
 * Methods for emulating the connection between the LA32 and the DAC, which involves
 * some hacks in the real devices for doubling the volume.
 * See also http://en.wikipedia.org/wiki/Roland_MT-32#Digital_overflow
 */
enum MT32EMU_DAC_INPUT_MODE_NAME {
	/**
	 * Produces samples at double the volume, without tricks.
	 * Nicer overdrive characteristics than the DAC hacks (it simply clips samples within range)
	 * Higher quality than the real devices
	 */
	MT32EMU_DAC_INPUT_MODE(NICE),

	/**
	 * Produces samples that exactly match the bits output from the emulated LA32.
	 * Nicer overdrive characteristics than the DAC hacks (it simply clips samples within range)
	 * Much less likely to overdrive than any other mode.
	 * Half the volume of any of the other modes.
	 * Perfect for developers while debugging :)
	 */
	MT32EMU_DAC_INPUT_MODE(PURE),

	/**
	 * Re-orders the LA32 output bits as in early generation MT-32s (according to Wikipedia).
	 * Bit order at DAC (where each number represents the original LA32 output bit number, and XX means the bit is always low):
	 * 15 13 12 11 10 09 08 07 06 05 04 03 02 01 00 XX
	 */
	MT32EMU_DAC_INPUT_MODE(GENERATION1),

	/**
	 * Re-orders the LA32 output bits as in later generations (personally confirmed on my CM-32L - KG).
	 * Bit order at DAC (where each number represents the original LA32 output bit number):
	 * 15 13 12 11 10 09 08 07 06 05 04 03 02 01 00 14
	 */
	MT32EMU_DAC_INPUT_MODE(GENERATION2)
};

/** Methods for emulating the effective delay of incoming MIDI messages introduced by a MIDI interface. */
enum MT32EMU_MIDI_DELAY_MODE_NAME {
	/** Process incoming MIDI events immediately. */
	MT32EMU_MIDI_DELAY_MODE(IMMEDIATE),

	/**
	 * Delay incoming short MIDI messages as if they where transferred via a MIDI cable to a real hardware unit and immediate sysex processing.
	 * This ensures more accurate timing of simultaneous NoteOn messages.
	 */
	MT32EMU_MIDI_DELAY_MODE(DELAY_SHORT_MESSAGES_ONLY),

	/** Delay all incoming MIDI events as if they where transferred via a MIDI cable to a real hardware unit.*/
	MT32EMU_MIDI_DELAY_MODE(DELAY_ALL)
};

/** Methods for emulating the effects of analogue circuits of real hardware units on the output signal. */
enum MT32EMU_ANALOG_OUTPUT_MODE_NAME {
	/** Only digital path is emulated. The output samples correspond to the digital signal at the DAC entrance. */
	MT32EMU_ANALOG_OUTPUT_MODE(DIGITAL_ONLY),
	/** Coarse emulation of LPF circuit. High frequencies are boosted, sample rate remains unchanged. */
	MT32EMU_ANALOG_OUTPUT_MODE(COARSE),
	/**
	 * Finer emulation of LPF circuit. Output signal is upsampled to 48 kHz to allow emulation of audible mirror spectra above 16 kHz,
	 * which is passed through the LPF circuit without significant attenuation.
	 */
	MT32EMU_ANALOG_OUTPUT_MODE(ACCURATE),
	/**
	 * Same as AnalogOutputMode_ACCURATE mode but the output signal is 2x oversampled, i.e. the output sample rate is 96 kHz.
	 * This makes subsequent resampling easier. Besides, due to nonlinear passband of the LPF emulated, it takes fewer number of MACs
	 * compared to a regular LPF FIR implementations.
	 */
	MT32EMU_ANALOG_OUTPUT_MODE(OVERSAMPLED)
};

enum MT32EMU_PARTIAL_STATE_NAME {
	MT32EMU_PARTIAL_STATE(INACTIVE),
	MT32EMU_PARTIAL_STATE(ATTACK),
	MT32EMU_PARTIAL_STATE(SUSTAIN),
	MT32EMU_PARTIAL_STATE(RELEASE)
};

enum MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME {
	/** Use this only when the speed is more important than the audio quality. */
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(FASTEST),
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(FAST),
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(GOOD),
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(BEST)
};

enum MT32EMU_RENDERER_TYPE_NAME {
	/** Use 16-bit signed samples in the renderer and the accurate wave generator model based on logarithmic fixed-point computations and LUTs. Maximum emulation accuracy and speed. */
	MT32EMU_RENDERER_TYPE(BIT16S),
	/** Use float samples in the renderer and simplified wave generator model. Maximum output quality and minimum noise. */
	MT32EMU_RENDERER_TYPE(FLOAT)
};

} // namespace MT32Emu

#undef MT32EMU_DAC_INPUT_MODE_NAME
#undef MT32EMU_DAC_INPUT_MODE

#undef MT32EMU_MIDI_DELAY_MODE_NAME
#undef MT32EMU_MIDI_DELAY_MODE

#undef MT32EMU_ANALOG_OUTPUT_MODE_NAME
#undef MT32EMU_ANALOG_OUTPUT_MODE

#undef MT32EMU_PARTIAL_STATE_NAME
#undef MT32EMU_PARTIAL_STATE

#undef MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME
#undef MT32EMU_SAMPLERATE_CONVERSION_QUALITY

#undef MT32EMU_RENDERER_TYPE_NAME
#undef MT32EMU_RENDERER_TYPE

#endif // #ifndef MT32EMU_ENUMERATIONS_H

#ifndef MT32EMU_SYNTH_H
#define MT32EMU_SYNTH_H

namespace MT32Emu {

class Analog;
class BReverbModel;
class Extensions;
class MemoryRegion;
class MidiEventQueue;
class Part;
class Poly;
class Partial;
class PartialManager;
class Renderer;
class ROMImage;

class PatchTempMemoryRegion;
class RhythmTempMemoryRegion;
class TimbreTempMemoryRegion;
class PatchesMemoryRegion;
class TimbresMemoryRegion;
class SystemMemoryRegion;
class DisplayMemoryRegion;
class ResetMemoryRegion;

struct ControlROMFeatureSet;
struct ControlROMMap;
struct PCMWaveEntry;
struct MemParams;

const Bit8u SYSEX_MANUFACTURER_ROLAND = 0x41;

const Bit8u SYSEX_MDL_MT32 = 0x16;
const Bit8u SYSEX_MDL_D50 = 0x14;

const Bit8u SYSEX_CMD_RQ1 = 0x11; // Request data #1
const Bit8u SYSEX_CMD_DT1 = 0x12; // Data set 1
const Bit8u SYSEX_CMD_WSD = 0x40; // Want to send data
const Bit8u SYSEX_CMD_RQD = 0x41; // Request data
const Bit8u SYSEX_CMD_DAT = 0x42; // Data set
const Bit8u SYSEX_CMD_ACK = 0x43; // Acknowledge
const Bit8u SYSEX_CMD_EOD = 0x45; // End of data
const Bit8u SYSEX_CMD_ERR = 0x4E; // Communications error
const Bit8u SYSEX_CMD_RJC = 0x4F; // Rejection

// This value isn't quite correct: the new-gen MT-32 control ROMs (ver. 2.XX) are twice as big.
// Nevertheless, this is still relevant for library internal usage because the higher half
// of those ROMs only contains the demo songs in all cases.
const Bit32u CONTROL_ROM_SIZE = 64 * 1024;

// Set of multiplexed output streams appeared at the DAC entrance.
template <class T>
struct DACOutputStreams {
	T *nonReverbLeft;
	T *nonReverbRight;
	T *reverbDryLeft;
	T *reverbDryRight;
	T *reverbWetLeft;
	T *reverbWetRight;
};

////DBP: Removed display and reporthandler
//class MT32EMU_EXPORT ReportHandler { ... }
class Synth {
friend class DefaultMidiStreamParser;
friend class MemoryRegion;
friend class Part;
friend class Partial;
friend class PartialManager;
friend class Poly;
friend class Renderer;
friend class RhythmPart;
friend class SamplerateAdapter;
friend class SoxrAdapter;
friend class TVA;
friend class TVF;
friend class TVP;

private:
	// **************************** Implementation fields **************************

	PatchTempMemoryRegion *patchTempMemoryRegion;
	RhythmTempMemoryRegion *rhythmTempMemoryRegion;
	TimbreTempMemoryRegion *timbreTempMemoryRegion;
	PatchesMemoryRegion *patchesMemoryRegion;
	TimbresMemoryRegion *timbresMemoryRegion;
	SystemMemoryRegion *systemMemoryRegion;
	DisplayMemoryRegion *displayMemoryRegion;
	ResetMemoryRegion *resetMemoryRegion;

	Bit8u *paddedTimbreMaxTable;

	PCMWaveEntry *pcmWaves; // Array

	const ControlROMFeatureSet *controlROMFeatures;
	const ControlROMMap *controlROMMap;
	Bit8u controlROMData[CONTROL_ROM_SIZE];
	Bit16s *pcmROMData;
	size_t pcmROMSize; // This is in 16-bit samples, therefore half the number of bytes in the ROM

	Bit8u soundGroupIx[128]; // For each standard timbre
	const char (*soundGroupNames)[9]; // Array

	Bit32u partialCount;
	Bit8u nukeme[16]; // FIXME: Nuke it. For binary compatibility only.

	MidiEventQueue *midiQueue;
	volatile Bit32u lastReceivedMIDIEventTimestamp;
	volatile Bit32u renderedSampleCount;

	MemParams &mt32ram, &mt32default;

	BReverbModel *reverbModels[4];
	BReverbModel *reverbModel;
	bool reverbOverridden;

	MIDIDelayMode midiDelayMode;
	DACInputMode dacInputMode;

	float outputGain;
	float reverbOutputGain;

	bool reversedStereoEnabled;

	bool opened;
	bool activated;

	////DBP: Removed display and reporthandler
	//bool isDefaultReportHandler; // No longer used, retained for binary compatibility only.
	//ReportHandler *reportHandler;

	PartialManager *partialManager;
	Part *parts[9];

	// When a partial needs to be aborted to free it up for use by a new Poly,
	// the controller will busy-loop waiting for the sound to finish.
	// We emulate this by delaying new MIDI events processing until abortion finishes.
	Poly *abortingPoly;

	Analog *analog;
	Renderer *renderer;

	// Binary compatibility helper.
	Extensions &extensions;

	// **************************** Implementation methods **************************

	Bit32u addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp);
	bool isAbortingPoly() const { return abortingPoly != NULL; }

	void writeSysexGlobal(Bit32u addr, const Bit8u *sysex, Bit32u len);
	void readSysex(Bit8u channel, const Bit8u *sysex, Bit32u len) const;
	void initMemoryRegions();
	void deleteMemoryRegions();
	MemoryRegion *findMemoryRegion(Bit32u addr);
	void writeMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, const Bit8u *data);
	void readMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, Bit8u *data);

	bool loadControlROM(const ROMImage &controlROMImage);
	bool loadPCMROM(const ROMImage &pcmROMImage);

	bool initPCMList(Bit16u mapAddress, Bit16u count);
	bool initTimbres(Bit16u mapAddress, Bit16u offset, Bit16u timbreCount, Bit16u startTimbre, bool compressed);
	bool initCompressedTimbre(Bit16u drumNum, const Bit8u *mem, Bit32u memLen);
	void initReverbModels(bool mt32CompatibleMode);
	void initSoundGroups(char newSoundGroupNames[][9]);

	void refreshSystemMasterTune();
	void refreshSystemReverbParameters();
	void refreshSystemReserveSettings();
	void refreshSystemChanAssign(Bit8u firstPart, Bit8u lastPart);
	void refreshSystemMasterVol();
	void refreshSystem();
	void reset();
	void dispose();

	void printPartialUsage(Bit32u sampleOffset = 0);

	//DBP: Made function that are empty for us inline
	inline void rhythmNotePlayed() const {}
	inline void voicePartStateChanged(Bit8u partNum, bool activated) const {}
	void newTimbreSet(Bit8u partNum) const;
	const char *getSoundGroupName(const Part *part) const;
	const char *getSoundGroupName(Bit8u timbreGroup, Bit8u timbreNumber) const;
	#if 0
	inline void printDebug(const char *fmt, ...) {va_list va;va_start(va, fmt);printf("[MT32] ");vprintf(fmt, va);printf("\n");va_end(va);}
	#else
	inline void printDebug(const char *fmt, ...) {}
	#endif

	// partNum should be 0..7 for Part 1..8, or 8 for Rhythm
	const Part *getPart(Bit8u partNum) const;

	void resetMasterTunePitchDelta();
	Bit32s getMasterTunePitchDelta() const;

public:
	static inline Bit16s clipSampleEx(Bit32s sampleEx) {
		// Clamp values above 32767 to 32767, and values below -32768 to -32768
		// FIXME: Do we really need this stuff? I think these branches are very well predicted. Instead, this introduces a chain.
		// The version below is actually a bit faster on my system...
		//return ((sampleEx + 0x8000) & ~0xFFFF) ? Bit16s((sampleEx >> 31) ^ 0x7FFF) : (Bit16s)sampleEx;
		return ((-0x8000 <= sampleEx) && (sampleEx <= 0x7FFF)) ? Bit16s(sampleEx) : Bit16s((sampleEx >> 31) ^ 0x7FFF);
	}

	static inline float clipSampleEx(float sampleEx) {
		return sampleEx;
	}

	template <class S>
	static inline void muteSampleBuffer(S *buffer, Bit32u len) {
		if (buffer == NULL) return;
		memset(buffer, 0, len * sizeof(S));
	}

	static inline void muteSampleBuffer(float *buffer, Bit32u len) {
		if (buffer == NULL) return;
		// FIXME: Use memset() where compatibility is guaranteed (if this turns out to be a win)
		while (len--) {
			*(buffer++) = 0.0f;
		}
	}

	static inline Bit16s convertSample(float sample) {
		return Synth::clipSampleEx(Bit32s(sample * 32768.0f)); // This multiplier corresponds to normalised floats
	}

	static inline float convertSample(Bit16s sample) {
		return float(sample) / 32768.0f; // This multiplier corresponds to normalised floats
	}

	// Returns library version as an integer in format: 0x00MMmmpp, where:
	// MM - major version number
	// mm - minor version number
	// pp - patch number
	MT32EMU_EXPORT static Bit32u getLibraryVersionInt();
	// Returns library version as a C-string in format: "MAJOR.MINOR.PATCH"
	MT32EMU_EXPORT static const char *getLibraryVersionString();

	MT32EMU_EXPORT static Bit32u getShortMessageLength(Bit32u msg);
	MT32EMU_EXPORT static Bit8u calcSysexChecksum(const Bit8u *data, const Bit32u len, const Bit8u initChecksum = 0);

	// Returns output sample rate used in emulation of stereo analog circuitry of hardware units.
	// See comment for AnalogOutputMode.
	MT32EMU_EXPORT static Bit32u getStereoOutputSampleRate(AnalogOutputMode analogOutputMode);

	// Optionally sets callbacks for reporting various errors, information and debug messages
	////DBP: Removed reporthandler argument
	MT32EMU_EXPORT explicit Synth();
	MT32EMU_EXPORT ~Synth();

	////DBP: Removed display and reporthandler
	//MT32EMU_EXPORT_V(2.6) void setReportHandler2(ReportHandler2 *reportHandler2);

	// Used to initialise the MT-32. Must be called before any other function.
	// Returns true if initialization was successful, otherwise returns false.
	// controlROMImage and pcmROMImage represent full Control and PCM ROM images for use by synth.
	// usePartialCount sets the maximum number of partials playing simultaneously for this session (optional).
	// analogOutputMode sets the mode for emulation of analogue circuitry of the hardware units (optional).
	MT32EMU_EXPORT bool open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, Bit32u usePartialCount = DEFAULT_MAX_PARTIALS, AnalogOutputMode analogOutputMode = AnalogOutputMode_COARSE);

	// Overloaded method which opens the synth with default partial count.
	MT32EMU_EXPORT bool open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, AnalogOutputMode analogOutputMode);

	// Closes the MT-32 and deallocates any memory used by the synthesizer
	MT32EMU_EXPORT void close();

	// Returns true if the synth is in completely initialized state, otherwise returns false.
	MT32EMU_EXPORT bool isOpen() const;

	// All the enqueued events are processed by the synth immediately.
	MT32EMU_EXPORT void flushMIDIQueue();

	// Sets size of the internal MIDI event queue. The queue size is set to the minimum power of 2 that is greater or equal to the size specified.
	// The queue is flushed before reallocation.
	// Returns the actual queue size being used.
	MT32EMU_EXPORT Bit32u setMIDIEventQueueSize(Bit32u requestedSize);

	// Configures the SysEx storage of the internal MIDI event queue.
	// Supplying 0 in the storageBufferSize argument makes the SysEx data stored
	// in multiple dynamically allocated buffers per MIDI event. These buffers are only disposed
	// when a new MIDI event replaces the SysEx event in the queue, thus never on the rendering thread.
	// This is the default behaviour.
	// In contrast, when a positive value is specified, SysEx data will be stored in a single preallocated buffer,
	// which makes this kind of storage safe for use in a realtime thread. Additionally, the space retained
	// by a SysEx event, that has been processed and thus is no longer necessary, is disposed instantly.
	// Note, the queue is flushed and recreated in the process so that its size remains intact.
	MT32EMU_EXPORT void configureMIDIEventQueueSysexStorage(Bit32u storageBufferSize);

	// Returns current value of the global counter of samples rendered since the synth was created (at the native sample rate 32000 Hz).
	// This method helps to compute accurate timestamp of a MIDI message to use with the methods below.
	MT32EMU_EXPORT Bit32u getInternalRenderedSampleCount() const;

	// Enqueues a MIDI event for subsequent playback.
	// The MIDI event will be processed not before the specified timestamp.
	// The timestamp is measured as the global rendered sample count since the synth was created (at the native sample rate 32000 Hz).
	// The minimum delay involves emulation of the delay introduced while the event is transferred via MIDI interface
	// and emulation of the MCU busy-loop while it frees partials for use by a new Poly.
	// Calls from multiple threads must be synchronised, although, no synchronisation is required with the rendering thread.
	// The methods return false if the MIDI event queue is full and the message cannot be enqueued.

	// Enqueues a single short MIDI message to play at specified time. The message must contain a status byte.
	MT32EMU_EXPORT bool playMsg(Bit32u msg, Bit32u timestamp);
	// Enqueues a single well formed System Exclusive MIDI message to play at specified time.
	MT32EMU_EXPORT bool playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp);

	// Enqueues a single short MIDI message to be processed ASAP. The message must contain a status byte.
	MT32EMU_EXPORT bool playMsg(Bit32u msg);
	// Enqueues a single well formed System Exclusive MIDI message to be processed ASAP.
	MT32EMU_EXPORT bool playSysex(const Bit8u *sysex, Bit32u len);

	// WARNING:
	// The methods below don't ensure minimum 1-sample delay between sequential MIDI events,
	// and a sequence of NoteOn and immediately succeeding NoteOff messages is always silent.
	// A thread that invokes these methods must be explicitly synchronised with the thread performing sample rendering.

	// Sends a short MIDI message to the synth for immediate playback. The message must contain a status byte.
	// See the WARNING above.
	MT32EMU_EXPORT void playMsgNow(Bit32u msg);
	// Sends unpacked short MIDI message to the synth for immediate playback. The message must contain a status byte.
	// See the WARNING above.
	MT32EMU_EXPORT void playMsgOnPart(Bit8u part, Bit8u code, Bit8u note, Bit8u velocity);

	// Sends a single well formed System Exclusive MIDI message for immediate processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexNow(const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexWithoutFraming(const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexWithoutHeader(Bit8u device, Bit8u command, const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void writeSysex(Bit8u channel, const Bit8u *sysex, Bit32u len);

	// Allows to disable wet reverb output altogether.
	MT32EMU_EXPORT void setReverbEnabled(bool reverbEnabled);
	// Returns whether wet reverb output is enabled.
	MT32EMU_EXPORT bool isReverbEnabled() const;
	// Sets override reverb mode. In this mode, emulation ignores sysexes (or the related part of them) which control the reverb parameters.
	// This mode is in effect until it is turned off. When the synth is re-opened, the override mode is unchanged but the state
	// of the reverb model is reset to default.
	MT32EMU_EXPORT void setReverbOverridden(bool reverbOverridden);
	// Returns whether reverb settings are overridden.
	MT32EMU_EXPORT bool isReverbOverridden() const;
	// Forces reverb model compatibility mode. By default, the compatibility mode corresponds to the used control ROM version.
	// Invoking this method with the argument set to true forces emulation of old MT-32 reverb circuit.
	// When the argument is false, emulation of the reverb circuit used in new generation of MT-32 compatible modules is enforced
	// (these include CM-32L and LAPC-I).
	MT32EMU_EXPORT void setReverbCompatibilityMode(bool mt32CompatibleMode);
	// Returns whether reverb is in old MT-32 compatibility mode.
	MT32EMU_EXPORT bool isMT32ReverbCompatibilityMode() const;
	// Returns whether default reverb compatibility mode is the old MT-32 compatibility mode.
	MT32EMU_EXPORT bool isDefaultReverbMT32Compatible() const;
	// If enabled, reverb buffers for all modes are kept around allocated all the time to avoid memory
	// allocating/freeing in the rendering thread, which may be required for realtime operation.
	// Otherwise, reverb buffers that are not in use are deleted to save memory (the default behaviour).
	MT32EMU_EXPORT void preallocateReverbMemory(bool enabled);
	// Sets new DAC input mode. See DACInputMode for details.
	MT32EMU_EXPORT void setDACInputMode(DACInputMode mode);
	// Returns current DAC input mode. See DACInputMode for details.
	MT32EMU_EXPORT DACInputMode getDACInputMode() const;
	// Sets new MIDI delay mode. See MIDIDelayMode for details.
	MT32EMU_EXPORT void setMIDIDelayMode(MIDIDelayMode mode);
	// Returns current MIDI delay mode. See MIDIDelayMode for details.
	MT32EMU_EXPORT MIDIDelayMode getMIDIDelayMode() const;

	// Sets output gain factor for synth output channels. Applied to all output samples and unrelated with the synth's Master volume,
	// it rather corresponds to the gain of the output analog circuitry of the hardware units. However, together with setReverbOutputGain()
	// it offers to the user a capability to control the gain of reverb and non-reverb output channels independently.
	MT32EMU_EXPORT void setOutputGain(float gain);
	// Returns current output gain factor for synth output channels.
	MT32EMU_EXPORT float getOutputGain() const;

	// Sets output gain factor for the reverb wet output channels. It rather corresponds to the gain of the output
	// analog circuitry of the hardware units. However, together with setOutputGain() it offers to the user a capability
	// to control the gain of reverb and non-reverb output channels independently.
	//
	// Note: We're currently emulate CM-32L/CM-64 reverb quite accurately and the reverb output level closely
	// corresponds to the level of digital capture. Although, according to the CM-64 PCB schematic,
	// there is a difference in the reverb analogue circuit, and the resulting output gain is 0.68
	// of that for LA32 analogue output. This factor is applied to the reverb output gain.
	MT32EMU_EXPORT void setReverbOutputGain(float gain);
	// Returns current output gain factor for reverb wet output channels.
	MT32EMU_EXPORT float getReverbOutputGain() const;

	// Swaps left and right output channels.
	MT32EMU_EXPORT void setReversedStereoEnabled(bool enabled);
	// Returns whether left and right output channels are swapped.
	MT32EMU_EXPORT bool isReversedStereoEnabled() const;

	// Allows to toggle the NiceAmpRamp mode.
	// In this mode, we want to ensure that amp ramp never jumps to the target
	// value and always gradually increases or decreases. It seems that real units
	// do not bother to always check if a newly started ramp leads to a jump.
	// We also prefer the quality improvement over the emulation accuracy,
	// so this mode is enabled by default.
	MT32EMU_EXPORT void setNiceAmpRampEnabled(bool enabled);
	// Returns whether NiceAmpRamp mode is enabled.
	MT32EMU_EXPORT bool isNiceAmpRampEnabled() const;

	// Allows to toggle the NicePanning mode.
	// Despite the Roland's manual specifies allowed panpot values in range 0-14,
	// the LA-32 only receives 3-bit pan setting in fact. In particular, this
	// makes it impossible to set the "middle" panning for a single partial.
	// In the NicePanning mode, we enlarge the pan setting accuracy to 4 bits
	// making it smoother thus sacrificing the emulation accuracy.
	// This mode is disabled by default.
	MT32EMU_EXPORT void setNicePanningEnabled(bool enabled);
	// Returns whether NicePanning mode is enabled.
	MT32EMU_EXPORT bool isNicePanningEnabled() const;

	// Allows to toggle the NicePartialMixing mode.
	// LA-32 is known to mix partials either in-phase (so that they are added)
	// or in counter-phase (so that they are subtracted instead).
	// In some cases, this quirk isn't highly desired because a pair of closely
	// sounding partials may occasionally cancel out.
	// In the NicePartialMixing mode, the mixing is always performed in-phase,
	// thus making the behaviour more predictable.
	// This mode is disabled by default.
	MT32EMU_EXPORT void setNicePartialMixingEnabled(bool enabled);
	// Returns whether NicePartialMixing mode is enabled.
	MT32EMU_EXPORT bool isNicePartialMixingEnabled() const;

	// Selects new type of the wave generator and renderer to be used during subsequent calls to open().
	// By default, RendererType_BIT16S is selected.
	// See RendererType for details.
	MT32EMU_EXPORT void selectRendererType(RendererType);
	// Returns previously selected type of the wave generator and renderer.
	// See RendererType for details.
	MT32EMU_EXPORT RendererType getSelectedRendererType() const;

	// Returns actual sample rate used in emulation of stereo analog circuitry of hardware units.
	// See comment for render() below.
	MT32EMU_EXPORT Bit32u getStereoOutputSampleRate() const;

	// Renders samples to the specified output stream as if they were sampled at the analog stereo output.
	// When AnalogOutputMode is set to ACCURATE (OVERSAMPLED), the output signal is upsampled to 48 (96) kHz in order
	// to retain emulation accuracy in whole audible frequency spectra. Otherwise, native digital signal sample rate is retained.
	// getStereoOutputSampleRate() can be used to query actual sample rate of the output signal.
	// The length is in frames, not bytes (in 16-bit stereo, one frame is 4 bytes). Uses NATIVE byte ordering.
	MT32EMU_EXPORT void render(Bit16s *stream, Bit32u len);
	// Same as above but outputs to a float stereo stream.
	MT32EMU_EXPORT void render(float *stream, Bit32u len);

	// Renders samples to the specified output streams as if they appeared at the DAC entrance.
	// No further processing performed in analog circuitry emulation is applied to the signal.
	// NULL may be specified in place of any or all of the stream buffers to skip it.
	// The length is in samples, not bytes. Uses NATIVE byte ordering.
	MT32EMU_EXPORT void renderStreams(Bit16s *nonReverbLeft, Bit16s *nonReverbRight, Bit16s *reverbDryLeft, Bit16s *reverbDryRight, Bit16s *reverbWetLeft, Bit16s *reverbWetRight, Bit32u len);
	MT32EMU_EXPORT void renderStreams(const DACOutputStreams<Bit16s> &streams, Bit32u len);
	// Same as above but outputs to float streams.
	MT32EMU_EXPORT void renderStreams(float *nonReverbLeft, float *nonReverbRight, float *reverbDryLeft, float *reverbDryRight, float *reverbWetLeft, float *reverbWetRight, Bit32u len);
	MT32EMU_EXPORT void renderStreams(const DACOutputStreams<float> &streams, Bit32u len);

	// Returns true when there is at least one active partial, otherwise false.
	MT32EMU_EXPORT bool hasActivePartials() const;

	// Returns true if the synth is active and subsequent calls to render() may result in non-trivial output (i.e. silence).
	// The synth is considered active when either there are pending MIDI events in the queue, there is at least one active partial,
	// or the reverb is (somewhat unreliably) detected as being active.
	MT32EMU_EXPORT bool isActive();

	// Returns the maximum number of partials playing simultaneously.
	MT32EMU_EXPORT Bit32u getPartialCount() const;

	// Fills in current states of all the parts into the array provided. The array must have at least 9 entries to fit values for all the parts.
	// If the value returned for a part is true, there is at least one active non-releasing partial playing on this part.
	// This info is useful in emulating behaviour of LCD display of the hardware units.
	MT32EMU_EXPORT void getPartStates(bool *partStates) const;

	// Returns current states of all the parts as a bit set. The least significant bit corresponds to the state of part 1,
	// total of 9 bits hold the states of all the parts. If the returned bit for a part is set, there is at least one active
	// non-releasing partial playing on this part. This info is useful in emulating behaviour of LCD display of the hardware units.
	MT32EMU_EXPORT Bit32u getPartStates() const;

	// Fills in current states of all the partials into the array provided. The array must be large enough to accommodate states of all the partials.
	MT32EMU_EXPORT void getPartialStates(PartialState *partialStates) const;

	// Fills in current states of all the partials into the array provided. Each byte in the array holds states of 4 partials
	// starting from the least significant bits. The state of each partial is packed in a pair of bits.
	// The array must be large enough to accommodate states of all the partials (see getPartialCount()).
	MT32EMU_EXPORT void getPartialStates(Bit8u *partialStates) const;

	// Fills in information about currently playing notes on the specified part into the arrays provided. The arrays must be large enough
	// to accommodate data for all the playing notes. The maximum number of simultaneously playing notes cannot exceed the number of partials.
	// Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
	// Returns the number of currently playing notes on the specified part.
	MT32EMU_EXPORT Bit32u getPlayingNotes(Bit8u partNumber, Bit8u *keys, Bit8u *velocities) const;

	// Returns name of the patch set on the specified part.
	// Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
	// The returned value is a null-terminated string which is guaranteed to remain valid until the next call to one of render methods.
	MT32EMU_EXPORT const char *getPatchName(Bit8u partNumber) const;

	// Retrieves the name of the sound group the timbre identified by arguments timbreGroup and timbreNumber is associated with.
	// Values 0-3 of timbreGroup correspond to the timbre banks GROUP A, GROUP B, MEMORY and RHYTHM.
	// For all but the RHYTHM timbre bank, allowed values of timbreNumber are in range 0-63. The number of timbres
	// contained in the RHYTHM bank depends on the used control ROM version.
	// The argument soundGroupName must point to an array of at least 8 characters. The result is a null-terminated string.
	// Returns whether the specified timbre has been found and the result written in soundGroupName.
	MT32EMU_EXPORT bool getSoundGroupName(char *soundGroupName, Bit8u timbreGroup, Bit8u timbreNumber) const;
	// Retrieves the name of the timbre identified by arguments timbreGroup and timbreNumber.
	// Values 0-3 of timbreGroup correspond to the timbre banks GROUP A, GROUP B, MEMORY and RHYTHM.
	// For all but the RHYTHM timbre bank, allowed values of timbreNumber are in range 0-63. The number of timbres
	// contained in the RHYTHM bank depends on the used control ROM version.
	// The argument soundName must point to an array of at least 11 characters. The result is a null-terminated string.
	// Returns whether the specified timbre has been found and the result written in soundName.
	MT32EMU_EXPORT bool getSoundName(char *soundName, Bit8u timbreGroup, Bit8u timbreNumber) const;

	// Stores internal state of emulated synth into an array provided (as it would be acquired from hardware).
	MT32EMU_EXPORT void readMemory(Bit32u addr, Bit32u len, Bit8u *data);

	////DBP: Removed display and reporthandler
	//MT32EMU_EXPORT_V(2.6) bool getDisplayState(char *targetBuffer, bool narrowLCD = false) const;
	//MT32EMU_EXPORT_V(2.6) void setMainDisplayMode();
	//MT32EMU_EXPORT_V(2.6) void setDisplayCompatibility(bool oldMT32CompatibilityEnabled);
	//MT32EMU_EXPORT_V(2.6) bool isDisplayOldMT32Compatible() const;
	//MT32EMU_EXPORT_V(2.6) bool isDefaultDisplayOldMT32Compatible() const;
}; // class Synth

} // namespace MT32Emu

#endif // #ifndef MT32EMU_SYNTH_H

#ifndef MT32EMU_CONFIG_H
#define MT32EMU_CONFIG_H

#define MT32EMU_VERSION      "2.7.1"
#define MT32EMU_VERSION_MAJOR 2
#define MT32EMU_VERSION_MINOR 7
#define MT32EMU_VERSION_PATCH 1

#endif

#ifndef MT32EMU_INTERNALS_H
#define MT32EMU_INTERNALS_H

// Debugging

// 0: Standard debug output is not stamped with the rendered sample count
// 1: Standard debug output is stamped with the rendered sample count
// NOTE: The "samplestamp" corresponds to the end of the last completed rendering run.
//       This is important to bear in mind for debug output that occurs during a run.
#ifndef MT32EMU_DEBUG_SAMPLESTAMPS
#define MT32EMU_DEBUG_SAMPLESTAMPS 0
#endif

// 0: No debug output for initialisation progress
// 1: Debug output for initialisation progress
#ifndef MT32EMU_MONITOR_INIT
#define MT32EMU_MONITOR_INIT 1
#endif

// 0: No debug output for MIDI events
// 1: Debug output for weird MIDI events
#ifndef MT32EMU_MONITOR_MIDI
#define MT32EMU_MONITOR_MIDI 1
#endif

// 0: No debug output for note on/off
// 1: Basic debug output for note on/off
// 2: Comprehensive debug output for note on/off
#ifndef MT32EMU_MONITOR_INSTRUMENTS
#define MT32EMU_MONITOR_INSTRUMENTS 0
#endif

// 0: No debug output for partial allocations
// 1: Show partial stats when an allocation fails
// 2: Show partial stats with every new poly
// 3: Show individual partial allocations/deactivations
#ifndef MT32EMU_MONITOR_PARTIALS
#define MT32EMU_MONITOR_PARTIALS 0
#endif

// 0: No debug output for sysex
// 1: Basic debug output for sysex
#ifndef MT32EMU_MONITOR_SYSEX
#define MT32EMU_MONITOR_SYSEX 1
#endif

// 0: No debug output for sysex writes to the timbre areas
// 1: Debug output with the name and location of newly-written timbres
// 2: Complete dump of timbre parameters for newly-written timbres
#ifndef MT32EMU_MONITOR_TIMBRES
#define MT32EMU_MONITOR_TIMBRES 0
#endif

// 0: No TVA/TVF-related debug output.
// 1: Shows changes to TVA/TVF target, increment and phase.
#ifndef MT32EMU_MONITOR_TVA
#define MT32EMU_MONITOR_TVA 0
#endif
#ifndef MT32EMU_MONITOR_TVF
#define MT32EMU_MONITOR_TVF 0
#endif

// Configuration

// 0: Maximum speed at the cost of a bit lower emulation accuracy.
// 1: Maximum achievable emulation accuracy.
#ifndef MT32EMU_BOSS_REVERB_PRECISE_MODE
#define MT32EMU_BOSS_REVERB_PRECISE_MODE 0
#endif

namespace MT32Emu {

typedef Bit16s IntSample;
typedef Bit32s IntSampleEx;
typedef float FloatSample;

enum PolyState {
	POLY_Playing,
	POLY_Held, // This marks keys that have been released on the keyboard, but are being held by the pedal
	POLY_Releasing,
	POLY_Inactive
};

enum ReverbMode {
	REVERB_MODE_ROOM,
	REVERB_MODE_HALL,
	REVERB_MODE_PLATE,
	REVERB_MODE_TAP_DELAY
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_INTERNALS_H

#ifndef MT32EMU_STRUCTURES_H
#define MT32EMU_STRUCTURES_H

namespace MT32Emu {

// MT32EMU_MEMADDR() converts from sysex-padded, MT32EMU_SYSEXMEMADDR converts to it
// Roland provides documentation using the sysex-padded addresses, so we tend to use that in code and output
#define MT32EMU_MEMADDR(x) ((((x) & 0x7f0000) >> 2) | (((x) & 0x7f00) >> 1) | ((x) & 0x7f))
#define MT32EMU_SYSEXMEMADDR(x) ((((x) & 0x1FC000) << 2) | (((x) & 0x3F80) << 1) | ((x) & 0x7f))

#ifdef _MSC_VER
#define  MT32EMU_ALIGN_PACKED __declspec(align(1))
#else
#define MT32EMU_ALIGN_PACKED __attribute__((packed))
#endif

// The following structures represent the MT-32's memory
// Since sysex allows this memory to be written to in blocks of bytes,
// we keep this packed so that we can copy data into the various
// banks directly
#if defined(_MSC_VER) || defined(__MINGW32__)
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif

struct TimbreParam {
	struct CommonParam {
		char name[10];
		Bit8u partialStructure12;  // 1 & 2  0-12 (1-13)
		Bit8u partialStructure34;  // 3 & 4  0-12 (1-13)
		Bit8u partialMute;  // 0-15 (0000-1111)
		Bit8u noSustain; // ENV MODE 0-1 (Normal, No sustain)
	} MT32EMU_ALIGN_PACKED common;

	struct PartialParam {
		struct WGParam {
			Bit8u pitchCoarse;  // 0-96 (C1,C#1-C9)
			Bit8u pitchFine;  // 0-100 (-50 to +50 (cents - confirmed by Mok))
			Bit8u pitchKeyfollow;  // 0-16 (-1, -1/2, -1/4, 0, 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, 1, 5/4, 3/2, 2, s1, s2)
			Bit8u pitchBenderEnabled;  // 0-1 (OFF, ON)
			Bit8u waveform; // MT-32: 0-1 (SQU/SAW); LAPC-I: WG WAVEFORM/PCM BANK 0 - 3 (SQU/1, SAW/1, SQU/2, SAW/2)
			Bit8u pcmWave; // 0-127 (1-128)
			Bit8u pulseWidth; // 0-100
			Bit8u pulseWidthVeloSensitivity; // 0-14 (-7 - +7)
		} MT32EMU_ALIGN_PACKED wg;

		struct PitchEnvParam {
			Bit8u depth; // 0-10
			Bit8u veloSensitivity; // 0-100
			Bit8u timeKeyfollow; // 0-4
			Bit8u time[4]; // 0-100
			Bit8u level[5]; // 0-100 (-50 - +50) // [3]: SUSTAIN LEVEL, [4]: END LEVEL
		} MT32EMU_ALIGN_PACKED pitchEnv;

		struct PitchLFOParam {
			Bit8u rate; // 0-100
			Bit8u depth; // 0-100
			Bit8u modSensitivity; // 0-100
		} MT32EMU_ALIGN_PACKED pitchLFO;

		struct TVFParam {
			Bit8u cutoff; // 0-100
			Bit8u resonance; // 0-30
			Bit8u keyfollow; // -1, -1/2, -1/4, 0, 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, 1, 5/4, 3/2, 2
			Bit8u biasPoint; // 0-127 (<1A-<7C >1A-7C)
			Bit8u biasLevel; // 0-14 (-7 - +7)
			Bit8u envDepth; // 0-100
			Bit8u envVeloSensitivity; // 0-100
			Bit8u envDepthKeyfollow; // DEPTH KEY FOLL0W 0-4
			Bit8u envTimeKeyfollow; // TIME KEY FOLLOW 0-4
			Bit8u envTime[5]; // 0-100
			Bit8u envLevel[4]; // 0-100 // [3]: SUSTAIN LEVEL
		} MT32EMU_ALIGN_PACKED tvf;

		struct TVAParam {
			Bit8u level; // 0-100
			Bit8u veloSensitivity; // 0-100
			Bit8u biasPoint1; // 0-127 (<1A-<7C >1A-7C)
			Bit8u biasLevel1; // 0-12 (-12 - 0)
			Bit8u biasPoint2; // 0-127 (<1A-<7C >1A-7C)
			Bit8u biasLevel2; // 0-12 (-12 - 0)
			Bit8u envTimeKeyfollow; // TIME KEY FOLLOW 0-4
			Bit8u envTimeVeloSensitivity; // VELOS KEY FOLL0W 0-4
			Bit8u envTime[5]; // 0-100
			Bit8u envLevel[4]; // 0-100 // [3]: SUSTAIN LEVEL
		} MT32EMU_ALIGN_PACKED tva;
	} MT32EMU_ALIGN_PACKED partial[4]; // struct PartialParam
} MT32EMU_ALIGN_PACKED; // struct TimbreParam

struct PatchParam {
	Bit8u timbreGroup; // TIMBRE GROUP  0-3 (group A, group B, Memory, Rhythm)
	Bit8u timbreNum; // TIMBRE NUMBER 0-63
	Bit8u keyShift; // KEY SHIFT 0-48 (-24 - +24 semitones)
	Bit8u fineTune; // FINE TUNE 0-100 (-50 - +50 cents)
	Bit8u benderRange; // BENDER RANGE 0-24
	Bit8u assignMode;  // ASSIGN MODE 0-3 (POLY1, POLY2, POLY3, POLY4)
	Bit8u reverbSwitch;  // REVERB SWITCH 0-1 (OFF,ON)
	Bit8u dummy; // (DUMMY)
} MT32EMU_ALIGN_PACKED;

const unsigned int SYSTEM_MASTER_TUNE_OFF = 0;
const unsigned int SYSTEM_REVERB_MODE_OFF = 1;
const unsigned int SYSTEM_REVERB_TIME_OFF = 2;
const unsigned int SYSTEM_REVERB_LEVEL_OFF = 3;
const unsigned int SYSTEM_RESERVE_SETTINGS_START_OFF = 4;
const unsigned int SYSTEM_RESERVE_SETTINGS_END_OFF = 12;
const unsigned int SYSTEM_CHAN_ASSIGN_START_OFF = 13;
const unsigned int SYSTEM_CHAN_ASSIGN_END_OFF = 21;
const unsigned int SYSTEM_MASTER_VOL_OFF = 22;

struct MemParams {
	// NOTE: The MT-32 documentation only specifies PatchTemp areas for parts 1-8.
	// The LAPC-I documentation specified an additional area for rhythm at the end,
	// where all parameters but fine tune, assign mode and output level are ignored
	struct PatchTemp {
		PatchParam patch;
		Bit8u outputLevel; // OUTPUT LEVEL 0-100
		Bit8u panpot; // PANPOT 0-14 (R-L)
		Bit8u dummyv[6];
	} MT32EMU_ALIGN_PACKED patchTemp[9];

	struct RhythmTemp {
		Bit8u timbre; // TIMBRE  0-94 (M1-M64,R1-30,OFF); LAPC-I: 0-127 (M01-M64,R01-R63)
		Bit8u outputLevel; // OUTPUT LEVEL 0-100
		Bit8u panpot; // PANPOT 0-14 (R-L)
		Bit8u reverbSwitch;  // REVERB SWITCH 0-1 (OFF,ON)
	} MT32EMU_ALIGN_PACKED rhythmTemp[85];

	TimbreParam timbreTemp[8];

	PatchParam patches[128];

	// NOTE: There are only 30 timbres in the "rhythm" bank for MT-32; the additional 34 are for LAPC-I and above
	struct PaddedTimbre {
		TimbreParam timbre;
		Bit8u padding[10];
	} MT32EMU_ALIGN_PACKED timbres[64 + 64 + 64 + 64]; // Group A, Group B, Memory, Rhythm

	struct System {
		Bit8u masterTune; // MASTER TUNE 0-127 432.1-457.6Hz
		Bit8u reverbMode; // REVERB MODE 0-3 (room, hall, plate, tap delay)
		Bit8u reverbTime; // REVERB TIME 0-7 (1-8)
		Bit8u reverbLevel; // REVERB LEVEL 0-7 (1-8)
		Bit8u reserveSettings[9]; // PARTIAL RESERVE (PART 1) 0-32
		Bit8u chanAssign[9]; // MIDI CHANNEL (PART1) 0-16 (1-16,OFF)
		Bit8u masterVol; // MASTER VOLUME 0-100
	} MT32EMU_ALIGN_PACKED system;
}; // struct MemParams

struct SoundGroup {
	Bit8u timbreNumberTableAddrLow;
	Bit8u timbreNumberTableAddrHigh;
	Bit8u displayPosition;
	Bit8u name[9];
	Bit8u timbreCount;
	Bit8u pad;
} MT32EMU_ALIGN_PACKED;

#if defined(_MSC_VER) || defined(__MINGW32__)
#pragma pack(pop)
#else
#pragma pack()
#endif

struct ControlROMFeatureSet {
	unsigned int quirkBasePitchOverflow : 1;
	unsigned int quirkPitchEnvelopeOverflow : 1;
	unsigned int quirkRingModulationNoMix : 1;
	unsigned int quirkTVAZeroEnvLevels : 1;
	unsigned int quirkPanMult : 1;
	unsigned int quirkKeyShift : 1;
	unsigned int quirkTVFBaseCutoffLimit : 1;
	unsigned int quirkFastPitchChanges : 1;
	unsigned int quirkDisplayCustomMessagePriority : 1;
	unsigned int oldMT32DisplayFeatures : 1;

	// Features below don't actually depend on control ROM version, which is used to identify hardware model
	unsigned int defaultReverbMT32Compatible : 1;
	unsigned int oldMT32AnalogLPF : 1;
};

struct ControlROMMap {
	const char *shortName;
	const ControlROMFeatureSet &featureSet;
	Bit16u pcmTable; // 4 * pcmCount bytes
	Bit16u pcmCount;
	Bit16u timbreAMap; // 128 bytes
	Bit16u timbreAOffset;
	bool timbreACompressed;
	Bit16u timbreBMap; // 128 bytes
	Bit16u timbreBOffset;
	bool timbreBCompressed;
	Bit16u timbreRMap; // 2 * timbreRCount bytes
	Bit16u timbreRCount;
	Bit16u rhythmSettings; // 4 * rhythmSettingsCount bytes
	Bit16u rhythmSettingsCount;
	Bit16u reserveSettings; // 9 bytes
	Bit16u panSettings; // 8 bytes
	Bit16u programSettings; // 8 bytes
	Bit16u rhythmMaxTable; // 4 bytes
	Bit16u patchMaxTable; // 16 bytes
	Bit16u systemMaxTable; // 23 bytes
	Bit16u timbreMaxTable; // 72 bytes
	Bit16u soundGroupsTable; // 14 bytes each entry
	Bit16u soundGroupsCount;
	Bit16u startupMessage; // 20 characters + NULL terminator
	Bit16u sysexErrorMessage; // 20 characters + NULL terminator
};

struct ControlROMPCMStruct {
	Bit8u pos;
	Bit8u len;
	Bit8u pitchLSB;
	Bit8u pitchMSB;
};

struct PCMWaveEntry {
	Bit32u addr;
	Bit32u len;
	bool loop;
	ControlROMPCMStruct *controlROMPCMStruct;
};

// This is basically a per-partial, pre-processed combination of timbre and patch/rhythm settings
struct PatchCache {
	bool playPartial;
	bool PCMPartial;
	int pcm;
	Bit8u waveform;

	Bit32u structureMix;
	int structurePosition;
	int structurePair;

	// The following fields are actually common to all partials in the timbre
	bool dirty;
	Bit32u partialCount;
	bool sustain;
	bool reverb;

	TimbreParam::PartialParam srcPartial;

	// The following directly points into live sysex-addressable memory
	const TimbreParam::PartialParam *partialParam;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_STRUCTURES_H

#ifndef MT32EMU_ANALOG_H
#define MT32EMU_ANALOG_H

namespace MT32Emu {

/* Analog class is dedicated to perform fair emulation of analogue circuitry of hardware units that is responsible
 * for processing output signal after the DAC. It appears that the analogue circuit labeled "LPF" on the schematic
 * also applies audible changes to the signal spectra. There is a significant boost of higher frequencies observed
 * aside from quite poor attenuation of the mirror spectra above 16 kHz which is due to a relatively low filter order.
 *
 * As the final mixing of multiplexed output signal is performed after the DAC, this function is migrated here from Synth.
 * Saying precisely, mixing is performed within the LPF as the entrance resistors are actually components of a LPF
 * designed using the multiple feedback topology. Nevertheless, the schematic separates them.
 */
class Analog {
public:
	static Analog *createAnalog(const AnalogOutputMode mode, const bool oldMT32AnalogLPF, const RendererType rendererType);

	virtual ~Analog() {}
	virtual unsigned int getOutputSampleRate() const = 0;
	virtual Bit32u getDACStreamsLength(const Bit32u outputLength) const = 0;
	virtual void setSynthOutputGain(const float synthGain) = 0;
	virtual void setReverbOutputGain(const float reverbGain, const bool mt32ReverbCompatibilityMode) = 0;

	virtual bool process(IntSample *outStream, const IntSample *nonReverbLeft, const IntSample *nonReverbRight, const IntSample *reverbDryLeft, const IntSample *reverbDryRight, const IntSample *reverbWetLeft, const IntSample *reverbWetRight, Bit32u outLength) = 0;
	virtual bool process(FloatSample *outStream, const FloatSample *nonReverbLeft, const FloatSample *nonReverbRight, const FloatSample *reverbDryLeft, const FloatSample *reverbDryRight, const FloatSample *reverbWetLeft, const FloatSample *reverbWetRight, Bit32u outLength) = 0;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_ANALOG_H

#ifndef MT32EMU_B_REVERB_MODEL_H
#define MT32EMU_B_REVERB_MODEL_H

namespace MT32Emu {

class BReverbModel {
public:
	static BReverbModel *createBReverbModel(const ReverbMode mode, const bool mt32CompatibleModel, const RendererType rendererType);

	virtual ~BReverbModel() {}
	virtual bool isOpen() const = 0;
	// After construction or a close(), open() must be called at least once before any other call (with the exception of close()).
	virtual void open() = 0;
	// May be called multiple times without an open() in between.
	virtual void close() = 0;
	virtual void mute() = 0;
	virtual void setParameters(Bit8u time, Bit8u level) = 0;
	virtual bool isActive() const = 0;
	virtual bool isMT32Compatible(const ReverbMode mode) const = 0;
	virtual bool process(const IntSample *inLeft, const IntSample *inRight, IntSample *outLeft, IntSample *outRight, Bit32u numSamples) = 0;
	virtual bool process(const FloatSample *inLeft, const FloatSample *inRight, FloatSample *outLeft, FloatSample *outRight, Bit32u numSamples) = 0;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_B_REVERB_MODEL_H

#ifndef MT32EMU_MEMORY_REGION_H
#define MT32EMU_MEMORY_REGION_H

namespace MT32Emu {

enum MemoryRegionType {
	MR_PatchTemp, MR_RhythmTemp, MR_TimbreTemp, MR_Patches, MR_Timbres, MR_System, MR_Display, MR_Reset
};

class Synth;

class MemoryRegion {
private:
	Synth *synth;
	Bit8u *realMemory;
	Bit8u *maxTable;
public:
	MemoryRegionType type;
	Bit32u startAddr, entrySize, entries;

	MemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable, MemoryRegionType useType, Bit32u useStartAddr, Bit32u useEntrySize, Bit32u useEntries) {
		synth = useSynth;
		realMemory = useRealMemory;
		maxTable = useMaxTable;
		type = useType;
		startAddr = useStartAddr;
		entrySize = useEntrySize;
		entries = useEntries;
	}
	int lastTouched(Bit32u addr, Bit32u len) const {
		return (offset(addr) + len - 1) / entrySize;
	}
	int firstTouchedOffset(Bit32u addr) const {
		return offset(addr) % entrySize;
	}
	int firstTouched(Bit32u addr) const {
		return offset(addr) / entrySize;
	}
	Bit32u regionEnd() const {
		return startAddr + entrySize * entries;
	}
	bool contains(Bit32u addr) const {
		return addr >= startAddr && addr < regionEnd();
	}
	int offset(Bit32u addr) const {
		return addr - startAddr;
	}
	Bit32u getClampedLen(Bit32u addr, Bit32u len) const {
		if (addr + len > regionEnd())
			return regionEnd() - addr;
		return len;
	}
	Bit32u next(Bit32u addr, Bit32u len) const {
		if (addr + len > regionEnd()) {
			return regionEnd() - addr;
		}
		return 0;
	}
	Bit8u getMaxValue(int off) const {
		if (maxTable == NULL)
			return 0xFF;
		return maxTable[off % entrySize];
	}
	Bit8u *getRealMemory() const {
		return realMemory;
	}
	bool isReadable() const {
		return getRealMemory() != NULL;
	}
	void read(unsigned int entry, unsigned int off, Bit8u *dst, unsigned int len) const;
	void write(unsigned int entry, unsigned int off, const Bit8u *src, unsigned int len, bool init = false) const;
}; // class MemoryRegion

class PatchTempMemoryRegion : public MemoryRegion {
public:
	PatchTempMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_PatchTemp, MT32EMU_MEMADDR(0x030000), sizeof(MemParams::PatchTemp), 9) {}
};
class RhythmTempMemoryRegion : public MemoryRegion {
public:
	RhythmTempMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_RhythmTemp, MT32EMU_MEMADDR(0x030110), sizeof(MemParams::RhythmTemp), 85) {}
};
class TimbreTempMemoryRegion : public MemoryRegion {
public:
	TimbreTempMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_TimbreTemp, MT32EMU_MEMADDR(0x040000), sizeof(TimbreParam), 8) {}
};
class PatchesMemoryRegion : public MemoryRegion {
public:
	PatchesMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_Patches, MT32EMU_MEMADDR(0x050000), sizeof(PatchParam), 128) {}
};
class TimbresMemoryRegion : public MemoryRegion {
public:
	TimbresMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_Timbres, MT32EMU_MEMADDR(0x080000), sizeof(MemParams::PaddedTimbre), 64 + 64 + 64 + 64) {}
};
class SystemMemoryRegion : public MemoryRegion {
public:
	SystemMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_System, MT32EMU_MEMADDR(0x100000), sizeof(MemParams::System), 1) {}
};
class DisplayMemoryRegion : public MemoryRegion {
public:
	// Note, we set realMemory to NULL despite the real devices buffer inbound strings. However, it is impossible to retrieve them.
	// This entrySize permits emulation of handling a 20-byte display message sent to an old-gen device at address 0x207F7F.
	DisplayMemoryRegion(Synth *useSynth) : MemoryRegion(useSynth, NULL, NULL, MR_Display, MT32EMU_MEMADDR(0x200000), 0x4013, 1) {}
};
class ResetMemoryRegion : public MemoryRegion {
public:
	ResetMemoryRegion(Synth *useSynth) : MemoryRegion(useSynth, NULL, NULL, MR_Reset, MT32EMU_MEMADDR(0x7F0000), 0x3FFF, 1) {}
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_MEMORY_REGION_H

#ifndef MT32EMU_MIDI_EVENT_QUEUE_H
#define MT32EMU_MIDI_EVENT_QUEUE_H

namespace MT32Emu {

/**
 * Simple queue implementation using a ring buffer to store incoming MIDI event before the synth actually processes it.
 * It is intended to:
 * - get rid of prerenderer while retaining graceful partial abortion
 * - add fair emulation of the MIDI interface delays
 * - extend the synth interface with the default implementation of a typical rendering loop.
 * THREAD SAFETY:
 * It is safe to use either in a single thread environment or when there are only two threads - one performs only reading
 * and one performs only writing. More complicated usage requires external synchronisation.
 */
class MidiEventQueue {
public:
	class SysexDataStorage;

	struct MidiEvent {
		const Bit8u *sysexData;
		union {
			Bit32u sysexLength;
			Bit32u shortMessageData;
		};
		Bit32u timestamp;
	};

	explicit MidiEventQueue(
		// Must be a power of 2
		Bit32u ringBufferSize,
		Bit32u storageBufferSize
	);
	~MidiEventQueue();
	void reset();
	bool pushShortMessage(Bit32u shortMessageData, Bit32u timestamp);
	bool pushSysex(const Bit8u *sysexData, Bit32u sysexLength, Bit32u timestamp);
	const volatile MidiEvent *peekMidiEvent();
	void dropMidiEvent();
	inline bool isEmpty() const;

private:
	SysexDataStorage &sysexDataStorage;

	MidiEvent * const ringBuffer;
	const Bit32u ringBufferMask;
	volatile Bit32u startPosition;
	volatile Bit32u endPosition;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_MIDI_EVENT_QUEUE_H

#ifndef MT32EMU_PART_H
#define MT32EMU_PART_H

namespace MT32Emu {

class Poly;
class Synth;

class PolyList {
private:
	Poly *firstPoly;
	Poly *lastPoly;

public:
	PolyList();
	bool isEmpty() const;
	Poly *getFirst() const;
	Poly *getLast() const;
	void prepend(Poly *poly);
	void append(Poly *poly);
	Poly *takeFirst();
	void remove(Poly * const poly);
};

class Part {
private:
	// Direct pointer to sysex-addressable memory dedicated to this part (valid for parts 1-8, NULL for rhythm)
	TimbreParam *timbreTemp;

	// 0=Part 1, .. 7=Part 8, 8=Rhythm
	unsigned int partNum;

	bool holdpedal;

	unsigned int activePartialCount;
	unsigned int activeNonReleasingPolyCount;
	PatchCache patchCache[4];
	PolyList activePolys;

	void setPatch(const PatchParam *patch);
	unsigned int midiKeyToKey(unsigned int midiKey);

	bool abortFirstPoly(unsigned int key);

protected:
	Synth *synth;
	// Direct pointer into sysex-addressable memory
	MemParams::PatchTemp *patchTemp;
	char name[8]; // "Part 1".."Part 8", "Rhythm"
	char currentInstr[11];
	// Values outside the valid range 0..100 imply no override.
	Bit8u volumeOverride;
	Bit8u modulation;
	Bit8u expression;
	Bit32s pitchBend;
	bool nrpn;
	Bit16u rpn;
	Bit16u pitchBenderRange; // (patchTemp->patch.benderRange * 683) at the time of the last MIDI program change or MIDI data entry.

	void backupCacheToPartials(PatchCache cache[4]);
	void cacheTimbre(PatchCache cache[4], const TimbreParam *timbre);
	void playPoly(const PatchCache cache[4], const MemParams::RhythmTemp *rhythmTemp, unsigned int midiKey, unsigned int key, unsigned int velocity);
	void stopNote(unsigned int key);
	const char *getName() const;

public:
	Part(Synth *synth, unsigned int usePartNum);
	virtual ~Part();
	void reset();
	void setDataEntryMSB(unsigned char midiDataEntryMSB);
	void setNRPN();
	void setRPNLSB(unsigned char midiRPNLSB);
	void setRPNMSB(unsigned char midiRPNMSB);
	void resetAllControllers();
	virtual void noteOn(unsigned int midiKey, unsigned int velocity);
	virtual void noteOff(unsigned int midiKey);
	void allNotesOff();
	void allSoundOff();
	Bit8u getVolume() const; // Effective output level, valid range 0..100.
	void setVolume(unsigned int midiVolume); // Valid range 0..127, as defined for MIDI controller 7.
	Bit8u getVolumeOverride() const;
	void setVolumeOverride(Bit8u volumeOverride);
	Bit8u getModulation() const;
	void setModulation(unsigned int midiModulation);
	Bit8u getExpression() const;
	void setExpression(unsigned int midiExpression);
	virtual void setPan(unsigned int midiPan);
	Bit32s getPitchBend() const;
	void setBend(unsigned int midiBend);
	virtual void setProgram(unsigned int midiProgram);
	void setHoldPedal(bool pedalval);
	void stopPedalHold();
	void updatePitchBenderRange();
	virtual void refresh();
	virtual void refreshTimbre(unsigned int absTimbreNum);
	virtual void setTimbre(TimbreParam *timbre);
	virtual unsigned int getAbsTimbreNum() const;
	const char *getCurrentInstr() const;
	const Poly *getFirstActivePoly() const;
	unsigned int getActivePartialCount() const;
	unsigned int getActiveNonReleasingPartialCount() const;
	Synth *getSynth() const;

	const MemParams::PatchTemp *getPatchTemp() const;

	// This should only be called by Poly
	void partialDeactivated(Poly *poly);
	virtual void polyStateChanged(PolyState oldState, PolyState newState);

	// These are rather specialised, and should probably only be used by PartialManager
	bool abortFirstPoly(PolyState polyState);
	// Abort the first poly in PolyState_HELD, or if none exists, the first active poly in any state.
	bool abortFirstPolyPreferHeld();
	bool abortFirstPoly();
}; // class Part

class RhythmPart: public Part {
	// Pointer to the area of the MT-32's memory dedicated to rhythm
	const MemParams::RhythmTemp *rhythmTemp;

	// This caches the timbres/settings in use by the rhythm part
	PatchCache drumCache[85][4];
public:
	RhythmPart(Synth *synth, unsigned int usePartNum);
	void refresh();
	void refreshTimbre(unsigned int timbreNum);
	void setTimbre(TimbreParam *timbre);
	void noteOn(unsigned int key, unsigned int velocity);
	void noteOff(unsigned int midiKey);
	unsigned int getAbsTimbreNum() const;
	void setPan(unsigned int midiPan);
	void setProgram(unsigned int patchNum);
	void polyStateChanged(PolyState oldState, PolyState newState);
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_PART_H

#ifndef MT32EMU_LA32RAMP_H
#define MT32EMU_LA32RAMP_H

namespace MT32Emu {

class LA32Ramp {
private:
	Bit32u current;
	unsigned int largeTarget;
	unsigned int largeIncrement;
	bool descending;

	int interruptCountdown;
	bool interruptRaised;

public:
	LA32Ramp();
	void startRamp(Bit8u target, Bit8u increment);
	Bit32u nextValue();
	bool checkInterrupt();
	void reset();
	bool isBelowCurrent(Bit8u target) const;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_LA32RAMP_H

#ifndef MT32EMU_LA32_WAVE_GENERATOR_H
#define MT32EMU_LA32_WAVE_GENERATOR_H

namespace MT32Emu {

/**
 * LA32 performs wave generation in the log-space that allows replacing multiplications by cheap additions
 * It's assumed that only low-bit multiplications occur in a few places which are unavoidable like these:
 * - interpolation of exponent table (obvious, a delta value has 4 bits)
 * - computation of resonance amp decay envelope (the table contains values with 1-2 "1" bits except the very first value 31 but this case can be found using inversion)
 * - interpolation of PCM samples (obvious, the wave position counter is in the linear space, there is no log() table in the chip)
 * and it seems to be implemented in the same way as in the Boss chip, i.e. right shifted additions which involved noticeable precision loss
 * Subtraction is supposed to be replaced by simple inversion
 * As the logarithmic sine is always negative, all the logarithmic values are treated as decrements
 */
struct LogSample {
	// 16-bit fixed point value, includes 12-bit fractional part
	// 4-bit integer part allows to present any 16-bit sample in the log-space
	// Obviously, the log value doesn't contain the sign of the resulting sample
	Bit16u logValue;
	enum {
		POSITIVE,
		NEGATIVE
	} sign;
};

class LA32Utilites {
public:
	static Bit16u interpolateExp(const Bit16u fract);
	static Bit16s unlog(const LogSample &logSample);
	static void addLogSamples(LogSample &logSample1, const LogSample &logSample2);
};

/**
 * LA32WaveGenerator is aimed to represent the exact model of LA32 wave generator.
 * The output square wave is created by adding high / low linear segments in-between
 * the rising and falling cosine segments. Basically, it's very similar to the phase distortion synthesis.
 * Behaviour of a true resonance filter is emulated by adding decaying sine wave.
 * The beginning and the ending of the resonant sine is multiplied by a cosine window.
 * To synthesise sawtooth waves, the resulting square wave is multiplied by synchronous cosine wave.
 */
class LA32WaveGenerator {
	//***************************************************************************
	//  The local copy of partial parameters below
	//***************************************************************************

	bool active;

	// True means the resulting square wave is to be multiplied by the synchronous cosine
	bool sawtoothWaveform;

	// Logarithmic amp of the wave generator
	Bit32u amp;

	// Logarithmic frequency of the resulting wave
	Bit16u pitch;

	// Values in range [1..31]
	// Value 1 correspong to the minimum resonance
	Bit8u resonance;

	// Processed value in range [0..255]
	// Values in range [0..128] have no effect and the resulting wave remains symmetrical
	// Value 255 corresponds to the maximum possible asymmetric of the resulting wave
	Bit8u pulseWidth;

	// Composed of the base cutoff in range [78..178] left-shifted by 18 bits and the TVF modifier
	Bit32u cutoffVal;

	// Logarithmic PCM sample start address
	const Bit16s *pcmWaveAddress;

	// Logarithmic PCM sample length
	Bit32u pcmWaveLength;

	// true for looped logarithmic PCM samples
	bool pcmWaveLooped;

	// false for slave PCM partials in the structures with the ring modulation
	bool pcmWaveInterpolated;

	//***************************************************************************
	// Internal variables below
	//***************************************************************************

	// Relative position within either the synth wave or the PCM sampled wave
	// 0 - start of the positive rising sine segment of the square wave or start of the PCM sample
	// 1048576 (2^20) - end of the negative rising sine segment of the square wave
	// For PCM waves, the address of the currently playing sample equals (wavePosition / 256)
	Bit32u wavePosition;

	// Relative position within a square wave phase:
	// 0             - start of the phase
	// 262144 (2^18) - end of a sine phase in the square wave
	Bit32u squareWavePosition;

	// Relative position within the positive or negative wave segment:
	// 0 - start of the corresponding positive or negative segment of the square wave
	// 262144 (2^18) - corresponds to end of the first sine phase in the square wave
	// The same increment sampleStep is used to indicate the current position
	// since the length of the resonance wave is always equal to four square wave sine segments.
	Bit32u resonanceSinePosition;

	// The amp of the resonance sine wave grows with the resonance value
	// As the resonance value cannot change while the partial is active, it is initialised once
	Bit32u resonanceAmpSubtraction;

	// The decay speed of resonance sine wave, depends on the resonance value
	Bit32u resAmpDecayFactor;

	// Fractional part of the pcmPosition
	Bit32u pcmInterpolationFactor;

	// Current phase of the square wave
	enum {
		POSITIVE_RISING_SINE_SEGMENT,
		POSITIVE_LINEAR_SEGMENT,
		POSITIVE_FALLING_SINE_SEGMENT,
		NEGATIVE_FALLING_SINE_SEGMENT,
		NEGATIVE_LINEAR_SEGMENT,
		NEGATIVE_RISING_SINE_SEGMENT
	} phase;

	// Current phase of the resonance wave
	enum ResonancePhase {
		POSITIVE_RISING_RESONANCE_SINE_SEGMENT,
		POSITIVE_FALLING_RESONANCE_SINE_SEGMENT,
		NEGATIVE_FALLING_RESONANCE_SINE_SEGMENT,
		NEGATIVE_RISING_RESONANCE_SINE_SEGMENT
	} resonancePhase;

	// Resulting log-space samples of the square and resonance waves
	LogSample squareLogSample;
	LogSample resonanceLogSample;

	// Processed neighbour log-space samples of the PCM wave
	LogSample firstPCMLogSample;
	LogSample secondPCMLogSample;

	//***************************************************************************
	// Internal methods below
	//***************************************************************************

	Bit32u getSampleStep();
	Bit32u getResonanceWaveLengthFactor(Bit32u effectiveCutoffValue);
	Bit32u getHighLinearLength(Bit32u effectiveCutoffValue);

	void computePositions(Bit32u highLinearLength, Bit32u lowLinearLength, Bit32u resonanceWaveLengthFactor);
	void advancePosition();

	void generateNextSquareWaveLogSample();
	void generateNextResonanceWaveLogSample();
	void generateNextSawtoothCosineLogSample(LogSample &logSample) const;

	void pcmSampleToLogSample(LogSample &logSample, const Bit16s pcmSample) const;
	void generateNextPCMWaveLogSamples();

public:
	// Initialise the WG engine for generation of synth partial samples and set up the invariant parameters
	void initSynth(const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance);

	// Initialise the WG engine for generation of PCM partial samples and set up the invariant parameters
	void initPCM(const Bit16s * const pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped, const bool pcmWaveInterpolated);

	// Update parameters with respect to TVP, TVA and TVF, and generate next sample
	void generateNextSample(const Bit32u amp, const Bit16u pitch, const Bit32u cutoff);

	// WG output in the log-space consists of two components which are to be added (or ring modulated) in the linear-space afterwards
	LogSample getOutputLogSample(const bool first) const;

	// Deactivate the WG engine
	void deactivate();

	// Return active state of the WG engine
	bool isActive() const;

	// Return true if the WG engine generates PCM wave samples
	bool isPCMWave() const;

	// Return current PCM interpolation factor
	Bit32u getPCMInterpolationFactor() const;
}; // class LA32WaveGenerator

// LA32PartialPair contains a structure of two partials being mixed / ring modulated
class LA32PartialPair {
public:
	enum PairType {
		MASTER,
		SLAVE
	};

	virtual ~LA32PartialPair() {}

	// ringModulated should be set to false for the structures with mixing or stereo output
	// ringModulated should be set to true for the structures with ring modulation
	// mixed is used for the structures with ring modulation and indicates whether the master partial output is mixed to the ring modulator output
	virtual void init(const bool ringModulated, const bool mixed) = 0;

	// Initialise the WG engine for generation of synth partial samples and set up the invariant parameters
	virtual void initSynth(const PairType master, const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance) = 0;

	// Initialise the WG engine for generation of PCM partial samples and set up the invariant parameters
	virtual void initPCM(const PairType master, const Bit16s * const pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped) = 0;

	// Deactivate the WG engine
	virtual void deactivate(const PairType master) = 0;
}; // class LA32PartialPair

class LA32IntPartialPair : public LA32PartialPair {
	LA32WaveGenerator master;
	LA32WaveGenerator slave;
	bool ringModulated;
	bool mixed;

	static Bit16s unlogAndMixWGOutput(const LA32WaveGenerator &wg);

public:
	// ringModulated should be set to false for the structures with mixing or stereo output
	// ringModulated should be set to true for the structures with ring modulation
	// mixed is used for the structures with ring modulation and indicates whether the master partial output is mixed to the ring modulator output
	void init(const bool ringModulated, const bool mixed);

	// Initialise the WG engine for generation of synth partial samples and set up the invariant parameters
	void initSynth(const PairType master, const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance);

	// Initialise the WG engine for generation of PCM partial samples and set up the invariant parameters
	void initPCM(const PairType master, const Bit16s * const pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped);

	// Update parameters with respect to TVP, TVA and TVF, and generate next sample
	void generateNextSample(const PairType master, const Bit32u amp, const Bit16u pitch, const Bit32u cutoff);

	// Perform mixing / ring modulation of WG output and return the result
	// Although, LA32 applies panning itself, we assume it is applied in the mixer, not within a pair
	Bit16s nextOutSample();

	// Deactivate the WG engine
	void deactivate(const PairType master);

	// Return active state of the WG engine
	bool isActive(const PairType master) const;
}; // class LA32IntPartialPair

} // namespace MT32Emu

#endif // #ifndef MT32EMU_LA32_WAVE_GENERATOR_H

#ifndef MT32EMU_LA32_FLOAT_WAVE_GENERATOR_H
#define MT32EMU_LA32_FLOAT_WAVE_GENERATOR_H

namespace MT32Emu {

/**
 * LA32WaveGenerator is aimed to represent the exact model of LA32 wave generator.
 * The output square wave is created by adding high / low linear segments in-between
 * the rising and falling cosine segments. Basically, it's very similar to the phase distortion synthesis.
 * Behaviour of a true resonance filter is emulated by adding decaying sine wave.
 * The beginning and the ending of the resonant sine is multiplied by a cosine window.
 * To synthesise sawtooth waves, the resulting square wave is multiplied by synchronous cosine wave.
 */
class LA32FloatWaveGenerator {
	//***************************************************************************
	//  The local copy of partial parameters below
	//***************************************************************************

	bool active;

	// True means the resulting square wave is to be multiplied by the synchronous cosine
	bool sawtoothWaveform;

	// Values in range [1..31]
	// Value 1 correspong to the minimum resonance
	Bit8u resonance;

	// Processed value in range [0..255]
	// Values in range [0..128] have no effect and the resulting wave remains symmetrical
	// Value 255 corresponds to the maximum possible asymmetric of the resulting wave
	Bit8u pulseWidth;

	// Logarithmic PCM sample start address
	const Bit16s *pcmWaveAddress;

	// Logarithmic PCM sample length
	Bit32u pcmWaveLength;

	// true for looped logarithmic PCM samples
	bool pcmWaveLooped;

	// false for slave PCM partials in the structures with the ring modulation
	bool pcmWaveInterpolated;

	//***************************************************************************
	// Internal variables below
	//***************************************************************************

	float wavePos;
	float lastFreq;
	float pcmPosition;

	float getPCMSample(unsigned int position);

public:
	// Initialise the WG engine for generation of synth partial samples and set up the invariant parameters
	void initSynth(const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance);

	// Initialise the WG engine for generation of PCM partial samples and set up the invariant parameters
	void initPCM(const Bit16s * const pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped, const bool pcmWaveInterpolated);

	// Update parameters with respect to TVP, TVA and TVF, and generate next sample
	float generateNextSample(const Bit32u amp, const Bit16u pitch, const Bit32u cutoff);

	// Deactivate the WG engine
	void deactivate();

	// Return active state of the WG engine
	bool isActive() const;

	// Return true if the WG engine generates PCM wave samples
	bool isPCMWave() const;
}; // class LA32FloatWaveGenerator

class LA32FloatPartialPair : public LA32PartialPair {
	LA32FloatWaveGenerator master;
	LA32FloatWaveGenerator slave;
	bool ringModulated;
	bool mixed;
	float masterOutputSample;
	float slaveOutputSample;

public:
	// ringModulated should be set to false for the structures with mixing or stereo output
	// ringModulated should be set to true for the structures with ring modulation
	// mixed is used for the structures with ring modulation and indicates whether the master partial output is mixed to the ring modulator output
	void init(const bool ringModulated, const bool mixed);

	// Initialise the WG engine for generation of synth partial samples and set up the invariant parameters
	void initSynth(const PairType master, const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance);

	// Initialise the WG engine for generation of PCM partial samples and set up the invariant parameters
	void initPCM(const PairType master, const Bit16s * const pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped);

	// Update parameters with respect to TVP, TVA and TVF, and generate next sample
	void generateNextSample(const PairType master, const Bit32u amp, const Bit16u pitch, const Bit32u cutoff);

	// Perform mixing / ring modulation and return the result
	float nextOutSample();

	// Deactivate the WG engine
	void deactivate(const PairType master);

	// Return active state of the WG engine
	bool isActive(const PairType master) const;
}; // class LA32FloatPartialPair

} // namespace MT32Emu

#endif // #ifndef MT32EMU_LA32_FLOAT_WAVE_GENERATOR_H

#ifndef MT32EMU_PARTIAL_H
#define MT32EMU_PARTIAL_H

namespace MT32Emu {

class Part;
class Poly;
class Synth;
class TVA;
class TVF;
class TVP;
struct ControlROMPCMStruct;

// A partial represents one of up to four waveform generators currently playing within a poly.
class Partial {
private:
	Synth *synth;
	const int partialIndex; // Index of this Partial in the global partial table
	// Number of the sample currently being rendered by produceOutput(), or 0 if no run is in progress
	// This is only kept available for debugging purposes.
	Bit32u sampleNum;

	// Actually, LA-32 receives only 3 bits as a pan setting, but we abuse these to emulate
	// the inverted partial mixing as well. Also we double the values (making them correspond
	// to the panpot range) to enable NicePanning mode, with respect to MoK.
	Bit32s leftPanValue, rightPanValue;

	int ownerPart; // -1 if unassigned
	int mixType;
	int structurePosition; // 0 or 1 of a structure pair

	// Only used for PCM partials
	int pcmNum;
	// FIXME: Give this a better name (e.g. pcmWaveInfo)
	PCMWaveEntry *pcmWave;

	// Final pulse width value, with velfollow applied, matching what is sent to the LA32.
	// Range: 0-255
	int pulseWidthVal;

	Poly *poly;
	Partial *pair;

	TVA *tva;
	TVP *tvp;
	TVF *tvf;

	LA32Ramp ampRamp;
	LA32Ramp cutoffModifierRamp;

	// TODO: This should be owned by PartialPair
	LA32PartialPair *la32Pair;
	const bool floatMode;

	const PatchCache *patchCache;
	PatchCache cachebackup;

	Bit32u getAmpValue();
	Bit32u getCutoffValue();

	template <class Sample, class LA32PairImpl>
	bool doProduceOutput(Sample *leftBuf, Sample *rightBuf, Bit32u length, LA32PairImpl *la32PairImpl);
	bool canProduceOutput();
	template <class LA32PairImpl>
	bool generateNextSample(LA32PairImpl *la32PairImpl);
	void produceAndMixSample(IntSample *&leftBuf, IntSample *&rightBuf, LA32IntPartialPair *la32IntPair);
	void produceAndMixSample(FloatSample *&leftBuf, FloatSample *&rightBuf, LA32FloatPartialPair *la32FloatPair);

public:
	bool alreadyOutputed;

	Partial(Synth *synth, int debugPartialNum);
	~Partial();

	int debugGetPartialNum() const;
	Bit32u debugGetSampleNum() const;

	int getOwnerPart() const;
	const Poly *getPoly() const;
	bool isActive() const;
	void activate(int part);
	void deactivate(void);
	void startPartial(const Part *part, Poly *usePoly, const PatchCache *useCache, const MemParams::RhythmTemp *rhythmTemp, Partial *pairPartial);
	void startAbort();
	void startDecayAll();
	bool shouldReverb();
	bool isRingModulatingNoMix() const;
	bool hasRingModulatingSlave() const;
	bool isRingModulatingSlave() const;
	bool isPCM() const;
	const ControlROMPCMStruct *getControlROMPCMStruct() const;
	Synth *getSynth() const;
	TVA *getTVA() const;

	void backupCache(const PatchCache &cache);

	// Returns true only if data written to buffer
	// These functions produce processed stereo samples
	// made from combining this single partial with its pair, if it has one.
	bool produceOutput(IntSample *leftBuf, IntSample *rightBuf, Bit32u length);
	bool produceOutput(FloatSample *leftBuf, FloatSample *rightBuf, Bit32u length);
}; // class Partial

} // namespace MT32Emu

#endif // #ifndef MT32EMU_PARTIAL_H

#ifndef MT32EMU_PARTIALMANAGER_H
#define MT32EMU_PARTIALMANAGER_H

namespace MT32Emu {

class Part;
class Partial;
class Poly;
class Synth;

class PartialManager {
private:
	Synth *synth;
	Part **parts;
	Poly **freePolys;
	Partial **partialTable;
	Bit8u numReservedPartialsForPart[9];
	Bit32u firstFreePolyIndex;
	int *inactivePartials; // Holds indices of inactive Partials in the Partial table
	Bit32u inactivePartialCount;

	bool abortFirstReleasingPolyWhereReserveExceeded(int minPart);
	bool abortFirstPolyPreferHeldWhereReserveExceeded(int minPart);

public:
	PartialManager(Synth *synth, Part **parts);
	~PartialManager();
	Partial *allocPartial(int partNum);
	unsigned int getFreePartialCount();
	void getPerPartPartialUsage(unsigned int perPartPartialUsage[9]);
	bool freePartials(unsigned int needed, int partNum);
	unsigned int setReserve(Bit8u *rset);
	void deactivateAll();
	bool produceOutput(int i, IntSample *leftBuf, IntSample *rightBuf, Bit32u bufferLength);
	bool produceOutput(int i, FloatSample *leftBuf, FloatSample *rightBuf, Bit32u bufferLength);
	bool shouldReverb(int i);
	void clearAlreadyOutputed();
	const Partial *getPartial(unsigned int partialNum) const;
	Poly *assignPolyToPart(Part *part);
	void polyFreed(Poly *poly);
	void partialDeactivated(int partialIndex);
}; // class PartialManager

} // namespace MT32Emu

#endif // #ifndef MT32EMU_PARTIALMANAGER_H

#ifndef MT32EMU_POLY_H
#define MT32EMU_POLY_H

namespace MT32Emu {

class Part;
class Partial;
struct PatchCache;

class Poly {
private:
	Part *part;
	unsigned int key;
	unsigned int velocity;
	unsigned int activePartialCount;
	bool sustain;

	PolyState state;

	Partial *partials[4];

	Poly *next;

	void setState(PolyState state);

public:
	Poly();
	void setPart(Part *usePart);
	void reset(unsigned int key, unsigned int velocity, bool sustain, Partial **partials);
	bool noteOff(bool pedalHeld);
	bool stopPedalHold();
	bool startDecay();
	bool startAbort();

	void backupCacheToPartials(PatchCache cache[4]);

	unsigned int getKey() const;
	unsigned int getVelocity() const;
	bool canSustain() const;
	PolyState getState() const;
	unsigned int getActivePartialCount() const;
	bool isActive() const;

	void partialDeactivated(Partial *partial);

	Poly *getNext() const;
	void setNext(Poly *poly);
}; // class Poly

} // namespace MT32Emu

#endif // #ifndef MT32EMU_POLY_H

#ifndef MT32EMU_TVA_H
#define MT32EMU_TVA_H

namespace MT32Emu {

class LA32Ramp;
class Part;
class Partial;

// Note that when entering nextPhase(), newPhase is set to phase + 1, and the descriptions/names below refer to
// newPhase's value.
enum {
	// In this phase, the base amp (as calculated in calcBasicAmp()) is targeted with an instant time.
	// This phase is entered by reset() only if time[0] != 0.
	TVA_PHASE_BASIC = 0,

	// In this phase, level[0] is targeted within time[0], and velocity potentially affects time
	TVA_PHASE_ATTACK = 1,

	// In this phase, level[1] is targeted within time[1]
	TVA_PHASE_2 = 2,

	// In this phase, level[2] is targeted within time[2]
	TVA_PHASE_3 = 3,

	// In this phase, level[3] is targeted within time[3]
	TVA_PHASE_4 = 4,

	// In this phase, immediately goes to PHASE_RELEASE unless the poly is set to sustain.
	// Aborts the partial if level[3] is 0.
	// Otherwise level[3] is continued, no phase change will occur until some external influence (like pedal release)
	TVA_PHASE_SUSTAIN = 5,

	// In this phase, 0 is targeted within time[4] (the time calculation is quite different from the other phases)
	TVA_PHASE_RELEASE = 6,

	// It's PHASE_DEAD, Jim.
	TVA_PHASE_DEAD = 7
};

class TVA {
private:
	const Partial * const partial;
	LA32Ramp *ampRamp;
	const MemParams::System * const system;

	const Part *part;
	const TimbreParam::PartialParam *partialParam;
	const MemParams::RhythmTemp *rhythmTemp;

	bool playing;

	int biasAmpSubtraction;
	int veloAmpSubtraction;
	int keyTimeSubtraction;

	Bit8u target;
	int phase;

	void startRamp(Bit8u newTarget, Bit8u newIncrement, int newPhase);
	void end(int newPhase);
	void nextPhase();

public:
	TVA(const Partial *partial, LA32Ramp *ampRamp);
	void reset(const Part *part, const TimbreParam::PartialParam *partialParam, const MemParams::RhythmTemp *rhythmTemp);
	void handleInterrupt();
	void recalcSustain();
	void startDecay();
	void startAbort();

	bool isPlaying() const;
	int getPhase() const;
}; // class TVA

} // namespace MT32Emu

#endif // #ifndef MT32EMU_TVA_H

#ifndef MT32EMU_TVF_H
#define MT32EMU_TVF_H

namespace MT32Emu {

class LA32Ramp;
class Partial;

class TVF {
private:
	const Partial * const partial;
	LA32Ramp *cutoffModifierRamp;
	const TimbreParam::PartialParam *partialParam;

	Bit8u baseCutoff;
	int keyTimeSubtraction;
	unsigned int levelMult;

	Bit8u target;
	unsigned int phase;

	void startRamp(Bit8u newTarget, Bit8u newIncrement, int newPhase);
	void nextPhase();

public:
	TVF(const Partial *partial, LA32Ramp *cutoffModifierRamp);
	void reset(const TimbreParam::PartialParam *partialParam, Bit32u basePitch);
	// Returns the base cutoff (without envelope modification).
	// The base cutoff is calculated when reset() is called and remains static
	// for the lifetime of the partial.
	// Barring bugs, the number returned is confirmed accurate
	// (based on specs from Mok).
	Bit8u getBaseCutoff() const;
	void handleInterrupt();
	void startDecay();
}; // class TVF

} // namespace MT32Emu

#endif // #ifndef MT32EMU_TVF_H

#ifndef MT32EMU_TVP_H
#define MT32EMU_TVP_H

namespace MT32Emu {

class Part;
class Partial;

class TVP {
private:
	const Partial * const partial;
	const MemParams::System * const system; // FIXME: Only necessary because masterTune calculation is done in the wrong place atm.

	const Part *part;
	const TimbreParam::PartialParam *partialParam;
	const MemParams::PatchTemp *patchTemp;

        const int processTimerTicksPerSampleX16;
	int processTimerIncrement;
	int counter;
	Bit32u timeElapsed;

	int phase;
	Bit32u basePitch;
	Bit32s targetPitchOffsetWithoutLFO;
	Bit32s currentPitchOffset;

	Bit16s lfoPitchOffset;
	// In range -12 - 36
	Bit8s timeKeyfollowSubtraction;

	Bit16s pitchOffsetChangePerBigTick;
	Bit16u targetPitchOffsetReachedBigTick;
	unsigned int shifts;

	Bit16u pitch;

	void updatePitch();
	void setupPitchChange(int targetPitchOffset, Bit8u changeDuration);
	void targetPitchOffsetReached();
	void nextPhase();
	void process();
public:
	TVP(const Partial *partial);
	void reset(const Part *part, const TimbreParam::PartialParam *partialParam);
	Bit32u getBasePitch() const;
	Bit16u nextPitch();
	void startDecay();
}; // class TVP

} // namespace MT32Emu

#endif // #ifndef MT32EMU_TVP_H

#ifndef MT32EMU_TABLES_H
#define MT32EMU_TABLES_H

namespace MT32Emu {

class Tables {
private:
	Tables();
	Tables(Tables &);
	~Tables() {}

public:
	static const Tables &getInstance();

	// Constant LUTs

	// CONFIRMED: This is used to convert several parameters to amp-modifying values in the TVA envelope:
	// - PatchTemp.outputLevel
	// - RhythmTemp.outlevel
	// - PartialParam.tva.level
	// - expression
	// It's used to determine how much to subtract from the amp envelope's target value
	Bit8u levelToAmpSubtraction[101];

	// CONFIRMED: ...
	Bit8u envLogarithmicTime[256];

	// CONFIRMED: ...
	Bit8u masterVolToAmpSubtraction[101];

	// CONFIRMED:
	Bit8u pulseWidth100To255[101];

	Bit16u exp9[512];
	Bit16u logsin9[512];

	const Bit8u *resAmpDecayFactor;
}; // class Tables

} // namespace MT32Emu

#endif // #ifndef MT32EMU_TABLES_H

#ifndef MT32EMU_MMATH_H
#define MT32EMU_MMATH_H

namespace MT32Emu {

// Mathematical constants
const double DOUBLE_PI = 3.141592653589793;
const double DOUBLE_LN_10 = 2.302585092994046;
const float FLOAT_PI = 3.1415927f;
const float FLOAT_2PI = 6.2831853f;
const float FLOAT_LN_2 = 0.6931472f;
const float FLOAT_LN_10 = 2.3025851f;

static inline float POWF(float x, float y) {
	return pow(x, y);
}

static inline float EXPF(float x) {
	return exp(x);
}

static inline float EXP2F(float x) {
#ifdef __APPLE__
	// on OSX exp2f() is 1.59 times faster than "exp() and the multiplication with FLOAT_LN_2"
	return exp2f(x);
#else
	return exp(FLOAT_LN_2 * x);
#endif
}

static inline float EXP10F(float x) {
	return exp(FLOAT_LN_10 * x);
}

static inline float LOGF(float x) {
	return log(x);
}

static inline float LOG2F(float x) {
	return log(x) / FLOAT_LN_2;
}

static inline float LOG10F(float x) {
	return log10(x);
}

} // namespace MT32Emu

#endif // #ifndef MT32EMU_MMATH_H

/************************************************** Synth.cpp ******************************************************/
#define produceDistortedSample SYNTH_CPP_produceDistortedSample

namespace MT32Emu {

// MIDI interface data transfer rate in samples. Used to simulate the transfer delay.
static const double MIDI_DATA_TRANSFER_RATE = double(SAMPLE_RATE) / 31250.0 * 8.0;

static const ControlROMFeatureSet OLD_MT32_ELDER = {
	true,  // quirkBasePitchOverflow
	true,  // quirkPitchEnvelopeOverflow
	true,  // quirkRingModulationNoMix
	true,  // quirkTVAZeroEnvLevels
	true,  // quirkPanMult
	true,  // quirkKeyShift
	true,  // quirkTVFBaseCutoffLimit
	false, // quirkFastPitchChanges
	true,  // quirkDisplayCustomMessagePriority
	true,  // oldMT32DisplayFeatures
	true,  // defaultReverbMT32Compatible
	true   // oldMT32AnalogLPF
};
static const ControlROMFeatureSet OLD_MT32_LATER = {
	true, // quirkBasePitchOverflow
	true, // quirkPitchEnvelopeOverflow
	true, // quirkRingModulationNoMix
	true, // quirkTVAZeroEnvLevels
	true, // quirkPanMult
	true, // quirkKeyShift
	true, // quirkTVFBaseCutoffLimit
	false, // quirkFastPitchChanges
	false, // quirkDisplayCustomMessagePriority
	true,  // oldMT32DisplayFeatures
	true, // defaultReverbMT32Compatible
	true // oldMT32AnalogLPF
};
static const ControlROMFeatureSet NEW_MT32_COMPATIBLE = {
	false, // quirkBasePitchOverflow
	false, // quirkPitchEnvelopeOverflow
	false, // quirkRingModulationNoMix
	false, // quirkTVAZeroEnvLevels
	false, // quirkPanMult
	false, // quirkKeyShift
	false, // quirkTVFBaseCutoffLimit
	false, // quirkFastPitchChanges
	false, // quirkDisplayCustomMessagePriority
	false, // oldMT32DisplayFeatures
	false, // defaultReverbMT32Compatible
	false  // oldMT32AnalogLPF
};
static const ControlROMFeatureSet CM32LN_COMPATIBLE = {
	false, // quirkBasePitchOverflow
	false, // quirkPitchEnvelopeOverflow
	false, // quirkRingModulationNoMix
	false, // quirkTVAZeroEnvLevels
	false, // quirkPanMult
	false, // quirkKeyShift
	false, // quirkTVFBaseCutoffLimit
	true,  // quirkFastPitchChanges
	false, // quirkDisplayCustomMessagePriority
	false, // oldMT32DisplayFeatures
	false, // defaultReverbMT32Compatible
	false // oldMT32AnalogLPF
};

static const ControlROMMap ControlROMMaps[] = {
	//     ID                Features        PCMmap  PCMc  tmbrA  tmbrAO, tmbrAC tmbrB   tmbrBO  tmbrBC tmbrR   trC rhythm rhyC  rsrv   panpot   prog   rhyMax  patMax  sysMax  timMax  sndGrp sGC  stMsg   sErMsg
	{"ctrl_mt32_1_04",    OLD_MT32_ELDER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x73A6, 85, 0x57C7, 0x57E2, 0x57D0, 0x5252, 0x525E, 0x526E, 0x520A, 0x7064, 19, 0x217A, 0x4BB6},
	{"ctrl_mt32_1_05",    OLD_MT32_ELDER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x7414, 85, 0x57C7, 0x57E2, 0x57D0, 0x5252, 0x525E, 0x526E, 0x520A, 0x70CA, 19, 0x217A, 0x4BB6},
	{"ctrl_mt32_1_06",    OLD_MT32_LATER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x7414, 85, 0x57D9, 0x57F4, 0x57E2, 0x5264, 0x5270, 0x5280, 0x521C, 0x70CA, 19, 0x217A, 0x4BBA},
	{"ctrl_mt32_1_07",    OLD_MT32_LATER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x73fe, 85, 0x57B1, 0x57CC, 0x57BA, 0x523C, 0x5248, 0x5258, 0x51F4, 0x70B0, 19, 0x217A, 0x4B92},
	{"ctrl_mt32_bluer",   OLD_MT32_LATER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x741C, 85, 0x57E5, 0x5800, 0x57EE, 0x5270, 0x527C, 0x528C, 0x5228, 0x70CE, 19, 0x217A, 0x4BC6},
	{"ctrl_mt32_2_03",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F49, 0x4F64, 0x4F52, 0x4885, 0x4889, 0x48A2, 0x48B9, 0x5A44, 19, 0x1EF0, 0x4066},
	{"ctrl_mt32_2_04",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F5D, 0x4F78, 0x4F66, 0x4899, 0x489D, 0x48B6, 0x48CD, 0x5A58, 19, 0x1EF0, 0x406D},
	{"ctrl_mt32_2_06",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F69, 0x4F84, 0x4F72, 0x48A5, 0x48A9, 0x48C2, 0x48D9, 0x5A64, 19, 0x1EF0, 0x4021},
	{"ctrl_mt32_2_07",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F81, 0x4F9C, 0x4F8A, 0x48B9, 0x48BD, 0x48D6, 0x48ED, 0x5A78, 19, 0x1EE7, 0x4035},
	{"ctrl_cm32l_1_00", NEW_MT32_COMPATIBLE, 0x8100, 256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F65, 0x4F80, 0x4F6E, 0x48A1, 0x48A5, 0x48BE, 0x48D5, 0x5A6C, 19, 0x1EF0, 0x401D},
	{"ctrl_cm32l_1_02", NEW_MT32_COMPATIBLE, 0x8100, 256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F93, 0x4FAE, 0x4F9C, 0x48CB, 0x48CF, 0x48E8, 0x48FF, 0x5A96, 19, 0x1EE7, 0x4047},
	{"ctrl_cm32ln_1_00", CM32LN_COMPATIBLE,  0x8100, 256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4EC7, 0x4EE2, 0x4ED0, 0x47FF, 0x4803, 0x481C, 0x4833, 0x55A2, 19, 0x1F59, 0x3F7C}
	// (Note that old MT-32 ROMs actually have 86 entries for rhythmTemp)
};

static const PartialState PARTIAL_PHASE_TO_STATE[8] = {
	PartialState_ATTACK, PartialState_ATTACK, PartialState_ATTACK, PartialState_ATTACK,
	PartialState_SUSTAIN, PartialState_SUSTAIN, PartialState_RELEASE, PartialState_INACTIVE
};

static inline PartialState getPartialState(PartialManager *partialManager, unsigned int partialNum) {
	const Partial *partial = partialManager->getPartial(partialNum);
	return partial->isActive() ? PARTIAL_PHASE_TO_STATE[partial->getTVA()->getPhase()] : PartialState_INACTIVE;
}

template <class I, class O>
static inline void convertSampleFormat(const I *inBuffer, O *outBuffer, const Bit32u len) {
	if (inBuffer == NULL || outBuffer == NULL) return;

	const I *inBufferEnd = inBuffer + len;
	while (inBuffer < inBufferEnd) {
		*(outBuffer++) = Synth::convertSample(*(inBuffer++));
	}
}

class Renderer {
protected:
	Synth &synth;

	void printDebug(const char *msg) const {
		synth.printDebug("%s", msg);
	}

	bool isActivated() const {
		return synth.activated;
	}

	bool isAbortingPoly() const {
		return synth.isAbortingPoly();
	}

	Analog &getAnalog() const {
		return *synth.analog;
	}

	MidiEventQueue &getMidiQueue() {
		return *synth.midiQueue;
	}

	PartialManager &getPartialManager() {
		return *synth.partialManager;
	}

	BReverbModel &getReverbModel() {
		return *synth.reverbModel;
	}

	Bit32u getRenderedSampleCount() {
		return synth.renderedSampleCount;
	}

	void incRenderedSampleCount(const Bit32u count) {
		synth.renderedSampleCount += count;
	}

	//DBP: Made unused function inline
	inline void updateDisplayState() {}

public:
	Renderer(Synth &useSynth) : synth(useSynth) {}

	virtual ~Renderer() {}

	virtual void render(IntSample *stereoStream, Bit32u len) = 0;
	virtual void render(FloatSample *stereoStream, Bit32u len) = 0;
	virtual void renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len) = 0;
	virtual void renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len) = 0;
};

template <class Sample>
class RendererImpl : public Renderer {
	// These buffers are used for building the output streams as they are found at the DAC entrance.
	// The output is mixed down to stereo interleaved further in the analog circuitry emulation.
	Sample tmpNonReverbLeft[MAX_SAMPLES_PER_RUN], tmpNonReverbRight[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbDryLeft[MAX_SAMPLES_PER_RUN], tmpReverbDryRight[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbWetLeft[MAX_SAMPLES_PER_RUN], tmpReverbWetRight[MAX_SAMPLES_PER_RUN];

	const DACOutputStreams<Sample> tmpBuffers;
	DACOutputStreams<Sample> createTmpBuffers() {
		DACOutputStreams<Sample> buffers = {
			tmpNonReverbLeft, tmpNonReverbRight,
			tmpReverbDryLeft, tmpReverbDryRight,
			tmpReverbWetLeft, tmpReverbWetRight
		};
		return buffers;
	}

public:
	RendererImpl(Synth &useSynth) :
		Renderer(useSynth),
		tmpBuffers(createTmpBuffers())
	{}

	void render(IntSample *stereoStream, Bit32u len);
	void render(FloatSample *stereoStream, Bit32u len);
	void renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len);
	void renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len);

	template <class O>
	void doRenderAndConvert(O *stereoStream, Bit32u len);
	void doRender(Sample *stereoStream, Bit32u len);

	template <class O>
	void doRenderAndConvertStreams(const DACOutputStreams<O> &streams, Bit32u len);
	void doRenderStreams(const DACOutputStreams<Sample> &streams, Bit32u len);
	void produceLA32Output(Sample *buffer, Bit32u len);
	void convertSamplesToOutput(Sample *buffer, Bit32u len);
	void produceStreams(const DACOutputStreams<Sample> &streams, Bit32u len);
};

class Extensions {
public:
	RendererType selectedRendererType;
	Bit32s masterTunePitchDelta;
	bool niceAmpRamp;
	bool nicePanning;
	bool nicePartialMixing;

	// Here we keep the reverse mapping of assigned parts per MIDI channel.
	// NOTE: value above 8 means that the channel is not assigned
	Bit8u chantable[16][9];

	// This stores the index of Part in chantable that failed to play and required partial abortion.
	Bit32u abortingPartIx;

	bool preallocatedReverbMemory;

	Bit32u midiEventQueueSize;
	Bit32u midiEventQueueSysexStorageBufferSize;

	////DBP: Removed display and reporthandler
	//Display *display;
	//bool oldMT32DisplayFeatures;
	//
	//ReportHandler2 defaultReportHandler;
	//ReportHandler2 *reportHandler2;
};

Bit32u Synth::getLibraryVersionInt() {
	return MT32EMU_CURRENT_VERSION_INT;
}

const char *Synth::getLibraryVersionString() {
	return MT32EMU_VERSION;
}

Bit8u Synth::calcSysexChecksum(const Bit8u *data, const Bit32u len, const Bit8u initChecksum) {
	unsigned int checksum = -initChecksum;
	for (unsigned int i = 0; i < len; i++) {
		checksum -= data[i];
	}
	return Bit8u(checksum & 0x7f);
}

Bit32u Synth::getStereoOutputSampleRate(AnalogOutputMode analogOutputMode) {
	static const unsigned int SAMPLE_RATES[] = {SAMPLE_RATE, SAMPLE_RATE, SAMPLE_RATE * 3 / 2, SAMPLE_RATE * 3};

	return SAMPLE_RATES[analogOutputMode];
}

Synth::Synth() :
	mt32ram(*new MemParams),
	mt32default(*new MemParams),
	extensions(*new Extensions)
{
	opened = false;
	reverbOverridden = false;
	partialCount = DEFAULT_MAX_PARTIALS;
	controlROMMap = NULL;
	controlROMFeatures = NULL;

	extensions.preallocatedReverbMemory = false;
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		reverbModels[i] = NULL;
	}
	reverbModel = NULL;
	analog = NULL;
	renderer = NULL;
	setDACInputMode(DACInputMode_NICE);
	setMIDIDelayMode(MIDIDelayMode_DELAY_SHORT_MESSAGES_ONLY);
	setOutputGain(1.0f);
	setReverbOutputGain(1.0f);
	setReversedStereoEnabled(false);
	setNiceAmpRampEnabled(true);
	setNicePanningEnabled(false);
	setNicePartialMixingEnabled(false);
	selectRendererType(RendererType_BIT16S);

	patchTempMemoryRegion = NULL;
	rhythmTempMemoryRegion = NULL;
	timbreTempMemoryRegion = NULL;
	patchesMemoryRegion = NULL;
	timbresMemoryRegion = NULL;
	systemMemoryRegion = NULL;
	displayMemoryRegion = NULL;
	resetMemoryRegion = NULL;
	paddedTimbreMaxTable = NULL;

	partialManager = NULL;
	pcmWaves = NULL;
	pcmROMData = NULL;
	soundGroupNames = NULL;
	midiQueue = NULL;
	extensions.midiEventQueueSize = DEFAULT_MIDI_EVENT_QUEUE_SIZE;
	extensions.midiEventQueueSysexStorageBufferSize = 0;
	lastReceivedMIDIEventTimestamp = 0;
	memset(parts, 0, sizeof(parts));
	renderedSampleCount = 0;
}

Synth::~Synth() {
	close(); // Make sure we're closed and everything is freed
	delete &mt32ram;
	delete &mt32default;
	delete &extensions;
}

void Synth::newTimbreSet(Bit8u partNum) const {
	const Part *part = getPart(partNum);
}

const char *Synth::getSoundGroupName(const Part *part) const {
	const PatchParam &patch = part->getPatchTemp()->patch;
	return getSoundGroupName(patch.timbreGroup, patch.timbreNum);
}

const char *Synth::getSoundGroupName(Bit8u timbreGroup, Bit8u timbreNumber) const {
	switch (timbreGroup) {
	case 1:
		timbreNumber += 64;
		// Fall-through
	case 0:
		return soundGroupNames[soundGroupIx[timbreNumber]];
	case 2:
		return soundGroupNames[controlROMMap->soundGroupsCount - 2];
	case 3:
		return soundGroupNames[controlROMMap->soundGroupsCount - 1];
	default:
		return NULL;
	}
}

void Synth::setReverbEnabled(bool newReverbEnabled) {
	if (!opened) return;
	if (isReverbEnabled() == newReverbEnabled) return;
	if (newReverbEnabled) {
		bool oldReverbOverridden = reverbOverridden;
		reverbOverridden = false;
		refreshSystemReverbParameters();
		reverbOverridden = oldReverbOverridden;
	} else {
		if (!extensions.preallocatedReverbMemory) {
			reverbModel->close();
		}
		reverbModel = NULL;
	}
}

bool Synth::isReverbEnabled() const {
	return reverbModel != NULL;
}

void Synth::setReverbOverridden(bool newReverbOverridden) {
	reverbOverridden = newReverbOverridden;
}

bool Synth::isReverbOverridden() const {
	return reverbOverridden;
}

void Synth::setReverbCompatibilityMode(bool mt32CompatibleMode) {
	if (!opened || (isMT32ReverbCompatibilityMode() == mt32CompatibleMode)) return;
	bool oldReverbEnabled = isReverbEnabled();
	setReverbEnabled(false);
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		delete reverbModels[i];
	}
	initReverbModels(mt32CompatibleMode);
	setReverbEnabled(oldReverbEnabled);
	setReverbOutputGain(reverbOutputGain);
}

bool Synth::isMT32ReverbCompatibilityMode() const {
	return opened && (reverbModels[REVERB_MODE_ROOM]->isMT32Compatible(REVERB_MODE_ROOM));
}

bool Synth::isDefaultReverbMT32Compatible() const {
	return opened && controlROMFeatures->defaultReverbMT32Compatible;
}

void Synth::preallocateReverbMemory(bool enabled) {
	if (extensions.preallocatedReverbMemory == enabled) return;
	extensions.preallocatedReverbMemory = enabled;
	if (!opened) return;
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		if (enabled) {
			reverbModels[i]->open();
		} else if (reverbModel != reverbModels[i]) {
			reverbModels[i]->close();
		}
	}
}

void Synth::setDACInputMode(DACInputMode mode) {
	dacInputMode = mode;
}

DACInputMode Synth::getDACInputMode() const {
	return dacInputMode;
}

void Synth::setMIDIDelayMode(MIDIDelayMode mode) {
	midiDelayMode = mode;
}

MIDIDelayMode Synth::getMIDIDelayMode() const {
	return midiDelayMode;
}

void Synth::setOutputGain(float newOutputGain) {
	if (newOutputGain < 0.0f) newOutputGain = -newOutputGain;
	outputGain = newOutputGain;
	if (analog != NULL) analog->setSynthOutputGain(newOutputGain);
}

float Synth::getOutputGain() const {
	return outputGain;
}

void Synth::setReverbOutputGain(float newReverbOutputGain) {
	if (newReverbOutputGain < 0.0f) newReverbOutputGain = -newReverbOutputGain;
	reverbOutputGain = newReverbOutputGain;
	if (analog != NULL) analog->setReverbOutputGain(newReverbOutputGain, isMT32ReverbCompatibilityMode());
}

float Synth::getReverbOutputGain() const {
	return reverbOutputGain;
}

void Synth::setReversedStereoEnabled(bool enabled) {
	reversedStereoEnabled = enabled;
}

bool Synth::isReversedStereoEnabled() const {
	return reversedStereoEnabled;
}

void Synth::setNiceAmpRampEnabled(bool enabled) {
	extensions.niceAmpRamp = enabled;
}

bool Synth::isNiceAmpRampEnabled() const {
	return extensions.niceAmpRamp;
}

void Synth::setNicePanningEnabled(bool enabled) {
	extensions.nicePanning = enabled;
}

bool Synth::isNicePanningEnabled() const {
	return extensions.nicePanning;
}

void Synth::setNicePartialMixingEnabled(bool enabled) {
	extensions.nicePartialMixing = enabled;
}

bool Synth::isNicePartialMixingEnabled() const {
	return extensions.nicePartialMixing;
}

bool Synth::loadControlROM(const ROMImage &controlROMImage) {
	File *file = controlROMImage.getFile();
	const ROMInfo *controlROMInfo = controlROMImage.getROMInfo();
	if ((controlROMInfo == NULL)
			|| (controlROMInfo->type != ROMInfo::Control)
			|| (controlROMInfo->pairType != ROMInfo::Full)) {
#if MT32EMU_MONITOR_INIT
		printDebug("Invalid Control ROM Info provided");
#endif
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Found Control ROM: %s, %s", controlROMInfo->shortName, controlROMInfo->description);
#endif
	const Bit8u *fileData = file->getData();
	memcpy(controlROMData, fileData, CONTROL_ROM_SIZE);

	// Control ROM successfully loaded, now check whether it's a known type
	controlROMMap = NULL;
	controlROMFeatures = NULL;
	for (unsigned int i = 0; i < sizeof(ControlROMMaps) / sizeof(ControlROMMaps[0]); i++) {
		if (strcmp(controlROMInfo->shortName, ControlROMMaps[i].shortName) == 0) {
			controlROMMap = &ControlROMMaps[i];
			controlROMFeatures = &controlROMMap->featureSet;
			return true;
		}
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Control ROM failed to load");
#endif
	return false;
}

bool Synth::loadPCMROM(const ROMImage &pcmROMImage) {
	File *file = pcmROMImage.getFile();
	const ROMInfo *pcmROMInfo = pcmROMImage.getROMInfo();
	if ((pcmROMInfo == NULL)
			|| (pcmROMInfo->type != ROMInfo::PCM)
			|| (pcmROMInfo->pairType != ROMInfo::Full)) {
		return false;
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Found PCM ROM: %s, %s", pcmROMInfo->shortName, pcmROMInfo->description);
#endif
	size_t fileSize = file->getSize();
	if (fileSize != (2 * pcmROMSize)) {
#if MT32EMU_MONITOR_INIT
		printDebug("PCM ROM file has wrong size (expected %d, got %d)", 2 * pcmROMSize, fileSize);
#endif
		return false;
	}
	const Bit8u *fileData = file->getData();
	for (size_t i = 0; i < pcmROMSize; i++) {
		Bit8u s = *(fileData++);
		Bit8u c = *(fileData++);

		int order[16] = {0, 9, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 8};

		Bit16s log = 0;
		for (int u = 0; u < 16; u++) {
			int bit;
			if (order[u] < 8) {
				bit = (s >> (7 - order[u])) & 0x1;
			} else {
				bit = (c >> (7 - (order[u] - 8))) & 0x1;
			}
			log = log | Bit16s(bit << (15 - u));
		}
		pcmROMData[i] = log;
	}
	return true;
}

bool Synth::initPCMList(Bit16u mapAddress, Bit16u count) {
	ControlROMPCMStruct *tps = reinterpret_cast<ControlROMPCMStruct *>(&controlROMData[mapAddress]);
	for (int i = 0; i < count; i++) {
		Bit32u rAddr = tps[i].pos * 0x800;
		Bit32u rLenExp = (tps[i].len & 0x70) >> 4;
		Bit32u rLen = 0x800 << rLenExp;
		if (rAddr + rLen > pcmROMSize) {
			printDebug("Control ROM error: Wave map entry %d points to invalid PCM address 0x%04X, length 0x%04X", i, rAddr, rLen);
			return false;
		}
		pcmWaves[i].addr = rAddr;
		pcmWaves[i].len = rLen;
		pcmWaves[i].loop = (tps[i].len & 0x80) != 0;
		pcmWaves[i].controlROMPCMStruct = &tps[i];
		//int pitch = (tps[i].pitchMSB << 8) | tps[i].pitchLSB;
		//bool unaffectedByMasterTune = (tps[i].len & 0x01) == 0;
		//printDebug("PCM %d: pos=%d, len=%d, pitch=%d, loop=%s, unaffectedByMasterTune=%s", i, rAddr, rLen, pitch, pcmWaves[i].loop ? "YES" : "NO", unaffectedByMasterTune ? "YES" : "NO");
	}
	return false;
}

bool Synth::initCompressedTimbre(Bit16u timbreNum, const Bit8u *src, Bit32u srcLen) {
	// "Compressed" here means that muted partials aren't present in ROM (except in the case of partial 0 being muted).
	// Instead the data from the previous unmuted partial is used.
	if (srcLen < sizeof(TimbreParam::CommonParam)) {
		return false;
	}
	TimbreParam *timbre = &mt32ram.timbres[timbreNum].timbre;
	timbresMemoryRegion->write(timbreNum, 0, src, sizeof(TimbreParam::CommonParam), true);
	unsigned int srcPos = sizeof(TimbreParam::CommonParam);
	unsigned int memPos = sizeof(TimbreParam::CommonParam);
	for (int t = 0; t < 4; t++) {
		if (t != 0 && ((timbre->common.partialMute >> t) & 0x1) == 0x00) {
			// This partial is muted - we'll copy the previously copied partial, then
			srcPos -= sizeof(TimbreParam::PartialParam);
		} else if (srcPos + sizeof(TimbreParam::PartialParam) >= srcLen) {
			return false;
		}
		timbresMemoryRegion->write(timbreNum, memPos, src + srcPos, sizeof(TimbreParam::PartialParam));
		srcPos += sizeof(TimbreParam::PartialParam);
		memPos += sizeof(TimbreParam::PartialParam);
	}
	return true;
}

bool Synth::initTimbres(Bit16u mapAddress, Bit16u offset, Bit16u count, Bit16u startTimbre, bool compressed) {
	const Bit8u *timbreMap = &controlROMData[mapAddress];
	for (Bit16u i = 0; i < count * 2; i += 2) {
		Bit16u address = (timbreMap[i + 1] << 8) | timbreMap[i];
		if (!compressed && (address + offset + sizeof(TimbreParam) > CONTROL_ROM_SIZE)) {
			printDebug("Control ROM error: Timbre map entry 0x%04x for timbre %d points to invalid timbre address 0x%04x", i, startTimbre, address);
			return false;
		}
		address += offset;
		if (compressed) {
			if (!initCompressedTimbre(startTimbre, &controlROMData[address], CONTROL_ROM_SIZE - address)) {
				printDebug("Control ROM error: Timbre map entry 0x%04x for timbre %d points to invalid timbre at 0x%04x", i, startTimbre, address);
				return false;
			}
		} else {
			timbresMemoryRegion->write(startTimbre, 0, &controlROMData[address], sizeof(TimbreParam), true);
		}
		startTimbre++;
	}
	return true;
}

void Synth::initReverbModels(bool mt32CompatibleMode) {
	for (int mode = REVERB_MODE_ROOM; mode <= REVERB_MODE_TAP_DELAY; mode++) {
		reverbModels[mode] = BReverbModel::createBReverbModel(ReverbMode(mode), mt32CompatibleMode, getSelectedRendererType());

		if (extensions.preallocatedReverbMemory) {
			reverbModels[mode]->open();
		}
	}
}

void Synth::initSoundGroups(char newSoundGroupNames[][9]) {
	memcpy(soundGroupIx, &controlROMData[controlROMMap->soundGroupsTable - sizeof(soundGroupIx)], sizeof(soundGroupIx));
	const SoundGroup *table = reinterpret_cast<SoundGroup *>(&controlROMData[controlROMMap->soundGroupsTable]);
	for (unsigned int i = 0; i < controlROMMap->soundGroupsCount; i++) {
		memcpy(&newSoundGroupNames[i][0], table[i].name, sizeof(table[i].name));
	}
}

bool Synth::open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, AnalogOutputMode analogOutputMode) {
	return open(controlROMImage, pcmROMImage, DEFAULT_MAX_PARTIALS, analogOutputMode);
}

bool Synth::open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, Bit32u usePartialCount, AnalogOutputMode analogOutputMode) {
	if (opened) {
		return false;
	}
	partialCount = usePartialCount;
	abortingPoly = NULL;
	extensions.abortingPartIx = 0;

	// This is to help detect bugs
	memset(&mt32ram, '?', sizeof(mt32ram));

#if MT32EMU_MONITOR_INIT
	printDebug("Loading Control ROM");
#endif
	if (!loadControlROM(controlROMImage)) {
		printDebug("Init Error - Missing or invalid Control ROM image");
		dispose();
		return false;
	}

	initMemoryRegions();

	// 512KB PCM ROM for MT-32, etc.
	// 1MB PCM ROM for CM-32L, LAPC-I, CM-64, CM-500
	// Note that the size below is given in samples (16-bit), not bytes
	pcmROMSize = controlROMMap->pcmCount == 256 ? 512 * 1024 : 256 * 1024;
	pcmROMData = new Bit16s[pcmROMSize];

#if MT32EMU_MONITOR_INIT
	printDebug("Loading PCM ROM");
#endif
	if (!loadPCMROM(pcmROMImage)) {
		printDebug("Init Error - Missing PCM ROM image");
		dispose();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Reverb Models");
#endif
	bool mt32CompatibleReverb = controlROMFeatures->defaultReverbMT32Compatible;
#if MT32EMU_MONITOR_INIT
	printDebug("Using %s Compatible Reverb Models", mt32CompatibleReverb ? "MT-32" : "CM-32L");
#endif
	initReverbModels(mt32CompatibleReverb);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank A");
#endif
	if (!initTimbres(controlROMMap->timbreAMap, controlROMMap->timbreAOffset, 0x40, 0, controlROMMap->timbreACompressed)) {
		dispose();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank B");
#endif
	if (!initTimbres(controlROMMap->timbreBMap, controlROMMap->timbreBOffset, 0x40, 64, controlROMMap->timbreBCompressed)) {
		dispose();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank R");
#endif
	if (!initTimbres(controlROMMap->timbreRMap, 0, controlROMMap->timbreRCount, 192, true)) {
		dispose();
		return false;
	}

	if (controlROMMap->timbreRCount == 30) {
		// We must initialise all 64 rhythm timbres to avoid undefined behaviour.
		// SEMI-CONFIRMED: Old-gen MT-32 units likely map timbres 30..59 to 0..29.
		// Attempts to play rhythm timbres 60..63 exhibit undefined behaviour.
		// We want to emulate the wrap around, so merely copy the entire set of standard
		// timbres once more. The last 4 dangerous timbres are zeroed out.
		memcpy(&mt32ram.timbres[222], &mt32ram.timbres[192], sizeof(*mt32ram.timbres) * 30);
		memset(&mt32ram.timbres[252], 0, sizeof(*mt32ram.timbres) * 4);
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank M");
#endif
	// CM-64 seems to initialise all bytes in this bank to 0.
	memset(&mt32ram.timbres[128], 0, sizeof(mt32ram.timbres[128]) * 64);

	partialManager = new PartialManager(this, parts);

	pcmWaves = new PCMWaveEntry[controlROMMap->pcmCount];

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising PCM List");
#endif
	initPCMList(controlROMMap->pcmTable, controlROMMap->pcmCount);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Rhythm Temp");
#endif
	memcpy(mt32ram.rhythmTemp, &controlROMData[controlROMMap->rhythmSettings], controlROMMap->rhythmSettingsCount * 4);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Patches");
#endif
	for (Bit8u i = 0; i < 128; i++) {
		PatchParam *patch = &mt32ram.patches[i];
		patch->timbreGroup = i / 64;
		patch->timbreNum = i % 64;
		patch->keyShift = 24;
		patch->fineTune = 50;
		patch->benderRange = 12;
		patch->assignMode = 0;
		patch->reverbSwitch = 1;
		patch->dummy = 0;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising System");
#endif
	// The MT-32 manual claims that "Standard pitch" is 442Hz.
	mt32ram.system.masterTune = 0x4A; // Confirmed on CM-64
	mt32ram.system.reverbMode = 0; // Confirmed
	mt32ram.system.reverbTime = 5; // Confirmed
	mt32ram.system.reverbLevel = 3; // Confirmed
	memcpy(mt32ram.system.reserveSettings, &controlROMData[controlROMMap->reserveSettings], 9); // Confirmed
	for (Bit8u i = 0; i < 9; i++) {
		// This is the default: {1, 2, 3, 4, 5, 6, 7, 8, 9}
		// An alternative configuration can be selected by holding "Master Volume"
		// and pressing "PART button 1" on the real MT-32's frontpanel.
		// The channel assignment is then {0, 1, 2, 3, 4, 5, 6, 7, 9}
		mt32ram.system.chanAssign[i] = i + 1;
	}
	mt32ram.system.masterVol = 100; // Confirmed

	bool oldReverbOverridden = reverbOverridden;
	reverbOverridden = false;
	refreshSystem();
	resetMasterTunePitchDelta();
	reverbOverridden = oldReverbOverridden;

	char(*writableSoundGroupNames)[9] = new char[controlROMMap->soundGroupsCount][9];
	soundGroupNames = writableSoundGroupNames;
	initSoundGroups(writableSoundGroupNames);

	for (int i = 0; i < 9; i++) {
		MemParams::PatchTemp *patchTemp = &mt32ram.patchTemp[i];

		// Note that except for the rhythm part, these patch fields will be set in setProgram() below anyway.
		patchTemp->patch.timbreGroup = 0;
		patchTemp->patch.timbreNum = 0;
		patchTemp->patch.keyShift = 24;
		patchTemp->patch.fineTune = 50;
		patchTemp->patch.benderRange = 12;
		patchTemp->patch.assignMode = 0;
		patchTemp->patch.reverbSwitch = 1;
		patchTemp->patch.dummy = 0;

		patchTemp->outputLevel = 80;
		patchTemp->panpot = controlROMData[controlROMMap->panSettings + i];
		memset(patchTemp->dummyv, 0, sizeof(patchTemp->dummyv));
		patchTemp->dummyv[1] = 127;

		if (i < 8) {
			parts[i] = new Part(this, i);
			parts[i]->setProgram(controlROMData[controlROMMap->programSettings + i]);
		} else {
			parts[i] = new RhythmPart(this, i);
		}
	}

	// For resetting mt32 mid-execution
	mt32default = mt32ram;

	midiQueue = new MidiEventQueue(extensions.midiEventQueueSize, extensions.midiEventQueueSysexStorageBufferSize);

	analog = Analog::createAnalog(analogOutputMode, controlROMFeatures->oldMT32AnalogLPF, getSelectedRendererType());
#if MT32EMU_MONITOR_INIT
	static const char *ANALOG_OUTPUT_MODES[] = { "Digital only", "Coarse", "Accurate", "Oversampled2x" };
	printDebug("Using Analog output mode %s", ANALOG_OUTPUT_MODES[analogOutputMode]);
#endif
	setOutputGain(outputGain);
	setReverbOutputGain(reverbOutputGain);

	switch (getSelectedRendererType()) {
		case RendererType_BIT16S:
			renderer = new RendererImpl<IntSample>(*this);
#if MT32EMU_MONITOR_INIT
			printDebug("Using integer 16-bit samples in renderer and wave generator");
#endif
			break;
		case RendererType_FLOAT:
			renderer = new RendererImpl<FloatSample>(*this);
#if MT32EMU_MONITOR_INIT
			printDebug("Using float 32-bit samples in renderer and wave generator");
#endif
			break;
		default:
			printDebug("Synth: Unknown renderer type %i\n", getSelectedRendererType());
			dispose();
			return false;
	}

	opened = true;
	activated = false;

#if MT32EMU_MONITOR_INIT
	printDebug("*** Initialisation complete ***");
#endif
	return true;
}

void Synth::dispose() {
	opened = false;

	delete midiQueue;
	midiQueue = NULL;

	delete renderer;
	renderer = NULL;

	delete analog;
	analog = NULL;

	delete partialManager;
	partialManager = NULL;

	for (int i = 0; i < 9; i++) {
		delete parts[i];
		parts[i] = NULL;
	}

	delete[] soundGroupNames;
	soundGroupNames = NULL;

	delete[] pcmWaves;
	pcmWaves = NULL;

	delete[] pcmROMData;
	pcmROMData = NULL;

	deleteMemoryRegions();

	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		delete reverbModels[i];
		reverbModels[i] = NULL;
	}
	reverbModel = NULL;
	controlROMFeatures = NULL;
	controlROMMap = NULL;
}

void Synth::close() {
	if (opened) {
		dispose();
	}
}

bool Synth::isOpen() const {
	return opened;
}

void Synth::flushMIDIQueue() {
	if (midiQueue == NULL) return;
	for (;;) {
		const volatile MidiEventQueue::MidiEvent *midiEvent = midiQueue->peekMidiEvent();
		if (midiEvent == NULL) break;
		if (midiEvent->sysexData == NULL) {
			playMsgNow(midiEvent->shortMessageData);
		} else {
			playSysexNow(midiEvent->sysexData, midiEvent->sysexLength);
		}
		midiQueue->dropMidiEvent();
	}
	lastReceivedMIDIEventTimestamp = renderedSampleCount;
}

Bit32u Synth::setMIDIEventQueueSize(Bit32u useSize) {
	static const Bit32u MAX_QUEUE_SIZE = (1 << 24); // This results in about 256 Mb - much greater than any reasonable value

	if (extensions.midiEventQueueSize == useSize) return useSize;

	// Find a power of 2 that is >= useSize
	Bit32u binarySize = 1;
	if (useSize < MAX_QUEUE_SIZE) {
		// Using simple linear search as this isn't time critical
		while (binarySize < useSize) binarySize <<= 1;
	} else {
		binarySize = MAX_QUEUE_SIZE;
	}
	extensions.midiEventQueueSize = binarySize;
	if (midiQueue != NULL) {
		flushMIDIQueue();
		delete midiQueue;
		midiQueue = new MidiEventQueue(binarySize, extensions.midiEventQueueSysexStorageBufferSize);
	}
	return binarySize;
}

void Synth::configureMIDIEventQueueSysexStorage(Bit32u storageBufferSize) {
	if (extensions.midiEventQueueSysexStorageBufferSize == storageBufferSize) return;

	extensions.midiEventQueueSysexStorageBufferSize = storageBufferSize;
	if (midiQueue != NULL) {
		flushMIDIQueue();
		delete midiQueue;
		midiQueue = new MidiEventQueue(extensions.midiEventQueueSize, storageBufferSize);
	}
}

Bit32u Synth::getShortMessageLength(Bit32u msg) {
	if ((msg & 0xF0) == 0xF0) {
		switch (msg & 0xFF) {
			case 0xF1:
			case 0xF3:
				return 2;
			case 0xF2:
				return 3;
			default:
				return 1;
		}
	}
	// NOTE: This calculation isn't quite correct
	// as it doesn't consider the running status byte
	return ((msg & 0xE0) == 0xC0) ? 2 : 3;
}

Bit32u Synth::addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp) {
	Bit32u transferTime =  Bit32u(double(len) * MIDI_DATA_TRANSFER_RATE);
	// Dealing with wrapping
	if (Bit32s(timestamp - lastReceivedMIDIEventTimestamp) < 0) {
		timestamp = lastReceivedMIDIEventTimestamp;
	}
	timestamp += transferTime;
	lastReceivedMIDIEventTimestamp = timestamp;
	return timestamp;
}

Bit32u Synth::getInternalRenderedSampleCount() const {
	return renderedSampleCount;
}

bool Synth::playMsg(Bit32u msg) {
	return playMsg(msg, renderedSampleCount);
}

bool Synth::playMsg(Bit32u msg, Bit32u timestamp) {
	if ((msg & 0xF8) == 0xF8) {
		return true;
	}
	if (midiQueue == NULL) return false;
	if (midiDelayMode != MIDIDelayMode_IMMEDIATE) {
		timestamp = addMIDIInterfaceDelay(getShortMessageLength(msg), timestamp);
	}
	if (!activated) activated = true;
	do {
		if (midiQueue->pushShortMessage(msg, timestamp)) return true;
	} while (false);
	return false;
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len) {
	return playSysex(sysex, len, renderedSampleCount);
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp) {
	if (midiQueue == NULL) return false;
	if (midiDelayMode == MIDIDelayMode_DELAY_ALL) {
		timestamp = addMIDIInterfaceDelay(len, timestamp);
	}
	if (!activated) activated = true;
	do {
		if (midiQueue->pushSysex(sysex, len, timestamp)) return true;
	} while (false);
	return false;
}

void Synth::playMsgNow(Bit32u msg) {
	if (!opened) return;

	// NOTE: Active sense IS implemented in real hardware. However, realtime processing is clearly out of the library scope.
	//       It is assumed that realtime consumers of the library respond to these MIDI events as appropriate.

	Bit8u code = Bit8u((msg & 0x0000F0) >> 4);
	Bit8u chan = Bit8u(msg & 0x00000F);
	Bit8u note = Bit8u((msg & 0x007F00) >> 8);
	Bit8u velocity = Bit8u((msg & 0x7F0000) >> 16);

	//printDebug("Playing chan %d, code 0x%01x note: 0x%02x", chan, code, note);

	Bit8u *chanParts = extensions.chantable[chan];
	if (*chanParts > 8) {
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Play msg on unreg chan %d (%d): code=0x%01x, vel=%d", chan, *chanParts, code, velocity);
#endif
		return;
	}
	for (Bit32u i = extensions.abortingPartIx; i <= 8; i++) {
		const Bit32u partNum = chanParts[i];
		if (partNum > 8) break;
		playMsgOnPart(partNum, code, note, velocity);
		if (isAbortingPoly()) {
			extensions.abortingPartIx = i;
			break;
		} else if (extensions.abortingPartIx) {
			extensions.abortingPartIx = 0;
		}
	}
}

void Synth::playMsgOnPart(Bit8u part, Bit8u code, Bit8u note, Bit8u velocity) {
	if (!opened) return;

	Bit32u bend;

	if (!activated) activated = true;
	//printDebug("Synth::playMsgOnPart(%02x, %02x, %02x, %02x)", part, code, note, velocity);
	switch (code) {
	case 0x8:
		//printDebug("Note OFF - Part %d", part);
		// The MT-32 ignores velocity for note off
		parts[part]->noteOff(note);
		break;
	case 0x9:
		//printDebug("Note ON - Part %d, Note %d Vel %d", part, note, velocity);
		if (velocity == 0) {
			// MIDI defines note-on with velocity 0 as being the same as note-off with velocity 40
			parts[part]->noteOff(note);
		} else if (parts[part]->getVolumeOverride() > 0) {
			parts[part]->noteOn(note, velocity);
		}
		break;
	case 0xB: // Control change
		switch (note) {
		case 0x01:  // Modulation
			//printDebug("Modulation: %d", velocity);
			parts[part]->setModulation(velocity);
			break;
		case 0x06:
			parts[part]->setDataEntryMSB(velocity);
			break;
		case 0x07:  // Set volume
			//printDebug("Volume set: %d", velocity);
			parts[part]->setVolume(velocity);
			break;
		case 0x0A:  // Pan
			//printDebug("Pan set: %d", velocity);
			parts[part]->setPan(velocity);
			break;
		case 0x0B:
			//printDebug("Expression set: %d", velocity);
			parts[part]->setExpression(velocity);
			break;
		case 0x40: // Hold (sustain) pedal
			//printDebug("Hold pedal set: %d", velocity);
			parts[part]->setHoldPedal(velocity >= 64);
			break;

		case 0x62:
		case 0x63:
			parts[part]->setNRPN();
			break;
		case 0x64:
			parts[part]->setRPNLSB(velocity);
			break;
		case 0x65:
			parts[part]->setRPNMSB(velocity);
			break;

		case 0x79: // Reset all controllers
			//printDebug("Reset all controllers");
			parts[part]->resetAllControllers();
			break;

		case 0x7B: // All notes off
			//printDebug("All notes off");
			parts[part]->allNotesOff();
			break;

		case 0x7C:
		case 0x7D:
		case 0x7E:
		case 0x7F:
			// CONFIRMED:Mok: A real LAPC-I responds to these controllers as follows:
			parts[part]->setHoldPedal(false);
			parts[part]->allNotesOff();
			break;

		default:
#if MT32EMU_MONITOR_MIDI > 0
			printDebug("Unknown MIDI Control code: 0x%02x - vel 0x%02x", note, velocity);
#endif
			return;
		}
		break;
	case 0xC: // Program change
		//printDebug("Program change %01x", note);
		parts[part]->setProgram(note);
		if (part < 8) {
		}
		break;
	case 0xE: // Pitch bender
		bend = (velocity << 7) | (note);
		//printDebug("Pitch bender %02x", bend);
		parts[part]->setBend(bend);
		break;
	default:
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Unknown Midi code: 0x%01x - %02x - %02x", code, note, velocity);
#endif
		return;
	}
}

void Synth::playSysexNow(const Bit8u *sysex, Bit32u len) {
	if (len < 2) {
		printDebug("playSysex: Message is too short for sysex (%d bytes)", len);
	}
	if (sysex[0] != 0xF0) {
		printDebug("playSysex: Message lacks start-of-sysex (0xF0)");
		return;
	}
	// Due to some programs (e.g. Java) sending buffers with junk at the end, we have to go through and find the end marker rather than relying on len.
	Bit32u endPos;
	for (endPos = 1; endPos < len; endPos++) {
		if (sysex[endPos] == 0xF7) {
			break;
		}
	}
	if (endPos == len) {
		printDebug("playSysex: Message lacks end-of-sysex (0xf7)");
		return;
	}
	playSysexWithoutFraming(sysex + 1, endPos - 1);
}

void Synth::playSysexWithoutFraming(const Bit8u *sysex, Bit32u len) {
	if (len < 4) {
		printDebug("playSysexWithoutFraming: Message is too short (%d bytes)!", len);
		return;
	}
	if (sysex[0] != SYSEX_MANUFACTURER_ROLAND) {
		printDebug("playSysexWithoutFraming: Header not intended for this device manufacturer: %02x %02x %02x %02x", int(sysex[0]), int(sysex[1]), int(sysex[2]), int(sysex[3]));
		return;
	}
	if (sysex[2] == SYSEX_MDL_D50) {
		printDebug("playSysexWithoutFraming: Header is intended for model D-50 (not yet supported): %02x %02x %02x %02x", int(sysex[0]), int(sysex[1]), int(sysex[2]), int(sysex[3]));
		return;
	} else if (sysex[2] != SYSEX_MDL_MT32) {
		printDebug("playSysexWithoutFraming: Header not intended for model MT-32: %02x %02x %02x %02x", int(sysex[0]), int(sysex[1]), int(sysex[2]), int(sysex[3]));
		return;
	}
	playSysexWithoutHeader(sysex[1], sysex[3], sysex + 4, len - 4);
}

void Synth::playSysexWithoutHeader(Bit8u device, Bit8u command, const Bit8u *sysex, Bit32u len) {
	if (device > 0x10) {
		// We have device ID 0x10 (default, but changeable, on real MT-32), < 0x10 is for channels
		printDebug("playSysexWithoutHeader: Message is not intended for this device ID (provided: %02x, expected: 0x10 or channel)", int(device));
		return;
	}

	// All models process the checksum before anything else and ignore messages lacking the checksum, or containing the checksum only.
	if (len < 2) {
		printDebug("playSysexWithoutHeader: Message is too short (%d bytes)!", len);
		return;
	}
	Bit8u checksum = calcSysexChecksum(sysex, len - 1);
	if (checksum != sysex[len - 1]) {
		printDebug("playSysexWithoutHeader: Message checksum is incorrect (provided: %02x, expected: %02x)!", sysex[len - 1], checksum);
		return;
	}
	len -= 1; // Exclude checksum

	if (command == SYSEX_CMD_EOD) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("playSysexWithoutHeader: Ignored unsupported command %02x", command);
#endif
		return;
	}
	switch (command) {
	case SYSEX_CMD_WSD:
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("playSysexWithoutHeader: Ignored unsupported command %02x", command);
#endif
		break;
	case SYSEX_CMD_DAT:
		/* Outcommented until we (ever) actually implement handshake communication
		if (hasActivePartials()) {
			printDebug("playSysexWithoutHeader: Got SYSEX_CMD_DAT but partials are active - ignoring");
			// FIXME: We should send SYSEX_CMD_RJC in this case
			break;
		}
		*/
		// Fall-through
	case SYSEX_CMD_DT1:
		writeSysex(device, sysex, len);
		break;
	case SYSEX_CMD_RQD:
		if (hasActivePartials()) {
			printDebug("playSysexWithoutHeader: Got SYSEX_CMD_RQD but partials are active - ignoring");
			// FIXME: We should send SYSEX_CMD_RJC in this case
			break;
		}
		// Fall-through
	case SYSEX_CMD_RQ1:
		readSysex(device, sysex, len);
		break;
	default:
		printDebug("playSysexWithoutHeader: Unsupported command %02x", command);
		return;
	}
}

void Synth::readSysex(Bit8u /*device*/, const Bit8u * /*sysex*/, Bit32u /*len*/) const {
	// NYI
}

void Synth::writeSysex(Bit8u device, const Bit8u *sysex, Bit32u len) {
	if (!opened || len < 1) return;

	// This is checked early in the real devices (before any sysex length checks or further processing)
	if (sysex[0] == 0x7F) {
		reset();
		return;
	}

	if (len < 3) {
		// A short message of just 1 or 2 bytes may be written to the display area yet it may cause a user-visible effect,
		// similarly to the reset area.
		if (sysex[0] == 0x20) {
			return;
		}
		printDebug("writeSysex: Message is too short (%d bytes)!", len);
		return;
	}

	Bit32u addr = (sysex[0] << 16) | (sysex[1] << 8) | (sysex[2]);
	addr = MT32EMU_MEMADDR(addr);
	sysex += 3;
	len -= 3;

	//printDebug("Sysex addr: 0x%06x", MT32EMU_SYSEXMEMADDR(addr));
	// NOTE: Please keep both lower and upper bounds in each check, for ease of reading

	// Process channel-specific sysex by converting it to device-global
	if (device < 0x10) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-CHANNEL: Channel %d temp area 0x%06x", device, MT32EMU_SYSEXMEMADDR(addr));
#endif
		if (/*addr >= MT32EMU_MEMADDR(0x000000) && */addr < MT32EMU_MEMADDR(0x010000)) {
			addr += MT32EMU_MEMADDR(0x030000);
			Bit8u *chanParts = extensions.chantable[device];
			if (*chanParts > 8) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel not mapped to a part... 0 offset)");
#endif
			} else {
				for (Bit32u partIx = 0; partIx <= 8; partIx++) {
					if (chanParts[partIx] > 8) break;
					int offset;
					if (chanParts[partIx] == 8) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Channel mapped to rhythm... 0 offset)");
#endif
						offset = 0;
					} else {
						offset = chanParts[partIx] * sizeof(MemParams::PatchTemp);
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Setting extra offset to %d)", offset);
#endif
					}
					writeSysexGlobal(addr + offset, sysex, len);
				}
				return;
			}
		} else if (/*addr >= MT32EMU_MEMADDR(0x010000) && */ addr < MT32EMU_MEMADDR(0x020000)) {
			addr += MT32EMU_MEMADDR(0x030110) - MT32EMU_MEMADDR(0x010000);
		} else if (/*addr >= MT32EMU_MEMADDR(0x020000) && */ addr < MT32EMU_MEMADDR(0x030000)) {
			addr += MT32EMU_MEMADDR(0x040000) - MT32EMU_MEMADDR(0x020000);
			Bit8u *chanParts = extensions.chantable[device];
			if (*chanParts > 8) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel not mapped to a part... 0 offset)");
#endif
			} else {
				for (Bit32u partIx = 0; partIx <= 8; partIx++) {
					if (chanParts[partIx] > 8) break;
					int offset;
					if (chanParts[partIx] == 8) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Channel mapped to rhythm... 0 offset)");
#endif
						offset = 0;
					} else {
						offset = chanParts[partIx] * sizeof(TimbreParam);
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Setting extra offset to %d)", offset);
#endif
					}
					writeSysexGlobal(addr + offset, sysex, len);
				}
				return;
			}
		} else {
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug(" Invalid channel");
#endif
			return;
		}
	}
	writeSysexGlobal(addr, sysex, len);
}

// Process device-global sysex (possibly converted from channel-specific sysex above)
void Synth::writeSysexGlobal(Bit32u addr, const Bit8u *sysex, Bit32u len) {
	for (;;) {
		// Find the appropriate memory region
		const MemoryRegion *region = findMemoryRegion(addr);

		if (region == NULL) {
			printDebug("Sysex write to unrecognised address %06x, len %d", MT32EMU_SYSEXMEMADDR(addr), len);
			// FIXME: Real devices may respond differently to a long SysEx that covers adjacent regions.
			break;
		}
		writeMemoryRegion(region, addr, region->getClampedLen(addr, len), sysex);

		Bit32u next = region->next(addr, len);
		if (next == 0) {
			break;
		}
		addr += next;
		sysex += next;
		len -= next;
	}
}

void Synth::readMemory(Bit32u addr, Bit32u len, Bit8u *data) {
	if (!opened) return;
	const MemoryRegion *region = findMemoryRegion(addr);
	if (region != NULL) {
		readMemoryRegion(region, addr, len, data);
	}
}

void Synth::initMemoryRegions() {
	// Timbre max tables are slightly more complicated than the others, which are used directly from the ROM.
	// The ROM (sensibly) just has maximums for TimbreParam.commonParam followed by just one TimbreParam.partialParam,
	// so we produce a table with all partialParams filled out, as well as padding for PaddedTimbre, for quick lookup.
	paddedTimbreMaxTable = new Bit8u[sizeof(MemParams::PaddedTimbre)];
	memcpy(&paddedTimbreMaxTable[0], &controlROMData[controlROMMap->timbreMaxTable], sizeof(TimbreParam::CommonParam) + sizeof(TimbreParam::PartialParam)); // commonParam and one partialParam
	int pos = sizeof(TimbreParam::CommonParam) + sizeof(TimbreParam::PartialParam);
	for (int i = 0; i < 3; i++) {
		memcpy(&paddedTimbreMaxTable[pos], &controlROMData[controlROMMap->timbreMaxTable + sizeof(TimbreParam::CommonParam)], sizeof(TimbreParam::PartialParam));
		pos += sizeof(TimbreParam::PartialParam);
	}
	memset(&paddedTimbreMaxTable[pos], 0, 10); // Padding
	patchTempMemoryRegion = new PatchTempMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.patchTemp[0]), &controlROMData[controlROMMap->patchMaxTable]);
	rhythmTempMemoryRegion = new RhythmTempMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.rhythmTemp[0]), &controlROMData[controlROMMap->rhythmMaxTable]);
	timbreTempMemoryRegion = new TimbreTempMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.timbreTemp[0]), paddedTimbreMaxTable);
	patchesMemoryRegion = new PatchesMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.patches[0]), &controlROMData[controlROMMap->patchMaxTable]);
	timbresMemoryRegion = new TimbresMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.timbres[0]), paddedTimbreMaxTable);
	systemMemoryRegion = new SystemMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.system), &controlROMData[controlROMMap->systemMaxTable]);
	displayMemoryRegion = new DisplayMemoryRegion(this);
	resetMemoryRegion = new ResetMemoryRegion(this);
}

void Synth::deleteMemoryRegions() {
	delete patchTempMemoryRegion;
	patchTempMemoryRegion = NULL;
	delete rhythmTempMemoryRegion;
	rhythmTempMemoryRegion = NULL;
	delete timbreTempMemoryRegion;
	timbreTempMemoryRegion = NULL;
	delete patchesMemoryRegion;
	patchesMemoryRegion = NULL;
	delete timbresMemoryRegion;
	timbresMemoryRegion = NULL;
	delete systemMemoryRegion;
	systemMemoryRegion = NULL;
	delete displayMemoryRegion;
	displayMemoryRegion = NULL;
	delete resetMemoryRegion;
	resetMemoryRegion = NULL;

	delete[] paddedTimbreMaxTable;
	paddedTimbreMaxTable = NULL;
}

MemoryRegion *Synth::findMemoryRegion(Bit32u addr) {
	MemoryRegion *regions[] = {
		patchTempMemoryRegion,
		rhythmTempMemoryRegion,
		timbreTempMemoryRegion,
		patchesMemoryRegion,
		timbresMemoryRegion,
		systemMemoryRegion,
		displayMemoryRegion,
		resetMemoryRegion,
		NULL
	};
	for (int pos = 0; regions[pos] != NULL; pos++) {
		if (regions[pos]->contains(addr)) {
			return regions[pos];
		}
	}
	return NULL;
}

void Synth::readMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, Bit8u *data) {
	unsigned int first = region->firstTouched(addr);
	//unsigned int last = region->lastTouched(addr, len);
	unsigned int off = region->firstTouchedOffset(addr);
	len = region->getClampedLen(addr, len);

	unsigned int m;

	if (region->isReadable()) {
		region->read(first, off, data, len);
	} else {
		// FIXME: We might want to do these properly in future
		for (m = 0; m < len; m += 2) {
			data[m] = 0xff;
			if (m + 1 < len) {
				data[m+1] = Bit8u(region->type);
			}
		}
	}
}

void Synth::writeMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, const Bit8u *data) {
	unsigned int first = region->firstTouched(addr);
	unsigned int last = region->lastTouched(addr, len);
	unsigned int off = region->firstTouchedOffset(addr);
	switch (region->type) {
	case MR_PatchTemp:
		region->write(first, off, data, len);
		//printDebug("Patch temp: Patch %d, offset %x, len %d", off/16, off % 16, len);

		for (unsigned int i = first; i <= last; i++) {
#if MT32EMU_MONITOR_SYSEX > 0
			int absTimbreNum = mt32ram.patchTemp[i].patch.timbreGroup * 64 + mt32ram.patchTemp[i].patch.timbreNum;
			char timbreName[11];
			memcpy(timbreName, mt32ram.timbres[absTimbreNum].timbre.common.name, 10);
			timbreName[10] = 0;
			printDebug("WRITE-PARTPATCH (%d-%d@%d..%d): %d; timbre=%d (%s), outlevel=%d", first, last, off, off + len, i, absTimbreNum, timbreName, mt32ram.patchTemp[i].outputLevel);
#endif
			if (parts[i] != NULL) {
				if (i != 8) {
					// Note: Confirmed on CM-64 that we definitely *should* update the timbre here,
					// but only in the case that the sysex actually writes to those values
					if (i == first && off > 2) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Not updating timbre, since those values weren't touched)");
#endif
					} else {
						parts[i]->setTimbre(&mt32ram.timbres[parts[i]->getAbsTimbreNum()].timbre);
					}
				}
				parts[i]->refresh();
			}
		}
		break;
	case MR_RhythmTemp:
		region->write(first, off, data, len);
#if MT32EMU_MONITOR_SYSEX > 0
		for (unsigned int i = first; i <= last; i++) {
			int timbreNum = mt32ram.rhythmTemp[i].timbre;
			char timbreName[11];
			if (timbreNum < 94) {
				memcpy(timbreName, mt32ram.timbres[128 + timbreNum].timbre.common.name, 10);
				timbreName[10] = 0;
			} else {
				strcpy(timbreName, "[None]");
			}
			printDebug("WRITE-RHYTHM (%d-%d@%d..%d): %d; level=%02x, panpot=%02x, reverb=%02x, timbre=%d (%s)", first, last, off, off + len, i, mt32ram.rhythmTemp[i].outputLevel, mt32ram.rhythmTemp[i].panpot, mt32ram.rhythmTemp[i].reverbSwitch, mt32ram.rhythmTemp[i].timbre, timbreName);
		}
#endif
		if (parts[8] != NULL) {
			parts[8]->refresh();
		}
		break;
	case MR_TimbreTemp:
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
#if MT32EMU_MONITOR_SYSEX > 0
			char instrumentName[11];
			memcpy(instrumentName, mt32ram.timbreTemp[i].common.name, 10);
			instrumentName[10] = 0;
			printDebug("WRITE-PARTTIMBRE (%d-%d@%d..%d): timbre=%d (%s)", first, last, off, off + len, i, instrumentName);
#endif
			if (parts[i] != NULL) {
				parts[i]->refresh();
			}
		}
		break;
	case MR_Patches:
		region->write(first, off, data, len);
#if MT32EMU_MONITOR_SYSEX > 0
		for (unsigned int i = first; i <= last; i++) {
			PatchParam *patch = &mt32ram.patches[i];
			int patchAbsTimbreNum = patch->timbreGroup * 64 + patch->timbreNum;
			char instrumentName[11];
			memcpy(instrumentName, mt32ram.timbres[patchAbsTimbreNum].timbre.common.name, 10);
			instrumentName[10] = 0;
			Bit8u *n = reinterpret_cast<Bit8u *>(patch);
			printDebug("WRITE-PATCH (%d-%d@%d..%d): %d; timbre=%d (%s) %02X%02X%02X%02X%02X%02X%02X%02X", first, last, off, off + len, i, patchAbsTimbreNum, instrumentName, n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7]);
		}
#endif
		break;
	case MR_Timbres:
		// Timbres
		first += 128;
		last += 128;
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
#if MT32EMU_MONITOR_TIMBRES >= 1
			TimbreParam *timbre = &mt32ram.timbres[i].timbre;
			char instrumentName[11];
			memcpy(instrumentName, timbre->common.name, 10);
			instrumentName[10] = 0;
			printDebug("WRITE-TIMBRE (%d-%d@%d..%d): %d; name=\"%s\"", first, last, off, off + len, i, instrumentName);
#if MT32EMU_MONITOR_TIMBRES >= 2
#define DT(x) printDebug(" " #x ": %d", timbre->x)
			DT(common.partialStructure12);
			DT(common.partialStructure34);
			DT(common.partialMute);
			DT(common.noSustain);

#define DTP(x) \
			DT(partial[x].wg.pitchCoarse); \
			DT(partial[x].wg.pitchFine); \
			DT(partial[x].wg.pitchKeyfollow); \
			DT(partial[x].wg.pitchBenderEnabled); \
			DT(partial[x].wg.waveform); \
			DT(partial[x].wg.pcmWave); \
			DT(partial[x].wg.pulseWidth); \
			DT(partial[x].wg.pulseWidthVeloSensitivity); \
			DT(partial[x].pitchEnv.depth); \
			DT(partial[x].pitchEnv.veloSensitivity); \
			DT(partial[x].pitchEnv.timeKeyfollow); \
			DT(partial[x].pitchEnv.time[0]); \
			DT(partial[x].pitchEnv.time[1]); \
			DT(partial[x].pitchEnv.time[2]); \
			DT(partial[x].pitchEnv.time[3]); \
			DT(partial[x].pitchEnv.level[0]); \
			DT(partial[x].pitchEnv.level[1]); \
			DT(partial[x].pitchEnv.level[2]); \
			DT(partial[x].pitchEnv.level[3]); \
			DT(partial[x].pitchEnv.level[4]); \
			DT(partial[x].pitchLFO.rate); \
			DT(partial[x].pitchLFO.depth); \
			DT(partial[x].pitchLFO.modSensitivity); \
			DT(partial[x].tvf.cutoff); \
			DT(partial[x].tvf.resonance); \
			DT(partial[x].tvf.keyfollow); \
			DT(partial[x].tvf.biasPoint); \
			DT(partial[x].tvf.biasLevel); \
			DT(partial[x].tvf.envDepth); \
			DT(partial[x].tvf.envVeloSensitivity); \
			DT(partial[x].tvf.envDepthKeyfollow); \
			DT(partial[x].tvf.envTimeKeyfollow); \
			DT(partial[x].tvf.envTime[0]); \
			DT(partial[x].tvf.envTime[1]); \
			DT(partial[x].tvf.envTime[2]); \
			DT(partial[x].tvf.envTime[3]); \
			DT(partial[x].tvf.envTime[4]); \
			DT(partial[x].tvf.envLevel[0]); \
			DT(partial[x].tvf.envLevel[1]); \
			DT(partial[x].tvf.envLevel[2]); \
			DT(partial[x].tvf.envLevel[3]); \
			DT(partial[x].tva.level); \
			DT(partial[x].tva.veloSensitivity); \
			DT(partial[x].tva.biasPoint1); \
			DT(partial[x].tva.biasLevel1); \
			DT(partial[x].tva.biasPoint2); \
			DT(partial[x].tva.biasLevel2); \
			DT(partial[x].tva.envTimeKeyfollow); \
			DT(partial[x].tva.envTimeVeloSensitivity); \
			DT(partial[x].tva.envTime[0]); \
			DT(partial[x].tva.envTime[1]); \
			DT(partial[x].tva.envTime[2]); \
			DT(partial[x].tva.envTime[3]); \
			DT(partial[x].tva.envTime[4]); \
			DT(partial[x].tva.envLevel[0]); \
			DT(partial[x].tva.envLevel[1]); \
			DT(partial[x].tva.envLevel[2]); \
			DT(partial[x].tva.envLevel[3]);

			DTP(0);
			DTP(1);
			DTP(2);
			DTP(3);
#undef DTP
#undef DT
#endif
#endif
			// FIXME:KG: Not sure if the stuff below should be done (for rhythm and/or parts)...
			// Does the real MT-32 automatically do this?
			for (unsigned int part = 0; part < 9; part++) {
				if (parts[part] != NULL) {
					parts[part]->refreshTimbre(i);
				}
			}
		}
		break;
	case MR_System:
		region->write(0, off, data, len);

		// FIXME: We haven't properly confirmed any of this behaviour
		// In particular, we tend to reset things such as reverb even if the write contained
		// the same parameters as were already set, which may be wrong.
		// On the other hand, the real thing could be resetting things even when they aren't touched
		// by the write at all.
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-SYSTEM:");
#endif
		if (off <= SYSTEM_MASTER_TUNE_OFF && off + len > SYSTEM_MASTER_TUNE_OFF) {
			refreshSystemMasterTune();
		}
		if (off <= SYSTEM_REVERB_LEVEL_OFF && off + len > SYSTEM_REVERB_MODE_OFF) {
			refreshSystemReverbParameters();
		}
		if (off <= SYSTEM_RESERVE_SETTINGS_END_OFF && off + len > SYSTEM_RESERVE_SETTINGS_START_OFF) {
			refreshSystemReserveSettings();
		}
		if (off <= SYSTEM_CHAN_ASSIGN_END_OFF && off + len > SYSTEM_CHAN_ASSIGN_START_OFF) {
			int firstPart = off - SYSTEM_CHAN_ASSIGN_START_OFF;
			if(firstPart < 0)
				firstPart = 0;
			int lastPart = off + len - SYSTEM_CHAN_ASSIGN_START_OFF;
			if(lastPart > 8)
				lastPart = 8;
			refreshSystemChanAssign(Bit8u(firstPart), Bit8u(lastPart));
		}
		if (off <= SYSTEM_MASTER_VOL_OFF && off + len > SYSTEM_MASTER_VOL_OFF) {
			refreshSystemMasterVol();
		}
		break;
	case MR_Display:
#if MT32EMU_MONITOR_SYSEX > 0
		if (len > /*Display::LCD_TEXT_SIZE*/20) len = /*Display::LCD_TEXT_SIZE*/20;
		// Holds zero-terminated string of the maximum length.
		char buf[/*Display::LCD_TEXT_SIZE*/20 + 1];
		memcpy(&buf, &data[0], len);
		buf[len] = 0;
		printDebug("WRITE-LCD: %s", buf);
#endif
		break;
	case MR_Reset:
		reset();
		break;
	default:
		break;
	}
}

void Synth::refreshSystemMasterTune() {
	// 171 is ~half a semitone.
	extensions.masterTunePitchDelta = ((mt32ram.system.masterTune - 64) * 171) >> 6; // PORTABILITY NOTE: Assumes arithmetic shift.
#if MT32EMU_MONITOR_SYSEX > 0
	//FIXME:KG: This is just an educated guess.
	// The LAPC-I documentation claims a range of 427.5Hz-452.6Hz (similar to what we have here)
	// The MT-32 documentation claims a range of 432.1Hz-457.6Hz
	float masterTune = 440.0f * EXP2F((mt32ram.system.masterTune - 64.0f) / (128.0f * 12.0f));
	printDebug(" Master Tune: %f", masterTune);
#endif
}

void Synth::refreshSystemReverbParameters() {
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Reverb: mode=%d, time=%d, level=%d", mt32ram.system.reverbMode, mt32ram.system.reverbTime, mt32ram.system.reverbLevel);
#endif
	if (reverbOverridden) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug(" (Reverb overridden - ignoring)");
#endif
		return;
	}

	BReverbModel *oldReverbModel = reverbModel;
	if (mt32ram.system.reverbTime == 0 && mt32ram.system.reverbLevel == 0) {
		// Setting both time and level to 0 effectively disables wet reverb output on real devices.
		// Take a shortcut in this case to reduce CPU load.
		reverbModel = NULL;
	} else {
		reverbModel = reverbModels[mt32ram.system.reverbMode];
	}
	if (reverbModel != oldReverbModel) {
		if (extensions.preallocatedReverbMemory) {
			if (isReverbEnabled()) {
				reverbModel->mute();
			}
		} else {
			if (oldReverbModel != NULL) {
				oldReverbModel->close();
			}
			if (isReverbEnabled()) {
				reverbModel->open();
			}
		}
	}
	if (isReverbEnabled()) {
		reverbModel->setParameters(mt32ram.system.reverbTime, mt32ram.system.reverbLevel);
	}
}

void Synth::refreshSystemReserveSettings() {
	Bit8u *rset = mt32ram.system.reserveSettings;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Partial reserve: 1=%02d 2=%02d 3=%02d 4=%02d 5=%02d 6=%02d 7=%02d 8=%02d Rhythm=%02d", rset[0], rset[1], rset[2], rset[3], rset[4], rset[5], rset[6], rset[7], rset[8]);
#endif
	partialManager->setReserve(rset);
}

void Synth::refreshSystemChanAssign(Bit8u firstPart, Bit8u lastPart) {
	memset(extensions.chantable, 0xFF, sizeof(extensions.chantable));

	// CONFIRMED: In the case of assigning a MIDI channel to multiple parts,
	//            the messages received on that MIDI channel are handled by all the parts.
	for (Bit32u i = 0; i <= 8; i++) {
		if (parts[i] != NULL && i >= firstPart && i <= lastPart) {
			// CONFIRMED: Decay is started for all polys, and all controllers are reset, for every part whose assignment was touched by the sysex write.
			parts[i]->allSoundOff();
			parts[i]->resetAllControllers();
		}
		Bit8u chan = mt32ram.system.chanAssign[i];
		if (chan > 15) continue;
		Bit8u *chanParts = extensions.chantable[chan];
		for (Bit32u j = 0; j <= 8; j++) {
			if (chanParts[j] > 8) {
				chanParts[j] = Bit8u(i);
				break;
			}
		}
	}

#if MT32EMU_MONITOR_SYSEX > 0
	Bit8u *rset = mt32ram.system.chanAssign;
	printDebug(" Part assign:     1=%02d 2=%02d 3=%02d 4=%02d 5=%02d 6=%02d 7=%02d 8=%02d Rhythm=%02d", rset[0], rset[1], rset[2], rset[3], rset[4], rset[5], rset[6], rset[7], rset[8]);
#endif
}

void Synth::refreshSystemMasterVol() {
	// Note, this should only occur when the user turns the volume knob. When the master volume is set via a SysEx, display
	// doesn't actually update on all real devices. However, we anyway update the display, as we don't foresee a dedicated
	// API for setting the master volume yet it's rather dubious that one really needs this quirk to be fairly emulated.
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Master volume: %d", mt32ram.system.masterVol);
#endif
}

void Synth::refreshSystem() {
	refreshSystemMasterTune();
	refreshSystemReverbParameters();
	refreshSystemReserveSettings();
	refreshSystemChanAssign(0, 8);
	refreshSystemMasterVol();
}

void Synth::reset() {
	if (!opened) return;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug("RESET");
#endif
	partialManager->deactivateAll();
	mt32ram = mt32default;
	for (int i = 0; i < 9; i++) {
		parts[i]->reset();
		if (i != 8) {
			parts[i]->setProgram(controlROMData[controlROMMap->programSettings + i]);
		} else {
			parts[8]->refresh();
		}
	}
	refreshSystem();
	resetMasterTunePitchDelta();
	isActive();
}

void Synth::resetMasterTunePitchDelta() {
	// This effectively resets master tune to 440.0Hz.
	// Despite that the manual claims 442.0Hz is the default setting for master tune,
	// it doesn't actually take effect upon a reset due to a bug in the reset routine.
	// CONFIRMED: This bug is present in all supported Control ROMs.
	extensions.masterTunePitchDelta = 0;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Actual Master Tune reset to 440.0");
#endif
}

Bit32s Synth::getMasterTunePitchDelta() const {
	return extensions.masterTunePitchDelta;
}

/** Defines an interface of a class that maintains storage of variable-sized data of SysEx messages. */
class MidiEventQueue::SysexDataStorage {
public:
	static MidiEventQueue::SysexDataStorage *create(Bit32u storageBufferSize);

	virtual ~SysexDataStorage() {}
	virtual Bit8u *allocate(Bit32u sysexLength) = 0;
	virtual void reclaimUnused(const Bit8u *sysexData, Bit32u sysexLength) = 0;
	virtual void dispose(const Bit8u *sysexData, Bit32u sysexLength) = 0;
};

/** Storage space for SysEx data is allocated dynamically on demand and is disposed lazily. */
class DynamicSysexDataStorage : public MidiEventQueue::SysexDataStorage {
public:
	Bit8u *allocate(Bit32u sysexLength) {
		return new Bit8u[sysexLength];
	}

	void reclaimUnused(const Bit8u *, Bit32u) {}

	void dispose(const Bit8u *sysexData, Bit32u) {
		delete[] sysexData;
	}
};

/**
 * SysEx data is stored in a preallocated buffer, that makes this kind of storage safe
 * for use in a realtime thread. Additionally, the space retained by a SysEx event,
 * that has been processed and thus is no longer necessary, is disposed instantly.
 */
class BufferedSysexDataStorage : public MidiEventQueue::SysexDataStorage {
public:
	explicit BufferedSysexDataStorage(Bit32u useStorageBufferSize) :
		storageBuffer(new Bit8u[useStorageBufferSize]),
		storageBufferSize(useStorageBufferSize),
		startPosition(),
		endPosition()
	{}

	~BufferedSysexDataStorage() {
		delete[] storageBuffer;
	}

	Bit8u *allocate(Bit32u sysexLength) {
		Bit32u myStartPosition = startPosition;
		Bit32u myEndPosition = endPosition;

		// When the free space isn't contiguous, the data is allocated either right after the end position
		// or at the buffer beginning, wherever it fits.
		if (myStartPosition > myEndPosition) {
			if (myStartPosition - myEndPosition <= sysexLength) return NULL;
		} else if (storageBufferSize - myEndPosition < sysexLength) {
			// There's not enough free space at the end to place the data block.
			if (myStartPosition == myEndPosition) {
				// The buffer is empty -> reset positions to the buffer beginning.
				if (storageBufferSize <= sysexLength) return NULL;
				if (myStartPosition != 0) {
					myStartPosition = 0;
					// It's OK to write startPosition here non-atomically. We don't expect any
					// concurrent reads, as there must be no SysEx messages in the queue.
					startPosition = myStartPosition;
				}
			} else if (myStartPosition <= sysexLength) return NULL;
			myEndPosition = 0;
		}
		endPosition = myEndPosition + sysexLength;
		return storageBuffer + myEndPosition;
	}

	void reclaimUnused(const Bit8u *sysexData, Bit32u sysexLength) {
		if (sysexData == NULL) return;
		Bit32u allocatedPosition = startPosition;
		if (storageBuffer + allocatedPosition == sysexData) {
			startPosition = allocatedPosition + sysexLength;
		} else if (storageBuffer == sysexData) {
			// Buffer wrapped around.
			startPosition = sysexLength;
		}
	}

	void dispose(const Bit8u *, Bit32u) {}

private:
	Bit8u * const storageBuffer;
	const Bit32u storageBufferSize;

	volatile Bit32u startPosition;
	volatile Bit32u endPosition;
};

MidiEventQueue::SysexDataStorage *MidiEventQueue::SysexDataStorage::create(Bit32u storageBufferSize) {
	if (storageBufferSize > 0) {
		return new BufferedSysexDataStorage(storageBufferSize);
	} else {
		return new DynamicSysexDataStorage;
	}
}

MidiEventQueue::MidiEventQueue(Bit32u useRingBufferSize, Bit32u storageBufferSize) :
	sysexDataStorage(*SysexDataStorage::create(storageBufferSize)),
	ringBuffer(new MidiEvent[useRingBufferSize]), ringBufferMask(useRingBufferSize - 1)
{
	for (Bit32u i = 0; i <= ringBufferMask; i++) {
		ringBuffer[i].sysexData = NULL;
	}
	reset();
}

MidiEventQueue::~MidiEventQueue() {
	for (Bit32u i = 0; i <= ringBufferMask; i++) {
		volatile MidiEvent &currentEvent = ringBuffer[i];
		sysexDataStorage.dispose(currentEvent.sysexData, currentEvent.sysexLength);
	}
	delete &sysexDataStorage;
	delete[] ringBuffer;
}

void MidiEventQueue::reset() {
	startPosition = 0;
	endPosition = 0;
}

bool MidiEventQueue::pushShortMessage(Bit32u shortMessageData, Bit32u timestamp) {
	Bit32u newEndPosition = (endPosition + 1) & ringBufferMask;
	// If ring buffer is full, bail out.
	if (startPosition == newEndPosition) return false;
	volatile MidiEvent &newEvent = ringBuffer[endPosition];
	sysexDataStorage.dispose(newEvent.sysexData, newEvent.sysexLength);
	newEvent.sysexData = NULL;
	newEvent.shortMessageData = shortMessageData;
	newEvent.timestamp = timestamp;
	endPosition = newEndPosition;
	return true;
}

bool MidiEventQueue::pushSysex(const Bit8u *sysexData, Bit32u sysexLength, Bit32u timestamp) {
	Bit32u newEndPosition = (endPosition + 1) & ringBufferMask;
	// If ring buffer is full, bail out.
	if (startPosition == newEndPosition) return false;
	volatile MidiEvent &newEvent = ringBuffer[endPosition];
	sysexDataStorage.dispose(newEvent.sysexData, newEvent.sysexLength);
	Bit8u *dstSysexData = sysexDataStorage.allocate(sysexLength);
	if (dstSysexData == NULL) return false;
	memcpy(dstSysexData, sysexData, sysexLength);
	newEvent.sysexData = dstSysexData;
	newEvent.sysexLength = sysexLength;
	newEvent.timestamp = timestamp;
	endPosition = newEndPosition;
	return true;
}

const volatile MidiEventQueue::MidiEvent *MidiEventQueue::peekMidiEvent() {
	return isEmpty() ? NULL : &ringBuffer[startPosition];
}

void MidiEventQueue::dropMidiEvent() {
	if (isEmpty()) return;
	volatile MidiEvent &unusedEvent = ringBuffer[startPosition];
	sysexDataStorage.reclaimUnused(unusedEvent.sysexData, unusedEvent.sysexLength);
	startPosition = (startPosition + 1) & ringBufferMask;
}

bool MidiEventQueue::isEmpty() const {
	return startPosition == endPosition;
}

void Synth::selectRendererType(RendererType newRendererType) {
	extensions.selectedRendererType = newRendererType;
}

RendererType Synth::getSelectedRendererType() const {
	return extensions.selectedRendererType;
}

Bit32u Synth::getStereoOutputSampleRate() const {
	return (analog == NULL) ? SAMPLE_RATE : analog->getOutputSampleRate();
}

template <class Sample>
void RendererImpl<Sample>::doRender(Sample *stereoStream, Bit32u len) {
	if (!isActivated()) {
		incRenderedSampleCount(getAnalog().getDACStreamsLength(len));
		if (!getAnalog().process(NULL, NULL, NULL, NULL, NULL, NULL, stereoStream, len)) {
			printDebug("RendererImpl: Invalid call to Analog::process()!\n");
		}
		Synth::muteSampleBuffer(stereoStream, len << 1);
		updateDisplayState();
		return;
	}

	while (len > 0) {
		// As in AnalogOutputMode_ACCURATE mode output is upsampled, MAX_SAMPLES_PER_RUN is more than enough for the temp buffers.
		Bit32u thisPassLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		doRenderStreams(tmpBuffers, getAnalog().getDACStreamsLength(thisPassLen));
		if (!getAnalog().process(stereoStream, tmpNonReverbLeft, tmpNonReverbRight, tmpReverbDryLeft, tmpReverbDryRight, tmpReverbWetLeft, tmpReverbWetRight, thisPassLen)) {
			printDebug("RendererImpl: Invalid call to Analog::process()!\n");
			Synth::muteSampleBuffer(stereoStream, len << 1);
			return;
		}
		stereoStream += thisPassLen << 1;
		len -= thisPassLen;
	}
}

template <class Sample>
template <class O>
void RendererImpl<Sample>::doRenderAndConvert(O *stereoStream, Bit32u len) {
	Sample renderingBuffer[MAX_SAMPLES_PER_RUN << 1];
	while (len > 0) {
		Bit32u thisPassLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		doRender(renderingBuffer, thisPassLen);
		convertSampleFormat(renderingBuffer, stereoStream, thisPassLen << 1);
		stereoStream += thisPassLen << 1;
		len -= thisPassLen;
	}
}

template<>
void RendererImpl<IntSample>::render(IntSample *stereoStream, Bit32u len) {
	doRender(stereoStream, len);
}

template<>
void RendererImpl<IntSample>::render(FloatSample *stereoStream, Bit32u len) {
	doRenderAndConvert(stereoStream, len);
}

template<>
void RendererImpl<FloatSample>::render(IntSample *stereoStream, Bit32u len) {
	doRenderAndConvert(stereoStream, len);
}

template<>
void RendererImpl<FloatSample>::render(FloatSample *stereoStream, Bit32u len) {
	doRender(stereoStream, len);
}

template <class S>
static inline void renderStereo(bool opened, Renderer *renderer, S *stream, Bit32u len) {
	if (opened) {
		renderer->render(stream, len);
	} else {
		Synth::muteSampleBuffer(stream, len << 1);
	}
}

void Synth::render(Bit16s *stream, Bit32u len) {
	renderStereo(opened, renderer, stream, len);
}

void Synth::render(float *stream, Bit32u len) {
	renderStereo(opened, renderer, stream, len);
}

template <class Sample>
static inline void advanceStream(Sample *&stream, Bit32u len) {
	if (stream != NULL) {
		stream += len;
	}
}

template <class Sample>
static inline void advanceStreams(DACOutputStreams<Sample> &streams, Bit32u len) {
	advanceStream(streams.nonReverbLeft, len);
	advanceStream(streams.nonReverbRight, len);
	advanceStream(streams.reverbDryLeft, len);
	advanceStream(streams.reverbDryRight, len);
	advanceStream(streams.reverbWetLeft, len);
	advanceStream(streams.reverbWetRight, len);
}

template <class Sample>
static inline void muteStreams(const DACOutputStreams<Sample> &streams, Bit32u len) {
	Synth::muteSampleBuffer(streams.nonReverbLeft, len);
	Synth::muteSampleBuffer(streams.nonReverbRight, len);
	Synth::muteSampleBuffer(streams.reverbDryLeft, len);
	Synth::muteSampleBuffer(streams.reverbDryRight, len);
	Synth::muteSampleBuffer(streams.reverbWetLeft, len);
	Synth::muteSampleBuffer(streams.reverbWetRight, len);
}

template <class I, class O>
static inline void convertStreamsFormat(const DACOutputStreams<I> &inStreams, const DACOutputStreams<O> &outStreams, Bit32u len) {
	convertSampleFormat(inStreams.nonReverbLeft, outStreams.nonReverbLeft, len);
	convertSampleFormat(inStreams.nonReverbRight, outStreams.nonReverbRight, len);
	convertSampleFormat(inStreams.reverbDryLeft, outStreams.reverbDryLeft, len);
	convertSampleFormat(inStreams.reverbDryRight, outStreams.reverbDryRight, len);
	convertSampleFormat(inStreams.reverbWetLeft, outStreams.reverbWetLeft, len);
	convertSampleFormat(inStreams.reverbWetRight, outStreams.reverbWetRight, len);
}

template <class Sample>
void RendererImpl<Sample>::doRenderStreams(const DACOutputStreams<Sample> &streams, Bit32u len)
{
	DACOutputStreams<Sample> tmpStreams = streams;
	while (len > 0) {
		// We need to ensure zero-duration notes will play so add minimum 1-sample delay.
		Bit32u thisLen = 1;
		if (!isAbortingPoly()) {
			const volatile MidiEventQueue::MidiEvent *nextEvent = getMidiQueue().peekMidiEvent();
			Bit32s samplesToNextEvent = (nextEvent != NULL) ? Bit32s(nextEvent->timestamp - getRenderedSampleCount()) : MAX_SAMPLES_PER_RUN;
			if (samplesToNextEvent > 0) {
				thisLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
				if (thisLen > Bit32u(samplesToNextEvent)) {
					thisLen = samplesToNextEvent;
				}
			} else {
				if (nextEvent->sysexData == NULL) {
					synth.playMsgNow(nextEvent->shortMessageData);
					// If a poly is aborting we don't drop the event from the queue.
					// Instead, we'll return to it again when the abortion is done.
					if (!isAbortingPoly()) {
						getMidiQueue().dropMidiEvent();
					}
				} else {
					synth.playSysexNow(nextEvent->sysexData, nextEvent->sysexLength);
					getMidiQueue().dropMidiEvent();
				}
			}
		}
		produceStreams(tmpStreams, thisLen);
		advanceStreams(tmpStreams, thisLen);
		len -= thisLen;
	}
}

template <class Sample>
template <class O>
void RendererImpl<Sample>::doRenderAndConvertStreams(const DACOutputStreams<O> &streams, Bit32u len) {
	Sample cnvNonReverbLeft[MAX_SAMPLES_PER_RUN], cnvNonReverbRight[MAX_SAMPLES_PER_RUN];
	Sample cnvReverbDryLeft[MAX_SAMPLES_PER_RUN], cnvReverbDryRight[MAX_SAMPLES_PER_RUN];
	Sample cnvReverbWetLeft[MAX_SAMPLES_PER_RUN], cnvReverbWetRight[MAX_SAMPLES_PER_RUN];

	const DACOutputStreams<Sample> cnvStreams = {
		cnvNonReverbLeft, cnvNonReverbRight,
		cnvReverbDryLeft, cnvReverbDryRight,
		cnvReverbWetLeft, cnvReverbWetRight
	};

	DACOutputStreams<O> tmpStreams = streams;

	while (len > 0) {
		Bit32u thisPassLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		doRenderStreams(cnvStreams, thisPassLen);
		convertStreamsFormat(cnvStreams, tmpStreams, thisPassLen);
		advanceStreams(tmpStreams, thisPassLen);
		len -= thisPassLen;
	}
}

template<>
void RendererImpl<IntSample>::renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len) {
	doRenderStreams(streams, len);
}

template<>
void RendererImpl<IntSample>::renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len) {
	doRenderAndConvertStreams(streams, len);
}

template<>
void RendererImpl<FloatSample>::renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len) {
	doRenderAndConvertStreams(streams, len);
}

template<>
void RendererImpl<FloatSample>::renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len) {
	doRenderStreams(streams, len);
}

template <class S>
static inline void renderStreams(bool opened, Renderer *renderer, const DACOutputStreams<S> &streams, Bit32u len) {
	if (opened) {
		renderer->renderStreams(streams, len);
	} else {
		muteStreams(streams, len);
	}
}

void Synth::renderStreams(const DACOutputStreams<Bit16s> &streams, Bit32u len) {
	MT32Emu::renderStreams(opened, renderer, streams, len);
}

void Synth::renderStreams(const DACOutputStreams<float> &streams, Bit32u len) {
	MT32Emu::renderStreams(opened, renderer, streams, len);
}

void Synth::renderStreams(
	Bit16s *nonReverbLeft, Bit16s *nonReverbRight,
	Bit16s *reverbDryLeft, Bit16s *reverbDryRight,
	Bit16s *reverbWetLeft, Bit16s *reverbWetRight,
	Bit32u len)
{
	DACOutputStreams<IntSample> streams = {
		nonReverbLeft, nonReverbRight,
		reverbDryLeft, reverbDryRight,
		reverbWetLeft, reverbWetRight
	};
	renderStreams(streams, len);
}

void Synth::renderStreams(
	float *nonReverbLeft, float *nonReverbRight,
	float *reverbDryLeft, float *reverbDryRight,
	float *reverbWetLeft, float *reverbWetRight,
	Bit32u len)
{
	DACOutputStreams<FloatSample> streams = {
		nonReverbLeft, nonReverbRight,
		reverbDryLeft, reverbDryRight,
		reverbWetLeft, reverbWetRight
	};
	renderStreams(streams, len);
}

// In GENERATION2 units, the output from LA32 goes to the Boss chip already bit-shifted.
// In NICE mode, it's also better to increase volume before the reverb processing to preserve accuracy.
template <>
void RendererImpl<IntSample>::produceLA32Output(IntSample *buffer, Bit32u len) {
	switch (synth.getDACInputMode()) {
		case DACInputMode_GENERATION2:
			while (len--) {
				*buffer = (*buffer & 0x8000) | ((*buffer << 1) & 0x7FFE) | ((*buffer >> 14) & 0x0001);
				++buffer;
			}
			break;
		case DACInputMode_NICE:
			while (len--) {
				*buffer = Synth::clipSampleEx(IntSampleEx(*buffer) << 1);
				++buffer;
			}
			break;
		default:
			break;
	}
}

template <>
void RendererImpl<IntSample>::convertSamplesToOutput(IntSample *buffer, Bit32u len) {
	if (synth.getDACInputMode() == DACInputMode_GENERATION1) {
		while (len--) {
			*buffer = IntSample((*buffer & 0x8000) | ((*buffer << 1) & 0x7FFE));
			++buffer;
		}
	}
}

static inline float produceDistortedSample(float sample) {
	// Here we roughly simulate the distortion caused by the DAC bit shift.
	if (sample < -1.0f) {
		return sample + 2.0f;
	} else if (1.0f < sample) {
		return sample - 2.0f;
	}
	return sample;
}

template <>
void RendererImpl<FloatSample>::produceLA32Output(FloatSample *buffer, Bit32u len) {
	switch (synth.getDACInputMode()) {
	case DACInputMode_NICE:
		// Note, we do not do any clamping for floats here to avoid introducing distortions.
		// This means that the output signal may actually overshoot the unity when the volume is set too high.
		// We leave it up to the consumer whether the output is to be clamped or properly normalised further on.
		while (len--) {
			*buffer *= 2.0f;
			buffer++;
		}
		break;
	case DACInputMode_GENERATION2:
		while (len--) {
			*buffer = produceDistortedSample(2.0f * *buffer);
			buffer++;
		}
		break;
	default:
		break;
	}
}

template <>
void RendererImpl<FloatSample>::convertSamplesToOutput(FloatSample *buffer, Bit32u len) {
	if (synth.getDACInputMode() == DACInputMode_GENERATION1) {
		while (len--) {
			*buffer = produceDistortedSample(2.0f * *buffer);
			buffer++;
		}
	}
}

template <class Sample>
void RendererImpl<Sample>::produceStreams(const DACOutputStreams<Sample> &streams, Bit32u len) {
	if (isActivated()) {
		// Even if LA32 output isn't desired, we proceed anyway with temp buffers
		Sample *nonReverbLeft = streams.nonReverbLeft == NULL ? tmpNonReverbLeft : streams.nonReverbLeft;
		Sample *nonReverbRight = streams.nonReverbRight == NULL ? tmpNonReverbRight : streams.nonReverbRight;
		Sample *reverbDryLeft = streams.reverbDryLeft == NULL ? tmpReverbDryLeft : streams.reverbDryLeft;
		Sample *reverbDryRight = streams.reverbDryRight == NULL ? tmpReverbDryRight : streams.reverbDryRight;

		Synth::muteSampleBuffer(nonReverbLeft, len);
		Synth::muteSampleBuffer(nonReverbRight, len);
		Synth::muteSampleBuffer(reverbDryLeft, len);
		Synth::muteSampleBuffer(reverbDryRight, len);

		for (unsigned int i = 0; i < synth.getPartialCount(); i++) {
			if (getPartialManager().shouldReverb(i)) {
				getPartialManager().produceOutput(i, reverbDryLeft, reverbDryRight, len);
			} else {
				getPartialManager().produceOutput(i, nonReverbLeft, nonReverbRight, len);
			}
		}

		produceLA32Output(reverbDryLeft, len);
		produceLA32Output(reverbDryRight, len);

		if (synth.isReverbEnabled()) {
			if (!getReverbModel().process(reverbDryLeft, reverbDryRight, streams.reverbWetLeft, streams.reverbWetRight, len)) {
				printDebug("RendererImpl: Invalid call to BReverbModel::process()!\n");
			}
			if (streams.reverbWetLeft != NULL) convertSamplesToOutput(streams.reverbWetLeft, len);
			if (streams.reverbWetRight != NULL) convertSamplesToOutput(streams.reverbWetRight, len);
		} else {
			Synth::muteSampleBuffer(streams.reverbWetLeft, len);
			Synth::muteSampleBuffer(streams.reverbWetRight, len);
		}

		// Don't bother with conversion if the output is going to be unused
		if (streams.nonReverbLeft != NULL) {
			produceLA32Output(nonReverbLeft, len);
			convertSamplesToOutput(nonReverbLeft, len);
		}
		if (streams.nonReverbRight != NULL) {
			produceLA32Output(nonReverbRight, len);
			convertSamplesToOutput(nonReverbRight, len);
		}
		if (streams.reverbDryLeft != NULL) convertSamplesToOutput(reverbDryLeft, len);
		if (streams.reverbDryRight != NULL) convertSamplesToOutput(reverbDryRight, len);
	} else {
		muteStreams(streams, len);
	}

	getPartialManager().clearAlreadyOutputed();
	incRenderedSampleCount(len);
	updateDisplayState();
}

void Synth::printPartialUsage(Bit32u sampleOffset) {
	unsigned int partialUsage[9];
	partialManager->getPerPartPartialUsage(partialUsage);
	if (sampleOffset > 0) {
		printDebug("[+%u] Partial Usage: 1:%02d 2:%02d 3:%02d 4:%02d 5:%02d 6:%02d 7:%02d 8:%02d R: %02d  TOTAL: %02d", sampleOffset, partialUsage[0], partialUsage[1], partialUsage[2], partialUsage[3], partialUsage[4], partialUsage[5], partialUsage[6], partialUsage[7], partialUsage[8], getPartialCount() - partialManager->getFreePartialCount());
	} else {
		printDebug("Partial Usage: 1:%02d 2:%02d 3:%02d 4:%02d 5:%02d 6:%02d 7:%02d 8:%02d R: %02d  TOTAL: %02d", partialUsage[0], partialUsage[1], partialUsage[2], partialUsage[3], partialUsage[4], partialUsage[5], partialUsage[6], partialUsage[7], partialUsage[8], getPartialCount() - partialManager->getFreePartialCount());
	}
}

bool Synth::hasActivePartials() const {
	if (!opened) {
		return false;
	}
	for (unsigned int partialNum = 0; partialNum < getPartialCount(); partialNum++) {
		if (partialManager->getPartial(partialNum)->isActive()) {
			return true;
		}
	}
	return false;
}

bool Synth::isActive() {
	if (!opened) {
		return false;
	}
	if (!midiQueue->isEmpty() || hasActivePartials()) {
		return true;
	}
	if (isReverbEnabled() && reverbModel->isActive()) {
		return true;
	}
	activated = false;
	return false;
}

Bit32u Synth::getPartialCount() const {
	return partialCount;
}

void Synth::getPartStates(bool *partStates) const {
	if (!opened) {
		memset(partStates, 0, 9 * sizeof(bool));
		return;
	}
	for (int partNumber = 0; partNumber < 9; partNumber++) {
		const Part *part = parts[partNumber];
		partStates[partNumber] = part->getActiveNonReleasingPartialCount() > 0;
	}
}

Bit32u Synth::getPartStates() const {
	if (!opened) return 0;
	bool partStates[9];
	getPartStates(partStates);
	Bit32u bitSet = 0;
	for (int partNumber = 8; partNumber >= 0; partNumber--) {
		bitSet = (bitSet << 1) | (partStates[partNumber] ? 1 : 0);
	}
	return bitSet;
}

void Synth::getPartialStates(PartialState *partialStates) const {
	if (!opened) {
		memset(partialStates, 0, partialCount * sizeof(PartialState));
		return;
	}
	for (unsigned int partialNum = 0; partialNum < partialCount; partialNum++) {
		partialStates[partialNum] = getPartialState(partialManager, partialNum);
	}
}

void Synth::getPartialStates(Bit8u *partialStates) const {
	if (!opened) {
		memset(partialStates, 0, ((partialCount + 3) >> 2));
		return;
	}
	for (unsigned int quartNum = 0; (4 * quartNum) < partialCount; quartNum++) {
		Bit8u packedStates = 0;
		for (unsigned int i = 0; i < 4; i++) {
			unsigned int partialNum = (4 * quartNum) + i;
			if (partialCount <= partialNum) break;
			PartialState partialState = getPartialState(partialManager, partialNum);
			packedStates |= (partialState & 3) << (2 * i);
		}
		partialStates[quartNum] = packedStates;
	}
}

Bit32u Synth::getPlayingNotes(Bit8u partNumber, Bit8u *keys, Bit8u *velocities) const {
	Bit32u playingNotes = 0;
	if (opened && (partNumber < 9)) {
		const Part *part = parts[partNumber];
		const Poly *poly = part->getFirstActivePoly();
		while (poly != NULL) {
			keys[playingNotes] = Bit8u(poly->getKey());
			velocities[playingNotes] = Bit8u(poly->getVelocity());
			playingNotes++;
			poly = poly->getNext();
		}
	}
	return playingNotes;
}

const char *Synth::getPatchName(Bit8u partNumber) const {
	return (!opened || partNumber > 8) ? NULL : parts[partNumber]->getCurrentInstr();
}

bool Synth::getSoundGroupName(char *soundGroupName, Bit8u timbreGroup, Bit8u timbreNumber) const {
	if (!opened || 63 < timbreNumber) return false;
	const char *foundGroupName = getSoundGroupName(timbreGroup, timbreNumber);
	if (foundGroupName == NULL) return false;
	memcpy(soundGroupName, foundGroupName, 7);
	soundGroupName[7] = 0;
	return true;
}

bool Synth::getSoundName(char *soundName, Bit8u timbreGroup, Bit8u timbreNumber) const {
	if (!opened || 3 < timbreGroup) return false;
	Bit8u timbresInGroup = 3 == timbreGroup ? controlROMMap->timbreRCount : 64;
	if (timbresInGroup <= timbreNumber) return false;
	TimbreParam::CommonParam &timbreCommon = mt32ram.timbres[timbreGroup * 64 + timbreNumber].timbre.common;
	if (timbreCommon.partialMute == 0) return false;
	memcpy(soundName, timbreCommon.name, sizeof timbreCommon.name);
	soundName[sizeof timbreCommon.name] = 0;
	return true;
}

const Part *Synth::getPart(Bit8u partNum) const {
	if (partNum > 8) {
		return NULL;
	}
	return parts[partNum];
}

void MemoryRegion::read(unsigned int entry, unsigned int off, Bit8u *dst, unsigned int len) const {
	off += entry * entrySize;
	// This method should never be called with out-of-bounds parameters,
	// or on an unsupported region - seeing any of this debug output indicates a bug in the emulator
	if (off > entrySize * entries - 1) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: parameters start out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	if (off + len > entrySize * entries) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: parameters end out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		len = entrySize * entries - off;
	}
	Bit8u *src = getRealMemory();
	if (src == NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: unreadable region: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	memcpy(dst, src + off, len);
}

void MemoryRegion::write(unsigned int entry, unsigned int off, const Bit8u *src, unsigned int len, bool init) const {
	unsigned int memOff = entry * entrySize + off;
	// This method should never be called with out-of-bounds parameters,
	// or on an unsupported region - seeing any of this debug output indicates a bug in the emulator
	if (off > entrySize * entries - 1) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: parameters start out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	if (off + len > entrySize * entries) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: parameters end out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		len = entrySize * entries - off;
	}
	Bit8u *dest = getRealMemory();
	if (dest == NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: unwritable region: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}

	for (unsigned int i = 0; i < len; i++) {
		Bit8u desiredValue = src[i];
		Bit8u maxValue = getMaxValue(memOff);
		// maxValue == 0 means write-protected unless called from initialisation code, in which case it really means the maximum value is 0.
		if (maxValue != 0 || init) {
			if (desiredValue > maxValue) {
#if MT32EMU_MONITOR_SYSEX > 0
				synth->printDebug("write[%d]: Wanted 0x%02x at %d, but max 0x%02x", type, desiredValue, memOff, maxValue);
#endif
				desiredValue = maxValue;
			}
			dest[memOff] = desiredValue;
		} else if (desiredValue != 0) {
#if MT32EMU_MONITOR_SYSEX > 0
			// Only output debug info if they wanted to write non-zero, since a lot of things cause this to spit out a lot of debug info otherwise.
			synth->printDebug("write[%d]: Wanted 0x%02x at %d, but write-protected", type, desiredValue, memOff);
#endif
		}
		memOff++;
	}
}

} // namespace MT32Emu
#undef produceDistortedSample

/************************************************** ROMInfo.cpp ******************************************************/
namespace MT32Emu {

namespace {

struct ROMInfoList {
	const ROMInfo * const *romInfos;
	const Bit32u itemCount;
};

struct ROMInfoLists {
	//DBP: Removed unused partial rom
	ROMInfoList allROMInfos;
};

}

#define _CALC_ARRAY_LENGTH(x) Bit32u(sizeof (x) / sizeof *(x) - 1)

static const ROMInfoLists &getROMInfoLists() {
	//DBP: Removed unused partial rom
	static const File::SHA1Digest CTRL_MT32_V1_04_SHA1 = "5a5cb5a77d7d55ee69657c2f870416daed52dea7";
	static const File::SHA1Digest CTRL_MT32_V1_05_SHA1 = "e17a3a6d265bf1fa150312061134293d2b58288c";
	static const File::SHA1Digest CTRL_MT32_V1_06_SHA1 = "a553481f4e2794c10cfe597fef154eef0d8257de";
	static const File::SHA1Digest CTRL_MT32_V1_07_SHA1 = "b083518fffb7f66b03c23b7eb4f868e62dc5a987";
	static const File::SHA1Digest CTRL_MT32_BLUER_SHA1 = "7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92";

	static const File::SHA1Digest CTRL_MT32_V2_03_SHA1 = "5837064c9df4741a55f7c4d8787ac158dff2d3ce";
	static const File::SHA1Digest CTRL_MT32_V2_04_SHA1 = "2c16432b6c73dd2a3947cba950a0f4c19d6180eb";
	static const File::SHA1Digest CTRL_MT32_V2_06_SHA1 = "2869cf4c235d671668cfcb62415e2ce8323ad4ed";
	static const File::SHA1Digest CTRL_MT32_V2_07_SHA1 = "47b52adefedaec475c925e54340e37673c11707c";
	static const File::SHA1Digest CTRL_CM32L_V1_00_SHA1 = "73683d585cd6948cc19547942ca0e14a0319456d";
	static const File::SHA1Digest CTRL_CM32L_V1_02_SHA1 = "a439fbb390da38cada95a7cbb1d6ca199cd66ef8";
	static const File::SHA1Digest CTRL_CM32LN_V1_00_SHA1 = "dc1c5b1b90a4646d00f7daf3679733c7badc7077";

	static const File::SHA1Digest PCM_MT32_SHA1 = "f6b1eebc4b2d200ec6d3d21d51325d5b48c60252";
	static const File::SHA1Digest PCM_CM32L_SHA1 = "289cc298ad532b702461bfc738009d9ebe8025ea";

	static const ROMInfo CTRL_MT32_V1_04 = {65536, CTRL_MT32_V1_04_SHA1, ROMInfo::Control, "ctrl_mt32_1_04", "MT-32 Control v1.04", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_V1_05 = {65536, CTRL_MT32_V1_05_SHA1, ROMInfo::Control, "ctrl_mt32_1_05", "MT-32 Control v1.05", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_V1_06 = {65536, CTRL_MT32_V1_06_SHA1, ROMInfo::Control, "ctrl_mt32_1_06", "MT-32 Control v1.06", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_V1_07 = {65536, CTRL_MT32_V1_07_SHA1, ROMInfo::Control, "ctrl_mt32_1_07", "MT-32 Control v1.07", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_BLUER = {65536, CTRL_MT32_BLUER_SHA1, ROMInfo::Control, "ctrl_mt32_bluer", "MT-32 Control BlueRidge", ROMInfo::Full};

	static const ROMInfo CTRL_MT32_V2_03 = {131072, CTRL_MT32_V2_03_SHA1, ROMInfo::Control, "ctrl_mt32_2_03", "MT-32 Control v2.03", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_V2_04 = {131072, CTRL_MT32_V2_04_SHA1, ROMInfo::Control, "ctrl_mt32_2_04", "MT-32 Control v2.04", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_V2_06 = {131072, CTRL_MT32_V2_06_SHA1, ROMInfo::Control, "ctrl_mt32_2_06", "MT-32 Control v2.06", ROMInfo::Full};
	static const ROMInfo CTRL_MT32_V2_07 = {131072, CTRL_MT32_V2_07_SHA1, ROMInfo::Control, "ctrl_mt32_2_07", "MT-32 Control v2.07", ROMInfo::Full};
	static const ROMInfo CTRL_CM32L_V1_00 = {65536, CTRL_CM32L_V1_00_SHA1, ROMInfo::Control, "ctrl_cm32l_1_00", "CM-32L/LAPC-I Control v1.00", ROMInfo::Full};
	static const ROMInfo CTRL_CM32L_V1_02 = {65536, CTRL_CM32L_V1_02_SHA1, ROMInfo::Control, "ctrl_cm32l_1_02", "CM-32L/LAPC-I Control v1.02", ROMInfo::Full};
	static const ROMInfo CTRL_CM32LN_V1_00 = {65536, CTRL_CM32LN_V1_00_SHA1, ROMInfo::Control, "ctrl_cm32ln_1_00", "CM-32LN/CM-500/LAPC-N Control v1.00", ROMInfo::Full};

	static const ROMInfo PCM_MT32 = {524288, PCM_MT32_SHA1, ROMInfo::PCM, "pcm_mt32", "MT-32 PCM ROM", ROMInfo::Full};
	static const ROMInfo PCM_CM32L = {1048576, PCM_CM32L_SHA1, ROMInfo::PCM, "pcm_cm32l", "CM-32L/CM-64/LAPC-I PCM ROM", ROMInfo::Full};

	static const ROMInfo * const FULL_ROM_INFOS[] = {
		&CTRL_MT32_V1_04,
		&CTRL_MT32_V1_05,
		&CTRL_MT32_V1_06,
		&CTRL_MT32_V1_07,
		&CTRL_MT32_BLUER,
		&CTRL_MT32_V2_03,
		&CTRL_MT32_V2_04,
		&CTRL_MT32_V2_06,
		&CTRL_MT32_V2_07,
		&CTRL_CM32L_V1_00,
		&CTRL_CM32L_V1_02,
		&CTRL_CM32LN_V1_00,
		&PCM_MT32,
		&PCM_CM32L,
		NULL
	};
	static const ROMInfoLists romInfoLists = {
		{FULL_ROM_INFOS, _CALC_ARRAY_LENGTH(FULL_ROM_INFOS)}
	};
	return romInfoLists;
}

static const ROMInfo * const *getKnownROMInfoList() {
	return getROMInfoLists().allROMInfos.romInfos;
}

static const ROMInfo *getKnownROMInfoFromList(Bit32u index) {
	return getKnownROMInfoList()[index];
}

const ROMInfo* ROMInfo::getROMInfo(File *file) {
	return getROMInfo(file, getKnownROMInfoList());
}

const ROMInfo *ROMInfo::getROMInfo(File *file, const ROMInfo * const *romInfos) {
	size_t fileSize = file->getSize();
	for (Bit32u i = 0; romInfos[i] != NULL; i++) {
		const ROMInfo *romInfo = romInfos[i];
		if (fileSize == romInfo->fileSize && !strcmp(file->getSHA1(), romInfo->sha1Digest)) {
			return romInfo;
		}
	}
	return NULL;
}

void ROMInfo::freeROMInfo(const ROMInfo *romInfo) {
	(void) romInfo;
}

const ROMInfo** ROMInfo::getROMInfoList(Bit32u types, Bit32u pairTypes) {
	Bit32u romCount = getROMInfoLists().allROMInfos.itemCount; // Excludes the NULL terminator.
	const ROMInfo **romInfoList = new const ROMInfo*[romCount + 1];
	const ROMInfo **currentROMInList = romInfoList;
	for (Bit32u i = 0; i < romCount; i++) {
		const ROMInfo *romInfo = getKnownROMInfoFromList(i);
		if ((types & (1 << romInfo->type)) && (pairTypes & (1 << romInfo->pairType))) {
			*currentROMInList++ = romInfo;
		}
	}
	*currentROMInList = NULL;
	return romInfoList;
}

void ROMInfo::freeROMInfoList(const ROMInfo **romInfoList) {
	delete[] romInfoList;
}

ROMImage::ROMImage(File *useFile, bool useOwnFile, const ROMInfo * const *romInfos) :
	file(useFile), ownFile(useOwnFile), romInfo(ROMInfo::getROMInfo(file, romInfos))
{}

ROMImage::~ROMImage() {
	ROMInfo::freeROMInfo(romInfo);
	if (ownFile) {
		const Bit8u *data = file->getData();
		delete file;
		delete[] data;
	}
}

const ROMImage* ROMImage::makeROMImage(File *file) {
	return new ROMImage(file, false, getKnownROMInfoList());
}

const ROMImage *ROMImage::makeROMImage(File *file, const ROMInfo * const *romInfos) {
	return new ROMImage(file, false, romInfos);
}

void ROMImage::freeROMImage(const ROMImage *romImage) {
	delete romImage;
}

File* ROMImage::getFile() const {
	return file;
}

bool ROMImage::isFileUserProvided() const {
	return !ownFile;
}

const ROMInfo* ROMImage::getROMInfo() const {
	return romInfo;
}

} // namespace MT32Emu

/************************************************** Analog.cpp ******************************************************/
namespace MT32Emu {

/* FIR approximation of the overall impulse response of the cascade composed of the sample & hold circuit and the low pass filter
 * of the MT-32 first generation.
 * The coefficients below are found by windowing the inverse DFT of the 1024 pin frequency response converted to the minimum phase.
 * The frequency response of the LPF is computed directly, the effect of the S&H is approximated by multiplying the LPF frequency
 * response by the corresponding sinc. Although, the LPF has DC gain of 3.2, we ignore this in the emulation and use normalised model.
 * The peak gain of the normalised cascade appears about 1.7 near 11.8 kHz. Relative error doesn't exceed 1% for the frequencies
 * below 12.5 kHz. In the higher frequency range, the relative error is below 8%. Peak error value is at 16 kHz.
 */
static const FloatSample COARSE_LPF_FLOAT_TAPS_MT32[] = {
	1.272473681f, -0.220267785f, -0.158039905f, 0.179603785f, -0.111484097f, 0.054137498f, -0.023518029f, 0.010997169f, -0.006935698f
};

// Similar approximation for new MT-32 and CM-32L/LAPC-I LPF. As the voltage controlled amplifier was introduced, LPF has unity DC gain.
// The peak gain value shifted towards higher frequencies and a bit higher about 1.83 near 13 kHz.
static const FloatSample COARSE_LPF_FLOAT_TAPS_CM32L[] = {
	1.340615635f, -0.403331694f, 0.036005517f, 0.066156844f, -0.069672532f, 0.049563806f, -0.031113416f, 0.019169774f, -0.012421368f
};

static const unsigned int COARSE_LPF_INT_FRACTION_BITS = 14;

// Integer versions of the FIRs above multiplied by (1 << 14) and rounded.
static const IntSampleEx COARSE_LPF_INT_TAPS_MT32[] = {
	20848, -3609, -2589, 2943, -1827, 887, -385, 180, -114
};

static const IntSampleEx COARSE_LPF_INT_TAPS_CM32L[] = {
	21965, -6608, 590, 1084, -1142, 812, -510, 314, -204
};

/* Combined FIR that both approximates the impulse response of the analogue circuits of sample & hold and the low pass filter
 * in the audible frequency range (below 20 kHz) and attenuates unwanted mirror spectra above 28 kHz as well. It is a polyphase
 * filter intended for resampling the signal to 48 kHz yet for applying high frequency boost.
 * As with the filter above, the analogue LPF frequency response is obtained for 1536 pin grid for range up to 96 kHz and multiplied
 * by the corresponding sinc. The result is further squared, windowed and passed to generalised Parks-McClellan routine as a desired response.
 * Finally, the minimum phase factor is found that's essentially the coefficients below.
 * Relative error in the audible frequency range doesn't exceed 0.0006%, attenuation in the stopband is better than 100 dB.
 * This level of performance makes it nearly bit-accurate for standard 16-bit sample resolution.
 */

// FIR version for MT-32 first generation.
static const FloatSample ACCURATE_LPF_TAPS_MT32[] = {
	0.003429281f, 0.025929869f, 0.096587777f, 0.228884848f, 0.372413431f, 0.412386503f, 0.263980018f,
	-0.014504962f, -0.237394528f, -0.257043496f, -0.103436603f, 0.063996095f, 0.124562333f, 0.083703206f,
	0.013921662f, -0.033475018f, -0.046239712f, -0.029310921f, 0.00126585f, 0.021060961f, 0.017925605f,
	0.003559874f, -0.005105248f, -0.005647917f, -0.004157918f, -0.002065664f, 0.00158747f, 0.003762585f,
	0.001867137f, -0.001090028f, -0.001433979f, -0.00022367f, 4.34308E-05f, -0.000247827f, 0.000157087f,
	0.000605823f, 0.000197317f, -0.000370511f, -0.000261202f, 9.96069E-05f, 9.85073E-05f, -5.28754E-05f,
	-1.00912E-05f, 7.69943E-05f, 2.03162E-05f, -5.67967E-05f, -3.30637E-05f, 1.61958E-05f, 1.73041E-05f
};

// FIR version for new MT-32 and CM-32L/LAPC-I.
static const FloatSample ACCURATE_LPF_TAPS_CM32L[] = {
	0.003917452f, 0.030693861f, 0.116424199f, 0.275101674f, 0.43217361f, 0.431247894f, 0.183255659f,
	-0.174955671f, -0.354240244f, -0.212401714f, 0.072259178f, 0.204655344f, 0.108336211f, -0.039099027f,
	-0.075138174f, -0.026261906f, 0.00582663f, 0.003052193f, 0.00613657f, 0.017017951f, 0.008732535f,
	-0.011027427f, -0.012933664f, 0.001158097f, 0.006765958f, 0.00046778f, -0.002191106f, 0.001561017f,
	0.001842871f, -0.001996876f, -0.002315836f, 0.000980965f, 0.001817454f, -0.000243272f, -0.000972848f,
	0.000149941f, 0.000498886f, -0.000204436f, -0.000347415f, 0.000142386f, 0.000249137f, -4.32946E-05f,
	-0.000131231f, 3.88575E-07f, 4.48813E-05f, -1.31906E-06f, -1.03499E-05f, 7.71971E-06f, 2.86721E-06f
};

// According to the CM-64 PCB schematic, there is a difference in the values of the LPF entrance resistors for the reverb and non-reverb channels.
// This effectively results in non-unity LPF DC gain for the reverb channel of 0.68 while the LPF has unity DC gain for the LA32 output channels.
// In emulation, the reverb output gain is multiplied by this factor to compensate for the LPF gain difference.
static const float CM32L_REVERB_TO_LA32_ANALOG_OUTPUT_GAIN_FACTOR = 0.68f;

static const unsigned int OUTPUT_GAIN_FRACTION_BITS = 8;
static const float OUTPUT_GAIN_MULTIPLIER = float(1 << OUTPUT_GAIN_FRACTION_BITS);

static const unsigned int COARSE_LPF_DELAY_LINE_LENGTH = 8; // Must be a power of 2
static const unsigned int ACCURATE_LPF_DELAY_LINE_LENGTH = 16; // Must be a power of 2
static const unsigned int ACCURATE_LPF_NUMBER_OF_PHASES = 3; // Upsampling factor
static const unsigned int ACCURATE_LPF_PHASE_INCREMENT_REGULAR = 2; // Downsampling factor
static const unsigned int ACCURATE_LPF_PHASE_INCREMENT_OVERSAMPLED = 1; // No downsampling
static const Bit32u ACCURATE_LPF_DELTAS_REGULAR[][ACCURATE_LPF_NUMBER_OF_PHASES] = { { 0, 0, 0 }, { 1, 1, 0 }, { 1, 2, 1 } };
static const Bit32u ACCURATE_LPF_DELTAS_OVERSAMPLED[][ACCURATE_LPF_NUMBER_OF_PHASES] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } };

template <class SampleEx>
class AbstractLowPassFilter {
public:
	static AbstractLowPassFilter<SampleEx> &createLowPassFilter(const AnalogOutputMode mode, const bool oldMT32AnalogLPF);

	virtual ~AbstractLowPassFilter() {}
	virtual SampleEx process(const SampleEx sample) = 0;

	virtual bool hasNextSample() const {
		return false;
	}

	virtual unsigned int getOutputSampleRate() const {
		return SAMPLE_RATE;
	}

	virtual unsigned int estimateInSampleCount(const unsigned int outSamples) const {
		return outSamples;
	}

	virtual void addPositionIncrement(const unsigned int) {}
};

template <class SampleEx>
class NullLowPassFilter : public AbstractLowPassFilter<SampleEx> {
public:
	SampleEx process(const SampleEx sample) {
		return sample;
	}
};

template <class SampleEx>
class CoarseLowPassFilter : public AbstractLowPassFilter<SampleEx> {
private:
	const SampleEx * const lpfTaps;
	SampleEx ringBuffer[COARSE_LPF_DELAY_LINE_LENGTH];
	unsigned int ringBufferPosition;

public:
	static inline const SampleEx *getLPFTaps(const bool oldMT32AnalogLPF);
	static inline SampleEx normaliseSample(const SampleEx sample);

	explicit CoarseLowPassFilter(const bool oldMT32AnalogLPF) :
		lpfTaps(getLPFTaps(oldMT32AnalogLPF)),
		ringBufferPosition(0)
	{
		Synth::muteSampleBuffer(ringBuffer, COARSE_LPF_DELAY_LINE_LENGTH);
	}

	SampleEx process(const SampleEx inSample) {
		static const unsigned int DELAY_LINE_MASK = COARSE_LPF_DELAY_LINE_LENGTH - 1;

		SampleEx sample = lpfTaps[COARSE_LPF_DELAY_LINE_LENGTH] * ringBuffer[ringBufferPosition];
		ringBuffer[ringBufferPosition] = Synth::clipSampleEx(inSample);

		for (unsigned int i = 0; i < COARSE_LPF_DELAY_LINE_LENGTH; i++) {
			sample += lpfTaps[i] * ringBuffer[(i + ringBufferPosition) & DELAY_LINE_MASK];
		}

		ringBufferPosition = (ringBufferPosition - 1) & DELAY_LINE_MASK;

		return normaliseSample(sample);
	}
};

class AccurateLowPassFilter : public AbstractLowPassFilter<IntSampleEx>, public AbstractLowPassFilter<FloatSample> {
private:
	const FloatSample * const LPF_TAPS;
	const Bit32u (* const deltas)[ACCURATE_LPF_NUMBER_OF_PHASES];
	const unsigned int phaseIncrement;
	const unsigned int outputSampleRate;

	FloatSample ringBuffer[ACCURATE_LPF_DELAY_LINE_LENGTH];
	unsigned int ringBufferPosition;
	unsigned int phase;

public:
	AccurateLowPassFilter(const bool oldMT32AnalogLPF, const bool oversample);
	FloatSample process(const FloatSample sample);
	IntSampleEx process(const IntSampleEx sample);
	bool hasNextSample() const;
	unsigned int getOutputSampleRate() const;
	unsigned int estimateInSampleCount(const unsigned int outSamples) const;
	void addPositionIncrement(const unsigned int positionIncrement);
};

static inline IntSampleEx normaliseSample(const IntSampleEx sample) {
	return sample >> OUTPUT_GAIN_FRACTION_BITS;
}

static inline FloatSample normaliseSample(const FloatSample sample) {
	return sample;
}

static inline float getActualReverbOutputGain(const float reverbGain, const bool mt32ReverbCompatibilityMode) {
	return mt32ReverbCompatibilityMode ? reverbGain : reverbGain * CM32L_REVERB_TO_LA32_ANALOG_OUTPUT_GAIN_FACTOR;
}

static inline IntSampleEx getIntOutputGain(const float outputGain) {
	return IntSampleEx(((OUTPUT_GAIN_MULTIPLIER < outputGain) ? OUTPUT_GAIN_MULTIPLIER : outputGain) * OUTPUT_GAIN_MULTIPLIER);
}

template <class SampleEx>
class AnalogImpl : public Analog {
public:
	AbstractLowPassFilter<SampleEx> &leftChannelLPF;
	AbstractLowPassFilter<SampleEx> &rightChannelLPF;
	SampleEx synthGain;
	SampleEx reverbGain;

	AnalogImpl(const AnalogOutputMode mode, const bool oldMT32AnalogLPF) :
		leftChannelLPF(AbstractLowPassFilter<SampleEx>::createLowPassFilter(mode, oldMT32AnalogLPF)),
		rightChannelLPF(AbstractLowPassFilter<SampleEx>::createLowPassFilter(mode, oldMT32AnalogLPF)),
		synthGain(0),
		reverbGain(0)
	{}

	~AnalogImpl() {
		delete &leftChannelLPF;
		delete &rightChannelLPF;
	}

	unsigned int getOutputSampleRate() const {
		return leftChannelLPF.getOutputSampleRate();
	}

	Bit32u getDACStreamsLength(const Bit32u outputLength) const {
		return leftChannelLPF.estimateInSampleCount(outputLength);
	}

	void setSynthOutputGain(const float synthGain);
	void setReverbOutputGain(const float reverbGain, const bool mt32ReverbCompatibilityMode);

	bool process(IntSample *outStream, const IntSample *nonReverbLeft, const IntSample *nonReverbRight, const IntSample *reverbDryLeft, const IntSample *reverbDryRight, const IntSample *reverbWetLeft, const IntSample *reverbWetRight, Bit32u outLength);
	bool process(FloatSample *outStream, const FloatSample *nonReverbLeft, const FloatSample *nonReverbRight, const FloatSample *reverbDryLeft, const FloatSample *reverbDryRight, const FloatSample *reverbWetLeft, const FloatSample *reverbWetRight, Bit32u outLength);

	template <class Sample>
	void produceOutput(Sample *outStream, const Sample *nonReverbLeft, const Sample *nonReverbRight, const Sample *reverbDryLeft, const Sample *reverbDryRight, const Sample *reverbWetLeft, const Sample *reverbWetRight, Bit32u outLength) {
		if (outStream == NULL) {
			leftChannelLPF.addPositionIncrement(outLength);
			rightChannelLPF.addPositionIncrement(outLength);
			return;
		}

		while (0 < (outLength--)) {
			SampleEx outSampleL;
			SampleEx outSampleR;

			if (leftChannelLPF.hasNextSample()) {
				outSampleL = leftChannelLPF.process(0);
				outSampleR = rightChannelLPF.process(0);
			} else {
				SampleEx inSampleL = (SampleEx(*(nonReverbLeft++)) + SampleEx(*(reverbDryLeft++))) * synthGain + SampleEx(*(reverbWetLeft++)) * reverbGain;
				SampleEx inSampleR = (SampleEx(*(nonReverbRight++)) + SampleEx(*(reverbDryRight++))) * synthGain + SampleEx(*(reverbWetRight++)) * reverbGain;

				outSampleL = leftChannelLPF.process(normaliseSample(inSampleL));
				outSampleR = rightChannelLPF.process(normaliseSample(inSampleR));
			}

			*(outStream++) = Synth::clipSampleEx(outSampleL);
			*(outStream++) = Synth::clipSampleEx(outSampleR);
		}
	}
};

Analog *Analog::createAnalog(const AnalogOutputMode mode, const bool oldMT32AnalogLPF, const RendererType rendererType) {
	switch (rendererType)
	{
	case RendererType_BIT16S:
		return new AnalogImpl<IntSampleEx>(mode, oldMT32AnalogLPF);
	case RendererType_FLOAT:
		return new AnalogImpl<FloatSample>(mode, oldMT32AnalogLPF);
	default:
		break;
	}
	return NULL;
}

template<>
bool AnalogImpl<IntSampleEx>::process(IntSample *outStream, const IntSample *nonReverbLeft, const IntSample *nonReverbRight, const IntSample *reverbDryLeft, const IntSample *reverbDryRight, const IntSample *reverbWetLeft, const IntSample *reverbWetRight, Bit32u outLength) {
	produceOutput(outStream, nonReverbLeft, nonReverbRight, reverbDryLeft, reverbDryRight, reverbWetLeft, reverbWetRight, outLength);
	return true;
}

template<>
bool AnalogImpl<FloatSample>::process(IntSample *, const IntSample *, const IntSample *, const IntSample *, const IntSample *, const IntSample *, const IntSample *, Bit32u) {
	return false;
}

template<>
bool AnalogImpl<IntSampleEx>::process(FloatSample *, const FloatSample *, const FloatSample *, const FloatSample *, const FloatSample *, const FloatSample *, const FloatSample *, Bit32u) {
	return false;
}

template<>
bool AnalogImpl<FloatSample>::process(FloatSample *outStream, const FloatSample *nonReverbLeft, const FloatSample *nonReverbRight, const FloatSample *reverbDryLeft, const FloatSample *reverbDryRight, const FloatSample *reverbWetLeft, const FloatSample *reverbWetRight, Bit32u outLength) {
	produceOutput(outStream, nonReverbLeft, nonReverbRight, reverbDryLeft, reverbDryRight, reverbWetLeft, reverbWetRight, outLength);
	return true;
}

template<>
void AnalogImpl<IntSampleEx>::setSynthOutputGain(const float useSynthGain) {
	synthGain = getIntOutputGain(useSynthGain);
}

template<>
void AnalogImpl<IntSampleEx>::setReverbOutputGain(const float useReverbGain, const bool mt32ReverbCompatibilityMode) {
	reverbGain = getIntOutputGain(getActualReverbOutputGain(useReverbGain, mt32ReverbCompatibilityMode));
}

template<>
void AnalogImpl<FloatSample>::setSynthOutputGain(const float useSynthGain) {
	synthGain = useSynthGain;
}

template<>
void AnalogImpl<FloatSample>::setReverbOutputGain(const float useReverbGain, const bool mt32ReverbCompatibilityMode) {
	reverbGain = getActualReverbOutputGain(useReverbGain, mt32ReverbCompatibilityMode);
}

template<>
AbstractLowPassFilter<IntSampleEx> &AbstractLowPassFilter<IntSampleEx>::createLowPassFilter(AnalogOutputMode mode, bool oldMT32AnalogLPF) {
	switch (mode) {
	case AnalogOutputMode_COARSE:
		return *new CoarseLowPassFilter<IntSampleEx>(oldMT32AnalogLPF);
	case AnalogOutputMode_ACCURATE:
		return *new AccurateLowPassFilter(oldMT32AnalogLPF, false);
	case AnalogOutputMode_OVERSAMPLED:
		return *new AccurateLowPassFilter(oldMT32AnalogLPF, true);
	default:
		return *new NullLowPassFilter<IntSampleEx>;
	}
}

template<>
AbstractLowPassFilter<FloatSample> &AbstractLowPassFilter<FloatSample>::createLowPassFilter(AnalogOutputMode mode, bool oldMT32AnalogLPF) {
	switch (mode) {
		case AnalogOutputMode_COARSE:
			return *new CoarseLowPassFilter<FloatSample>(oldMT32AnalogLPF);
		case AnalogOutputMode_ACCURATE:
			return *new AccurateLowPassFilter(oldMT32AnalogLPF, false);
		case AnalogOutputMode_OVERSAMPLED:
			return *new AccurateLowPassFilter(oldMT32AnalogLPF, true);
		default:
			return *new NullLowPassFilter<FloatSample>;
	}
}

template<>
const IntSampleEx *CoarseLowPassFilter<IntSampleEx>::getLPFTaps(const bool oldMT32AnalogLPF) {
	return oldMT32AnalogLPF ? COARSE_LPF_INT_TAPS_MT32 : COARSE_LPF_INT_TAPS_CM32L;
}

template<>
const FloatSample *CoarseLowPassFilter<FloatSample>::getLPFTaps(const bool oldMT32AnalogLPF) {
	return oldMT32AnalogLPF ? COARSE_LPF_FLOAT_TAPS_MT32 : COARSE_LPF_FLOAT_TAPS_CM32L;
}

template<>
IntSampleEx CoarseLowPassFilter<IntSampleEx>::normaliseSample(const IntSampleEx sample) {
	return sample >> COARSE_LPF_INT_FRACTION_BITS;
}

template<>
FloatSample CoarseLowPassFilter<FloatSample>::normaliseSample(const FloatSample sample) {
	return sample;
}

AccurateLowPassFilter::AccurateLowPassFilter(const bool oldMT32AnalogLPF, const bool oversample) :
	LPF_TAPS(oldMT32AnalogLPF ? ACCURATE_LPF_TAPS_MT32 : ACCURATE_LPF_TAPS_CM32L),
	deltas(oversample ? ACCURATE_LPF_DELTAS_OVERSAMPLED : ACCURATE_LPF_DELTAS_REGULAR),
	phaseIncrement(oversample ? ACCURATE_LPF_PHASE_INCREMENT_OVERSAMPLED : ACCURATE_LPF_PHASE_INCREMENT_REGULAR),
	outputSampleRate(SAMPLE_RATE * ACCURATE_LPF_NUMBER_OF_PHASES / phaseIncrement),
	ringBufferPosition(0),
	phase(0)
{
	Synth::muteSampleBuffer(ringBuffer, ACCURATE_LPF_DELAY_LINE_LENGTH);
}

FloatSample AccurateLowPassFilter::process(const FloatSample inSample) {
	static const unsigned int DELAY_LINE_MASK = ACCURATE_LPF_DELAY_LINE_LENGTH - 1;

	FloatSample sample = (phase == 0) ? LPF_TAPS[ACCURATE_LPF_DELAY_LINE_LENGTH * ACCURATE_LPF_NUMBER_OF_PHASES] * ringBuffer[ringBufferPosition] : 0.0f;
	if (!hasNextSample()) {
		ringBuffer[ringBufferPosition] = inSample;
	}

	for (unsigned int tapIx = phase, delaySampleIx = 0; delaySampleIx < ACCURATE_LPF_DELAY_LINE_LENGTH; delaySampleIx++, tapIx += ACCURATE_LPF_NUMBER_OF_PHASES) {
		sample += LPF_TAPS[tapIx] * ringBuffer[(delaySampleIx + ringBufferPosition) & DELAY_LINE_MASK];
	}

	phase += phaseIncrement;
	if (ACCURATE_LPF_NUMBER_OF_PHASES <= phase) {
		phase -= ACCURATE_LPF_NUMBER_OF_PHASES;
		ringBufferPosition = (ringBufferPosition - 1) & DELAY_LINE_MASK;
	}

	return ACCURATE_LPF_NUMBER_OF_PHASES * sample;
}

IntSampleEx AccurateLowPassFilter::process(const IntSampleEx sample) {
	return IntSampleEx(process(FloatSample(sample)));
}

bool AccurateLowPassFilter::hasNextSample() const {
	return phaseIncrement <= phase;
}

unsigned int AccurateLowPassFilter::getOutputSampleRate() const {
	return outputSampleRate;
}

unsigned int AccurateLowPassFilter::estimateInSampleCount(const unsigned int outSamples) const {
	Bit32u cycleCount = outSamples / ACCURATE_LPF_NUMBER_OF_PHASES;
	Bit32u remainder = outSamples - cycleCount * ACCURATE_LPF_NUMBER_OF_PHASES;
	return cycleCount * phaseIncrement + deltas[remainder][phase];
}

void AccurateLowPassFilter::addPositionIncrement(const unsigned int positionIncrement) {
	phase = (phase + positionIncrement * phaseIncrement) % ACCURATE_LPF_NUMBER_OF_PHASES;
}

} // namespace MT32Emu

/************************************************** BReverbModel.cpp ******************************************************/
// Analysing of state of reverb RAM address lines gives exact sizes of the buffers of filters used. This also indicates that
// the reverb model implemented in the real devices consists of three series allpass filters preceded by a non-feedback comb (or a delay with a LPF)
// and followed by three parallel comb filters

namespace MT32Emu {

// Because LA-32 chip makes it's output available to process by the Boss chip with a significant delay,
// the Boss chip puts to the buffer the LA32 dry output when it is ready and performs processing of the _previously_ latched data.
// Of course, the right way would be to use a dedicated variable for this, but our reverb model is way higher level,
// so we can simply increase the input buffer size.
static const Bit32u PROCESS_DELAY = 1;

static const Bit32u MODE_3_ADDITIONAL_DELAY = 1;
static const Bit32u MODE_3_FEEDBACK_DELAY = 1;

// Avoid denormals degrading performance, using biased input
static const FloatSample BIAS = 1e-20f;

struct BReverbSettings {
	const Bit32u numberOfAllpasses;
	const Bit32u * const allpassSizes;
	const Bit32u numberOfCombs;
	const Bit32u * const combSizes;
	const Bit32u * const outLPositions;
	const Bit32u * const outRPositions;
	const Bit8u * const filterFactors;
	const Bit8u * const feedbackFactors;
	const Bit8u * const dryAmps;
	const Bit8u * const wetLevels;
	const Bit8u lpfAmp;
};

// Default reverb settings for "new" reverb model implemented in CM-32L / LAPC-I.
// Found by tracing reverb RAM data lines (thanks go to Lord_Nightmare & balrog).
static const BReverbSettings &getCM32L_LAPCSettings(const ReverbMode mode) {
	static const Bit32u MODE_0_NUMBER_OF_ALLPASSES = 3;
	static const Bit32u MODE_0_ALLPASSES[] = {994, 729, 78};
	static const Bit32u MODE_0_NUMBER_OF_COMBS = 4; // Well, actually there are 3 comb filters, but the entrance LPF + delay can be processed via a hacked comb.
	static const Bit32u MODE_0_COMBS[] = {705 + PROCESS_DELAY, 2349, 2839, 3632};
	static const Bit32u MODE_0_OUTL[] = {2349, 141, 1960};
	static const Bit32u MODE_0_OUTR[] = {1174, 1570, 145};
	static const Bit8u  MODE_0_COMB_FACTOR[] = {0xA0, 0x60, 0x60, 0x60};
	static const Bit8u  MODE_0_COMB_FEEDBACK[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98};
	static const Bit8u  MODE_0_DRY_AMP[] = {0xA0, 0xA0, 0xA0, 0xA0, 0xB0, 0xB0, 0xB0, 0xD0};
	static const Bit8u  MODE_0_WET_AMP[] = {0x10, 0x30, 0x50, 0x70, 0x90, 0xC0, 0xF0, 0xF0};
	static const Bit8u  MODE_0_LPF_AMP = 0x60;

	static const Bit32u MODE_1_NUMBER_OF_ALLPASSES = 3;
	static const Bit32u MODE_1_ALLPASSES[] = {1324, 809, 176};
	static const Bit32u MODE_1_NUMBER_OF_COMBS = 4; // Same as for mode 0 above
	static const Bit32u MODE_1_COMBS[] = {961 + PROCESS_DELAY, 2619, 3545, 4519};
	static const Bit32u MODE_1_OUTL[] = {2618, 1760, 4518};
	static const Bit32u MODE_1_OUTR[] = {1300, 3532, 2274};
	static const Bit8u  MODE_1_COMB_FACTOR[] = {0x80, 0x60, 0x60, 0x60};
	static const Bit8u  MODE_1_COMB_FEEDBACK[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                              0x28, 0x48, 0x60, 0x70, 0x78, 0x80, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98};
	static const Bit8u  MODE_1_DRY_AMP[] = {0xA0, 0xA0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xE0};
	static const Bit8u  MODE_1_WET_AMP[] = {0x10, 0x30, 0x50, 0x70, 0x90, 0xC0, 0xF0, 0xF0};
	static const Bit8u  MODE_1_LPF_AMP = 0x60;

	static const Bit32u MODE_2_NUMBER_OF_ALLPASSES = 3;
	static const Bit32u MODE_2_ALLPASSES[] = {969, 644, 157};
	static const Bit32u MODE_2_NUMBER_OF_COMBS = 4; // Same as for mode 0 above
	static const Bit32u MODE_2_COMBS[] = {116 + PROCESS_DELAY, 2259, 2839, 3539};
	static const Bit32u MODE_2_OUTL[] = {2259, 718, 1769};
	static const Bit32u MODE_2_OUTR[] = {1136, 2128, 1};
	static const Bit8u  MODE_2_COMB_FACTOR[] = {0, 0x20, 0x20, 0x20};
	static const Bit8u  MODE_2_COMB_FEEDBACK[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                              0x30, 0x58, 0x78, 0x88, 0xA0, 0xB8, 0xC0, 0xD0,
	                                              0x30, 0x58, 0x78, 0x88, 0xA0, 0xB8, 0xC0, 0xD0,
	                                              0x30, 0x58, 0x78, 0x88, 0xA0, 0xB8, 0xC0, 0xD0};
	static const Bit8u  MODE_2_DRY_AMP[] = {0xA0, 0xA0, 0xB0, 0xB0, 0xB0, 0xB0, 0xC0, 0xE0};
	static const Bit8u  MODE_2_WET_AMP[] = {0x10, 0x30, 0x50, 0x70, 0x90, 0xC0, 0xF0, 0xF0};
	static const Bit8u  MODE_2_LPF_AMP = 0x80;

	static const Bit32u MODE_3_NUMBER_OF_ALLPASSES = 0;
	static const Bit32u MODE_3_NUMBER_OF_COMBS = 1;
	static const Bit32u MODE_3_DELAY[] = {16000 + MODE_3_FEEDBACK_DELAY + PROCESS_DELAY + MODE_3_ADDITIONAL_DELAY};
	static const Bit32u MODE_3_OUTL[] = {400, 624, 960, 1488, 2256, 3472, 5280, 8000};
	static const Bit32u MODE_3_OUTR[] = {800, 1248, 1920, 2976, 4512, 6944, 10560, 16000};
	static const Bit8u  MODE_3_COMB_FACTOR[] = {0x68};
	static const Bit8u  MODE_3_COMB_FEEDBACK[] = {0x68, 0x60};
	static const Bit8u  MODE_3_DRY_AMP[] = {0x20, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
	                                        0x20, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50};
	static const Bit8u  MODE_3_WET_AMP[] = {0x18, 0x18, 0x28, 0x40, 0x60, 0x80, 0xA8, 0xF8};

	static const BReverbSettings REVERB_MODE_0_SETTINGS = {MODE_0_NUMBER_OF_ALLPASSES, MODE_0_ALLPASSES, MODE_0_NUMBER_OF_COMBS, MODE_0_COMBS, MODE_0_OUTL, MODE_0_OUTR, MODE_0_COMB_FACTOR, MODE_0_COMB_FEEDBACK, MODE_0_DRY_AMP, MODE_0_WET_AMP, MODE_0_LPF_AMP};
	static const BReverbSettings REVERB_MODE_1_SETTINGS = {MODE_1_NUMBER_OF_ALLPASSES, MODE_1_ALLPASSES, MODE_1_NUMBER_OF_COMBS, MODE_1_COMBS, MODE_1_OUTL, MODE_1_OUTR, MODE_1_COMB_FACTOR, MODE_1_COMB_FEEDBACK, MODE_1_DRY_AMP, MODE_1_WET_AMP, MODE_1_LPF_AMP};
	static const BReverbSettings REVERB_MODE_2_SETTINGS = {MODE_2_NUMBER_OF_ALLPASSES, MODE_2_ALLPASSES, MODE_2_NUMBER_OF_COMBS, MODE_2_COMBS, MODE_2_OUTL, MODE_2_OUTR, MODE_2_COMB_FACTOR, MODE_2_COMB_FEEDBACK, MODE_2_DRY_AMP, MODE_2_WET_AMP, MODE_2_LPF_AMP};
	static const BReverbSettings REVERB_MODE_3_SETTINGS = {MODE_3_NUMBER_OF_ALLPASSES, NULL, MODE_3_NUMBER_OF_COMBS, MODE_3_DELAY, MODE_3_OUTL, MODE_3_OUTR, MODE_3_COMB_FACTOR, MODE_3_COMB_FEEDBACK, MODE_3_DRY_AMP, MODE_3_WET_AMP, 0};

	static const BReverbSettings * const REVERB_SETTINGS[] = {&REVERB_MODE_0_SETTINGS, &REVERB_MODE_1_SETTINGS, &REVERB_MODE_2_SETTINGS, &REVERB_MODE_3_SETTINGS};

	return *REVERB_SETTINGS[mode];
}

// Default reverb settings for "old" reverb model implemented in MT-32.
// Found by tracing reverb RAM data lines (thanks go to Lord_Nightmare & balrog).
static const BReverbSettings &getMT32Settings(const ReverbMode mode) {
	static const Bit32u MODE_0_NUMBER_OF_ALLPASSES = 3;
	static const Bit32u MODE_0_ALLPASSES[] = {994, 729, 78};
	static const Bit32u MODE_0_NUMBER_OF_COMBS = 4; // Same as above in the new model implementation
	static const Bit32u MODE_0_COMBS[] = {575 + PROCESS_DELAY, 2040, 2752, 3629};
	static const Bit32u MODE_0_OUTL[] = {2040, 687, 1814};
	static const Bit32u MODE_0_OUTR[] = {1019, 2072, 1};
	static const Bit8u  MODE_0_COMB_FACTOR[] = {0xB0, 0x60, 0x60, 0x60};
	static const Bit8u  MODE_0_COMB_FEEDBACK[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                              0x28, 0x48, 0x60, 0x70, 0x78, 0x80, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98};
	static const Bit8u  MODE_0_DRY_AMP[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
	static const Bit8u  MODE_0_WET_AMP[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x70, 0xA0, 0xE0};
	static const Bit8u  MODE_0_LPF_AMP = 0x80;

	static const Bit32u MODE_1_NUMBER_OF_ALLPASSES = 3;
	static const Bit32u MODE_1_ALLPASSES[] = {1324, 809, 176};
	static const Bit32u MODE_1_NUMBER_OF_COMBS = 4; // Same as above in the new model implementation
	static const Bit32u MODE_1_COMBS[] = {961 + PROCESS_DELAY, 2619, 3545, 4519};
	static const Bit32u MODE_1_OUTL[] = {2618, 1760, 4518};
	static const Bit32u MODE_1_OUTR[] = {1300, 3532, 2274};
	static const Bit8u  MODE_1_COMB_FACTOR[] = {0x90, 0x60, 0x60, 0x60};
	static const Bit8u  MODE_1_COMB_FEEDBACK[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                              0x28, 0x48, 0x60, 0x70, 0x78, 0x80, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98};
	static const Bit8u  MODE_1_DRY_AMP[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
	static const Bit8u  MODE_1_WET_AMP[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x70, 0xA0, 0xE0};
	static const Bit8u  MODE_1_LPF_AMP = 0x80;

	static const Bit32u MODE_2_NUMBER_OF_ALLPASSES = 3;
	static const Bit32u MODE_2_ALLPASSES[] = {969, 644, 157};
	static const Bit32u MODE_2_NUMBER_OF_COMBS = 4; // Same as above in the new model implementation
	static const Bit32u MODE_2_COMBS[] = {116 + PROCESS_DELAY, 2259, 2839, 3539};
	static const Bit32u MODE_2_OUTL[] = {2259, 718, 1769};
	static const Bit32u MODE_2_OUTR[] = {1136, 2128, 1};
	static const Bit8u  MODE_2_COMB_FACTOR[] = {0, 0x60, 0x60, 0x60};
	static const Bit8u  MODE_2_COMB_FEEDBACK[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                              0x28, 0x48, 0x60, 0x70, 0x78, 0x80, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98,
	                                              0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98};
	static const Bit8u  MODE_2_DRY_AMP[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
	static const Bit8u  MODE_2_WET_AMP[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x70, 0xA0, 0xE0};
	static const Bit8u  MODE_2_LPF_AMP = 0x80;

	static const Bit32u MODE_3_NUMBER_OF_ALLPASSES = 0;
	static const Bit32u MODE_3_NUMBER_OF_COMBS = 1;
	static const Bit32u MODE_3_DELAY[] = {16000 + MODE_3_FEEDBACK_DELAY + PROCESS_DELAY + MODE_3_ADDITIONAL_DELAY};
	static const Bit32u MODE_3_OUTL[] = {400, 624, 960, 1488, 2256, 3472, 5280, 8000};
	static const Bit32u MODE_3_OUTR[] = {800, 1248, 1920, 2976, 4512, 6944, 10560, 16000};
	static const Bit8u  MODE_3_COMB_FACTOR[] = {0x68};
	static const Bit8u  MODE_3_COMB_FEEDBACK[] = {0x68, 0x60};
	static const Bit8u  MODE_3_DRY_AMP[] = {0x10, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	                                        0x10, 0x20, 0x20, 0x10, 0x20, 0x10, 0x20, 0x10};
	static const Bit8u  MODE_3_WET_AMP[] = {0x08, 0x18, 0x28, 0x40, 0x60, 0x80, 0xA8, 0xF8};

	static const BReverbSettings REVERB_MODE_0_SETTINGS = {MODE_0_NUMBER_OF_ALLPASSES, MODE_0_ALLPASSES, MODE_0_NUMBER_OF_COMBS, MODE_0_COMBS, MODE_0_OUTL, MODE_0_OUTR, MODE_0_COMB_FACTOR, MODE_0_COMB_FEEDBACK, MODE_0_DRY_AMP, MODE_0_WET_AMP, MODE_0_LPF_AMP};
	static const BReverbSettings REVERB_MODE_1_SETTINGS = {MODE_1_NUMBER_OF_ALLPASSES, MODE_1_ALLPASSES, MODE_1_NUMBER_OF_COMBS, MODE_1_COMBS, MODE_1_OUTL, MODE_1_OUTR, MODE_1_COMB_FACTOR, MODE_1_COMB_FEEDBACK, MODE_1_DRY_AMP, MODE_1_WET_AMP, MODE_1_LPF_AMP};
	static const BReverbSettings REVERB_MODE_2_SETTINGS = {MODE_2_NUMBER_OF_ALLPASSES, MODE_2_ALLPASSES, MODE_2_NUMBER_OF_COMBS, MODE_2_COMBS, MODE_2_OUTL, MODE_2_OUTR, MODE_2_COMB_FACTOR, MODE_2_COMB_FEEDBACK, MODE_2_DRY_AMP, MODE_2_WET_AMP, MODE_2_LPF_AMP};
	static const BReverbSettings REVERB_MODE_3_SETTINGS = {MODE_3_NUMBER_OF_ALLPASSES, NULL, MODE_3_NUMBER_OF_COMBS, MODE_3_DELAY, MODE_3_OUTL, MODE_3_OUTR, MODE_3_COMB_FACTOR, MODE_3_COMB_FEEDBACK, MODE_3_DRY_AMP, MODE_3_WET_AMP, 0};

	static const BReverbSettings * const REVERB_SETTINGS[] = {&REVERB_MODE_0_SETTINGS, &REVERB_MODE_1_SETTINGS, &REVERB_MODE_2_SETTINGS, &REVERB_MODE_3_SETTINGS};

	return *REVERB_SETTINGS[mode];
}

static inline IntSample weirdMul(IntSample sample, Bit8u addMask, Bit8u carryMask) {
#if MT32EMU_BOSS_REVERB_PRECISE_MODE
	// This algorithm tries to emulate exactly Boss multiplication operation (at least this is what we see on reverb RAM data lines).
	Bit8u mask = 0x80;
	IntSampleEx res = 0;
	for (int i = 0; i < 8; i++) {
		IntSampleEx carry = (sample < 0) && (mask & carryMask) > 0 ? sample & 1 : 0;
		sample >>= 1;
		res += (mask & addMask) > 0 ? sample + carry : 0;
		mask >>= 1;
	}
	return IntSample(res);
#else
	(void)carryMask;
	return IntSample((IntSampleEx(sample) * addMask) >> 8);
#endif
}

static inline FloatSample weirdMul(FloatSample sample, Bit8u addMask, Bit8u carryMask) {
	(void)carryMask;
	return sample * addMask / 256.0f;
}

static inline IntSample halveSample(IntSample sample) {
	return sample >> 1;
}

static inline FloatSample halveSample(FloatSample sample) {
	return 0.5f * sample;
}

static inline IntSample quarterSample(IntSample sample) {
#if MT32EMU_BOSS_REVERB_PRECISE_MODE
	return (sample >> 1) / 2;
#else
	return sample >> 2;
#endif
}

static inline FloatSample quarterSample(FloatSample sample) {
	return 0.25f * sample;
}

static inline IntSample addDCBias(IntSample sample) {
	return sample;
}

static inline FloatSample addDCBias(FloatSample sample) {
	return sample + BIAS;
}

static inline IntSample addAllpassNoise(IntSample sample) {
#if MT32EMU_BOSS_REVERB_PRECISE_MODE
	// This introduces reverb noise which actually makes output from the real Boss chip nondeterministic
	return sample - 1;
#else
	return sample;
#endif
}

static inline FloatSample addAllpassNoise(FloatSample sample) {
	return sample;
}

/* NOTE:
 *   Thanks to Mok for discovering, the adder in BOSS reverb chip is found to perform addition with saturation to avoid integer overflow.
 *   Analysing of the algorithm suggests that the overflow is most probable when the combs output is added below.
 *   So, despite this isn't actually accurate, we only add the check here for performance reasons.
 */
static inline IntSample mixCombs(IntSample out1, IntSample out2, IntSample out3) {
#if MT32EMU_BOSS_REVERB_PRECISE_MODE
	return Synth::clipSampleEx(Synth::clipSampleEx(Synth::clipSampleEx(Synth::clipSampleEx(IntSampleEx(out1) + (IntSampleEx(out1) >> 1)) + IntSampleEx(out2)) + (IntSampleEx(out2) >> 1)) + IntSampleEx(out3));
#else
	return Synth::clipSampleEx(IntSampleEx(out1) + (IntSampleEx(out1) >> 1) + IntSampleEx(out2) + (IntSampleEx(out2) >> 1) + IntSampleEx(out3));
#endif
}

static inline FloatSample mixCombs(FloatSample out1, FloatSample out2, FloatSample out3) {
	return 1.5f * (out1 + out2) + out3;
}

template <class Sample>
class RingBuffer {
	static inline Sample sampleValueThreshold();

protected:
	Sample *buffer;
	const Bit32u size;
	Bit32u index;

public:
	RingBuffer(const Bit32u newsize) : size(newsize), index(0) {
		buffer = new Sample[size];
	}

	virtual ~RingBuffer() {
		delete[] buffer;
		buffer = NULL;
	}

	Sample next() {
		if (++index >= size) {
			index = 0;
		}
		return buffer[index];
	}

	bool isEmpty() const {
		if (buffer == NULL) return true;

		Sample *buf = buffer;
		for (Bit32u i = 0; i < size; i++) {
			if (*buf < -sampleValueThreshold() || *buf > sampleValueThreshold()) return false;
			buf++;
		}
		return true;
	}

	void mute() {
		Synth::muteSampleBuffer(buffer, size);
	}
};

template<>
IntSample RingBuffer<IntSample>::sampleValueThreshold() {
	return 8;
}

template<>
FloatSample RingBuffer<FloatSample>::sampleValueThreshold() {
	return 0.001f;
}

template <class Sample>
class AllpassFilter : public RingBuffer<Sample> {
public:
	AllpassFilter(const Bit32u useSize) : RingBuffer<Sample>(useSize) {}

	// This model corresponds to the allpass filter implementation of the real CM-32L device
	// found from sample analysis
	Sample process(const Sample in) {
		const Sample bufferOut = this->next();

		// store input - feedback / 2
		this->buffer[this->index] = in - halveSample(bufferOut);

		// return buffer output + feedforward / 2
		return bufferOut + halveSample(this->buffer[this->index]);
	}
};

template <class Sample>
class CombFilter : public RingBuffer<Sample> {
protected:
	const Bit8u filterFactor;
	Bit8u feedbackFactor;

public:
	CombFilter(const Bit32u useSize, const Bit8u useFilterFactor) : RingBuffer<Sample>(useSize), filterFactor(useFilterFactor) {}

	// This model corresponds to the comb filter implementation of the real CM-32L device
	void process(const Sample in) {

		// the previously stored value
		const Sample last = this->buffer[this->index];

		// prepare input + feedback
		const Sample filterIn = in + weirdMul(this->next(), feedbackFactor, 0xF0);

		// store input + feedback processed by a low-pass filter
		this->buffer[this->index] = weirdMul(last, filterFactor, 0xC0) - filterIn;
	}

	Sample getOutputAt(const Bit32u outIndex) const {
		return this->buffer[(this->size + this->index - outIndex) % this->size];
	}

	void setFeedbackFactor(const Bit8u useFeedbackFactor) {
		feedbackFactor = useFeedbackFactor;
	}
};

template <class Sample>
class DelayWithLowPassFilter : public CombFilter<Sample> {
	Bit8u amp;

public:
	DelayWithLowPassFilter(const Bit32u useSize, const Bit8u useFilterFactor, const Bit8u useAmp)
		: CombFilter<Sample>(useSize, useFilterFactor), amp(useAmp) {}

	void process(const Sample in) {
		// the previously stored value
		const Sample last = this->buffer[this->index];

		// move to the next index
		this->next();

		// low-pass filter process
		Sample lpfOut = weirdMul(last, this->filterFactor, 0xFF) + in;

		// store lpfOut multiplied by LPF amp factor
		this->buffer[this->index] = weirdMul(lpfOut, amp, 0xFF);
	}
};

template <class Sample>
class TapDelayCombFilter : public CombFilter<Sample> {
	Bit32u outL;
	Bit32u outR;

public:
	TapDelayCombFilter(const Bit32u useSize, const Bit8u useFilterFactor) : CombFilter<Sample>(useSize, useFilterFactor) {}

	void process(const Sample in) {
		// the previously stored value
		const Sample last = this->buffer[this->index];

		// move to the next index
		this->next();

		// prepare input + feedback
		// Actually, the size of the filter varies with the TIME parameter, the feedback sample is taken from the position just below the right output
		const Sample filterIn = in + weirdMul(this->getOutputAt(outR + MODE_3_FEEDBACK_DELAY), this->feedbackFactor, 0xF0);

		// store input + feedback processed by a low-pass filter
		this->buffer[this->index] = weirdMul(last, this->filterFactor, 0xF0) - filterIn;
	}

	Sample getLeftOutput() const {
		return this->getOutputAt(outL + PROCESS_DELAY + MODE_3_ADDITIONAL_DELAY);
	}

	Sample getRightOutput() const {
		return this->getOutputAt(outR + PROCESS_DELAY + MODE_3_ADDITIONAL_DELAY);
	}

	void setOutputPositions(const Bit32u useOutL, const Bit32u useOutR) {
		outL = useOutL;
		outR = useOutR;
	}
};

template <class Sample>
class BReverbModelImpl : public BReverbModel {
public:
	AllpassFilter<Sample> **allpasses;
	CombFilter<Sample> **combs;

	const BReverbSettings &currentSettings;
	const bool tapDelayMode;
	Bit8u dryAmp;
	Bit8u wetLevel;

	BReverbModelImpl(const ReverbMode mode, const bool mt32CompatibleModel) :
		allpasses(NULL), combs(NULL),
		currentSettings(mt32CompatibleModel ? getMT32Settings(mode) : getCM32L_LAPCSettings(mode)),
		tapDelayMode(mode == REVERB_MODE_TAP_DELAY)
	{}

	~BReverbModelImpl() {
		close();
	}

	bool isOpen() const {
		return combs != NULL;
	}

	void open() {
		if (isOpen()) return;
		if (currentSettings.numberOfAllpasses > 0) {
			allpasses = new AllpassFilter<Sample>*[currentSettings.numberOfAllpasses];
			for (Bit32u i = 0; i < currentSettings.numberOfAllpasses; i++) {
				allpasses[i] = new AllpassFilter<Sample>(currentSettings.allpassSizes[i]);
			}
		}
		combs = new CombFilter<Sample>*[currentSettings.numberOfCombs];
		if (tapDelayMode) {
			*combs = new TapDelayCombFilter<Sample>(*currentSettings.combSizes, *currentSettings.filterFactors);
		} else {
			combs[0] = new DelayWithLowPassFilter<Sample>(currentSettings.combSizes[0], currentSettings.filterFactors[0], currentSettings.lpfAmp);
			for (Bit32u i = 1; i < currentSettings.numberOfCombs; i++) {
				combs[i] = new CombFilter<Sample>(currentSettings.combSizes[i], currentSettings.filterFactors[i]);
			}
		}
		mute();
	}

	void close() {
		if (allpasses != NULL) {
			for (Bit32u i = 0; i < currentSettings.numberOfAllpasses; i++) {
				if (allpasses[i] != NULL) {
					delete allpasses[i];
					allpasses[i] = NULL;
				}
			}
			delete[] allpasses;
			allpasses = NULL;
		}
		if (combs != NULL) {
			for (Bit32u i = 0; i < currentSettings.numberOfCombs; i++) {
				if (combs[i] != NULL) {
					delete combs[i];
					combs[i] = NULL;
				}
			}
			delete[] combs;
			combs = NULL;
		}
	}

	void mute() {
		if (allpasses != NULL) {
			for (Bit32u i = 0; i < currentSettings.numberOfAllpasses; i++) {
				allpasses[i]->mute();
			}
		}
		if (combs != NULL) {
			for (Bit32u i = 0; i < currentSettings.numberOfCombs; i++) {
				combs[i]->mute();
			}
		}
	}

	void setParameters(Bit8u time, Bit8u level) {
		if (!isOpen()) return;
		level &= 7;
		time &= 7;
		if (tapDelayMode) {
			TapDelayCombFilter<Sample> *comb = static_cast<TapDelayCombFilter<Sample> *> (*combs);
			comb->setOutputPositions(currentSettings.outLPositions[time], currentSettings.outRPositions[time & 7]);
			comb->setFeedbackFactor(currentSettings.feedbackFactors[((level < 3) || (time < 6)) ? 0 : 1]);
		} else {
			for (Bit32u i = 1; i < currentSettings.numberOfCombs; i++) {
				combs[i]->setFeedbackFactor(currentSettings.feedbackFactors[(i << 3) + time]);
			}
		}
		if (time == 0 && level == 0) {
			dryAmp = wetLevel = 0;
		} else {
			if (tapDelayMode && ((time == 0) || (time == 1 && level == 1))) {
				// Looks like MT-32 implementation has some minor quirks in this mode:
				// for odd level values, the output level changes sometimes depending on the time value which doesn't seem right.
				dryAmp = currentSettings.dryAmps[level + 8];
			} else {
				dryAmp = currentSettings.dryAmps[level];
			}
			wetLevel = currentSettings.wetLevels[level];
		}
	}

	bool isActive() const {
		if (!isOpen()) return false;
		for (Bit32u i = 0; i < currentSettings.numberOfAllpasses; i++) {
			if (!allpasses[i]->isEmpty()) return true;
		}
		for (Bit32u i = 0; i < currentSettings.numberOfCombs; i++) {
			if (!combs[i]->isEmpty()) return true;
		}
		return false;
	}

	bool isMT32Compatible(const ReverbMode mode) const {
		return &currentSettings == &getMT32Settings(mode);
	}

	template <class SampleEx>
	void produceOutput(const Sample *inLeft, const Sample *inRight, Sample *outLeft, Sample *outRight, Bit32u numSamples) {
		if (!isOpen()) {
			Synth::muteSampleBuffer(outLeft, numSamples);
			Synth::muteSampleBuffer(outRight, numSamples);
			return;
		}

		while ((numSamples--) > 0) {
			Sample dry;

			if (tapDelayMode) {
				dry = halveSample(*(inLeft++)) + halveSample(*(inRight++));
			} else {
				dry = quarterSample(*(inLeft++)) + quarterSample(*(inRight++));
			}

			// Looks like dryAmp doesn't change in MT-32 but it does in CM-32L / LAPC-I
			dry = weirdMul(addDCBias(dry), dryAmp, 0xFF);

			if (tapDelayMode) {
				TapDelayCombFilter<Sample> *comb = static_cast<TapDelayCombFilter<Sample> *>(*combs);
				comb->process(dry);
				if (outLeft != NULL) {
					*(outLeft++) = weirdMul(comb->getLeftOutput(), wetLevel, 0xFF);
				}
				if (outRight != NULL) {
					*(outRight++) = weirdMul(comb->getRightOutput(), wetLevel, 0xFF);
				}
			} else {
				DelayWithLowPassFilter<Sample> * const entranceDelay = static_cast<DelayWithLowPassFilter<Sample> *>(combs[0]);
				// If the output position is equal to the comb size, get it now in order not to loose it
				Sample link = entranceDelay->getOutputAt(currentSettings.combSizes[0] - 1);

				// Entrance LPF. Note, comb.process() differs a bit here.
				entranceDelay->process(dry);

				link = allpasses[0]->process(addAllpassNoise(link));
				link = allpasses[1]->process(link);
				link = allpasses[2]->process(link);

				// If the output position is equal to the comb size, get it now in order not to loose it
				Sample outL1 = combs[1]->getOutputAt(currentSettings.outLPositions[0] - 1);

				combs[1]->process(link);
				combs[2]->process(link);
				combs[3]->process(link);

				if (outLeft != NULL) {
					Sample outL2 = combs[2]->getOutputAt(currentSettings.outLPositions[1]);
					Sample outL3 = combs[3]->getOutputAt(currentSettings.outLPositions[2]);
					Sample outSample = mixCombs(outL1, outL2, outL3);
					*(outLeft++) = weirdMul(outSample, wetLevel, 0xFF);
				}
				if (outRight != NULL) {
					Sample outR1 = combs[1]->getOutputAt(currentSettings.outRPositions[0]);
					Sample outR2 = combs[2]->getOutputAt(currentSettings.outRPositions[1]);
					Sample outR3 = combs[3]->getOutputAt(currentSettings.outRPositions[2]);
					Sample outSample = mixCombs(outR1, outR2, outR3);
					*(outRight++) = weirdMul(outSample, wetLevel, 0xFF);
				}
			} // if (tapDelayMode)
		} // while ((numSamples--) > 0)
	} // produceOutput

	bool process(const IntSample *inLeft, const IntSample *inRight, IntSample *outLeft, IntSample *outRight, Bit32u numSamples);
	bool process(const FloatSample *inLeft, const FloatSample *inRight, FloatSample *outLeft, FloatSample *outRight, Bit32u numSamples);
};

BReverbModel *BReverbModel::createBReverbModel(const ReverbMode mode, const bool mt32CompatibleModel, const RendererType rendererType) {
	switch (rendererType)
	{
	case RendererType_BIT16S:
		return new BReverbModelImpl<IntSample>(mode, mt32CompatibleModel);
	case RendererType_FLOAT:
		return new BReverbModelImpl<FloatSample>(mode, mt32CompatibleModel);
	default:
		break;
	}
	return NULL;
}

template <>
bool BReverbModelImpl<IntSample>::process(const IntSample *inLeft, const IntSample *inRight, IntSample *outLeft, IntSample *outRight, Bit32u numSamples) {
	produceOutput<IntSampleEx>(inLeft, inRight, outLeft, outRight, numSamples);
	return true;
}

template <>
bool BReverbModelImpl<IntSample>::process(const FloatSample *, const FloatSample *, FloatSample *, FloatSample *, Bit32u) {
	return false;
}

template <>
bool BReverbModelImpl<FloatSample>::process(const IntSample *, const IntSample *, IntSample *, IntSample *, Bit32u) {
	return false;
}

template <>
bool BReverbModelImpl<FloatSample>::process(const FloatSample *inLeft, const FloatSample *inRight, FloatSample *outLeft, FloatSample *outRight, Bit32u numSamples) {
	produceOutput<FloatSample>(inLeft, inRight, outLeft, outRight, numSamples);
	return true;
}

} // namespace MT32Emu

/************************************************** Part.cpp ******************************************************/
namespace MT32Emu {

static const Bit8u PartialStruct[13] = {
	0, 0, 2, 2, 1, 3,
	3, 0, 3, 0, 2, 1, 3
};

static const Bit8u PartialMixStruct[13] = {
	0, 1, 0, 1, 1, 0,
	1, 3, 3, 2, 2, 2, 2
};

RhythmPart::RhythmPart(Synth *useSynth, unsigned int usePartNum): Part(useSynth, usePartNum) {
	strcpy(name, "Rhythm");
	rhythmTemp = &synth->mt32ram.rhythmTemp[0];
	refresh();
}

Part::Part(Synth *useSynth, unsigned int usePartNum) {
	synth = useSynth;
	partNum = usePartNum;
	patchCache[0].dirty = true;
	holdpedal = false;
	patchTemp = &synth->mt32ram.patchTemp[partNum];
	if (usePartNum == 8) {
		// Nasty hack for rhythm
		timbreTemp = NULL;
	} else {
		sprintf(name, "Part %d", partNum + 1);
		timbreTemp = &synth->mt32ram.timbreTemp[partNum];
	}
	currentInstr[0] = 0;
	currentInstr[10] = 0;
	volumeOverride = 255;
	modulation = 0;
	expression = 100;
	pitchBend = 0;
	activePartialCount = 0;
	activeNonReleasingPolyCount = 0;
	memset(patchCache, 0, sizeof(patchCache));
}

Part::~Part() {
	while (!activePolys.isEmpty()) {
		delete activePolys.takeFirst();
	}
}

void Part::setDataEntryMSB(unsigned char midiDataEntryMSB) {
	if (nrpn) {
		// The last RPN-related control change was for an NRPN,
		// which the real synths don't support.
		return;
	}
	if (rpn != 0) {
		// The RPN has been set to something other than 0,
		// which is the only RPN that these synths support
		return;
	}
	patchTemp->patch.benderRange = midiDataEntryMSB > 24 ? 24 : midiDataEntryMSB;
	updatePitchBenderRange();
}

void Part::setNRPN() {
	nrpn = true;
}

void Part::setRPNLSB(unsigned char midiRPNLSB) {
	nrpn = false;
	rpn = (rpn & 0xFF00) | midiRPNLSB;
}

void Part::setRPNMSB(unsigned char midiRPNMSB) {
	nrpn = false;
	rpn = (rpn & 0x00FF) | (midiRPNMSB << 8);
}

void Part::setHoldPedal(bool pressed) {
	if (holdpedal && !pressed) {
		holdpedal = false;
		stopPedalHold();
	} else {
		holdpedal = pressed;
	}
}

Bit32s Part::getPitchBend() const {
	return pitchBend;
}

void Part::setBend(unsigned int midiBend) {
	// CONFIRMED:
	pitchBend = ((signed(midiBend) - 8192) * pitchBenderRange) >> 14; // PORTABILITY NOTE: Assumes arithmetic shift
}

Bit8u Part::getModulation() const {
	return modulation;
}

void Part::setModulation(unsigned int midiModulation) {
	modulation = Bit8u(midiModulation);
}

void Part::resetAllControllers() {
	modulation = 0;
	expression = 100;
	pitchBend = 0;
	setHoldPedal(false);
}

void Part::reset() {
	resetAllControllers();
	allSoundOff();
	rpn = 0xFFFF;
}

void RhythmPart::refresh() {
	// (Re-)cache all the mapped timbres ahead of time
	for (unsigned int drumNum = 0; drumNum < synth->controlROMMap->rhythmSettingsCount; drumNum++) {
		int drumTimbreNum = rhythmTemp[drumNum].timbre;
		if (drumTimbreNum >= 127) { // 94 on MT-32
			continue;
		}
		PatchCache *cache = drumCache[drumNum];
		backupCacheToPartials(cache);
		for (int t = 0; t < 4; t++) {
			// Common parameters, stored redundantly
			cache[t].dirty = true;
			cache[t].reverb = rhythmTemp[drumNum].reverbSwitch > 0;
		}
	}
	updatePitchBenderRange();
}

void Part::refresh() {
	backupCacheToPartials(patchCache);
	for (int t = 0; t < 4; t++) {
		// Common parameters, stored redundantly
		patchCache[t].dirty = true;
		patchCache[t].reverb = patchTemp->patch.reverbSwitch > 0;
	}
	memcpy(currentInstr, timbreTemp->common.name, 10);
	synth->newTimbreSet(partNum);
	updatePitchBenderRange();
}

const char *Part::getCurrentInstr() const {
	return &currentInstr[0];
}

void RhythmPart::refreshTimbre(unsigned int absTimbreNum) {
	for (int m = 0; m < 85; m++) {
		if (rhythmTemp[m].timbre == absTimbreNum - 128) {
			drumCache[m][0].dirty = true;
		}
	}
}

void Part::refreshTimbre(unsigned int absTimbreNum) {
	if (getAbsTimbreNum() == absTimbreNum) {
		memcpy(currentInstr, timbreTemp->common.name, 10);
		patchCache[0].dirty = true;
	}
}

void Part::setPatch(const PatchParam *patch) {
	patchTemp->patch = *patch;
}

void RhythmPart::setTimbre(TimbreParam * /*timbre*/) {
	synth->printDebug("%s: Attempted to call setTimbre() - doesn't make sense for rhythm", name);
}

void Part::setTimbre(TimbreParam *timbre) {
	*timbreTemp = *timbre;
}

unsigned int RhythmPart::getAbsTimbreNum() const {
	synth->printDebug("%s: Attempted to call getAbsTimbreNum() - doesn't make sense for rhythm", name);
	return 0;
}

unsigned int Part::getAbsTimbreNum() const {
	return (patchTemp->patch.timbreGroup * 64) + patchTemp->patch.timbreNum;
}

#if MT32EMU_MONITOR_MIDI > 0
void RhythmPart::setProgram(unsigned int patchNum) {
	synth->printDebug("%s: Attempt to set program (%d) on rhythm is invalid", name, patchNum);
}
#else
void RhythmPart::setProgram(unsigned int) { }
#endif

void Part::setProgram(unsigned int patchNum) {
	setPatch(&synth->mt32ram.patches[patchNum]);
	holdpedal = false;
	allSoundOff();
	setTimbre(&synth->mt32ram.timbres[getAbsTimbreNum()].timbre);
	refresh();
}

void Part::updatePitchBenderRange() {
	pitchBenderRange = patchTemp->patch.benderRange * 683;
}

void Part::backupCacheToPartials(PatchCache cache[4]) {
	// check if any partials are still playing with the old patch cache
	// if so then duplicate the cached data from the part to the partial so that
	// we can change the part's cache without affecting the partial.
	// We delay this until now to avoid a copy operation with every note played
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		poly->backupCacheToPartials(cache);
	}
}

void Part::cacheTimbre(PatchCache cache[4], const TimbreParam *timbre) {
	backupCacheToPartials(cache);
	int partialCount = 0;
	for (int t = 0; t < 4; t++) {
		if (((timbre->common.partialMute >> t) & 0x1) == 1) {
			cache[t].playPartial = true;
			partialCount++;
		} else {
			cache[t].playPartial = false;
			continue;
		}

		// Calculate and cache common parameters
		cache[t].srcPartial = timbre->partial[t];

		cache[t].pcm = timbre->partial[t].wg.pcmWave;

		switch (t) {
		case 0:
			cache[t].PCMPartial = (PartialStruct[int(timbre->common.partialStructure12)] & 0x2) ? true : false;
			cache[t].structureMix = PartialMixStruct[int(timbre->common.partialStructure12)];
			cache[t].structurePosition = 0;
			cache[t].structurePair = 1;
			break;
		case 1:
			cache[t].PCMPartial = (PartialStruct[int(timbre->common.partialStructure12)] & 0x1) ? true : false;
			cache[t].structureMix = PartialMixStruct[int(timbre->common.partialStructure12)];
			cache[t].structurePosition = 1;
			cache[t].structurePair = 0;
			break;
		case 2:
			cache[t].PCMPartial = (PartialStruct[int(timbre->common.partialStructure34)] & 0x2) ? true : false;
			cache[t].structureMix = PartialMixStruct[int(timbre->common.partialStructure34)];
			cache[t].structurePosition = 0;
			cache[t].structurePair = 3;
			break;
		case 3:
			cache[t].PCMPartial = (PartialStruct[int(timbre->common.partialStructure34)] & 0x1) ? true : false;
			cache[t].structureMix = PartialMixStruct[int(timbre->common.partialStructure34)];
			cache[t].structurePosition = 1;
			cache[t].structurePair = 2;
			break;
		default:
			break;
		}

		cache[t].partialParam = &timbre->partial[t];

		cache[t].waveform = timbre->partial[t].wg.waveform;
	}
	for (int t = 0; t < 4; t++) {
		// Common parameters, stored redundantly
		cache[t].dirty = false;
		cache[t].partialCount = partialCount;
		cache[t].sustain = (timbre->common.noSustain == 0);
	}
	//synth->printDebug("Res 1: %d 2: %d 3: %d 4: %d", cache[0].waveform, cache[1].waveform, cache[2].waveform, cache[3].waveform);

#if MT32EMU_MONITOR_INSTRUMENTS > 0
	synth->printDebug("%s (%s): Recached timbre", name, currentInstr);
	for (int i = 0; i < 4; i++) {
		synth->printDebug(" %d: play=%s, pcm=%s (%d), wave=%d", i, cache[i].playPartial ? "YES" : "NO", cache[i].PCMPartial ? "YES" : "NO", timbre->partial[i].wg.pcmWave, timbre->partial[i].wg.waveform);
	}
#endif
}

const char *Part::getName() const {
	return name;
}

void Part::setVolume(unsigned int midiVolume) {
	// CONFIRMED: This calculation matches the table used in the control ROM
	patchTemp->outputLevel = Bit8u(midiVolume * 100 / 127);
	//synth->printDebug("%s (%s): Set volume to %d", name, currentInstr, midiVolume);
}

Bit8u Part::getVolume() const {
	return volumeOverride <= 100 ? volumeOverride : patchTemp->outputLevel;
}

void Part::setVolumeOverride(Bit8u volume) {
	volumeOverride = volume;
	// When volume is 0, we want the part to stop producing any sound at all.
	// For that to achieve, we have to actually stop processing NoteOn MIDI messages; merely
	// returning 0 volume is not enough - the output may still be generated at a very low level.
	// But first, we have to stop all the currently playing polys. This behaviour may also help
	// with performance issues, because parts muted this way barely consume CPU resources.
	if (volume == 0) allSoundOff();
}

Bit8u Part::getVolumeOverride() const {
	return volumeOverride;
}

Bit8u Part::getExpression() const {
	return expression;
}

void Part::setExpression(unsigned int midiExpression) {
	// CONFIRMED: This calculation matches the table used in the control ROM
	expression = Bit8u(midiExpression * 100 / 127);
}

void RhythmPart::setPan(unsigned int midiPan) {
	// CONFIRMED: This does change patchTemp, but has no actual effect on playback.
#if MT32EMU_MONITOR_MIDI > 0
	synth->printDebug("%s: Pointlessly setting pan (%d) on rhythm part", name, midiPan);
#endif
	Part::setPan(midiPan);
}

void Part::setPan(unsigned int midiPan) {
	// NOTE: Panning is inverted compared to GM.

	if (synth->controlROMFeatures->quirkPanMult) {
		// MT-32: Divide by 9
		patchTemp->panpot = Bit8u(midiPan / 9);
	} else {
		// CM-32L: Divide by 8.5
		patchTemp->panpot = Bit8u((midiPan << 3) / 68);
	}

	//synth->printDebug("%s (%s): Set pan to %d", name, currentInstr, panpot);
}

/**
 * Applies key shift to a MIDI key and converts it into an internal key value in the range 12-108.
 */
unsigned int Part::midiKeyToKey(unsigned int midiKey) {
	if (synth->controlROMFeatures->quirkKeyShift) {
		// NOTE: On MT-32 GEN0, key isn't adjusted, and keyShift is applied further in TVP, unlike newer units:
		return midiKey;
	}
	int key = midiKey + patchTemp->patch.keyShift;
	if (key < 36) {
		// After keyShift is applied, key < 36, so move up by octaves
		while (key < 36) {
			key += 12;
		}
	} else if (key > 132) {
		// After keyShift is applied, key > 132, so move down by octaves
		while (key > 132) {
			key -= 12;
		}
	}
	key -= 24;
	return key;
}

void RhythmPart::noteOn(unsigned int midiKey, unsigned int velocity) {
	if (midiKey < 24 || midiKey > 108) { /*> 87 on MT-32)*/
		synth->printDebug("%s: Attempted to play invalid key %d (velocity %d)", name, midiKey, velocity);
		return;
	}
	synth->rhythmNotePlayed();
	unsigned int key = midiKey;
	unsigned int drumNum = key - 24;
	int drumTimbreNum = rhythmTemp[drumNum].timbre;
	const int drumTimbreCount = 64 + synth->controlROMMap->timbreRCount; // 94 on MT-32, 128 on LAPC-I/CM32-L
	if (drumTimbreNum == 127 || drumTimbreNum >= drumTimbreCount) { // timbre #127 is OFF, no sense to play it
		synth->printDebug("%s: Attempted to play unmapped key %d (velocity %d)", name, midiKey, velocity);
		return;
	}
	// CONFIRMED: Two special cases described by Mok
	if (drumTimbreNum == 64 + 6) {
		noteOff(0);
		key = 1;
	} else if (drumTimbreNum == 64 + 7) {
		// This noteOff(0) is not performed on MT-32, only LAPC-I
		noteOff(0);
		key = 0;
	}
	int absTimbreNum = drumTimbreNum + 128;
	TimbreParam *timbre = &synth->mt32ram.timbres[absTimbreNum].timbre;
	memcpy(currentInstr, timbre->common.name, 10);
	if (drumCache[drumNum][0].dirty) {
		cacheTimbre(drumCache[drumNum], timbre);
	}
#if MT32EMU_MONITOR_INSTRUMENTS > 0
	synth->printDebug("%s (%s): Start poly (drum %d, timbre %d): midiKey %u, key %u, velo %u, mod %u, exp %u, bend %u", name, currentInstr, drumNum, absTimbreNum, midiKey, key, velocity, modulation, expression, pitchBend);
#if MT32EMU_MONITOR_INSTRUMENTS > 1
	// According to info from Mok, keyShift does not appear to affect anything on rhythm part on LAPC-I, but may do on MT-32 - needs investigation
	synth->printDebug(" Patch: (timbreGroup %u), (timbreNum %u), (keyShift %u), fineTune %u, benderRange %u, assignMode %u, (reverbSwitch %u)", patchTemp->patch.timbreGroup, patchTemp->patch.timbreNum, patchTemp->patch.keyShift, patchTemp->patch.fineTune, patchTemp->patch.benderRange, patchTemp->patch.assignMode, patchTemp->patch.reverbSwitch);
	synth->printDebug(" PatchTemp: outputLevel %u, (panpot %u)", patchTemp->outputLevel, patchTemp->panpot);
	synth->printDebug(" RhythmTemp: timbre %u, outputLevel %u, panpot %u, reverbSwitch %u", rhythmTemp[drumNum].timbre, rhythmTemp[drumNum].outputLevel, rhythmTemp[drumNum].panpot, rhythmTemp[drumNum].reverbSwitch);
#endif
#endif
	playPoly(drumCache[drumNum], &rhythmTemp[drumNum], midiKey, key, velocity);
}

void Part::noteOn(unsigned int midiKey, unsigned int velocity) {
	unsigned int key = midiKeyToKey(midiKey);
	if (patchCache[0].dirty) {
		cacheTimbre(patchCache, timbreTemp);
	}
#if MT32EMU_MONITOR_INSTRUMENTS > 0
	synth->printDebug("%s (%s): Start poly: midiKey %u, key %u, velo %u, mod %u, exp %u, bend %u", name, currentInstr, midiKey, key, velocity, modulation, expression, pitchBend);
#if MT32EMU_MONITOR_INSTRUMENTS > 1
	synth->printDebug(" Patch: timbreGroup %u, timbreNum %u, keyShift %u, fineTune %u, benderRange %u, assignMode %u, reverbSwitch %u", patchTemp->patch.timbreGroup, patchTemp->patch.timbreNum, patchTemp->patch.keyShift, patchTemp->patch.fineTune, patchTemp->patch.benderRange, patchTemp->patch.assignMode, patchTemp->patch.reverbSwitch);
	synth->printDebug(" PatchTemp: outputLevel %u, panpot %u", patchTemp->outputLevel, patchTemp->panpot);
#endif
#endif
	playPoly(patchCache, NULL, midiKey, key, velocity);
}

bool Part::abortFirstPoly(unsigned int key) {
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		if (poly->getKey() == key) {
			return poly->startAbort();
		}
	}
	return false;
}

bool Part::abortFirstPoly(PolyState polyState) {
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		if (poly->getState() == polyState) {
			return poly->startAbort();
		}
	}
	return false;
}

bool Part::abortFirstPolyPreferHeld() {
	if (abortFirstPoly(POLY_Held)) {
		return true;
	}
	return abortFirstPoly();
}

bool Part::abortFirstPoly() {
	if (activePolys.isEmpty()) {
		return false;
	}
	return activePolys.getFirst()->startAbort();
}

void Part::playPoly(const PatchCache cache[4], const MemParams::RhythmTemp *rhythmTemp, unsigned int midiKey, unsigned int key, unsigned int velocity) {
	// CONFIRMED: Even in single-assign mode, we don't abort playing polys if the timbre to play is completely muted.
	unsigned int needPartials = cache[0].partialCount;
	if (needPartials == 0) {
		synth->printDebug("%s (%s): Completely muted instrument", name, currentInstr);
		return;
	}

	if ((patchTemp->patch.assignMode & 2) == 0) {
		// Single-assign mode
		abortFirstPoly(key);
		if (synth->isAbortingPoly()) return;
	}

	if (!synth->partialManager->freePartials(needPartials, partNum)) {
#if MT32EMU_MONITOR_PARTIALS > 0
		synth->printDebug("%s (%s): Insufficient free partials to play key %d (velocity %d); needed=%d, free=%d, assignMode=%d", name, currentInstr, midiKey, velocity, needPartials, synth->partialManager->getFreePartialCount(), patchTemp->patch.assignMode);
		synth->printPartialUsage();
#endif
		return;
	}
	if (synth->isAbortingPoly()) return;

	Poly *poly = synth->partialManager->assignPolyToPart(this);
	if (poly == NULL) {
		synth->printDebug("%s (%s): No free poly to play key %d (velocity %d)", name, currentInstr, midiKey, velocity);
		return;
	}
	if (patchTemp->patch.assignMode & 1) {
		// Priority to data first received
		activePolys.prepend(poly);
	} else {
		activePolys.append(poly);
	}

	Partial *partials[4];
	for (int x = 0; x < 4; x++) {
		if (cache[x].playPartial) {
			partials[x] = synth->partialManager->allocPartial(partNum);
			activePartialCount++;
		} else {
			partials[x] = NULL;
		}
	}
	poly->reset(key, velocity, cache[0].sustain, partials);

	for (int x = 0; x < 4; x++) {
		if (partials[x] != NULL) {
#if MT32EMU_MONITOR_PARTIALS > 2
			synth->printDebug("%s (%s): Allocated partial %d", name, currentInstr, partials[x]->debugGetPartialNum());
#endif
			partials[x]->startPartial(this, poly, &cache[x], rhythmTemp, partials[cache[x].structurePair]);
		}
	}
#if MT32EMU_MONITOR_PARTIALS > 1
	synth->printPartialUsage();
#endif
}

void Part::allNotesOff() {
	// The MIDI specification states - and Mok confirms - that all notes off (0x7B)
	// should treat the hold pedal as usual.
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		// FIXME: This has special handling of key 0 in NoteOff that Mok has not yet confirmed applies to AllNotesOff.
		// if (poly->canSustain() || poly->getKey() == 0) {
		// FIXME: The real devices are found to be ignoring non-sustaining polys while processing AllNotesOff. Need to be confirmed.
		if (poly->canSustain()) {
			poly->noteOff(holdpedal);
		}
	}
}

void Part::allSoundOff() {
	// MIDI "All sound off" (0x78) should release notes immediately regardless of the hold pedal.
	// This controller is not actually implemented by the synths, though (according to the docs and Mok) -
	// we're only using this method internally.
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		poly->startDecay();
	}
}

void Part::stopPedalHold() {
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		poly->stopPedalHold();
	}
}

void RhythmPart::noteOff(unsigned int midiKey) {
	stopNote(midiKey);
}

void Part::noteOff(unsigned int midiKey) {
	stopNote(midiKeyToKey(midiKey));
}

void Part::stopNote(unsigned int key) {
#if MT32EMU_MONITOR_INSTRUMENTS > 0
	synth->printDebug("%s (%s): stopping key %d", name, currentInstr, key);
#endif

	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		// Generally, non-sustaining instruments ignore note off. They die away eventually anyway.
		// Key 0 (only used by special cases on rhythm part) reacts to note off even if non-sustaining or pedal held.
		if (poly->getKey() == key && (poly->canSustain() || key == 0)) {
			if (poly->noteOff(holdpedal && key != 0)) {
				break;
			}
		}
	}
}

const MemParams::PatchTemp *Part::getPatchTemp() const {
	return patchTemp;
}

unsigned int Part::getActivePartialCount() const {
	return activePartialCount;
}

const Poly *Part::getFirstActivePoly() const {
	return activePolys.getFirst();
}

unsigned int Part::getActiveNonReleasingPartialCount() const {
	unsigned int activeNonReleasingPartialCount = 0;
	for (Poly *poly = activePolys.getFirst(); poly != NULL; poly = poly->getNext()) {
		if (poly->getState() != POLY_Releasing) {
			activeNonReleasingPartialCount += poly->getActivePartialCount();
		}
	}
	return activeNonReleasingPartialCount;
}

Synth *Part::getSynth() const {
	return synth;
}

void Part::partialDeactivated(Poly *poly) {
	activePartialCount--;
	if (!poly->isActive()) {
		activePolys.remove(poly);
		synth->partialManager->polyFreed(poly);
	}
}

void RhythmPart::polyStateChanged(PolyState, PolyState) {}

void Part::polyStateChanged(PolyState oldState, PolyState newState) {
	switch (newState) {
	case POLY_Playing:
		if (activeNonReleasingPolyCount++ == 0) synth->voicePartStateChanged(partNum, true);
		break;
	case POLY_Releasing:
	case POLY_Inactive:
		if (oldState == POLY_Playing || oldState == POLY_Held) {
			if (--activeNonReleasingPolyCount == 0) synth->voicePartStateChanged(partNum, false);
		}
		break;
	default:
		break;
	}
#ifdef MT32EMU_TRACE_POLY_STATE_CHANGES
	synth->printDebug("Part %d: Changed poly state %d->%d, activeNonReleasingPolyCount=%d", partNum, oldState, newState, activeNonReleasingPolyCount);
#endif
}

PolyList::PolyList() : firstPoly(NULL), lastPoly(NULL) {}

bool PolyList::isEmpty() const {
#ifdef MT32EMU_POLY_LIST_DEBUG
	if ((firstPoly == NULL || lastPoly == NULL) && firstPoly != lastPoly) {
		printf("PolyList: desynchronised firstPoly & lastPoly pointers\n");
	}
#endif
	return firstPoly == NULL && lastPoly == NULL;
}

Poly *PolyList::getFirst() const {
	return firstPoly;
}

Poly *PolyList::getLast() const {
	return lastPoly;
}

void PolyList::prepend(Poly *poly) {
#ifdef MT32EMU_POLY_LIST_DEBUG
	if (poly->getNext() != NULL) {
		printf("PolyList: Non-NULL next field in a Poly being prepended is ignored\n");
	}
#endif
	poly->setNext(firstPoly);
	firstPoly = poly;
	if (lastPoly == NULL) {
		lastPoly = poly;
	}
}

void PolyList::append(Poly *poly) {
#ifdef MT32EMU_POLY_LIST_DEBUG
	if (poly->getNext() != NULL) {
		printf("PolyList: Non-NULL next field in a Poly being appended is ignored\n");
	}
#endif
	poly->setNext(NULL);
	if (lastPoly != NULL) {
#ifdef MT32EMU_POLY_LIST_DEBUG
		if (lastPoly->getNext() != NULL) {
			printf("PolyList: Non-NULL next field in the lastPoly\n");
		}
#endif
		lastPoly->setNext(poly);
	}
	lastPoly = poly;
	if (firstPoly == NULL) {
		firstPoly = poly;
	}
}

Poly *PolyList::takeFirst() {
	Poly *oldFirst = firstPoly;
	firstPoly = oldFirst->getNext();
	if (firstPoly == NULL) {
#ifdef MT32EMU_POLY_LIST_DEBUG
		if (lastPoly != oldFirst) {
			printf("PolyList: firstPoly != lastPoly in a list with a single Poly\n");
		}
#endif
		lastPoly = NULL;
	}
	oldFirst->setNext(NULL);
	return oldFirst;
}

void PolyList::remove(Poly * const polyToRemove) {
	if (polyToRemove == firstPoly) {
		takeFirst();
		return;
	}
	for (Poly *poly = firstPoly; poly != NULL; poly = poly->getNext()) {
		if (poly->getNext() == polyToRemove) {
			if (polyToRemove == lastPoly) {
#ifdef MT32EMU_POLY_LIST_DEBUG
				if (lastPoly->getNext() != NULL) {
					printf("PolyList: Non-NULL next field in the lastPoly\n");
				}
#endif
				lastPoly = poly;
			}
			poly->setNext(polyToRemove->getNext());
			polyToRemove->setNext(NULL);
			break;
		}
	}
}

} // namespace MT32Emu

/************************************************** Partial.cpp ******************************************************/
namespace MT32Emu {

static const Bit8u PAN_NUMERATOR_MASTER[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7};
static const Bit8u PAN_NUMERATOR_SLAVE[]  = {0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7};

// We assume the pan is applied using the same 13-bit multiplier circuit that is also used for ring modulation
// because of the observed sample overflow, so the panSetting values are likely mapped in a similar way via a LUT.
// FIXME: Sample analysis suggests that the use of panSetting is linear, but there are some quirks that still need to be resolved.
static Bit32s getPanFactor(Bit32s panSetting) {
	static const Bit32u PAN_FACTORS_COUNT = 15;
	static Bit32s PAN_FACTORS[PAN_FACTORS_COUNT];
	static bool firstRun = true;

	if (firstRun) {
		firstRun = false;
		for (Bit32u i = 1; i < PAN_FACTORS_COUNT; i++) {
			PAN_FACTORS[i] = Bit32s(0.5 + i * 8192.0 / double(PAN_FACTORS_COUNT - 1));
		}
	}
	return PAN_FACTORS[panSetting];
}

Partial::Partial(Synth *useSynth, int usePartialIndex) :
	synth(useSynth), partialIndex(usePartialIndex), sampleNum(0),
	floatMode(useSynth->getSelectedRendererType() == RendererType_FLOAT) {
	// Initialisation of tva, tvp and tvf uses 'this' pointer
	// and thus should not be in the initializer list to avoid a compiler warning
	tva = new TVA(this, &ampRamp);
	tvp = new TVP(this);
	tvf = new TVF(this, &cutoffModifierRamp);
	ownerPart = -1;
	poly = NULL;
	pair = NULL;
	switch (synth->getSelectedRendererType()) {
	case RendererType_BIT16S:
		la32Pair = new LA32IntPartialPair;
		break;
	case RendererType_FLOAT:
		la32Pair = new LA32FloatPartialPair;
		break;
	default:
		la32Pair = NULL;
	}
}

Partial::~Partial() {
	delete la32Pair;
	delete tva;
	delete tvp;
	delete tvf;
}

// Only used for debugging purposes
int Partial::debugGetPartialNum() const {
	return partialIndex;
}

// Only used for debugging purposes
Bit32u Partial::debugGetSampleNum() const {
	return sampleNum;
}

int Partial::getOwnerPart() const {
	return ownerPart;
}

bool Partial::isActive() const {
	return ownerPart > -1;
}

const Poly *Partial::getPoly() const {
	return poly;
}

void Partial::activate(int part) {
	// This just marks the partial as being assigned to a part
	ownerPart = part;
}

void Partial::deactivate() {
	if (!isActive()) {
		return;
	}
	ownerPart = -1;
	synth->partialManager->partialDeactivated(partialIndex);
	if (poly != NULL) {
		poly->partialDeactivated(this);
	}
#if MT32EMU_MONITOR_PARTIALS > 2
	synth->printDebug("[+%lu] [Partial %d] Deactivated", sampleNum, partialIndex);
	synth->printPartialUsage(sampleNum);
#endif
	if (isRingModulatingSlave()) {
		pair->la32Pair->deactivate(LA32PartialPair::SLAVE);
	} else {
		la32Pair->deactivate(LA32PartialPair::MASTER);
		if (hasRingModulatingSlave()) {
			pair->deactivate();
			pair = NULL;
		}
	}
	if (pair != NULL) {
		pair->pair = NULL;
	}
}

void Partial::startPartial(const Part *part, Poly *usePoly, const PatchCache *usePatchCache, const MemParams::RhythmTemp *rhythmTemp, Partial *pairPartial) {
	if (usePoly == NULL || usePatchCache == NULL) {
		synth->printDebug("[Partial %d] *** Error: Starting partial for owner %d, usePoly=%s, usePatchCache=%s", partialIndex, ownerPart, usePoly == NULL ? "*** NULL ***" : "OK", usePatchCache == NULL ? "*** NULL ***" : "OK");
		return;
	}
	patchCache = usePatchCache;
	poly = usePoly;
	mixType = patchCache->structureMix;
	structurePosition = patchCache->structurePosition;

	Bit8u panSetting = rhythmTemp != NULL ? rhythmTemp->panpot : part->getPatchTemp()->panpot;
	if (mixType == 3) {
		if (structurePosition == 0) {
			panSetting = PAN_NUMERATOR_MASTER[panSetting] << 1;
		} else {
			panSetting = PAN_NUMERATOR_SLAVE[panSetting] << 1;
		}
		// Do a normal mix independent of any pair partial.
		mixType = 0;
		pairPartial = NULL;
	} else if (!synth->isNicePanningEnabled()) {
		// Mok wanted an option for smoother panning, and we love Mok.
		// CONFIRMED by Mok: exactly bytes like this (right shifted) are sent to the LA32.
		panSetting &= 0x0E;
	}

	leftPanValue = synth->reversedStereoEnabled ? 14 - panSetting : panSetting;
	rightPanValue = 14 - leftPanValue;

	if (!floatMode) {
		leftPanValue = getPanFactor(leftPanValue);
		rightPanValue = getPanFactor(rightPanValue);
	}

	// SEMI-CONFIRMED: From sample analysis:
	// Found that timbres with 3 or 4 partials (i.e. one using two partial pairs) are mixed in two different ways.
	// Either partial pairs are added or subtracted, it depends on how the partial pairs are allocated.
	// It seems that partials are grouped into quarters and if the partial pairs are allocated in different quarters the subtraction happens.
	// Though, this matters little for the majority of timbres, it becomes crucial for timbres which contain several partials that sound very close.
	// In this case that timbre can sound totally different depending on the way it is mixed up.
	// Most easily this effect can be displayed with the help of a special timbre consisting of several identical square wave partials (3 or 4).
	// Say, it is 3-partial timbre. Just play any two notes simultaneously and the polys very probably are mixed differently.
	// Moreover, the partial allocator retains the last partial assignment it did and all the subsequent notes will sound the same as the last released one.
	// The situation is better with 4-partial timbres since then a whole quarter is assigned for each poly. However, if a 3-partial timbre broke the normal
	// whole-quarter assignment or after some partials got aborted, even 4-partial timbres can be found sounding differently.
	// This behaviour is also confirmed with two more special timbres: one with identical sawtooth partials, and one with PCM wave 02.
	// For my personal taste, this behaviour rather enriches the sounding and should be emulated.
	if (!synth->isNicePartialMixingEnabled() && (partialIndex & 4)) {
		leftPanValue = -leftPanValue;
		rightPanValue = -rightPanValue;
	}

	if (patchCache->PCMPartial) {
		pcmNum = patchCache->pcm;
		if (synth->controlROMMap->pcmCount > 128) {
			// CM-32L, etc. support two "banks" of PCMs, selectable by waveform type parameter.
			if (patchCache->waveform > 1) {
				pcmNum += 128;
			}
		}
		pcmWave = &synth->pcmWaves[pcmNum];
	} else {
		pcmWave = NULL;
	}

	// CONFIRMED: pulseWidthVal calculation is based on information from Mok
	pulseWidthVal = (poly->getVelocity() - 64) * (patchCache->srcPartial.wg.pulseWidthVeloSensitivity - 7) + Tables::getInstance().pulseWidth100To255[patchCache->srcPartial.wg.pulseWidth];
	if (pulseWidthVal < 0) {
		pulseWidthVal = 0;
	} else if (pulseWidthVal > 255) {
		pulseWidthVal = 255;
	}

	pair = pairPartial;
	alreadyOutputed = false;
	tva->reset(part, patchCache->partialParam, rhythmTemp);
	tvp->reset(part, patchCache->partialParam);
	tvf->reset(patchCache->partialParam, tvp->getBasePitch());

	LA32PartialPair::PairType pairType;
	LA32PartialPair *useLA32Pair;
	if (isRingModulatingSlave()) {
		pairType = LA32PartialPair::SLAVE;
		useLA32Pair = pair->la32Pair;
	} else {
		pairType = LA32PartialPair::MASTER;
		la32Pair->init(hasRingModulatingSlave(), mixType == 1);
		useLA32Pair = la32Pair;
	}
	if (isPCM()) {
		useLA32Pair->initPCM(pairType, &synth->pcmROMData[pcmWave->addr], pcmWave->len, pcmWave->loop);
	} else {
		useLA32Pair->initSynth(pairType, (patchCache->waveform & 1) != 0, pulseWidthVal, patchCache->srcPartial.tvf.resonance + 1);
	}
	if (!hasRingModulatingSlave()) {
		la32Pair->deactivate(LA32PartialPair::SLAVE);
	}
}

Bit32u Partial::getAmpValue() {
	// SEMI-CONFIRMED: From sample analysis:
	// (1) Tested with a single partial playing PCM wave 77 with pitchCoarse 36 and no keyfollow, velocity follow, etc.
	// This gives results within +/- 2 at the output (before any DAC bitshifting)
	// when sustaining at levels 156 - 255 with no modifiers.
	// (2) Tested with a special square wave partial (internal capture ID tva5) at TVA envelope levels 155-255.
	// This gives deltas between -1 and 0 compared to the real output. Note that this special partial only produces
	// positive amps, so negative still needs to be explored, as well as lower levels.
	//
	// Also still partially unconfirmed is the behaviour when ramping between levels, as well as the timing.
	// TODO: The tests above were performed using the float model, to be refined
	Bit32u ampRampVal = 67117056 - ampRamp.nextValue();
	if (ampRamp.checkInterrupt()) {
		tva->handleInterrupt();
	}
	return ampRampVal;
}

Bit32u Partial::getCutoffValue() {
	if (isPCM()) {
		return 0;
	}
	Bit32u cutoffModifierRampVal = cutoffModifierRamp.nextValue();
	if (cutoffModifierRamp.checkInterrupt()) {
		tvf->handleInterrupt();
	}
	return (tvf->getBaseCutoff() << 18) + cutoffModifierRampVal;
}

bool Partial::hasRingModulatingSlave() const {
	return pair != NULL && structurePosition == 0 && (mixType == 1 || mixType == 2);
}

bool Partial::isRingModulatingSlave() const {
	return pair != NULL && structurePosition == 1 && (mixType == 1 || mixType == 2);
}

bool Partial::isRingModulatingNoMix() const {
	return pair != NULL && ((structurePosition == 1 && mixType == 1) || mixType == 2);
}

bool Partial::isPCM() const {
	return pcmWave != NULL;
}

const ControlROMPCMStruct *Partial::getControlROMPCMStruct() const {
	if (pcmWave != NULL) {
		return pcmWave->controlROMPCMStruct;
	}
	return NULL;
}

Synth *Partial::getSynth() const {
	return synth;
}

TVA *Partial::getTVA() const {
	return tva;
}

void Partial::backupCache(const PatchCache &cache) {
	if (patchCache == &cache) {
		cachebackup = cache;
		patchCache = &cachebackup;
	}
}

bool Partial::canProduceOutput() {
	if (!isActive() || alreadyOutputed || isRingModulatingSlave()) {
		return false;
	}
	if (poly == NULL) {
		synth->printDebug("[Partial %d] *** ERROR: poly is NULL at Partial::produceOutput()!", partialIndex);
		return false;
	}
	return true;
}

template <class LA32PairImpl>
bool Partial::generateNextSample(LA32PairImpl *la32PairImpl) {
	if (!tva->isPlaying() || !la32PairImpl->isActive(LA32PartialPair::MASTER)) {
		deactivate();
		return false;
	}
	la32PairImpl->generateNextSample(LA32PartialPair::MASTER, getAmpValue(), tvp->nextPitch(), getCutoffValue());
	if (hasRingModulatingSlave()) {
		la32PairImpl->generateNextSample(LA32PartialPair::SLAVE, pair->getAmpValue(), pair->tvp->nextPitch(), pair->getCutoffValue());
		if (!pair->tva->isPlaying() || !la32PairImpl->isActive(LA32PartialPair::SLAVE)) {
			pair->deactivate();
			if (mixType == 2) {
				deactivate();
				return false;
			}
		}
	}
	return true;
}

void Partial::produceAndMixSample(IntSample *&leftBuf, IntSample *&rightBuf, LA32IntPartialPair *la32IntPair) {
	IntSampleEx sample = la32IntPair->nextOutSample();

	// FIXME: LA32 may produce distorted sound in case if the absolute value of maximal amplitude of the input exceeds 8191
	// when the panning value is non-zero. Most probably the distortion occurs in the same way it does with ring modulation,
	// and it seems to be caused by limited precision of the common multiplication circuit.
	// From analysis of this overflow, it is obvious that the right channel output is actually found
	// by subtraction of the left channel output from the input.
	// Though, it is unknown whether this overflow is exploited somewhere.

	IntSampleEx leftOut = ((sample * leftPanValue) >> 13) + IntSampleEx(*leftBuf);
	IntSampleEx rightOut = ((sample * rightPanValue) >> 13) + IntSampleEx(*rightBuf);
	*(leftBuf++) = Synth::clipSampleEx(leftOut);
	*(rightBuf++) = Synth::clipSampleEx(rightOut);
}

void Partial::produceAndMixSample(FloatSample *&leftBuf, FloatSample *&rightBuf, LA32FloatPartialPair *la32FloatPair) {
	FloatSample sample = la32FloatPair->nextOutSample();
	FloatSample leftOut = (sample * leftPanValue) / 14.0f;
	FloatSample rightOut = (sample * rightPanValue) / 14.0f;
	*(leftBuf++) += leftOut;
	*(rightBuf++) += rightOut;
}

template <class Sample, class LA32PairImpl>
bool Partial::doProduceOutput(Sample *leftBuf, Sample *rightBuf, Bit32u length, LA32PairImpl *la32PairImpl) {
	if (!canProduceOutput()) return false;
	alreadyOutputed = true;

	for (sampleNum = 0; sampleNum < length; sampleNum++) {
		if (!generateNextSample(la32PairImpl)) break;
		produceAndMixSample(leftBuf, rightBuf, la32PairImpl);
	}
	sampleNum = 0;
	return true;
}

bool Partial::produceOutput(IntSample *leftBuf, IntSample *rightBuf, Bit32u length) {
	if (floatMode) {
		synth->printDebug("Partial: Invalid call to produceOutput()! Renderer = %d\n", synth->getSelectedRendererType());
		return false;
	}
	return doProduceOutput(leftBuf, rightBuf, length, static_cast<LA32IntPartialPair *>(la32Pair));
}

bool Partial::produceOutput(FloatSample *leftBuf, FloatSample *rightBuf, Bit32u length) {
	if (!floatMode) {
		synth->printDebug("Partial: Invalid call to produceOutput()! Renderer = %d\n", synth->getSelectedRendererType());
		return false;
	}
	return doProduceOutput(leftBuf, rightBuf, length, static_cast<LA32FloatPartialPair *>(la32Pair));
}

bool Partial::shouldReverb() {
	if (!isActive()) {
		return false;
	}
	return patchCache->reverb;
}

void Partial::startAbort() {
	// This is called when the partial manager needs to terminate partials for re-use by a new Poly.
	tva->startAbort();
}

void Partial::startDecayAll() {
	tva->startDecay();
	tvp->startDecay();
	tvf->startDecay();
}

} // namespace MT32Emu

/************************************************** LA32Ramp.cpp ******************************************************/
/*
Some notes on this class:

This emulates the LA-32's implementation of "ramps". A ramp in this context is a smooth transition from one value to another, handled entirely within the LA-32.
The LA-32 provides this feature for amplitude and filter cutoff values.

The 8095 starts ramps on the LA-32 by setting two values in memory-mapped registers:

(1) The target value (between 0 and 255) for the ramp to end on. This is represented by the "target" argument to startRamp().
(2) The speed at which that value should be approached. This is represented by the "increment" argument to startRamp().

Once the ramp target value has been hit, the LA-32 raises an interrupt.

Note that the starting point of the ramp is whatever internal value the LA-32 had when the registers were set. This is usually the end point of a previously completed ramp.

Our handling of the "target" and "increment" values is based on sample analysis and a little guesswork.
Here's what we're pretty confident about:
 - The most significant bit of "increment" indicates the direction that the LA32's current internal value ("current" in our emulation) should change in.
   Set means downward, clear means upward.
 - The lower 7 bits of "increment" indicate how quickly "current" should be changed.
 - If "increment" is 0, no change to "current" is made and no interrupt is raised. [SEMI-CONFIRMED by sample analysis]
 - Otherwise, if the MSb is set:
    - If "current" already corresponds to a value <= "target", "current" is set immediately to the equivalent of "target" and an interrupt is raised.
    - Otherwise, "current" is gradually reduced (at a rate determined by the lower 7 bits of "increment"), and once it reaches the equivalent of "target" an interrupt is raised.
 - Otherwise (the MSb is unset):
    - If "current" already corresponds to a value >= "target", "current" is set immediately to the equivalent of "target" and an interrupt is raised.
    - Otherwise, "current" is gradually increased (at a rate determined by the lower 7 bits of "increment"), and once it reaches the equivalent of "target" an interrupt is raised.

We haven't fully explored:
 - Values when ramping between levels (though this is probably correct).
 - Transition timing (may not be 100% accurate, especially for very fast ramps).
*/

namespace MT32Emu {

// SEMI-CONFIRMED from sample analysis.
const unsigned int TARGET_SHIFTS = 18;
const unsigned int MAX_CURRENT = 0xFF << TARGET_SHIFTS;

// We simulate the delay in handling "target was reached" interrupts by waiting
// this many samples before setting interruptRaised.
// FIXME: This should vary with the sample rate, but doesn't.
// SEMI-CONFIRMED: Since this involves asynchronous activity between the LA32
// and the 8095, a good value is hard to pin down.
// This one matches observed behaviour on a few digital captures I had handy,
// and should be double-checked. We may also need a more sophisticated delay
// scheme eventually.
const int INTERRUPT_TIME = 7;

LA32Ramp::LA32Ramp() :
	current(0),
	largeTarget(0),
	largeIncrement(0),
	interruptCountdown(0),
	interruptRaised(false) {
}

void LA32Ramp::startRamp(Bit8u target, Bit8u increment) {
	// CONFIRMED: From sample analysis, this appears to be very accurate.
	if (increment == 0) {
		largeIncrement = 0;
	} else {
		// Three bits in the fractional part, no need to interpolate
		// (unsigned int)(EXP2F(((increment & 0x7F) + 24) / 8.0f) + 0.125f)
		Bit32u expArg = increment & 0x7F;
		largeIncrement = 8191 - Tables::getInstance().exp9[~(expArg << 6) & 511];
		largeIncrement <<= expArg >> 3;
		largeIncrement += 64;
		largeIncrement >>= 9;
	}
	descending = (increment & 0x80) != 0;
	if (descending) {
		// CONFIRMED: From sample analysis, descending increments are slightly faster
		largeIncrement++;
	}

	largeTarget = target << TARGET_SHIFTS;
	interruptCountdown = 0;
	interruptRaised = false;
}

Bit32u LA32Ramp::nextValue() {
	if (interruptCountdown > 0) {
		if (--interruptCountdown == 0) {
			interruptRaised = true;
		}
	} else if (largeIncrement != 0) {
		// CONFIRMED from sample analysis: When increment is 0, the LA32 does *not* change the current value at all (and of course doesn't fire an interrupt).
		if (descending) {
			// Lowering current value
			if (largeIncrement > current) {
				current = largeTarget;
				interruptCountdown = INTERRUPT_TIME;
			} else {
				current -= largeIncrement;
				if (current <= largeTarget) {
					current = largeTarget;
					interruptCountdown = INTERRUPT_TIME;
				}
			}
		} else {
			// Raising current value
			if (MAX_CURRENT - current < largeIncrement) {
				current = largeTarget;
				interruptCountdown = INTERRUPT_TIME;
			} else {
				current += largeIncrement;
				if (current >= largeTarget) {
					current = largeTarget;
					interruptCountdown = INTERRUPT_TIME;
				}
			}
		}
	}
	return current;
}

bool LA32Ramp::checkInterrupt() {
	bool wasRaised = interruptRaised;
	interruptRaised = false;
	return wasRaised;
}

void LA32Ramp::reset() {
	current = 0;
	largeTarget = 0;
	largeIncrement = 0;
	descending = false;
	interruptCountdown = 0;
	interruptRaised = false;
}

// This is actually beyond the LA32 ramp interface.
// Instead of polling the current value, MCU receives an interrupt when a ramp completes.
// However, this is a simple way to work around the specific behaviour of TVA
// when in sustain phase which one normally wants to avoid.
// See TVA::recalcSustain() for details.
bool LA32Ramp::isBelowCurrent(Bit8u target) const {
	return Bit32u(target << TARGET_SHIFTS) < current;
}

} // namespace MT32Emu

/************************************************** LA32WaveGenerator.cpp ******************************************************/
#define MIDDLE_CUTOFF_VALUE                    INT_MIDDLE_CUTOFF_VALUE
#define RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE INT_RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE
#define MAX_CUTOFF_VALUE                       INT_MAX_CUTOFF_VALUE
#define produceDistortedSample                 INT_produceDistortedSample

namespace MT32Emu {

static const Bit32u SINE_SEGMENT_RELATIVE_LENGTH = 1 << 18;
static const Bit32u MIDDLE_CUTOFF_VALUE = 128 << 18;
static const Bit32u RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE = 144 << 18;
static const Bit32u MAX_CUTOFF_VALUE = 240 << 18;
static const LogSample SILENCE = {65535, LogSample::POSITIVE};

Bit16u LA32Utilites::interpolateExp(const Bit16u fract) {
	Bit16u expTabIndex = fract >> 3;
	Bit16u extraBits = ~fract & 7;
	Bit16u expTabEntry2 = 8191 - Tables::getInstance().exp9[expTabIndex];
	Bit16u expTabEntry1 = expTabIndex == 0 ? 8191 : (8191 - Tables::getInstance().exp9[expTabIndex - 1]);
	return expTabEntry2 + (((expTabEntry1 - expTabEntry2) * extraBits) >> 3);
}

Bit16s LA32Utilites::unlog(const LogSample &logSample) {
	//Bit16s sample = (Bit16s)EXP2F(13.0f - logSample.logValue / 1024.0f);
	Bit32u intLogValue = logSample.logValue >> 12;
	Bit16u fracLogValue = logSample.logValue & 4095;
	Bit16s sample = interpolateExp(fracLogValue) >> intLogValue;
	return logSample.sign == LogSample::POSITIVE ? sample : -sample;
}

void LA32Utilites::addLogSamples(LogSample &logSample1, const LogSample &logSample2) {
	Bit32u logSampleValue = logSample1.logValue + logSample2.logValue;
	logSample1.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	logSample1.sign = logSample1.sign == logSample2.sign ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

Bit32u LA32WaveGenerator::getSampleStep() {
	// sampleStep = EXP2F(pitch / 4096.0f + 4.0f)
	Bit32u sampleStep = LA32Utilites::interpolateExp(~pitch & 4095);
	sampleStep <<= pitch >> 12;
	sampleStep >>= 8;
	sampleStep &= ~1;
	return sampleStep;
}

Bit32u LA32WaveGenerator::getResonanceWaveLengthFactor(Bit32u effectiveCutoffValue) {
	// resonanceWaveLengthFactor = (Bit32u)EXP2F(12.0f + effectiveCutoffValue / 4096.0f);
	Bit32u resonanceWaveLengthFactor = LA32Utilites::interpolateExp(~effectiveCutoffValue & 4095);
	resonanceWaveLengthFactor <<= effectiveCutoffValue >> 12;
	return resonanceWaveLengthFactor;
}

Bit32u LA32WaveGenerator::getHighLinearLength(Bit32u effectiveCutoffValue) {
	// Ratio of positive segment to wave length
	Bit32u effectivePulseWidthValue = 0;
	if (pulseWidth > 128) {
		effectivePulseWidthValue = (pulseWidth - 128) << 6;
	}

	Bit32u highLinearLength = 0;
	// highLinearLength = EXP2F(19.0f - effectivePulseWidthValue / 4096.0f + effectiveCutoffValue / 4096.0f) - 2 * SINE_SEGMENT_RELATIVE_LENGTH;
	if (effectivePulseWidthValue < effectiveCutoffValue) {
		Bit32u expArg = effectiveCutoffValue - effectivePulseWidthValue;
		highLinearLength = LA32Utilites::interpolateExp(~expArg & 4095);
		highLinearLength <<= 7 + (expArg >> 12);
		highLinearLength -= 2 * SINE_SEGMENT_RELATIVE_LENGTH;
	}
	return highLinearLength;
}

void LA32WaveGenerator::computePositions(Bit32u highLinearLength, Bit32u lowLinearLength, Bit32u resonanceWaveLengthFactor) {
	// Assuming 12-bit multiplication used here
	squareWavePosition = resonanceSinePosition = (wavePosition >> 8) * (resonanceWaveLengthFactor >> 4);
	if (squareWavePosition < SINE_SEGMENT_RELATIVE_LENGTH) {
		phase = POSITIVE_RISING_SINE_SEGMENT;
		return;
	}
	squareWavePosition -= SINE_SEGMENT_RELATIVE_LENGTH;
	if (squareWavePosition < highLinearLength) {
		phase = POSITIVE_LINEAR_SEGMENT;
		return;
	}
	squareWavePosition -= highLinearLength;
	if (squareWavePosition < SINE_SEGMENT_RELATIVE_LENGTH) {
		phase = POSITIVE_FALLING_SINE_SEGMENT;
		return;
	}
	squareWavePosition -= SINE_SEGMENT_RELATIVE_LENGTH;
	resonanceSinePosition = squareWavePosition;
	if (squareWavePosition < SINE_SEGMENT_RELATIVE_LENGTH) {
		phase = NEGATIVE_FALLING_SINE_SEGMENT;
		return;
	}
	squareWavePosition -= SINE_SEGMENT_RELATIVE_LENGTH;
	if (squareWavePosition < lowLinearLength) {
		phase = NEGATIVE_LINEAR_SEGMENT;
		return;
	}
	squareWavePosition -= lowLinearLength;
	phase = NEGATIVE_RISING_SINE_SEGMENT;
}

void LA32WaveGenerator::advancePosition() {
	wavePosition += getSampleStep();
	wavePosition %= 4 * SINE_SEGMENT_RELATIVE_LENGTH;

	Bit32u effectiveCutoffValue = (cutoffVal > MIDDLE_CUTOFF_VALUE) ? (cutoffVal - MIDDLE_CUTOFF_VALUE) >> 10 : 0;
	Bit32u resonanceWaveLengthFactor = getResonanceWaveLengthFactor(effectiveCutoffValue);
	Bit32u highLinearLength = getHighLinearLength(effectiveCutoffValue);
	Bit32u lowLinearLength = (resonanceWaveLengthFactor << 8) - 4 * SINE_SEGMENT_RELATIVE_LENGTH - highLinearLength;
	computePositions(highLinearLength, lowLinearLength, resonanceWaveLengthFactor);

	resonancePhase = ResonancePhase(((resonanceSinePosition >> 18) + (phase > POSITIVE_FALLING_SINE_SEGMENT ? 2 : 0)) & 3);
}

void LA32WaveGenerator::generateNextSquareWaveLogSample() {
	Bit32u logSampleValue;
	switch (phase) {
		case POSITIVE_RISING_SINE_SEGMENT:
		case NEGATIVE_FALLING_SINE_SEGMENT:
			logSampleValue = Tables::getInstance().logsin9[(squareWavePosition >> 9) & 511];
			break;
		case POSITIVE_FALLING_SINE_SEGMENT:
		case NEGATIVE_RISING_SINE_SEGMENT:
			logSampleValue = Tables::getInstance().logsin9[~(squareWavePosition >> 9) & 511];
			break;
		case POSITIVE_LINEAR_SEGMENT:
		case NEGATIVE_LINEAR_SEGMENT:
		default:
			logSampleValue = 0;
			break;
	}
	logSampleValue <<= 2;
	logSampleValue += amp >> 10;
	if (cutoffVal < MIDDLE_CUTOFF_VALUE) {
		logSampleValue += (MIDDLE_CUTOFF_VALUE - cutoffVal) >> 9;
	}

	squareLogSample.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	squareLogSample.sign = phase < NEGATIVE_FALLING_SINE_SEGMENT ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

void LA32WaveGenerator::generateNextResonanceWaveLogSample() {
	Bit32u logSampleValue;
	if (resonancePhase == POSITIVE_FALLING_RESONANCE_SINE_SEGMENT || resonancePhase == NEGATIVE_RISING_RESONANCE_SINE_SEGMENT) {
		logSampleValue = Tables::getInstance().logsin9[~(resonanceSinePosition >> 9) & 511];
	} else {
		logSampleValue = Tables::getInstance().logsin9[(resonanceSinePosition >> 9) & 511];
	}
	logSampleValue <<= 2;
	logSampleValue += amp >> 10;

	// From the digital captures, the decaying speed of the resonance sine is found a bit different for the positive and the negative segments
	Bit32u decayFactor = phase < NEGATIVE_FALLING_SINE_SEGMENT ? resAmpDecayFactor : resAmpDecayFactor + 1;
	// Unsure about resonanceSinePosition here. It's possible that dedicated counter & decrement are used. Although, cutoff is finely ramped, so maybe not.
	logSampleValue += resonanceAmpSubtraction + (((resonanceSinePosition >> 4) * decayFactor) >> 8);

	// To ensure the output wave has no breaks, two different windows are applied to the beginning and the ending of the resonance sine segment
	if (phase == POSITIVE_RISING_SINE_SEGMENT || phase == NEGATIVE_FALLING_SINE_SEGMENT) {
		// The window is synchronous sine here
		logSampleValue += Tables::getInstance().logsin9[(squareWavePosition >> 9) & 511] << 2;
	} else if (phase == POSITIVE_FALLING_SINE_SEGMENT || phase == NEGATIVE_RISING_SINE_SEGMENT) {
		// The window is synchronous square sine here
		logSampleValue += Tables::getInstance().logsin9[~(squareWavePosition >> 9) & 511] << 3;
	}

	if (cutoffVal < MIDDLE_CUTOFF_VALUE) {
		// For the cutoff values below the cutoff middle point, it seems the amp of the resonance wave is exponentially decayed
		logSampleValue += 31743 + ((MIDDLE_CUTOFF_VALUE - cutoffVal) >> 9);
	} else if (cutoffVal < RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE) {
		// For the cutoff values below this point, the amp of the resonance wave is sinusoidally decayed
		Bit32u sineIx = (cutoffVal - MIDDLE_CUTOFF_VALUE) >> 13;
		logSampleValue += Tables::getInstance().logsin9[sineIx] << 2;
	}

	// After all the amp decrements are added, it should be safe now to adjust the amp of the resonance wave to what we see on captures
	logSampleValue -= 1 << 12;

	resonanceLogSample.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	resonanceLogSample.sign = resonancePhase < NEGATIVE_FALLING_RESONANCE_SINE_SEGMENT ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

void LA32WaveGenerator::generateNextSawtoothCosineLogSample(LogSample &logSample) const {
	Bit32u sawtoothCosinePosition = wavePosition + (1 << 18);
	if ((sawtoothCosinePosition & (1 << 18)) > 0) {
		logSample.logValue = Tables::getInstance().logsin9[~(sawtoothCosinePosition >> 9) & 511];
	} else {
		logSample.logValue = Tables::getInstance().logsin9[(sawtoothCosinePosition >> 9) & 511];
	}
	logSample.logValue <<= 2;
	logSample.sign = ((sawtoothCosinePosition & (1 << 19)) == 0) ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

void LA32WaveGenerator::pcmSampleToLogSample(LogSample &logSample, const Bit16s pcmSample) const {
	Bit32u logSampleValue = (32787 - (pcmSample & 32767)) << 1;
	logSampleValue += amp >> 10;
	logSample.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	logSample.sign = pcmSample < 0 ? LogSample::NEGATIVE : LogSample::POSITIVE;
}

void LA32WaveGenerator::generateNextPCMWaveLogSamples() {
	// This should emulate the ladder we see in the PCM captures for pitches 01, 02, 07, etc.
	// The most probable cause is the factor in the interpolation formula is one bit less
	// accurate than the sample position counter
	pcmInterpolationFactor = (wavePosition & 255) >> 1;
	Bit32u pcmWaveTableIx = wavePosition >> 8;
	pcmSampleToLogSample(firstPCMLogSample, pcmWaveAddress[pcmWaveTableIx]);
	if (pcmWaveInterpolated) {
		pcmWaveTableIx++;
		if (pcmWaveTableIx < pcmWaveLength) {
			pcmSampleToLogSample(secondPCMLogSample, pcmWaveAddress[pcmWaveTableIx]);
		} else {
			if (pcmWaveLooped) {
				pcmWaveTableIx -= pcmWaveLength;
				pcmSampleToLogSample(secondPCMLogSample, pcmWaveAddress[pcmWaveTableIx]);
			} else {
				secondPCMLogSample = SILENCE;
			}
		}
	} else {
		secondPCMLogSample = SILENCE;
	}
	// pcmSampleStep = (Bit32u)EXP2F(pitch / 4096.0f + 3.0f);
	Bit32u pcmSampleStep = LA32Utilites::interpolateExp(~pitch & 4095);
	pcmSampleStep <<= pitch >> 12;
	// Seeing the actual lengths of the PCM wave for pitches 00..12,
	// the pcmPosition counter can be assumed to have 8-bit fractions
	pcmSampleStep >>= 9;
	wavePosition += pcmSampleStep;
	if (wavePosition >= (pcmWaveLength << 8)) {
		if (pcmWaveLooped) {
			wavePosition -= pcmWaveLength << 8;
		} else {
			deactivate();
		}
	}
}

void LA32WaveGenerator::initSynth(const bool useSawtoothWaveform, const Bit8u usePulseWidth, const Bit8u useResonance) {
	sawtoothWaveform = useSawtoothWaveform;
	pulseWidth = usePulseWidth;
	resonance = useResonance;

	wavePosition = 0;

	squareWavePosition = 0;
	phase = POSITIVE_RISING_SINE_SEGMENT;

	resonanceSinePosition = 0;
	resonancePhase = POSITIVE_RISING_RESONANCE_SINE_SEGMENT;
	resonanceAmpSubtraction = (32 - resonance) << 10;
	resAmpDecayFactor = Tables::getInstance().resAmpDecayFactor[resonance >> 2] << 2;

	pcmWaveAddress = NULL;
	active = true;
}

void LA32WaveGenerator::initPCM(const Bit16s * const usePCMWaveAddress, const Bit32u usePCMWaveLength, const bool usePCMWaveLooped, const bool usePCMWaveInterpolated) {
	pcmWaveAddress = usePCMWaveAddress;
	pcmWaveLength = usePCMWaveLength;
	pcmWaveLooped = usePCMWaveLooped;
	pcmWaveInterpolated = usePCMWaveInterpolated;

	wavePosition = 0;
	active = true;
}

void LA32WaveGenerator::generateNextSample(const Bit32u useAmp, const Bit16u usePitch, const Bit32u useCutoffVal) {
	if (!active) {
		return;
	}

	amp = useAmp;
	pitch = usePitch;

	if (isPCMWave()) {
		generateNextPCMWaveLogSamples();
		return;
	}

	// The 240 cutoffVal limit was determined via sample analysis (internal Munt capture IDs: glop3, glop4).
	// More research is needed to be sure that this is correct, however.
	cutoffVal = (useCutoffVal > MAX_CUTOFF_VALUE) ? MAX_CUTOFF_VALUE : useCutoffVal;

	generateNextSquareWaveLogSample();
	generateNextResonanceWaveLogSample();
	if (sawtoothWaveform) {
		LogSample cosineLogSample;
		generateNextSawtoothCosineLogSample(cosineLogSample);
		LA32Utilites::addLogSamples(squareLogSample, cosineLogSample);
		LA32Utilites::addLogSamples(resonanceLogSample, cosineLogSample);
	}
	advancePosition();
}

LogSample LA32WaveGenerator::getOutputLogSample(const bool first) const {
	if (!isActive()) {
		return SILENCE;
	}
	if (isPCMWave()) {
		return first ? firstPCMLogSample : secondPCMLogSample;
	}
	return first ? squareLogSample : resonanceLogSample;
}

void LA32WaveGenerator::deactivate() {
	active = false;
}

bool LA32WaveGenerator::isActive() const {
	return active;
}

bool LA32WaveGenerator::isPCMWave() const {
	return pcmWaveAddress != NULL;
}

Bit32u LA32WaveGenerator::getPCMInterpolationFactor() const {
	return pcmInterpolationFactor;
}

void LA32IntPartialPair::init(const bool useRingModulated, const bool useMixed) {
	ringModulated = useRingModulated;
	mixed = useMixed;
}

void LA32IntPartialPair::initSynth(const PairType useMaster, const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance) {
	if (useMaster == MASTER) {
		master.initSynth(sawtoothWaveform, pulseWidth, resonance);
	} else {
		slave.initSynth(sawtoothWaveform, pulseWidth, resonance);
	}
}

void LA32IntPartialPair::initPCM(const PairType useMaster, const Bit16s *pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped) {
	if (useMaster == MASTER) {
		master.initPCM(pcmWaveAddress, pcmWaveLength, pcmWaveLooped, true);
	} else {
		slave.initPCM(pcmWaveAddress, pcmWaveLength, pcmWaveLooped, !ringModulated);
	}
}

void LA32IntPartialPair::generateNextSample(const PairType useMaster, const Bit32u amp, const Bit16u pitch, const Bit32u cutoff) {
	if (useMaster == MASTER) {
		master.generateNextSample(amp, pitch, cutoff);
	} else {
		slave.generateNextSample(amp, pitch, cutoff);
	}
}

Bit16s LA32IntPartialPair::unlogAndMixWGOutput(const LA32WaveGenerator &wg) {
	if (!wg.isActive()) {
		return 0;
	}
	Bit16s firstSample = LA32Utilites::unlog(wg.getOutputLogSample(true));
	Bit16s secondSample = LA32Utilites::unlog(wg.getOutputLogSample(false));
	if (wg.isPCMWave()) {
		return Bit16s(firstSample + (((Bit32s(secondSample) - Bit32s(firstSample)) * wg.getPCMInterpolationFactor()) >> 7));
	}
	return firstSample + secondSample;
}

static inline Bit16s produceDistortedSample(Bit16s sample) {
	return ((sample & 0x2000) == 0) ? Bit16s(sample & 0x1fff) : Bit16s(sample | ~0x1fff);
}

Bit16s LA32IntPartialPair::nextOutSample() {
	if (!ringModulated) {
		return unlogAndMixWGOutput(master) + unlogAndMixWGOutput(slave);
	}

	Bit16s masterSample = unlogAndMixWGOutput(master); // Store master partial sample for further mixing

	/* SEMI-CONFIRMED from sample analysis:
	 * We observe that for partial structures with ring modulation the interpolation is not applied to the slave PCM partial.
	 * It's assumed that the multiplication circuitry intended to perform the interpolation on the slave PCM partial
	 * is borrowed by the ring modulation circuit (or the LA32 chip has a similar lack of resources assigned to each partial pair).
	 */
	Bit16s slaveSample = slave.isPCMWave() ? LA32Utilites::unlog(slave.getOutputLogSample(true)) : unlogAndMixWGOutput(slave);

	/* SEMI-CONFIRMED: Ring modulation model derived from sample analysis of specially constructed patches which exploit distortion.
	 * LA32 ring modulator found to produce distorted output in case if the absolute value of maximal amplitude of one of the input partials exceeds 8191.
	 * This is easy to reproduce using synth partials with resonance values close to the maximum. It looks like an integer overflow happens in this case.
	 * As the distortion is strictly bound to the amplitude of the complete mixed square + resonance wave in the linear space,
	 * it is reasonable to assume the ring modulation is performed also in the linear space by sample multiplication.
	 * Most probably the overflow is caused by limited precision of the multiplication circuit as the very similar distortion occurs with panning.
	 */
	Bit16s ringModulatedSample = Bit16s((Bit32s(produceDistortedSample(masterSample)) * Bit32s(produceDistortedSample(slaveSample))) >> 13);

	return mixed ? masterSample + ringModulatedSample : ringModulatedSample;
}

void LA32IntPartialPair::deactivate(const PairType useMaster) {
	if (useMaster == MASTER) {
		master.deactivate();
	} else {
		slave.deactivate();
	}
}

bool LA32IntPartialPair::isActive(const PairType useMaster) const {
	return useMaster == MASTER ? master.isActive() : slave.isActive();
}

} // namespace MT32Emu
#undef MIDDLE_CUTOFF_VALUE
#undef RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE
#undef MAX_CUTOFF_VALUE
#undef produceDistortedSample

/************************************************** LA32FloatWaveGenerator.cpp ******************************************************/
#define MIDDLE_CUTOFF_VALUE                    FLOAT_MIDDLE_CUTOFF_VALUE
#define RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE FLOAT_RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE
#define MAX_CUTOFF_VALUE                       FLOAT_MAX_CUTOFF_VALUE
#define produceDistortedSample                 FLOAT_produceDistortedSample

namespace MT32Emu {

static const float MIDDLE_CUTOFF_VALUE = 128.0f;
static const float RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE = 144.0f;
static const float MAX_CUTOFF_VALUE = 240.0f;

float LA32FloatWaveGenerator::getPCMSample(unsigned int position) {
	if (position >= pcmWaveLength) {
		if (!pcmWaveLooped) {
			return 0;
		}
		position = position % pcmWaveLength;
	}
	Bit16s pcmSample = pcmWaveAddress[position];
	float sampleValue = EXP2F(((pcmSample & 32767) - 32787.0f) / 2048.0f);
	return ((pcmSample & 32768) == 0) ? sampleValue : -sampleValue;
}

void LA32FloatWaveGenerator::initSynth(const bool useSawtoothWaveform, const Bit8u usePulseWidth, const Bit8u useResonance) {
	sawtoothWaveform = useSawtoothWaveform;
	pulseWidth = usePulseWidth;
	resonance = useResonance;

	wavePos = 0.0f;
	lastFreq = 0.0f;

	pcmWaveAddress = NULL;
	active = true;
}

void LA32FloatWaveGenerator::initPCM(const Bit16s * const usePCMWaveAddress, const Bit32u usePCMWaveLength, const bool usePCMWaveLooped, const bool usePCMWaveInterpolated) {
	pcmWaveAddress = usePCMWaveAddress;
	pcmWaveLength = usePCMWaveLength;
	pcmWaveLooped = usePCMWaveLooped;
	pcmWaveInterpolated = usePCMWaveInterpolated;

	pcmPosition = 0.0f;
	active = true;
}

// ampVal - Logarithmic amp of the wave generator
// pitch - Logarithmic frequency of the resulting wave
// cutoffRampVal - Composed of the base cutoff in range [78..178] left-shifted by 18 bits and the TVF modifier
float LA32FloatWaveGenerator::generateNextSample(const Bit32u ampVal, const Bit16u pitch, const Bit32u cutoffRampVal) {
	if (!active) {
		return 0.0f;
	}

	float sample = 0.0f;

	// SEMI-CONFIRMED: From sample analysis:
	// (1) Tested with a single partial playing PCM wave 77 with pitchCoarse 36 and no keyfollow, velocity follow, etc.
	// This gives results within +/- 2 at the output (before any DAC bitshifting)
	// when sustaining at levels 156 - 255 with no modifiers.
	// (2) Tested with a special square wave partial (internal capture ID tva5) at TVA envelope levels 155-255.
	// This gives deltas between -1 and 0 compared to the real output. Note that this special partial only produces
	// positive amps, so negative still needs to be explored, as well as lower levels.
	//
	// Also still partially unconfirmed is the behaviour when ramping between levels, as well as the timing.

	float amp = EXP2F(ampVal / -1024.0f / 4096.0f);
	float freq = EXP2F(pitch / 4096.0f - 16.0f) * SAMPLE_RATE;

	if (isPCMWave()) {
		// Render PCM waveform
		int len = pcmWaveLength;
		int intPCMPosition = int(pcmPosition);
		if (intPCMPosition >= len && !pcmWaveLooped) {
			// We're now past the end of a non-looping PCM waveform so it's time to die.
			deactivate();
			return 0.0f;
		}
		float positionDelta = freq * 2048.0f / SAMPLE_RATE;

		// Linear interpolation
		float firstSample = getPCMSample(intPCMPosition);
		// We observe that for partial structures with ring modulation the interpolation is not applied to the slave PCM partial.
		// It's assumed that the multiplication circuitry intended to perform the interpolation on the slave PCM partial
		// is borrowed by the ring modulation circuit (or the LA32 chip has a similar lack of resources assigned to each partial pair).
		if (pcmWaveInterpolated) {
			sample = firstSample + (getPCMSample(intPCMPosition + 1) - firstSample) * (pcmPosition - intPCMPosition);
		} else {
			sample = firstSample;
		}

		float newPCMPosition = pcmPosition + positionDelta;
		if (pcmWaveLooped) {
			newPCMPosition = fmod(newPCMPosition, float(pcmWaveLength));
		}
		pcmPosition = newPCMPosition;
	} else {
		// Render synthesised waveform
		wavePos *= lastFreq / freq;
		lastFreq = freq;

		float resAmp = EXP2F(1.0f - (32 - resonance) / 4.0f);
		{
			//static const float resAmpFactor = EXP2F(-7);
			//resAmp = EXP2I(resonance << 10) * resAmpFactor;
		}

		// The cutoffModifier may not be supposed to be directly added to the cutoff -
		// it may for example need to be multiplied in some way.
		// The 240 cutoffVal limit was determined via sample analysis (internal Munt capture IDs: glop3, glop4).
		// More research is needed to be sure that this is correct, however.
		float cutoffVal = cutoffRampVal / 262144.0f;
		if (cutoffVal > MAX_CUTOFF_VALUE) {
			cutoffVal = MAX_CUTOFF_VALUE;
		}

		// Wave length in samples
		float waveLen = SAMPLE_RATE / freq;

		// Init cosineLen
		float cosineLen = 0.5f * waveLen;
		if (cutoffVal > MIDDLE_CUTOFF_VALUE) {
			cosineLen *= EXP2F((cutoffVal - MIDDLE_CUTOFF_VALUE) / -16.0f); // found from sample analysis
		}

		// Start playing in center of first cosine segment
		// relWavePos is shifted by a half of cosineLen
		float relWavePos = wavePos + 0.5f * cosineLen;
		if (relWavePos > waveLen) {
			relWavePos -= waveLen;
		}

		// Ratio of positive segment to wave length
		float pulseLen = 0.5f;
		if (pulseWidth > 128) {
			pulseLen = EXP2F((64 - pulseWidth) / 64.0f);
			//static const float pulseLenFactor = EXP2F(-192 / 64);
			//pulseLen = EXP2I((256 - pulseWidthVal) << 6) * pulseLenFactor;
		}
		pulseLen *= waveLen;

		float hLen = pulseLen - cosineLen;

		// Ignore pulsewidths too high for given freq
		if (hLen < 0.0f) {
			hLen = 0.0f;
		}

		// Correct resAmp for cutoff in range 50..66
		if ((cutoffVal >= MIDDLE_CUTOFF_VALUE) && (cutoffVal < RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE)) {
			resAmp *= sin(FLOAT_PI * (cutoffVal - MIDDLE_CUTOFF_VALUE) / 32.0f);
		}

		// Produce filtered square wave with 2 cosine waves on slopes

		// 1st cosine segment
		if (relWavePos < cosineLen) {
			sample = -cos(FLOAT_PI * relWavePos / cosineLen);
		} else

		// high linear segment
		if (relWavePos < (cosineLen + hLen)) {
			sample = 1.f;
		} else

		// 2nd cosine segment
		if (relWavePos < (2 * cosineLen + hLen)) {
			sample = cos(FLOAT_PI * (relWavePos - (cosineLen + hLen)) / cosineLen);
		} else {

		// low linear segment
			sample = -1.f;
		}

		if (cutoffVal < MIDDLE_CUTOFF_VALUE) {

			// Attenuate samples below cutoff 50
			// Found by sample analysis
			sample *= EXP2F(-0.125f * (MIDDLE_CUTOFF_VALUE - cutoffVal));
		} else {

			// Add resonance sine. Effective for cutoff > 50 only
			float resSample = 1.0f;

			// Resonance decay speed factor
			float resAmpDecayFactor = Tables::getInstance().resAmpDecayFactor[resonance >> 2];

			// Now relWavePos counts from the middle of first cosine
			relWavePos = wavePos;

			// negative segments
			if (!(relWavePos < (cosineLen + hLen))) {
				resSample = -resSample;
				relWavePos -= cosineLen + hLen;

				// From the digital captures, the decaying speed of the resonance sine is found a bit different for the positive and the negative segments
				resAmpDecayFactor += 0.25f;
			}

			// Resonance sine WG
			resSample *= sin(FLOAT_PI * relWavePos / cosineLen);

			// Resonance sine amp
			float resAmpFadeLog2 = -0.125f * resAmpDecayFactor * (relWavePos / cosineLen); // seems to be exact
			float resAmpFade = EXP2F(resAmpFadeLog2);

			// Now relWavePos set negative to the left from center of any cosine
			relWavePos = wavePos;

			// negative segment
			if (!(wavePos < (waveLen - 0.5f * cosineLen))) {
				relWavePos -= waveLen;
			} else

			// positive segment
			if (!(wavePos < (hLen + 0.5f * cosineLen))) {
				relWavePos -= cosineLen + hLen;
			}

			// To ensure the output wave has no breaks, two different windows are applied to the beginning and the ending of the resonance sine segment
			if (relWavePos < 0.5f * cosineLen) {
				float syncSine = sin(FLOAT_PI * relWavePos / cosineLen);
				if (relWavePos < 0.0f) {
					// The window is synchronous square sine here
					resAmpFade *= syncSine * syncSine;
				} else {
					// The window is synchronous sine here
					resAmpFade *= syncSine;
				}
			}

			sample += resSample * resAmp * resAmpFade;
		}

		// sawtooth waves
		if (sawtoothWaveform) {
			sample *= cos(FLOAT_2PI * wavePos / waveLen);
		}

		wavePos++;

		// wavePos isn't supposed to be > waveLen
		if (wavePos > waveLen) {
			wavePos -= waveLen;
		}
	}

	// Multiply sample with current TVA value
	sample *= amp;
	return sample;
}

void LA32FloatWaveGenerator::deactivate() {
	active = false;
}

bool LA32FloatWaveGenerator::isActive() const {
	return active;
}

bool LA32FloatWaveGenerator::isPCMWave() const {
	return pcmWaveAddress != NULL;
}

void LA32FloatPartialPair::init(const bool useRingModulated, const bool useMixed) {
	ringModulated = useRingModulated;
	mixed = useMixed;
	masterOutputSample = 0.0f;
	slaveOutputSample = 0.0f;
}

void LA32FloatPartialPair::initSynth(const PairType useMaster, const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance) {
	if (useMaster == MASTER) {
		master.initSynth(sawtoothWaveform, pulseWidth, resonance);
	} else {
		slave.initSynth(sawtoothWaveform, pulseWidth, resonance);
	}
}

void LA32FloatPartialPair::initPCM(const PairType useMaster, const Bit16s *pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped) {
	if (useMaster == MASTER) {
		master.initPCM(pcmWaveAddress, pcmWaveLength, pcmWaveLooped, true);
	} else {
		slave.initPCM(pcmWaveAddress, pcmWaveLength, pcmWaveLooped, !ringModulated);
	}
}

void LA32FloatPartialPair::generateNextSample(const PairType useMaster, const Bit32u amp, const Bit16u pitch, const Bit32u cutoff) {
	if (useMaster == MASTER) {
		masterOutputSample = master.generateNextSample(amp, pitch, cutoff);
	} else {
		slaveOutputSample = slave.generateNextSample(amp, pitch, cutoff);
	}
}

static inline float produceDistortedSample(float sample) {
	if (sample < -1.0f) {
		return sample + 2.0f;
	} else if (1.0f < sample) {
		return sample - 2.0f;
	}
	return sample;
}

float LA32FloatPartialPair::nextOutSample() {
	// Note, LA32FloatWaveGenerator produces each sample normalised in terms of a single playing partial,
	// so the unity sample corresponds to the internal LA32 logarithmic fixed-point unity sample.
	// However, each logarithmic sample is then unlogged to a 14-bit signed integer value, i.e. the max absolute value is 8192.
	// Thus, considering that samples are further mapped to a 16-bit signed integer,
	// we apply a conversion factor 0.25 to produce properly normalised float samples.
	if (!ringModulated) {
		return 0.25f * (masterOutputSample + slaveOutputSample);
	}
	/*
	 * SEMI-CONFIRMED: Ring modulation model derived from sample analysis of specially constructed patches which exploit distortion.
	 * LA32 ring modulator found to produce distorted output in case if the absolute value of maximal amplitude of one of the input partials exceeds 8191.
	 * This is easy to reproduce using synth partials with resonance values close to the maximum. It looks like an integer overflow happens in this case.
	 * As the distortion is strictly bound to the amplitude of the complete mixed square + resonance wave in the linear space,
	 * it is reasonable to assume the ring modulation is performed also in the linear space by sample multiplication.
	 * Most probably the overflow is caused by limited precision of the multiplication circuit as the very similar distortion occurs with panning.
	 */
	float ringModulatedSample = produceDistortedSample(masterOutputSample) * produceDistortedSample(slaveOutputSample);
	return 0.25f * (mixed ? masterOutputSample + ringModulatedSample : ringModulatedSample);
}

void LA32FloatPartialPair::deactivate(const PairType useMaster) {
	if (useMaster == MASTER) {
		master.deactivate();
		masterOutputSample = 0.0f;
	} else {
		slave.deactivate();
		slaveOutputSample = 0.0f;
	}
}

bool LA32FloatPartialPair::isActive(const PairType useMaster) const {
	return useMaster == MASTER ? master.isActive() : slave.isActive();
}

} // namespace MT32Emu
#undef MIDDLE_CUTOFF_VALUE
#undef RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE
#undef MAX_CUTOFF_VALUE
#undef produceDistortedSample

/************************************************** PartialManager.cpp ******************************************************/
namespace MT32Emu {

PartialManager::PartialManager(Synth *useSynth, Part **useParts) {
	synth = useSynth;
	parts = useParts;
	inactivePartialCount = synth->getPartialCount();
	partialTable = new Partial *[inactivePartialCount];
	inactivePartials = new int[inactivePartialCount];
	freePolys = new Poly *[synth->getPartialCount()];
	firstFreePolyIndex = 0;
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		partialTable[i] = new Partial(synth, i);
		inactivePartials[i] = inactivePartialCount - i - 1;
		freePolys[i] = new Poly();
	}
}

PartialManager::~PartialManager(void) {
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		delete partialTable[i];
		if (freePolys[i] != NULL) delete freePolys[i];
	}
	delete[] partialTable;
	delete[] inactivePartials;
	delete[] freePolys;
}

void PartialManager::clearAlreadyOutputed() {
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		partialTable[i]->alreadyOutputed = false;
	}
}

bool PartialManager::shouldReverb(int i) {
	return partialTable[i]->shouldReverb();
}

bool PartialManager::produceOutput(int i, IntSample *leftBuf, IntSample *rightBuf, Bit32u bufferLength) {
	return partialTable[i]->produceOutput(leftBuf, rightBuf, bufferLength);
}

bool PartialManager::produceOutput(int i, FloatSample *leftBuf, FloatSample *rightBuf, Bit32u bufferLength) {
	return partialTable[i]->produceOutput(leftBuf, rightBuf, bufferLength);
}

void PartialManager::deactivateAll() {
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		partialTable[i]->deactivate();
	}
}

unsigned int PartialManager::setReserve(Bit8u *rset) {
	unsigned int pr = 0;
	for (int x = 0; x <= 8; x++) {
		numReservedPartialsForPart[x] = rset[x];
		pr += rset[x];
	}
	return pr;
}

Partial *PartialManager::allocPartial(int partNum) {
	if (inactivePartialCount > 0) {
		Partial *partial = partialTable[inactivePartials[--inactivePartialCount]];
		partial->activate(partNum);
		return partial;
	}
	synth->printDebug("PartialManager Error: No inactive partials to allocate for part %d, current partial state:\n", partNum);
	for (Bit32u i = 0; i < synth->getPartialCount(); i++) {
		const Partial *partial = partialTable[i];
		synth->printDebug("[Partial %d]: activation=%d, owner part=%d\n", i, partial->isActive(), partial->getOwnerPart());
	}
	return NULL;
}

unsigned int PartialManager::getFreePartialCount() {
	return inactivePartialCount;
}

// This function is solely used to gather data for debug output at the moment.
void PartialManager::getPerPartPartialUsage(unsigned int perPartPartialUsage[9]) {
	memset(perPartPartialUsage, 0, 9 * sizeof(unsigned int));
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		if (partialTable[i]->isActive()) {
			perPartPartialUsage[partialTable[i]->getOwnerPart()]++;
		}
	}
}

// Finds the lowest-priority part that is exceeding its reserved partial allocation and has a poly
// in POLY_Releasing, then kills its first releasing poly.
// Parts with higher priority than minPart are not checked.
// Assumes that getFreePartials() has been called to make numReservedPartialsForPart up-to-date.
bool PartialManager::abortFirstReleasingPolyWhereReserveExceeded(int minPart) {
	if (minPart == 8) {
		// Rhythm is highest priority
		minPart = -1;
	}
	for (int partNum = 7; partNum >= minPart; partNum--) {
		int usePartNum = partNum == -1 ? 8 : partNum;
		if (parts[usePartNum]->getActivePartialCount() > numReservedPartialsForPart[usePartNum]) {
			// This part has exceeded its reserved partial count.
			// If it has any releasing polys, kill its first one and we're done.
			if (parts[usePartNum]->abortFirstPoly(POLY_Releasing)) {
				return true;
			}
		}
	}
	return false;
}

// Finds the lowest-priority part that is exceeding its reserved partial allocation and has a poly, then kills
// its first poly in POLY_Held - or failing that, its first poly in any state.
// Parts with higher priority than minPart are not checked.
// Assumes that getFreePartials() has been called to make numReservedPartialsForPart up-to-date.
bool PartialManager::abortFirstPolyPreferHeldWhereReserveExceeded(int minPart) {
	if (minPart == 8) {
		// Rhythm is highest priority
		minPart = -1;
	}
	for (int partNum = 7; partNum >= minPart; partNum--) {
		int usePartNum = partNum == -1 ? 8 : partNum;
		if (parts[usePartNum]->getActivePartialCount() > numReservedPartialsForPart[usePartNum]) {
			// This part has exceeded its reserved partial count.
			// If it has any polys, kill its first (preferably held) one and we're done.
			if (parts[usePartNum]->abortFirstPolyPreferHeld()) {
				return true;
			}
		}
	}
	return false;
}

bool PartialManager::freePartials(unsigned int needed, int partNum) {
	// CONFIRMED: Barring bugs, this matches the real LAPC-I according to information from Mok.

	// BUG: There's a bug in the LAPC-I implementation:
	// When allocating for rhythm part, or when allocating for a part that is using fewer partials than it has reserved,
	// held and playing polys on the rhythm part can potentially be aborted before releasing polys on the rhythm part.
	// This bug isn't present on MT-32.
	// I consider this to be a bug because I think that playing polys should always have priority over held polys,
	// and held polys should always have priority over releasing polys.

	// NOTE: This code generally aborts polys in parts (according to certain conditions) in the following order:
	// 7, 6, 5, 4, 3, 2, 1, 0, 8 (rhythm)
	// (from lowest priority, meaning most likely to have polys aborted, to highest priority, meaning least likely)

	if (needed == 0) {
		return true;
	}

	// Note that calling getFreePartialCount() also ensures that numReservedPartialsPerPart is up-to-date
	if (getFreePartialCount() >= needed) {
		return true;
	}

	// Note: These #ifdefs are temporary until we have proper "quirk" configuration.
	// Also, the MT-32 version isn't properly confirmed yet.
#ifdef MT32EMU_QUIRK_FREE_PARTIALS_MT32
	// On MT-32, we bail out before even killing releasing partials if the allocating part has exceeded its reserve and is configured for priority-to-earlier-polys.
	if (parts[partNum]->getActiveNonReleasingPartialCount() + needed > numReservedPartialsForPart[partNum] && (synth->getPart(partNum)->getPatchTemp()->patch.assignMode & 1)) {
		return false;
	}
#endif

	for (;;) {
#ifdef MT32EMU_QUIRK_FREE_PARTIALS_MT32
		// Abort releasing polys in parts that have exceeded their partial reservation (working backwards from part 7, with rhythm last)
		if (!abortFirstReleasingPolyWhereReserveExceeded(-1)) {
			break;
		}
#else
		// Abort releasing polys in non-rhythm parts that have exceeded their partial reservation (working backwards from part 7)
		if (!abortFirstReleasingPolyWhereReserveExceeded(0)) {
			break;
		}
#endif
		if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
			return true;
		}
	}

	if (parts[partNum]->getActiveNonReleasingPartialCount() + needed > numReservedPartialsForPart[partNum]) {
		// With the new partials we're freeing for, we would end up using more partials than we have reserved.
		if (synth->getPart(partNum)->getPatchTemp()->patch.assignMode & 1) {
			// Priority is given to earlier polys, so just give up
			return false;
		}
		// Only abort held polys in the target part and parts that have a lower priority
		// (higher part number = lower priority, except for rhythm, which has the highest priority).
		for (;;) {
			if (!abortFirstPolyPreferHeldWhereReserveExceeded(partNum)) {
				break;
			}
			if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
				return true;
			}
		}
		if (needed > numReservedPartialsForPart[partNum]) {
			return false;
		}
	} else {
		// At this point, we're certain that we've reserved enough partials to play our poly.
		// Check all parts from lowest to highest priority to see whether they've exceeded their
		// reserve, and abort their polys until until we have enough free partials or they're within
		// their reserve allocation.
		for (;;) {
			if (!abortFirstPolyPreferHeldWhereReserveExceeded(-1)) {
				break;
			}
			if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
				return true;
			}
		}
	}

	// Abort polys in the target part until there are enough free partials for the new one
	for (;;) {
		if (!parts[partNum]->abortFirstPolyPreferHeld()) {
			break;
		}
		if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
			return true;
		}
	}

	// Aww, not enough partials for you.
	return false;
}

const Partial *PartialManager::getPartial(unsigned int partialNum) const {
	if (partialNum > synth->getPartialCount() - 1) {
		return NULL;
	}
	return partialTable[partialNum];
}

Poly *PartialManager::assignPolyToPart(Part *part) {
	if (firstFreePolyIndex < synth->getPartialCount()) {
		Poly *poly = freePolys[firstFreePolyIndex];
		freePolys[firstFreePolyIndex] = NULL;
		firstFreePolyIndex++;
		poly->setPart(part);
		return poly;
	}
	return NULL;
}

void PartialManager::polyFreed(Poly *poly) {
	if (0 == firstFreePolyIndex) {
		synth->printDebug("PartialManager Error: Cannot return freed poly, currently active polys:\n");
		for (Bit32u partNum = 0; partNum < 9; partNum++) {
			const Poly *activePoly = synth->getPart(partNum)->getFirstActivePoly();
			Bit32u polyCount = 0;
			while (activePoly != NULL) {
				activePoly = activePoly->getNext();
				polyCount++;
			}
			synth->printDebug("Part: %i, active poly count: %i\n", partNum, polyCount);
		}
	} else {
		firstFreePolyIndex--;
		freePolys[firstFreePolyIndex] = poly;
	}
	poly->setPart(NULL);
}

void PartialManager::partialDeactivated(int partialIndex) {
	if (inactivePartialCount < synth->getPartialCount()) {
		inactivePartials[inactivePartialCount++] = partialIndex;
		return;
	}
	synth->printDebug("PartialManager Error: Cannot return deactivated partial %d, current partial state:\n", partialIndex);
	for (Bit32u i = 0; i < synth->getPartialCount(); i++) {
		const Partial *partial = partialTable[i];
		synth->printDebug("[Partial %d]: activation=%d, owner part=%d\n", i, partial->isActive(), partial->getOwnerPart());
	}
}

} // namespace MT32Emu

/************************************************** Poly.cpp ******************************************************/
namespace MT32Emu {

Poly::Poly() {
	part = NULL;
	key = 255;
	velocity = 255;
	sustain = false;
	activePartialCount = 0;
	for (int i = 0; i < 4; i++) {
		partials[i] = NULL;
	}
	state = POLY_Inactive;
	next = NULL;
}

void Poly::setPart(Part *usePart) {
	part = usePart;
}

void Poly::reset(unsigned int newKey, unsigned int newVelocity, bool newSustain, Partial **newPartials) {
	if (isActive()) {
		// This should never happen
		part->getSynth()->printDebug("Resetting active poly. Active partial count: %i\n", activePartialCount);
		for (int i = 0; i < 4; i++) {
			if (partials[i] != NULL && partials[i]->isActive()) {
				partials[i]->deactivate();
				activePartialCount--;
			}
		}
		setState(POLY_Inactive);
	}

	key = newKey;
	velocity = newVelocity;
	sustain = newSustain;

	activePartialCount = 0;
	for (int i = 0; i < 4; i++) {
		partials[i] = newPartials[i];
		if (newPartials[i] != NULL) {
			activePartialCount++;
			setState(POLY_Playing);
		}
	}
}

bool Poly::noteOff(bool pedalHeld) {
	// Generally, non-sustaining instruments ignore note off. They die away eventually anyway.
	// Key 0 (only used by special cases on rhythm part) reacts to note off even if non-sustaining or pedal held.
	if (state == POLY_Inactive || state == POLY_Releasing) {
		return false;
	}
	if (pedalHeld) {
		if (state == POLY_Held) {
			return false;
		}
		setState(POLY_Held);
	} else {
		startDecay();
	}
	return true;
}

bool Poly::stopPedalHold() {
	if (state != POLY_Held) {
		return false;
	}
	return startDecay();
}

bool Poly::startDecay() {
	if (state == POLY_Inactive || state == POLY_Releasing) {
		return false;
	}
	setState(POLY_Releasing);

	for (int t = 0; t < 4; t++) {
		Partial *partial = partials[t];
		if (partial != NULL) {
			partial->startDecayAll();
		}
	}
	return true;
}

bool Poly::startAbort() {
	if (state == POLY_Inactive || part->getSynth()->isAbortingPoly()) {
		return false;
	}
	for (int t = 0; t < 4; t++) {
		Partial *partial = partials[t];
		if (partial != NULL) {
			partial->startAbort();
			part->getSynth()->abortingPoly = this;
		}
	}
	return true;
}

void Poly::setState(PolyState newState) {
	if (state == newState) return;
	PolyState oldState = state;
	state = newState;
	part->polyStateChanged(oldState, newState);
}

void Poly::backupCacheToPartials(PatchCache cache[4]) {
	for (int partialNum = 0; partialNum < 4; partialNum++) {
		Partial *partial = partials[partialNum];
		if (partial != NULL) {
			partial->backupCache(cache[partialNum]);
		}
	}
}

/**
 * Returns the internal key identifier.
 * For non-rhythm, this is within the range 12 to 108.
 * For rhythm on MT-32, this is 0 or 1 (special cases) or within the range 24 to 87.
 * For rhythm on devices with extended PCM sounds (e.g. CM-32L), this is 0, 1 or 24 to 108
 */
unsigned int Poly::getKey() const {
	return key;
}

unsigned int Poly::getVelocity() const {
	return velocity;
}

bool Poly::canSustain() const {
	return sustain;
}

PolyState Poly::getState() const {
	return state;
}

unsigned int Poly::getActivePartialCount() const {
	return activePartialCount;
}

bool Poly::isActive() const {
	return state != POLY_Inactive;
}

// This is called by Partial to inform the poly that the Partial has deactivated
void Poly::partialDeactivated(Partial *partial) {
	for (int i = 0; i < 4; i++) {
		if (partials[i] == partial) {
			partials[i] = NULL;
			activePartialCount--;
		}
	}
	if (activePartialCount == 0) {
		setState(POLY_Inactive);
		if (part->getSynth()->abortingPoly == this) {
			part->getSynth()->abortingPoly = NULL;
		}
	}
	part->partialDeactivated(this);
}

Poly *Poly::getNext() const {
	return next;
}

void Poly::setNext(Poly *poly) {
	next = poly;
}

} // namespace MT32Emu

/************************************************** TVA.cpp ******************************************************/
/*
 * This class emulates the calculations performed by the 8095 microcontroller in order to configure the LA-32's amplitude ramp for a single partial at each stage of its TVA envelope.
 * Unless we introduced bugs, it should be pretty much 100% accurate according to Mok's specifications.
*/

namespace MT32Emu {

// CONFIRMED: Matches a table in ROM - haven't got around to coming up with a formula for it yet.
static Bit8u biasLevelToAmpSubtractionCoeff[13] = {255, 187, 137, 100, 74, 54, 40, 29, 21, 15, 10, 5, 0};

TVA::TVA(const Partial *usePartial, LA32Ramp *useAmpRamp) :
	partial(usePartial), ampRamp(useAmpRamp), system(&usePartial->getSynth()->mt32ram.system), phase(TVA_PHASE_DEAD) {
}

void TVA::startRamp(Bit8u newTarget, Bit8u newIncrement, int newPhase) {
	target = newTarget;
	phase = newPhase;
	ampRamp->startRamp(newTarget, newIncrement);
#if MT32EMU_MONITOR_TVA >= 1
	partial->getSynth()->printDebug("[+%lu] [Partial %d] TVA,ramp,%x,%s%x,%d", partial->debugGetSampleNum(), partial->debugGetPartialNum(), newTarget, (newIncrement & 0x80) ? "-" : "+", (newIncrement & 0x7F), newPhase);
#endif
}

void TVA::end(int newPhase) {
	phase = newPhase;
	playing = false;
#if MT32EMU_MONITOR_TVA >= 1
	partial->getSynth()->printDebug("[+%lu] [Partial %d] TVA,end,%d", partial->debugGetSampleNum(), partial->debugGetPartialNum(), newPhase);
#endif
}

static int multBias(Bit8u biasLevel, int bias) {
	return (bias * biasLevelToAmpSubtractionCoeff[biasLevel]) >> 5;
}

static int calcBiasAmpSubtraction(Bit8u biasPoint, Bit8u biasLevel, int key) {
	if ((biasPoint & 0x40) == 0) {
		int bias = biasPoint + 33 - key;
		if (bias > 0) {
			return multBias(biasLevel, bias);
		}
	} else {
		int bias = biasPoint - 31 - key;
		if (bias < 0) {
			bias = -bias;
			return multBias(biasLevel, bias);
		}
	}
	return 0;
}

static int calcBiasAmpSubtractions(const TimbreParam::PartialParam *partialParam, int key) {
	int biasAmpSubtraction1 = calcBiasAmpSubtraction(partialParam->tva.biasPoint1, partialParam->tva.biasLevel1, key);
	if (biasAmpSubtraction1 > 255) {
		return 255;
	}
	int biasAmpSubtraction2 = calcBiasAmpSubtraction(partialParam->tva.biasPoint2, partialParam->tva.biasLevel2, key);
	if (biasAmpSubtraction2 > 255) {
		return 255;
	}
	int biasAmpSubtraction = biasAmpSubtraction1 + biasAmpSubtraction2;
	if (biasAmpSubtraction > 255) {
		return 255;
	}
	return biasAmpSubtraction;
}

static int calcVeloAmpSubtraction(Bit8u veloSensitivity, unsigned int velocity) {
	// FIXME:KG: Better variable names
	int velocityMult = veloSensitivity - 50;
	int absVelocityMult = velocityMult < 0 ? -velocityMult : velocityMult;
	velocityMult = signed(unsigned(velocityMult * (signed(velocity) - 64)) << 2);
	return absVelocityMult - (velocityMult >> 8); // PORTABILITY NOTE: Assumes arithmetic shift
}

static int calcBasicAmp(const Tables *tables, const Partial *partial, const MemParams::System *system, const TimbreParam::PartialParam *partialParam, Bit8u partVolume, const MemParams::RhythmTemp *rhythmTemp, int biasAmpSubtraction, int veloAmpSubtraction, Bit8u expression, bool hasRingModQuirk) {
	int amp = 155;

	if (!(hasRingModQuirk ? partial->isRingModulatingNoMix() : partial->isRingModulatingSlave())) {
		amp -= tables->masterVolToAmpSubtraction[system->masterVol];
		if (amp < 0) {
			return 0;
		}
		amp -= tables->levelToAmpSubtraction[partVolume];
		if (amp < 0) {
			return 0;
		}
		amp -= tables->levelToAmpSubtraction[expression];
		if (amp < 0) {
			return 0;
		}
		if (rhythmTemp != NULL) {
			amp -= tables->levelToAmpSubtraction[rhythmTemp->outputLevel];
			if (amp < 0) {
				return 0;
			}
		}
	}
	amp -= biasAmpSubtraction;
	if (amp < 0) {
		return 0;
	}
	amp -= tables->levelToAmpSubtraction[partialParam->tva.level];
	if (amp < 0) {
		return 0;
	}
	amp -= veloAmpSubtraction;
	if (amp < 0) {
		return 0;
	}
	if (amp > 155) {
		amp = 155;
	}
	amp -= partialParam->tvf.resonance >> 1;
	if (amp < 0) {
		return 0;
	}
	return amp;
}

static int calcKeyTimeSubtraction(Bit8u envTimeKeyfollow, int key) {
	if (envTimeKeyfollow == 0) {
		return 0;
	}
	return (key - 60) >> (5 - envTimeKeyfollow); // PORTABILITY NOTE: Assumes arithmetic shift
}

void TVA::reset(const Part *newPart, const TimbreParam::PartialParam *newPartialParam, const MemParams::RhythmTemp *newRhythmTemp) {
	part = newPart;
	partialParam = newPartialParam;
	rhythmTemp = newRhythmTemp;

	playing = true;

	const Tables *tables = &Tables::getInstance();

	int key = partial->getPoly()->getKey();
	int velocity = partial->getPoly()->getVelocity();

	keyTimeSubtraction = calcKeyTimeSubtraction(partialParam->tva.envTimeKeyfollow, key);

	biasAmpSubtraction = calcBiasAmpSubtractions(partialParam, key);
	veloAmpSubtraction = calcVeloAmpSubtraction(partialParam->tva.veloSensitivity, velocity);

	int newTarget = calcBasicAmp(tables, partial, system, partialParam, part->getVolume(), newRhythmTemp, biasAmpSubtraction, veloAmpSubtraction, part->getExpression(), partial->getSynth()->controlROMFeatures->quirkRingModulationNoMix);
	int newPhase;
	if (partialParam->tva.envTime[0] == 0) {
		// Initially go to the TVA_PHASE_ATTACK target amp, and spend the next phase going from there to the TVA_PHASE_2 target amp
		// Note that this means that velocity never affects time for this partial.
		newTarget += partialParam->tva.envLevel[0];
		newPhase = TVA_PHASE_ATTACK; // The first target used in nextPhase() will be TVA_PHASE_2
	} else {
		// Initially go to the base amp determined by TVA level, part volume, etc., and spend the next phase going from there to the full TVA_PHASE_ATTACK target amp.
		newPhase = TVA_PHASE_BASIC; // The first target used in nextPhase() will be TVA_PHASE_ATTACK
	}

	ampRamp->reset();//currentAmp = 0;

	// "Go downward as quickly as possible".
	// Since the current value is 0, the LA32Ramp will notice that we're already at or below the target and trying to go downward,
	// and therefore jump to the target immediately and raise an interrupt.
	startRamp(Bit8u(newTarget), 0x80 | 127, newPhase);
}

void TVA::startAbort() {
	startRamp(64, 0x80 | 127, TVA_PHASE_RELEASE);
}

void TVA::startDecay() {
	if (phase >= TVA_PHASE_RELEASE) {
		return;
	}
	Bit8u newIncrement;
	if (partialParam->tva.envTime[4] == 0) {
		newIncrement = 1;
	} else {
		newIncrement = -partialParam->tva.envTime[4];
	}
	// The next time nextPhase() is called, it will think TVA_PHASE_RELEASE has finished and the partial will be aborted
	startRamp(0, newIncrement, TVA_PHASE_RELEASE);
}

void TVA::handleInterrupt() {
	nextPhase();
}

void TVA::recalcSustain() {
	// We get pinged periodically by the pitch code to recalculate our values when in sustain.
	// This is done so that the TVA will respond to things like MIDI expression and volume changes while it's sustaining, which it otherwise wouldn't do.

	// The check for envLevel[3] == 0 strikes me as slightly dumb. FIXME: Explain why
	if (phase != TVA_PHASE_SUSTAIN || partialParam->tva.envLevel[3] == 0) {
		return;
	}
	// We're sustaining. Recalculate all the values
	const Tables *tables = &Tables::getInstance();
	int newTarget = calcBasicAmp(tables, partial, system, partialParam, part->getVolume(), rhythmTemp, biasAmpSubtraction, veloAmpSubtraction, part->getExpression(), partial->getSynth()->controlROMFeatures->quirkRingModulationNoMix);
	newTarget += partialParam->tva.envLevel[3];

	// Although we're in TVA_PHASE_SUSTAIN at this point, we cannot be sure that there is no active ramp at the moment.
	// In case the channel volume or the expression changes frequently, the previously started ramp may still be in progress.
	// Real hardware units ignore this possibility and rely on the assumption that the target is the current amp.
	// This is OK in most situations but when the ramp that is currently in progress needs to change direction
	// due to a volume/expression update, this leads to a jump in the amp that is audible as an unpleasant click.
	// To avoid that, we compare the newTarget with the the actual current ramp value and correct the direction if necessary.
	int targetDelta = newTarget - target;

	// Calculate an increment to get to the new amp value in a short, more or less consistent amount of time
	Bit8u newIncrement;
	bool descending = targetDelta < 0;
	if (!descending) {
		newIncrement = tables->envLogarithmicTime[Bit8u(targetDelta)] - 2;
	} else {
		newIncrement = (tables->envLogarithmicTime[Bit8u(-targetDelta)] - 2) | 0x80;
	}
	if (part->getSynth()->isNiceAmpRampEnabled() && (descending != ampRamp->isBelowCurrent(newTarget))) {
		newIncrement ^= 0x80;
	}

	// Configure so that once the transition's complete and nextPhase() is called, we'll just re-enter sustain phase (or decay phase, depending on parameters at the time).
	startRamp(newTarget, newIncrement, TVA_PHASE_SUSTAIN - 1);
}

bool TVA::isPlaying() const {
	return playing;
}

int TVA::getPhase() const {
	return phase;
}

void TVA::nextPhase() {
	const Tables *tables = &Tables::getInstance();

	if (phase >= TVA_PHASE_DEAD || !playing) {
		partial->getSynth()->printDebug("TVA::nextPhase(): Shouldn't have got here with phase %d, playing=%s", phase, playing ? "true" : "false");
		return;
	}
	int newPhase = phase + 1;

	if (newPhase == TVA_PHASE_DEAD) {
		end(newPhase);
		return;
	}

	bool allLevelsZeroFromNowOn = false;
	if (partialParam->tva.envLevel[3] == 0) {
		if (newPhase == TVA_PHASE_4) {
			allLevelsZeroFromNowOn = true;
		} else if (!partial->getSynth()->controlROMFeatures->quirkTVAZeroEnvLevels && partialParam->tva.envLevel[2] == 0) {
			if (newPhase == TVA_PHASE_3) {
				allLevelsZeroFromNowOn = true;
			} else if (partialParam->tva.envLevel[1] == 0) {
				if (newPhase == TVA_PHASE_2) {
					allLevelsZeroFromNowOn = true;
				} else if (partialParam->tva.envLevel[0] == 0) {
					if (newPhase == TVA_PHASE_ATTACK)  { // this line added, missing in ROM - FIXME: Add description of repercussions
						allLevelsZeroFromNowOn = true;
					}
				}
			}
		}
	}

	int newTarget;
	int newIncrement = 0; // Initialised to please compilers
	int envPointIndex = phase;

	if (!allLevelsZeroFromNowOn) {
		newTarget = calcBasicAmp(tables, partial, system, partialParam, part->getVolume(), rhythmTemp, biasAmpSubtraction, veloAmpSubtraction, part->getExpression(), partial->getSynth()->controlROMFeatures->quirkRingModulationNoMix);

		if (newPhase == TVA_PHASE_SUSTAIN || newPhase == TVA_PHASE_RELEASE) {
			if (partialParam->tva.envLevel[3] == 0) {
				end(newPhase);
				return;
			}
			if (!partial->getPoly()->canSustain()) {
				newPhase = TVA_PHASE_RELEASE;
				newTarget = 0;
				newIncrement = -partialParam->tva.envTime[4];
				if (newIncrement == 0) {
					// We can't let the increment be 0, or there would be no emulated interrupt.
					// So we do an "upward" increment, which should set the amp to 0 extremely quickly
					// and cause an "interrupt" to bring us back to nextPhase().
					newIncrement = 1;
				}
			} else {
				newTarget += partialParam->tva.envLevel[3];
				newIncrement = 0;
			}
		} else {
			newTarget += partialParam->tva.envLevel[envPointIndex];
		}
	} else {
		newTarget = 0;
	}

	if ((newPhase != TVA_PHASE_SUSTAIN && newPhase != TVA_PHASE_RELEASE) || allLevelsZeroFromNowOn) {
		int envTimeSetting = partialParam->tva.envTime[envPointIndex];

		if (newPhase == TVA_PHASE_ATTACK) {
			envTimeSetting -= (signed(partial->getPoly()->getVelocity()) - 64) >> (6 - partialParam->tva.envTimeVeloSensitivity); // PORTABILITY NOTE: Assumes arithmetic shift

			if (envTimeSetting <= 0 && partialParam->tva.envTime[envPointIndex] != 0) {
				envTimeSetting = 1;
			}
		} else {
			envTimeSetting -= keyTimeSubtraction;
		}
		if (envTimeSetting > 0) {
			int targetDelta = newTarget - target;
			if (targetDelta <= 0) {
				if (targetDelta == 0) {
					// target and newTarget are the same.
					// We can't have an increment of 0 or we wouldn't get an emulated interrupt.
					// So instead make the target one less than it really should be and set targetDelta accordingly.
					targetDelta = -1;
					newTarget--;
					if (newTarget < 0) {
						// Oops, newTarget is less than zero now, so let's do it the other way:
						// Make newTarget one more than it really should've been and set targetDelta accordingly.
						// FIXME (apparent bug in real firmware):
						// This means targetDelta will be positive just below here where it's inverted, and we'll end up using envLogarithmicTime[-1], and we'll be setting newIncrement to be descending later on, etc..
						targetDelta = 1;
						newTarget = -newTarget;
					}
				}
				targetDelta = -targetDelta;
				newIncrement = tables->envLogarithmicTime[Bit8u(targetDelta)] - envTimeSetting;
				if (newIncrement <= 0) {
					newIncrement = 1;
				}
				newIncrement = newIncrement | 0x80;
			} else {
				// FIXME: The last 22 or so entries in this table are 128 - surely that fucks things up, since that ends up being -128 signed?
				newIncrement = tables->envLogarithmicTime[Bit8u(targetDelta)] - envTimeSetting;
				if (newIncrement <= 0) {
					newIncrement = 1;
				}
			}
		} else {
			newIncrement = newTarget >= target ? (0x80 | 127) : 127;
		}

		// FIXME: What's the point of this? It's checked or set to non-zero everywhere above
		if (newIncrement == 0) {
			newIncrement = 1;
		}
	}

	startRamp(Bit8u(newTarget), Bit8u(newIncrement), newPhase);
}

} // namespace MT32Emu

/************************************************** TVF.cpp ******************************************************/
namespace MT32Emu {

// Note that when entering nextPhase(), newPhase is set to phase + 1, and the descriptions/names below refer to
// newPhase's value.
enum {
	// When this is the target phase, level[0] is targeted within time[0]
	// Note that this phase is always set up in reset(), not nextPhase()
	PHASE_ATTACK = 1,

	// When this is the target phase, level[1] is targeted within time[1]
	PHASE_2 = 2,

	// When this is the target phase, level[2] is targeted within time[2]
	PHASE_3 = 3,

	// When this is the target phase, level[3] is targeted within time[3]
	PHASE_4 = 4,

	// When this is the target phase, immediately goes to PHASE_RELEASE unless the poly is set to sustain.
	// Otherwise level[3] is continued with increment 0 - no phase change will occur until some external influence (like pedal release)
	PHASE_SUSTAIN = 5,

	// 0 is targeted within time[4] (the time calculation is quite different from the other phases)
	PHASE_RELEASE = 6,

	// 0 is targeted with increment 0 (thus theoretically staying that way forever)
	PHASE_DONE = 7
};

static int calcBaseCutoff(const TimbreParam::PartialParam *partialParam, Bit32u basePitch, unsigned int key, bool quirkTVFBaseCutoffLimit) {
	// This table matches the values used by a real LAPC-I.
	static const Bit8s biasLevelToBiasMult[] = {85, 42, 21, 16, 10, 5, 2, 0, -2, -5, -10, -16, -21, -74, -85};
	// These values represent unique options with no consistent pattern, so we have to use something like a table in any case.
	// The table entries, when divided by 21, match approximately what the manual claims:
	// -1, -1/2, -1/4, 0, 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, 1, 5/4, 3/2, 2, s1, s2
	// Note that the entry for 1/8 is rounded to 2 (from 1/8 * 21 = 2.625), which seems strangely inaccurate compared to the others.
	static const Bit8s keyfollowMult21[] = {-21, -10, -5, 0, 2, 5, 8, 10, 13, 16, 18, 21, 26, 32, 42, 21, 21};
	int baseCutoff = keyfollowMult21[partialParam->tvf.keyfollow] - keyfollowMult21[partialParam->wg.pitchKeyfollow];
	// baseCutoff range now: -63 to 63
	baseCutoff *= int(key) - 60;
	// baseCutoff range now: -3024 to 3024
	int biasPoint = partialParam->tvf.biasPoint;
	if ((biasPoint & 0x40) == 0) {
		// biasPoint range here: 0 to 63
		int bias = biasPoint + 33 - key; // bias range here: -75 to 84
		if (bias > 0) {
			bias = -bias; // bias range here: -1 to -84
			baseCutoff += bias * biasLevelToBiasMult[partialParam->tvf.biasLevel]; // Calculation range: -7140 to 7140
			// baseCutoff range now: -10164 to 10164
		}
	} else {
		// biasPoint range here: 64 to 127
		int bias = biasPoint - 31 - key; // bias range here: -75 to 84
		if (bias < 0) {
			baseCutoff += bias * biasLevelToBiasMult[partialParam->tvf.biasLevel]; // Calculation range: -6375 to 6375
			// baseCutoff range now: -9399 to 9399
		}
	}
	// baseCutoff range now: -10164 to 10164
	baseCutoff += ((partialParam->tvf.cutoff << 4) - 800);
	// baseCutoff range now: -10964 to 10964
	if (baseCutoff >= 0) {
		// FIXME: Potentially bad if baseCutoff ends up below -2056?
		int pitchDeltaThing = (basePitch >> 4) + baseCutoff - 3584;
		if (pitchDeltaThing > 0) {
			baseCutoff -= pitchDeltaThing;
		}
	} else if (quirkTVFBaseCutoffLimit) {
		if (baseCutoff <= -0x400) {
			baseCutoff = -400;
		}
	} else {
		if (baseCutoff < -2048) {
			baseCutoff = -2048;
		}
	}
	baseCutoff += 2056;
	baseCutoff >>= 4; // PORTABILITY NOTE: Hmm... Depends whether it could've been below -2056, but maybe arithmetic shift assumed?
	if (baseCutoff > 255) {
		baseCutoff = 255;
	}
	return Bit8u(baseCutoff);
}

TVF::TVF(const Partial *usePartial, LA32Ramp *useCutoffModifierRamp) :
	partial(usePartial), cutoffModifierRamp(useCutoffModifierRamp) {
}

void TVF::startRamp(Bit8u newTarget, Bit8u newIncrement, int newPhase) {
	target = newTarget;
	phase = newPhase;
	cutoffModifierRamp->startRamp(newTarget, newIncrement);
#if MT32EMU_MONITOR_TVF >= 1
	partial->getSynth()->printDebug("[+%lu] [Partial %d] TVF,ramp,%x,%s%x,%d", partial->debugGetSampleNum(), partial->debugGetPartialNum(), newTarget, (newIncrement & 0x80) ? "-" : "+", (newIncrement & 0x7F), newPhase);
#endif
}

void TVF::reset(const TimbreParam::PartialParam *newPartialParam, unsigned int basePitch) {
	partialParam = newPartialParam;

	unsigned int key = partial->getPoly()->getKey();
	unsigned int velocity = partial->getPoly()->getVelocity();

	const Tables *tables = &Tables::getInstance();

	baseCutoff = calcBaseCutoff(newPartialParam, basePitch, key, partial->getSynth()->controlROMFeatures->quirkTVFBaseCutoffLimit);
#if MT32EMU_MONITOR_TVF >= 1
	partial->getSynth()->printDebug("[+%lu] [Partial %d] TVF,base,%d", partial->debugGetSampleNum(), partial->debugGetPartialNum(), baseCutoff);
#endif

	int newLevelMult = velocity * newPartialParam->tvf.envVeloSensitivity;
	newLevelMult >>= 6;
	newLevelMult += 109 - newPartialParam->tvf.envVeloSensitivity;
	newLevelMult += (signed(key) - 60) >> (4 - newPartialParam->tvf.envDepthKeyfollow);
	if (newLevelMult < 0) {
		newLevelMult = 0;
	}
	newLevelMult *= newPartialParam->tvf.envDepth;
	newLevelMult >>= 6;
	if (newLevelMult > 255) {
		newLevelMult = 255;
	}
	levelMult = newLevelMult;

	if (newPartialParam->tvf.envTimeKeyfollow != 0) {
		keyTimeSubtraction = (signed(key) - 60) >> (5 - newPartialParam->tvf.envTimeKeyfollow);
	} else {
		keyTimeSubtraction = 0;
	}

	int newTarget = (newLevelMult * newPartialParam->tvf.envLevel[0]) >> 8;
	int envTimeSetting = newPartialParam->tvf.envTime[0] - keyTimeSubtraction;
	int newIncrement;
	if (envTimeSetting <= 0) {
		newIncrement = (0x80 | 127);
	} else {
		newIncrement = tables->envLogarithmicTime[newTarget] - envTimeSetting;
		if (newIncrement <= 0) {
			newIncrement = 1;
		}
	}
	cutoffModifierRamp->reset();
	startRamp(newTarget, newIncrement, PHASE_2 - 1);
}

Bit8u TVF::getBaseCutoff() const {
	return baseCutoff;
}

void TVF::handleInterrupt() {
	nextPhase();
}

void TVF::startDecay() {
	if (phase >= PHASE_RELEASE) {
		return;
	}
	if (partialParam->tvf.envTime[4] == 0) {
		startRamp(0, 1, PHASE_DONE - 1);
	} else {
		startRamp(0, -partialParam->tvf.envTime[4], PHASE_DONE - 1);
	}
}

void TVF::nextPhase() {
	const Tables *tables = &Tables::getInstance();
	int newPhase = phase + 1;

	switch (newPhase) {
	case PHASE_DONE:
		startRamp(0, 0, newPhase);
		return;
	case PHASE_SUSTAIN:
	case PHASE_RELEASE:
		// FIXME: Afaict newPhase should never be PHASE_RELEASE here. And if it were, this is an odd way to handle it.
		if (!partial->getPoly()->canSustain()) {
			phase = newPhase; // FIXME: Correct?
			startDecay(); // FIXME: This should actually start decay even if phase is already 6. Does that matter?
			return;
		}
		startRamp((levelMult * partialParam->tvf.envLevel[3]) >> 8, 0, newPhase);
		return;
	default:
		break;
	}

	int envPointIndex = phase;
	int envTimeSetting = partialParam->tvf.envTime[envPointIndex] - keyTimeSubtraction;

	int newTarget = (levelMult * partialParam->tvf.envLevel[envPointIndex]) >> 8;
	int newIncrement;
	if (envTimeSetting > 0) {
		int targetDelta = newTarget - target;
		if (targetDelta == 0) {
			if (newTarget == 0) {
				targetDelta = 1;
				newTarget = 1;
			} else {
				targetDelta = -1;
				newTarget--;
			}
		}
		newIncrement = tables->envLogarithmicTime[targetDelta < 0 ? -targetDelta : targetDelta] - envTimeSetting;
		if (newIncrement <= 0) {
			newIncrement = 1;
		}
		if (targetDelta < 0) {
			newIncrement |= 0x80;
		}
	} else {
		newIncrement = newTarget >= target ? (0x80 | 127) : 127;
	}
	startRamp(newTarget, newIncrement, newPhase);
}

} // namespace MT32Emu

/************************************************** TVP.cpp ******************************************************/
namespace MT32Emu {

// FIXME: Add Explanation
static Bit16u lowerDurationToDivisor[] = {34078, 37162, 40526, 44194, 48194, 52556, 57312, 62499};

// These values represent unique options with no consistent pattern, so we have to use something like a table in any case.
// The table matches exactly what the manual claims (when divided by 8192):
// -1, -1/2, -1/4, 0, 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, 1, 5/4, 3/2, 2, s1, s2
// ...except for the last two entries, which are supposed to be "1 cent above 1" and "2 cents above 1", respectively. They can only be roughly approximated with this integer math.
static Bit16s pitchKeyfollowMult[] = {-8192, -4096, -2048, 0, 1024, 2048, 3072, 4096, 5120, 6144, 7168, 8192, 10240, 12288, 16384, 8198, 8226};

// Note: Keys < 60 use keyToPitchTable[60 - key], keys >= 60 use keyToPitchTable[key - 60].
// FIXME: This table could really be shorter, since we never use e.g. key 127.
static Bit16u keyToPitchTable[] = {
	    0,   341,   683,  1024,  1365,  1707,  2048,  2389,
	 2731,  3072,  3413,  3755,  4096,  4437,  4779,  5120,
	 5461,  5803,  6144,  6485,  6827,  7168,  7509,  7851,
	 8192,  8533,  8875,  9216,  9557,  9899, 10240, 10581,
	10923, 11264, 11605, 11947, 12288, 12629, 12971, 13312,
	13653, 13995, 14336, 14677, 15019, 15360, 15701, 16043,
	16384, 16725, 17067, 17408, 17749, 18091, 18432, 18773,
	19115, 19456, 19797, 20139, 20480, 20821, 21163, 21504,
	21845, 22187, 22528, 22869
};

// We want to do processing 4000 times per second. FIXME: This is pretty arbitrary.
static const int NOMINAL_PROCESS_TIMER_PERIOD_SAMPLES = SAMPLE_RATE / 4000;

// In all hardware units we emulate, the main clock frequency of the MCU is 12MHz.
// However, the MCU used in the 3rd-gen sound modules (like CM-500 and LAPC-N)
// is significantly faster. Importantly, the software timer also works faster,
// yet this fact has been seemingly missed. To be more specific, the software timer
// ticks each 8 "state times", and 1 state time equals to 3 clock periods
// for 8095 and 8098 but 2 clock periods for 80C198. That is, on MT-32 and CM-32L,
// the software timer tick rate is 12,000,000 / 3 / 8 = 500kHz, but on the 3rd-gen
// devices it's 12,000,000 / 2 / 8 = 750kHz instead.

// For 1st- and 2nd-gen devices, the timer ticks at 500kHz. This is how much to increment
// timeElapsed once 16 samples passes. We multiply by 16 to get rid of the fraction
// and deal with just integers.
static const int PROCESS_TIMER_TICKS_PER_SAMPLE_X16_1N2_GEN = (500000 << 4) / SAMPLE_RATE;
// For 3rd-gen devices, the timer ticks at 750kHz. This is how much to increment
// timeElapsed once 16 samples passes. We multiply by 16 to get rid of the fraction
// and deal with just integers.
static const int PROCESS_TIMER_TICKS_PER_SAMPLE_X16_3_GEN = (750000 << 4) / SAMPLE_RATE;

TVP::TVP(const Partial *usePartial) :
	partial(usePartial),
	system(&usePartial->getSynth()->mt32ram.system),
	processTimerTicksPerSampleX16(
		partial->getSynth()->controlROMFeatures->quirkFastPitchChanges
		? PROCESS_TIMER_TICKS_PER_SAMPLE_X16_3_GEN
		: PROCESS_TIMER_TICKS_PER_SAMPLE_X16_1N2_GEN)
{}

static Bit16s keyToPitch(unsigned int key) {
	// We're using a table to do: return round_to_nearest_or_even((key - 60) * (4096.0 / 12.0))
	// Banker's rounding is just slightly annoying to do in C++
	int k = int(key);
	Bit16s pitch = keyToPitchTable[abs(k - 60)];
	return key < 60 ? -pitch : pitch;
}

static inline Bit32s coarseToPitch(Bit8u coarse) {
	return (coarse - 36) * 4096 / 12; // One semitone per coarse offset
}

static inline Bit32s fineToPitch(Bit8u fine) {
	return (fine - 50) * 4096 / 1200; // One cent per fine offset
}

static Bit32u calcBasePitch(const Partial *partial, const TimbreParam::PartialParam *partialParam, const MemParams::PatchTemp *patchTemp, unsigned int key, const ControlROMFeatureSet *controlROMFeatures) {
	Bit32s basePitch = keyToPitch(key);
	basePitch = (basePitch * pitchKeyfollowMult[partialParam->wg.pitchKeyfollow]) >> 13; // PORTABILITY NOTE: Assumes arithmetic shift
	basePitch += coarseToPitch(partialParam->wg.pitchCoarse);
	basePitch += fineToPitch(partialParam->wg.pitchFine);
	if (controlROMFeatures->quirkKeyShift) {
		// NOTE:Mok: This is done on MT-32, but not LAPC-I:
		basePitch += coarseToPitch(patchTemp->patch.keyShift + 12);
	}
	basePitch += fineToPitch(patchTemp->patch.fineTune);

	const ControlROMPCMStruct *controlROMPCMStruct = partial->getControlROMPCMStruct();
	if (controlROMPCMStruct != NULL) {
		basePitch += (Bit32s(controlROMPCMStruct->pitchMSB) << 8) | Bit32s(controlROMPCMStruct->pitchLSB);
	} else {
		if ((partialParam->wg.waveform & 1) == 0) {
			basePitch += 37133; // This puts Middle C at around 261.64Hz (assuming no other modifications, masterTune of 64, etc.)
		} else {
			// Sawtooth waves are effectively double the frequency of square waves.
			// Thus we add 4096 less than for square waves here, which results in halving the frequency.
			basePitch += 33037;
		}
	}

	// MT-32 GEN0 does 16-bit calculations here, allowing an integer overflow.
	// This quirk is observable playing the patch defined for timbre "HIT BOTTOM" in Larry 3.
	// Note, the upper bound isn't checked either.
	if (controlROMFeatures->quirkBasePitchOverflow) {
		basePitch = basePitch & 0xffff;
	} else if (basePitch < 0) {
		basePitch = 0;
	} else if (basePitch > 59392) {
		basePitch = 59392;
	}
	return Bit32u(basePitch);
}

static Bit32u calcVeloMult(Bit8u veloSensitivity, unsigned int velocity) {
	if (veloSensitivity == 0) {
		return 21845; // aka floor(4096 / 12 * 64), aka ~64 semitones
	}
	unsigned int reversedVelocity = 127 - velocity;
	unsigned int scaledReversedVelocity;
	if (veloSensitivity > 3) {
		// Note that on CM-32L/LAPC-I veloSensitivity is never > 3, since it's clipped to 3 by the max tables.
		// MT-32 GEN0 has a bug here that leads to unspecified behaviour. We assume it is as follows.
		scaledReversedVelocity = (reversedVelocity << 8) >> ((3 - veloSensitivity) & 0x1f);
	} else {
		scaledReversedVelocity = reversedVelocity << (5 + veloSensitivity);
	}
	// When velocity is 127, the multiplier is 21845, aka ~64 semitones (regardless of veloSensitivity).
	// The lower the velocity, the lower the multiplier. The veloSensitivity determines the amount decreased per velocity value.
	// The minimum multiplier on CM-32L/LAPC-I (with velocity 0, veloSensitivity 3) is 170 (~half a semitone).
	return ((32768 - scaledReversedVelocity) * 21845) >> 15;
}

static Bit32s calcTargetPitchOffsetWithoutLFO(const TimbreParam::PartialParam *partialParam, int levelIndex, unsigned int velocity) {
	int veloMult = calcVeloMult(partialParam->pitchEnv.veloSensitivity, velocity);
	int targetPitchOffsetWithoutLFO = partialParam->pitchEnv.level[levelIndex] - 50;
	targetPitchOffsetWithoutLFO = (targetPitchOffsetWithoutLFO * veloMult) >> (16 - partialParam->pitchEnv.depth); // PORTABILITY NOTE: Assumes arithmetic shift
	return targetPitchOffsetWithoutLFO;
}

void TVP::reset(const Part *usePart, const TimbreParam::PartialParam *usePartialParam) {
	part = usePart;
	partialParam = usePartialParam;
	patchTemp = part->getPatchTemp();

	unsigned int key = partial->getPoly()->getKey();
	unsigned int velocity = partial->getPoly()->getVelocity();

	// FIXME: We're using a per-TVP timer instead of a system-wide one for convenience.
	timeElapsed = 0;
	processTimerIncrement = 0;

	basePitch = calcBasePitch(partial, partialParam, patchTemp, key, partial->getSynth()->controlROMFeatures);
	currentPitchOffset = calcTargetPitchOffsetWithoutLFO(partialParam, 0, velocity);
	targetPitchOffsetWithoutLFO = currentPitchOffset;
	phase = 0;

	if (partialParam->pitchEnv.timeKeyfollow) {
		timeKeyfollowSubtraction = Bit32s(key - 60) >> (5 - partialParam->pitchEnv.timeKeyfollow); // PORTABILITY NOTE: Assumes arithmetic shift
	} else {
		timeKeyfollowSubtraction = 0;
	}
	lfoPitchOffset = 0;
	counter = 0;
	pitch = basePitch;

	// These don't really need to be initialised, but it aids debugging.
	pitchOffsetChangePerBigTick = 0;
	targetPitchOffsetReachedBigTick = 0;
	shifts = 0;
}

Bit32u TVP::getBasePitch() const {
	return basePitch;
}

void TVP::updatePitch() {
	Bit32s newPitch = basePitch + currentPitchOffset;
	if (!partial->isPCM() || (partial->getControlROMPCMStruct()->len & 0x01) == 0) { // FIXME: Use !partial->pcmWaveEntry->unaffectedByMasterTune instead
		// FIXME: There are various bugs not yet emulated
		// 171 is ~half a semitone.
		newPitch += partial->getSynth()->getMasterTunePitchDelta();
	}
	if ((partialParam->wg.pitchBenderEnabled & 1) != 0) {
		newPitch += part->getPitchBend();
	}

	// MT-32 GEN0 does 16-bit calculations here, allowing an integer overflow.
	// This quirk is exploited e.g. in Colonel's Bequest timbres "Lightning" and "SwmpBackgr".
	if (partial->getSynth()->controlROMFeatures->quirkPitchEnvelopeOverflow) {
		newPitch = newPitch & 0xffff;
	} else if (newPitch < 0) {
		newPitch = 0;
	}
	// This check is present in every unit.
	if (newPitch > 59392) {
		newPitch = 59392;
	}
	pitch = Bit16u(newPitch);

	// FIXME: We're doing this here because that's what the CM-32L does - we should probably move this somewhere more appropriate in future.
	partial->getTVA()->recalcSustain();
}

void TVP::targetPitchOffsetReached() {
	currentPitchOffset = targetPitchOffsetWithoutLFO + lfoPitchOffset;

	switch (phase) {
	case 3:
	case 4:
	{
		int newLFOPitchOffset = (part->getModulation() * partialParam->pitchLFO.modSensitivity) >> 7;
		newLFOPitchOffset = (newLFOPitchOffset + partialParam->pitchLFO.depth) << 1;
		if (pitchOffsetChangePerBigTick > 0) {
			// Go in the opposite direction to last time
			newLFOPitchOffset = -newLFOPitchOffset;
		}
		lfoPitchOffset = newLFOPitchOffset;
		int targetPitchOffset = targetPitchOffsetWithoutLFO + lfoPitchOffset;
		setupPitchChange(targetPitchOffset, 101 - partialParam->pitchLFO.rate);
		updatePitch();
		break;
	}
	case 6:
		updatePitch();
		break;
	default:
		nextPhase();
	}
}

void TVP::nextPhase() {
	phase++;
	int envIndex = phase == 6 ? 4 : phase;

	targetPitchOffsetWithoutLFO = calcTargetPitchOffsetWithoutLFO(partialParam, envIndex, partial->getPoly()->getVelocity()); // pitch we'll reach at the end

	int changeDuration = partialParam->pitchEnv.time[envIndex - 1];
	changeDuration -= timeKeyfollowSubtraction;
	if (changeDuration > 0) {
		setupPitchChange(targetPitchOffsetWithoutLFO, changeDuration); // changeDuration between 0 and 112 now
		updatePitch();
	} else {
		targetPitchOffsetReached();
	}
}

// Shifts val to the left until bit 31 is 1 and returns the number of shifts
static Bit8u normalise(Bit32u &val) {
	Bit8u leftShifts;
	for (leftShifts = 0; leftShifts < 31; leftShifts++) {
		if ((val & 0x80000000) != 0) {
			break;
		}
		val = val << 1;
	}
	return leftShifts;
}

void TVP::setupPitchChange(int targetPitchOffset, Bit8u changeDuration) {
	bool negativeDelta = targetPitchOffset < currentPitchOffset;
	Bit32s pitchOffsetDelta = targetPitchOffset - currentPitchOffset;
	if (pitchOffsetDelta > 32767 || pitchOffsetDelta < -32768) {
		pitchOffsetDelta = 32767;
	}
	if (negativeDelta) {
		pitchOffsetDelta = -pitchOffsetDelta;
	}
	// We want to maximise the number of bits of the Bit16s "pitchOffsetChangePerBigTick" we use in order to get the best possible precision later
	Bit32u absPitchOffsetDelta = (pitchOffsetDelta & 0xFFFF) << 16;
	Bit8u normalisationShifts = normalise(absPitchOffsetDelta); // FIXME: Double-check: normalisationShifts is usually between 0 and 15 here, unless the delta is 0, in which case it's 31
	absPitchOffsetDelta = absPitchOffsetDelta >> 1; // Make room for the sign bit

	changeDuration--; // changeDuration's now between 0 and 111
	unsigned int upperDuration = changeDuration >> 3; // upperDuration's now between 0 and 13
	shifts = normalisationShifts + upperDuration + 2;
	Bit16u divisor = lowerDurationToDivisor[changeDuration & 7];
	Bit16s newPitchOffsetChangePerBigTick = ((absPitchOffsetDelta & 0xFFFF0000) / divisor) >> 1; // Result now fits within 15 bits. FIXME: Check nothing's getting sign-extended incorrectly
	if (negativeDelta) {
		newPitchOffsetChangePerBigTick = -newPitchOffsetChangePerBigTick;
	}
	pitchOffsetChangePerBigTick = newPitchOffsetChangePerBigTick;

	int currentBigTick = timeElapsed >> 8;
	int durationInBigTicks = divisor >> (12 - upperDuration);
	if (durationInBigTicks > 32767) {
		durationInBigTicks = 32767;
	}
	// The result of the addition may exceed 16 bits, but wrapping is fine and intended here.
	targetPitchOffsetReachedBigTick = currentBigTick + durationInBigTicks;
}

void TVP::startDecay() {
	phase = 5;
	lfoPitchOffset = 0;
	targetPitchOffsetReachedBigTick = timeElapsed >> 8; // FIXME: Afaict there's no good reason for this - check
}

Bit16u TVP::nextPitch() {
	// We emulate MCU software timer using these counter and processTimerIncrement variables.
	// The value of NOMINAL_PROCESS_TIMER_PERIOD_SAMPLES approximates the period in samples
	// between subsequent firings of the timer that normally occur.
	// However, accurate emulation is quite complicated because the timer is not guaranteed to fire in time.
	// This makes pitch variations on real unit non-deterministic and dependent on various factors.
	if (counter == 0) {
		timeElapsed = (timeElapsed + processTimerIncrement) & 0x00FFFFFF;
		// This roughly emulates pitch deviations observed on real units when playing a single partial that uses TVP/LFO.
		counter = NOMINAL_PROCESS_TIMER_PERIOD_SAMPLES + (rand() & 3);
		processTimerIncrement = (processTimerTicksPerSampleX16 * counter) >> 4;
		process();
	}
	counter--;
	return pitch;
}

void TVP::process() {
	if (phase == 0) {
		targetPitchOffsetReached();
		return;
	}
	if (phase == 5) {
		nextPhase();
		return;
	}
	if (phase > 7) {
		updatePitch();
		return;
	}

	Bit16s negativeBigTicksRemaining = (timeElapsed >> 8) - targetPitchOffsetReachedBigTick;
	if (negativeBigTicksRemaining >= 0) {
		// We've reached the time for a phase change
		targetPitchOffsetReached();
		return;
	}
	// FIXME: Write explanation for this stuff
	// NOTE: Value of shifts may happily exceed the maximum of 31 specified for the 8095 MCU.
	// We assume the device performs a shift with the rightmost 5 bits of the counter regardless of argument size,
	// since shift instructions of any size have the same maximum.
	int rightShifts = shifts;
	if (rightShifts > 13) {
		rightShifts -= 13;
		negativeBigTicksRemaining = negativeBigTicksRemaining >> (rightShifts & 0x1F); // PORTABILITY NOTE: Assumes arithmetic shift
		rightShifts = 13;
	}
	int newResult = (negativeBigTicksRemaining * pitchOffsetChangePerBigTick) >> (rightShifts & 0x1F); // PORTABILITY NOTE: Assumes arithmetic shift
	newResult += targetPitchOffsetWithoutLFO + lfoPitchOffset;
	currentPitchOffset = newResult;
	updatePitch();
}

} // namespace MT32Emu

/************************************************** Tables.cpp ******************************************************/
namespace MT32Emu {

// UNUSED: const int MIDDLEC = 60;

const Tables &Tables::getInstance() {
	static const Tables instance;
	return instance;
}

Tables::Tables() {
	for (int lf = 0; lf <= 100; lf++) {
		// CONFIRMED:KG: This matches a ROM table found by Mok
		float fVal = (2.0f - LOG10F(float(lf) + 1.0f)) * 128.0f;
		int val = int(fVal + 1.0);
		if (val > 255) {
			val = 255;
		}
		levelToAmpSubtraction[lf] = Bit8u(val);
	}

	envLogarithmicTime[0] = 64;
	for (int lf = 1; lf <= 255; lf++) {
		// CONFIRMED:KG: This matches a ROM table found by Mok
		envLogarithmicTime[lf] = Bit8u(ceil(64.0f + LOG2F(float(lf)) * 8.0f));
	}

#if 0
	// The table below is to be used in conjunction with emulation of VCA of newer generation units which is currently missing.
	// These relatively small values are rather intended to fine-tune the overall amplification of the VCA.
	// CONFIRMED: Based on a table found by Mok in the LAPC-I control ROM
	// Note that this matches the MT-32 table, but with the values clamped to a maximum of 8.
	memset(masterVolToAmpSubtraction, 8, 71);
	memset(masterVolToAmpSubtraction + 71, 7, 3);
	memset(masterVolToAmpSubtraction + 74, 6, 4);
	memset(masterVolToAmpSubtraction + 78, 5, 3);
	memset(masterVolToAmpSubtraction + 81, 4, 4);
	memset(masterVolToAmpSubtraction + 85, 3, 3);
	memset(masterVolToAmpSubtraction + 88, 2, 4);
	memset(masterVolToAmpSubtraction + 92, 1, 4);
	memset(masterVolToAmpSubtraction + 96, 0, 5);
#else
	// CONFIRMED: Based on a table found by Mok in the MT-32 control ROM
	masterVolToAmpSubtraction[0] = 255;
	for (int masterVol = 1; masterVol <= 100; masterVol++) {
		masterVolToAmpSubtraction[masterVol] = Bit8u(106.31 - 16.0f * LOG2F(float(masterVol)));
	}
#endif

	for (int i = 0; i <= 100; i++) {
		pulseWidth100To255[i] = Bit8u(i * 255 / 100.0f + 0.5f);
		//synth->printDebug("%d: %d", i, pulseWidth100To255[i]);
	}

	// The LA32 chip contains an exponent table inside. The table contains 12-bit integer values.
	// The actual table size is 512 rows. The 9 higher bits of the fractional part of the argument are used as a lookup address.
	// To improve the precision of computations, the lower bits are supposed to be used for interpolation as the LA32 chip also
	// contains another 512-row table with inverted differences between the main table values.
	for (int i = 0; i < 512; i++) {
		exp9[i] = Bit16u(8191.5f - EXP2F(13.0f + ~i / 512.0f));
	}

	// There is a logarithmic sine table inside the LA32 chip. The table contains 13-bit integer values.
	for (int i = 1; i < 512; i++) {
		logsin9[i] = Bit16u(0.5f - LOG2F(sin((i + 0.5f) / 1024.0f * FLOAT_PI)) * 1024.0f);
	}

	// The very first value is clamped to the maximum possible 13-bit integer
	logsin9[0] = 8191;

	// found from sample analysis
	static const Bit8u resAmpDecayFactorTable[] = {31, 16, 12, 8, 5, 3, 2, 1};
	resAmpDecayFactor = resAmpDecayFactorTable;
}

} // namespace MT32Emu
