/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *  Copyright (C) 2022-2023  Bernhard Schelling
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


#include "dosbox.h"
#include "callback.h"
#include "bios.h"
#include "bios_disk.h"
#include "regs.h"
#include "mem.h"
#include "dos_inc.h" /* for Drives[] */
#include "../dos/drives.h"
#include "mapper.h"

//DBP: for mem_readb_inline and mem_writeb_inline
#include "paging.h"

#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
struct discardDisk
{
	std::vector<Bit8u*> tempwrites;
	
	~discardDisk()
	{
		for (Bit8u* p : tempwrites)
			delete p;
	}

	bool Read_AbsoluteSector(Bit32u sectnum, void* data, Bit32u sector_size)
	{
		const Bit8u* tempwrite = (tempwrites.size() <= sectnum ? NULL : tempwrites[sectnum]);
		if (!tempwrite) return false;
		memcpy(data, tempwrite, sector_size);
		return true;
	}

	void Write_AbsoluteSector(Bit32u sectnum, const void* data, Bit32u sector_size)
	{
		if (tempwrites.size() <= sectnum)
			tempwrites.resize(sectnum + 1);

		Bit8u*& tempwrite = tempwrites[sectnum];
		if (!tempwrite)
			tempwrite = (Bit8u*)malloc(sector_size);
		memcpy(tempwrite, data, sector_size);
	}
};

struct differencingDisk
{
	enum ddDefs : Bit32u
	{
		BYTESPERSECTOR    = 512,
		NULL_CURSOR       = (Bit32u)-1,
	};

	struct ffddBuf { Bit8u data[BYTESPERSECTOR]; };
	struct ffddSec { Bit32u cursor = NULL_CURSOR; };
	std::vector<ffddBuf>  diffSectorBufs;
	std::vector<ffddSec>  diffSectors;
	std::vector<Bit32u>   diffFreeCursors;
	std::string           savePath;
	FILE*                 saveFile = NULL;
	Bit32u                saveEndCursor = 0;

	~differencingDisk()
	{
		if (saveFile)
			fclose(saveFile);
	}

	void SetupSave(const char* inSavePath, Bit32u sect_disk_end)
	{
		DBP_ASSERT(inSavePath && *inSavePath);
		if (FILE* f = fopen_wrap(inSavePath, "rb+"))
		{
			saveFile = f;
			char fhead[5];
			if (!fread(fhead, sizeof(fhead), 1, f) || memcmp(fhead, "FFDD\x1", sizeof(fhead))) goto invalid_file;
			Bit32u cursor = sizeof(fhead);
			for (Bit32u sectnumval; fread(&sectnumval, sizeof(sectnumval), 1, f); cursor += (Bit32u)(sizeof(sectnumval) + BYTESPERSECTOR))
			{
				fseek(f, BYTESPERSECTOR, SEEK_CUR);
				if (sectnumval != 0xFFFFFFFF)
				{
					Bit32u sectnum = var_read(&sectnumval);
					if (sectnum >= sect_disk_end) goto invalid_file;
					if (sectnum >= diffSectors.size()) diffSectors.resize(sectnum + 1);
					diffSectors[sectnum].cursor = cursor;
				}
				else diffFreeCursors.push_back(cursor);
			}
			saveEndCursor = cursor;
		}
		else if (0)
		{
			invalid_file:
			LOG_MSG("[DOSBOX] Invalid disk save file %s", inSavePath);
			fclose(f);
			saveFile = NULL;
		}
		else
		{
			savePath = inSavePath; // remember until needed
		}
	}

	bool WriteDiff(Bit32u sectnum, const void* data, const void* unmodified)
	{
		if (sectnum >= diffSectors.size()) diffSectors.resize(sectnum + 128);
		Bit32u *cursor_ptr = &diffSectors[sectnum].cursor, cursor_val = *cursor_ptr;

		int is_different;
		if (!unmodified)
		{
			is_different = false; // to be equal it must be filled with zeroes
			for (Bit64u* p = (Bit64u*)data, *pEnd = p + (BYTESPERSECTOR / sizeof(Bit64u)); p != pEnd; p++)
				if (*p) { is_different = true; break; }
		}
		else is_different = memcmp(unmodified, data, BYTESPERSECTOR);

		if (is_different)
		{
			if (!saveFile && !savePath.empty())
			{
				saveFile = fopen(savePath.c_str(), "wb+");
				if (saveFile) { fwrite("FFDD\x1", 5, 1, saveFile); saveEndCursor = 5; };
				savePath.clear();
			}
			const bool reuseFree = (cursor_val == NULL_CURSOR && diffFreeCursors.size());
			if (reuseFree)
			{
				*cursor_ptr = cursor_val = diffFreeCursors.back();
				diffFreeCursors.pop_back();
			}
			if (saveFile)
			{
				if (cursor_val == NULL_CURSOR)
				{
					*cursor_ptr = cursor_val = saveEndCursor;
					saveEndCursor += sizeof(sectnum) + BYTESPERSECTOR;
					writeSectNum:
					Bit32u sectnumval;
					var_write(&sectnumval, sectnum);
					fseek_wrap(saveFile, cursor_val, SEEK_SET);
					fwrite(&sectnumval, sizeof(sectnumval), 1, saveFile);
				}
				else if (reuseFree)
					goto writeSectNum;
				else
					fseek_wrap(saveFile, cursor_val + sizeof(sectnum), SEEK_SET);
				fwrite(data, BYTESPERSECTOR, 1, saveFile);
			}
			else
			{
				if (cursor_val == NULL_CURSOR)
				{
					*cursor_ptr = cursor_val = (Bit32u)diffSectorBufs.size();
					diffSectorBufs.resize(cursor_val + 1);
				}
				memcpy(diffSectorBufs[cursor_val].data, data, BYTESPERSECTOR);
			}
			return true;
		}
		else if (cursor_val != NULL_CURSOR)
		{
			if (saveFile)
			{
				// mark sector in diff file as free
				Bit32u sectnumval = 0xFFFFFFFF;
				fseek_wrap(saveFile, cursor_val, SEEK_SET);
				fwrite(&sectnumval, sizeof(sectnumval), 1, saveFile);
			}
			diffFreeCursors.push_back(cursor_val);
			*cursor_ptr = NULL_CURSOR;
			return true;
		}
		return false;
	}

	bool GetDiff(Bit32u sectnum, void* data)
	{
		Bit32u cursor = (sectnum >= diffSectors.size() ? NULL_CURSOR : diffSectors[sectnum].cursor);
		if (cursor == NULL_CURSOR) return false;
		if (saveFile)
		{
			fseek_wrap(saveFile, cursor + sizeof(sectnum), SEEK_SET);
			return !!fread(data, BYTESPERSECTOR, 1, saveFile);
		}
		memcpy(data, diffSectorBufs[cursor].data, BYTESPERSECTOR);
		return true;
	}
};

#ifdef _MSC_VER
#pragma pack (1)
#endif
struct lfndirentry {
	Bit8u ord;
	Bit8u name1[10];
	Bit8u attrib;
	Bit8u type;
	Bit8u chksum;
	Bit8u name2[12];
	Bit16u loFirstClust;
	Bit8u name3[4];
	char* Name(int j) { return (char*)(j < 5 ? name1 + j*2 : j < 11 ? name2 + (j-5)*2 : name3 + (j-11)*2); }
} GCC_ATTRIBUTE(packed);
#ifdef _MSC_VER
#pragma pack ()
#endif
DBP_STATIC_ASSERT(sizeof(direntry) == sizeof(lfndirentry));
enum
{
	DOS_ATTR_LONG_NAME = (DOS_ATTR_READ_ONLY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM | DOS_ATTR_VOLUME),
	DOS_ATTR_LONG_NAME_MASK = (DOS_ATTR_READ_ONLY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM | DOS_ATTR_VOLUME | DOS_ATTR_DIRECTORY | DOS_ATTR_ARCHIVE),
	DOS_ATTR_PENDING_SHORT_NAME = 0x80,
};

struct fatFromDOSDrive
{
	DOS_Drive* drive;

	enum ffddDefs : Bit32u
	{
		BYTESPERSECTOR    = 512,
		HEADCOUNT         = 240, // needs to be >128 to fit 4GB into CHS
		SECTORSPERTRACK   = 63,
		SECT_MBR          = 0,
		SECT_BOOT         = 32,
		CACHECOUNT        = 256,
		KEEPOPENCOUNT     = 4,
	};

	partTable  mbr;
	bootstrap  bootsec;
	Bit8u      fsinfosec[BYTESPERSECTOR];
	Bit32u     sectorsPerCluster;
	Bit8u      fatSz, readOnly;

	struct ffddFile { char path[DOS_PATHLENGTH+1]; Bit32u firstSect; };
	std::vector<direntry> root, dirs;
	std::vector<ffddFile> files;
	std::vector<Bit32u>   fileAtSector;
	std::vector<Bit8u>    fat;
	Bit32u sect_disk_end, sect_files_end, sect_files_start, sect_dirs_start, sect_root_start, sect_fat2_start, sect_fat1_start;

	Bit8u                 cacheSectorData[CACHECOUNT][BYTESPERSECTOR];
	Bit32u                cacheSectorNumber[CACHECOUNT];
	differencingDisk      difference;
	DOS_File*             openFiles[KEEPOPENCOUNT];
	Bit32u                openIndex[KEEPOPENCOUNT];
	Bit32u                openCursor = 0;

	~fatFromDOSDrive()
	{
		for (DOS_File* df : openFiles)
			if (df) { df->Close(); delete df; }
	}

