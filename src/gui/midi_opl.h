/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../hardware/adlib.h"

#define MIDIHANDLER_ADLIB_USE_INSTRUMENT_REMAPPING 0

// Controls the behavior for calculating note frequency and volume.
#define MIDIHANDLER_ADLIB_ACCURACY_MODE_SB16_WIN95 0 // Accurate to the behavior of the Windows 95 SB16 driver.
#define MIDIHANDLER_ADLIB_ACCURACY_MODE_GM         1 // Accurate to the General MIDI and MIDI specifications.

// Controls the OPL channel allocation behavior.
#define MIDIHANDLER_ADLIB_ALLOCATION_MODE_DYNAMIC  1 // Dynamic channel allocation (new OPL channel allocated to each note played).
#define MIDIHANDLER_ADLIB_ALLOCATION_MODE_STATIC   0 // Static channel allocation (fixed OPL channel allocated to each MIDI channel).

#define MIDIHANDLER_ADLIB_MIN(x, y) ((x) < (y) ? (x) : (y))
#define MIDIHANDLER_ADLIB_WARNING(...) LOG_MSG(__VA_ARGS__)

struct MidiHandler_adlib : public MidiHandler
{
	MidiHandler_adlib() : MidiHandler(), inited(false) {  }
	bool inited;

	const char * GetName(void) { return "adlib"; }

	static bool RETRO_CALLCONV dummy_flush(void) { return true; }

	bool Open(const char * conf)
	{
		return (conf && !strcasecmp(conf, "adlib"));
	}

	void Close(void)
	{
		inited = false;
	}

	enum
	{
		MIDI_CHANNEL_COUNT = 16,
		MIDI_RHYTHM_CHANNEL = 9,
		OPL_NUM_RHYTHM_INSTRUMENTS = 5,
		OPL2_NUM_CHANNELS = 9,
		OPL3_NUM_CHANNELS = 18,

		MIDI_COMMAND_NOTE_OFF = 0x80,
		MIDI_COMMAND_NOTE_ON = 0x90,
		MIDI_COMMAND_POLYPHONIC_AFTERTOUCH = 0xA0,
		MIDI_COMMAND_CONTROL_CHANGE = 0xB0,
		MIDI_COMMAND_PROGRAM_CHANGE = 0xC0,
		MIDI_COMMAND_CHANNEL_AFTERTOUCH = 0xD0,
		MIDI_COMMAND_PITCH_BEND = 0xE0,
		MIDI_COMMAND_SYSTEM = 0xF0,

		MIDI_CONTROLLER_BANK_SELECT_MSB = 0x00,
		MIDI_CONTROLLER_MODULATION = 0x01,
		MIDI_CONTROLLER_DATA_ENTRY_MSB = 0x06,
		MIDI_CONTROLLER_VOLUME = 0x07,
		MIDI_CONTROLLER_BALANCE = 0x08,
		MIDI_CONTROLLER_PANNING = 0x0A,
		MIDI_CONTROLLER_EXPRESSION = 0x0B,
		MIDI_CONTROLLER_BANK_SELECT_LSB = 0x20,
		MIDI_CONTROLLER_DATA_ENTRY_LSB = 0x26,
		MIDI_CONTROLLER_SUSTAIN = 0x40,
		MIDI_CONTROLLER_PORTAMENTO = 0x41,
		MIDI_CONTROLLER_SOSTENUTO = 0x42,
		MIDI_CONTROLLER_SOFT = 0x43,
		MIDI_CONTROLLER_REVERB = 0x5B,
		MIDI_CONTROLLER_CHORUS = 0x5D,
		MIDI_CONTROLLER_RPN_LSB = 0x64,
		MIDI_CONTROLLER_RPN_MSB = 0x65,
		MIDI_CONTROLLER_ALL_SOUND_OFF = 0x78,
		MIDI_CONTROLLER_RESET_ALL_CONTROLLERS = 0x79,
		MIDI_CONTROLLER_ALL_NOTES_OFF = 0x7B,
		MIDI_CONTROLLER_OMNI_ON = 0x7C,
		MIDI_CONTROLLER_OMNI_OFF = 0x7D,
		MIDI_CONTROLLER_MONO_ON = 0x7E,
		MIDI_CONTROLLER_POLY_ON = 0x7F,

		MIDI_RPN_PITCH_BEND_SENSITIVITY = 0x0000,
		MIDI_RPN_MASTER_TUNING_FINE = 0x0001,
		MIDI_RPN_MASTER_TUNING_COARSE = 0x0002,
		MIDI_RPN_NULL = 0x7F7F,

		MIDI_PITCH_BEND_DEFAULT = 0x2000,
		MIDI_PANNING_DEFAULT = 0x40,
		MIDI_EXPRESSION_DEFAULT = 0x7F,

		MIDI_META_END_OF_TRACK = 0x2F,

		MIDI_MASTER_TUNING_FINE_DEFAULT = 0x2000,
		MIDI_MASTER_TUNING_COARSE_DEFAULT = 0x40,

		GM_PITCH_BEND_SENSITIVITY_DEFAULT = 0x02,

		GS_RHYTHM_FIRST_NOTE = 0x1B,
		GS_RHYTHM_LAST_NOTE = 0x58,

		/**
		 * OPL test and timer registers.
		 */
		OPL_REGISTER_TEST = 0x01,
		OPL_REGISTER_TIMER1 = 0x02,
		OPL_REGISTER_TIMER2 = 0x03,
		OPL_REGISTER_TIMERCONTROL = 0x04,

		/**
		 * OPL global setting registers.
		 */
		OPL_REGISTER_NOTESELECT_CSM = 0x08,
		OPL_REGISTER_RHYTHM = 0xBD,

		/**
		 * OPL operator base registers.
		 */
		OPL_REGISTER_BASE_FREQMULT_MISC = 0x20,
		OPL_REGISTER_BASE_LEVEL = 0x40,
		OPL_REGISTER_BASE_DECAY_ATTACK = 0x60,
		OPL_REGISTER_BASE_RELEASE_SUSTAIN = 0x80,
		OPL_REGISTER_BASE_WAVEFORMSELECT = 0xE0,

		/**
		 * OPL channel base registers.
		 */
		OPL_REGISTER_BASE_FNUMLOW = 0xA0,
		OPL_REGISTER_BASE_FNUMHIGH_BLOCK_KEYON = 0xB0,
		OPL_REGISTER_BASE_CONNECTION_FEEDBACK_PANNING = 0xC0,

		/**
		 * OPL3-specific global setting registers.
		 */
		OPL3_REGISTER_CONNECTIONSELECT = 0x104,
		OPL3_REGISTER_NEW = 0x105,

		/**
		 * Offset to the second register set (for dual OPL2 and OPL3).
		 */
		OPL_REGISTER_SET_2_OFFSET = 0x100,

		/**
		 * Bitmasks for various parameters in the OPL registers.
		 */
		OPL_MASK_LEVEL = 0x3F,
		OPL_MASK_FNUMHIGH_BLOCK = 0x1F,
		OPL_MASK_KEYON = 0x20,
		OPL_MASK_PANNING = 0x30,

		/**
		 * Settings for the panning bits in the OPL Cx registers.
		 */
		OPL_PANNING_CENTER = 0x30,
		OPL_PANNING_LEFT = 0x10,
		OPL_PANNING_RIGHT = 0x20,

		/**
		 * The lowest MIDI panning controller value interpreted as left panning.
		 */
		OPL_MIDI_PANNING_LEFT_LIMIT = 0x2F,
		/**
		 * The highest MIDI panning controller value interpreted as right panning.
		 */
		OPL_MIDI_PANNING_RIGHT_LIMIT = 0x51,
	};

	void PlayMsg(Bit8u * msg)
	{
		if (!inited && !init()) return;

		// Extract the MIDI bytes.
		Bit8u command = msg[0] & 0xF0;
		Bit8u channel = msg[0] & 0x0F;

		switch (command) {
		case MIDI_COMMAND_NOTE_OFF:
			noteOff(channel, msg[1], msg[2]);
			break;
		case MIDI_COMMAND_NOTE_ON:
			noteOn(channel, msg[1], msg[2]);
			break;
		case MIDI_COMMAND_POLYPHONIC_AFTERTOUCH: // Not supported by GM
			polyAftertouch(channel, msg[1], msg[2]);
			break;
		case MIDI_COMMAND_CONTROL_CHANGE:
			controlChange(channel, msg[1], msg[2]);
			break;
		case MIDI_COMMAND_PROGRAM_CHANGE:
			programChange(channel, msg[1]);
			break;
		case MIDI_COMMAND_CHANNEL_AFTERTOUCH:
			channelAftertouch(channel, msg[1]);
			break;
		case MIDI_COMMAND_PITCH_BEND:
			pitchBend(channel, msg[1], msg[2]);
			break;
		case MIDI_COMMAND_SYSTEM:
			// The only supported system event is SysEx and that should be sent
			// using the sysEx functions.
			MIDIHANDLER_ADLIB_WARNING("MidiDriver_ADLIB_Multisource: send received system event (not processed): %x", msg[0]);
			break;
		default:
			MIDIHANDLER_ADLIB_WARNING("MidiDriver_ADLIB_Multisource: Received unknown event %02x", command);
			break;
		}
	}

	void PlaySysex(Bit8u * msg,Bitu length)
	{
		if (!inited && !init()) return;

		// was void sysEx(const Bit8u *msg, Bit16u length) {
		if (length >= 4 && msg[0] == 0x7E && msg[2] == 0x09 && msg[3] == 0x01) {
			// F0 7E <device ID> 09 01 F7
			// General MIDI System On

			// Reset the MIDI context and the OPL chip.

			stopAllNotes(true);

			for (int j = 0; j < MIDI_CHANNEL_COUNT; j++) {
				_controlData[j].init();
			}

			setRhythmMode();

			for (int i = 0; i < _numMelodicChannels; i++) {
				_activeNotes[_melodicChannels[i]].init();
			}

			memset(_channelAllocations, 0xFF, sizeof(_channelAllocations));
			_noteCounter = 1;

			initOpl();
		} else {
			// Ignore other SysEx messages.
			MIDIHANDLER_ADLIB_WARNING("sysEx - Unrecognized SysEx");
		}
	}

	bool init() {
		Adlib::Module* adlib_module = OPL_GetActiveModule();
		if (!adlib_module) return false;
		_opl = adlib_module; //->handler;
		_oplType = adlib_module->oplmode;

		_rhythmModeIgnoreNoteOffs = false;
		_noteSelect = NOTE_SELECT_MODE_0;

		// MidiDriver_ADLIB_Multisource
		//_modulationDepth = MODULATION_DEPTH_HIGH;
		//_vibratoDepth = VIBRATO_DEPTH_HIGH;
		//_defaultChannelVolume = 0;

		// MidiDriver_ADLIB_MADE
		_modulationDepth = MODULATION_DEPTH_LOW;
		_vibratoDepth = VIBRATO_DEPTH_LOW;
		_defaultChannelVolume = 0x7F;

		_rhythmMode = false;
		_instrumentBank = OPL_INSTRUMENT_BANK;
		_rhythmBank = OPL_RHYTHM_BANK;
		_rhythmBankFirstNote = GS_RHYTHM_FIRST_NOTE;
		_rhythmBankLastNote = GS_RHYTHM_LAST_NOTE;
		_melodicChannels = nullptr;
		_numMelodicChannels = 0;
		_noteCounter = 1;
		_oplFrequencyConversionFactor = (float)(pow(2, 20) / 49716.0f); // make constant?
		memset(_channelAllocations, 0xFF, sizeof(_channelAllocations));
		memset(_shadowRegisters, 0, sizeof(_shadowRegisters));

		// Set default MIDI channel volume on control data.
		for (int j = 0; j < MIDI_CHANNEL_COUNT; j++) {
			_controlData[j].init();
			_controlData[j].volume = _defaultChannelVolume;
		}
		for (int i = 0; i < OPL3_NUM_CHANNELS; i++) {
			_activeNotes[i].init();
		}
		for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
			_activeRhythmNotes[i].init();
		}

		// Set the melodic channels applicable for the OPL chip type.
		determineMelodicChannels();

		// Set default OPL register values.
		initOpl();

