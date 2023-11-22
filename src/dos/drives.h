/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *  Copyright (C) 2020-2023  Bernhard Schelling
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


#ifndef _DRIVES_H__
#define _DRIVES_H__

#include <vector>
#include <string>
#include <stdlib.h>
#include <sys/types.h>
#include "dos_system.h"
#include "shell.h" /* for DOS_Shell */

bool WildFileCmp(const char * file, const char * wild);
void Set_Label(char const * const input, char * const output, bool cdrom);

class DriveManager {
public:
	static void AppendDisk(int drive, DOS_Drive* disk);
	static void InitializeDrive(int drive);
	static int UnmountDrive(int drive);
//	static void CycleDrive(bool pressed);
//	static void CycleDisk(bool pressed);
	static void CycleDisks(int drive, bool notify);
	static void CycleAllDisks(void);
	static void Init(Section* sec);
	
private:
	static struct DriveInfo {
		std::vector<DOS_Drive*> disks;
		Bit32u currentDisk;
	} driveInfos[DOS_DRIVES];
	
	static int currentDrive;
};

class localDrive : public DOS_Drive {
public:
	localDrive(const char * startdir,Bit16u _bytes_sector,Bit8u _sectors_cluster,Bit16u _total_clusters,Bit16u _free_clusters,Bit8u _mediaid);
	virtual bool FileOpen(DOS_File * * file,char * name,Bit32u flags);
	virtual FILE *GetSystemFilePtr(char const * const name, char const * const type);
	virtual bool GetSystemFilename(char* sysName, char const * const dosName);
	virtual bool FileCreate(DOS_File * * file,char * name,Bit16u attributes);
	virtual bool FileUnlink(char * name);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
	virtual bool TestDir(char * dir);
	virtual bool FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst=false);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool GetFileAttr(char * name,Bit16u * attr);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool AllocationInfo(Bit16u * _bytes_sector,Bit8u * _sectors_cluster,Bit16u * _total_clusters,Bit16u * _free_clusters);
	virtual bool FileExists(const char* name);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual bool GetLongFileName(const char* name, char longname[256]);
	virtual Bit8u GetMediaByte(void);
	virtual void EmptyCache(void) { dirCache.EmptyCache(label); };
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
	const char* getBasedir() {return basedir;};

	//DBP: Moved from DOS_Drive
	DOS_Drive_Cache dirCache;

protected:
	char basedir[CROSS_LEN];
private:
	friend void DOS_Shell::CMD_SUBST(char* args);
protected:
	struct {
		char srch_dir[CROSS_LEN];
	} srchInfo[MAX_OPENDIRS];

private:
	struct {
		Bit16u bytes_sector;
		Bit8u sectors_cluster;
		Bit16u total_clusters;
		Bit16u free_clusters;
		Bit8u mediaid;
	} allocation;
};

#ifdef _MSC_VER
#pragma pack (1)
#endif
struct bootstrap {
	Bit8u  nearjmp[3];
	Bit8u  oemname[8];
	Bit16u bytespersector;
	Bit8u  sectorspercluster;
	Bit16u reservedsectors;
	Bit8u  fatcopies;
	Bit16u rootdirentries;
	Bit16u totalsectorcount;
	Bit8u  mediadescriptor;
	Bit16u sectorsperfat;
	Bit16u sectorspertrack;
	Bit16u headcount;
	/* 32-bit FAT extensions */
	Bit32u hiddensectorcount;
	Bit32u totalsecdword;
	Bit8u  bootcode[474];
	Bit8u  magic1; /* 0x55 */
	Bit8u  magic2; /* 0xaa */
} GCC_ATTRIBUTE(packed);

struct direntry {
	Bit8u entryname[11];
	Bit8u attrib;
	Bit8u NTRes;
	Bit8u milliSecondStamp;
	Bit16u crtTime;
	Bit16u crtDate;
	Bit16u accessDate;
	Bit16u hiFirstClust;
	Bit16u modTime;
	Bit16u modDate;
	Bit16u loFirstClust;
	Bit32u entrysize;
} GCC_ATTRIBUTE(packed);

struct partTable {
	Bit8u booter[446];
	struct {
		Bit8u bootflag;
		Bit8u beginchs[3];
		Bit8u parttype;
		Bit8u endchs[3];
		Bit32u absSectStart;
		Bit32u partSize;
	} pentry[4];
	Bit8u  magic1; /* 0x55 */
	Bit8u  magic2; /* 0xaa */
} GCC_ATTRIBUTE(packed);