	fatFromDOSDrive(DOS_Drive* drv, Bit32u freeSpaceMB = 0, const char* inSavePath = NULL, Bit32u serial = 0, const StringToPointerHashMap<void>* fileFilter = NULL) : drive(drv)
	{
		cacheSectorNumber[0] = 1; // must not state that sector 0 is already cached
		memset(&cacheSectorNumber[1], 0, sizeof(cacheSectorNumber) - sizeof(cacheSectorNumber[0]));
		memset(openFiles, 0, sizeof(openFiles));

		struct Iter
		{
			static void SetFAT(fatFromDOSDrive& ffdd, size_t idx, Bit32u val)
			{
				while (idx >= (Bit64u)ffdd.fat.size() * 8 / ffdd.fatSz)
				{
					// FAT12 table grows in steps of 3 sectors otherwise the table doesn't align
					size_t addSz = (ffdd.fatSz != 12 ? BYTESPERSECTOR : (BYTESPERSECTOR * 3));
					ffdd.fat.resize(ffdd.fat.size() + addSz);
					memset(&ffdd.fat[ffdd.fat.size() - addSz], 0, addSz);
				}
				if (ffdd.fatSz == 32) // FAT32
					var_write((Bit32u*)&ffdd.fat[idx * 4], val);
				else if (ffdd.fatSz == 16) // FAT 16
					var_write((Bit16u*)&ffdd.fat[idx * 2], (Bit16u)val);
				else if (idx & 1) // FAT12 odd cluster
					var_write((Bit16u*)&ffdd.fat[idx + idx / 2], (Bit16u)((var_read((Bit16u *)&ffdd.fat[idx + idx / 2]) & 0xF) | ((val & 0xFFF) << 4)));
				else // FAT12 even cluster
					var_write((Bit16u*)&ffdd.fat[idx + idx / 2], (Bit16u)((var_read((Bit16u *)&ffdd.fat[idx + idx / 2]) & 0xF000) | (val & 0xFFF)));
			}

			static direntry* AddDirEntry(fatFromDOSDrive& ffdd, bool useFAT16Root, size_t& diridx)
			{
				const Bit32u entriesPerCluster = ffdd.sectorsPerCluster * BYTESPERSECTOR / sizeof(direntry);
				if (!useFAT16Root && (diridx % entriesPerCluster) == 0)
				{
					// link fat (was set to 0xFFFF before but now we knew the chain continues)
					if (diridx) SetFAT(ffdd, 2 + (diridx - 1) / entriesPerCluster, (Bit32u)(2 + ffdd.dirs.size() / entriesPerCluster));
					diridx = ffdd.dirs.size();
					ffdd.dirs.resize(diridx + entriesPerCluster);
					memset(&ffdd.dirs[diridx], 0, sizeof(direntry) * entriesPerCluster);
					SetFAT(ffdd, 2 + diridx / entriesPerCluster, (Bit32u)0xFFFFFFFF); // set as last cluster in chain for now
				}
				else if (useFAT16Root && diridx && (diridx % 512) == 0)
				{
					// this actually should never be larger than 512 for some FAT16 drivers
					ffdd.root.resize(diridx + 512);
					memset(&ffdd.root[diridx], 0, sizeof(direntry) * 512);
				}
				return &(!useFAT16Root ? ffdd.dirs : ffdd.root)[diridx++];
			}

			static void ParseDir(fatFromDOSDrive& ffdd, char* dir, const StringToPointerHashMap<void>* filter, int dirlen = 0, Bit16u parentFirstCluster = 0)
			{
				const bool useFAT16Root = (!dirlen && ffdd.fatSz != 32), readOnly = ffdd.readOnly;
				const size_t firstidx = (!useFAT16Root ? ffdd.dirs.size() : 0);
				const Bit32u sectorsPerCluster = ffdd.sectorsPerCluster, bytesPerCluster = sectorsPerCluster * BYTESPERSECTOR, entriesPerCluster = bytesPerCluster / sizeof(direntry);
				const Bit16u myFirstCluster = (dirlen ? (Bit16u)(2 + firstidx / entriesPerCluster) : (Bit16u)0) ;

				char finddir[DOS_PATHLENGTH+4];
				memcpy(finddir, dir, dirlen); // because FindFirst can modify this...
				finddir[dirlen] = '\0';
				if (dirlen) dir[dirlen++] = '\\';

				size_t diridx = 0;
				RealPt save_dta = dos.dta();
				dos.dta(dos.tables.tempdta);
				DOS_DTA dta(dos.dta());
				dta.SetupSearch(255, 0xFF, (char*)"*.*");
				for (bool more = ffdd.drive->FindFirst(finddir, dta); more; more = ffdd.drive->FindNext(dta))
				{
					char dta_name[DOS_NAMELENGTH_ASCII]; Bit32u dta_size; Bit16u dta_date, dta_time; Bit8u dta_attr;
					dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
					const char *fend = dta_name + strlen(dta_name);
					const bool dot = (dta_name[0] == '.' && dta_name[1] == '\0'), dotdot = (dta_name[0] == '.' && dta_name[1] == '.' && dta_name[2] == '\0');
					if (!dirlen && (dot || dotdot)) continue; // root shouldn't have dot entries (yet localDrive does...)

					ffddFile f;
					memcpy(f.path, dir, dirlen);
					memcpy(f.path + dirlen, dta_name, fend - dta_name + 1);
					if (filter && filter->Get(f.path)) continue;

					char longname[256];
					const bool isLongFileName = (!dot && !dotdot && !(dta_attr & DOS_ATTR_VOLUME) && ffdd.drive->GetLongFileName(f.path, longname));
					if (isLongFileName)
					{
						size_t lfnlen = strlen(longname);
						const char *lfn_end = longname + lfnlen;
						for (size_t i = 0, lfnblocks = (lfnlen + 12) / 13; i != lfnblocks; i++)
						{
							lfndirentry* le = (lfndirentry*)AddDirEntry(ffdd, useFAT16Root, diridx);
							le->ord = (Bit8u)((lfnblocks - i)|(i == 0 ? 0x40 : 0x0));
							le->attrib = DOS_ATTR_LONG_NAME;
							le->type = 0;
							le->loFirstClust = 0;
							const char* plfn = longname + (lfnblocks - i - 1) * 13;
							for (int j = 0; j != 13; j++, plfn++)
							{
								char* p = le->Name(j);
								if (plfn > lfn_end) { p[0] = p[1] = (char)0xFF; }
								else if (plfn == lfn_end) { p[0] = p[1] = 0; }
								else { p[0] = *plfn; p[1] = 0; }
							}
						}
					}

					const char *fext = (dot || dotdot ? NULL : strrchr(dta_name, '.'));
					direntry* e = AddDirEntry(ffdd, useFAT16Root, diridx);
					memset(e->entryname, ' ', sizeof(e->entryname));
					memcpy(e->entryname, dta_name, (fext ? fext : fend) - dta_name);
					if (fext++) memcpy(e->entryname + 8, fext, fend - fext);

					e->attrib = dta_attr | (readOnly ? DOS_ATTR_READ_ONLY : 0) | (isLongFileName ? DOS_ATTR_PENDING_SHORT_NAME : 0);
					//var_write(&e->crtTime,    dta_time); // create date/time is DOS 7.0 and up only
					//var_write(&e->crtDate,    dta_date); // create date/time is DOS 7.0 and up only
					var_write(&e->accessDate, dta_date);
					var_write(&e->modTime,    dta_time);
					var_write(&e->modDate,    dta_date);

					if (dot)
					{
						e->attrib |= DOS_ATTR_DIRECTORY; // make sure
						var_write(&e->loFirstClust, myFirstCluster);
					}
					else if (dotdot)
					{
						e->attrib |= DOS_ATTR_DIRECTORY; // make sure
						var_write(&e->loFirstClust, parentFirstCluster);
					}
					else if (dta_attr & DOS_ATTR_VOLUME)
					{
						DBP_ASSERT(!dirlen && !(e->attrib & DOS_ATTR_DIRECTORY) && !dta_size); // only root can have a volume entry
					}
					else if (!(dta_attr & DOS_ATTR_DIRECTORY))
					{
						var_write(&e->entrysize, dta_size);

						Bit32u fileIdx = (Bit32u)ffdd.files.size();
						ffdd.files.push_back(f);

						Bit32u numSects = (dta_size + bytesPerCluster - 1) / bytesPerCluster * sectorsPerCluster;
						for (Bit32u i = 0; i != numSects; i++) ffdd.fileAtSector.push_back(fileIdx);
					}
				}
				dos.dta(save_dta);
				DBP_ASSERT(!dirlen || diridx >= firstidx + 2); // directories need at least dot and dotdot entries

				// Now fill out the subdirectories (can't be done above because only one dos.dta can run simultaneously 
				std::vector<direntry>& entries = (!useFAT16Root ? ffdd.dirs : ffdd.root);
				for (size_t ei = firstidx; ei != diridx; ei++)
				{
					direntry& e = entries[ei];
					Bit8u* entryname = e.entryname;
					int totlen = dirlen;
					if (e.attrib & DOS_ATTR_DIRECTORY) // copy name before modifying SFN
					{
						if (entryname[0] == '.' && entryname[entryname[1] == '.' ? 2 : 1] == ' ') continue;
						for (int i = 0; i != 8 && entryname[i] != ' '; i++) dir[totlen++] = entryname[i];
						if (entryname[8] != ' ') dir[totlen++] = '.';
						for (int i = 8; i != 11 && entryname[i] != ' '; i++) dir[totlen++] = entryname[i];
					}
					if (e.attrib & DOS_ATTR_PENDING_SHORT_NAME) // convert LFN to SFN
					{
						memset(entryname, ' ', sizeof(e.entryname));
						int ni = 0, niext = 0, lossy = 0;
						for (lfndirentry* le = (lfndirentry*)&e; le-- == (lfndirentry*)&e || !(le[1].ord & 0x40);)
						{
							for (int j = 0; j != 13; j++)
							{
								char c = *le->Name(j);
								if (c == '\0') { lossy |= (niext && ni - niext > 3); break; }
								if (c == '.') { if (ni > 8) { memset(entryname+8, ' ', 3); ni = 8; } if (!ni || niext) { lossy = 1; } niext = ni; continue; }
								if (c == ' ' || ni == 11 || (ni == 8 && !niext)) { lossy = 1; continue; }
								if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { }
								else if (c >= 'a' && c <= 'z') { c ^= 0x20; }
								else if (strchr("$%'-_@~`!(){}^#&", c)) { }
								else { lossy = 1; c = '_'; }
								entryname[ni++] = (Bit8u)c;
							}
						}

						if (niext && niext != 8)
							for (int i = 2; i >= 0; i--)
								entryname[8+i] = entryname[niext+i], entryname[niext+i] = ' ';
						if (niext && niext <= 4 && ni - niext > 3)
							for (int i = niext + 3; i != 8; i++)
								entryname[i] = ' ';

						if (lossy)
						{
							if (!niext) niext = ni;
							for (int i = 1; i <= 999999; i++)
							{
								int taillen = (i<=9?2:i<=99?3:i<=999?4:i<=9999?5:i<=99999?6:7);
								char* ptr = (char*)&entryname[niext + taillen > 8 ? 8 : niext + taillen];
								for (int j = i; j; j /= 10) *--ptr = '0'+(j%10);
								*--ptr = '~';

								bool conflict = false;
								for (size_t e2 = firstidx; e2 != diridx; e2++)
									if (!(entries[e2].attrib & (DOS_ATTR_VOLUME|DOS_ATTR_PENDING_SHORT_NAME)) && !memcmp(entryname, entries[e2].entryname, sizeof(e.entryname)))
										{ conflict = true; break; }
								if (!conflict) break;
							}
						}

						Bit8u chksum = 0;
						for (int i = 0; i != 11;) chksum = (chksum >> 1) + (chksum << 7) + entryname[i++];
						for (lfndirentry* le = (lfndirentry*)&e; le-- == (lfndirentry*)&e || !(le[1].ord & 0x40);) le->chksum = chksum;
						e.attrib &= ~DOS_ATTR_PENDING_SHORT_NAME;
					}
					if (e.attrib & DOS_ATTR_DIRECTORY) // this reallocates ffdd.dirs so do this last
					{
						var_write(&e.loFirstClust, (Bit16u)(2 + ffdd.dirs.size() / entriesPerCluster));
						ParseDir(ffdd, dir, filter, totlen, myFirstCluster);
					}
				}
			}

			struct SumInfo { Bit64u used_bytes; const StringToPointerHashMap<void>* filter; };
			static void SumFileSize(const char* path, bool is_dir, Bit32u size, Bit16u, Bit16u, Bit8u, Bitu data)
			{
				if (!((SumInfo*)data)->filter || !((SumInfo*)data)->filter->Get(path))
					((SumInfo*)data)->used_bytes += (size + (32*1024-1)) / (32*1024) * (32*1024); // count as 32 kb clusters
			}
		};

		Iter::SumInfo sum = { 0, fileFilter }; Bit16u drv_bytes_sector; Bit8u drv_sectors_cluster;  Bit16u drv_total_clusters, drv_free_clusters;
		drv->AllocationInfo(&drv_bytes_sector, &drv_sectors_cluster, &drv_total_clusters, &drv_free_clusters);
		DriveFileIterator(drv, Iter::SumFileSize, (Bitu)&sum);

		readOnly = (drv_free_clusters == 0 || freeSpaceMB == 0);

		const Bit32u addFreeMB = (readOnly ? 0 : freeSpaceMB), totalMB = (Bit32u)(sum.used_bytes / (1024*1024)) + (addFreeMB ? (1 + addFreeMB) : 0);
		if      (totalMB >= 3072) { fatSz = 32; sectorsPerCluster = 64; } // 32 kb clusters ( 98304 ~        FAT entries)
		else if (totalMB >= 2048) { fatSz = 32; sectorsPerCluster = 32; } // 16 kb clusters (131072 ~ 196608 FAT entries)
		else if (totalMB >=  384) { fatSz = 16; sectorsPerCluster = 64; } // 32 kb clusters ( 12288 ~  65504 FAT entries)
		else if (totalMB >=  192) { fatSz = 16; sectorsPerCluster = 32; } // 16 kb clusters ( 12288 ~  24576 FAT entries)
		else if (totalMB >=   96) { fatSz = 16; sectorsPerCluster = 16; } //  8 kb clusters ( 12288 ~  24576 FAT entries)
		else if (totalMB >=   48) { fatSz = 16; sectorsPerCluster =  8; } //  4 kb clusters ( 12288 ~  24576 FAT entries)
		else if (totalMB >=   12) { fatSz = 16; sectorsPerCluster =  4; } //  2 kb clusters (  6144 ~  24576 FAT entries)
		else if (totalMB >=    4) { fatSz = 16; sectorsPerCluster =  1; } // .5 kb clusters (  8192 ~  24576 FAT entries)
		else if (totalMB >=    2) { fatSz = 12; sectorsPerCluster =  4; } //  2 kb clusters (  1024 ~   2048 FAT entries)
		else if (totalMB >=    1) { fatSz = 12; sectorsPerCluster =  2; } //  1 kb clusters (  1024 ~   2048 FAT entries)
		else                      { fatSz = 12; sectorsPerCluster =  1; } // .5 kb clusters (       ~   2048 FAT entries)

		// mediadescriptor in very first byte of FAT table
		Iter::SetFAT(*this, 0, (Bit32u)0xFFFFFF8);
		Iter::SetFAT(*this, 1, (Bit32u)0xFFFFFFF);

		if (fatSz != 32)
		{
			// this actually should never be anything but 512 for some FAT16 drivers
			root.resize(512);
			memset(&root[0], 0, sizeof(direntry) * 512);
		}

		char dirbuf[DOS_PATHLENGTH+4];
		Iter::ParseDir(*this, dirbuf, fileFilter);

		const Bit32u bytesPerCluster = sectorsPerCluster * BYTESPERSECTOR;
		const Bit32u entriesPerCluster = bytesPerCluster / sizeof(direntry);
		Bit32u fileCluster = (Bit32u)(2 + dirs.size() / entriesPerCluster);
		for (Bit32u fileSect = 0, rootOrDir = 0; rootOrDir != 2; rootOrDir++)
		{
			for (direntry& e : (rootOrDir ? dirs : root))
			{
				if (!e.entrysize || (e.attrib & DOS_ATTR_LONG_NAME_MASK) == DOS_ATTR_LONG_NAME) continue;
				var_write(&e.hiFirstClust, (Bit16u)(fileCluster >> 16));
				var_write(&e.loFirstClust, (Bit16u)(fileCluster));

				// Write FAT link chain
				Bit32u numClusters = (var_read(&e.entrysize) + bytesPerCluster - 1) / bytesPerCluster;
				for (Bit32u i = fileCluster, iEnd = i + numClusters - 1; i != iEnd; i++) Iter::SetFAT(*this, i, i + 1);
				Iter::SetFAT(*this, fileCluster + numClusters - 1, (Bit32u)0xFFFFFFF);

				files[fileAtSector[fileSect]].firstSect = fileSect;

				fileCluster += numClusters;
				fileSect += numClusters * sectorsPerCluster;
			}
		}

		// Add at least one page after the last file or FAT spec minimume to make ScanDisk happy (even on read-only disks)
		const Bit32u FATPageClusters = BYTESPERSECTOR * 8 / fatSz, FATMinCluster = (fatSz == 32 ? 65525 : (fatSz == 16 ? 4085 : 0)) + FATPageClusters;
		const Bit32u addFreeClusters = ((addFreeMB * (1024*1024/BYTESPERSECTOR)) + sectorsPerCluster - 1) / sectorsPerCluster;
		const Bit32u targetClusters = fileCluster + (addFreeClusters < FATPageClusters ? FATPageClusters : addFreeClusters);
		Iter::SetFAT(*this, (targetClusters < FATMinCluster ? FATMinCluster : targetClusters) - 1, 0);
		const Bit32u totalClusters = (Bit32u)((Bit64u)fat.size() * 8 / fatSz); // as set by Iter::SetFAT

		// on read-only disks, fill up the end of the FAT table with "Bad sector in cluster or reserved cluster" markers
		if (readOnly)
			for (Bit32u cluster = fileCluster; cluster != totalClusters; cluster++)
				Iter::SetFAT(*this, cluster, 0xFFFFFF7);

		const Bit32u sectorsPerFat = (Bit32u)(fat.size() / BYTESPERSECTOR);
		const Bit16u reservedSectors = (fatSz == 32 ? 32 : 1);
		const Bit32u partSize = totalClusters * sectorsPerCluster + reservedSectors;
		sect_fat1_start = SECT_BOOT + reservedSectors;
		sect_fat2_start = sect_fat1_start + sectorsPerFat;
		sect_root_start = sect_fat2_start + sectorsPerFat;
		sect_dirs_start = sect_root_start + ((root.size() * sizeof(direntry) + BYTESPERSECTOR - 1) / BYTESPERSECTOR);
		sect_files_start = sect_dirs_start + ((dirs.size() * sizeof(direntry) + BYTESPERSECTOR - 1) / BYTESPERSECTOR);
		sect_files_end = sect_files_start + fileAtSector.size();
		sect_disk_end = SECT_BOOT + partSize;
		DBP_ASSERT(sect_disk_end >= sect_files_end);

		for (ffddFile& f : files)
			f.firstSect += sect_files_start;

		if (!serial)
		{
			serial = DriveCalculateCRC32(&fat[0], fat.size());
			if (root.size()) serial = DriveCalculateCRC32((Bit8u*)&root[0], root.size() * sizeof(direntry), serial);
			if (dirs.size()) serial = DriveCalculateCRC32((Bit8u*)&dirs[0], dirs.size() * sizeof(direntry), serial);
		}

		memset(&mbr, 0, sizeof(mbr));
		var_write((Bit32u*)&mbr.booter[440], serial); //4 byte disk serial number
		var_write(&mbr.pentry[0].bootflag, 0x80); //Active bootable
		if ((sect_disk_end - 1) / (HEADCOUNT * SECTORSPERTRACK) > 0x3FF)
		{
			mbr.pentry[0].beginchs[0] = mbr.pentry[0].beginchs[1] = mbr.pentry[0].beginchs[2] = 0;
			mbr.pentry[0].endchs[0] = mbr.pentry[0].endchs[1] = mbr.pentry[0].endchs[2] = 0;
		}
		else
		{
			chs_write(mbr.pentry[0].beginchs, SECT_BOOT);
			chs_write(mbr.pentry[0].endchs, sect_disk_end - 1);
		}
		var_write(&mbr.pentry[0].absSectStart, SECT_BOOT);
		var_write(&mbr.pentry[0].partSize, partSize);
		mbr.magic1 = 0x55; mbr.magic2 = 0xaa;

		memset(&bootsec, 0, sizeof(bootsec));
		memcpy(bootsec.nearjmp, "\xEB\x3C\x90", sizeof(bootsec.nearjmp));
		memcpy(bootsec.oemname, "MSWIN4.1", sizeof(bootsec.oemname));
		var_write(&bootsec.bytespersector, BYTESPERSECTOR);
		var_write(&bootsec.sectorspercluster, sectorsPerCluster);
		var_write(&bootsec.reservedsectors, reservedSectors);
		var_write(&bootsec.fatcopies, 2);
		var_write(&bootsec.totalsectorcount, 0); // 16 bit field is 0, actual value is in totalsecdword
		var_write(&bootsec.mediadescriptor, 0xF8); //also in FAT[0]
		var_write(&bootsec.sectorspertrack, SECTORSPERTRACK);
		var_write(&bootsec.headcount, HEADCOUNT);
		var_write(&bootsec.hiddensectorcount, SECT_BOOT);
		var_write(&bootsec.totalsecdword, partSize);
		bootsec.magic1 = 0x55; bootsec.magic2 = 0xaa;
		if (fatSz != 32) // FAT12/FAT16
		{
			var_write(&mbr.pentry[0].parttype, (fatSz == 12 ? 0x01 : (sect_disk_end < 65536 ? 0x04 : 0x06))); // FAT12/16
			var_write(&bootsec.rootdirentries, (Bit16u)root.size());
			var_write(&bootsec.sectorsperfat, (Bit16u)sectorsPerFat);
			bootsec.bootcode[0] = 0x80; //Physical drive (harddisk) flag
			bootsec.bootcode[2] = 0x29; //Extended boot signature
			var_write((Bit32u*)&bootsec.bootcode[3], serial + 1); //4 byte partition serial number
			memcpy(&bootsec.bootcode[7], "NO NAME    ", 11); // volume label
			memcpy(&bootsec.bootcode[18], "FAT1    ", 8); // file system string name
			bootsec.bootcode[22] = (char)('0' + (fatSz % 10)); // '2' or '6'
		}
		else // FAT32
		{
			var_write(&mbr.pentry[0].parttype, 0x0C); //FAT32
			var_write((Bit32u*)&bootsec.bootcode[0], sectorsPerFat);
			var_write((Bit32u*)&bootsec.bootcode[8], (Bit32u)2); // First cluster number of the root directory
			var_write((Bit16u*)&bootsec.bootcode[12], (Bit16u)1); // Sector of FSInfo structure in offset from top of the FAT32 volume
			var_write((Bit16u*)&bootsec.bootcode[14], (Bit16u)6); // Sector of backup boot sector in offset from top of the FAT32 volume
			bootsec.bootcode[28] = 0x80; //Physical drive (harddisk) flag
			bootsec.bootcode[30] = 0x29; //Extended boot signature
			var_write((Bit32u*)&bootsec.bootcode[31], serial + 1); //4 byte partition serial number
			memcpy(&bootsec.bootcode[35], "NO NAME    ", 11); // volume label
			memcpy(&bootsec.bootcode[46], "FAT32   ", 8); // file system string name

			memset(fsinfosec, 0, sizeof(fsinfosec));
			var_write((Bit32u*)&fsinfosec[0], (Bit32u)0x41615252); //lead signature
			var_write((Bit32u*)&fsinfosec[484], (Bit32u)0x61417272); //Another signature
			var_write((Bit32u*)&fsinfosec[488], (Bit32u)0xFFFFFFFF); //last known free cluster count (all FF is unknown)
			var_write((Bit32u*)&fsinfosec[492], (Bit32u)0xFFFFFFFF); //the cluster number at which the driver should start looking for free clusters (all FF is unknown)
			var_write((Bit32u*)&fsinfosec[508], (Bit32u)0xAA550000); //ending signature
		}

		if (inSavePath)
			difference.SetupSave(inSavePath, sect_disk_end);
	}