		inited = true;
		return true;
	}

	OPL_Mode _oplType;
	Adlib::Module* _opl;

	/**
	 * Rhythm instrument types used by the OPL2 and OPL3 rhythm mode.
	 */
	enum OplInstrumentRhythmType {
		RHYTHM_TYPE_UNDEFINED,
		RHYTHM_TYPE_HI_HAT,
		RHYTHM_TYPE_CYMBAL,
		RHYTHM_TYPE_TOM_TOM,
		RHYTHM_TYPE_SNARE_DRUM,
		RHYTHM_TYPE_BASS_DRUM
	};

	/**
	 * Data for one operator of an OPL instrument definition.
	 */
	struct OplInstrumentOperatorDefinition {
		/**
		 * 2x register: frequency multiplier, key scaling rate, envelope gain type,
		 * vibrato and modulation.
		 */
		Bit8u freqMultMisc;
		/**
		 * 4x register: level and key scaling level.
		 */
		Bit8u level;
		/**
		 * 6x register: decay and attack.
		 */
		Bit8u decayAttack;
		/**
		 * 8x register: release and sustain.
		 */
		Bit8u releaseSustain;
		/**
		 * Ex register: waveform select.
		 */
		Bit8u waveformSelect;

		/**
		 * Check if this operator definition contains any data.
		 *
		 * @return True if this operator is empty; false otherwise.
		 */
		bool isEmpty() { return freqMultMisc == 0 && level == 0 && decayAttack == 0 && releaseSustain == 0 && waveformSelect == 0; }
	};

	/**
	 * Instrument definition for an OPL2 or OPL3 chip. Contains the data for all
	 * registers belonging to an OPL channel, except the Ax and Bx registers (these
	 * determine the frequency and are derived from the note played).
	 */
	struct OplInstrumentDefinition {
		/**
		 * Indicates if this instrument uses 2 or 4 operators.
		 */
		bool fourOperator;

		/**
		 * Operator data. 2 operator instruments use operators 0 and 1 only.
		 */
		OplInstrumentOperatorDefinition operator0;
		OplInstrumentOperatorDefinition operator1;
		OplInstrumentOperatorDefinition operator2;
		OplInstrumentOperatorDefinition operator3;

		/**
		 * Cx register: connection and feedback.
		 * Note: panning is determined by a MIDI controller and not part of the
		 * instrument definition.
		 */
		Bit8u connectionFeedback0;
		/**
		 * Second Cx register (used by 4 operator instruments).
		 */
		Bit8u connectionFeedback1;

		/**
		 * Notes played on a MIDI rhythm channel indicate which rhythm instrument
		 * should be played, not which note should be played. This field indicates
		 * the pitch (MIDI note) which should be used to play this rhythm
		 * instrument. Not used for melodic instruments.
		 */
		Bit8u rhythmNote;
		/**
		 * The type of OPL rhythm instrument that this definition should be used
		 * with. Type undefined indicates that this definition should not be used
		 * with rhythm mode.
		 */
		OplInstrumentRhythmType rhythmType;

		/**
		 * Check if this instrument definition contains any data.
		 *
		 * @return True if this instrument is empty; false otherwise.
		 */
		bool isEmpty() {
			if (rhythmType != RHYTHM_TYPE_UNDEFINED) {
				return operator0.isEmpty() &&
					(rhythmType != RHYTHM_TYPE_BASS_DRUM || operator1.isEmpty());
			} else if (!fourOperator) {
				return operator0.isEmpty() && operator1.isEmpty();
			} else {
				return operator0.isEmpty() && operator1.isEmpty() &&
					operator2.isEmpty() && operator3.isEmpty();
			}
		}
		/**
		 * Returns the number of operators used by this instrument definition.
		 *
		 * @return The number of operators (2 or 4).
		 */
		Bit8u getNumberOfOperators() {
			if (rhythmType == RHYTHM_TYPE_UNDEFINED) {
				return fourOperator ? 4 : 2;
			} else {
				// The bass drum rhythm instrument uses 2 operators; the others use
				// only 1.
				return rhythmType == RHYTHM_TYPE_BASS_DRUM ? 2 : 1;
			}
		}
		/**
		 * Returns the definition data for the operator with the specified number.
		 * Specify 0 or 1 for 2 operator instruments or 0-3 for 4 operator
		 * instruments.
		 *
		 * @param operatorNum The operator for which the data should be returned.
		 * @return Pointer to the definition data for the specified operator.
		 */
		OplInstrumentOperatorDefinition &getOperatorDefinition(Bit8u operatorNum) {
			assert((!fourOperator && operatorNum < 2) || operatorNum < 4);

			switch (operatorNum) {
			case 0:
				return operator0;
			case 1:
				return operator1;
			case 2:
				return operator2;
			case 3:
				return operator3;
			default:
				// Should not happen.
				return operator0;
			}
		}
	};

	/**
	 * OPL instrument data for playing a note.
	 */
	struct InstrumentInfo {
		/**
		 * MIDI note value to use for playing this instrument
		 * (@see ActiveNote.oplNote).
		 */
		Bit8u oplNote;
		/**
		 * Pointer to the instrument definition.
		 */
		OplInstrumentDefinition *instrumentDef;
		/**
		 * Unique identifer for this instrument (@see ActiveNote.instrumentId).
		 */
		Bit8u instrumentId;
	};

	/**
	 * Contains the current controller settings for a MIDI channel.
	 */
	struct MidiChannelControlData {
		Bit8u program;
		Bit8u channelPressure;
		Bit16u pitchBend; // 14 bit value; 0x2000 is neutral

		Bit8u modulation;
		Bit8u volume;
		Bit8u panning; // 0x40 is center
		Bit8u expression;
		bool sustain;
		Bit16u rpn; // Two 7 bit values stored in 8 bits each

		Bit8u pitchBendSensitivity; // Semitones
		Bit8u pitchBendSensitivityCents;
		Bit16u masterTuningFine; // 14 bit value; 0x2000 is neutral
		Bit8u masterTuningCoarse; // Semitones; 0x40 is neutral

		/**
		 * Initializes the controller settings to default values.
		 */
		void init() {
			program = 0;
			channelPressure = 0;
			pitchBend = MIDI_PITCH_BEND_DEFAULT;

			modulation = 0;
			volume = 0;
			panning = MIDI_PANNING_DEFAULT;
			expression = MIDI_EXPRESSION_DEFAULT;
			sustain = false;
			rpn = MIDI_RPN_NULL;

			pitchBendSensitivity = GM_PITCH_BEND_SENSITIVITY_DEFAULT;
			pitchBendSensitivityCents = 0;
			masterTuningFine = MIDI_MASTER_TUNING_FINE_DEFAULT;
			masterTuningCoarse = MIDI_MASTER_TUNING_COARSE_DEFAULT;
		}
	};

	/**
	 * Contains information on the currently active note on an OPL channel.
	 */
	struct ActiveNote {
		/**
		 * True if a note is currently playing (including if it is sustained,
		 * but not if it is in the "release" phase).
		 */
		bool noteActive;
		/**
		 * True if the currently playing note is sustained, i.e. note has been
		 * turned off but is kept active due to the sustain controller.
		 */
		bool noteSustained;

		/**
		 * The MIDI note value as it appeared in the note on event.
		 */
		Bit8u note;
		/**
		 * The MIDI velocity value of the note on event.
		 */
		Bit8u velocity;
		/**
		 * The MIDI channel that played the current/last note (0xFF if no note
		 * has been played since initialization).
		 */
		Bit8u channel;

		/**
		 * The MIDI note value that is actually played. This is the same as the
		 * note field for melodic instruments, but on the MIDI rhythm channel
		 * the note indicates which rhythm instrument should be played instead
		 * of the pitch. In that case this field is different
		 * (@see determineInstrument).
		 */
		Bit8u oplNote;
		/**
		 * The OPL frequency (F-num) and octave (block) (in Ax (low byte) and
		 * Bx (high byte) register format) that was calculated to play the MIDI
		 * note.
		 */
		Bit16u oplFrequency;
		/**
		 * The value of the note counter when a note was last turned on or off
		 * on this OPL channel.
		 */
		Bit32u noteCounterValue;

		/**
		 * A unique identifier of the instrument that is used to play the note.
		 * In the default implementation this is the MIDI program number for
		 * melodic instruments and the rhythm channel note number + 0x80 for
		 * rhythm instruments (@see determineInstrument).
		 */
		Bit8u instrumentId;
		/**
		 * Pointer to the instrument definition used to play the note.
		 */
		OplInstrumentDefinition *instrumentDef;

		/**
		 * True if this OPL channel has been allocated to a MIDI channel.
		 * Note that in the default driver implementation only the static
		 * channel allocation algorithm uses this field.
		 */
		bool channelAllocated;

		/**
		 * Initializes the active note data to default values.
		 */
		void init() {
			noteActive = false;
			noteSustained = false;

			note = 0;
			velocity = 0;
			channel = 0xFF;

			oplNote = 0;
			oplFrequency = 0;
			noteCounterValue = 0;

			instrumentId = 0;
			instrumentDef = nullptr;

			channelAllocated = false;
		}
	};

	/**
	 * The available modes for the OPL note select setting.
	 */
	enum NoteSelectMode {
		NOTE_SELECT_MODE_0,
		NOTE_SELECT_MODE_1
	};

	/**
	 * The available modes for the OPL modulation depth setting.
	 */
	enum ModulationDepth {
		/**
		 * Low modulation depth (1 dB).
		 */
		MODULATION_DEPTH_LOW,
		/**
		 * High modulation depth (4.8 dB).
		 */
		MODULATION_DEPTH_HIGH
	};

	/**
	 * The available modes for the OPL vibrato depth setting.
	 */
	enum VibratoDepth {
		/**
		 * Low vibrato depth (7 %).
		 */
		VIBRATO_DEPTH_LOW,
		/**
		 * High vibrato depth (14 %).
		 */
		VIBRATO_DEPTH_HIGH
	};

	/**
	 * The melodic channel numbers available on an OPL2 chip with rhythm mode
	 * disabled.
	 */
	static Bit8u MELODIC_CHANNELS_OPL2[9];
	/**
	 * The melodic channel numbers available on an OPL2 chip with rhythm mode
	 * enabled.
	 */
	static Bit8u MELODIC_CHANNELS_OPL2_RHYTHM[6];
	/**
	 * The melodic channel numbers available on an OPL3 chip with rhythm mode
	 * disabled.
	 */
	static Bit8u MELODIC_CHANNELS_OPL3[18];
	/**
	 * The melodic channel numbers available on an OPL3 chip with rhythm mode
	 * enabled.
	 */
	static Bit8u MELODIC_CHANNELS_OPL3_RHYTHM[15];

	/**
	 * Offsets for the rhythm mode instrument registers.
	 */
	static const Bit8u OPL_REGISTER_RHYTHM_OFFSETS[OPL_NUM_RHYTHM_INSTRUMENTS];

	/**
	 * The OPL channels used by the rhythm instruments, in order:
	 * hi-hat, cymbal, tom tom, snare drum, bass drum.
	 */
	static const Bit8u OPL_RHYTHM_INSTRUMENT_CHANNELS[OPL_NUM_RHYTHM_INSTRUMENTS];

	/**
	 * The default melodic instrument definitions.
	 */
	static OplInstrumentDefinition OPL_INSTRUMENT_BANK[];
	/**
	 * The default rhythm instrument definitions.
	 */
	static OplInstrumentDefinition OPL_RHYTHM_BANK[];

	// Controls response to rhythm note off events when rhythm mode is active.
	bool _rhythmModeIgnoreNoteOffs;

	// The default MIDI channel volume (set when opening the driver).
	Bit8u _defaultChannelVolume;

	// OPL global settings. Set these, then call oplInit or open to apply the
	// new values.
	NoteSelectMode _noteSelect;
	ModulationDepth _modulationDepth;
	VibratoDepth _vibratoDepth;
	// Current OPL rhythm mode setting. Use setRhythmMode to set and activate.
	bool _rhythmMode;

	// Pointer to the melodic instrument definitions.
	OplInstrumentDefinition *_instrumentBank;
	// Pointer to the rhythm instrument definitions.
	OplInstrumentDefinition *_rhythmBank;
	// The MIDI note value of the first rhythm instrument in the bank.
	Bit8u _rhythmBankFirstNote;
	// The MIDI note value of the last rhythm instrument in the bank.
	Bit8u _rhythmBankLastNote;

	// The current MIDI controller values for each MIDI channel.
	MidiChannelControlData _controlData[MIDI_CHANNEL_COUNT];
	// The active note data for each OPL channel.
	ActiveNote _activeNotes[OPL3_NUM_CHANNELS];
	// The active note data for the OPL rhythm instruments.
	ActiveNote _activeRhythmNotes[OPL_NUM_RHYTHM_INSTRUMENTS];
	// The OPL channel allocated to each MIDI channel; 0xFF if a
	// MIDI channel has no OPL channel allocated. Note that this is only used by
	// the static channel allocation mode.
	Bit8u _channelAllocations[MIDI_CHANNEL_COUNT];
	// Array containing the numbers of the available melodic channels.
	Bit8u *_melodicChannels;
	// The number of available melodic channels (length of _melodicChannels).
	Bit8u _numMelodicChannels;
	// The amount of notes played since the driver was opened / reset.
	Bit32u _noteCounter;

	// Factor to convert a frequency in Hertz to the format used by the OPL
	// registers (F - num).
	float _oplFrequencyConversionFactor;
	// The values last written to each OPL register.
	Bit8u _shadowRegisters[0x200];

	void noteOff(Bit8u channel, Bit8u note, Bit8u velocity) {
		if (_rhythmMode && channel == MIDI_RHYTHM_CHANNEL) {
			if (!_rhythmModeIgnoreNoteOffs) {
				// Find the OPL rhythm instrument playing this note.
				for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
					if (_activeRhythmNotes[i].noteActive &&
							_activeRhythmNotes[i].note == note) {
						writeKeyOff(0, static_cast<OplInstrumentRhythmType>(i + 1));
						break;
					}
				}
			}
		} else {
			// Find the OPL channel playing this note.
			for (int i = 0; i < _numMelodicChannels; i++) {
				Bit8u oplChannel = _melodicChannels[i];
				if (_activeNotes[oplChannel].noteActive &&
						_activeNotes[oplChannel].channel == channel && _activeNotes[oplChannel].note == note) {
					if (_controlData[channel].sustain) {
						// Sustain controller is on. Sustain the note instead of
						// ending it.
						_activeNotes[oplChannel].noteSustained = true;
					} else {
						writeKeyOff(oplChannel);
					}
				}
			}
		}
	}

	void noteOn(Bit8u channel, Bit8u note, Bit8u velocity) {
		if (velocity == 0) {
			// Note on with velocity 0 is a note off.
			noteOff(channel, note, velocity);
			return;
		}

		InstrumentInfo instrument = determineInstrument(channel, note);
		// If rhythm mode is on and the note is on the rhythm channel, this note
		// will be played using the OPL rhythm register.
		bool rhythmNote = _rhythmMode && channel == MIDI_RHYTHM_CHANNEL;

		if (!instrument.instrumentDef || instrument.instrumentDef->isEmpty() ||
				(rhythmNote && instrument.instrumentDef->rhythmType == RHYTHM_TYPE_UNDEFINED)) {
			// Instrument definition contains no data or it is not suitable for
			// rhythm mode, so the note cannot be played.
			return;
		}

		// Determine the OPL channel to use and the active note data to update.
		Bit8u oplChannel = 0xFF;
		ActiveNote *activeNote = nullptr;
		if (rhythmNote) {
			activeNote = &_activeRhythmNotes[instrument.instrumentDef->rhythmType - 1];
		} else {
			// Allocate a melodic OPL channel.
			oplChannel = allocateOplChannel(channel, instrument.instrumentId);
			if (oplChannel != 0xFF)
				activeNote = &_activeNotes[oplChannel];
		}
		if (activeNote != nullptr) {
			if (activeNote->noteActive) {
				// Turn off the note currently playing on this OPL channel or
				// rhythm instrument.
				writeKeyOff(oplChannel, instrument.instrumentDef->rhythmType);
			}

			// Update the active note data.
			activeNote->noteActive = true;
			activeNote->noteSustained = false;
			activeNote->note = note;
			activeNote->velocity = velocity;
			activeNote->channel = channel;

			activeNote->oplNote = instrument.oplNote;
			// Increase the note counter when playing a new note.
			activeNote->noteCounterValue = _noteCounter++;
			activeNote->instrumentId = instrument.instrumentId;
			activeNote->instrumentDef = instrument.instrumentDef;

			// Write out the instrument definition, volume and panning.
			writeInstrument(oplChannel, instrument);

			// Calculate and write frequency and block and write key on bit.
			writeFrequency(oplChannel, instrument.instrumentDef->rhythmType);

			if (rhythmNote)
				// Update the rhythm register.
				writeRhythm();
		}
	}

	void polyAftertouch(Bit8u channel, Bit8u note, Bit8u pressure) {
		// Because this event is not required by General MIDI and not implemented
		// in the Win95 SB16 driver, there is no default implementation.
	}

	void controlChange(Bit8u channel, Bit8u controller, Bit8u value) {
		// Call the function for handling each controller.
		switch (controller) {
		case MIDI_CONTROLLER_MODULATION:
			modulation(channel, value);
			break;
		case MIDI_CONTROLLER_DATA_ENTRY_MSB:
			dataEntry(channel, value, 0xFF);
			break;
		case MIDI_CONTROLLER_VOLUME:
			volume(channel, value);
			break;
		case MIDI_CONTROLLER_PANNING:
			panning(channel, value);
			break;
		case MIDI_CONTROLLER_EXPRESSION:
			expression(channel, value);
			break;
		case MIDI_CONTROLLER_DATA_ENTRY_LSB:
			dataEntry(channel, 0xFF, value);
			break;
		case MIDI_CONTROLLER_SUSTAIN:
			sustain(channel, value);
			break;
		case MIDI_CONTROLLER_RPN_LSB:
			registeredParameterNumber(channel, 0xFF, value);
			break;
		case MIDI_CONTROLLER_RPN_MSB:
			registeredParameterNumber(channel, value, 0xFF);
			break;
		case MIDI_CONTROLLER_ALL_SOUND_OFF:
			allSoundOff(channel);
			break;
		case MIDI_CONTROLLER_RESET_ALL_CONTROLLERS:
			resetAllControllers(channel);
			break;
		case MIDI_CONTROLLER_ALL_NOTES_OFF:
		case MIDI_CONTROLLER_OMNI_OFF:
		case MIDI_CONTROLLER_OMNI_ON:
		case MIDI_CONTROLLER_MONO_ON:
		case MIDI_CONTROLLER_POLY_ON:
			// The omni/mono/poly events also act as an all notes off.
			allNotesOff(channel);
			break;
		default:
			//debug("MidiDriver_ADLIB_Multisource::controlChange - Unsupported controller %X", controller);
			break;
		}
	}

	void programChange(Bit8u channel, Bit8u program) {
		// Just set the MIDI program value; this event does not affect active notes.
		_controlData[channel].program = program;
	}

	void channelAftertouch(Bit8u channel, Bit8u pressure) {
		// Even though this event is required by General MIDI, it is not implemented
		// in the Win95 SB16 driver, so there is no default implementation.
	}

	void pitchBend(Bit8u channel, Bit8u pitchBendLsb, Bit8u pitchBendMsb) {
		_controlData[channel].pitchBend = ((Bit16u)pitchBendMsb) << 7 | pitchBendLsb;

		// Recalculate and write the frequencies of the active notes on this MIDI
		// channel to let the new pitch bend value take effect.
		recalculateFrequencies(channel);
	}

	void modulation(Bit8u channel, Bit8u modulation) {
		// Even though this controller is required by General MIDI, it is not
		// implemented in the Win95 SB16 driver, so there is no default
		// implementation.
	}

	void dataEntry(Bit8u channel, Bit8u dataMsb, Bit8u dataLsb) {
		// Set the data on the currently active RPN.
		switch (_controlData[channel].rpn) {
		case MIDI_RPN_PITCH_BEND_SENSITIVITY:
			// MSB = semitones, LSB = cents.
			if (dataMsb != 0xFF) {
				_controlData[channel].pitchBendSensitivity = dataMsb;
			}
			if (dataLsb != 0xFF) {
				_controlData[channel].pitchBendSensitivityCents = dataLsb;
			}
			// Apply the new pitch bend sensitivity to any active notes.
			recalculateFrequencies(channel);
			break;
		case MIDI_RPN_MASTER_TUNING_FINE:
			// MSB and LSB are combined to a fraction of a semitone.
			if (dataMsb != 0xFF) {
				_controlData[channel].masterTuningFine &= 0x00FF;
				_controlData[channel].masterTuningFine |= dataMsb << 8;
			}
			if (dataLsb != 0xFF) {
				_controlData[channel].masterTuningFine &= 0xFF00;
				_controlData[channel].masterTuningFine |= dataLsb;
			}
			// Apply the new master tuning to any active notes.
			recalculateFrequencies(channel);
			break;
		case MIDI_RPN_MASTER_TUNING_COARSE:
			// MSB = semitones, LSB is ignored.
			if (dataMsb != 0xFF) {
				_controlData[channel].masterTuningCoarse = dataMsb;
			}
			// Apply the new master tuning to any active notes.
			recalculateFrequencies(channel);
			break;
		default:
			// Ignore data entry if null or an unknown RPN is active.
			break;
		}
	}

	void volume(Bit8u channel, Bit8u volume) {
		if (_controlData[channel].volume == volume)
			return;

		_controlData[channel].volume = volume;
		// Apply the new channel volume to any active notes.
		recalculateVolumes(channel);
	}

	void panning(Bit8u channel, Bit8u panning) {
		if (_controlData[channel].panning == panning)
			return;

		_controlData[channel].panning = panning;

		// Apply the new channel panning to any active notes.
		if (_rhythmMode && channel == MIDI_RHYTHM_CHANNEL) {
			for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
				if (_activeRhythmNotes[i].noteActive) {
					writePanning(0xFF, static_cast<OplInstrumentRhythmType>(i + 1));
				}
			}
		} else {
			for (int i = 0; i < _numMelodicChannels; i++) {
				Bit8u oplChannel = _melodicChannels[i];
				if (_activeNotes[oplChannel].noteActive && _activeNotes[oplChannel].channel == channel) {
					writePanning(oplChannel);
				}
			}
		}
	}

	void expression(Bit8u channel, Bit8u expression) {
		if (_controlData[channel].expression == expression)
			return;

		_controlData[channel].expression = expression;
		// Apply the new expression value to any active notes.
		recalculateVolumes(channel);
	}

	void sustain(Bit8u channel, Bit8u sustain) {
		if (sustain >= 0x40) {
			// Turn on sustain.
			_controlData[channel].sustain = true;
		} else if (_controlData[channel].sustain) {
			// Sustain is currently on. Turn it off.
			_controlData[channel].sustain = false;

			// Turn off any sustained notes on this channel.
			for (int i = 0; i < _numMelodicChannels; i++) {
				Bit8u oplChannel = _melodicChannels[i];
				if (_activeNotes[oplChannel].noteActive && _activeNotes[oplChannel].noteSustained &&
						_activeNotes[oplChannel].channel == channel) {
					writeKeyOff(oplChannel);
				}
			}
		}
	}

	void registeredParameterNumber(Bit8u channel, Bit8u rpnMsb, Bit8u rpnLsb) {
		// Set the currently active RPN. MSB and LSB combined form the RPN number.
		if (rpnMsb != 0xFF) {
			_controlData[channel].rpn &= 0x00FF;
			_controlData[channel].rpn |= rpnMsb << 8;
		}
		if (rpnLsb != 0xFF) {
			_controlData[channel].rpn &= 0xFF00;
			_controlData[channel].rpn |= rpnLsb;
		}
	}

	void allSoundOff(Bit8u channel) {
		// It is not possible to immediately terminate the sound on an OPL chip
		// (skipping the "release" of the notes), so just turn the notes off.
		stopAllNotes(channel);
	}

	void resetAllControllers(Bit8u channel) {
		modulation(channel, 0);
		expression(channel, MIDI_EXPRESSION_DEFAULT);
		sustain(channel, 0);
		registeredParameterNumber(channel, MIDI_RPN_NULL >> 8, MIDI_RPN_NULL & 0xFF);
		pitchBend(channel, MIDI_PITCH_BEND_DEFAULT & 0x7F, MIDI_PITCH_BEND_DEFAULT >> 7);
		channelAftertouch(channel, 0);
		// TODO Polyphonic aftertouch should also be reset; not implemented because
		// polyphonic aftertouch is not implemented.
	}

	void allNotesOff(Bit8u channel) {
		// Execute a note off for all active notes on this MIDI channel. This will
		// turn the notes off if sustain is off and sustain the notes if it is on.
		if (_rhythmMode && channel == MIDI_RHYTHM_CHANNEL) {
			for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
				if (_activeRhythmNotes[i].noteActive) {
					noteOff(channel, _activeRhythmNotes[i].note, 0);
				}
			}
		} else {
			for (int i = 0; i < _numMelodicChannels; i++) {
				Bit8u oplChannel = _melodicChannels[i];
				if (_activeNotes[oplChannel].noteActive && !_activeNotes[oplChannel].noteSustained &&
						_activeNotes[oplChannel].channel == channel) {
					noteOff(channel, _activeNotes[oplChannel].note, 0);
				}
			}
		}
	}

	void stopAllNotes(bool stopSustainedNotes) {
		// Just write the key off bit on all OPL channels. No special handling is
		// needed to make sure sustained notes are turned off.
		for (int i = 0; i < _numMelodicChannels; i++) {
			// Force the register write to prevent accidental hanging notes.
			writeKeyOff(_melodicChannels[i], RHYTHM_TYPE_UNDEFINED, true);
		}
		if (_rhythmMode) {
			for (int i = 0; i < 5; i++) {
				_activeRhythmNotes[i].noteActive = false;
			}
			writeRhythm(true);
		}
	}

	void stopAllNotes(Bit8u channel) {
		// Write the key off bit for all active notes on this MIDI channel.
		for (int i = 0; i < _numMelodicChannels; i++) {
			Bit8u oplChannel = _melodicChannels[i];
			if (_activeNotes[oplChannel].noteActive &&
				(channel == 0xFF || _activeNotes[oplChannel].channel == channel)) {
				writeKeyOff(oplChannel);
			}
		}
		if (_rhythmMode && !_rhythmModeIgnoreNoteOffs && (channel == 0xFF || channel == MIDI_RHYTHM_CHANNEL)) {
			bool rhythmChanged = false;
			for (int i = 0; i < 5; i++) {
				if (_activeRhythmNotes[i].noteActive) {
					_activeRhythmNotes[i].noteActive = false;
					rhythmChanged = true;
				}
			}
			if (rhythmChanged)
				writeRhythm();
		}
	}

	void initOpl() {
		// Clear test flags and enable waveform select for OPL2 chips.
		writeRegister(OPL_REGISTER_TEST, _oplType == OPL_opl3 ? 0 : 0x20, true);
		if (_oplType != OPL_opl2) {
			writeRegister(OPL_REGISTER_TEST | OPL_REGISTER_SET_2_OFFSET, _oplType == OPL_opl3 ? 0 : 0x20, true);
		}

		// Clear, stop and mask the timers and reset the interrupt.
		writeRegister(OPL_REGISTER_TIMER1, 0, true);
		writeRegister(OPL_REGISTER_TIMER2, 0, true);
		writeRegister(OPL_REGISTER_TIMERCONTROL, 0x60, true);
		writeRegister(OPL_REGISTER_TIMERCONTROL, 0x80, true);
		if (_oplType == OPL_dualopl2) {
			writeRegister(OPL_REGISTER_TIMER1 | OPL_REGISTER_SET_2_OFFSET, 0, true);
			writeRegister(OPL_REGISTER_TIMER2 | OPL_REGISTER_SET_2_OFFSET, 0, true);
			writeRegister(OPL_REGISTER_TIMERCONTROL | OPL_REGISTER_SET_2_OFFSET, 0x60, true);
			writeRegister(OPL_REGISTER_TIMERCONTROL | OPL_REGISTER_SET_2_OFFSET, 0x80, true);
		}

		if (_oplType == OPL_opl3) {
			// Turn off 4 operator mode for all channels.
			writeRegister(OPL3_REGISTER_CONNECTIONSELECT, 0, true);
			// Enable "new" OPL3 functionality.
			writeRegister(OPL3_REGISTER_NEW, 1, true);
		}

		// Set note select mode and disable CSM mode for OPL2 chips.
		writeRegister(OPL_REGISTER_NOTESELECT_CSM, _noteSelect << 6, true);
		if (_oplType == OPL_dualopl2) {
			writeRegister(OPL_REGISTER_NOTESELECT_CSM | OPL_REGISTER_SET_2_OFFSET, _noteSelect << 6, true);
		}

		// Set operator registers to default values.
		for (int i = 0; i < 5; i++) {
			Bit8u baseReg = 0;
			Bit8u value = 0;
			switch (i) {
			case 0:
				baseReg = OPL_REGISTER_BASE_FREQMULT_MISC;
				break;
			case 1:
				baseReg = OPL_REGISTER_BASE_LEVEL;
				// Set volume to the default MIDI channel volume.
				// Convert from MIDI to OPL register value.
				value = 0x3F - (_defaultChannelVolume >> 1);
				break;
			case 2:
				baseReg = OPL_REGISTER_BASE_DECAY_ATTACK;
				break;
			case 3:
				baseReg = OPL_REGISTER_BASE_RELEASE_SUSTAIN;
				break;
			case 4:
				baseReg = OPL_REGISTER_BASE_WAVEFORMSELECT;
				break;
			}

			for (int j = 0; j < (_oplType == OPL_opl2 ? OPL2_NUM_CHANNELS : OPL3_NUM_CHANNELS); j++) {
				writeRegister(baseReg + determineOperatorRegisterOffset(j, 0), value, true);
				writeRegister(baseReg + determineOperatorRegisterOffset(j, 1), value, true);
			}
		}

		// Set channel registers to default values.
		for (int i = 0; i < 3; i++) {
			Bit8u baseReg = 0;
			Bit8u value = 0;
			switch (i) {
			case 0:
				baseReg = OPL_REGISTER_BASE_FNUMLOW;
				break;
			case 1:
				baseReg = OPL_REGISTER_BASE_FNUMHIGH_BLOCK_KEYON;
				break;
			case 2:
				baseReg = OPL_REGISTER_BASE_CONNECTION_FEEDBACK_PANNING;
				if (_oplType == OPL_opl3) {
					// Set default panning to center.
					value = OPL_PANNING_CENTER;
				}
				break;
			}

			for (int j = 0; j < (_oplType == OPL_opl2 ? OPL2_NUM_CHANNELS : OPL3_NUM_CHANNELS); j++) {
				writeRegister(baseReg + determineChannelRegisterOffset(j), value, true);
			}
		}

		// Set rhythm mode, modulation and vibrato depth.
		writeRhythm(true);
	}

	void recalculateFrequencies(Bit8u channel) {
		// Calculate and write the frequency of all active notes on this MIDI
		// channel.
		if (_rhythmMode && channel == MIDI_RHYTHM_CHANNEL) {
			// Always rewrite bass drum frequency if it is active.
			if (_activeRhythmNotes[RHYTHM_TYPE_BASS_DRUM - 1].noteActive) {
				writeFrequency(0xFF, RHYTHM_TYPE_BASS_DRUM);
			}

			// Snare drum and hi-hat share the same frequency setting. If both are
			// active, use the most recently played instrument.
			OplInstrumentRhythmType rhythmType = RHYTHM_TYPE_UNDEFINED;
			bool snareActive = _activeRhythmNotes[RHYTHM_TYPE_SNARE_DRUM - 1].noteActive;
			bool hiHatActive = _activeRhythmNotes[RHYTHM_TYPE_HI_HAT - 1].noteActive;
			if (snareActive && hiHatActive) {
				rhythmType = (_activeRhythmNotes[RHYTHM_TYPE_SNARE_DRUM - 1].noteCounterValue >=
					_activeRhythmNotes[RHYTHM_TYPE_HI_HAT - 1].noteCounterValue ? RHYTHM_TYPE_SNARE_DRUM : RHYTHM_TYPE_HI_HAT);
			} else if (snareActive) {
				rhythmType = RHYTHM_TYPE_SNARE_DRUM;
			} else if (hiHatActive) {
				rhythmType = RHYTHM_TYPE_HI_HAT;
			}
			if (rhythmType != RHYTHM_TYPE_UNDEFINED)
				writeFrequency(0xFF, rhythmType);

			// Tom tom and cymbal share the same frequency setting. If both are
			// active, use the most recently played instrument.
			rhythmType = RHYTHM_TYPE_UNDEFINED;
			bool tomTomActive = _activeRhythmNotes[RHYTHM_TYPE_TOM_TOM - 1].noteActive;
			bool cymbalActive = _activeRhythmNotes[RHYTHM_TYPE_CYMBAL - 1].noteActive;
			if (tomTomActive && cymbalActive) {
				rhythmType = (_activeRhythmNotes[RHYTHM_TYPE_TOM_TOM - 1].noteCounterValue >=
					_activeRhythmNotes[RHYTHM_TYPE_CYMBAL - 1].noteCounterValue ? RHYTHM_TYPE_TOM_TOM : RHYTHM_TYPE_CYMBAL);
			} else if (tomTomActive) {
				rhythmType = RHYTHM_TYPE_TOM_TOM;
			} else if (cymbalActive) {
				rhythmType = RHYTHM_TYPE_CYMBAL;
			}
			if (rhythmType != RHYTHM_TYPE_UNDEFINED)
				writeFrequency(0xFF, rhythmType);
		} else {
			for (int i = 0; i < _numMelodicChannels; i++) {
				Bit8u oplChannel = _melodicChannels[i];
				if (_activeNotes[oplChannel].noteActive && _activeNotes[oplChannel].channel == channel) {
					writeFrequency(oplChannel);
				}
			}
		}
	}

	void recalculateVolumes(Bit8u channel) {
		// Calculate and write the volume of all operators of all active notes on
		// this MIDI channel.
		for (int i = 0; i < _numMelodicChannels; i++) {
			Bit8u oplChannel = _melodicChannels[i];
			if (_activeNotes[oplChannel].noteActive &&
					(channel == 0xFF || _activeNotes[oplChannel].channel == channel)) {
				for (int j = 0; j < _activeNotes[oplChannel].instrumentDef->getNumberOfOperators(); j++) {
					writeVolume(oplChannel, j);
				}
			}
		}
		if (_rhythmMode && (channel == 0xFF || channel == MIDI_RHYTHM_CHANNEL)) {
			for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
				if (_activeRhythmNotes[i].noteActive) {
					for (int j = 0; j < _activeRhythmNotes[i].instrumentDef->getNumberOfOperators(); j++) {
						writeVolume(0xFF, j, static_cast<OplInstrumentRhythmType>(i + 1));
					}
				}
			}
		}
	}

	InstrumentInfo determineInstrument(Bit8u channel, Bit8u note) {
		InstrumentInfo instrument = { 0, nullptr, 0 };

		if (channel == MIDI_RHYTHM_CHANNEL) {
			// On the rhythm channel, the note played indicates which instrument
			// should be used.
			if (note < _rhythmBankFirstNote || note > _rhythmBankLastNote)
				// No rhythm instrument assigned to this note number.
				return instrument;

			// Set the high bit for rhythm instrument IDs.
			instrument.instrumentId = 0x80 | note;
			instrument.instrumentDef = &_rhythmBank[note - _rhythmBankFirstNote];
			// Get the note to play from the instrument definition.
			instrument.oplNote = instrument.instrumentDef->rhythmNote;
		} else {
			// On non-rhythm channels, use the active instrument (program) on the
			// MIDI channel.
			Bit8u program = _controlData[channel].program;
#if MIDIHANDLER_ADLIB_USE_INSTRUMENT_REMAPPING
			if (_instrumentRemapping)
				// Apply instrument remapping (if specified).
				program = _instrumentRemapping[program];
#endif
			instrument.instrumentId = program;
			instrument.instrumentDef = &_instrumentBank[instrument.instrumentId];
			instrument.oplNote = note;
		}

		return instrument;
	}

	Bit8u allocateOplChannel(Bit8u channel, Bit8u instrumentId) {

		Bit8u allocatedChannel = 0xFF;
#if MIDIHANDLER_ADLIB_ALLOCATION_MODE_DYNAMIC //		if (_allocationMode == ALLOCATION_MODE_DYNAMIC) {
			// In dynamic channel allocation mode, each note is allocated a new
			// OPL channel. The following criteria are used, in this order:
			// - The channel with the lowest number that has not yet been used to
			//   play a note (note counter value is 0).
			// - The channel with the lowest note counter value that is not
			//   currently playing a note.
			// - The channel with the lowest note counter value that is playing a
			//   note using the same instrument.
			// - The channel with the lowest note counter value (i.e. playing the
			//   oldest note).
			// This will always return a channel; if a note is currently playing,
			// it will be aborted.

			Bit8u unusedChannel = 0xFF, inactiveChannel = 0xFF, instrumentChannel = 0xFF, lowestCounterChannel = 0xFF;
			Bit32u inactiveNoteCounter = 0xFFFF, instrumentNoteCounter = 0xFFFF, lowestNoteCounter = 0xFFFF;
			for (int i = 0; i < _numMelodicChannels; i++) {
				Bit8u oplChannel = _melodicChannels[i];
				if (_activeNotes[oplChannel].channelAllocated)
					// Channel has been statically allocated. Try the next channel.
					continue;

				if (_activeNotes[oplChannel].noteCounterValue == 0) {
					// This channel is unused. No need to look any further.
					unusedChannel = oplChannel;
					break;
				}
				if (!_activeNotes[oplChannel].noteActive && _activeNotes[oplChannel].noteCounterValue < inactiveNoteCounter) {
					// A channel not playing a note with a lower note counter value
					// has been found.
					inactiveNoteCounter = _activeNotes[oplChannel].noteCounterValue;
					inactiveChannel = oplChannel;
					continue;
				}
				if (_activeNotes[oplChannel].noteActive && _activeNotes[oplChannel].instrumentId == instrumentId &&
						_activeNotes[oplChannel].noteCounterValue < instrumentNoteCounter) {
					// A channel playing a note using the same instrument with a
					// lower note counter value has been found.
					instrumentNoteCounter = _activeNotes[oplChannel].noteCounterValue;
					instrumentChannel = oplChannel;
				}
				if (_activeNotes[oplChannel].noteActive && _activeNotes[oplChannel].noteCounterValue < lowestNoteCounter) {
					// A channel playing a note with a lower note counter value has
					// been found.
					lowestNoteCounter = _activeNotes[oplChannel].noteCounterValue;
					lowestCounterChannel = oplChannel;
				}
			}

			if (unusedChannel != 0xFF)
				// An unused channel has been found. Use this.
				allocatedChannel = unusedChannel;
			else if (inactiveChannel != 0xFF)
				// An inactive channel has been found. Use this.
				allocatedChannel = inactiveChannel;
			else if (instrumentChannel != 0xFF)
				// An active channel using the same instrument has been found.
				// Use this.
				allocatedChannel = instrumentChannel;
			else
				// Just use the channel playing the oldest note.
				allocatedChannel = lowestCounterChannel;
#elif MIDIHANDLER_ADLIB_ALLOCATION_MODE_STATIC //		} else {
			// In static allocation mode, each MIDI channel is
			// allocated a fixed OPL channel to use. All notes on that MIDI channel
			// are played using the allocated OPL channel. If a new MIDI channel
			// needs an OPL channel and all OPL channels have already been
			// allocated, allocation will fail.

			allocatedChannel = 0xFF;

			if (_channelAllocations[channel] != 0xFF) {
				// An OPL channel has already been allocated to this MIDI channel.
				// Use the previously allocated channel.
				allocatedChannel = _channelAllocations[channel];
			} else {
				// No OPL channel has been allocated yet. Find a free OPL channel.
				for (int i = 0; i < _numMelodicChannels; i++) {
					Bit8u oplChannel = _melodicChannels[i];
					if (!_activeNotes[oplChannel].channelAllocated) {
						// Found a free channel. Allocate this.
						_activeNotes[oplChannel].channelAllocated = true;
						_activeNotes[oplChannel].channel = channel;

						_channelAllocations[channel] = oplChannel;

						allocatedChannel = oplChannel;

						break;
					}
				}
				// If no free channel could be found, allocatedChannel will be 0xFF.
			}
#else
#error Missing MIDIHANDLER_ADLIB_ALLOCATION_MODE_*
#endif //		}

		return allocatedChannel;
	}

	void determineMelodicChannels() {
		if (_oplType == OPL_opl2 || _oplType == OPL_dualopl2) {
			_numMelodicChannels = OPL2_NUM_CHANNELS;
			if (_rhythmMode) {
				// Rhythm mode uses 3 OPL channels for rhythm instruments.
				_numMelodicChannels -= 3;
				_melodicChannels = MELODIC_CHANNELS_OPL2_RHYTHM;
			} else {
				// Use all available OPL channels as melodic channels.
				_melodicChannels = MELODIC_CHANNELS_OPL2;
			}
		} else {
			_numMelodicChannels = OPL3_NUM_CHANNELS;
			if (_rhythmMode) {
				_numMelodicChannels -= 3;
				_melodicChannels = MELODIC_CHANNELS_OPL3_RHYTHM;
			} else {
				_melodicChannels = MELODIC_CHANNELS_OPL3;
			}
		}
	}

	Bit16u calculateFrequency(Bit8u channel, Bit8u note) {
		// Split note into octave and octave note.
		Bit8u octaveNote = note % 12;
		Bit8u octave = note / 12;

		// Calculate OPL octave (block) and frequency (F-num).
		Bit8u block;
		Bit32u oplFrequency;

#if MIDIHANDLER_ADLIB_ACCURACY_MODE_SB16_WIN95 //		if (_accuracyMode == ACCURACY_MODE_SB16_WIN95) {
			// Frequency calculation using the algorithm of the Win95 SB16 driver.

			// Look up the octave note OPL frequency. These values assume octave 5.
			oplFrequency = OPL_NOTE_FREQUENCIES[octaveNote];
			// Correct for octaves other than 5 by doubling or halving the OPL
			// frequency for each octave higher or lower, respectively.
			if (octave > 5) {
				oplFrequency <<= (octave - 5);
			} else {
				oplFrequency >>= (5 - octave);
			}
			// The resulting value is likely larger than the 10 bit length of the
			// F-num in the OPL registers. This is correct later by increasing the
			// block.
			block = 1;
#elif MIDIHANDLER_ADLIB_ACCURACY_MODE_GM //		} else {
			// Frequency calculation using a more accurate algorithm.

			// Calculate the note frequency in Hertz by relating it to a known
			// frequency (in this case A4 (0x45) = 440 Hz). Formula is
			// freq * 2 ^ (semitones / 12).
			float noteFrequency = 440.0f * (powf(2, (note - 0x45) / 12.0f));
			// Convert the frequency in Hz to the format used by the OPL registers.
			// Note that the resulting value is double the actual frequency because
			// of the use of block 0 (which halves the frequency). This allows for
			// slightly higher precision in the pitch bend calculation.
			oplFrequency = (Bit32u)round(noteFrequency * _oplFrequencyConversionFactor);
			block = 0;
#else
#error Missing MIDIHANDLER_ADLIB_ACCURACY_MODE_*
#endif //		}

		// Calculate and apply pitch bend and tuning.
		oplFrequency += calculatePitchBend(channel, oplFrequency);

		// Shift the frequency down to the 10 bits used by the OPL registers.
		// Increase the block to compensate.
		while (oplFrequency > 0x3FF) {
			oplFrequency >>= 1;
			block++;
		}
		// Maximum supported block value is 7, so clip higher values. The highest
		// MIDI notes exceed the maximum OPL frequency, so these will be transposed
		// down 1 or 2 octaves.
		block = MIDIHANDLER_ADLIB_MIN(block, (Bit8u)7);

		// Combine the block and frequency in the OPL Ax and Bx register format.
		return oplFrequency | (block << 10);
	}

	Bit32s calculatePitchBend(Bit8u channel, Bit16u oplFrequency) {
		Bit32s pitchBend;

#if MIDIHANDLER_ADLIB_ACCURACY_MODE_SB16_WIN95 //		if (_accuracyMode == ACCURACY_MODE_SB16_WIN95) {
			// Pitch bend calculation using the algorithm of the Win95 SB16 driver.

			// Convert the 14 bit MIDI pitch bend value to a 16 bit signed value.
			// WORKAROUND The conversion to signed in the Win95 SB16 driver is
			// slightly inaccurate and causes minimum pitch bend to underflow to
			// maximum pitch bend. This is corrected here by clipping the result to
			// the int16 minimum value.
			pitchBend = MAX(-0x8000, (_controlData[channel].pitchBend << 2) - 0x8001);
			// Scale pitch bend by 0x1F (up) or 0x1B (down), which is a fixed
			// distance of 2 semitones up or down (pitch bend sensitivity is not
			// supported by this algorithm).
			pitchBend *= (pitchBend > 0 ? 0x1F : 0x1B);
			pitchBend >>= 8;
			// Scale by the OPL note frequency.
			pitchBend *= oplFrequency;
			pitchBend >>= 0xF;
#elif MIDIHANDLER_ADLIB_ACCURACY_MODE_GM //		} else {
			// Pitch bend calculation using a more accurate algorithm.

			// Calculate the pitch bend in cents.
			Bit16s signedPitchBend = _controlData[channel].pitchBend - 0x2000;
			Bit16u pitchBendSensitivityCents = (_controlData[channel].pitchBendSensitivity * 100) +
				_controlData[channel].pitchBendSensitivityCents;
			// Pitch bend upwards has 1 less resolution than downwards
			// (0x2001-0x3FFF vs 0x0000-0x1FFF).
			float pitchBendCents = signedPitchBend * pitchBendSensitivityCents /
				(signedPitchBend > 0 ? 8191.0f : 8192.0f);
			// Calculate the tuning in cents.
			float tuningCents = ((_controlData[channel].masterTuningCoarse - 0x40) * 100) +
				((_controlData[channel].masterTuningFine - 0x2000) * 100 / 8192.0f);

			// Calculate pitch bend (formula is freq * 2 ^ (cents / 1200)).
			// Note that if unrealistically large values for pitch bend sensitivity
			// and/or tuning are used, the result could overflow int32. Since this is
			// far into the ultrasonic frequencies, this should not occur in practice.
			pitchBend = (Bit32s)round(oplFrequency * pow(2, (pitchBendCents + tuningCents) / 1200.0f) - oplFrequency);
#else
#error Missing MIDIHANDLER_ADLIB_ACCURACY_MODE_*
#endif //		}

		return pitchBend;
	}

	Bit8u calculateVolume(Bit8u channel, Bit8u velocity, OplInstrumentDefinition &instrumentDef, Bit8u operatorNum) {
		// Get the volume (level) for this operator from the instrument definition.
		Bit8u operatorDefVolume = instrumentDef.getOperatorDefinition(operatorNum).level & 0x3F;

		// Determine if volume settings should be applied to this operator. Carrier
		// operators in FM synthesis and all operators in additive synthesis need
		// to have volume settings applied; modulator operators just use the
		// instrument definition volume.
		bool applyVolume = false;
		if (instrumentDef.rhythmType != RHYTHM_TYPE_UNDEFINED) {
			applyVolume = (instrumentDef.rhythmType != RHYTHM_TYPE_BASS_DRUM || operatorNum == 1);
		} else if (instrumentDef.fourOperator) {
			// 4 operator instruments have 4 different operator connections.
			Bit8u connection = (instrumentDef.connectionFeedback0 & 0x01) | ((instrumentDef.connectionFeedback1 & 0x01) << 1);
			switch (connection) {
			case 0x00:
				// 4FM
				// Operator 3 is a carrier.
				applyVolume = (operatorNum == 3);
				break;
			case 0x01:
				// 1ADD+3FM
				// Operator 0 is additive and operator 3 is a carrier.
				applyVolume = (operatorNum == 0 || operatorNum == 3);
				break;
			case 0x10:
				// 2FM+2FM
				// Operators 1 and 3 are carriers.
				applyVolume = (operatorNum == 1 || operatorNum == 3);
				break;
			case 0x11:
				// 1ADD+2FM+1ADD
				// Operators 0 and 3 are additive and operator 2 is a carrier.
				applyVolume = (operatorNum == 0 || operatorNum == 2 || operatorNum == 3);
				break;
			default:
				// Should not happen.
				applyVolume = false;
			}
		} else {
			// 2 operator instruments have 2 different operator connections:
			// additive (0x01) or FM (0x00) synthesis.  Carrier operators in FM
			// synthesis and all operators in additive synthesis need to have
			// volume settings applied; modulator operators just use the instrument
			// definition volume. In FM synthesis connection, operator 1 is a
			// carrier.
			applyVolume = (instrumentDef.connectionFeedback0 & 0x01) == 0x01 || operatorNum == 1;
		}
		if (!applyVolume)
			// No need to apply volume settings; just use the instrument definition
			// operator volume.
			return operatorDefVolume;

		// Calculate the volume based on note velocity, channel volume and
		// expression.
		Bit8u unscaledVolume = calculateUnscaledVolume(channel, velocity, instrumentDef, operatorNum);

		Bit8u invertedVolume = 0x3F - unscaledVolume;

		Bit8u scaledVolume = 0x3F - invertedVolume;

		return scaledVolume;
	}

	Bit8u calculateUnscaledVolume(Bit8u channel, Bit8u velocity, OplInstrumentDefinition &instrumentDef, Bit8u operatorNum) {
		Bit8u unscaledVolume;
		// Get the volume (level) for this operator from the instrument definition.
		Bit8u operatorVolume = instrumentDef.getOperatorDefinition(operatorNum).level & 0x3F;

#if MIDIHANDLER_ADLIB_ACCURACY_MODE_SB16_WIN95 //		if (_accuracyMode == ACCURACY_MODE_SB16_WIN95) {
			// Volume calculation using the algorithm of the Win95 SB16 driver.

			// Shift velocity and channel volume to a 5 bit value and look up the OPL
			// volume value.
			Bit8u velocityVolume = OPL_VOLUME_LOOKUP[velocity >> 2];
			Bit8u channelVolume = OPL_VOLUME_LOOKUP[_controlData[channel].volume >> 2];
			// Add velocity and channel OPL volume to get the unscaled volume. The
			// operator volume is an additional (negative) volume adjustment to balance
			// the instruments.
			// Note that large OPL volume values can exceed the 0x3F limit; this is
			// handled below. (0x3F means maximum attenuation - no sound.)
			unscaledVolume = velocityVolume + channelVolume + operatorVolume;
#elif MIDIHANDLER_ADLIB_ACCURACY_MODE_GM //		} else {
			// Volume calculation using an algorithm more accurate to the General MIDI
			// standard.

			// Calculate the volume in dB according to the GM formula:
			// 40 log(velocity * volume * expression / 127 ^ 3)
			// Note that velocity is not specified in detail in the MIDI standards;
			// we use the same volume curve as channel volume and expression.
			float volumeDb = 40 * log10f((velocity * _controlData[channel].volume * _controlData[channel].expression) / 2048383.0f);
			// Convert to OPL volume (every unit is 0.75 dB attenuation). The
			// operator volume is an additional (negative) volume adjustment to balance
			// the instruments.
			unscaledVolume = (Bit8u)(volumeDb / -0.75f + operatorVolume);
#else
#error Missing MIDIHANDLER_ADLIB_ACCURACY_MODE_*
#endif //		}

		// Clip the volume to the maximum value.
		return MIDIHANDLER_ADLIB_MIN((Bit8u)0x3F, unscaledVolume);
	}

	Bit8u calculatePanning(Bit8u channel) {
		if (_oplType != OPL_opl3)
			return 0;

		// MIDI panning is converted to OPL panning using these values:
		// 0x00...L...0x2F 0x30...C...0x50 0x51...R...0x7F
		if (_controlData[channel].panning <= OPL_MIDI_PANNING_LEFT_LIMIT) {
			return OPL_PANNING_LEFT;
		} else if (_controlData[channel].panning >= OPL_MIDI_PANNING_RIGHT_LIMIT) {
			return OPL_PANNING_RIGHT;
		} else {
			return OPL_PANNING_CENTER;
		}
	}

	void setRhythmMode(bool rhythmMode = false) {
		if (_rhythmMode == rhythmMode)
			return;

		if (!_rhythmMode && rhythmMode) {
			// Rhythm mode is turned on.

			// Reset the OPL channels that will be used for rhythm mode.
			for (int i = 6; i <= 8; i++) {
				writeKeyOff(i);
				_channelAllocations[i] = 0xFF;
				_activeNotes[i].init();
			}
			// Initialize the rhythm note data.
			for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
				_activeRhythmNotes[i].init();
			}
		} else if (_rhythmMode && !rhythmMode) {
			// Rhythm mode is turned off.
			// Turn off any active rhythm notes.
			for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
				_activeRhythmNotes[i].noteActive = false;
			}
		}
		_rhythmMode = rhythmMode;

		determineMelodicChannels();
		writeRhythm();
	}

	Bit16u determineOperatorRegisterOffset(Bit8u oplChannel, Bit8u operatorNum, OplInstrumentRhythmType rhythmType = RHYTHM_TYPE_UNDEFINED, bool fourOperator = false) {
		assert(!fourOperator || oplChannel < 6);
		assert(fourOperator || operatorNum < 2);

		Bit16u offset = 0;
		if (rhythmType != RHYTHM_TYPE_UNDEFINED) {
			// Look up the offset for rhythm instruments.
			offset = OPL_REGISTER_RHYTHM_OFFSETS[rhythmType - 1];
			if (rhythmType == RHYTHM_TYPE_BASS_DRUM && operatorNum == 1)
				// Bass drum is the only rhythm instrument with 2 operators.
				offset += 3;
		} else if (fourOperator) {
			// 4 operator register offset for each channel and operator:
			//
			// Channel  | 0 | 1 | 2 | 0 | 1 | 2 | 0 | 1 | 2 | 0 | 1 | 2 |
			// Operator | 0         | 1         | 2         | 3         |
			// Register | 0 | 1 | 2 | 3 | 4 | 5 | 8 | 9 | A | B | C | D |
			//
			// Channels 3-5 are in the second register set (add 0x100 to the register).
			offset += (oplChannel / 3) * OPL_REGISTER_SET_2_OFFSET;
			offset += operatorNum / 2 * 8;
			offset += (operatorNum % 2) * 3;
			offset += oplChannel % 3;
		} else {
			// 2 operator register offset for each channel and operator:
			//
			// Channel  | 0 | 1 | 2 | 0 | 1 | 2 | 3 | 4 | 5 | 3 | 4 | 5 | 6 | 7 | 8 | 6 | 7 | 8 |
			// Operator | 0         | 1         | 0         | 1         | 0         | 1         |
			// Register | 0 | 1 | 2 | 3 | 4 | 5 | 8 | 9 | A | B | C | D |10 |11 |12 |13 |14 |15 |
			//
			// Channels 9-17 are in the second register set (add 0x100 to the register).
			offset += (oplChannel / 9) * OPL_REGISTER_SET_2_OFFSET;
			offset += (oplChannel % 9) / 3 * 8;
			offset += (oplChannel % 9) % 3;
			offset += operatorNum * 3;
		}

		return offset;
	}

	Bit16u determineChannelRegisterOffset(Bit8u oplChannel, bool fourOperator = false) {
		assert(!fourOperator || oplChannel < 6);

		// In 4 operator mode, only the first three channel registers are used in
		// each register set.
		Bit8u numChannelsPerSet = fourOperator ? 3 : 9;
		Bit16u offset = (oplChannel / numChannelsPerSet) * OPL_REGISTER_SET_2_OFFSET;
		return offset + (oplChannel % numChannelsPerSet);
	}

	void writeInstrument(Bit8u oplChannel, InstrumentInfo instrument) {
		ActiveNote *activeNote = (instrument.instrumentDef->rhythmType == RHYTHM_TYPE_UNDEFINED ? &_activeNotes[oplChannel] : &_activeRhythmNotes[instrument.instrumentDef->rhythmType - 1]);
		activeNote->instrumentDef = instrument.instrumentDef;

		// Calculate operator volumes and write operator definitions to
		// the OPL registers.
		for (int i = 0; i < instrument.instrumentDef->getNumberOfOperators(); i++) {
			Bit16u operatorOffset = determineOperatorRegisterOffset(oplChannel, i, instrument.instrumentDef->rhythmType, instrument.instrumentDef->fourOperator);
			const OplInstrumentOperatorDefinition &operatorDef = instrument.instrumentDef->getOperatorDefinition(i);
			writeRegister(OPL_REGISTER_BASE_FREQMULT_MISC + operatorOffset, operatorDef.freqMultMisc);
			writeVolume(oplChannel, i, instrument.instrumentDef->rhythmType);
			writeRegister(OPL_REGISTER_BASE_DECAY_ATTACK + operatorOffset, operatorDef.decayAttack);
			writeRegister(OPL_REGISTER_BASE_RELEASE_SUSTAIN + operatorOffset, operatorDef.releaseSustain);
			writeRegister(OPL_REGISTER_BASE_WAVEFORMSELECT + operatorOffset, operatorDef.waveformSelect);
		}

		// Determine and write panning and write feedback and connection.
		writePanning(oplChannel, instrument.instrumentDef->rhythmType);
	}

	void writeKeyOff(Bit8u oplChannel, OplInstrumentRhythmType rhythmType = RHYTHM_TYPE_UNDEFINED, bool forceWrite = false) {
		ActiveNote *activeNote = nullptr;
		if (rhythmType == RHYTHM_TYPE_UNDEFINED) {
			// Melodic instrument.
			activeNote = &_activeNotes[oplChannel];
			// Rewrite the current Bx register value with the key on bit set to 0.
			writeRegister(OPL_REGISTER_BASE_FNUMHIGH_BLOCK_KEYON + determineChannelRegisterOffset(oplChannel),
				(activeNote->oplFrequency >> 8) & OPL_MASK_FNUMHIGH_BLOCK, forceWrite);
		} else {
			// Rhythm instrument.
			activeNote = &_activeRhythmNotes[rhythmType - 1];
		}

		// Update the active note data.
		activeNote->noteActive = false;
		activeNote->noteSustained = false;
		// Register the current note counter value when turning off a note.
		activeNote->noteCounterValue = _noteCounter;

		if (rhythmType != RHYTHM_TYPE_UNDEFINED) {
			// Rhythm instrument. Rewrite the rhythm register.
			writeRhythm();
		}
	}

	void writeRhythm(bool forceWrite = false) {
		Bit8u value = (_modulationDepth << 7) | (_vibratoDepth << 6) | ((_rhythmMode ? 1 : 0) << 5);
		if (_rhythmMode) {
			// Add the key on bits for each rhythm instrument.
			for (int i = 0; i < OPL_NUM_RHYTHM_INSTRUMENTS; i++) {
				value |= ((_activeRhythmNotes[i].noteActive ? 1 : 0) << i);
			}
		}

		writeRegister(OPL_REGISTER_RHYTHM, value, forceWrite);
		if (_oplType == OPL_dualopl2) {
			writeRegister(OPL_REGISTER_RHYTHM | OPL_REGISTER_SET_2_OFFSET, value, forceWrite);
		}
	}

	void writeVolume(Bit8u oplChannel, Bit8u operatorNum, OplInstrumentRhythmType rhythmType = RHYTHM_TYPE_UNDEFINED) {
		ActiveNote *activeNote = (rhythmType == RHYTHM_TYPE_UNDEFINED ? &_activeNotes[oplChannel] : &_activeRhythmNotes[rhythmType - 1]);

		// Calculate operator volume.
		Bit16u registerOffset = determineOperatorRegisterOffset(
			oplChannel, operatorNum, rhythmType, activeNote->instrumentDef->fourOperator);
		const OplInstrumentOperatorDefinition &operatorDef =
			activeNote->instrumentDef->getOperatorDefinition(operatorNum);
		Bit8u level = calculateVolume(activeNote->channel, activeNote->velocity,
			*activeNote->instrumentDef, operatorNum);

		// Add key scaling level from the operator definition to the calculated
		// level.
		writeRegister(OPL_REGISTER_BASE_LEVEL + registerOffset, level | (operatorDef.level & ~OPL_MASK_LEVEL));
	}

	void writePanning(Bit8u oplChannel, OplInstrumentRhythmType rhythmType = RHYTHM_TYPE_UNDEFINED) {
		ActiveNote *activeNote;
		if (rhythmType != RHYTHM_TYPE_UNDEFINED) {
			activeNote = &_activeRhythmNotes[rhythmType - 1];
			oplChannel = OPL_RHYTHM_INSTRUMENT_CHANNELS[rhythmType - 1];
		} else {
			activeNote = &_activeNotes[oplChannel];
		}

		// Calculate channel panning.
		Bit16u registerOffset = determineChannelRegisterOffset(
			oplChannel, activeNote->instrumentDef->fourOperator);
		Bit8u panning = calculatePanning(activeNote->channel);

		// Add connection and feedback from the instrument definition to the
		// calculated panning.
		writeRegister(OPL_REGISTER_BASE_CONNECTION_FEEDBACK_PANNING + registerOffset,
			panning | (activeNote->instrumentDef->connectionFeedback0 & ~OPL_MASK_PANNING));
		if (activeNote->instrumentDef->fourOperator)
			// TODO Not sure if panning is necessary here.
			writeRegister(OPL_REGISTER_BASE_CONNECTION_FEEDBACK_PANNING + registerOffset + 3,
				panning | (activeNote->instrumentDef->connectionFeedback1 & ~OPL_MASK_PANNING));
	}

	void writeFrequency(Bit8u oplChannel, OplInstrumentRhythmType rhythmType = RHYTHM_TYPE_UNDEFINED) {
		ActiveNote *activeNote;
		if (rhythmType != RHYTHM_TYPE_UNDEFINED) {
			activeNote = &_activeRhythmNotes[rhythmType - 1];
			oplChannel = OPL_RHYTHM_INSTRUMENT_CHANNELS[rhythmType - 1];
		} else {
			activeNote = &_activeNotes[oplChannel];
		}

		// Calculate the frequency.
		Bit16u channelOffset = determineChannelRegisterOffset(oplChannel, activeNote->instrumentDef->fourOperator);
		Bit16u frequency = calculateFrequency(activeNote->channel, activeNote->oplNote);
		activeNote->oplFrequency = frequency;

		// Write the low 8 frequency bits.
		writeRegister(OPL_REGISTER_BASE_FNUMLOW + channelOffset, frequency & 0xFF);
		// Write the high 2 frequency bits and block and add the key on bit.
		writeRegister(OPL_REGISTER_BASE_FNUMHIGH_BLOCK_KEYON + channelOffset,
			(frequency >> 8) | (rhythmType == RHYTHM_TYPE_UNDEFINED && activeNote->noteActive ? OPL_MASK_KEYON : 0));
	}

	void writeRegister(Bit16u reg, Bit8u value, bool forceWrite = false) {
		//debug("Writing register %X %X", reg, value);

		// Write the value to the register if it is a timer register, if forceWrite
		// is specified or if the new register value is different from the current
		// value.
		if ((reg >= 1 && reg <= 3) || (_oplType == OPL_dualopl2 && reg >= 0x101 && reg <= 0x103) ||
				forceWrite || _shadowRegisters[reg] != value) {
			_shadowRegisters[reg] = value;
			_opl->PortWrite(0x388, reg, 0);
			//_opl->PortWrite(0x389, value, 0);
			_opl->handler->WriteReg(reg, value);
		}
	}
};

