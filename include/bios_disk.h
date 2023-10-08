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

#ifndef DOSBOX_BIOS_DISK_H
#define DOSBOX_BIOS_DISK_H

#include <stdio.h>
#ifndef DOSBOX_MEM_H
#include "mem.h"
#endif
#ifndef DOSBOX_DOS_INC_H
#include "dos_inc.h"
#endif
#ifndef DOSBOX_BIOS_H
#include "bios.h"
#endif

/* The Section handling Bios Disk Access */
#define BIOS_MAX_DISK 10

#ifdef C_DBP_ENABLE_DISKSWAP
#define MAX_SWAPPABLE_DISKS 20
#endif
struct diskGeo {
	Bit32u ksize;  /* Size in kilobytes */
	Bit16u secttrack; /* Sectors per track */
	Bit16u headscyl;  /* Heads per cylinder */
	Bit16u cylcount;  /* Cylinders per side */
	Bit16u biosval;   /* Type to return from BIOS */
};
extern diskGeo DiskGeometryList[];

#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
template <typename TVal> struct StringToPointerHashMap;
#endif

class imageDisk  {
public:
	Bit8u Read_Sector(Bit32u head,Bit32u cylinder,Bit32u sector,void * data);
	Bit8u Write_Sector(Bit32u head,Bit32u cylinder,Bit32u sector,void * data);
	Bit8u Read_AbsoluteSector(Bit32u sectnum, void * data);
	Bit8u Write_AbsoluteSector(Bit32u sectnum, void * data);

	void Set_Geometry(Bit32u setHeads, Bit32u setCyl, Bit32u setSect, Bit32u setSectSize);
	void Get_Geometry(Bit32u * getHeads, Bit32u *getCyl, Bit32u *getSect, Bit32u *getSectSize);
	Bit8u GetBiosType(void);
	Bit32u getSectSize(void);
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	imageDisk(class DOS_File *imgFile, const char *imgName, Bit32u imgSizeK, bool isHardDisk);
	~imageDisk();
	Bit32u Read_Raw(Bit8u *buffer, Bit32u seek, Bit32u len);
	void SetDifferencingDisk(const char* savePath);
	#else
	imageDisk(FILE *imgFile, const char *imgName, Bit32u imgSizeK, bool isHardDisk);
	~imageDisk() { if(diskimg != NULL) { fclose(diskimg); }	};
	#endif
	#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
	imageDisk(class DOS_Drive *useDrive, Bit32u freeSpaceMB = 0, const char* savePath = NULL, Bit32u driveSerial = 0, const StringToPointerHashMap<void>* fileFilter = NULL);
	void Set_GeometryForHardDisk();
	#endif

	bool hardDrive;
	bool active;
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	class DOS_File* dos_file;
	#else
	FILE *diskimg;
	#endif
	char diskname[512];
	Bit8u floppytype;

	Bit32u sector_size;
	Bit32u heads,cylinders,sectors;
private:
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	Bit64u current_fpos;
	#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
	struct fatFromDOSDrive* ffdd = NULL;
	#endif
	struct discardDisk* discard = NULL;
	struct differencingDisk* differencing = NULL;
	#else
	Bit32u current_fpos;
	#endif
	enum { NONE,READ,WRITE } last_action;
};

void updateDPT(void);
void incrementFDD(void);

//DBP: Increased from 2 to 4
#define MAX_HDD_IMAGES 4

#define MAX_DISK_IMAGES (2 + MAX_HDD_IMAGES)

extern imageDisk *imageDiskList[MAX_DISK_IMAGES];
#ifdef C_DBP_ENABLE_DISKSWAP
extern imageDisk *diskSwap[MAX_SWAPPABLE_DISKS];
extern Bit32s swapPosition;
#endif
extern Bit16u imgDTASeg; /* Real memory location of temporary DTA pointer for fat image disk access */
extern RealPt imgDTAPtr; /* Real memory location of temporary DTA pointer for fat image disk access */
extern DOS_DTA *imgDTA;

#ifdef C_DBP_ENABLE_DISKSWAP
void swapInDisks(void);
void swapInNextDisk(void);
#endif
bool getSwapRequest(void);

#endif