	static void chs_write(Bit8u* chs, Bit32u lba)
	{
		Bit32u cylinder = lba / (HEADCOUNT * SECTORSPERTRACK);
		Bit32u head = (lba / SECTORSPERTRACK) % HEADCOUNT;
		Bit32u sector = (lba % SECTORSPERTRACK) + 1;
		DBP_ASSERT(head <= 0xFF && sector <= 0x3F && cylinder <= 0x3FF);
		chs[0] = (Bit8u)(head & 0xFF);
		chs[1] = (Bit8u)((sector & 0x3F) | ((cylinder >> 8) & 0x3));
		chs[2] = (Bit8u)(cylinder & 0xFF);
	}

	Bit8u WriteSector(Bit32u sectnum, const void* data)
	{
		if (sectnum >= sect_disk_end) return 1;
		if (sectnum == SECT_MBR)
		{
			// Windows 9x writes the disk timestamp into the booter area on startup.
			// Just copy that part over so it doesn't get treated as a difference that needs to be stored.
			memcpy(mbr.booter, data, sizeof(mbr.booter));
		}

		if (readOnly) return 0; // just return without error to avoid bluescreens in Windows 9x

		Bit8u filebuf[BYTESPERSECTOR];
		void* unmodified = GetUnmodifiedSector(sectnum, filebuf);

		if (difference.WriteDiff(sectnum, data, unmodified))
			cacheSectorNumber[sectnum % CACHECOUNT] = (Bit32u)-1; // invalidate cache

		return 0;
	}

