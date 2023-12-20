/*
 *  IDE ATA/ATAPI and controller emulation for DOSBox-X
 *  Copyright (C) 2012-2022 Jonathan Campbell
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

 /*
   The code has been refactored for use in this variant of DOSBox.
   For the original see https://github.com/joncampbell123/dosbox-x/blob/4490c68/src/hardware/ide.cpp
 */

#include "dosbox.h"
#ifdef C_DBP_ENABLE_IDE
#include "inout.h"
#include "pic.h"
#include "../dos/drives.h"
#include "../dos/cdrom.h"

static const unsigned char IDE_default_IRQs[4] = {
	14, /* primary */
	15, /* secondary */
	11, /* tertiary */
	10, /* quaternary */
};

static const unsigned short IDE_default_bases[4] = {
	0x1F0, /* primary */
	0x170, /* secondary */
	0x1E8, /* tertiary */
	0x168, /* quaternary */
};

static const unsigned short IDE_default_alts[4] = {
	0x3F6, /* primary */
	0x376, /* secondary */
	0x3EE, /* tertiary */
	0x36E, /* quaternary */
};

enum IDEDeviceType : uint8_t {
	IDE_TYPE_NONE,
#ifdef C_DBP_ENABLE_IDE_ATA
	IDE_TYPE_HDD,
#endif
	IDE_TYPE_CDROM,
};

enum IDEDeviceState : uint8_t {
	IDE_DEV_READY,
	IDE_DEV_SELECT_WAIT,
	IDE_DEV_CONFUSED,
	IDE_DEV_BUSY,
	IDE_DEV_DATA_READ,
	IDE_DEV_DATA_WRITE,
	IDE_DEV_ATAPI_PACKET_COMMAND,
	IDE_DEV_ATAPI_BUSY,
};

enum IDEStatus : uint8_t {
	IDE_STATUS_BUSY                = 0x80,
	IDE_STATUS_DRIVE_READY         = 0x40,
	IDE_STATUS_DRIVE_SEEK_COMPLETE = 0x10,
	IDE_STATUS_DRQ                 = 0x08,
	IDE_STATUS_ERROR               = 0x01,
};

static inline bool drivehead_is_lba(uint8_t val) {
	return (val&0xE0) == 0xE0;
}

static const float ide_identify_command_delay =    0.01f; // 10us
static const float atapi_spinup_time          =   100.0f; // 0.1s (time period appropriate would be 1s)
static const float atapi_spindown_timeout     = 10000.0f; // 10s (drive spins down automatically after this)

enum { MAX_IDE_CONTROLLERS = 2 };

static struct IDEController* idecontroller[MAX_IDE_CONTROLLERS];

#define IDEMIN(a,b) ((a) < (b) ? (a) : (b))

static void ide_altio_w(Bitu port, Bitu val, Bitu iolen);
static Bitu ide_altio_r(Bitu port, Bitu iolen);
static void ide_baseio_w(Bitu port, Bitu val, Bitu iolen);
static Bitu ide_baseio_r(Bitu port, Bitu iolen);
static void IDE_DelayedCommand(Bitu idx);

struct IDEController {
	IO_ReadHandleObject ReadHandler[8], ReadHandlerAlt[2];
	IO_WriteHandleObject WriteHandler[8], WriteHandlerAlt[2];
	struct IDEDevice* device[2];  /* IDE devices (master, slave) */
	Bitu select;                  /* which is selected */
	bool interrupt_enable;        /* bit 1 of alt (0x3F6) */
	bool host_reset;              /* bit 2 of alt */
	bool irq_pending;
	unsigned char interface_index;
	unsigned short alt_io;
	unsigned short base_io;
	int IRQ;

	IDEController(unsigned char index) {
		host_reset = false;
		irq_pending = false;
		interrupt_enable = true;
		interface_index = index;
		device[0] = NULL;
		device[1] = NULL;
		select = 0;
		IRQ = IDE_default_IRQs[index];
		alt_io = IDE_default_alts[index];
		base_io = IDE_default_bases[index];
		install_io_port();
		PIC_SetIRQMask((unsigned int)IRQ,false);
	}

	void check_device_irq();
	~IDEController();

	void install_io_port() {
		if (base_io != 0) {
			for (unsigned int i=0;i < 8;i++) {
				WriteHandler[i].Install(base_io+i,ide_baseio_w,IO_MA);
				ReadHandler[i].Install(base_io+i,ide_baseio_r,IO_MA);
			}
		}

		if (alt_io != 0) {
			WriteHandlerAlt[0].Install(alt_io,ide_altio_w,IO_MA);
			ReadHandlerAlt[0].Install(alt_io,ide_altio_r,IO_MA);

			WriteHandlerAlt[1].Install(alt_io+1u,ide_altio_w,IO_MA);
			ReadHandlerAlt[1].Install(alt_io+1u,ide_altio_r,IO_MA);
		}
	}
};

struct IDEDevice {
	IDEController *controller;
	uint16_t feature, count, lba[3]; /* feature = BASE+1  count = BASE+2   lba[3] = BASE+3,+4,+5 */
	uint8_t command, drivehead, status; /* command/status = BASE+7  drivehead = BASE+6 */
	uint8_t device_index;
	enum IDEDeviceType type;
	bool allow_writing;
	bool irq_signal;
	bool asleep;
	IDEDeviceState state;

	/* feature: 0x1F1 (Word 00h in ATA specs)
		count: 0x1F2 (Word 01h in ATA specs)
		lba[3]: 0x1F3 (Word 02h) 0x1F4 (Word 03h) and 0x1F5 (Word 04h)
		drivehead: 0x1F6 (copy of last value written)
		command: 0x1F7 (Word 05h)
		status: 0x1F7 (value read back to IDE controller, including busy and drive ready bits as well as error status)

	In C/H/S modes lba[3] becomes lba[0]=sector lba[1]=cylinder-low lba[2]=cylinder-high and
	the code must read the 4-bit head number from drivehead[bits 3:0].

	"drivehead" in this struct is always maintained as a device copy of the controller's
	drivehead value. it is only updated on write, and not returned on read.

	"allow_writing" if set allows the DOS program/OS to write the registers. It is
	clear during command execution, obviously, so the state of the device is not confused
	while executing the command.

	Registers are 16-bit where applicable so future revisions of this code
	can support LBA48 commands
	*/

	IDEDevice(IDEController *c, uint8_t dev_idx, IDEDeviceType typ) {
		device_index = dev_idx;
		type = typ;
		controller = c;
		asleep = false;
		irq_signal = false;
		allow_writing = true;
		state = IDE_DEV_READY;
		feature = count = lba[0] = lba[1] = lba[2] = command = drivehead = 0;
		status = IDE_STATUS_DRIVE_READY | IDE_STATUS_DRIVE_SEEK_COMPLETE;
	}

	virtual ~IDEDevice() {}

	virtual void writecommand(uint8_t cmd) = 0;
	virtual Bitu data_read(Bitu iolen) = 0;
	virtual void data_write(Bitu v,Bitu iolen) = 0;

	static inline IDEDevice* GetByIndex(Bitu dev_idx/*which IDE device*/) {
		return (dev_idx < MAX_IDE_CONTROLLERS*2 ? idecontroller[dev_idx>>1]->device[dev_idx&1] : NULL);
	}

	void host_reset_begin() {    /* IDE controller -> upon setting bit 2 of alt (0x3F6) */
		status = 0xFF;
		asleep = false;
		allow_writing = true;
		state = IDE_DEV_BUSY;
	}

	void host_reset_complete() { /* IDE controller -> upon setting bit 2 of alt (0x3F6) */
		asleep = false;
		allow_writing = true;
		state = IDE_DEV_READY;
		status = IDE_STATUS_DRIVE_READY | IDE_STATUS_DRIVE_SEEK_COMPLETE;
	}

	/* the hard disk or CD-ROM class override of this member is responsible for checking
	   the head value and clamping within range if C/H/S mode is selected */
	inline void select(uint8_t ndh,bool switched_to) {
		(void)switched_to;//UNUSED
		(void)ndh;//UNUSED
		/* NTS: I thought there was some delay between selecting a drive and sending a command.
			Apparently I was wrong. */
		if (allow_writing) drivehead = ndh;
		//status = (!asleep)?(IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE):0;
		//allow_writing = !asleep;
		//state = IDE_DEV_READY;
	}

	inline void deselect() {}

	inline void interface_wakeup() {
		if (asleep) {
			asleep = false;
		}
	}

	void raise_irq() {
		if (!irq_signal) {
			irq_signal = true;
			controller->check_device_irq();
		}
	}

	void lower_irq() {
		if (irq_signal) {
			irq_signal = false;
			controller->check_device_irq();
		}
	}

	bool command_interruption_ok(uint8_t cmd) {
		/* apparently this is OK, if the Linux kernel is doing it:
		 * writing the same command byte as the one in progress, OR, issuing
		 * Device Reset while another command is waiting for data read/write */
		if (cmd == command) return true;
		if (state != IDE_DEV_READY && state != IDE_DEV_BUSY && cmd == 0x08) {
			LOG_MSG("Device reset while another (%02x) is in progress (state=%u). Aborting current command to begin another",command,state);
			abort_silent();
			return true;
		}

		if (state != IDE_DEV_READY) {
			LOG_MSG("Command %02x written while another (%02x) is in progress (state=%u). Aborting current command",cmd,command,state);
			abort_error();
			return false;
		}

		return true;
	}

	void abort_error() {
		DBP_ASSERT(controller != NULL);
		LOG_MSG("IDE abort dh=0x%02x with error on 0x%03x",drivehead,controller->base_io);

		/* a command was written while another is in progress */
		state = IDE_DEV_READY;
		allow_writing = true;
		command = 0x00;
		status = IDE_STATUS_ERROR | IDE_STATUS_DRIVE_READY | IDE_STATUS_DRIVE_SEEK_COMPLETE;
	}

	void abort_normal() {
		/* a command was written while another is in progress */
		state = IDE_DEV_READY;
		allow_writing = true;
		command = 0x00;
		status = IDE_STATUS_DRIVE_READY | IDE_STATUS_DRIVE_SEEK_COMPLETE;
	}

	void abort_silent() {
		DBP_ASSERT(controller != NULL);

		/* a command was written while another is in progress */
		state = IDE_DEV_READY;
		allow_writing = true;
		command = 0x00;
		status = IDE_STATUS_ERROR | IDE_STATUS_DRIVE_READY | IDE_STATUS_DRIVE_SEEK_COMPLETE;
	}
};

void IDEController::check_device_irq() {
	IDEDevice* dev = device[select];
	bool sig = false;

	if (dev) sig = dev->irq_signal && interrupt_enable;

	if (irq_pending != sig) {
		if (sig) {
			irq_pending = true;
			if (IRQ >= 0) PIC_ActivateIRQ((unsigned int)IRQ);
		}
		else {
			irq_pending = false;
			if (IRQ >= 0) PIC_DeActivateIRQ((unsigned int)IRQ);
		}
	}
}

IDEController::~IDEController()
{
	if (device[0]) delete device[0];
	if (device[1]) delete device[1];
}

struct IDEATAPICDROMDevice : public IDEDevice {
	enum LoadingMode : uint8_t {
		LOAD_NO_DISC,
#ifdef C_DBP_ENABLE_IDE_CDINSERTION_DELAY
		LOAD_INSERT_CD,    /* user is "inserting" the CD */
#endif
		LOAD_IDLE,         /* disc is stationary, not spinning */
		LOAD_DISC_LOADING, /* disc is "spinning up" */
		LOAD_DISC_READIED, /* disc just "became ready" */
		LOAD_READY
	};
	CDROM_Interface* my_cdrom;
	Bitu TransferLengthRemaining;
	Bitu LBA, LBAnext, TransferLength;
	Bitu TransferSectorSize;
	Bitu host_maximum_byte_count; /* host maximum byte count during PACKET transfer */
	Bitu sense_length;
	Bitu sector_i, sector_total;
	LoadingMode loading_mode;
	uint8_t TransferSectorType;
	uint8_t TransferReadCD9;
	bool atapi_to_host; /* if set, PACKET data transfer is to be read by host */
	bool has_changed;
	unsigned char sense[256];
	unsigned char atapi_cmd[12];
	unsigned char atapi_cmd_i, atapi_cmd_total;
	unsigned char sector[512*128];

	IDEATAPICDROMDevice(IDEController *c, uint8_t device_index) : IDEDevice(c, device_index, IDE_TYPE_CDROM) {
		my_cdrom = NULL;
		sector_i = sector_total = 0;
		atapi_to_host = false;
		host_maximum_byte_count = 0;
		LBA = 0;
		LBAnext = 0;
		TransferLength = 0;
		TransferLengthRemaining = 0;
		memset(atapi_cmd, 0, sizeof(atapi_cmd));
		atapi_cmd_i = 0;
		atapi_cmd_total = 0;
		memset(sector, 0, sizeof(sector));

		memset(sense,0,sizeof(sense));
		set_sense(/*SK=*/0);

		loading_mode = LOAD_NO_DISC;
		has_changed = false;
	}

	inline CDROM_Interface *getMSCDEXDrive() {
		return my_cdrom;
	}

	#if 0
	static const char* getIDECommandName(uint8_t cmd)
	{
		switch (cmd)
		{
			case 0x08: return "DEVICE RESET";
			case 0x20: return "READ SECTOR";
			case 0xA0: return "ATAPI PACKET";
			case 0xA1: return "IDENTIFY PACKET DEVICE";
			case 0xEC: return "IDENTIFY DEVICE";
			case 0xEF: return "SET FEATURES";
			default: return "?????? UNKNOWN ??????";
		}
	}
	static const char* getATAPICommandName(uint8_t cmd)
	{
		switch (cmd)
		{
			case 0x00: return "TEST_UNIT_READY";
			case 0x03: return "REQUEST_SENSE";
			case 0x04: return "FORMAT_UNIT";
			case 0x12: return "INQUIRY";
			case 0x15: return "MODE_SELECT_6";
			case 0x1A: return "MODE_SENSE_6";
			case 0x1B: return "START_STOP_UNIT";
			case 0x1E: return "PREVENT_ALLOW_MEDIUM_REMOVAL";
			case 0x23: return "READ_FORMAT_CAPACITIES";
			case 0x25: return "READ_CAPACITY";
			case 0x28: return "READ_10";
			case 0x2A: return "WRITE_10";
			case 0x2B: return "SEEK_10";
			case 0x2C: return "ERASE_10";
			case 0x2E: return "WRITE_AND_VERIFY_10";
			case 0x2F: return "VERIFY_10";
			case 0x35: return "SYNCHRONIZE_CACHE";
			case 0x3B: return "WRITE_BUFFER";
			case 0x3C: return "READ_BUFFER";
			case 0x42: return "READ_SUBCHANNEL";
			case 0x43: return "READ_TOC_PMA_ATIP";
			case 0x45: return "PLAY_AUDIO_10";
			case 0x46: return "GET_CONFIGURATION";
			case 0x47: return "PLAY_AUDIO_MSF";
			case 0x4A: return "GET_EVENT_STATUS_NOTIFICATION";
			case 0x4B: return "PAUSE_RESUME";
			case 0x4E: return "STOP_PLAY_SCAN";
			case 0x51: return "READ_DISC_INFORMATION";
			case 0x52: return "READ_TRACK_INFORMATION";
			case 0x53: return "RESERVE_TRACK";
			case 0x54: return "SEND_OPC_INFORMATION";
			case 0x55: return "MODE_SELECT_10";
			case 0x58: return "REPAIR_TRACK";
			case 0x5A: return "MODE_SENSE_10";
			case 0x5B: return "CLOSE_TRACK_SESSION";
			case 0x5C: return "READ_BUFFER_CAPACITY";
			case 0x5D: return "SEND_CUE_SHEET";
			case 0xA1: return "BLANK";
			case 0xA2: return "SEND_EVENT";
			case 0xA3: return "SEND_KEY";
			case 0xA4: return "REPORT_KEY";
			case 0xA5: return "PLAY_AUDIO_12";
			case 0xA6: return "LOAD_UNLOAD_MEDUIM";
			case 0xA7: return "SET_READ_AHEAD";
			case 0xA8: return "READ_12";
			case 0xAA: return "WRITE_12";
			case 0xAC: return "GET_PERFORMANCE";
			case 0xAD: return "READ_DISC_STRUCTURE";
			case 0xB6: return "SET_STREAMING";
			case 0xB9: return "READ_CD_MSF";
			case 0xBA: return "SCAN";
			case 0xBB: return "SET_CD_SPEED";
			case 0xBD: return "MECHANISM_STATUS";
			case 0xBE: return "READ_CD";
			case 0xBF: return "SEND_DVD_STRUCTURE";
			default: return "?????? UNKNOWN ??????";
		}
	}
	#endif