#ifdef _MSC_VER
#pragma pack ()
#endif
//Forward
class imageDisk;
class fatDrive : public DOS_Drive {
public:
	fatDrive(const char * sysFilename, Bit32u bytesector, Bit32u cylsector, Bit32u headscyl, Bit32u cylinders, Bit32u startSector);
	~fatDrive();
	virtual bool FileOpen(DOS_File * * file,char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file,char * name,Bit16u attributes);
	virtual bool FileUnlink(char * name);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
	virtual bool TestDir(char * dir);
	virtual bool FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst=false);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool GetFileAttr(char * name,Bit16u * attr);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool AllocationInfo(Bit16u * _bytes_sector,Bit8u * _sectors_cluster,Bit16u * _total_clusters,Bit16u * _free_clusters);
	virtual bool FileExists(const char* name);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual Bit8u GetMediaByte(void);
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
	virtual void EmptyCache(void){}
public:
	Bit8u readSector(Bit32u sectnum, void * data);
	Bit8u writeSector(Bit32u sectnum, void * data);
	Bit32u getAbsoluteSectFromBytePos(Bit32u startClustNum, Bit32u bytePos);
	Bit32u getSectorCount(void);
	Bit32u getSectorSize(void);
	Bit32u getClusterSize(void);
	Bit32u getAbsoluteSectFromChain(Bit32u startClustNum, Bit32u logicalSector);
	bool allocateCluster(Bit32u useCluster, Bit32u prevCluster);
	Bit32u appendCluster(Bit32u startCluster);
	void deleteClustChain(Bit32u startCluster, Bit32u bytePos);
	Bit32u getFirstFreeClust(void);
	bool directoryBrowse(Bit32u dirClustNumber, direntry *useEntry, Bit32s entNum, Bit32s start=0);
	bool directoryChange(Bit32u dirClustNumber, direntry *useEntry, Bit32s entNum);
	imageDisk *loadedDisk;
	bool created_successfully;
	Bit32u partSectOff;
private:
	Bit32u getClusterValue(Bit32u clustNum);
	void setClusterValue(Bit32u clustNum, Bit32u clustValue);
	Bit32u getClustFirstSect(Bit32u clustNum);
	bool FindNextInternal(Bit32u dirClustNumber, DOS_DTA & dta, direntry *foundEntry);
	bool getDirClustNum(char * dir, Bit32u * clustNum, bool parDir);
	bool getFileDirEntry(char const * const filename, direntry * useEntry, Bit32u * dirClust, Bit32u * subEntry);
	bool addDirectoryEntry(Bit32u dirClustNumber, direntry useEntry);
	void zeroOutCluster(Bit32u clustNumber);
	bool getEntryName(char *fullname, char *entname);
	friend void DOS_Shell::CMD_SUBST(char* args); 	
	struct {
		char srch_dir[CROSS_LEN];
	} srchInfo[MAX_OPENDIRS];

	struct {
		Bit16u bytes_sector;
		Bit8u sectors_cluster;
		Bit16u total_clusters;
		Bit16u free_clusters;
		Bit8u mediaid;
	} allocation;
	
	bootstrap bootbuffer;
	bool absolute;
	Bit8u fattype;
	Bit32u CountOfClusters;
	Bit32u firstDataSector;
	Bit32u firstRootDirSect;

	Bit32u cwdDirCluster;
	Bit32u dirPosition; /* Position in directory search */

	Bit8u fatSectBuffer[1024];
	Bit32u curFatSect;
};


class cdromDrive : public localDrive
{
public:
	cdromDrive(const char driveLetter, const char * startdir,Bit16u _bytes_sector,Bit8u _sectors_cluster,Bit16u _total_clusters,Bit16u _free_clusters,Bit8u _mediaid, int& error);
	virtual bool FileOpen(DOS_File * * file,char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file,char * name,Bit16u attributes);
	virtual bool FileUnlink(char * name);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool GetFileAttr(char * name,Bit16u * attr);
	virtual bool FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst=false);
	virtual void SetDir(const char* path);
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
private:
	Bit8u subUnit;
	char driveLetter;
};