	void* GetUnmodifiedSector(Bit32u sectnum, void* filebuf)
	{
		if (sectnum >= sect_files_end) {}
		else if (sectnum >= sect_files_start)
		{
			Bit32u idx = fileAtSector[sectnum - sect_files_start];
			ffddFile& f = files[idx];
			DOS_File* df = NULL;
			for (Bit32u i = 0; i != KEEPOPENCOUNT; i++)
				if (openIndex[i] == idx && openFiles[i])
					{ df = openFiles[i]; break; }
			if (!df)
			{
				openCursor = (openCursor + 1) % KEEPOPENCOUNT;
				DOS_File*& cachedf = openFiles[openCursor];
				if (cachedf)
				{
					cachedf->Close();
					delete cachedf;
					cachedf = NULL;
				}
				if (drive->FileOpen(&df, f.path, OPEN_READ))
				{
					df->AddRef();
					cachedf = df;
					openIndex[openCursor] = idx;
				}
				else return NULL;
			}
			if (df)
			{
				Bit32u pos = (sectnum - f.firstSect) * BYTESPERSECTOR;
				Bit16u read = (Bit16u)BYTESPERSECTOR;
				df->Seek(&pos, DOS_SEEK_SET);
				if (!df->Read((Bit8u*)filebuf, &read)) { read = 0; DBP_ASSERT(0); }
				if (read != BYTESPERSECTOR)
					memset((Bit8u*)filebuf + read, 0, BYTESPERSECTOR - read);
				return filebuf;
			}
		}
		else if (sectnum >= sect_dirs_start) return &dirs[(sectnum - sect_dirs_start) * (BYTESPERSECTOR / sizeof(direntry))];
		else if (sectnum >= sect_root_start) return &root[(sectnum - sect_root_start) * (BYTESPERSECTOR / sizeof(direntry))];
		else if (sectnum >= sect_fat2_start) return &fat[(sectnum - sect_fat2_start) * BYTESPERSECTOR];
		else if (sectnum >= sect_fat1_start) return &fat[(sectnum - sect_fat1_start) * BYTESPERSECTOR];
		else if (sectnum == SECT_BOOT) return &bootsec; // boot sector 1
		else if (sectnum == SECT_MBR) return &mbr;
		else if (sectnum == SECT_BOOT+1) return fsinfosec; // boot sector 2: fs information sector
		else if (sectnum == SECT_BOOT+2) return fsinfosec; // boot sector 3: additional boot loader code (anything is ok for us but needs 0x55AA footer signature)
		else if (sectnum == SECT_BOOT+6) return &bootsec;  // boot sector 1 copy
		else if (sectnum == SECT_BOOT+7) return fsinfosec; // boot sector 2 copy
		else if (sectnum == SECT_BOOT+8) return fsinfosec; // boot sector 3 copy
		return NULL;
	}