	virtual void writecommand(uint8_t cmd) {
		#if 0
		LOG_MSG("[ATAPI] [IDE] WRITE COMMAND - CMD: %s (0x%02x) - INTERRUPT_OK: %d - FEATURE: %x - LBA: %x %x %x (%u)",
			getIDECommandName(cmd), (int)cmd, command_interruption_ok(cmd), feature, lba[0], lba[1], lba[2], (unsigned)((lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8)));
		#endif

		if (!command_interruption_ok(cmd))
			return;

		/* if the drive is asleep, then writing a command wakes it up */
		interface_wakeup();

		/* drive is ready to accept command */
		allow_writing = false;
		command = cmd;
		switch (cmd) {
			case 0x08: /* DEVICE RESET */
				status = 0x00;
				drivehead &= 0x10;
				count = 0x01;
				lba[0] = 0x01;
				feature = 0x01;
				lba[1] = 0x14;  /* <- magic ATAPI identification */
				lba[2] = 0xEB;
				/* NTS: Testing suggests that ATAPI devices do NOT trigger an IRQ on receipt of this command */
				allow_writing = true;
				break;
			case 0x20: /* READ SECTOR */
				abort_normal();
				status = IDE_STATUS_ERROR|IDE_STATUS_DRIVE_READY;
				drivehead &= 0x30;
				count = 0x01;
				lba[0] = 0x01;
				feature = 0x04; /* abort */
				lba[1] = 0x14;  /* <- magic ATAPI identification */
				lba[2] = 0xEB;
				raise_irq();
				allow_writing = true;
				break;
			case 0xA0: /* ATAPI PACKET */
				if (feature & 1) {
					/* this code does not support DMA packet commands */
					LOG_MSG("Attempted DMA transfer");
					abort_error();
					count = 0x03; /* no more data (command/data=1, input/output=1) */
					feature = 0xF4;
					raise_irq();
				}
				else {
					state = IDE_DEV_BUSY;
					status = IDE_STATUS_BUSY;
					atapi_to_host = (feature >> 2) & 1; /* 0=to device 1=to host */
					host_maximum_byte_count = ((unsigned int)lba[2] << 8) + (unsigned int)lba[1]; /* LBA field bits 23:8 are byte count */
					if (host_maximum_byte_count == 0) host_maximum_byte_count = 0x10000UL;
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(0.25)/*ms*/,device_index);
				}
				break;
			case 0xA1: /* IDENTIFY PACKET DEVICE */
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(ide_identify_command_delay),device_index);
				break;
			case 0xEC: /* IDENTIFY DEVICE */
				/* "devices that implement the PACKET command set shall post command aborted and place PACKET command feature
				   set in the appropriate fields". We have to do this. Unlike OAKCDROM.SYS Windows 95 appears to autodetect
				   IDE devices by what they do when they're sent command 0xEC out of the blue---Microsoft didn't write their
				   IDE drivers to use command 0x08 DEVICE RESET. */
				abort_normal();
				status = IDE_STATUS_ERROR|IDE_STATUS_DRIVE_READY;
				drivehead &= 0x30;
				count = 0x01;
				lba[0] = 0x01;
				feature = 0x04; /* abort */
				lba[1] = 0x14;  /* <- magic ATAPI identification */
				lba[2] = 0xEB;
				raise_irq();
				allow_writing = true;
				break;
			case 0xEF: /* SET FEATURES */
				if (feature == 0x66/*Disable reverting to power on defaults*/ ||
					feature == 0xCC/*Enable reverting to power on defaults*/) {
					/* ignore */
					status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					state = IDE_DEV_READY;
				}
				else {
					LOG_MSG("SET FEATURES %02xh SC=%02x SN=%02x CL=%02x CH=%02x",feature,count,lba[0],lba[1],lba[2]);
					abort_error();
				}
				allow_writing = true;
				raise_irq();
				break;
			default:
				LOG_MSG("Unknown IDE/ATAPI command %02X",cmd);
				abort_error();
				allow_writing = true;
				count = 0x03; /* no more data (command/data=1, input/output=1) */
				feature = 0xF4;
				raise_irq();
				break;
		}
	}

	virtual Bitu data_read(Bitu iolen) { /* read from 1F0h data port from IDE device */
		Bitu w = ~0u;

		if (state != IDE_DEV_DATA_READ)
			return 0xFFFFUL;

		if (!(status & IDE_STATUS_DRQ)) {
			LOG_MSG("IDE: Data read when DRQ=0");
			return 0xFFFFUL;
		}

		if (sector_i >= sector_total)
			return 0xFFFFUL;

		if (iolen >= 4) {
			w = host_readd(sector+sector_i);
			sector_i += 4;
		}
		else if (iolen >= 2) {
			w = host_readw(sector+sector_i);
			sector_i += 2;
		}
		/* NTS: Some MS-DOS CD-ROM drivers like OAKCDROM.SYS use byte-wide I/O for the initial identification */
		else if (iolen == 1) {
			w = sector[sector_i++];
		}

		if (sector_i >= sector_total)
			io_completion();

		return w;
	}

	virtual void data_write(Bitu v,Bitu iolen) { /* write to 1F0h data port to IDE device */
		if (state == IDE_DEV_ATAPI_PACKET_COMMAND) {
			if (atapi_cmd_i < atapi_cmd_total)
				atapi_cmd[atapi_cmd_i++] = (unsigned char)v;
			if (iolen >= 2 && atapi_cmd_i < atapi_cmd_total)
				atapi_cmd[atapi_cmd_i++] = (unsigned char)(v >> 8);
			if (iolen >= 4 && atapi_cmd_i < atapi_cmd_total) {
				atapi_cmd[atapi_cmd_i++] = (unsigned char)(v >> 16);
				atapi_cmd[atapi_cmd_i++] = (unsigned char)(v >> 24);
			}

			if (atapi_cmd_i >= atapi_cmd_total)
				atapi_cmd_completion();
		}
		else {
			if (state != IDE_DEV_DATA_WRITE) {
				LOG_MSG("ide atapi warning: data write when device not in data_write state");
				return;
			}
			if (!(status & IDE_STATUS_DRQ)) {
				LOG_MSG("ide atapi warning: data write with drq=0");
				return;
			}
			if ((sector_i+iolen) > sector_total) {
				LOG_MSG("ide atapi warning: sector already full %lu / %lu",(unsigned long)sector_i,(unsigned long)sector_total);
				return;
			}

			if (iolen >= 4) {
				host_writed(sector+sector_i,(Bit32u)v);
				sector_i += 4;
			}
			else if (iolen >= 2) {
				host_writew(sector+sector_i,(Bit32u)v);
				sector_i += 2;
			}
			else if (iolen == 1) {
				sector[sector_i++] = (unsigned char)v;
			}

			if (sector_i >= sector_total)
				io_completion();
		}
	}

	void update_from_cdrom() {
		CDROM_Interface *cdrom = getMSCDEXDrive();
		if (cdrom == NULL) {
			LOG_MSG("WARNING: IDE update from CD-ROM failed, disk not available");
			return;
		}
	}

	void generate_identify_device() {
		unsigned char csum;
		Bitu i;

		/* IN RESPONSE TO IDENTIFY DEVICE (0xA1)
		   GENERATE 512-BYTE REPLY */
		memset(sector,0,512);

		host_writew(sector+(0*2),0x85C0U);  /* ATAPI device, command set #5 (what does that mean?), removable, */

		// These strings are encoded with a 2 byte invert pattern (1234 becomes 2143)
		memcpy(sector+(10*2), "21436587 9          ",                     20); //id_serial "123456789", 20 bytes, padded with space
		memcpy(sector+(23*2), ".038X-  ",                                  8); //id_firmware_rev "0.83-X", 8 bytes, padded with space
		memcpy(sector+(27*2), "ODBSxoX-V riutlaC -DOR M                ", 40); //id_model "DOSBox-X Virtual CD-ROM", 40 bytes, padded with space

		host_writew(sector+(49*2),
			0x0800UL|/*IORDY supported*/
			0x0200UL|/*must be one*/
			0);
		host_writew(sector+(50*2),
			0x4000UL);
		host_writew(sector+(51*2),
			0x00F0UL);
		host_writew(sector+(52*2),
			0x00F0UL);
		host_writew(sector+(53*2),
			0x0006UL);
		host_writew(sector+(64*2),      /* PIO modes supported */
			0x0003UL);
		host_writew(sector+(67*2),      /* PIO cycle time */
			0x0078UL);
		host_writew(sector+(68*2),      /* PIO cycle time */
			0x0078UL);
		host_writew(sector+(80*2),0x007E); /* major version number. Here we say we support ATA-1 through ATA-8 */
		host_writew(sector+(81*2),0x0022); /* minor version */
		host_writew(sector+(82*2),0x4008); /* command set: NOP, DEVICE RESET[XXXXX], POWER MANAGEMENT */
		host_writew(sector+(83*2),0x0000); /* command set: LBA48[XXXX] */
		host_writew(sector+(85*2),0x4208); /* commands in 82 enabled */
		host_writew(sector+(86*2),0x0000); /* commands in 83 enabled */

		/* ATA-8 integrity checksum */
		sector[510] = 0xA5;
		csum = 0; for (i=0;i < 511;i++) csum += sector[i];
		sector[511] = 0 - csum;
	}

	void generate_mmc_inquiry() {
		/* IN RESPONSE TO ATAPI COMMAND 0x12: INQUIRY */
		memset(sector,0,36);
		sector[0] = (0 << 5) | 5;   /* Peripheral qualifier=0   device type=5 (CDROM) */
		sector[1] = 0x80;       /* RMB=1 removable media */
		sector[3] = 0x21;
		sector[4] = 36 - 5;     /* additional length */

		/* id_mmc_product_id must not contain the word "Virtual" otherwise SafeDisc2 copy protection refuses to run */
		memcpy(sector+8, "DOSBox-X", 8); //id_mmc_vendor_id, 8 bytes, padded with space
		memcpy(sector+16, "DOSBox CD-ROM   ", 16); //id_mmc_product_id, 16 bytes, padded with space
		memcpy(sector+32, "0.83", 4); //id_mmc_product_rev 4 bytes, padded with space
	}

	void prepare_read(Bitu offset,Bitu size) {
		/* I/O must be WORD ALIGNED */
		DBP_ASSERT((offset&1) == 0);
		//assert((size&1) == 0);

		sector_i = offset;
		sector_total = size;
		DBP_ASSERT(sector_i <= sector_total);
		DBP_ASSERT(sector_total <= sizeof(sector));
	}

	void prepare_write(Bitu offset,Bitu size) {
		/* I/O must be WORD ALIGNED */
		DBP_ASSERT((offset&1) == 0);
		//assert((size&1) == 0);

		sector_i = offset;
		sector_total = size;
		DBP_ASSERT(sector_i <= sector_total);
		DBP_ASSERT(sector_total <= sizeof(sector));
	}

	void set_sense(unsigned char SK,unsigned char ASC=0,unsigned char ASCQ=0,unsigned int info=0) {
		const unsigned int len = 18;
		memset(sense,0,len);
		sense_length = len;

		sense[0] = 0xF0;    /* RESPONSE CODE */
		sense[2] = SK&0xF;  /* SENSE KEY */
		sense[3] = (unsigned char)(info >> 24u);
		sense[4] = (unsigned char)(info >> 16u);
		sense[5] = (unsigned char)(info >> 8u);
		sense[6] = (unsigned char)(info >> 0u);
		sense[7] = len - 18;    /* additional sense length */
		sense[12] = ASC;
		sense[13] = ASCQ;
	}

	void atapi_add_pic_event(PIC_EventHandler handler,float delay)
	{
		PIC_RemoveSpecificEvents(IDE_ATAPI_SpinDown,device_index);
#ifdef C_DBP_ENABLE_IDE_CDINSERTION_DELAY
		PIC_RemoveSpecificEvents(IDE_ATAPI_CDInsertion,device_index);
#endif
		PIC_RemoveSpecificEvents(IDE_ATAPI_SpinUpComplete,device_index);
		if (handler) PIC_AddEvent(handler,delay/*ms*/,device_index);
	}

	static void IDE_ATAPI_SpinDown(Bitu dev_idx/*which IDE device*/) {
		IDEDevice *dev = GetByIndex(dev_idx);
		if (dev == NULL) return;

		if (dev->type == IDE_TYPE_CDROM) {
			IDEATAPICDROMDevice *atapi = (IDEATAPICDROMDevice*)dev;

			if (atapi->loading_mode == LOAD_DISC_READIED || atapi->loading_mode == LOAD_READY) {
				atapi->loading_mode = LOAD_IDLE;
				//LOG_MSG("ATAPI CD-ROM: spinning down");
			}
		}
		else {
			LOG_MSG("Unknown ATAPI spinup callback");
		}
	}

#ifdef C_DBP_ENABLE_IDE_CDINSERTION_DELAY
	static void IDE_ATAPI_CDInsertion(Bitu dev_idx/*which IDE device*/) {
		IDEDevice *dev = GetByIndex(dev_idx);
		if (dev == NULL) return;

		if (dev->type == IDE_TYPE_CDROM) {
			IDEATAPICDROMDevice *atapi = (IDEATAPICDROMDevice*)dev;

			if (atapi->loading_mode == LOAD_INSERT_CD) {
				atapi->loading_mode = LOAD_DISC_LOADING;
				LOG_MSG("ATAPI CD-ROM: insert CD to loading");
				atapi->atapi_add_pic_event(IDE_ATAPI_SpinUpComplete,atapi_spinup_time/*ms*/);
			}
		}
		else {
			LOG_MSG("Unknown ATAPI spinup callback");
		}
	}