#ifdef _MSC_VER
#pragma pack (1)
#endif
struct isoPVD {
	Bit8u type;
	Bit8u standardIdent[5];
	Bit8u version;
	Bit8u unused1;
	Bit8u systemIdent[32];
	Bit8u volumeIdent[32];
	Bit8u unused2[8];
	Bit32u volumeSpaceSizeL;
	Bit32u volumeSpaceSizeM;
	Bit8u unused3[32];
	Bit16u volumeSetSizeL;
	Bit16u volumeSetSizeM;
	Bit16u volumeSeqNumberL;
	Bit16u volumeSeqNumberM;
	Bit16u logicBlockSizeL;
	Bit16u logicBlockSizeM;
	Bit32u pathTableSizeL;
	Bit32u pathTableSizeM;
	Bit32u locationPathTableL;
	Bit32u locationOptPathTableL;
	Bit32u locationPathTableM;
	Bit32u locationOptPathTableM;
	Bit8u rootEntry[34];
	Bit32u unused4[1858];
} GCC_ATTRIBUTE(packed);

struct isoDirEntry {
	Bit8u length;
	Bit8u extAttrLength;
	Bit32u extentLocationL;
	Bit32u extentLocationM;
	Bit32u dataLengthL;
	Bit32u dataLengthM;
	Bit8u dateYear;
	Bit8u dateMonth;
	Bit8u dateDay;
	Bit8u timeHour;
	Bit8u timeMin;
	Bit8u timeSec;
	Bit8u timeZone;
	Bit8u fileFlags;
	Bit8u fileUnitSize;
	Bit8u interleaveGapSize;
	Bit16u VolumeSeqNumberL;
	Bit16u VolumeSeqNumberM;
	Bit8u fileIdentLength;
	Bit8u ident[222];
} GCC_ATTRIBUTE(packed);

#ifdef _MSC_VER
#pragma pack ()
#endif

#if defined (WORDS_BIGENDIAN)
#define EXTENT_LOCATION(de)	((de).extentLocationM)
#define DATA_LENGTH(de)		((de).dataLengthM)
#else
#define EXTENT_LOCATION(de)	((de).extentLocationL)
#define DATA_LENGTH(de)		((de).dataLengthL)
#endif

#define ISO_FRAMESIZE		2048
#define ISO_ASSOCIATED		4
#define ISO_DIRECTORY		2
#define ISO_HIDDEN		1
#define ISO_MAX_FILENAME_LENGTH 37
#define ISO_MAXPATHNAME		256
#define ISO_FIRST_VD		16
#define IS_ASSOC(fileFlags)	(fileFlags & ISO_ASSOCIATED)
#define IS_DIR(fileFlags)	(fileFlags & ISO_DIRECTORY)
#define IS_HIDDEN(fileFlags)	(fileFlags & ISO_HIDDEN)
#define ISO_MAX_HASH_TABLE_SIZE 	100

class isoDrive : public DOS_Drive {
public:
	isoDrive(char driveLetter, const char* device_name, Bit8u mediaid, int &error);
	~isoDrive();
	virtual bool FileOpen(DOS_File **file, char *name, Bit32u flags);
	virtual bool FileCreate(DOS_File **file, char *name, Bit16u attributes);
	virtual bool FileUnlink(char *name);
	virtual bool RemoveDir(char *dir);
	virtual bool MakeDir(char *dir);
	virtual bool TestDir(char *dir);
	virtual bool FindFirst(char *_dir, DOS_DTA &dta, bool fcb_findfirst);
	virtual bool FindNext(DOS_DTA &dta);
	virtual bool GetFileAttr(char *name, Bit16u *attr);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool AllocationInfo(Bit16u *bytes_sector, Bit8u *sectors_cluster, Bit16u *total_clusters, Bit16u *free_clusters);
	virtual bool FileExists(const char *name);
   	virtual bool FileStat(const char *name, FileStat_Block *const stat_block);
	virtual bool GetLongFileName(const char* name, char longname[256]);
	virtual Bit8u GetMediaByte(void);
	virtual void EmptyCache(void){}
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
	bool readSector(Bit8u *buffer, Bit32u sector);
	virtual char const* GetLabel(void) {return discLabel;};
	virtual void Activate(void);
	bool CheckBootDiskImage(Bit8u** read_image = NULL, Bit32u* read_size = NULL);
	#ifdef C_DBP_ENABLE_IDE
	class CDROM_Interface* GetInterface();
	#endif
private:
	int  readDirEntry(isoDirEntry *de, Bit8u *data);
	bool loadImage();
	bool lookupSingle(isoDirEntry *de, const char *name, Bit32u sectorStart, Bit32u length);
	bool lookup(isoDirEntry *de, const char *path);
	int  UpdateMscdex(char driveLetter, const char* physicalPath, Bit8u& subUnit);
	int  GetDirIterator(const isoDirEntry* de);
	bool GetNextDirEntry(const int dirIterator, isoDirEntry* de);
	void FreeDirIterator(const int dirIterator);
	bool ReadCachedSector(Bit8u** buffer, const Bit32u sector);
	