	Bit8u ReadSector(Bit32u sectnum, void* data)
	{
		Bit32u sectorHash = sectnum % CACHECOUNT;
		void *cachedata = cacheSectorData[sectorHash];
		if (cacheSectorNumber[sectorHash] == sectnum)
		{
			memcpy(data, cachedata, BYTESPERSECTOR);
			return 0;
		}
		cacheSectorNumber[sectorHash] = sectnum;

		if (difference.GetDiff(sectnum, data))
		{
			memcpy(cachedata, data, BYTESPERSECTOR);
		}
		else
		{
			void *src = GetUnmodifiedSector(sectnum, cachedata);
			if (src) memcpy(data, src, BYTESPERSECTOR);
			else memset(data, 0, BYTESPERSECTOR);
			if (src != cachedata) memcpy(cachedata, data, BYTESPERSECTOR);
		}
		return 0;
	}
};
#endif // C_DBP_SUPPORT_DISK_FAT_EMULATOR

diskGeo DiskGeometryList[] = {
	{ 160,  8, 1, 40, 0},	// SS/DD 5.25"
	{ 180,  9, 1, 40, 0},	// SS/DD 5.25"
	{ 200, 10, 1, 40, 0},	// SS/DD 5.25" (booters)
	{ 320,  8, 2, 40, 1},	// DS/DD 5.25"
	{ 360,  9, 2, 40, 1},	// DS/DD 5.25"
	{ 400, 10, 2, 40, 1},	// DS/DD 5.25" (booters)
	{ 720,  9, 2, 80, 3},	// DS/DD 3.5"
	{1200, 15, 2, 80, 2},	// DS/HD 5.25"
	{1440, 18, 2, 80, 4},	// DS/HD 3.5"
	{1680, 21, 2, 80, 4},	// DS/HD 3.5"  (DMF)
	{2880, 36, 2, 80, 6},	// DS/ED 3.5"
	{0, 0, 0, 0, 0}
};

Bitu call_int13;
Bitu diskparm0, diskparm1;
static Bit8u last_status;
static Bit8u last_drive;
Bit16u imgDTASeg;
RealPt imgDTAPtr;
DOS_DTA *imgDTA;
bool killRead;
static bool swapping_requested;

void BIOS_SetEquipment(Bit16u equipment);

/* 2 floppys and 2 harddrives, max */
imageDisk *imageDiskList[MAX_DISK_IMAGES];
#ifdef C_DBP_ENABLE_DISKSWAP
imageDisk *diskSwap[MAX_SWAPPABLE_DISKS];
Bit32s swapPosition;
#endif

void updateDPT(void) {
	Bit32u tmpheads, tmpcyl, tmpsect, tmpsize;
	if(imageDiskList[2] != NULL) {
		PhysPt dp0physaddr=CALLBACK_PhysPointer(diskparm0);
		imageDiskList[2]->Get_Geometry(&tmpheads, &tmpcyl, &tmpsect, &tmpsize);
		phys_writew(dp0physaddr,(Bit16u)tmpcyl);
		phys_writeb(dp0physaddr+0x2,(Bit8u)tmpheads);
		phys_writew(dp0physaddr+0x3,0);
		phys_writew(dp0physaddr+0x5,(Bit16u)-1);
		phys_writeb(dp0physaddr+0x7,0);
		phys_writeb(dp0physaddr+0x8,(0xc0 | (((imageDiskList[2]->heads) > 8) << 3)));
		phys_writeb(dp0physaddr+0x9,0);
		phys_writeb(dp0physaddr+0xa,0);
		phys_writeb(dp0physaddr+0xb,0);
		phys_writew(dp0physaddr+0xc,(Bit16u)tmpcyl);
		phys_writeb(dp0physaddr+0xe,(Bit8u)tmpsect);
	}
	if(imageDiskList[3] != NULL) {
		PhysPt dp1physaddr=CALLBACK_PhysPointer(diskparm1);
		imageDiskList[3]->Get_Geometry(&tmpheads, &tmpcyl, &tmpsect, &tmpsize);
		phys_writew(dp1physaddr,(Bit16u)tmpcyl);
		phys_writeb(dp1physaddr+0x2,(Bit8u)tmpheads);
		phys_writeb(dp1physaddr+0xe,(Bit8u)tmpsect);
	}
}

void incrementFDD(void) {
	Bit16u equipment=mem_readw(BIOS_CONFIGURATION);
	if(equipment&1) {
		Bitu numofdisks = (equipment>>6)&3;
		numofdisks++;
		if(numofdisks > 1) numofdisks=1;//max 2 floppies at the moment
		equipment&=~0x00C0;
		equipment|=(numofdisks<<6);
	} else equipment|=1;
	BIOS_SetEquipment(equipment);
}

#ifdef C_DBP_ENABLE_DISKSWAP
void swapInDisks(void) {
	bool allNull = true;
	Bit32s diskcount = 0;
	Bit32s swapPos = swapPosition;
	Bit32s i;

	/* Check to make sure that  there is at least one setup image */
	for(i=0;i<MAX_SWAPPABLE_DISKS;i++) {
		if(diskSwap[i]!=NULL) {
			allNull = false;
			break;
		}
	}

	/* No disks setup... fail */
	if (allNull) return;

	/* If only one disk is loaded, this loop will load the same disk in dive A and drive B */
	while(diskcount<2) {
		if(diskSwap[swapPos] != NULL) {
			LOG_MSG("Loaded disk %d from swaplist position %d - \"%s\"", diskcount, swapPos, diskSwap[swapPos]->diskname);
			imageDiskList[diskcount] = diskSwap[swapPos];
			diskcount++;
		}
		swapPos++;
		if(swapPos>=MAX_SWAPPABLE_DISKS) swapPos=0;
	}
}
#endif

bool getSwapRequest(void) {
	bool sreq=swapping_requested;
	swapping_requested = false;
	return sreq;
}

#ifdef C_DBP_ENABLE_DISKSWAP
void swapInNextDisk(bool pressed) {
	if (!pressed)
		return;
	DriveManager::CycleAllDisks();
	/* Hack/feature: rescan all disks as well */
	LOG_MSG("Diskcaching reset for normal mounted drives.");
	for(Bitu i=0;i<DOS_DRIVES;i++) {
		if (Drives[i]) Drives[i]->EmptyCache();
	}
	swapPosition++;
	if(diskSwap[swapPosition] == NULL) swapPosition = 0;
	swapInDisks();
	swapping_requested = true;
}
#endif

Bit8u imageDisk::Read_Sector(Bit32u head,Bit32u cylinder,Bit32u sector,void * data) {
	Bit32u sectnum;

	sectnum = ( (cylinder * heads + head) * sectors ) + sector - 1L;

	return Read_AbsoluteSector(sectnum, data);
}

Bit8u imageDisk::Read_AbsoluteSector(Bit32u sectnum, void * data) {
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
	if (ffdd) return ffdd->ReadSector(sectnum, data);
	#endif

	if (discard && discard->Read_AbsoluteSector(sectnum, data, sector_size))
		return 0x00;

	if (differencing && differencing->GetDiff(sectnum, data))
		return 0x00;

	Bit64u bytenum = (Bit64u)sectnum * sector_size;
	if (last_action==WRITE || bytenum!=current_fpos) dos_file->Seek64(&bytenum, DOS_SEEK_SET);
	DBP_ASSERT(sector_size <= 0xFFFF);
	Bit16u read_size = (Bit16u)sector_size;
	size_t ret=dos_file->Read((Bit8u*)data, &read_size)?read_size:0;
	#else
	Bit32u bytenum;

	bytenum = sectnum * sector_size;

	if (last_action==WRITE || bytenum!=current_fpos) fseek_wrap(diskimg,bytenum,SEEK_SET);
	size_t ret=fread(data, 1, sector_size, diskimg);
	#endif
	current_fpos=bytenum+ret;
	last_action=READ;

	return 0x00;
}

Bit8u imageDisk::Write_Sector(Bit32u head,Bit32u cylinder,Bit32u sector,void * data) {
	Bit32u sectnum;

	sectnum = ( (cylinder * heads + head) * sectors ) + sector - 1L;

	return Write_AbsoluteSector(sectnum, data);
}