// These are the melodic instrument definitions used by the Win95 SB16 driver.
MidiHandler_adlib::OplInstrumentDefinition MidiHandler_adlib::OPL_INSTRUMENT_BANK[128] = {
	// 0x00
	{ false, { 0x01, 0x8F, 0xF2, 0xF4, 0x00 }, { 0x01, 0x06, 0xF2, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x4B, 0xF2, 0xF4, 0x00 }, { 0x01, 0x00, 0xF2, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x49, 0xF2, 0xF4, 0x00 }, { 0x01, 0x00, 0xF2, 0xF6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x81, 0x12, 0xF2, 0xF7, 0x00 }, { 0x41, 0x00, 0xF2, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x57, 0xF1, 0xF7, 0x00 }, { 0x01, 0x00, 0xF2, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x93, 0xF1, 0xF7, 0x00 }, { 0x01, 0x00, 0xF2, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x80, 0xA1, 0xF2, 0x00 }, { 0x16, 0x0E, 0xF2, 0xF5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x92, 0xC2, 0xF8, 0x00 }, { 0x01, 0x00, 0xC2, 0xF8, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x08
	{ false, { 0x0C, 0x5C, 0xF6, 0xF4, 0x00 }, { 0x81, 0x00, 0xF3, 0xF5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x07, 0x97, 0xF3, 0xF2, 0x00 }, { 0x11, 0x80, 0xF2, 0xF1, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x17, 0x21, 0x54, 0xF4, 0x00 }, { 0x01, 0x00, 0xF4, 0xF4, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x98, 0x62, 0xF3, 0xF6, 0x00 }, { 0x81, 0x00, 0xF2, 0xF6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x18, 0x23, 0xF6, 0xF6, 0x00 }, { 0x01, 0x00, 0xE7, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x15, 0x91, 0xF6, 0xF6, 0x00 }, { 0x01, 0x00, 0xF6, 0xF6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x45, 0x59, 0xD3, 0xF3, 0x00 }, { 0x81, 0x80, 0xA3, 0xF3, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x03, 0x49, 0x75, 0xF5, 0x01 }, { 0x81, 0x80, 0xB5, 0xF5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x10
	{ false, { 0x71, 0x92, 0xF6, 0x14, 0x00 }, { 0x31, 0x00, 0xF1, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x72, 0x14, 0xC7, 0x58, 0x00 }, { 0x30, 0x00, 0xC7, 0x08, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x70, 0x44, 0xAA, 0x18, 0x00 }, { 0xB1, 0x00, 0x8A, 0x08, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x23, 0x93, 0x97, 0x23, 0x01 }, { 0xB1, 0x00, 0x55, 0x14, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x13, 0x97, 0x04, 0x01 }, { 0xB1, 0x80, 0x55, 0x04, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x24, 0x48, 0x98, 0x2A, 0x01 }, { 0xB1, 0x00, 0x46, 0x1A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x13, 0x91, 0x06, 0x01 }, { 0x21, 0x00, 0x61, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x13, 0x71, 0x06, 0x00 }, { 0xA1, 0x89, 0x61, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x18
	{ false, { 0x02, 0x9C, 0xF3, 0x94, 0x01 }, { 0x41, 0x80, 0xF3, 0xC8, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x03, 0x54, 0xF3, 0x9A, 0x01 }, { 0x11, 0x00, 0xF1, 0xE7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x23, 0x5F, 0xF1, 0x3A, 0x00 }, { 0x21, 0x00, 0xF2, 0xF8, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x03, 0x87, 0xF6, 0x22, 0x01 }, { 0x21, 0x80, 0xF3, 0xF8, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x03, 0x47, 0xF9, 0x54, 0x00 }, { 0x21, 0x00, 0xF6, 0x3A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x23, 0x4A, 0x91, 0x41, 0x01 }, { 0x21, 0x05, 0x84, 0x19, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x23, 0x4A, 0x95, 0x19, 0x01 }, { 0x21, 0x00, 0x94, 0x19, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x09, 0xA1, 0x20, 0x4F, 0x00 }, { 0x84, 0x80, 0xD1, 0xF8, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x20
	{ false, { 0x21, 0x1E, 0x94, 0x06, 0x00 }, { 0xA2, 0x00, 0xC3, 0xA6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x12, 0xF1, 0x28, 0x00 }, { 0x31, 0x00, 0xF1, 0x18, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x8D, 0xF1, 0xE8, 0x00 }, { 0x31, 0x00, 0xF1, 0x78, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x5B, 0x51, 0x28, 0x00 }, { 0x32, 0x00, 0x71, 0x48, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x8B, 0xA1, 0x9A, 0x00 }, { 0x21, 0x40, 0xF2, 0xDF, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x8B, 0xA2, 0x16, 0x00 }, { 0x21, 0x08, 0xA1, 0xDF, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x8B, 0xF4, 0xE8, 0x00 }, { 0x31, 0x00, 0xF1, 0x78, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x12, 0xF1, 0x28, 0x00 }, { 0x31, 0x00, 0xF1, 0x18, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x28
	{ false, { 0x31, 0x15, 0xDD, 0x13, 0x01 }, { 0x21, 0x00, 0x56, 0x26, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x16, 0xDD, 0x13, 0x01 }, { 0x21, 0x00, 0x66, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x71, 0x49, 0xD1, 0x1C, 0x01 }, { 0x31, 0x00, 0x61, 0x0C, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x4D, 0x71, 0x12, 0x01 }, { 0x23, 0x80, 0x72, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xF1, 0x40, 0xF1, 0x21, 0x01 }, { 0xE1, 0x00, 0x6F, 0x16, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x02, 0x1A, 0xF5, 0x75, 0x01 }, { 0x01, 0x80, 0x85, 0x35, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x02, 0x1D, 0xF5, 0x75, 0x01 }, { 0x01, 0x80, 0xF3, 0xF4, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x10, 0x41, 0xF5, 0x05, 0x01 }, { 0x11, 0x00, 0xF2, 0xC3, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x30
	{ false, { 0x21, 0x9B, 0xB1, 0x25, 0x01 }, { 0xA2, 0x01, 0x72, 0x08, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x98, 0x7F, 0x03, 0x01 }, { 0x21, 0x00, 0x3F, 0x07, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x93, 0xC1, 0x12, 0x00 }, { 0x61, 0x00, 0x4F, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x18, 0xC1, 0x22, 0x00 }, { 0x61, 0x00, 0x4F, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x5B, 0xF4, 0x15, 0x00 }, { 0x72, 0x83, 0x8A, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x90, 0x74, 0x39, 0x00 }, { 0x61, 0x00, 0x71, 0x67, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x71, 0x57, 0x54, 0x05, 0x00 }, { 0x72, 0x00, 0x7A, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x90, 0x00, 0x54, 0x63, 0x00 }, { 0x41, 0x00, 0xA5, 0x45, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x38
	{ false, { 0x21, 0x92, 0x85, 0x17, 0x00 }, { 0x21, 0x01, 0x8F, 0x09, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x94, 0x75, 0x17, 0x00 }, { 0x21, 0x05, 0x8F, 0x09, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x94, 0x76, 0x15, 0x00 }, { 0x61, 0x00, 0x82, 0x37, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x43, 0x9E, 0x17, 0x01 }, { 0x21, 0x00, 0x62, 0x2C, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x9B, 0x61, 0x6A, 0x00 }, { 0x21, 0x00, 0x7F, 0x0A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x8A, 0x75, 0x1F, 0x00 }, { 0x22, 0x06, 0x74, 0x0F, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x86, 0x72, 0x55, 0x01 }, { 0x21, 0x83, 0x71, 0x18, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x4D, 0x54, 0x3C, 0x00 }, { 0x21, 0x00, 0xA6, 0x1C, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x40
	{ false, { 0x31, 0x8F, 0x93, 0x02, 0x01 }, { 0x61, 0x00, 0x72, 0x0B, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x8E, 0x93, 0x03, 0x01 }, { 0x61, 0x00, 0x72, 0x09, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x91, 0x93, 0x03, 0x01 }, { 0x61, 0x00, 0x82, 0x09, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x8E, 0x93, 0x0F, 0x01 }, { 0x61, 0x00, 0x72, 0x0F, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x4B, 0xAA, 0x16, 0x01 }, { 0x21, 0x00, 0x8F, 0x0A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x90, 0x7E, 0x17, 0x01 }, { 0x21, 0x00, 0x8B, 0x0C, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x81, 0x75, 0x19, 0x01 }, { 0x32, 0x00, 0x61, 0x19, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x32, 0x90, 0x9B, 0x21, 0x00 }, { 0x21, 0x00, 0x72, 0x17, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x48
	{ false, { 0xE1, 0x1F, 0x85, 0x5F, 0x00 }, { 0xE1, 0x00, 0x65, 0x1A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xE1, 0x46, 0x88, 0x5F, 0x00 }, { 0xE1, 0x00, 0x65, 0x1A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x9C, 0x75, 0x1F, 0x00 }, { 0x21, 0x00, 0x75, 0x0A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x8B, 0x84, 0x58, 0x00 }, { 0x21, 0x00, 0x65, 0x1A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xE1, 0x4C, 0x66, 0x56, 0x00 }, { 0xA1, 0x00, 0x65, 0x26, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x62, 0xCB, 0x76, 0x46, 0x00 }, { 0xA1, 0x00, 0x55, 0x36, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x62, 0x99, 0x57, 0x07, 0x00 }, { 0xA1, 0x00, 0x56, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3B, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x62, 0x93, 0x77, 0x07, 0x00 }, { 0xA1, 0x00, 0x76, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3B, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x50
	{ false, { 0x22, 0x59, 0xFF, 0x03, 0x02 }, { 0x21, 0x00, 0xFF, 0x0F, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x0E, 0xFF, 0x0F, 0x01 }, { 0x21, 0x00, 0xFF, 0x0F, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x22, 0x46, 0x86, 0x55, 0x00 }, { 0x21, 0x80, 0x64, 0x18, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x45, 0x66, 0x12, 0x00 }, { 0xA1, 0x00, 0x96, 0x0A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x8B, 0x92, 0x2A, 0x01 }, { 0x22, 0x00, 0x91, 0x2A, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA2, 0x9E, 0xDF, 0x05, 0x00 }, { 0x61, 0x40, 0x6F, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x20, 0x1A, 0xEF, 0x01, 0x00 }, { 0x60, 0x00, 0x8F, 0x06, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x8F, 0xF1, 0x29, 0x00 }, { 0x21, 0x80, 0xF4, 0x09, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x58
	{ false, { 0x77, 0xA5, 0x53, 0x94, 0x00 }, { 0xA1, 0x00, 0xA0, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x1F, 0xA8, 0x11, 0x00 }, { 0xB1, 0x80, 0x25, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x17, 0x91, 0x34, 0x00 }, { 0x61, 0x00, 0x55, 0x16, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x71, 0x5D, 0x54, 0x01, 0x00 }, { 0x72, 0x00, 0x6A, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x97, 0x21, 0x43, 0x00 }, { 0xA2, 0x00, 0x42, 0x35, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x1C, 0xA1, 0x77, 0x01 }, { 0x21, 0x00, 0x31, 0x47, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x89, 0x11, 0x33, 0x00 }, { 0x61, 0x03, 0x42, 0x25, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA1, 0x15, 0x11, 0x47, 0x01 }, { 0x21, 0x00, 0xCF, 0x07, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x60
	{ false, { 0x3A, 0xCE, 0xF8, 0xF6, 0x00 }, { 0x51, 0x00, 0x86, 0x02, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x32, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x15, 0x21, 0x23, 0x01 }, { 0x21, 0x00, 0x41, 0x13, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x06, 0x5B, 0x74, 0x95, 0x00 }, { 0x01, 0x00, 0xA5, 0x72, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x22, 0x92, 0xB1, 0x81, 0x00 }, { 0x61, 0x83, 0xF2, 0x26, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x41, 0x4D, 0xF1, 0x51, 0x01 }, { 0x42, 0x00, 0xF2, 0xF5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x94, 0x11, 0x51, 0x01 }, { 0xA3, 0x80, 0x11, 0x13, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x61, 0x8C, 0x11, 0x31, 0x00 }, { 0xA1, 0x80, 0x1D, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xA4, 0x4C, 0xF3, 0x73, 0x01 }, { 0x61, 0x00, 0x81, 0x23, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x68
	{ false, { 0x02, 0x85, 0xD2, 0x53, 0x00 }, { 0x07, 0x03, 0xF2, 0xF6, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x11, 0x0C, 0xA3, 0x11, 0x01 }, { 0x13, 0x80, 0xA2, 0xE5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x11, 0x06, 0xF6, 0x41, 0x01 }, { 0x11, 0x00, 0xF2, 0xE6, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x93, 0x91, 0xD4, 0x32, 0x00 }, { 0x91, 0x00, 0xEB, 0x11, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x04, 0x4F, 0xFA, 0x56, 0x00 }, { 0x01, 0x00, 0xC2, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x21, 0x49, 0x7C, 0x20, 0x00 }, { 0x22, 0x00, 0x6F, 0x0C, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x31, 0x85, 0xDD, 0x33, 0x01 }, { 0x21, 0x00, 0x56, 0x16, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x20, 0x04, 0xDA, 0x05, 0x02 }, { 0x21, 0x81, 0x8F, 0x0B, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x70
	{ false, { 0x05, 0x6A, 0xF1, 0xE5, 0x00 }, { 0x03, 0x80, 0xC3, 0xE5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x07, 0x15, 0xEC, 0x26, 0x00 }, { 0x02, 0x00, 0xF8, 0x16, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x05, 0x9D, 0x67, 0x35, 0x00 }, { 0x01, 0x00, 0xDF, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x18, 0x96, 0xFA, 0x28, 0x00 }, { 0x12, 0x00, 0xF8, 0xE5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x10, 0x86, 0xA8, 0x07, 0x00 }, { 0x00, 0x03, 0xFA, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x11, 0x41, 0xF8, 0x47, 0x02 }, { 0x10, 0x03, 0xF3, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x8E, 0xF1, 0x06, 0x02 }, { 0x10, 0x00, 0xF3, 0x02, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0x1F, 0x00, 0x00 }, { 0xC0, 0x00, 0x1F, 0xFF, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x78
	{ false, { 0x06, 0x80, 0xF8, 0x24, 0x00 }, { 0x03, 0x88, 0x56, 0x84, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0xF8, 0x00, 0x00 }, { 0xD0, 0x05, 0x34, 0x04, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0xF6, 0x00, 0x00 }, { 0xC0, 0x00, 0x1F, 0x02, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xD5, 0x95, 0x37, 0xA3, 0x00 }, { 0xDA, 0x40, 0x56, 0x37, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x35, 0x5C, 0xB2, 0x61, 0x02 }, { 0x14, 0x08, 0xF4, 0x15, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0xF6, 0x00, 0x00 }, { 0xD0, 0x00, 0x4F, 0xF5, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x26, 0x00, 0xFF, 0x01, 0x00 }, { 0xE4, 0x00, 0x12, 0x16, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xF3, 0xF0, 0x00 }, { 0x00, 0x00, 0xF6, 0xC9, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED }
};

// These are the rhythm instrument definitions used by the Win95 SB16 driver.
MidiHandler_adlib::OplInstrumentDefinition MidiHandler_adlib::OPL_RHYTHM_BANK[62] = {
	// GS percussion start
	// 0x1B
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	// 0x20
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED },

	// GM percussion start
	// 0x23
	{ false, { 0x10, 0x44, 0xF8, 0x77, 0x02 }, { 0x11, 0x00, 0xF3, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x23, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x10, 0x44, 0xF8, 0x77, 0x02 }, { 0x11, 0x00, 0xF3, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x23, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x02, 0x07, 0xF9, 0xFF, 0x00 }, { 0x11, 0x00, 0xF8, 0xFF, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x34, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xFC, 0x05, 0x02 }, { 0x00, 0x00, 0xFA, 0x17, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x30, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x02, 0xFF, 0x07, 0x00 }, { 0x01, 0x00, 0xFF, 0x08, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x3A, RHYTHM_TYPE_UNDEFINED },
	// 0x28
	{ false, { 0x00, 0x00, 0xFC, 0x05, 0x02 }, { 0x00, 0x00, 0xFA, 0x17, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x3C, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xF6, 0x0C, 0x00 }, { 0x00, 0x00, 0xF6, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x2F, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0C, 0x00, 0xF6, 0x08, 0x00 }, { 0x12, 0x00, 0xFB, 0x47, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x2B, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xF6, 0x0C, 0x00 }, { 0x00, 0x00, 0xF6, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x31, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0C, 0x00, 0xF6, 0x08, 0x00 }, { 0x12, 0x05, 0x7B, 0x47, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x2B, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xF6, 0x0C, 0x00 }, { 0x00, 0x00, 0xF6, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x33, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0C, 0x00, 0xF6, 0x02, 0x00 }, { 0x12, 0x00, 0xCB, 0x43, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3A, 0x00, 0x2B, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xF6, 0x0C, 0x00 }, { 0x00, 0x00, 0xF6, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x36, RHYTHM_TYPE_UNDEFINED },
	// 0x30
	{ false, { 0x00, 0x00, 0xF6, 0x0C, 0x00 }, { 0x00, 0x00, 0xF6, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x39, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0xF6, 0x00, 0x00 }, { 0xD0, 0x00, 0x9F, 0x02, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x48, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0xF6, 0x0C, 0x00 }, { 0x00, 0x00, 0xF6, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x34, 0x00, 0x3C, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x08, 0xF8, 0x42, 0x00 }, { 0x07, 0x4A, 0xF4, 0xE4, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x4C, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0xF5, 0x30, 0x00 }, { 0xD0, 0x0A, 0x9F, 0x02, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x54, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x0A, 0xE4, 0xE4, 0x03 }, { 0x07, 0x5D, 0xF5, 0xE5, 0x01 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x24, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x02, 0x03, 0xB4, 0x04, 0x00 }, { 0x05, 0x0A, 0x97, 0xF7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x4C, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x4E, 0x00, 0xF6, 0x00, 0x00 }, { 0x9E, 0x00, 0x9F, 0x02, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x54, RHYTHM_TYPE_UNDEFINED },
	// 0x38
	{ false, { 0x11, 0x45, 0xF8, 0x37, 0x02 }, { 0x10, 0x08, 0xF3, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x38, 0x00, 0x53, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x00, 0xF6, 0x00, 0x00 }, { 0xD0, 0x00, 0x9F, 0x02, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x54, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x80, 0x00, 0xFF, 0x03, 0x03 }, { 0x10, 0x0D, 0xFF, 0x14, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3C, 0x00, 0x18, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x08, 0xF8, 0x42, 0x00 }, { 0x07, 0x4A, 0xF4, 0xE4, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x4D, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x06, 0x0B, 0xF5, 0x0C, 0x00 }, { 0x02, 0x00, 0xF5, 0x08, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x3C, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x00, 0xFA, 0xBF, 0x00 }, { 0x02, 0x00, 0xC8, 0x97, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x37, 0x00, 0x41, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x51, 0xFA, 0x87, 0x00 }, { 0x01, 0x00, 0xFA, 0xB7, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x3B, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x54, 0xFA, 0x8D, 0x00 }, { 0x02, 0x00, 0xF8, 0xB8, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x33, RHYTHM_TYPE_UNDEFINED },
	// 0x40
	{ false, { 0x01, 0x59, 0xFA, 0x88, 0x00 }, { 0x02, 0x00, 0xF8, 0xB6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x36, 0x00, 0x2D, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x01, 0x00, 0xF9, 0x0A, 0x03 }, { 0x00, 0x00, 0xFA, 0x06, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x47, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x80, 0xF9, 0x89, 0x03 }, { 0x00, 0x00, 0xF6, 0x6C, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x3C, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x03, 0x80, 0xF8, 0x88, 0x03 }, { 0x0C, 0x08, 0xF6, 0xB6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3F, 0x00, 0x3A, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x03, 0x85, 0xF8, 0x88, 0x03 }, { 0x0C, 0x00, 0xF6, 0xB6, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3F, 0x00, 0x35, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x40, 0x76, 0x4F, 0x00 }, { 0x00, 0x08, 0x77, 0x18, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x40, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x0E, 0x40, 0xC8, 0x49, 0x00 }, { 0x03, 0x00, 0x9B, 0x69, 0x02 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x47, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0xD7, 0xDC, 0xAD, 0x05, 0x03 }, { 0xC7, 0x00, 0x8D, 0x05, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x3D, RHYTHM_TYPE_UNDEFINED },
	// 0x48
	{ false, { 0xD7, 0xDC, 0xA8, 0x04, 0x03 }, { 0xC7, 0x00, 0x88, 0x04, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x3D, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x80, 0x00, 0xF6, 0x06, 0x03 }, { 0x11, 0x00, 0x67, 0x17, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x30, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x80, 0x00, 0xF5, 0x05, 0x02 }, { 0x11, 0x09, 0x46, 0x16, 0x03 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3E, 0x00, 0x30, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x06, 0x3F, 0x00, 0xF4, 0x00 }, { 0x15, 0x00, 0xF7, 0xF5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x31, 0x00, 0x45, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x06, 0x3F, 0x00, 0xF4, 0x03 }, { 0x12, 0x00, 0xF7, 0xF5, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x30, 0x00, 0x44, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x3F, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x4A, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x3C, RHYTHM_TYPE_UNDEFINED },
	// 0x50
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x50, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x40, RHYTHM_TYPE_UNDEFINED },
	// GM percussion end

	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x45, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x49, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x4B, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x44, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x30, RHYTHM_TYPE_UNDEFINED },
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x35, RHYTHM_TYPE_UNDEFINED },
	// 0x58
	{ false, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x00, 0x00, 0x00, RHYTHM_TYPE_UNDEFINED }
	// GS percussion end
};

// Rhythm mode uses OPL channels 6, 7 and 8. The remaining channels are available for melodic instruments.
Bit8u MidiHandler_adlib::MELODIC_CHANNELS_OPL2[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
Bit8u MidiHandler_adlib::MELODIC_CHANNELS_OPL2_RHYTHM[] = { 0, 1, 2, 3, 4, 5 };
Bit8u MidiHandler_adlib::MELODIC_CHANNELS_OPL3[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
Bit8u MidiHandler_adlib::MELODIC_CHANNELS_OPL3_RHYTHM[] = { 0, 1, 2, 3, 4, 5, 9, 10, 11, 12, 13, 14, 15, 16, 17 };

const Bit8u MidiHandler_adlib::OPL_REGISTER_RHYTHM_OFFSETS[OPL_NUM_RHYTHM_INSTRUMENTS] = { 0x11, 0x15, 0x12, 0x14, 0x10 };

const Bit8u MidiHandler_adlib::OPL_RHYTHM_INSTRUMENT_CHANNELS[OPL_NUM_RHYTHM_INSTRUMENTS] = { 7, 8, 8, 7, 6 };

static MidiHandler_adlib Midi_adlib;