	struct DirIterator {
		bool valid;
		bool root;
		Bit32u currentSector;
		Bit32u endSector;
		Bit32u pos;
	} dirIterators[MAX_OPENDIRS];
	
	int nextFreeDirIterator;
	
	struct SectorHashEntry {
		bool valid;
		Bit32u sector;
		Bit8u data[ISO_FRAMESIZE];
	} sectorHashEntries[ISO_MAX_HASH_TABLE_SIZE];

	bool iso;
	bool dataCD;
	isoDirEntry rootEntry;
	Bit8u mediaid;
	char fileName[CROSS_LEN];
	Bit8u subUnit;
	char driveLetter;
	char discLabel[32];
};

struct VFILE_Block;

class Virtual_Drive: public DOS_Drive {
public:
	Virtual_Drive();
	~Virtual_Drive();
	bool FileOpen(DOS_File * * file,char * name,Bit32u flags);
	bool FileCreate(DOS_File * * file,char * name,Bit16u attributes);
	bool FileUnlink(char * name);
	bool RemoveDir(char * dir);
	bool MakeDir(char * dir);
	bool TestDir(char * dir);
	bool FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst);
	bool FindNext(DOS_DTA & dta);
	bool GetFileAttr(char * name,Bit16u * attr);
	bool Rename(char * oldname,char * newname);
	bool AllocationInfo(Bit16u * _bytes_sector,Bit8u * _sectors_cluster,Bit16u * _total_clusters,Bit16u * _free_clusters);
	bool FileExists(const char* name);
	bool FileStat(const char* name, FileStat_Block* const stat_block);
	Bit8u GetMediaByte(void);
	void EmptyCache(void){}
	bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
	virtual char const* GetLabel(void);
private:
	VFILE_Block * search_file;
};

#ifdef C_DBP_NATIVE_OVERLAY
class Overlay_Drive: public localDrive {
public:
	Overlay_Drive(const char * startdir,const char* overlay, Bit16u _bytes_sector,Bit8u _sectors_cluster,Bit16u _total_clusters,Bit16u _free_clusters,Bit8u _mediaid,Bit8u &error);

	virtual bool FileOpen(DOS_File * * file,char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file,char * name,Bit16u /*attributes*/);
	virtual bool FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool FileUnlink(char * name);
	virtual bool GetFileAttr(char * name,Bit16u * attr);
	virtual bool FileExists(const char* name);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual void EmptyCache(void);

	FILE* create_file_in_overlay(char* dos_filename, char const* mode);
	virtual Bits UnMount(void);
	virtual bool TestDir(char * dir);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
private:
	char overlaydir[CROSS_LEN];
	bool optimize_cache_v1;
	bool Sync_leading_dirs(const char* dos_filename);
	void add_DOSname_to_cache(const char* name);
	void remove_DOSname_from_cache(const char* name);
	void add_DOSdir_to_cache(const char* name);
	void remove_DOSdir_from_cache(const char* name);
	void update_cache(bool read_directory_contents = false);
	
	std::vector<std::string> deleted_files_in_base; //Set is probably better, or some other solution (involving the disk).
	std::vector<std::string> deleted_paths_in_base; //Currently only used to hide the overlay folder.
	std::string overlap_folder;
	void add_deleted_file(const char* name, bool create_on_disk);
	void remove_deleted_file(const char* name, bool create_on_disk);
	bool is_deleted_file(const char* name);
	void add_deleted_path(const char* name, bool create_on_disk);
	void remove_deleted_path(const char* name, bool create_on_disk);
	bool is_deleted_path(const char* name);
	bool check_if_leading_is_deleted(const char* name);