Bit8u imageDisk::Write_AbsoluteSector(Bit32u sectnum, void *data) {
	#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
	if (ffdd) return ffdd->WriteSector(sectnum, data);
	#endif

	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	if (discard)
	{
		discard->Write_AbsoluteSector(sectnum, data, sector_size);
		return 0x00;
	}

	if (differencing)
	{
		Bit64u unmodified_fpos = (Bit64u)sectnum * differencingDisk::BYTESPERSECTOR;
		if (unmodified_fpos != current_fpos) dos_file->Seek64(&unmodified_fpos, DOS_SEEK_SET);
		current_fpos = unmodified_fpos + differencingDisk::BYTESPERSECTOR;
		Bit8u buf[differencingDisk::BYTESPERSECTOR];
		Bit16u read_size = (Bit16u)differencingDisk::BYTESPERSECTOR;
		const void* unmodified = (dos_file->Read(buf, &read_size) ? buf : NULL);
		differencing->WriteDiff(sectnum, data, unmodified);
		return 0x00;
	}

	Bit64u bytenum = (Bit64u)sectnum * sector_size;
	if (last_action==READ || bytenum!=current_fpos) dos_file->Seek64(&bytenum, DOS_SEEK_SET);
	DBP_ASSERT(sector_size <= 0xFFFF);
	Bit16u write_size = (Bit16u)sector_size;
	size_t ret=dos_file->Write((Bit8u*)data, &write_size)?write_size:0;
	#else
	Bit32u bytenum;

	bytenum = sectnum * sector_size;

	//LOG_MSG("Writing sectors to %ld at bytenum %d", sectnum, bytenum);

	if (last_action==READ || bytenum!=current_fpos) fseek_wrap(diskimg,bytenum,SEEK_SET);
	size_t ret=fwrite(data, 1, sector_size, diskimg);
	#endif
	current_fpos=bytenum+ret;
	last_action=WRITE;

	return ((ret>0)?0x00:0x05);

}

#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
Bit32u imageDisk::Read_Raw(Bit8u *buffer, Bit32u seek, Bit32u len)
{
	if (seek != current_fpos)
	{
		current_fpos = seek;
		dos_file->Seek64(&current_fpos, DOS_SEEK_SET);
	}
	for (Bit32u remain = (Bit32u)len; remain;)
	{
		Bit16u sz = (remain > 0xFFFF ? 0xFFFF : (Bit16u)remain);
		if (!dos_file->Read(buffer, &sz) || !sz) { len -= remain; break; }
		remain -= sz;
		buffer += sz;
	}
	current_fpos += (Bit32u)len;
	return len;
}

void imageDisk::SetDifferencingDisk(const char* savePath)
{
	if (sector_size != differencingDisk::BYTESPERSECTOR) { E_Exit("Cannot use differencing disk on image with %d bytes per sector", sector_size); return; }
	if (discard) { delete discard; discard = NULL; }
	if (differencing) delete differencing;
	differencing = new differencingDisk();
	differencing->SetupSave(savePath, heads*cylinders*sectors);
}

imageDisk::~imageDisk()
{
	for (Bit16u i=0;i<DOS_DRIVES;i++)
	{
		fatDrive* fat_drive = (Drives[i] ? dynamic_cast<fatDrive*>(Drives[i]) : NULL);
		if (!fat_drive || fat_drive->loadedDisk != this) continue;
		fat_drive->loadedDisk = NULL;
	}
	if (dos_file)
	{
		if (dos_file->IsOpen()) dos_file->Close();
		if (dos_file->RemoveRef() <= 0) delete dos_file;
	}
#ifdef C_DBP_ENABLE_DISKSWAP
	for(int i=0;i<MAX_SWAPPABLE_DISKS;i++)
		if (diskSwap[i] == this)
			diskSwap[i] = NULL;
#endif
	for(int i=0;i<MAX_DISK_IMAGES;i++)
		if (imageDiskList[i] == this)
			imageDiskList[i] = NULL;
	if (discard) delete discard;
	if (differencing) delete differencing;
#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
	if (ffdd) delete ffdd;
#endif
}

imageDisk::imageDisk(DOS_File *imgFile, const char *imgName, Bit32u imgSizeK, bool isHardDisk)
#else
imageDisk::imageDisk(FILE *imgFile, const char *imgName, Bit32u imgSizeK, bool isHardDisk)
#endif
{
	heads = 0;
	cylinders = 0;
	sectors = 0;
	sector_size = 512;
	current_fpos = 0;
	last_action = NONE;
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	DBP_ASSERT(imgFile->refCtr >= 1);
	dos_file = imgFile;
	dos_file->Seek64(&current_fpos, DOS_SEEK_SET);
	if (!OPEN_IS_WRITING(dos_file->flags))
		discard = new discardDisk();
	#else
	diskimg = imgFile;
	fseek(diskimg,0,SEEK_SET);
	#endif
	memset(diskname,0,512);
	safe_strncpy(diskname, imgName, sizeof(diskname));
	active = false;
	hardDrive = isHardDisk;
	if(!isHardDisk) {
		Bit8u i=0;
		bool founddisk = false;
		while (DiskGeometryList[i].ksize!=0x0) {
			if ((DiskGeometryList[i].ksize==imgSizeK) ||
				(DiskGeometryList[i].ksize+1==imgSizeK)) {
				if (DiskGeometryList[i].ksize!=imgSizeK)
					LOG_MSG("ImageLoader: image file with additional data, might not load!");
				founddisk = true;
				active = true;
				floppytype = i;
				heads = DiskGeometryList[i].headscyl;
				cylinders = DiskGeometryList[i].cylcount;
				sectors = DiskGeometryList[i].secttrack;
				break;
			}
			i++;
		}
		if(!founddisk) {
			active = false;
		} else {
			incrementFDD();
		}
	}
}

#ifdef C_DBP_SUPPORT_DISK_FAT_EMULATOR
imageDisk::imageDisk(class DOS_Drive *useDrive, Bit32u freeSpaceMB, const char* savePath, Bit32u driveSerial, const StringToPointerHashMap<void>* fileFilter)
{
	ffdd = new fatFromDOSDrive(useDrive, freeSpaceMB, savePath, driveSerial, fileFilter);
	last_action = NONE;
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	dos_file = NULL;
	#else
	diskimg = NULL;
	#endif
	diskname[0] = '\0';
	hardDrive = true;
	Set_GeometryForHardDisk();
}

void imageDisk::Set_GeometryForHardDisk()
{
	sector_size = 512;
	partTable mbrData;
	for (int m = (Read_AbsoluteSector(0, &mbrData) ? 0 : 4); m--;)
	{
		if(!mbrData.pentry[m].partSize) continue;
		bootstrap bootbuffer;
		if (Read_AbsoluteSector(mbrData.pentry[m].absSectStart, &bootbuffer)) continue;
		bootbuffer.sectorspertrack = var_read(&bootbuffer.sectorspertrack);
		bootbuffer.headcount = var_read(&bootbuffer.headcount);
		Bit32u setSect = bootbuffer.sectorspertrack;
		Bit32u setHeads = bootbuffer.headcount;
		Bit32u setCyl = (mbrData.pentry[m].absSectStart + mbrData.pentry[m].partSize + setSect * setHeads - 1) / (setSect * setHeads);
		Set_Geometry(setHeads, setCyl, setSect, 512);
		return;
	}
	#ifdef C_DBP_SUPPORT_DISK_MOUNT_DOSFILE
	if (!dos_file) { DBP_ASSERT(false); return; }
	Bit64u diskimgsize = 0;
	dos_file->Seek64(&diskimgsize, DOS_SEEK_END);
	dos_file->Seek64(&current_fpos, DOS_SEEK_SET);
	#else
	if (!diskimg) return;
	Bit32u diskimgsize;
	fseek(diskimg,0,SEEK_END);
	diskimgsize = (Bit32u)ftell(diskimg);
	fseek(diskimg,current_fpos,SEEK_SET);
	#endif
	Set_Geometry(16, (Bit32u)(diskimgsize / (512 * 63 * 16)), 63, 512);
}
#endif

void imageDisk::Set_Geometry(Bit32u setHeads, Bit32u setCyl, Bit32u setSect, Bit32u setSectSize) {
	DBP_ASSERT(setHeads);
	heads = setHeads;
	cylinders = setCyl;
	sectors = setSect;
	sector_size = setSectSize;
	active = true;
}

void imageDisk::Get_Geometry(Bit32u * getHeads, Bit32u *getCyl, Bit32u *getSect, Bit32u *getSectSize) {
	*getHeads = heads;
	*getCyl = cylinders;
	*getSect = sectors;
	*getSectSize = sector_size;
}

Bit8u imageDisk::GetBiosType(void) {
	if(!hardDrive) {
		return (Bit8u)DiskGeometryList[floppytype].biosval;
	} else return 0;
}

Bit32u imageDisk::getSectSize(void) {
	return sector_size;
}

static Bit8u GetDosDriveNumber(Bit8u biosNum) {
	switch(biosNum) {
		case 0x0:
			return 0x0;
		case 0x1:
			return 0x1;
		case 0x80:
			return 0x2;
		case 0x81:
			return 0x3;
		case 0x82:
			return 0x4;
		case 0x83:
			return 0x5;
		default:
			return 0x7f;
	}
}

static bool driveInactive(Bit8u driveNum) {
	if(driveNum>=MAX_DISK_IMAGES) {
		LOG(LOG_BIOS,LOG_ERROR)("Disk %d non-existant", driveNum);
		last_status = 0x01;
		CALLBACK_SCF(true);
		return true;
	}
	if(imageDiskList[driveNum] == NULL) {
		LOG(LOG_BIOS,LOG_ERROR)("Disk %d not active", driveNum);
		last_status = 0x01;
		CALLBACK_SCF(true);
		return true;
	}
	if(!imageDiskList[driveNum]->active) {
		LOG(LOG_BIOS,LOG_ERROR)("Disk %d not active", driveNum);
		last_status = 0x01;
		CALLBACK_SCF(true);
		return true;
	}
	return false;
}