#endif

	static void IDE_ATAPI_SpinUpComplete(Bitu dev_idx/*which IDE device*/) {
		IDEDevice *dev = GetByIndex(dev_idx);
		if (dev == NULL) return;

		if (dev->type == IDE_TYPE_CDROM) {
			IDEATAPICDROMDevice *atapi = (IDEATAPICDROMDevice*)dev;

			if (atapi->loading_mode == LOAD_DISC_LOADING) {
				atapi->loading_mode = LOAD_DISC_READIED;
				//LOG_MSG("ATAPI CD-ROM: spinup complete");
				atapi->atapi_add_pic_event(IDE_ATAPI_SpinDown,atapi_spindown_timeout/*ms*/);
			}
		}
		else {
			LOG_MSG("Unknown ATAPI spinup callback");
		}
	}

	/* returns "true" if command should proceed as normal, "false" if sense data was set and command should not proceed.
	 * this function helps to enforce virtual "spin up" and "ready" delays. */
	bool common_spinup_response(bool trigger,bool wait) {
		if (loading_mode == LOAD_IDLE) {
			if (trigger) {
				//LOG_MSG("ATAPI CD-ROM: triggered to spin up from idle");
				loading_mode = LOAD_DISC_LOADING;
				atapi_add_pic_event(IDE_ATAPI_SpinUpComplete,atapi_spinup_time/*ms*/);
			}
		}
		else if (loading_mode == LOAD_READY) {
			if (trigger) {
				atapi_add_pic_event(IDE_ATAPI_SpinDown,atapi_spindown_timeout/*ms*/);
			}
		}

		switch (loading_mode) {
			case LOAD_NO_DISC:
#ifdef C_DBP_ENABLE_IDE_CDINSERTION_DELAY
			case LOAD_INSERT_CD:
#endif
				set_sense(/*SK=*/0x02,/*ASC=*/0x3A); /* Medium Not Present */
				return false;
			case LOAD_DISC_LOADING:
				if (has_changed && !wait/*if command will block until LOADING complete*/) {
					set_sense(/*SK=*/0x02,/*ASC=*/0x04,/*ASCQ=*/0x01); /* Medium is becoming available */
					return false;
				}
				break;
			case LOAD_DISC_READIED:
				loading_mode = LOAD_READY;
				if (has_changed) {
					if (trigger) has_changed = false;
					set_sense(/*SK=*/0x02,/*ASC=*/0x28,/*ASCQ=*/0x00); /* Medium is ready (has changed) */
					//DBP: Added this if to not return false when coming from on_atapi_busy_time so an initial INQUIRY (0x12) request
					//from Win9x doesn't end up unanswered which causes the drive to not show up in the OS.
					if (!trigger || wait)
						return false;
				}
				break;
			case LOAD_IDLE:
			case LOAD_READY:
				break;
			default:
				abort();
		}

		return true;
	}

	void on_mode_select_io_complete() {
		unsigned int AllocationLength = ((unsigned int)atapi_cmd[7] << 8) + atapi_cmd[8];
		unsigned char *scan,*fence;

		/* the first 8 bytes are a mode parameter header.
		 * It's supposed to provide length, density, etc. or whatever the hell
		 * it means. Windows 95 seems to send all zeros there, so ignore it.
		 *
		 * we care about the bytes following it, which contain page_0 mode
		 * pages */

		scan = sector + 8;
		fence = sector + IDEMIN((unsigned int)sector_total,(unsigned int)AllocationLength);

		while ((scan+2) < fence) {
			unsigned char PAGE = *scan++;
			unsigned int LEN = (unsigned int)(*scan++);

			if ((scan+LEN) > fence) {
				LOG_MSG("ATAPI MODE SELECT warning, page_0 length extends %u bytes past buffer",(unsigned int)(scan+LEN-fence));
				break;
			}

			LOG_MSG("ATAPI MODE SELECT, PAGE 0x%02x len=%u",PAGE,LEN);
			#if 0
			printf("  ");
			for (unsigned int i=0;i < LEN;i++) printf("%02x ",scan[i]);
			printf("\n");
			#endif

			scan += LEN;
		}
	}

	void atapi_io_completion() {
		#if 0
		LOG_MSG("[ATAPI] IO COMPLETE - COUNT: %d - CMD: %s (0x%02x) - LBA: %x %x %x (%u)",
			(int)count, getATAPICommandName(atapi_cmd[0]), (int)atapi_cmd[0], lba[0], lba[1], lba[2], (unsigned)((lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8)));
		#endif
		/* for most ATAPI PACKET commands, the transfer is done and we need to clear
		   all indication of a possible data transfer */

		if (count != 0x03) { /* the command was expecting data. now it can act on it */
			switch (atapi_cmd[0]) {
				case 0x28:/*READ(10)*/
				case 0xA8:/*READ(12)*/
					/* How much does the guest want to transfer? */
					/* NTS: This is required to work correctly with the ide-cd driver in the Linux kernel.
					 *      The Linux kernel appears to negotiate a 32KB or 64KB transfer size here even
					 *      if the total transfer from a CD READ would exceed that size, and it expects
					 *      the full result in those DRQ block transfer sizes. */
					sector_total = (lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8);

					/* FIXME: We actually should NOT be capping the transfer length, but instead should
					   be breaking the larger transfer into smaller DRQ block transfers like
					   most IDE ATAPI drives do. Writing the test IDE code taught me that if you
					   go to most drives and request a transfer length of 0xFFFE the drive will
					   happily set itself up to transfer that many sectors in one IDE command! */
					/* NTS: In case you're wondering, it's legal to issue READ(10) with transfer length == 0.
					   MSCDEX.EXE does it when starting up, for example */
					TransferLength = TransferLengthRemaining;
					if ((TransferLength*2048) > sizeof(sector))
						TransferLength = sizeof(sector)/2048;
					if ((TransferLength*2048) > sector_total)
						TransferLength = sector_total/2048;

					LBA = LBAnext;
					DBP_ASSERT(TransferLengthRemaining >= TransferLength);
					TransferLengthRemaining -= TransferLength;

					if (TransferLength != 0) {
						//LOG_MSG("ATAPI CD READ CD LBA=%x xfer=%x xferrem=%x continued",(unsigned int)LBA,(unsigned int)TransferLength,(unsigned int)TransferLengthRemaining);

						count = 0x02;
						state = IDE_DEV_ATAPI_BUSY;
						status = IDE_STATUS_BUSY;
						/* TODO: Emulate CD-ROM spin-up delay, and seek delay */
						PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
						PIC_AddEvent(IDE_DelayedCommand,(3)/*ms*/,device_index);
						return;
					}
					else {
						//LOG_MSG("ATAPI CD READ LBA=%x xfer=%x xferrem=%x transfer complete",(unsigned int)LBA,(unsigned int)TransferLength,(unsigned int)TransferLengthRemaining);
					}
					break;
				case 0xBE:/*READ CD*/
					/* How much does the guest want to transfer? */
					sector_total = (lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8);

					TransferLength = TransferLengthRemaining;
					if (TransferSectorSize > 0) {
						if ((TransferLength*TransferSectorSize) > sizeof(sector))
							TransferLength = sizeof(sector)/TransferSectorSize;
						if ((TransferLength*TransferSectorSize) > sector_total)
							TransferLength = sector_total/TransferSectorSize;

						DBP_ASSERT(TransferLengthRemaining >= TransferLength);
						TransferLengthRemaining -= TransferLength;
					}
					else {
						TransferLengthRemaining = 0;
						TransferLength = 0;
					}

					LBA = LBAnext;
					DBP_ASSERT(TransferLengthRemaining >= TransferLength);
					TransferLengthRemaining -= TransferLength;

					if (TransferLength != 0) {
						//LOG_MSG("ATAPI CD READ CD LBA=%x xfer=%x xferrem=%x continued",(unsigned int)LBA,(unsigned int)TransferLength,(unsigned int)TransferLengthRemaining);

						count = 0x02;
						state = IDE_DEV_ATAPI_BUSY;
						status = IDE_STATUS_BUSY;
						/* TODO: Emulate CD-ROM spin-up delay, and seek delay */
						PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
						PIC_AddEvent(IDE_DelayedCommand,(3)/*ms*/,device_index);
						return;
					}
					else {
						//LOG_MSG("ATAPI CD READ LBA=%x xfer=%x xferrem=%x transfer complete",(unsigned int)LBA,(unsigned int)TransferLength,(unsigned int)TransferLengthRemaining);
					}
					break;

				case 0x55: /* MODE SELECT(10) */
					on_mode_select_io_complete();
					break;
			}
		}

		count = 0x03; /* no more data (command/data=1, input/output=1) */
		status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
		state = IDE_DEV_READY;
		allow_writing = true;

		/* Apparently: real IDE ATAPI controllers fire another IRQ after the transfer.
		   And there are MS-DOS CD-ROM drivers that assume that. */
		raise_irq();
	}

	void io_completion() {
		#if 0
		LOG_MSG("[ATAPI] [IDE] IO COMPLETION - CMD: %s (0x%02x) - FEATURE: %x - LBA: %x %x %x (%u)",
			getIDECommandName(command), (int)command, feature, lba[0], lba[1], lba[2], (unsigned)((lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8)));
		#endif

		/* lower DRQ */
		status &= ~IDE_STATUS_DRQ;

		/* depending on the command, either continue it or finish up */
		switch (command) {
			case 0xA0:/*ATAPI PACKET*/
				atapi_io_completion();
				break;
			default: /* most commands: signal drive ready, return to ready state */
				/* NTS: Some MS-DOS CD-ROM drivers will loop endlessly if we never set "drive seek complete"
						because they like to hit the device with DEVICE RESET (08h) whether or not it's
					a hard disk or CD-ROM drive */
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				state = IDE_DEV_READY;
				allow_writing = true;
				count = 0x03; /* no more data (command/data=1, input/output=1) */
				break;
		}
	}

	/* TODO: Your code should also be paying attention to the "transfer length" field
			 in many of the commands here. Right now it doesn't matter. */
	void atapi_cmd_completion() {
		#if 0
		LOG_MSG("[ATAPI] CMD COMPLETION - CMD: %s (0x%02x) - DATA: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x to_host=%u", getATAPICommandName(atapi_cmd[0]),
			atapi_cmd[ 0],atapi_cmd[ 1],atapi_cmd[ 2],atapi_cmd[ 3],atapi_cmd[ 4],atapi_cmd[ 5],
			atapi_cmd[ 6],atapi_cmd[ 7],atapi_cmd[ 8],atapi_cmd[ 9],atapi_cmd[10],atapi_cmd[11],
			atapi_to_host);
		#endif

		switch (atapi_cmd[0]) {
			case 0x00: /* TEST UNIT READY */
				if (common_spinup_response(/*spin up*/false,/*wait*/false))
					set_sense(0); /* <- nothing wrong */

				count = 0x03;
				state = IDE_DEV_READY;
				feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
				status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
				raise_irq();
				allow_writing = true;
				break;
			case 0x03: /* REQUEST SENSE */
				count = 0x02;
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
				count = 0x02;
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			case 0x25: /* READ CAPACITY */
				count = 0x02;
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			case 0x2B: /* SEEK */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */
					count = 0x02;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0x12: /* INQUIRY */
				DBP_ASSERT(!(atapi_cmd[1] & 0x01));  /* EVPD (Enable Vital Product Data) bit not supported (see atapi.c from qemu) */
				DBP_ASSERT(atapi_cmd[2] == 0); /* without EVPD, PAGE CODE must be zero */
				count = 0x02;
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			case 0xBE: /* READ CD */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */

					/* How much does the guest want to transfer? */
					/* NTS: This is required to work correctly with Windows NT 4.0. Windows NT will emit a READ CD
					 *      command at startup with transfer length == 0. If an error is returned, NT ignores the
					 *      CD-ROM drive entirely and acts like it's in a perpetual error state. */
					sector_total = (lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8);
					LBA = ((Bitu)atapi_cmd[2] << 24UL) |
						((Bitu)atapi_cmd[3] << 16UL) |
						((Bitu)atapi_cmd[4] << 8UL) |
						((Bitu)atapi_cmd[5] << 0UL);
					TransferLength = ((Bitu)atapi_cmd[6] << 16UL) |
						((Bitu)atapi_cmd[7] << 8UL) |
						((Bitu)atapi_cmd[8]);

					/* Sector size? */
					TransferSectorType = (atapi_cmd[1] >> 2) & 7u; /* RESERVED=[7:5] ExpectedSectorType=[4:2] RESERVED=[1:1] RELOAD=[0:0] */
					TransferReadCD9 = atapi_cmd[9]; /* SYNC=[7:7] HeaderCodes=[6:5] UserData=[4:4] EDCECC=[3:3] ErrorField=[2:1] RESERVED=[0:0] */
					DBP_ASSERT(atapi_cmd[10] == 0); /* subchannels not supported */

					if (TransferSectorType <= 5) { /* Treat unspecified sector type == 0 the same as CDDA with regard to sector size */

						static const uint16_t ReadCDTransferSectorSizeTable[5/*SectorType-1*/][0x20/*READ CD byte 9 >> 3*/] = {
								/* Sector type 0: Any
								 * Sector type 1: CDDA */
								{
										0,    /* 00h */ 0,    /* 08h */ 2352, /* 10h */ 2352, /* 18h */
										2352, /* 20h */ 2352, /* 28h */ 2352, /* 30h */ 2352, /* 38h */
										2352, /* 40h */ 2352, /* 48h */ 2352, /* 50h */ 2352, /* 58h */
										2352, /* 60h */ 2352, /* 68h */ 2352, /* 70h */ 2352, /* 78h */
										0,    /* 80h */ 0,    /* 88h */ 2352, /* 90h */ 2352, /* 98h */
										2352, /* A0h */ 2352, /* A8h */ 2352, /* B0h */ 2352, /* B8h */
										2352, /* C0h */ 2352, /* C8h */ 2352, /* D0h */ 2352, /* D8h */
										2352, /* E0h */ 2352, /* E8h */ 2352, /* F0h */ 2352  /* F8h */
								},
								/* Sector type 2: Mode 1 */
								{
										0,    /* 00h */ 0,    /* 08h */ 2048, /* 10h */ 2336, /* 18h */
										4,    /* 20h */ 0,    /* 28h */ 2052, /* 30h */ 2340, /* 38h */
										0,    /* 40h */ 0,    /* 48h */ 2048, /* 50h */ 2336, /* 58h */
										4,    /* 60h */ 0,    /* 68h */ 2052, /* 70h */ 2340, /* 78h */
										0,    /* 80h */ 0,    /* 88h */ 0,    /* 90h */ 0,    /* 98h */
										16,   /* A0h */ 0,    /* A8h */ 2064, /* B0h */ 2352, /* B8h */
										0,    /* C0h */ 0,    /* C8h */ 0,    /* D0h */ 0,    /* D8h */
										16,   /* E0h */ 0,    /* E8h */ 2064, /* F0h */ 2352  /* F8h */
								},
								/* Sector type 3: Mode 2 formless */
								{
										0,    /* 00h */ 0,    /* 08h */ 2336, /* 10h */ 2336, /* 18h */
										4,    /* 20h */ 0,    /* 28h */ 2340, /* 30h */ 2340, /* 38h */
										0,    /* 40h */ 0,    /* 48h */ 2336, /* 50h */ 2336, /* 58h */
										4,    /* 60h */ 4,    /* 68h */ 12,   /* 70h */ 12,   /* 78h */
										0,    /* 80h */ 0,    /* 88h */ 0,    /* 90h */ 0,    /* 98h */
										16,   /* A0h */ 0,    /* A8h */ 2352, /* B0h */ 2352, /* B8h */
										0,    /* C0h */ 0,    /* C8h */ 0,    /* D0h */ 0,    /* D8h */
										16,   /* E0h */ 0,    /* E8h */ 2352, /* F0h */ 2352  /* F8h */
								},
								/* Sector type 4: Mode 2 form 1 */
								{
										0,    /* 00h */ 0,    /* 08h */ 2048, /* 10h */ 2328, /* 18h */
										4,    /* 20h */ 0,    /* 28h */ 0,    /* 30h */ 0,    /* 38h */
										8,    /* 40h */ 0,    /* 48h */ 2056, /* 50h */ 2336, /* 58h */
										12,   /* 60h */ 0,    /* 68h */ 2060, /* 70h */ 2340, /* 78h */
										0,    /* 80h */ 0,    /* 88h */ 0,    /* 90h */ 0,    /* 98h */
										16,   /* A0h */ 0,    /* A8h */ 0,    /* B0h */ 0,    /* B8h */
										0,    /* C0h */ 0,    /* C8h */ 0,    /* D0h */ 0,    /* D8h */
										24,   /* E0h */ 0,    /* E8h */ 2072, /* F0h */ 2352  /* F8h */
								},
								/* Sector type 5: Mode 2 form 2 */
								{
										0,    /* 00h */ 0,    /* 08h */ 2328, /* 10h */ 2328, /* 18h */
										4,    /* 20h */ 0,    /* 28h */ 0,    /* 30h */ 0,    /* 38h */
										8,    /* 40h */ 0,    /* 48h */ 2336, /* 50h */ 2336, /* 58h */
										12,   /* 60h */ 0,    /* 68h */ 2340, /* 70h */ 2340, /* 78h */
										0,    /* 80h */ 0,    /* 88h */ 0,    /* 90h */ 0,    /* 98h */
										16,   /* A0h */ 0,    /* A8h */ 0,    /* B0h */ 0,    /* B8h */
										0,    /* C0h */ 0,    /* C8h */ 0,    /* D0h */ 0,    /* D8h */
										24,   /* E0h */ 0,    /* E8h */ 2352, /* F0h */ 2352  /* F8h */
								}
						};

						TransferSectorSize = ReadCDTransferSectorSizeTable[(TransferSectorType>0)?(TransferSectorType-1):0][TransferReadCD9>>3u];
					}
					else
						TransferSectorSize = 0;

					if (TransferReadCD9 & 4) /* include block and error bits */
						TransferSectorSize += 296;
					else if (TransferReadCD9 & 2) /* include error bits */
						TransferSectorSize += 294;

					/* keep track of the original transfer length */
					TransferLengthRemaining = TransferLength;

					if (TransferSectorSize > 0) {
						if ((TransferLength*TransferSectorSize) > sizeof(sector))
							TransferLength = sizeof(sector)/TransferSectorSize;
						if ((TransferLength*TransferSectorSize) > sector_total)
							TransferLength = sector_total/TransferSectorSize;

						DBP_ASSERT(TransferLengthRemaining >= TransferLength);
						TransferLengthRemaining -= TransferLength;
					}
					else {
						TransferLengthRemaining = 0;
						TransferLength = 0;
					}

					count = 0x02;
					LBAnext = LBA;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					/* TODO: Emulate CD-ROM spin-up delay, and seek delay */
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(3)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0xA8: /* READ(12) */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */

					/* How much does the guest want to transfer? */
					/* NTS: This is required to work correctly with the ide-cd driver in the Linux kernel.
					 *      The Linux kernel appears to negotiate a 32KB or 64KB transfer size here even
					 *      if the total transfer from a CD READ would exceed that size, and it expects
					 *      the full result in those DRQ block transfer sizes. */
					sector_total = (lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8);

					/* FIXME: MSCDEX.EXE appears to test the drive by issuing READ(10) with transfer length == 0.
					   This is all well and good but our response seems to cause a temporary 2-3 second
					   pause for each attempt. Why? */
					LBA = ((Bitu)atapi_cmd[2] << 24UL) |
						((Bitu)atapi_cmd[3] << 16UL) |
						((Bitu)atapi_cmd[4] << 8UL) |
						((Bitu)atapi_cmd[5] << 0UL);
					TransferLength = ((Bitu)atapi_cmd[6] << 24UL) |
						((Bitu)atapi_cmd[7] << 16UL) |
						((Bitu)atapi_cmd[8] << 8UL) |
						((Bitu)atapi_cmd[9]);

					/* keep track of the original transfer length */
					TransferLengthRemaining = TransferLength;

					/* FIXME: We actually should NOT be capping the transfer length, but instead should
					   be breaking the larger transfer into smaller DRQ block transfers like
					   most IDE ATAPI drives do. Writing the test IDE code taught me that if you
					   go to most drives and request a transfer length of 0xFFFE the drive will
					   happily set itself up to transfer that many sectors in one IDE command! */
					/* NTS: In case you're wondering, it's legal to issue READ(10) with transfer length == 0.
					   MSCDEX.EXE does it when starting up, for example */
					if ((TransferLength*2048) > sizeof(sector))
						TransferLength = sizeof(sector)/2048;
					if ((TransferLength*2048) > sector_total)
						TransferLength = sector_total/2048;

					DBP_ASSERT(TransferLengthRemaining >= TransferLength);
					TransferLengthRemaining -= TransferLength;
					LBAnext = LBA;

					//LOG_MSG("ATAPI CD READ LBA=%x xfer=%x xferrem=%x",(unsigned int)LBA,(unsigned int)TransferLength,(unsigned int)TransferLengthRemaining);

					count = 0x02;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					/* TODO: Emulate CD-ROM spin-up delay, and seek delay */
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(3)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0x28: /* READ(10) */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */

					/* How much does the guest want to transfer? */
					/* NTS: This is required to work correctly with the ide-cd driver in the Linux kernel.
					 *      The Linux kernel appears to negotiate a 32KB or 64KB transfer size here even
					 *      if the total transfer from a CD READ would exceed that size, and it expects
					 *      the full result in those DRQ block transfer sizes. */
					sector_total = (lba[1] & 0xFF) | ((lba[2] & 0xFF) << 8);

					/* FIXME: MSCDEX.EXE appears to test the drive by issuing READ(10) with transfer length == 0.
					   This is all well and good but our response seems to cause a temporary 2-3 second
					   pause for each attempt. Why? */
					LBA = ((Bitu)atapi_cmd[2] << 24UL) |
						((Bitu)atapi_cmd[3] << 16UL) |
						((Bitu)atapi_cmd[4] << 8UL) |
						((Bitu)atapi_cmd[5] << 0UL);
					TransferLength = ((Bitu)atapi_cmd[7] << 8) |
						((Bitu)atapi_cmd[8]);

					/* keep track of the original transfer length */
					TransferLengthRemaining = TransferLength;

					/* FIXME: We actually should NOT be capping the transfer length, but instead should
					   be breaking the larger transfer into smaller DRQ block transfers like
					   most IDE ATAPI drives do. Writing the test IDE code taught me that if you
					   go to most drives and request a transfer length of 0xFFFE the drive will
					   happily set itself up to transfer that many sectors in one IDE command! */
					/* NTS: In case you're wondering, it's legal to issue READ(10) with transfer length == 0.
					   MSCDEX.EXE does it when starting up, for example */
					if ((TransferLength*2048) > sizeof(sector))
						TransferLength = sizeof(sector)/2048;
					if ((TransferLength*2048) > sector_total)
						TransferLength = sector_total/2048;

					DBP_ASSERT(TransferLengthRemaining >= TransferLength);
					TransferLengthRemaining -= TransferLength;
					LBAnext = LBA;

					//LOG_MSG("ATAPI CD READ LBA=%x xfer=%x xferrem=%x",(unsigned int)LBA,(unsigned int)TransferLength,(unsigned int)TransferLengthRemaining);

					count = 0x02;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					/* TODO: Emulate CD-ROM spin-up delay, and seek delay */
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(3)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0x42: /* READ SUB-CHANNEL */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */

					count = 0x02;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0x43: /* READ TOC */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */

					count = 0x02;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0x45: /* PLAY AUDIO (1) */
			case 0x47: /* PLAY AUDIO MSF */
			case 0x4B: /* PAUSE/RESUME */
				if (common_spinup_response(/*spin up*/true,/*wait*/true)) {
					set_sense(0); /* <- nothing wrong */

					count = 0x02;
					state = IDE_DEV_ATAPI_BUSY;
					status = IDE_STATUS_BUSY;
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				}
				else {
					count = 0x03;
					state = IDE_DEV_READY;
					feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
					status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
					raise_irq();
					allow_writing = true;
				}
				break;
			case 0x55: /* MODE SELECT(10) */
				count = 0x00;   /* we will be accepting data */
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			case 0x5A: /* MODE SENSE(10) */
				count = 0x02;
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			case 0xBD: /* MECHANISM STATUS */
				count = 0x02;
				state = IDE_DEV_ATAPI_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(1)/*ms*/,device_index);
				break;
			default:
				/* we don't know the command, immediately return an error */
				LOG_MSG("Unknown ATAPI command %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
					atapi_cmd[ 0],atapi_cmd[ 1],atapi_cmd[ 2],atapi_cmd[ 3],atapi_cmd[ 4],atapi_cmd[ 5],
					atapi_cmd[ 6],atapi_cmd[ 7],atapi_cmd[ 8],atapi_cmd[ 9],atapi_cmd[10],atapi_cmd[11]);

				abort_error();
				count = 0x03; /* no more data (command/data=1, input/output=1) */
				feature = 0xF4;
				raise_irq();
				allow_writing = true;
				break;
		}
	}

	/* when the ATAPI command has been accepted, and the timeout has passed */
	void on_atapi_busy_time() {
		#if 0
		static const char* loadingModeNames[] = { "NO_DISC", "INSERT_CD", "IDLE", "DISC_LOADING", "DISC_READIED", "READY", "?????", "?????" };
		LOG_MSG("[ATAPI] BUSY TIME CMD: %s (0x%02x) - Loading Mode: %s - TransferSectorType: %u, TransferLength: %u, TransferSectorSize: %u, TransferLengthRemaining: %u",
			getATAPICommandName(atapi_cmd[0]), atapi_cmd[0], loadingModeNames[loading_mode], (unsigned)TransferSectorType, (unsigned)TransferLength, (unsigned)TransferSectorSize, (unsigned)TransferLengthRemaining);
		#endif
		/* if the drive is spinning up, then the command waits */
		if (loading_mode == LOAD_DISC_LOADING) {
			switch (atapi_cmd[0]) {
				case 0x00: /* TEST UNIT READY */
				case 0x03: /* REQUEST SENSE */
					allow_writing = true;
					break; /* do not delay */
				default:
					PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
					PIC_AddEvent(IDE_DelayedCommand,100/*ms*/,device_index);
					return;
			}
		}
		else if (loading_mode == LOAD_DISC_READIED) {
			switch (atapi_cmd[0]) {
				case 0x00: /* TEST UNIT READY */
				case 0x03: /* REQUEST SENSE */
					allow_writing = true;
					break; /* do not delay */
				default:
					if (!common_spinup_response(/*spin up*/true,/*wait*/false)) {
						count = 0x03;
						state = IDE_DEV_READY;
						feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
						status = IDE_STATUS_DRIVE_READY|((sense[2]&0xF) ? IDE_STATUS_ERROR:IDE_STATUS_DRIVE_SEEK_COMPLETE);
						raise_irq();
						allow_writing = true;
						return;
					}
					break;
			}
		}

		switch (atapi_cmd[0]) {
			case 0x03: /* REQUEST SENSE */
				prepare_read(0,IDEMIN((unsigned int)sense_length,(unsigned int)host_maximum_byte_count));
				memcpy(sector,sense,sense_length);
				set_sense(0); /* clear sense data now after it has been copied */

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
				count = 0x03;
				feature = 0x00;
				sector_total = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				/* Don't care. Do nothing. */

				goto write_back_lba;
			case 0x25: /* READ CAPACITY */ {
				const unsigned int secsize = 2048;
				int first,last;
				TMSF leadOut;

				CDROM_Interface *cdrom = getMSCDEXDrive();

				if (!cdrom || !cdrom->GetAudioTracks(first,last,leadOut)) {
					LOG_MSG("WARNING: ATAPI READ TOC failed to get track info");
				}

				uint32_t sec = (leadOut.min*60u*75u)+(leadOut.sec*75u)+leadOut.fr - 150u;

				prepare_read(0,IDEMIN((unsigned int)8,(unsigned int)host_maximum_byte_count));
				sector[0] = sec >> 24u;
				sector[1] = sec >> 16u;
				sector[2] = sec >> 8u;
				sector[3] = sec & 0xFF;
				sector[4] = secsize >> 24u;
				sector[5] = secsize >> 16u;
				sector[6] = secsize >> 8u;
				sector[7] = secsize & 0xFF;
				//LOG_MSG("sec=%lu secsize=%lu",sec,secsize);

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
				}
			case 0x2B: /* SEEK */
				count = 0x03;
				feature = 0x00;
				sector_total = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				/* Don't care. Do nothing. */

				/* Except... Windows 95's CD player expects the SEEK command to interrupt CD audio playback.
				 * In fact it depends on it to the exclusion of commands explicitly standardized to... you know...
				 * stop or pause playback. Oh Microsoft, you twits... */
				{
					CDROM_Interface *cdrom = getMSCDEXDrive();
					if (cdrom) {
						bool playing,pause;

						if (!cdrom->GetAudioStatus(playing,pause))
							playing = true;

						if (playing) {
							LOG_MSG("ATAPI: Interrupting CD audio playback due to SEEK");
							cdrom->StopAudio();
						}
					}
				}

				goto write_back_lba;
			case 0x12: /* INQUIRY */
				/* NTS: the state of atapi_to_host doesn't seem to matter. */
				generate_mmc_inquiry();
				prepare_read(0,IDEMIN((unsigned int)36,(unsigned int)host_maximum_byte_count));

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x28: /* READ(10) */
			case 0xA8: /* READ(12) */
				TransferSectorSize = 2048;
				TransferSectorType = 8; /* Special type, non-CD-DA, user data only */
				TransferReadCD9 = 1; /* Special type, non-CD-DA, user data only */
				/* fall through */
			case 0xBE: /* READ CD */
				if (TransferLength == 0 || TransferSectorSize == 0) {
					/* this is legal. the SCSI MMC standards say so.
					   and apparently, MSCDEX.EXE issues READ(10) commands with transfer length == 0
					   to test the drive, so we have to emulate this */
					feature = 0x00;
					count = 0x03; /* no more transfer */
					sector_total = 0;/*nothing to transfer */
					state = IDE_DEV_READY;
					status = IDE_STATUS_DRIVE_READY;
				}
				else {
					/* OK, try to read */
					CDROM_Interface *cdrom = getMSCDEXDrive();
					CDROM_Interface::atapi_res res;
					if (!cdrom)
						res = CDROM_Interface::ATAPI_NO_MEDIA;
					else if (TransferReadCD9 & 6)
						res = CDROM_Interface::ATAPI_ILLEGAL_MODE; /* TODO: support returning zero filled block and error bit areas */
					else
						res = cdrom->ReadSectorsAtapi(sector, sizeof(sector), LBA, TransferLength, TransferSectorType, TransferSectorSize);

					if (res == CDROM_Interface::ATAPI_OK) {
						prepare_read(0,IDEMIN((unsigned int)(TransferLength*TransferSectorSize),(unsigned int)host_maximum_byte_count));
						LBAnext = LBA + TransferLength;
						feature = 0x00;
						count = 0x02; /* data for computer */
						state = IDE_DEV_DATA_READ;
						status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					}
					else {
						LOG_MSG("ATAPI: Failed to read %lu sectors at %lu (res: %d)", (unsigned long)TransferLength,(unsigned long)LBA,(int)res);
						if (res == CDROM_Interface::ATAPI_ILLEGAL_MODE)
							set_sense(/*SK=*/0x05,/*ASC=*/0x64, /*ASCQ=*/0x00, (unsigned int)LBA); /* Illegal Request: Illegal Mode For This Track  */
						else if (res == CDROM_Interface::ATAPI_READ_ERROR)
							set_sense(/*SK=*/0x03,/*ASC=*/0x11, /*ASCQ=*/0x04, (unsigned int)LBA); /* Medium Error: Unrecovered Read Error */
						else if (res == CDROM_Interface::ATAPI_NO_MEDIA)
							set_sense(/*SK=*/0x02,/*ASC=*/0x3A); /* Medium Not Present */
						feature = ((sense[2]&0xF) << 4) | ((sense[2]&0xF) ? 0x04/*abort*/ : 0x00);
						count = 0x03; /* no more transfer */
						sector_total = 0;/*nothing to transfer */
						TransferLength = 0;
						TransferLengthRemaining = 0;
						state = IDE_DEV_READY;
						status = IDE_STATUS_DRIVE_READY|IDE_STATUS_ERROR;
					}
				}
				goto write_back_lba;
			case 0x42: /* READ SUB-CHANNEL */
				read_subchannel();

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x43: /* READ TOC */
				read_toc();

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x45: /* PLAY AUDIO(10) */
				play_audio10();

				count = 0x03;
				feature = 0x00;
				sector_total = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x47: /* PLAY AUDIO MSF */
				play_audio_msf();

				count = 0x03;
				feature = 0x00;
				sector_total = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x4B: /* PAUSE/RESUME */
				pause_resume();

				count = 0x03;
				feature = 0x00;
				sector_total = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0x55: /* MODE SELECT(10) */
				/* we need the data written first, will act in I/O completion routine */
				{
					unsigned int x;

					x = (unsigned int)lba[1] + ((unsigned int)lba[2] << 8u);

					/* Windows 95 likes to set 0xFFFF here for whatever reason.
					 * Negotiate it down to a maximum of 512 for sanity's sake */
					if (x > 512) x = 512;
					lba[2] = x >> 8u;
					lba[1] = x;

					//LOG_MSG("MODE SELECT expecting %u bytes",x);
					prepare_write(0,(x+1u)&(~1u));
				}

				feature = 0x00;
				state = IDE_DEV_DATA_WRITE;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				raise_irq();
				allow_writing = true;
				break;
			case 0x5A: /* MODE SENSE(10) */
				mode_sense();

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			case 0xBD: /* MECHANISM STATUS */
				mechanism_status();

				feature = 0x00;
				state = IDE_DEV_DATA_READ;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ|IDE_STATUS_DRIVE_SEEK_COMPLETE;

				goto write_back_lba;
			default:
				LOG_MSG("Unknown ATAPI command after busy wait. Why?");
				abort_error();
				raise_irq();
				allow_writing = true;
				break;
			write_back_lba:
				/* ATAPI protocol also says we write back into LBA 23:8 what we're going to transfer in the block */
				lba[2] = (uint16_t)(sector_total >> 8);
				lba[1] = (uint16_t)sector_total;

				#if 0
				LOG_MSG("[ATAPI] BUSY TIME DONE - FEATURE: %x - COUNT: %x - LBA: %x %x %x - DRIVEHEAD: %x",
					feature, count, lba[0], lba[1], lba[2], drivehead);
				#endif

				raise_irq();
				allow_writing = true;
				break;
		}
	}

	void mechanism_status() {
		unsigned char *write;

		write = sector;

		/* MECHANISM STATUS PARAMETER LIST */
		/* - Status Header */
		/* - Slot Table(s) */

		/* Status Header */
		*write++ = 0x00; // fault=0 changerstate=0 currentslot=0
		*write++ = (0 << 5u)/* mechanism state=idle=0 (TODO) */ | (0x00)/*door open=0*/;
		*write++ = 0x00; // current LBA (TODO)
		*write++ = 0x00; // .
		*write++ = 0x00; // .
		*write++ = 0x00; // number of slots available = 0
		*write++ = 0x00; // length of slot table(s)
		*write++ = 0x00; // .

		/* Slot table(s) */
		// None, we're not emulating ourselves as a CD changer.

		// TODO: Actually this command might be a neat way to expose the CD-ROM
		//       "swap chain" the user might have set up with IMGMOUNT before
		//       booting the guest OS. If enabled, we should report each and
		//       every ISO image like we're a CD changer. :)

		prepare_read(0,IDEMIN((unsigned int)(write-sector),(unsigned int)host_maximum_byte_count));
	}

	void read_subchannel() {
		//unsigned char Format = atapi_cmd[2] & 0xF;
		//unsigned char Track = atapi_cmd[6];
		unsigned char paramList = atapi_cmd[3];
		unsigned char attr,track,index;
		bool SUBQ = !!(atapi_cmd[2] & 0x40);
		bool TIME = !!(atapi_cmd[1] & 2);
		unsigned char *write;
		unsigned char astat;
		bool playing,pause;
		TMSF rel,abs;

		CDROM_Interface *cdrom = getMSCDEXDrive();
		if (cdrom == NULL) {
			LOG_MSG("WARNING: ATAPI READ TOC unable to get CDROM drive");
			prepare_read(0,8);
			return;
		}

		if (paramList == 0 || paramList > 3) {
			LOG_MSG("ATAPI READ SUBCHANNEL unknown param list");
			prepare_read(0,8);
			return;
		}
		else if (paramList == 2) {
			LOG_MSG("ATAPI READ SUBCHANNEL Media Catalog Number not supported");
			prepare_read(0,8);
			return;
		}
		else if (paramList == 3) {
			LOG_MSG("ATAPI READ SUBCHANNEL ISRC not supported");
			prepare_read(0,8);
			return;
		}

		/* get current subchannel position */
		if (!cdrom->GetAudioSub(attr,track,index,rel,abs)) {
			LOG_MSG("ATAPI READ SUBCHANNEL unable to read current pos");
			prepare_read(0,8);
			return;
		}

		if (!cdrom->GetAudioStatus(playing,pause))
			playing = pause = false;

		if (playing)
			astat = pause ? 0x12 : 0x11;
		else
			astat = 0x13;

		memset(sector,0,8);
		write = sector;
		*write++ = 0x00;
		*write++ = astat;/* AUDIO STATUS */
		*write++ = 0x00;/* SUBCHANNEL DATA LENGTH */
		*write++ = 0x00;

		if (SUBQ) {
			*write++ = 0x01;    /* subchannel data format code */
			*write++ = (attr >> 4) | 0x10;  /* ADR/CONTROL */
			*write++ = track;
			*write++ = index;
			if (TIME) {
				*write++ = 0x00;
				*write++ = abs.min;
				*write++ = abs.sec;
				*write++ = abs.fr;
				*write++ = 0x00;
				*write++ = rel.min;
				*write++ = rel.sec;
				*write++ = rel.fr;
			}
			else {
				uint32_t sec;

				sec = (abs.min*60u*75u)+(abs.sec*75u)+abs.fr - 150u;
				*write++ = (unsigned char)(sec >> 24u);
				*write++ = (unsigned char)(sec >> 16u);
				*write++ = (unsigned char)(sec >> 8u);
				*write++ = (unsigned char)(sec >> 0u);

				sec = (rel.min*60u*75u)+(rel.sec*75u)+rel.fr - 150u;
				*write++ = (unsigned char)(sec >> 24u);
				*write++ = (unsigned char)(sec >> 16u);
				*write++ = (unsigned char)(sec >> 8u);
				*write++ = (unsigned char)(sec >> 0u);
			}
		}

		{
			unsigned int x = (unsigned int)(write-sector) - 4;
			sector[2] = x >> 8;
			sector[3] = x;
		}

		prepare_read(0,IDEMIN((unsigned int)(write-sector),(unsigned int)host_maximum_byte_count));
		#if 0
		printf("SUBCH ");
		for (size_t i=0;i < sector_total;i++) printf("%02x ",sector[i]);
		printf("\n");
		#endif
	}

	void play_audio_msf() {
		uint32_t start_lba,end_lba;

		CDROM_Interface *cdrom = getMSCDEXDrive();
		if (cdrom == NULL) {
			LOG_MSG("WARNING: ATAPI READ TOC unable to get CDROM drive");
			sector_total = 0;
			return;
		}

		if (atapi_cmd[3] == 0xFF && atapi_cmd[4] == 0xFF && atapi_cmd[5] == 0xFF)
			start_lba = 0xFFFFFFFF;
		else {
			start_lba = (atapi_cmd[3] * 60u * 75u) +
				(atapi_cmd[4] * 75u) +
				atapi_cmd[5];

			if (start_lba >= 150u) start_lba -= 150u; /* LBA sector 0 == M:S:F sector 0:2:0 */
			else end_lba = 0;
		}

		if (atapi_cmd[6] == 0xFF && atapi_cmd[7] == 0xFF && atapi_cmd[8] == 0xFF)
			end_lba = 0xFFFFFFFF;
		else {
			end_lba = (atapi_cmd[6] * 60u * 75u) +
				(atapi_cmd[7] * 75u) +
				atapi_cmd[8];

			if (end_lba >= 150u) end_lba -= 150u; /* LBA sector 0 == M:S:F sector 0:2:0 */
			else end_lba = 0;
		}

		if (start_lba == end_lba) {
			/* The play length field specifies the number of contiguous logical blocks that shall
			 * be played. A play length of zero indicates that no audio operation shall occur.
			 * This condition is not an error. */
			/* TODO: How do we interpret that? Does that mean audio playback stops? Or does it
			 * mean we do nothing to the state of audio playback? */
			sector_total = 0;
			return;
		}

		/* LBA 0xFFFFFFFF means start playing wherever the optics of the CD sit */
		if (start_lba != 0xFFFFFFFF)
			cdrom->PlayAudioSector(start_lba,end_lba - start_lba);
		else
			cdrom->PauseAudio(true);

		sector_total = 0;
	}

	void pause_resume() {
		bool Resume = !!(atapi_cmd[8] & 1);

		CDROM_Interface *cdrom = getMSCDEXDrive();
		if (cdrom == NULL) {
			LOG_MSG("WARNING: ATAPI READ TOC unable to get CDROM drive");
			sector_total = 0;
			return;
		}

		cdrom->PauseAudio(Resume);
	}

	void play_audio10() {
		uint16_t play_length;
		uint32_t start_lba;

		CDROM_Interface *cdrom = getMSCDEXDrive();
		if (cdrom == NULL) {
			LOG_MSG("WARNING: ATAPI READ TOC unable to get CDROM drive");
			sector_total = 0;
			return;
		}

		start_lba = ((uint32_t)atapi_cmd[2] << 24) +
			((uint32_t)atapi_cmd[3] << 16) +
			((uint32_t)atapi_cmd[4] << 8) +
			((uint32_t)atapi_cmd[5] << 0);

		play_length = ((uint16_t)atapi_cmd[7] << 8) +
			((uint16_t)atapi_cmd[8] << 0);

		if (play_length == 0) {
			/* The play length field specifies the number of contiguous logical blocks that shall
			 * be played. A play length of zero indicates that no audio operation shall occur.
			 * This condition is not an error. */
			/* TODO: How do we interpret that? Does that mean audio playback stops? Or does it
			 * mean we do nothing to the state of audio playback? */
			sector_total = 0;
			return;
		}

		/* LBA 0xFFFFFFFF means start playing wherever the optics of the CD sit */
		if (start_lba != 0xFFFFFFFF)
			cdrom->PlayAudioSector(start_lba,play_length);
		else
			cdrom->PauseAudio(true);

		sector_total = 0;
	}

	void mode_sense() {
		unsigned char PAGE = atapi_cmd[2] & 0x3F;
		//unsigned char SUBPAGE = atapi_cmd[3];
		unsigned char *write;
		unsigned int x;

		write = sector;

		/* Mode Parameter List MMC-3 Table 340 */
		/* - Mode parameter header */
		/* - Page(s) */

		/* Mode Parameter Header (response for 10-byte MODE SENSE) SPC-2 Table 148 */
		*write++ = 0x00;    /* MODE DATA LENGTH                     (MSB) */
		*write++ = 0x00;    /*                                      (LSB) */
		*write++ = 0x00;    /* MEDIUM TYPE */
		*write++ = 0x00;    /* DEVICE-SPECIFIC PARAMETER */
		*write++ = 0x00;    /* Reserved */
		*write++ = 0x00;    /* Reserved */
		*write++ = 0x00;    /* BLOCK DESCRIPTOR LENGTH              (MSB) */
		*write++ = 0x00;    /*                                      (LSB) */
		/* NTS: MMC-3 Table 342 says that BLOCK DESCRIPTOR LENGTH is zero, where it would be 8 for legacy units */

		/* Mode Page Format MMC-3 Table 341 */
		*write++ = PAGE;    /* PS|reserved|Page Code */
		*write++ = 0x00;    /* Page Length (n - 1) ... Length in bytes of the mode parameters that follow */
		switch (PAGE) {
			case 0x01: /* Read error recovery MMC-3 Section 6.3.4 table 344 */
				*write++ = 0x00;    /* +2 Error recovery Parameter  AWRE|ARRE|TB|RC|Reserved|PER|DTE|DCR */
				*write++ = 3;       /* +3 Read Retry Count */
				*write++ = 0x00;    /* +4 Reserved */
				*write++ = 0x00;    /* +5 Reserved */
				*write++ = 0x00;    /* +6 Reserved */
				*write++ = 0x00;    /* +7 Reserved */
				*write++ = 0x00;    /* +8 Write Retry Count (this is not yet CD burner) */
				*write++ = 0x00;    /* +9 Reserved */
				*write++ = 0x00;    /* +10 Recovery Time Limit (should be zero)         (MSB) */
				*write++ = 0x00;    /* +11                                              (LSB) */
				break;
			case 0x0E: /* CD-ROM audio control MMC-3 Section 6.3.7 table 354 */
					   /* also MMC-1 Section 5.2.3.1 table 97 */
				*write++ = 0x04;    /* +2 Reserved|IMMED=1|SOTC=0|Reserved */
				*write++ = 0x00;    /* +3 Reserved */
				*write++ = 0x00;    /* +4 Reserved */
				*write++ = 0x00;    /* +5 Reserved */
				*write++ = 0x00;    /* +6 Obsolete (75) */
				*write++ = 75;      /* +7 Obsolete (75) */
				*write++ = 0x01;    /* +8 output port 0 selection (0001b = channel 0) */
				*write++ = 0xFF;    /* +9 output port 0 volume (0xFF = 0dB atten.) */
				*write++ = 0x02;    /* +10 output port 1 selection (0010b = channel 1) */
				*write++ = 0xFF;    /* +11 output port 1 volume (0xFF = 0dB atten.) */
				*write++ = 0x00;    /* +12 output port 2 selection (none) */
				*write++ = 0x00;    /* +13 output port 2 volume (0x00 = mute) */
				*write++ = 0x00;    /* +14 output port 3 selection (none) */
				*write++ = 0x00;    /* +15 output port 3 volume (0x00 = mute) */
				break;
			case 0x2A: /* CD-ROM mechanical status MMC-3 Section 6.3.11 table 361 */
									/*    MSB            |             |             |             |              |               |              |       LSB */
				*write++ = 0x07;    /* +2 Reserved       |Reserved     |DVD-RAM read |DVD-R read   |DVD-ROM read  |   Method 2    | CD-RW read   | CD-R read */
				*write++ = 0x00;    /* +3 Reserved       |Reserved     |DVD-RAM write|DVD-R write  |   Reserved   |  Test Write   | CD-RW write  | CD-R write */
				*write++ = 0x71;    /* +4 Buffer Underrun|Multisession |Mode 2 form 2|Mode 2 form 1|Digital Port 2|Digital Port 1 |  Composite   | Audio play */
				*write++ = 0xFF;    /* +5 Read code bar  |UPC          |ISRC         |C2 Pointers  |R-W deintcorr | R-W supported |CDDA accurate |CDDA support */
				*write++ = 0x2F;    /* +6 Loading mechanism type                     |Reserved     |Eject         |Prevent Jumper |Lock state    |Lock */
									/*      0 (0x00) = Caddy
									 *      1 (0x20) = Tray
									 *      2 (0x40) = Popup
									 *      3 (0x60) = Reserved
									 *      4 (0x80) = Changer with indivually changeable discs
									 *      5 (0xA0) = Changer using a magazine mechanism
									 *      6 (0xC0) = Reserved
									 *      6 (0xE0) = Reserved */
				*write++ = 0x03;    /* +7 Reserved       |Reserved     |R-W in leadin|Side chg cap |S/W slot sel  |Changer disc pr|Sep. ch. mute |Sep. volume levels */

				x = 176 * 8;        /* +8 maximum speed supported in kB: 8X  (obsolete in MMC-3) */
				*write++ = x>>8;
				*write++ = x;

				x = 256;            /* +10 Number of volume levels supported */
				*write++ = x>>8;
				*write++ = x;

				x = 6 * 256;        /* +12 buffer size supported by drive in kB */
				*write++ = x>>8;
				*write++ = x;

				x = 176 * 8;        /* +14 current read speed selected in kB: 8X  (obsolete in MMC-3) */
				*write++ = x>>8;
				*write++ = x;

				*write++ = 0;       /* +16 Reserved */
				*write++ = 0x00;    /* +17 Reserved | Reserved | Length | Length | LSBF | RCK | BCK | Reserved */

				x = 0;              /* +18 maximum write speed supported in kB: 0  (obsolete in MMC-3) */
				*write++ = x>>8;
				*write++ = x;

				x = 0;              /* +20 current write speed in kB: 0  (obsolete in MMC-3) */
				*write++ = x>>8;
				*write++ = x;
				break;
			default:
				memset(write,0,6); write += 6;
				LOG_MSG("WARNING: MODE SENSE on page 0x%02x not supported",PAGE);
				break;
		}

		/* mode param header, data length */
		x = (unsigned int)(write-sector) - 2;
		sector[0] = (unsigned char)(x >> 8u);
		sector[1] = (unsigned char)x;
		/* page length */
		sector[8+1] = (unsigned int)(write-sector) - 2 - 8;

		prepare_read(0,IDEMIN((unsigned int)(write-sector),(unsigned int)host_maximum_byte_count));

		#if 0
		printf("SENSE ");
		for (size_t i=0;i < sector_total;i++) printf("%02x ",sector[i]);
		printf("\n");
		#endif
	}

	void read_toc() {
		/* NTS: The SCSI MMC standards say we're allowed to indicate the return data
		 *      is longer than it's allocation length. But here's the thing: some MS-DOS
		 *      CD-ROM drivers will ask for the TOC but only provide enough room for one
		 *      entry (OAKCDROM.SYS) and if we signal more data than it's buffer, it will
		 *      reject our response and render the CD-ROM drive inaccessible. So to make
		 *      this emulation work, we have to cut our response short to the driver's
		 *      allocation length */
		unsigned int AllocationLength = ((unsigned int)atapi_cmd[7] << 8) + atapi_cmd[8];
		unsigned char Format = atapi_cmd[2] & 0xF;
		unsigned char Track = atapi_cmd[6];
		bool TIME = !!(atapi_cmd[1] & 2);
		unsigned char *write;
		int first,last,track;
		TMSF leadOut;

		CDROM_Interface *cdrom = getMSCDEXDrive();
		if (cdrom == NULL) {
			LOG_MSG("WARNING: ATAPI READ TOC unable to get CDROM drive");
			prepare_read(0,8);
			return;
		}

		memset(sector,0,8);

		if (!cdrom->GetAudioTracks(first,last,leadOut)) {
			LOG_MSG("WARNING: ATAPI READ TOC failed to get track info");
			prepare_read(0,8);
			return;
		}

		/* start 2 bytes out. we'll fill in the data length later */
		write = sector + 2;

		if (Format == 1) { /* Read multisession info */
			unsigned char attr;
			TMSF start;

			*write++ = (unsigned char)1;        /* @+2 first complete session */
			*write++ = (unsigned char)1;        /* @+3 last complete session */

			if (!cdrom->GetAudioTrackInfo(first,start,attr)) {
				LOG_MSG("WARNING: ATAPI READ TOC unable to read track %u information",first);
				attr = 0x41; /* ADR=1 CONTROL=4 */
				start.min = 0;
				start.sec = 0;
				start.fr = 0;
			}

			LOG_MSG("Track %u attr=0x%02x %02u:%02u:%02u",first,attr,start.min,start.sec,start.fr);

			*write++ = 0x00;        /* entry+0 RESERVED */
			*write++ = (attr >> 4) | 0x10;  /* entry+1 ADR=1 CONTROL=4 (DATA) */
			*write++ = (unsigned char)first;/* entry+2 TRACK */
			*write++ = 0x00;        /* entry+3 RESERVED */

			/* then, start address of first track in session */
			if (TIME) {
				*write++ = 0x00;
				*write++ = start.min;
				*write++ = start.sec;
				*write++ = start.fr;
			}
			else {
				uint32_t sec = (start.min*60u*75u)+(start.sec*75u)+start.fr - 150u;
				*write++ = (unsigned char)(sec >> 24u);
				*write++ = (unsigned char)(sec >> 16u);
				*write++ = (unsigned char)(sec >> 8u);
				*write++ = (unsigned char)(sec >> 0u);
			}
		}
		else if (Format == 0) { /* Read table of contents */
			*write++ = (unsigned char)first;    /* @+2 */
			*write++ = (unsigned char)last;     /* @+3 */

			for (track=first;track <= last;track++) {
				unsigned char attr;
				TMSF start;

				if (!cdrom->GetAudioTrackInfo(track,start,attr)) {
					LOG_MSG("WARNING: ATAPI READ TOC unable to read track %u information",track);
					attr = 0x41; /* ADR=1 CONTROL=4 */
					start.min = 0;
					start.sec = 0;
					start.fr = 0;
				}

				if (track < Track)
					continue;
				if ((write+8) > (sector+AllocationLength))
					break;

				LOG_MSG("Track %u attr=0x%02x %02u:%02u:%02u",first,attr,start.min,start.sec,start.fr);

				*write++ = 0x00;        /* entry+0 RESERVED */
				*write++ = (attr >> 4) | 0x10; /* entry+1 ADR=1 CONTROL=4 (DATA) */
				*write++ = (unsigned char)track;/* entry+2 TRACK */
				*write++ = 0x00;        /* entry+3 RESERVED */
				if (TIME) {
					*write++ = 0x00;
					*write++ = start.min;
					*write++ = start.sec;
					*write++ = start.fr;
				}
				else {
					uint32_t sec = (start.min*60u*75u)+(start.sec*75u)+start.fr - 150u;
					*write++ = (unsigned char)(sec >> 24u);
					*write++ = (unsigned char)(sec >> 16u);
					*write++ = (unsigned char)(sec >> 8u);
					*write++ = (unsigned char)(sec >> 0u);
				}
			}

			if ((write+8) <= (sector+AllocationLength)) {
				*write++ = 0x00;
				*write++ = 0x14;
				*write++ = 0xAA;/*TRACK*/
				*write++ = 0x00;
				if (TIME) {
					*write++ = 0x00;
					*write++ = leadOut.min;
					*write++ = leadOut.sec;
					*write++ = leadOut.fr;
				}
				else {
					uint32_t sec = (leadOut.min*60u*75u)+(leadOut.sec*75u)+leadOut.fr - 150u;
					*write++ = (unsigned char)(sec >> 24u);
					*write++ = (unsigned char)(sec >> 16u);
					*write++ = (unsigned char)(sec >> 8u);
					*write++ = (unsigned char)(sec >> 0u);
				}
			}
		}
		else {
			LOG_MSG("WARNING: ATAPI READ TOC Format=%u not supported",Format);
			prepare_read(0,8);
			return;
		}

		/* update the TOC data length field */
		{
			unsigned int x = (unsigned int)(write-sector) - 2;
			sector[0] = x >> 8;
			sector[1] = x & 0xFF;
		}

		prepare_read(0,IDEMIN(IDEMIN((unsigned int)(write-sector),(unsigned int)host_maximum_byte_count),AllocationLength));
	}
};