	bool is_dir_only_in_overlay(const char* name); //cached


	void remove_special_file_from_disk(const char* dosname, const char* operation);
	void add_special_file_to_disk(const char* dosname, const char* operation);
	std::string create_filename_of_special_operation(const char* dosname, const char* operation);
	void convert_overlay_to_DOSname_in_base(char* dirname );
	//For caching the update_cache routine.
	std::vector<std::string> DOSnames_cache; //Also set is probably better.
	std::vector<std::string> DOSdirs_cache; //Can not blindly change its type. it is important that subdirs come after the parent directory.
	const std::string special_prefix;
};
#endif /* C_DBP_NATIVE_OVERLAY */

//DBP: New drive types
#define FALSE_SET_DOSERR(ERRNAME) (dos.errorcode = (DOSERR_##ERRNAME), false)
#define DOSPATH_REMOVE_ENDINGDOTS(VAR) char VAR##_buf[DOS_PATHLENGTH]; DrivePathRemoveEndingDots((const char**)&VAR, VAR##_buf)
#define DOSPATH_REMOVE_ENDINGDOTS_KEEP(VAR) const char* VAR##_org = VAR; DOSPATH_REMOVE_ENDINGDOTS(VAR)
void DrivePathRemoveEndingDots(const char** path, char path_buf[DOS_PATHLENGTH]);
Bit8u DriveGetIndex(DOS_Drive* drv); // index in Drives array, returns DOS_DRIVES if not found
bool DriveForceCloseFile(DOS_Drive* drv, const char* name);
bool DriveFindDriveVolume(DOS_Drive* drv, char* dir_path, DOS_DTA & dta, bool fcb_findfirst);
Bit32u DBP_Make8dot3FileName(char* target, Bit32u target_len, const char* source, Bit32u source_len);
DOS_File *FindAndOpenDosFile(char const* filename, Bit32u *bsize = NULL, bool* writable = NULL, char const* relative_to = NULL);
bool ReadAndClose(DOS_File *df, std::string& out, Bit32u maxsize = 1024*1024);
Bit16u DriveReadFileBytes(DOS_Drive* drv, const char* path, Bit8u* outbuf, Bit16u numbytes);
bool DriveCreateFile(DOS_Drive* drv, const char* path, const Bit8u* buf, Bit32u numbytes);
Bit32u DriveCalculateCRC32(const Bit8u *ptr, size_t len, Bit32u crc = 0);
void DriveFileIterator(DOS_Drive* drv, void(*func)(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data), Bitu data = 0);