#ifdef C_DBP_LIBRETRO // added implementation of INT13 extensions from Taewoong's Daum branch
struct DAP {
	Bit8u sz;
	Bit8u res;
	Bit16u num;
	Bit16u off;
	Bit16u seg;
	Bit32u sector;
};

static void readDAP(Bit16u seg, Bit16u off, DAP& dap) {
	dap.sz = real_readb(seg,off++);
	dap.res = real_readb(seg,off++);
	dap.num = real_readw(seg,off); off += 2;
	dap.off = real_readw(seg,off); off += 2;
	dap.seg = real_readw(seg,off); off += 2;

	/* Although sector size is 64-bit, 32-bit 2TB limit should be more than enough */
	dap.sector = real_readd(seg,off); off +=4;

	if (real_readd(seg,off)) {
		E_Exit("INT13: 64-bit sector addressing not supported");
	}
}
#endif

static Bitu INT13_DiskHandler(void) {
	Bit16u segat, bufptr;
	Bit8u sectbuf[512];
	Bit8u  drivenum;
	Bitu  i,t;
	last_drive = reg_dl;
	drivenum = GetDosDriveNumber(reg_dl);
	bool any_images = false;
	for(i = 0;i < MAX_DISK_IMAGES;i++) {
		if(imageDiskList[i]) any_images=true;
	}

	// unconditionally enable the interrupt flag
	CALLBACK_SIF(true);

	//drivenum = 0;
	//LOG_MSG("INT13: Function %x called on drive %x (dos drive %d)", reg_ah,  reg_dl, drivenum);

	// NOTE: the 0xff error code returned in some cases is questionable; 0x01 seems more correct
	switch(reg_ah) {
	case 0x0: /* Reset disk */
		{
			/* if there aren't any diskimages (so only localdrives and virtual drives)
			 * always succeed on reset disk. If there are diskimages then and only then
			 * do real checks
			 */
			if (any_images && driveInactive(drivenum)) {
				/* driveInactive sets carry flag if the specified drive is not available */
				if ((machine==MCH_CGA) || (machine==MCH_PCJR)) {
					/* those bioses call floppy drive reset for invalid drive values */
					if (((imageDiskList[0]) && (imageDiskList[0]->active)) || ((imageDiskList[1]) && (imageDiskList[1]->active))) {
						if (machine!=MCH_PCJR && reg_dl<0x80) reg_ip++;
						last_status = 0x00;
						CALLBACK_SCF(false);
					}
				}
				return CBRET_NONE;
			}
			if (machine!=MCH_PCJR && reg_dl<0x80) reg_ip++;
			last_status = 0x00;
			CALLBACK_SCF(false);
		}
        break;
	case 0x1: /* Get status of last operation */

		if(last_status != 0x00) {
			reg_ah = last_status;
			CALLBACK_SCF(true);
		} else {
			reg_ah = 0x00;
			CALLBACK_SCF(false);
		}
		break;
	case 0x2: /* Read sectors */
		if (reg_al==0) {
			reg_ah = 0x01;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}
		if (drivenum >= MAX_DISK_IMAGES || imageDiskList[drivenum] == NULL) {
			if (drivenum >= DOS_DRIVES || !Drives[drivenum] || Drives[drivenum]->isRemovable()) {
				reg_ah = 0x01;
				CALLBACK_SCF(true);
				return CBRET_NONE;
			}
			// Inherit the Earth cdrom and Amberstar use it as a disk test
			if (((reg_dl&0x80)==0x80) && (reg_dh==0) && ((reg_cl&0x3f)==1)) {
				if (reg_ch==0) {
					// write some MBR data into buffer for Amberstar installer
					real_writeb(SegValue(es),reg_bx+0x1be,0x80); // first partition is active
					real_writeb(SegValue(es),reg_bx+0x1c2,0x06); // first partition is FAT16B
				}
				reg_ah = 0;
				CALLBACK_SCF(false);
				return CBRET_NONE;
			}
		}
		if (driveInactive(drivenum)) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}

		segat = SegValue(es);
		bufptr = reg_bx;
		for(i=0;i<reg_al;i++) {
			last_status = imageDiskList[drivenum]->Read_Sector((Bit32u)reg_dh, (Bit32u)(reg_ch | ((reg_cl & 0xc0)<< 2)), (Bit32u)((reg_cl & 63)+i), sectbuf);
			if((last_status != 0x00) || (killRead)) {
				LOG_MSG("Error in disk read");
				killRead = false;
				reg_ah = 0x04;
				CALLBACK_SCF(true);
				return CBRET_NONE;
			}
			//DBP: Changed loop to use mem_writeb_inline
			for(t=0;t<512;t++) {
				//real_writeb(segat,bufptr,sectbuf[t]);
				mem_writeb_inline((segat<<4)+bufptr,sectbuf[t]);
				bufptr++;
			}
		}
		reg_ah = 0x00;
		CALLBACK_SCF(false);
		break;
	case 0x3: /* Write sectors */
		
		if(driveInactive(drivenum)) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
        }                     


		bufptr = reg_bx;
		for(i=0;i<reg_al;i++) {
			//DBP: Changed loop to use mem_readb_inline (and fixed sector size like write)
			DBP_ASSERT(imageDiskList[drivenum]->getSectSize() == 512);
			for(t=0;t<512;t++) {
				//sectbuf[t] = real_readb(SegValue(es),bufptr);
				sectbuf[t] = mem_readb_inline((SegValue(es)<<4)+bufptr);
				bufptr++;
			}

			last_status = imageDiskList[drivenum]->Write_Sector((Bit32u)reg_dh, (Bit32u)(reg_ch | ((reg_cl & 0xc0) << 2)), (Bit32u)((reg_cl & 63) + i), &sectbuf[0]);
			if(last_status != 0x00) {
            CALLBACK_SCF(true);
				return CBRET_NONE;
			}
        }
		reg_ah = 0x00;
		CALLBACK_SCF(false);
        break;
	case 0x04: /* Verify sectors */
		if (reg_al==0) {
			reg_ah = 0x01;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}
		if(driveInactive(drivenum)) {
			reg_ah = last_status;
			return CBRET_NONE;
		}

		/* TODO: Finish coding this section */
		/*
		segat = SegValue(es);
		bufptr = reg_bx;
		for(i=0;i<reg_al;i++) {
			last_status = imageDiskList[drivenum]->Read_Sector((Bit32u)reg_dh, (Bit32u)(reg_ch | ((reg_cl & 0xc0)<< 2)), (Bit32u)((reg_cl & 63)+i), sectbuf);
			if(last_status != 0x00) {
				LOG_MSG("Error in disk read");
				CALLBACK_SCF(true);
				return CBRET_NONE;
			}
			for(t=0;t<512;t++) {
				real_writeb(segat,bufptr,sectbuf[t]);
				bufptr++;
			}
		}*/
		reg_ah = 0x00;
		//Qbix: The following codes don't match my specs. al should be number of sector verified
		//reg_al = 0x10; /* CRC verify failed */
		//reg_al = 0x00; /* CRC verify succeeded */
		CALLBACK_SCF(false);
          
		break;
	case 0x05: /* Format track */
		if (driveInactive(drivenum)) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}
		reg_ah = 0x00;
		CALLBACK_SCF(false);
		break;
	case 0x08: /* Get drive parameters */
		if(driveInactive(drivenum)) {
			last_status = 0x07;
			reg_ah = last_status;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}
		reg_ax = 0x00;
		reg_bl = imageDiskList[drivenum]->GetBiosType();
		Bit32u tmpheads, tmpcyl, tmpsect, tmpsize;
		imageDiskList[drivenum]->Get_Geometry(&tmpheads, &tmpcyl, &tmpsect, &tmpsize);
		if (tmpcyl==0) LOG(LOG_BIOS,LOG_ERROR)("INT13 DrivParm: cylinder count zero!");
		else tmpcyl--;		// cylinder count -> max cylinder
		if (tmpheads==0) LOG(LOG_BIOS,LOG_ERROR)("INT13 DrivParm: head count zero!");
		else tmpheads--;	// head count -> max head
		reg_ch = (Bit8u)(tmpcyl & 0xff);
		reg_cl = (Bit8u)(((tmpcyl >> 2) & 0xc0) | (tmpsect & 0x3f)); 
		reg_dh = (Bit8u)tmpheads;
		last_status = 0x00;
		if (reg_dl&0x80) {	// harddisks
			reg_dl = 0;
			//DBP: Support more than 2 harddisks
			for (int i = 2; i != MAX_DISK_IMAGES; i++)
				if(imageDiskList[i] != NULL) reg_dl = i - 1;
		} else {		// floppy disks
			reg_dl = 0;
			if(imageDiskList[0] != NULL) reg_dl++;
			if(imageDiskList[1] != NULL) reg_dl++;
		}
		CALLBACK_SCF(false);
		break;
	case 0x11: /* Recalibrate drive */
		reg_ah = 0x00;
		CALLBACK_SCF(false);
		break;
	case 0x15: /* Get disk type */
		/* Korean Powerdolls uses this to detect harddrives */
		LOG(LOG_BIOS,LOG_WARN)("INT13: Get disktype used!");
		if (any_images) {
			if(driveInactive(drivenum)) {
				last_status = 0x07;
				reg_ah = last_status;
				CALLBACK_SCF(true);
				return CBRET_NONE;
			}
			Bit32u tmpheads, tmpcyl, tmpsect, tmpsize;
			imageDiskList[drivenum]->Get_Geometry(&tmpheads, &tmpcyl, &tmpsect, &tmpsize);
			Bit64u largesize = tmpheads*tmpcyl*tmpsect*tmpsize;
			largesize/=512;
			Bit32u ts = static_cast<Bit32u>(largesize);
			reg_ah = (drivenum <2)?1:3; //With 2 for floppy MSDOS starts calling int 13 ah 16
			if(reg_ah == 3) {
				reg_cx = static_cast<Bit16u>(ts >>16);
				reg_dx = static_cast<Bit16u>(ts & 0xffff);
			}
			CALLBACK_SCF(false);
		} else {
			if (drivenum <DOS_DRIVES && (Drives[drivenum] != 0 || drivenum <2)) {
				if (drivenum <2) {
					//TODO use actual size (using 1.44 for now).
					reg_ah = 0x1; // type
//					reg_cx = 0;
//					reg_dx = 2880; //Only set size for harddrives.
				} else {
					//TODO use actual size (using 105 mb for now).
					reg_ah = 0x3; // type
					reg_cx = 3;
					reg_dx = 0x4800;
				}
				CALLBACK_SCF(false);
			} else { 
				LOG(LOG_BIOS,LOG_WARN)("INT13: no images, but invalid drive for call 15");
				reg_ah=0xff;
				CALLBACK_SCF(true);
			}
		}
		break;
	case 0x17: /* Set disk type for format */
		/* Pirates! needs this to load */
		killRead = true;
		reg_ah = 0x00;
		CALLBACK_SCF(false);
		break;