static void IDE_DelayedCommand(Bitu dev_idx/*which IDE device*/) {
	IDEDevice *dev = IDEDevice::GetByIndex(dev_idx);
	if (dev == NULL) return;

	#ifdef C_DBP_ENABLE_IDE_ATA
	if (dev->type == IDE_TYPE_HDD) {
		IDEATADevice *ata = (IDEATADevice*)dev;
		uint32_t sectorn = 0;/* FIXME: expand to uint64_t when adding LBA48 emulation */
		unsigned int sectcount;
		imageDisk *disk;
		//int i;

		switch (dev->command) {
			case 0x30:/* WRITE SECTOR */
				disk = ata->getBIOSdisk();
				if (disk == NULL) {
					LOG_MSG("ATA READ fail, bios disk N/A");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				sectcount = ata->count & 0xFF;
				if (sectcount == 0) sectcount = 256;
				if (drivehead_is_lba(ata->drivehead)) {
					/* LBA */
					sectorn = (uint32_t)(((ata->drivehead & 0xFu) << 24u) | (unsigned int)ata->lba[0] |
						((unsigned int)ata->lba[1] << 8u) |
						((unsigned int)ata->lba[2] << 16u));
				}
				else {
					/* C/H/S */
					if (ata->lba[0] == 0) {
						LOG_MSG("ATA sector 0 does not exist");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
					else if ((unsigned int)(ata->drivehead & 0xFu) >= (unsigned int)ata->heads ||
						(unsigned int)ata->lba[0] > (unsigned int)ata->sects ||
						(unsigned int)(ata->lba[1] | (ata->lba[2] << 8u)) >= (unsigned int)ata->cyls) {
						LOG_MSG("C/H/S %u/%u/%u out of bounds %u/%u/%u",
							(unsigned int)(ata->lba[1] | (ata->lba[2] << 8u)),
							(unsigned int)(ata->drivehead&0xFu),
							(unsigned int)(ata->lba[0]),
							(unsigned int)ata->cyls,
							(unsigned int)ata->heads,
							(unsigned int)ata->sects);
						ata->abort_error();
						dev->raise_irq();
						return;
					}

					sectorn = (uint32_t)(((ata->drivehead & 0xF) * ata->sects) +
						(((unsigned int)ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) * ata->sects * ata->heads) +
						((unsigned int)ata->lba[0] - 1u));
				}

				if (disk->Write_AbsoluteSector(sectorn, ata->sector) != 0) {
					LOG_MSG("Failed to write sector");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				/* NTS: the way this command works is that the drive writes ONE sector, then fires the IRQ
						and lets the host read it, then reads another sector, fires the IRQ, etc. One
					IRQ signal per sector. We emulate that here by adding another event to trigger this
					call unless the sector count has just dwindled to zero, then we let it stop. */
				if ((ata->count&0xFF) == 1) {
					/* end of the transfer */
					ata->count = 0;
					ata->status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					dev->raise_irq();
					ata->state = IDE_DEV_READY;
					ata->allow_writing = true;
					return;
				}
				else if ((ata->count&0xFF) == 0) ata->count = 255;
				else ata->count--;
				ata->progress_count++;

				if (!ata->increment_current_address()) {
					LOG_MSG("READ advance error");
					ata->abort_error();
					return;
				}

				/* begin another sector */
				dev->state = IDE_DEV_DATA_WRITE;
				dev->status = IDE_STATUS_DRQ|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				ata->prepare_write(0,512);
				dev->raise_irq();
				break;

			case 0x20:/* READ SECTOR */
				disk = ata->getBIOSdisk();
				if (disk == NULL) {
					LOG_MSG("ATA READ fail, bios disk N/A");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				sectcount = ata->count & 0xFF;
				if (sectcount == 0) sectcount = 256;
				if (drivehead_is_lba(ata->drivehead)) {
					/* LBA */
					sectorn = (uint32_t)((((unsigned int)ata->drivehead & 0xFu) << 24u) | (unsigned int)ata->lba[0] |
						((unsigned int)ata->lba[1] << 8u) |
						((unsigned int)ata->lba[2] << 16u));
				}
				else {
					/* C/H/S */
					if (ata->lba[0] == 0) {
						LOG_MSG("WARNING C/H/S access mode and sector==0");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
					else if ((unsigned int)(ata->drivehead & 0xF) >= (unsigned int)ata->heads ||
						(unsigned int)ata->lba[0] > (unsigned int)ata->sects ||
						(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) >= (unsigned int)ata->cyls) {
						LOG_MSG("C/H/S %u/%u/%u out of bounds %u/%u/%u",
							(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)),
							(unsigned int)(ata->drivehead&0xF),
							(unsigned int)ata->lba[0],
							(unsigned int)ata->cyls,
							(unsigned int)ata->heads,
							(unsigned int)ata->sects);
						ata->abort_error();
						dev->raise_irq();
						return;
					}

					sectorn = (uint32_t)(((ata->drivehead & 0xFu) * ata->sects) +
						(((unsigned int)ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) * ata->sects * ata->heads) +
						((unsigned int)ata->lba[0] - 1u));
				}

				if (disk->Read_AbsoluteSector(sectorn, ata->sector) != 0) {
					LOG_MSG("ATA read failed");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				/* NTS: the way this command works is that the drive reads ONE sector, then fires the IRQ
						and lets the host read it, then reads another sector, fires the IRQ, etc. One
					IRQ signal per sector. We emulate that here by adding another event to trigger this
					call unless the sector count has just dwindled to zero, then we let it stop. */
				/* NTS: The sector advance + count decrement is done in the I/O completion function */
				dev->state = IDE_DEV_DATA_READ;
				dev->status = IDE_STATUS_DRQ|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				ata->prepare_read(0,512);
				dev->raise_irq();
				break;

			case 0x40:/* READ SECTOR VERIFY WITH RETRY */
			case 0x41: /* READ SECTOR VERIFY WITHOUT RETRY */
				disk = ata->getBIOSdisk();
				if (disk == NULL) {
					LOG_MSG("ATA READ fail, bios disk N/A");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				sectcount = ata->count & 0xFF;
				if (sectcount == 0) sectcount = 256;
				if (drivehead_is_lba(ata->drivehead)) {
					/* LBA */
					sectorn = (uint32_t)((((unsigned int)ata->drivehead & 0xFu) << 24u) | (unsigned int)ata->lba[0] |
						((unsigned int)ata->lba[1] << 8u) |
						((unsigned int)ata->lba[2] << 16u));
				}
				else {
					/* C/H/S */
					if (ata->lba[0] == 0) {
						LOG_MSG("WARNING C/H/S access mode and sector==0");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
					else if ((unsigned int)(ata->drivehead & 0xF) >= (unsigned int)ata->heads ||
						(unsigned int)ata->lba[0] > (unsigned int)ata->sects ||
						(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) >= (unsigned int)ata->cyls) {
						LOG_MSG("C/H/S %u/%u/%u out of bounds %u/%u/%u",
							(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)),
							(unsigned int)(ata->drivehead&0xFu),
							(unsigned int)ata->lba[0],
							(unsigned int)ata->cyls,
							(unsigned int)ata->heads,
							(unsigned int)ata->sects);
						ata->abort_error();
						dev->raise_irq();
						return;
					}

					sectorn = (uint32_t)((((unsigned int)ata->drivehead & 0xFu) * ata->sects) +
						(((unsigned int)ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) * ata->sects * ata->heads) +
						((unsigned int)ata->lba[0] - 1u));
				}

				if (disk->Read_AbsoluteSector(sectorn, ata->sector) != 0) {
					LOG_MSG("ATA read failed");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				if ((ata->count&0xFF) == 1) {
					/* end of the transfer */
					ata->count = 0;
					ata->status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					dev->raise_irq();
					ata->state = IDE_DEV_READY;
					ata->allow_writing = true;
					return;
				}
				else if ((ata->count&0xFF) == 0) ata->count = 255;
				else ata->count--;
				ata->progress_count++;

				if (!ata->increment_current_address()) {
					LOG_MSG("READ advance error");
					ata->abort_error();
					return;
				}

				ata->state = IDE_DEV_BUSY;
				ata->status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,dev->device_index);
				PIC_AddEvent(IDE_DelayedCommand,0.00001f/*ms*/,dev->device_index);
				break;

			case 0xC4:/* READ MULTIPLE */
				disk = ata->getBIOSdisk();
				if (disk == NULL) {
					LOG_MSG("ATA READ fail, bios disk N/A");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				sectcount = ata->count & 0xFF;
				if (sectcount == 0) sectcount = 256;
				if (drivehead_is_lba(ata->drivehead)) {
					/* LBA */
					sectorn = (uint32_t)((((unsigned int)ata->drivehead & 0xFu) << 24u) | (unsigned int)ata->lba[0] |
						((unsigned int)ata->lba[1] << 8u) |
						((unsigned int)ata->lba[2] << 16u));
				}
				else {
					/* C/H/S */
					if (ata->lba[0] == 0) {
						LOG_MSG("WARNING C/H/S access mode and sector==0");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
					else if ((unsigned int)(ata->drivehead & 0xF) >= (unsigned int)ata->heads ||
						(unsigned int)ata->lba[0] > (unsigned int)ata->sects ||
						(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) >= (unsigned int)ata->cyls) {
						LOG_MSG("C/H/S %u/%u/%u out of bounds %u/%u/%u",
							(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)),
							(unsigned int)(ata->drivehead&0xF),
							(unsigned int)ata->lba[0],
							(unsigned int)ata->cyls,
							(unsigned int)ata->heads,
							(unsigned int)ata->sects);
						ata->abort_error();
						dev->raise_irq();
						return;
					}

					sectorn = (uint32_t)(((ata->drivehead & 0xF) * ata->sects) +
						(((unsigned int)ata->lba[1] | ((unsigned int)ata->lba[2] << 8u)) * ata->sects * ata->heads) +
						((unsigned int)ata->lba[0] - 1));
				}

				if ((512*ata->multiple_sector_count) > sizeof(ata->sector))
					E_Exit("SECTOR OVERFLOW");

				for (unsigned int cc=0;cc < IDEMIN((Bitu)ata->multiple_sector_count,(Bitu)sectcount);cc++) {
					/* it would be great if the disk object had a "read multiple sectors" member function */
					if (disk->Read_AbsoluteSector(sectorn+cc, ata->sector+(cc*512)) != 0) {
						LOG_MSG("ATA read failed");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
				}

				/* NTS: the way this command works is that the drive reads ONE sector, then fires the IRQ
						and lets the host read it, then reads another sector, fires the IRQ, etc. One
					IRQ signal per sector. We emulate that here by adding another event to trigger this
					call unless the sector count has just dwindled to zero, then we let it stop. */
				/* NTS: The sector advance + count decrement is done in the I/O completion function */
				dev->state = IDE_DEV_DATA_READ;
				dev->status = IDE_STATUS_DRQ|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				ata->prepare_read(0,512*IDEMIN((Bitu)ata->multiple_sector_count,(Bitu)sectcount));
				dev->raise_irq();
				break;

			case 0xC5:/* WRITE MULTIPLE */
				disk = ata->getBIOSdisk();
				if (disk == NULL) {
					LOG_MSG("ATA READ fail, bios disk N/A");
					ata->abort_error();
					dev->raise_irq();
					return;
				}

				sectcount = ata->count & 0xFF;
				if (sectcount == 0) sectcount = 256;
				if (drivehead_is_lba(ata->drivehead)) {
					/* LBA */
					sectorn = (uint32_t)((((unsigned int)ata->drivehead & 0xF) << 24) | (unsigned int)ata->lba[0] |
						((unsigned int)ata->lba[1] << 8) |
						((unsigned int)ata->lba[2] << 16));
				}
				else {
					/* C/H/S */
					if (ata->lba[0] == 0) {
						LOG_MSG("ATA sector 0 does not exist");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
					else if ((unsigned int)(ata->drivehead & 0xF) >= (unsigned int)ata->heads ||
						(unsigned int)ata->lba[0] > (unsigned int)ata->sects ||
						(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8)) >= (unsigned int)ata->cyls) {
						LOG_MSG("C/H/S %u/%u/%u out of bounds %u/%u/%u",
							(unsigned int)(ata->lba[1] | ((unsigned int)ata->lba[2] << 8)),
							(unsigned int)(ata->drivehead&0xF),
							(unsigned int)ata->lba[0],
							(unsigned int)ata->cyls,
							(unsigned int)ata->heads,
							(unsigned int)ata->sects);
						ata->abort_error();
						dev->raise_irq();
						return;
					}

					sectorn = (uint32_t)(((unsigned int)(ata->drivehead & 0xF) * ata->sects) +
						(((unsigned int)ata->lba[1] | ((unsigned int)ata->lba[2] << 8)) * ata->sects * ata->heads) +
						((unsigned int)ata->lba[0] - 1));
				}

				for (unsigned int cc=0;cc < IDEMIN((Bitu)ata->multiple_sector_count,(Bitu)sectcount);cc++) {
					/* it would be great if the disk object had a "write multiple sectors" member function */
					if (disk->Write_AbsoluteSector(sectorn+cc, ata->sector+(cc*512)) != 0) {
						LOG_MSG("Failed to write sector");
						ata->abort_error();
						dev->raise_irq();
						return;
					}
				}

				for (unsigned int cc=0;cc < IDEMIN((Bitu)ata->multiple_sector_count,(Bitu)sectcount);cc++) {
					if ((ata->count&0xFF) == 1) {
						/* end of the transfer */
						ata->count = 0;
						ata->status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
						dev->raise_irq();
						ata->state = IDE_DEV_READY;
						ata->allow_writing = true;
						return;
					}
					else if ((ata->count&0xFF) == 0) ata->count = 255;
					else ata->count--;
					ata->progress_count++;

					if (!ata->increment_current_address()) {
						LOG_MSG("READ advance error");
						ata->abort_error();
						return;
					}
				}

				/* begin another sector */
				sectcount = ata->count & 0xFF;
				if (sectcount == 0) sectcount = 256;
				dev->state = IDE_DEV_DATA_WRITE;
				dev->status = IDE_STATUS_DRQ|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				ata->prepare_write(0,512*IDEMIN((Bitu)ata->multiple_sector_count,(Bitu)sectcount));
				dev->raise_irq();
				break;
			case 0xEC:/*IDENTIFY DEVICE (CONTINUED) */
				dev->state = IDE_DEV_DATA_READ;
				dev->status = IDE_STATUS_DRQ|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				ata->generate_identify_device();
				ata->prepare_read(0,512);
				dev->count = 0x01;
				dev->lba[0] = 0x00;
				dev->feature = 0x00;
				dev->lba[1] = 0x00;
				dev->lba[2] = 0x00;
				dev->raise_irq();
				break;
			default:
				LOG_MSG("Unknown delayed IDE/ATA command");
				dev->abort_error();
				dev->raise_irq();
				break;
		}
	}
	else
	#endif
	if (dev->type == IDE_TYPE_CDROM) {
		IDEATAPICDROMDevice *atapi = (IDEATAPICDROMDevice*)dev;

		if (dev->state == IDE_DEV_ATAPI_BUSY) {
			switch (dev->command) {
				case 0xA0:/*ATAPI PACKET*/
					atapi->on_atapi_busy_time();
					break;
				default:
					LOG_MSG("Unknown delayed IDE/ATAPI busy wait command");
					dev->abort_error();
					dev->raise_irq();
					break;
			}
		}
		else {
			switch (dev->command) {
				case 0xA0:/*ATAPI PACKET*/
					if (atapi->atapi_cmd_i != atapi->atapi_cmd_total) {
						LOG_MSG("ATAPI WARNING: Start new ATAPI PACKET ATAPI command before finishing previous? Received %d of %d cmd bytes (%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x)", atapi->atapi_cmd_i, atapi->atapi_cmd_total,
							atapi->atapi_cmd[0], atapi->atapi_cmd[1], atapi->atapi_cmd[2], atapi->atapi_cmd[3], atapi->atapi_cmd[4], atapi->atapi_cmd[5], atapi->atapi_cmd[6], atapi->atapi_cmd[7], atapi->atapi_cmd[8], atapi->atapi_cmd[9], atapi->atapi_cmd[10], atapi->atapi_cmd[11]);
					}
					dev->state = IDE_DEV_ATAPI_PACKET_COMMAND;
					dev->status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE|IDE_STATUS_DRQ;
					dev->count = 0x01;  /* input/output == 0, command/data == 1 */
					atapi->atapi_cmd_total = 12; /* NTS: do NOT raise IRQ */
					atapi->atapi_cmd_i = 0;
					break;
				case 0xA1:/*IDENTIFY PACKET DEVICE (CONTINUED) */
					dev->state = IDE_DEV_DATA_READ;
					dev->status = IDE_STATUS_DRQ|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					atapi->generate_identify_device();
					atapi->prepare_read(0,512);
					dev->raise_irq();
					break;
				default:
					LOG_MSG("Unknown delayed IDE/ATAPI command");
					dev->abort_error();
					dev->raise_irq();
					break;
			}
		}
	}
	else {
		LOG_MSG("Unknown delayed command");
		dev->abort_error();
		dev->raise_irq();
	}
}

static IDEController *match_ide_controller(Bitu port) {
	for (unsigned int i=0;i < MAX_IDE_CONTROLLERS;i++) {
		IDEController *ide = idecontroller[i];
		if (ide == NULL) continue;
		if (ide->base_io != 0U && ide->base_io == (port&0xFFF8U)) return ide;
		if (ide->alt_io != 0U && ide->alt_io == (port&0xFFFEU)) return ide;
	}
	return NULL;
}

static void ide_altio_w(Bitu port,Bitu val,Bitu iolen) {
	IDEController *ide = match_ide_controller(port);
	if (ide == NULL) {
		LOG_MSG("WARNING: port read from I/O port not registered to IDE, yet callback triggered");
		return;
	}

	if (iolen == 4) {
		ide_altio_w(port,val&0xFFFF,2);
		ide_altio_w(port+2u,val>>16u,2);
		return;
	}

	port &= 1;

	if (port == 0) {/*3F6*/
		ide->interrupt_enable = (val&2u)?0:1;
		ide->check_device_irq();

		if ((val&4) && !ide->host_reset) {
			if (ide->device[0]) ide->device[0]->host_reset_begin();
			if (ide->device[1]) ide->device[1]->host_reset_begin();
			ide->host_reset=1;
		}
		else if (!(val&4) && ide->host_reset) {
			if (ide->device[0]) ide->device[0]->host_reset_complete();
			if (ide->device[1]) ide->device[1]->host_reset_complete();
			ide->host_reset=0;
		}
	}
}

static Bitu ide_altio_r(Bitu port,Bitu iolen) {
	IDEController *ide = match_ide_controller(port);
	IDEDevice *dev;

	if (ide == NULL) {
		LOG_MSG("WARNING: port read from I/O port not registered to IDE, yet callback triggered");
		return ~(0UL);
	}

	if (iolen == 4)
		return ide_altio_r(port,2) + (ide_altio_r(port+2u,2) << 16u);

	dev = ide->device[ide->select];

	port &= 1;

	if (port == 0)/*3F6(R) status, does NOT clear interrupt*/
		return (dev != NULL) ? dev->status : 0x00;
	else /*3F7(R) Drive Address Register*/
		return 0x80u|(ide->select==0?0u:1u)|(ide->select==1?0u:2u)|
			((dev != NULL) ? (((dev->drivehead&0xFu)^0xFu) << 2u) : 0x3Cu);

	return ~(0UL);
}

static Bitu ide_baseio_r(Bitu port,Bitu iolen) {
	IDEController *ide = match_ide_controller(port);
	IDEDevice *dev;
	Bitu ret = ~0ul;

	if (ide == NULL) {
		LOG_MSG("WARNING: port read from I/O port not registered to IDE, yet callback triggered");
		return ~(0UL);
	}

	if (iolen == 4)
		return ide_baseio_r(port,2) + (ide_baseio_r(port+2,2) << 16);

	dev = ide->device[ide->select];

	port &= 7;

	/* ATA-1 Section 7.2.13 Status Register: BSY (Busy) bit.
		*
		* BSY(Busy) is set whenever the drive has access to the Command Block Registers.
		* The host should not access the Command Block Register when BSY=1. When BSY=1,
		* a read of any Command Block Register shall return the contents of the Status
		* Register. */
	if (dev != NULL && (dev->status & IDE_STATUS_BUSY))
		port = 7;

	switch (port) {
		case 0: /* 1F0 */
			ret = (dev != NULL) ? dev->data_read(iolen) : 0xFFFFFFFFUL;
			break;
		case 1: /* 1F1 */
			ret = (dev != NULL) ? dev->feature : 0x00;
			break;
		case 2: /* 1F2 */
			ret = (dev != NULL) ? dev->count : 0x00;
			break;
		case 3: /* 1F3 */
			ret = (dev != NULL) ? dev->lba[0] : 0x00;
			break;
		case 4: /* 1F4 */
			ret = (dev != NULL) ? dev->lba[1] : 0x00;
			break;
		case 5: /* 1F5 */
			ret = (dev != NULL) ? dev->lba[2] : 0x00;
			break;
		case 6: /* 1F6 */
			ret = (dev != NULL) ? dev->drivehead : 0x00;
			break;
		case 7: /* 1F7 */
			/* reading this port clears the device pending IRQ */
			if (dev) {
					if (!(dev->status & IDE_STATUS_BUSY))
						dev->lower_irq();
			}

			ret = (dev != NULL) ? dev->status : 0x00;
			ide->check_device_irq();
			break;
	}

	#if 0
	if (ide == idecontroller[1])
		LOG_MSG("IDE: baseio read port %u ret %02x\n",(unsigned int)port,(unsigned int)ret);
	#endif

	return ret;
}

static void ide_baseio_w(Bitu port,Bitu val,Bitu iolen) {
	IDEController *ide = match_ide_controller(port);
	IDEDevice *dev;

	if (ide == NULL) {
		LOG_MSG("WARNING: port read from I/O port not registered to IDE, yet callback triggered");
		return;
	}

	if (iolen == 4) {
		ide_baseio_w(port,val&0xFFFF,2);
		ide_baseio_w(port+2,val>>16,2);
		return;
	}

	dev = ide->device[ide->select];

	port &= 7;

	/* ignore I/O writes if the controller is busy */
	if (dev) {
		if (dev->status & IDE_STATUS_BUSY) {
			if (port == 6 && ((val>>4)&1) == ide->select) {
				/* some MS-DOS drivers like ATAPICD.SYS are just very pedantic about writing to port +6 to ensure the right drive is selected */
				return;
			}
			else {
				LOG_MSG("W-%03X %02X BUSY DROP [DEV]",(int)(port+ide->base_io),(int)val);
				return;
			}
		}
	}

	#if 0
	if (ide == idecontroller[1])
		LOG_MSG("IDE: baseio write port %u val %02x",(unsigned int)port,(unsigned int)val);
	#endif

	if (port >= 1 && port <= 5 && dev && !dev->allow_writing) {
		LOG_MSG("IDE WARNING: Write to port %u val %02x when device not ready to accept writing",
			(unsigned int)port,(unsigned int)val);
	}

	switch (port) {
		case 0: /* 1F0 */
			if (dev) dev->data_write(val,iolen); /* <- TODO: what about 32-bit PIO modes? */
			break;
		case 1: /* 1F1 */
			if (dev && dev->allow_writing) /* TODO: LBA48 16-bit wide register */
				dev->feature = (uint16_t)val;
			break;
		case 2: /* 1F2 */
			if (dev && dev->allow_writing) /* TODO: LBA48 16-bit wide register */
				dev->count = (uint16_t)val;
			break;
		case 3: /* 1F3 */
			if (dev && dev->allow_writing) /* TODO: LBA48 16-bit wide register */
				dev->lba[0] = (uint16_t)val;
			break;
		case 4: /* 1F4 */
			if (dev && dev->allow_writing) /* TODO: LBA48 16-bit wide register */
				dev->lba[1] = (uint16_t)val;
			break;
		case 5: /* 1F5 */
			if (dev && dev->allow_writing) /* TODO: LBA48 16-bit wide register */
				dev->lba[2] = (uint16_t)val;
			break;
		case 6: /* 1F6 */
			if (((val>>4)&1) != ide->select) {
				/* update select pointer if bit 4 changes.
					also emulate IDE busy state when changing drives */
				if (dev) dev->deselect();
				ide->select = (val>>4)&1;
				dev = ide->device[ide->select];
				if (dev) dev->select((uint8_t)val,1);
			}
			else if (dev) {
				dev->select((uint8_t)val,0);
			}

			ide->check_device_irq();
			break;
		case 7: /* 1F7 */
			if (dev) dev->writecommand((uint8_t)val);
			break;
	}
}

void IDE_RefreshCDROMs()
{
	for (Bit8u i = 0; i != MAX_IDE_CONTROLLERS*2; i++)
	{
		IDEController* c = idecontroller[i>>1];
		if (!c) continue;

		IDEATAPICDROMDevice* d = (IDEATAPICDROMDevice*)c->device[i&1];
		if (!d || d->type != IDE_TYPE_CDROM) continue;

		DOS_Drive* drive = Drives[i+2];
		CDROM_Interface* cdrom = (drive && dynamic_cast<isoDrive*>(drive) ? ((isoDrive*)drive)->GetInterface() : NULL);
		if (cdrom == d->my_cdrom) continue;

		d->my_cdrom = cdrom;
		d->has_changed = true;
		if (!cdrom)
		{
			// Set drive to ejected state
			d->loading_mode = IDEATAPICDROMDevice::LOAD_NO_DISC;
			d->atapi_add_pic_event(NULL, 0);
		}
		else
		{
			// Do ATAPI Media Change Notify (CD insertion is an additional artificial delay between ejected and spin up)
#ifdef C_DBP_ENABLE_IDE_CDINSERTION_DELAY
			const float atapi_cd_insertion_time = 4000; // a quick user that can switch CDs in 4 seconds
			d->loading_mode = IDEATAPICDROMDevice::LOAD_INSERT_CD;
			PIC_AddEvent(IDEATAPICDROMDevice::IDE_ATAPI_CDInsertion, atapi_cd_insertion_time/*ms*/, d->device_index);
#else
			d->loading_mode = IDEATAPICDROMDevice::LOAD_DISC_LOADING;
			d->atapi_add_pic_event(IDEATAPICDROMDevice::IDE_ATAPI_SpinUpComplete, atapi_spinup_time/*ms*/);
#endif
		}
	}
}

void IDE_SetupControllers(char force_cd_drive_letter)
{
	DBP_ASSERT(!idecontroller[0] && !idecontroller[1]);

	for (Bit8u i = 0; i != MAX_IDE_CONTROLLERS; i++)
		idecontroller[i] = new IDEController(i);

	for (Bit8u i = 0; i != MAX_IDE_CONTROLLERS*2; i++)
	{
		IDEController* c = idecontroller[i>>1];
		#ifdef C_DBP_ENABLE_IDE_ATA
		if (i < MAX_HDD_IMAGES && imageDiskList[i+2])
			c->device[i&1] = new IDEATADevice(c, i, i+2);
		else
		#endif
		if ((Drives[i+2] && dynamic_cast<isoDrive*>(Drives[i+2])) || (force_cd_drive_letter-'A') == (i+2))
			c->device[i&1] = new IDEATAPICDROMDevice(c, i);
	}

	IDE_RefreshCDROMs();
}

void IDE_ShutdownControllers(void)
{
	for (IDEController*& c : idecontroller)
		if (c) { delete c; c = NULL; }
}

#include <dbp_serialize.h>
DBP_SERIALIZE_SET_POINTER_LIST(PIC_EventHandler, IDEController, IDE_DelayedCommand, IDEATAPICDROMDevice::IDE_ATAPI_SpinDown, IDEATAPICDROMDevice::IDE_ATAPI_SpinUpComplete);
#ifdef C_DBP_ENABLE_IDE_CDINSERTION_DELAY
#error make sure IDEATAPICDROMDevice::IDE_ATAPI_CDInsertion is added to the list above and loading compatibility versioning is added to DBPSerialize_PIC
#endif

#ifdef C_DBP_ENABLE_IDE_ATA // Disabled ATA drive support because BIOS access covers most cases
#include "bios_disk.h"

static inline bool is_power_of_2(Bitu val) {
	return (val != 0) && ((val&(val-1)) == 0);
}

struct IDEATADevice : public IDEDevice {
	Bitu multiple_sector_max,multiple_sector_count;
	Bitu heads,sects,cyls,headshr,progress_count;
	Bitu phys_heads,phys_sects,phys_cyls;
	Bitu sector_i,sector_total;
	unsigned char sector[512 * 128];
	Bit8u bios_disk_index;
	bool geo_translate;

	IDEATADevice(IDEController *c, Bit8u device_index, Bit8u bios_index)
		: IDEDevice(c, device_index, IDE_TYPE_HDD) {
		DBP_ASSERT(bios_index < MAX_DISK_IMAGES);
		bios_disk_index = bios_index;
		sector_i = sector_total = 0;
		headshr = 0;
		memset(sector, 0, sizeof(sector));
		multiple_sector_max = sizeof(sector) / 512;
		multiple_sector_count = 1;
		geo_translate = false;
		heads = 0;
		sects = 0;
		cyls = 0;
		progress_count = 0;
		phys_heads = 0;
		phys_sects = 0;
		phys_cyls = 0;
		update_from_biosdisk();
	}

	inline imageDisk *getBIOSdisk() {
		return imageDiskList[bios_disk_index];
	}

	virtual void writecommand(uint8_t cmd) {
		if (!command_interruption_ok(cmd))
			return;

		/* if the drive is asleep, then writing a command wakes it up */
		interface_wakeup();

		/* FIXME: OAKCDROM.SYS is sending the hard disk command 0xA0 (ATAPI packet) for some reason. Why? */

		/* drive is ready to accept command */
		allow_writing = false;
		command = cmd;
		switch (cmd) {
			case 0x00: /* NOP */
				feature = 0x04;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_ERROR;
				raise_irq();
				allow_writing = true;
				break;
			case 0x08: /* DEVICE RESET */
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				drivehead &= 0x10;
				count = 0x01; lba[0] = 0x01; feature = 0x00;
				lba[1] = lba[2] = 0;
				/* NTS: Testing suggests that ATA hard drives DO fire an IRQ at this stage.
						In fact, Windows 95 won't detect hard drives that don't fire an IRQ in desponse */
				raise_irq();
				allow_writing = true;
				break;
			case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: /* RECALIBRATE (1xh) */
			case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				/* "if the command is executed in CHS mode, then ... sector number register shall be 1.
				 *  if executed in LAB mode, then ... sector number register shall be 0" */
				if (drivehead_is_lba(drivehead)) lba[0] = 0x00;
				else lba[0] = 0x01;
				drivehead &= 0x10;
				lba[1] = lba[2] = 0;
				feature = 0x00;
				raise_irq();
				allow_writing = true;
				break;
			case 0x20: /* READ SECTOR */
				progress_count = 0;
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(0.1f)/*ms*/,device_index);
				break;
			case 0x30: /* WRITE SECTOR */
				/* the drive does NOT signal an interrupt. it sets DRQ and waits for a sector
				 * to be transferred to it before executing the command */
				progress_count = 0;
				state = IDE_DEV_DATA_WRITE;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ;
				prepare_write(0,512);
				break;
			case 0x40: /* READ SECTOR VERIFY WITH RETRY */
			case 0x41: /* READ SECTOR VERIFY WITHOUT RETRY */
				progress_count = 0;
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(0.1f)/*ms*/,device_index);
				break;
			case 0x91: /* INITIALIZE DEVICE PARAMETERS */
				if ((unsigned int)count != (unsigned int)sects || (unsigned int)((drivehead&0xF)+1) != (unsigned int)heads) {
					if (count == 0) {
						LOG_MSG("IDE warning: OS attempted to change geometry to invalid H/S %u/%u",
							count,(drivehead&0xF)+1);
						abort_error();
						allow_writing = true;
						return;
					}
					else {
						unsigned int ncyls;

						ncyls = (unsigned int)(phys_cyls * phys_heads * phys_sects);
						ncyls += (count * ((unsigned int)(drivehead&0xF)+1u)) - 1u;
						ncyls /= count * ((unsigned int)(drivehead&0xF)+1u);

						/* the OS is changing logical disk geometry, so update our head/sector count (needed for Windows ME) */
						LOG_MSG("IDE warning: OS is changing logical geometry from C/H/S %u/%u/%u to logical H/S %u/%u/%u",
							(int)cyls,(int)heads,(int)sects,
							(int)ncyls,(int)((drivehead&0xF)+1),(int)count);
						LOG_MSG("             Compatibility issues may occur if the OS tries to use INT 13 at the same time!");

						cyls = ncyls;
						sects = count;
						heads = (drivehead&0xFu)+1u;
					}
				}

				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				allow_writing = true;
				raise_irq(); // NTS: The Linux kernel will pause for up to 30 seconds waiting for this command to issue an IRQ if we don't do this
				break;
			case 0xC4: /* READ MULTIPLE */
				progress_count = 0;
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(0.1f)/*ms*/,device_index);
				break;
			case 0xC5: /* WRITE MULTIPLE */
				/* the drive does NOT signal an interrupt. it sets DRQ and waits for a sector
				 * to be transferred to it before executing the command */
				progress_count = 0;
				state = IDE_DEV_DATA_WRITE;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRQ;
				prepare_write(0UL,512UL*IDEMIN((unsigned long)multiple_sector_count,(unsigned long)(count == 0 ? 256 : count)));
				break;
			case 0xC6: /* SET MULTIPLE MODE */
				/* only sector counts 1, 2, 4, 8, 16, 32, 64, and 128 are legal by standard.
				 * NTS: There's a bug in VirtualBox that makes 0 legal too! */
				if (count != 0 && count <= multiple_sector_max && is_power_of_2(count)) {
					multiple_sector_count = count;
					status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				}
				else {
					feature = 0x04; /* abort error */
					abort_error();
				}
				raise_irq();
				allow_writing = true;
				break;
			case 0xA0:/*ATAPI PACKET*/
				/* We're not an ATAPI packet device!
				 * Windows 95 seems to issue this at startup to hard drives. Duh. */
				/* fall through */
			case 0xA1: /* IDENTIFY PACKET DEVICE */
				/* We are not an ATAPI packet device.
				 * Most MS-DOS drivers and Windows 95 like to issue both IDENTIFY ATA and IDENTIFY ATAPI commands.
				 * I also gather from some contributors on the github comments that people think our "Unknown IDE/ATA command"
				 * error message is part of some other error in the emulation. Rather than put up with that, we'll just
				 * silently abort the command with an error. */
				abort_normal();
				status = IDE_STATUS_ERROR|IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE|0x20/*write fault*/;
				drivehead &= 0x30;
				count = 0x01;
				lba[0] = 0x01;
				feature = 0x04; /* abort */
				lba[1] = 0x00;
				lba[2] = 0x00;
				raise_irq();
				allow_writing = true;
				break;
			case 0xE7: /* FLUSH CACHE */
				/* NTS: Windows 2000 and Windows XP like this command a lot. They REALLY REALLY like
				 *      to issue this command a lot, especially during the install phase. This is
				 *      here to avoid filling your log file with many repetitions of
				 *      "Unknown IDE/ATA command E7" */
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				state = IDE_DEV_READY;
				allow_writing = true;
				raise_irq();
				break;
			case 0xEC: /* IDENTIFY DEVICE */
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,(ide_identify_command_delay),device_index);
				break;
			case 0xEF: /* SET FEATURES */
				if (feature == 0x66/*Disable reverting to power on defaults*/ ||
					feature == 0xCC/*Enable reverting to power on defaults*/) {
					/* ignore */
					status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					state = IDE_DEV_READY;
				}
				else {
					LOG_MSG("SET FEATURES %02xh SC=%02x SN=%02x CL=%02x CH=%02x",feature,count,lba[0],lba[1],lba[2]);
					abort_error();
				}
				allow_writing = true;
				raise_irq();
				break;
			default:
				LOG_MSG("Unknown IDE/ATA command %02X",cmd);
				abort_error();
				allow_writing = true;
				raise_irq();
				break;
		}
	}

	virtual Bitu data_read(Bitu iolen) { /* read from 1F0h data port from IDE device */
		Bitu w = ~0u;

		if (state != IDE_DEV_DATA_READ)
			return 0xFFFFUL;

		if (!(status & IDE_STATUS_DRQ)) {
			LOG_MSG("IDE: Data read when DRQ=0");
			return 0xFFFFUL;
		}

		if ((sector_i+iolen) > sector_total) {
			LOG_MSG("ide ata warning: sector already read %lu / %lu",(unsigned long)sector_i,(unsigned long)sector_total);
			return 0xFFFFUL;
		}

		if (iolen >= 4) {
			w = host_readd(sector+sector_i);
			sector_i += 4;
		}
		else if (iolen >= 2) {
			w = host_readw(sector+sector_i);
			sector_i += 2;
		}
		/* NTS: Some MS-DOS CD-ROM drivers like OAKCDROM.SYS use byte-wide I/O for the initial identification */
		else if (iolen == 1) {
			w = sector[sector_i++];
		}

		if (sector_i >= sector_total)
			io_completion();

		return w;
	}

	virtual void data_write(Bitu v,Bitu iolen) { /* write to 1F0h data port to IDE device */
		if (state != IDE_DEV_DATA_WRITE) {
			LOG_MSG("ide ata warning: data write when device not in data_write state");
			return;
		}
		if (!(status & IDE_STATUS_DRQ)) {
			LOG_MSG("ide ata warning: data write with drq=0");
			return;
		}
		if ((sector_i+iolen) > sector_total) {
			LOG_MSG("ide ata warning: sector already full %lu / %lu",(unsigned long)sector_i,(unsigned long)sector_total);
			return;
		}

		if (iolen >= 4) {
			host_writed(sector+sector_i,(Bit32u)v);
			sector_i += 4;
		}
		else if (iolen >= 2) {
			host_writew(sector+sector_i,(Bit32u)v);
			sector_i += 2;
		}
		else if (iolen == 1) {
			sector[sector_i++] = (unsigned char)v;
		}

		if (sector_i >= sector_total)
			io_completion();
	}

	void update_from_biosdisk() {
		imageDisk *dsk = getBIOSdisk();
		if (dsk == NULL) {
			LOG_MSG("WARNING: IDE update from BIOS disk failed, disk not available");
			return;
		}

		headshr = 0;
		geo_translate = false;
		cyls = dsk->cylinders;
		heads = dsk->heads;
		sects = dsk->sectors;

		/* One additional correction: The disk image is probably using BIOS-style geometry
		   translation (such as C/H/S 1024/64/63) which is impossible given that the IDE
		   standard only allows up to 16 heads. So we have to translate the geometry. */
		while (heads > 16 && (heads & 1) == 0) {
			cyls <<= 1U;
			heads >>= 1U;
			headshr++;
		}

		/* If we can't divide the heads down, then pick a LBA-like mapping that is good enough.
		 * Note that if what we pick does not evenly map to the INT 13h geometry, and the partition
		 * contained within is not an LBA type FAT16/FAT32 partition, then Windows 95's IDE driver
		 * will ignore this device and fall back to using INT 13h. For user convenience we will
		 * print a warning to reminder the user of exactly that. */
		if (heads > 16) {
			unsigned long tmp;

			geo_translate = true;

			tmp = (unsigned long)(heads * cyls * sects);
			sects = 63;
			heads = 16;
			cyls = (tmp + ((63 * 16) - 1)) / (63 * 16);
			LOG_MSG("WARNING: Unable to reduce heads to 16 and below");
			LOG_MSG("If at all possible, please consider using INT 13h geometry with a head");
			LOG_MSG("count that is easier to map to the BIOS, like 240 heads or 128 heads/track.");
			LOG_MSG("Some OSes, such as Windows 95, will not enable their 32-bit IDE driver if");
			LOG_MSG("a clean mapping does not exist between IDE and BIOS geometry.");
			LOG_MSG("Mapping BIOS DISK C/H/S %u/%u/%u as IDE %u/%u/%u (non-straightforward mapping)",
				(unsigned int)dsk->cylinders,
				(unsigned int)dsk->heads,
				(unsigned int)dsk->sectors,
				(unsigned int)cyls,
				(unsigned int)heads,
				(unsigned int)sects);
		}
		else {
			LOG_MSG("Mapping BIOS DISK C/H/S %u/%u/%u as IDE %u/%u/%u",
				(unsigned int)dsk->cylinders,
				(unsigned int)dsk->heads,
				(unsigned int)dsk->sectors,
				(unsigned int)cyls,
				(unsigned int)heads,
				(unsigned int)sects);
		}

		phys_heads = heads;
		phys_sects = sects;
		phys_cyls = cyls;
	}

	void generate_identify_device() {
		//imageDisk *disk = getBIOSdisk();
		unsigned char csum;
		uint64_t ptotal;
		uint64_t total;
		Bitu i;

		/* IN RESPONSE TO IDENTIFY DEVICE (0xEC)
		   GENERATE 512-BYTE REPLY */
		memset(sector,0,512);

		/* total disk capacity in sectors */
		total = sects * cyls * heads;
		ptotal = phys_sects * phys_cyls * phys_heads;

		host_writew(sector+(0*2),0x0040);   /* bit 6: 1=fixed disk */
		host_writew(sector+(1*2),(Bit16u)phys_cyls);
		host_writew(sector+(3*2),(Bit16u)phys_heads);
		host_writew(sector+(4*2),(Bit16u)phys_sects * 512); /* unformatted bytes per track */
		host_writew(sector+(5*2),512);      /* unformatted bytes per sector */
		host_writew(sector+(6*2),(Bit16u)phys_sects);

		host_writew(sector+(20*2),1);       /* ATA-1: single-ported single sector buffer */
		host_writew(sector+(21*2),4);       /* ATA-1: ECC bytes on read/write long */

		// These strings are encoded with a 2 byte invert pattern (1234 becomes 2143)
		memcpy(sector+(10*2), "0868",                4); memset(sector+(10*2)+ 4, ' ',  20- 4); //id_serial "8086", 20 bytes, padded with space
		memcpy(sector+(23*2), "0868",                4); memset(sector+(23*2)+ 4, ' ',   8- 4); //id_firmware_rev "8086", 8 bytes, padded with space
		memcpy(sector+(27*2), "ODBSxoX-I EDd si k", 18); memset(sector+(27*2)+18, ' ',  40-18); //id_model "DOSBox-X IDE disk", 40 bytes, padded with space

		if (multiple_sector_max != 0)
			host_writew(sector+(47*2),(Bit16u)(0x80|multiple_sector_max)); /* <- READ/WRITE MULTIPLE MAX SECTORS */

		host_writew(sector+(48*2),0x0000);  /* :0  0=we do not support doubleword (32-bit) PIO */
		host_writew(sector+(49*2),0x0A00);  /* :13 0=Standby timer values managed by device */
							/* :11 1=IORDY supported */
							/* :10 0=IORDY not disabled */
							/* :9  1=LBA supported */
							/* :8  0=DMA not supported */
		host_writew(sector+(50*2),0x4000);  /* FIXME: ??? */
		host_writew(sector+(51*2),0x00F0);  /* PIO data transfer cycle timing mode */
		host_writew(sector+(52*2),0x00F0);  /* DMA data transfer cycle timing mode */
		host_writew(sector+(53*2),0x0007);  /* :2  1=the fields in word 88 are valid */
							/* :1  1=the fields in word (70:64) are valid */
							/* :0  1= ??? */
		host_writew(sector+(54*2),(Bit16u)cyls);    /* current cylinders */
		host_writew(sector+(55*2),(Bit16u)heads);   /* current heads */
		host_writew(sector+(56*2),(Bit16u)sects);   /* current sectors per track */
		host_writed(sector+(57*2),(Bit32u)total);   /* current capacity in sectors */

		if (multiple_sector_count != 0)
			host_writew(sector+(59*2),(Bit16u)(0x0100|multiple_sector_count)); /* :8  multiple sector setting is valid */
							/* 7:0 current setting for number of log. sectors per DRQ of READ/WRITE MULTIPLE */

		host_writed(sector+(60*2),(Bit32u)ptotal);  /* total user addressable sectors (LBA) */
		host_writew(sector+(62*2),0x0000);  /* FIXME: ??? */
		host_writew(sector+(63*2),0x0000);  /* :10 0=Multiword DMA mode 2 not selected */
							/* TODO: Basically, we don't do DMA. Fill out this comment */
		host_writew(sector+(64*2),0x0003);  /* 7:0 PIO modes supported (FIXME ???) */
		host_writew(sector+(65*2),0x0000);  /* FIXME: ??? */
		host_writew(sector+(66*2),0x0000);  /* FIXME: ??? */
		host_writew(sector+(67*2),0x0078);  /* FIXME: ??? */
		host_writew(sector+(68*2),0x0078);  /* FIXME: ??? */
		host_writew(sector+(80*2),0x007E);  /* major version number. Here we say we support ATA-1 through ATA-8 */
		host_writew(sector+(81*2),0x0022);  /* minor version */
		host_writew(sector+(82*2),0x4208);  /* command set: NOP, DEVICE RESET[XXXXX], POWER MANAGEMENT */
		host_writew(sector+(83*2),0x4000);  /* command set: LBA48[XXXX] */
		host_writew(sector+(84*2),0x4000);  /* FIXME: ??? */
		host_writew(sector+(85*2),0x4208);  /* commands in 82 enabled */
		host_writew(sector+(86*2),0x4000);  /* commands in 83 enabled */
		host_writew(sector+(87*2),0x4000);  /* FIXME: ??? */
		host_writew(sector+(88*2),0x0000);  /* FIXME: ??? */
		host_writew(sector+(93*3),0x0000);  /* FIXME: ??? */

		/* ATA-8 integrity checksum */
		sector[510] = 0xA5;
		csum = 0; for (i=0;i < 511;i++) csum += sector[i];
		sector[511] = 0 - csum;
	}

	void prepare_read(Bitu offset,Bitu size) {
		/* I/O must be WORD ALIGNED */
		DBP_ASSERT((offset&1) == 0);
		//assert((size&1) == 0);

		sector_i = offset;
		sector_total = size;
		DBP_ASSERT(sector_i <= sector_total);
		DBP_ASSERT(sector_total <= sizeof(sector));
	}

	void prepare_write(Bitu offset,Bitu size) {
		/* I/O must be WORD ALIGNED */
		DBP_ASSERT((offset&1) == 0);
		//assert((size&1) == 0);

		sector_i = offset;
		sector_total = size;
		DBP_ASSERT(sector_i <= sector_total);
		DBP_ASSERT(sector_total <= sizeof(sector));
	}

	void io_completion() {
		/* lower DRQ */
		status &= ~IDE_STATUS_DRQ;

		/* depending on the command, either continue it or finish up */
		switch (command) {
			case 0x20:/* READ SECTOR */
				/* OK, decrement count, increment address */
				/* NTS: Remember that count == 0 means the host wanted to transfer 256 sectors */
				progress_count++;
				if ((count&0xFF) == 1) {
					/* end of the transfer */
					count = 0;
					status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
					state = IDE_DEV_READY;
					allow_writing = true;
					return;
				}
				else if ((count&0xFF) == 0) count = 255;
				else count--;

				if (!increment_current_address()) {
					LOG_MSG("READ advance error");
					abort_error();
					return;
				}

				/* cause another delay, another sector read */
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,0.00001f/*ms*/,device_index);
				break;
			case 0x30:/* WRITE SECTOR */
				/* this is where the drive has accepted the sector, lowers DRQ, and begins executing the command */
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,((progress_count == 0) ? 0.1f : 0.00001f)/*ms*/,device_index);
				break;
			case 0xC4:/* READ MULTIPLE */
				/* OK, decrement count, increment address */
				/* NTS: Remember that count == 0 means the host wanted to transfer 256 sectors */
				for (unsigned int cc=0;cc < multiple_sector_count;cc++) {
					progress_count++;
					if ((count&0xFF) == 1) {
						/* end of the transfer */
						count = 0;
						status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
						state = IDE_DEV_READY;
						allow_writing = true;
						return;
					}
					else if ((count&0xFF) == 0) count = 255;
					else count--;

					if (!increment_current_address()) {
						LOG_MSG("READ advance error");
						abort_error();
						return;
					}
				}

				/* cause another delay, another sector read */
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,0.00001f/*ms*/,device_index);
				break;
			case 0xC5:/* WRITE MULTIPLE */
				/* this is where the drive has accepted the sector, lowers DRQ, and begins executing the command */
				state = IDE_DEV_BUSY;
				status = IDE_STATUS_BUSY;
				PIC_RemoveSpecificEvents(IDE_DelayedCommand,device_index);
				PIC_AddEvent(IDE_DelayedCommand,((progress_count == 0) ? 0.1f : 0.00001f)/*ms*/,device_index);
				break;
			default: /* most commands: signal drive ready, return to ready state */
				/* NTS: Some MS-DOS CD-ROM drivers will loop endlessly if we never set "drive seek complete"
						because they like to hit the device with DEVICE RESET (08h) whether or not it's
					a hard disk or CD-ROM drive */
				count = 0;
				drivehead &= 0xF0;
				lba[0] = 0;
				lba[1] = lba[2] = 0;
				status = IDE_STATUS_DRIVE_READY|IDE_STATUS_DRIVE_SEEK_COMPLETE;
				state = IDE_DEV_READY;
				allow_writing = true;
				break;
		}
	}

	bool increment_current_address(Bitu count=1) {
		if (count == 0) return false;

		if (drivehead_is_lba(drivehead)) {
			/* 28-bit LBA:
			 *    drivehead: 27:24
			 *    lba[2]:    23:16
			 *    lba[1]:    15:8
			 *    lba[0]:    7:0 */
			do {
				if (((++lba[0])&0xFF) == 0x00) {
					lba[0] = 0x00;
					if (((++lba[1])&0xFF) == 0x00) {
						lba[1] = 0x00;
						if (((++lba[2])&0xFF) == 0x00) {
							lba[2] = 0x00;
							if (((++drivehead)&0xF) == 0) {
								drivehead -= 0x10;
								return false;
							}
						}
					}
				}
			} while ((--count) != 0);
		}
		else {
			/* C/H/S increment with rollover */
			do {
				/* increment sector */
				if (((++lba[0])&0xFF) == ((sects+1)&0xFF)) {
					lba[0] = 1;
					/* increment head */
					if (((++drivehead)&0xF) == (heads&0xF)) {
						drivehead &= 0xF0;
						if (heads == 16) drivehead -= 0x10;
						/* increment cylinder */
						if (((++lba[1])&0xFF) == 0x00) {
							if (((++lba[2])&0xFF) == 0x00) {
								return false;
							}
						}
					}

				}
			} while ((--count) != 0);
		}

		return true;
	}
};
#endif //C_DBP_ENABLE_IDE_ATA

#endif //C_DBP_ENABLE_IDE