template <typename TVal> struct StringToPointerHashMap
{
	StringToPointerHashMap() : len(0), maxlen(0), keys(NULL), vals(NULL) { }
	~StringToPointerHashMap() { free(keys); free(vals); }

	static Bit32u Hash(const char* str, Bit32u str_limit = 0xFFFF, Bit32u hash_init = (Bit32u)0x811c9dc5)
	{
		for (const char* e = str + str_limit; *str && str != e;)
			hash_init = ((hash_init * (Bit32u)0x01000193) ^ (Bit32u)*(str++));
		return hash_init;
	}

	TVal* Get(const char* str, Bit32u str_limit = 0xFFFF, Bit32u hash_init = (Bit32u)0x811c9dc5) const
	{
		if (len == 0) return NULL;
		for (Bit32u key0 = Hash(str, str_limit, hash_init), key = (key0 ? key0 : 1), i = key;; i++)
		{
			if (keys[i &= maxlen] == key) return vals[i];
			if (!keys[i]) return NULL;
		}
	}

	void Put(const char* str, TVal* val, Bit32u str_limit = 0xFFFF, Bit32u hash_init = (Bit32u)0x811c9dc5)
	{
		if (len * 2 >= maxlen) Grow();
		for (Bit32u key0 = Hash(str, str_limit, hash_init), key = (key0 ? key0 : 1), i = key;; i++)
		{
			if (!keys[i &= maxlen]) { len++; keys[i] = key; vals[i] = val; return; }
			if (keys[i] == key) { vals[i] = val; return; }
		}
	}

	bool Remove(const char* str, Bit32u str_limit = 0xFFFF, Bit32u hash_init = (Bit32u)0x811c9dc5)
	{
		if (len == 0) return false;
		for (Bit32u key0 = Hash(str, str_limit, hash_init), key = (key0 ? key0 : 1), i = key;; i++)
		{
			if (keys[i &= maxlen] == key)
			{
				keys[i] = 0;
				len--;
				while ((key = keys[i = (i + 1) & maxlen]) != 0)
				{
					for (Bit32u j = key;; j++)
					{
						if (keys[j &= maxlen] == key) break;
						if (!keys[j]) { keys[i] = 0; keys[j] = key; vals[j] = vals[i]; break; }
					}
				}
				return true;
			}
			if (!keys[i]) return false;
		}
	}

	void Clear() { memset(keys, len = 0, (maxlen + 1) * sizeof(Bit32u)); }

	Bit32u Len() const { return len; }
	Bit32u Capacity() const { return (maxlen ? maxlen + 1 : 0); }
	TVal* GetAtIndex(Bit32u idx) const { return (keys[idx] ? vals[idx] : NULL); }

	struct Iterator
	{
		Iterator(StringToPointerHashMap<TVal>& _map, Bit32u _index) : map(_map), index(_index - 1) { this->operator++(); }
		StringToPointerHashMap<TVal>& map;
		Bit32u index;
		TVal* operator *() const { return map.vals[index]; }
		bool operator ==(const Iterator &other) const { return index == other.index; }
		bool operator !=(const Iterator &other) const { return index != other.index; }
		Iterator& operator ++()
		{
			if (!map.maxlen) { index = 0; return *this; }
			if (++index > map.maxlen) index = map.maxlen + 1;
			while (index <= map.maxlen && !map.keys[index]) index++;
			return *this;
		}
	};

	Iterator begin() { return Iterator(*this, 0); }
	Iterator end() { return Iterator(*this, (maxlen ? maxlen + 1 : 0)); }

private:
	Bit32u len, maxlen, *keys;
	TVal** vals;

	void Grow()
	{
		Bit32u oldMax = maxlen, oldCap = (maxlen ? oldMax + 1 : 0), *oldKeys = keys;
		TVal **oldVals = vals;
		maxlen  = (maxlen ? maxlen * 2 + 1 : 15);
		keys = (Bit32u*)calloc(maxlen + 1, sizeof(Bit32u));
		vals = (TVal**)malloc((maxlen + 1) * sizeof(TVal*));
		for (Bit32u i = 0; i != oldCap; i++)
		{
			if (!oldKeys[i]) continue;
			for (Bit32u key = oldKeys[i], j = key;; j++)
			{
				if (!keys[j &= maxlen]) { keys[j] = key; vals[j] = oldVals[i]; break; }
			}
		}
		free(oldKeys);
		free(oldVals);
	}

	// not copyable
	StringToPointerHashMap(const StringToPointerHashMap&);
	StringToPointerHashMap& operator=(const StringToPointerHashMap&);
};

//Used to load drive images and archives from the native filesystem not a DOS_Drive
struct rawFile : public DOS_File
{
	FILE* f;
	rawFile(FILE* _f, bool writable) : f(_f) { open = true; if (writable) flags |= OPEN_READWRITE; }
	~rawFile() { if (f) fclose(f); }
	virtual bool Close() { if (refCtr == 1) open = false; return true; }
	virtual bool Read(Bit8u* data, Bit16u* size) { *size = (Bit16u)fread(data, 1, *size, f); return open; }
	virtual bool Write(Bit8u* data, Bit16u* size) { if (!OPEN_IS_WRITING(flags)) return false; *size = (Bit16u)fwrite(data, 1, *size, f); return (*size && open); }
	virtual bool Seek(Bit32u* pos, Bit32u type) { fseek_wrap(f, *pos, type); *pos = (Bit32u)ftell_wrap(f); return open; }
	virtual bool Seek64(Bit64u* pos, Bit32u type) { fseek_wrap(f, *pos, type); *pos = (Bit64u)ftell_wrap(f); return open; }
	virtual Bit16u GetInformation(void) { return (OPEN_IS_WRITING(flags) ? 0x40 : 0); }
};