#ifdef C_DBP_LIBRETRO // added implementation of INT13 extensions from Taewoong's Daum branch
	case 0x41: /* Check Extensions Present */
		if ((reg_bx == 0x55aa) && !(driveInactive(drivenum))) {
			reg_ah=0x1;	/* 1.x extension supported */
			reg_bx=0xaa55;	/* Extensions installed */
			reg_cx=0x1;	/* Extended disk access functions (AH=42h-44h,47h,48h) supported */
			CALLBACK_SCF(false);
			break;
		}
		LOG_MSG("INT13: AH=41h, Function not supported 0x%x for drive: 0x%x", reg_bx, reg_dl);
		CALLBACK_SCF(true);
		break;
	case 0x42: /* Extended Read Sectors From Drive */
		/* Read Disk Address Packet */
		DAP dap;
		readDAP(SegValue(ds),reg_si,dap);

		if (dap.num==0) {
			reg_ah = 0x01;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}
		if (!any_images) {
			// Inherit the Earth cdrom (uses it as disk test)
			if (((reg_dl&0x80)==0x80) && (reg_dh==0) && ((reg_cl&0x3f)==1)) {
				reg_ah = 0;
				CALLBACK_SCF(false);
				return CBRET_NONE;
			}
		}
		if (driveInactive(drivenum)) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}

		segat = dap.seg;
		bufptr = dap.off;
		for(i=0;i<dap.num;i++) {
			last_status = imageDiskList[drivenum]->Read_AbsoluteSector(dap.sector+i, sectbuf);

			////DBP: Omitted for now
			//IDE_EmuINT13DiskReadByBIOS_LBA(reg_dl,dap.sector+i);

			if((last_status != 0x00) || (killRead)) {
				LOG_MSG("Error in disk read");
				killRead = false;
				reg_ah = 0x04;
				CALLBACK_SCF(true);
				return CBRET_NONE;
			}
			//DBP: Changed loop to use mem_writeb_inline
			for(t=0;t<512;t++) {
				//real_writeb(segat,bufptr,sectbuf[t]);
				mem_writeb_inline((segat<<4)+bufptr,sectbuf[t]);
				bufptr++;
			}
		}
		reg_ah = 0x00;
		CALLBACK_SCF(false);
		break;
	case 0x43: /* Extended Write Sectors to Drive */
		if(driveInactive(drivenum)) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}

		/* Read Disk Address Packet */
		readDAP(SegValue(ds),reg_si,dap);
		bufptr = dap.off;
		for(i=0;i<dap.num;i++) {
			//DBP: Changed loop to use mem_readb_inline (and fixed sector size like write)
			DBP_ASSERT(imageDiskList[drivenum]->getSectSize() == 512);
			for(t=0;t<512;t++) {
				//sectbuf[t] = real_readb(dap.seg,bufptr);
				sectbuf[t] = mem_readb_inline((dap.seg<<4)+bufptr);
				bufptr++;
			}

			last_status = imageDiskList[drivenum]->Write_AbsoluteSector(dap.sector+i, &sectbuf[0]);
			if(last_status != 0x00) {
				CALLBACK_SCF(true);
				return CBRET_NONE;
			}
		}
		reg_ah = 0x00;
		CALLBACK_SCF(false);
		break;
	case 0x48: { /* get drive parameters */
		uint16_t bufsz;

		if(driveInactive(drivenum)) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}

		segat = SegValue(ds);
		bufptr = reg_si;
		bufsz = real_readw(segat,bufptr+0);
		if (bufsz < 0x1A) {
			reg_ah = 0xff;
			CALLBACK_SCF(true);
			return CBRET_NONE;
		}
		if (bufsz > 0x1E) bufsz = 0x1E;
		else bufsz = 0x1A;

		Bit32u tmpheads, tmpcyl, tmpsect, tmpsize;
		imageDiskList[drivenum]->Get_Geometry(&tmpheads, &tmpcyl, &tmpsect, &tmpsize);

		real_writew(segat,bufptr+0x00,bufsz);
		real_writew(segat,bufptr+0x02,0x0003);	/* C/H/S valid, DMA boundary errors handled */
		real_writed(segat,bufptr+0x04,tmpcyl);
		real_writed(segat,bufptr+0x08,tmpheads);
		real_writed(segat,bufptr+0x0C,tmpsect);
		real_writed(segat,bufptr+0x10,tmpcyl*tmpheads*tmpsect);
		real_writed(segat,bufptr+0x14,0);
		real_writew(segat,bufptr+0x18,512);
		if (bufsz >= 0x1E)
			real_writed(segat,bufptr+0x1A,0xFFFFFFFF); /* no EDD information available */

		reg_ah = 0x00;
		CALLBACK_SCF(false);
		} break;
#endif
	default:
		LOG(LOG_BIOS,LOG_ERROR)("INT13: Function %x called on drive %x (dos drive %d)", reg_ah,  reg_dl, drivenum);
		reg_ah=0xff;
		CALLBACK_SCF(true);
	}
	return CBRET_NONE;
}


void BIOS_SetupDisks(void) {
/* TODO Start the time correctly */
	call_int13=CALLBACK_Allocate();	
	CALLBACK_Setup(call_int13,&INT13_DiskHandler,CB_INT13,"Int 13 Bios disk");
	RealSetVec(0x13,CALLBACK_RealPointer(call_int13));
	int i;
	//DBP: Changed fixed 4 to MAX_DISK_IMAGES
	for(i=0;i<MAX_DISK_IMAGES;i++) {
		imageDiskList[i] = NULL;
	}

#ifdef C_DBP_ENABLE_DISKSWAP
	for(i=0;i<MAX_SWAPPABLE_DISKS;i++) {
		diskSwap[i] = NULL;
	}
#endif

	diskparm0 = CALLBACK_Allocate();
	diskparm1 = CALLBACK_Allocate();
#ifdef C_DBP_ENABLE_DISKSWAP
	swapPosition = 0;
#endif

	RealSetVec(0x41,CALLBACK_RealPointer(diskparm0));
	RealSetVec(0x46,CALLBACK_RealPointer(diskparm1));

	PhysPt dp0physaddr=CALLBACK_PhysPointer(diskparm0);
	PhysPt dp1physaddr=CALLBACK_PhysPointer(diskparm1);
	for(i=0;i<16;i++) {
		phys_writeb(dp0physaddr+i,0);
		phys_writeb(dp1physaddr+i,0);
	}

	imgDTASeg = 0;

/* Setup the Bios Area */
	mem_writeb(BIOS_HARDDISK_COUNT,2);

#ifdef C_DBP_ENABLE_MAPPER
	MAPPER_AddHandler(swapInNextDisk,MK_f4,MMOD1,"swapimg","Swap Image");
#endif
	killRead = false;
	swapping_requested = false;
}

//DBP: Memory cleanup
void BIOS_ShutdownDisks(void) {
#ifdef C_DBP_ENABLE_DISKSWAP
	for (int i = 0; i != MAX_SWAPPABLE_DISKS + MAX_DISK_IMAGES; i++) {
		imageDisk* id = (i < MAX_SWAPPABLE_DISKS ? diskSwap[i] : imageDiskList[i - MAX_SWAPPABLE_DISKS]);
		if (!id) continue;
		delete id;
		for (int j = 0; j != MAX_SWAPPABLE_DISKS + MAX_DISK_IMAGES; j++) {
			imageDisk*& jd = (j < MAX_SWAPPABLE_DISKS ? diskSwap[j] : imageDiskList[j - MAX_SWAPPABLE_DISKS]);
			if (jd == id) jd = NULL;
		}
	}
#else
	for (int i = 0; i != MAX_DISK_IMAGES; i++) {
		if (!imageDiskList[i]) continue;
		delete imageDiskList[i];
		imageDiskList[i] = NULL;
	}
#endif
	imgDTASeg = 0;
	imgDTAPtr = 0;
	if (imgDTA) { delete imgDTA; imgDTA = NULL; }
}

void DBP_SetMountSwappingRequested()
{
	swapping_requested = true;
}