class memoryDrive : public DOS_Drive {
public:
	memoryDrive();
	virtual ~memoryDrive();
	virtual bool FileOpen(DOS_File * * file, char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file, char * name,Bit16u attributes);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool FileUnlink(char * name);
	virtual bool FileExists(const char* name);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
	virtual bool TestDir(char * dir);
	virtual bool FindFirst(char * dir, DOS_DTA & dta, bool fcb_findfirst=false);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual bool GetFileAttr(char * name, Bit16u * attr);
	virtual bool AllocationInfo(Bit16u * bytes_sector, Bit8u * sectors_cluster, Bit16u * total_clusters, Bit16u * free_clusters);
	virtual Bit8u GetMediaByte(void);
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);

	bool CloneEntry(DOS_Drive* src_drv, const char* src_path);
private:
	struct memoryDriveImpl* impl;
};

class zipDrive : public DOS_Drive {
public:
	zipDrive(DOS_File* zip, bool enter_solo_root_dir);
	virtual ~zipDrive();
	virtual bool FileOpen(DOS_File * * file, char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file, char * name,Bit16u attributes);
	virtual bool FileUnlink(char * name);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
	virtual bool TestDir(char * dir);
	virtual bool FindFirst(char * dir, DOS_DTA & dta, bool fcb_findfirst=false);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool FileExists(const char* name);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual bool GetFileAttr(char * name, Bit16u * attr);
	virtual bool GetLongFileName(const char* name, char longname[256]);
	virtual bool AllocationInfo(Bit16u * bytes_sector, Bit8u * sectors_cluster, Bit16u * total_clusters, Bit16u * free_clusters);
	virtual Bit8u GetMediaByte(void);
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
	static void Uncompress(const Bit8u* src, Bit32u src_len, Bit8u* trg, Bit32u trg_len);
private:
	struct zipDriveImpl* impl;
};

class unionDrive : public DOS_Drive {
public:
	unionDrive(DOS_Drive& under, DOS_Drive& over, bool autodelete_under = false, bool autodelete_over = false);
	unionDrive(DOS_Drive& under, const char* save_file = NULL, bool autodelete_under = false, bool strict_mode = false);
	void AddUnder(DOS_Drive& add_under, bool autodelete_under = false);
	virtual ~unionDrive();
	virtual bool FileOpen(DOS_File * * file, char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file, char * name,Bit16u attributes);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool FileUnlink(char * name);
	virtual bool FileExists(const char* name);
	virtual bool MakeDir(char * dir);
	virtual bool RemoveDir(char * dir);
	virtual bool TestDir(char * dir);
	virtual bool FindFirst(char * dir, DOS_DTA & dta, bool fcb_findfirst=false);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual bool GetFileAttr(char * name, Bit16u * attr);
	virtual bool GetLongFileName(const char* name, char longname[256]);
	virtual bool AllocationInfo(Bit16u * bytes_sector, Bit8u * sectors_cluster, Bit16u * total_clusters, Bit16u * free_clusters);
	virtual bool GetShadows(DOS_Drive*& a, DOS_Drive*& b);
	virtual Bit8u GetMediaByte(void);
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
private:
	struct unionDriveImpl* impl;
};

class patchDrive : public DOS_Drive {
public:
	patchDrive(DOS_Drive* under, bool autodelete_under, DOS_File* patchzip = NULL);
	virtual ~patchDrive();
	virtual bool FileOpen(DOS_File * * file, char * name,Bit32u flags);
	virtual bool FileCreate(DOS_File * * file, char * name,Bit16u attributes);
	virtual bool Rename(char * oldname,char * newname);
	virtual bool FileUnlink(char * name);
	virtual bool FileExists(const char* name);
	virtual bool RemoveDir(char * dir);
	virtual bool MakeDir(char * dir);
	virtual bool TestDir(char * dir);
	virtual bool FindFirst(char * dir, DOS_DTA & dta, bool fcb_findfirst=false);
	virtual bool FindNext(DOS_DTA & dta);
	virtual bool FileStat(const char* name, FileStat_Block * const stat_block);
	virtual bool GetFileAttr(char * name, Bit16u * attr);
	virtual bool AllocationInfo(Bit16u * bytes_sector, Bit8u * sectors_cluster, Bit16u * total_clusters, Bit16u * free_clusters);
	virtual bool GetShadows(DOS_Drive*& a, DOS_Drive*& b);
	virtual Bit8u GetMediaByte(void);
	virtual bool isRemote(void);
	virtual bool isRemovable(void);
	virtual Bits UnMount(void);
private:
	struct patchDriveImpl* impl;
};

#endif
