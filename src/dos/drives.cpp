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


#include "dosbox.h"
#include "dos_system.h"
#include "drives.h"
#include "bios_disk.h"
#include "mapper.h"
#include "support.h"
#include "setup.h"

bool WildFileCmp(const char * file, const char * wild) 
{
	char file_name[9];
	char file_ext[4];
	char wild_name[9];
	char wild_ext[4];
	const char * find_ext;
	Bitu r;

	strcpy(file_name,"        ");
	strcpy(file_ext,"   ");
	strcpy(wild_name,"        ");
	strcpy(wild_ext,"   ");

	find_ext=strrchr(file,'.');
	if (find_ext) {
		Bitu size=(Bitu)(find_ext-file);
		if (size>8) size=8;
		memcpy(file_name,file,size);
		find_ext++;
		memcpy(file_ext,find_ext,(strlen(find_ext)>3) ? 3 : strlen(find_ext)); 
	} else {
		memcpy(file_name,file,(strlen(file) > 8) ? 8 : strlen(file));
	}
	upcase(file_name);upcase(file_ext);
	find_ext=strrchr(wild,'.');
	if (find_ext) {
		Bitu size=(Bitu)(find_ext-wild);
		if (size>8) size=8;
		memcpy(wild_name,wild,size);
		find_ext++;
		memcpy(wild_ext,find_ext,(strlen(find_ext)>3) ? 3 : strlen(find_ext));
	} else {
		memcpy(wild_name,wild,(strlen(wild) > 8) ? 8 : strlen(wild));
	}
	upcase(wild_name);upcase(wild_ext);
	/* Names are right do some checking */
	r=0;
	while (r<8) {
		if (wild_name[r]=='*') goto checkext;
		if (wild_name[r]!='?' && wild_name[r]!=file_name[r]) return false;
		r++;
	}
checkext:
    r=0;
	while (r<3) {
		if (wild_ext[r]=='*') return true;
		if (wild_ext[r]!='?' && wild_ext[r]!=file_ext[r]) return false;
		r++;
	}
	return true;
}

void Set_Label(char const * const input, char * const output, bool cdrom) {
	Bitu togo     = 8;
	Bitu vnamePos = 0;
	Bitu labelPos = 0;
	bool point    = false;

	//spacepadding the filenamepart to include spaces after the terminating zero is more closely to the specs. (not doing this now)
	// HELLO\0' '' '

	while (togo > 0) {
		if (input[vnamePos]==0) break;
		if (!point && (input[vnamePos]=='.')) {	togo=4; point=true; }

		//another mscdex quirk. Label is not always uppercase. (Daggerfall)
		output[labelPos] = (cdrom?input[vnamePos]:toupper(input[vnamePos]));

		labelPos++; vnamePos++;
		togo--;
		if ((togo==0) && !point) {
			if (input[vnamePos]=='.') vnamePos++;
			output[labelPos]='.'; labelPos++; point=true; togo=3;
		}
	};
	output[labelPos]=0;

	//Remove trailing dot. except when on cdrom and filename is exactly 8 (9 including the dot) letters. MSCDEX feature/bug (fifa96 cdrom detection)
	if((labelPos > 0) && (output[labelPos-1] == '.') && !(cdrom && labelPos ==9))
		output[labelPos-1] = 0;
}



DOS_Drive::DOS_Drive() {
	curdir[0]=0;
	info[0]=0;
}

//DBP: Added these helper utility functions
void DOS_Drive::ForceCloseAll() {
	Bit8u i, drive = DOS_DRIVES;
	for (i = 0; i < DOS_DRIVES; i++) {
		if (Drives[i] == this) {
			drive = i;
			break;
		}
	}
	if (drive != DOS_DRIVES) {
		for (i = 0; i < DOS_FILES; i++) {
			if (Files[i] && Files[i]->GetDrive() == drive) {
				DBP_ASSERT((Files[i]->refCtr > 0) == Files[i]->open); // closed files can hang around while the DOS program still holds the handle
				while (Files[i]->refCtr > 0) { if (Files[i]->IsOpen()) Files[i]->Close(); Files[i]->RemoveRef(); }
				delete Files[i];
				Files[i] = NULL;
			}
		}
	}
}

// static members variables
int DriveManager::currentDrive;
DriveManager::DriveInfo DriveManager::driveInfos[26];

void DriveManager::AppendDisk(int drive, DOS_Drive* disk) {
	driveInfos[drive].disks.push_back(disk);
}

void DriveManager::InitializeDrive(int drive) {
	currentDrive = drive;
	DriveInfo& driveInfo = driveInfos[currentDrive];
	if (driveInfo.disks.size() > 0) {
		driveInfo.currentDisk = 0;
		DOS_Drive* disk = driveInfo.disks[driveInfo.currentDisk];
		Drives[currentDrive] = disk;
		if (driveInfo.disks.size() > 1) disk->Activate();
	}
}

/*
void DriveManager::CycleDrive(bool pressed) {
	if (!pressed) return;
		
	// do one round through all drives or stop at the next drive with multiple disks
	int oldDrive = currentDrive;
	do {
		currentDrive = (currentDrive + 1) % DOS_DRIVES;
		int numDisks = driveInfos[currentDrive].disks.size();
		if (numDisks > 1) break;
	} while (currentDrive != oldDrive);
}

void DriveManager::CycleDisk(bool pressed) {
	if (!pressed) return;
	
	int numDisks = driveInfos[currentDrive].disks.size();
	if (numDisks > 1) {
		// cycle disk
		int currentDisk = driveInfos[currentDrive].currentDisk;
		DOS_Drive* oldDisk = driveInfos[currentDrive].disks[currentDisk];
		currentDisk = (currentDisk + 1) % numDisks;		
		DOS_Drive* newDisk = driveInfos[currentDrive].disks[currentDisk];
		driveInfos[currentDrive].currentDisk = currentDisk;
		
		// copy working directory, acquire system resources and finally switch to next drive		
		strcpy(newDisk->curdir, oldDisk->curdir);
		newDisk->Activate();
		Drives[currentDrive] = newDisk;
	}
}
*/

void DriveManager::CycleDisks(int drive, bool notify) {
	int numDisks = (int)driveInfos[drive].disks.size();
	if (numDisks > 1) {
		// cycle disk
		int currentDisk = driveInfos[drive].currentDisk;
		DOS_Drive* oldDisk = driveInfos[drive].disks[currentDisk];
		currentDisk = (currentDisk + 1) % numDisks;		
		DOS_Drive* newDisk = driveInfos[drive].disks[currentDisk];
		driveInfos[drive].currentDisk = currentDisk;
		if (drive < MAX_DISK_IMAGES && imageDiskList[drive] != NULL) {
			if (strncmp(newDisk->GetInfo(),"fatDrive",8) == 0)
				imageDiskList[drive] = ((fatDrive *)newDisk)->loadedDisk;
			else
				imageDiskList[drive] = (imageDisk *)newDisk;
			if ((drive == 2 || drive == 3) && imageDiskList[drive]->hardDrive) updateDPT();
		}

		// copy working directory, acquire system resources and finally switch to next drive		
		strcpy(newDisk->curdir, oldDisk->curdir);
		newDisk->Activate();
		Drives[drive] = newDisk;
		if (notify) LOG_MSG("Drive %c: disk %d of %d now active", 'A'+drive, currentDisk+1, numDisks);
	}
}

void DriveManager::CycleAllDisks(void) {
	for (int idrive=0; idrive<DOS_DRIVES; idrive++) CycleDisks(idrive, true);
}

int DriveManager::UnmountDrive(int drive) {
	int result = 0;
	// unmanaged drive
	if (driveInfos[drive].disks.size() == 0) {
		result = Drives[drive]->UnMount();
	} else {
		// managed drive
		int currentDisk = driveInfos[drive].currentDisk;
		result = driveInfos[drive].disks[currentDisk]->UnMount();
		// only delete on success, current disk set to NULL because of UnMount
		if (result == 0) {
			driveInfos[drive].disks[currentDisk] = NULL;
			for (int i = 0; i < (int)driveInfos[drive].disks.size(); i++) {
				delete driveInfos[drive].disks[i];
			}
			driveInfos[drive].disks.clear();
		}
	}
	
	return result;
}

void DriveManager::Init(Section* /* sec */) {
	
	// setup driveInfos structure
	currentDrive = 0;
	for(int i = 0; i < DOS_DRIVES; i++) {
		driveInfos[i].currentDisk = 0;
	}
	
//	MAPPER_AddHandler(&CycleDisk, MK_f3, MMOD1, "cycledisk", "Cycle Disk");
//	MAPPER_AddHandler(&CycleDrive, MK_f3, MMOD2, "cycledrive", "Cycle Drv");
}

//DBP: memory cleanup for restart
static void DRIVES_ShutDown(Section* /*sec*/) {
	// this needs to run before MSCDEX_ShutDown and DOS_ShutDown
	bool MSCDEX_HasDrive(char);
	DBP_ASSERT(MSCDEX_HasDrive(-1) == false); // this fails had mscdex already shut down (confirm MSCDEX_ShutDown not yet called)
	DBP_ASSERT(Drives['Z'-'A']->TestDir((char*)"")); // virtual drive still needs to exist (confirm DOS_ShutDown not yet called)

	// need to make sure this is called before drives are deleted as it may contain disks mounted from files managed by drives
	void BIOS_ShutdownDisks(void);
	BIOS_ShutdownDisks();

	void IDE_ShutdownControllers(void);
	IDE_ShutdownControllers();

	// unmount image file based drives first (they could be mounted from other mounted drives)
	for (Bit8u i = 0; i < DOS_DRIVES; i++)
		if (Drives[i] && (dynamic_cast<fatDrive*>(Drives[i]) || dynamic_cast<isoDrive*>(Drives[i])) && DriveManager::UnmountDrive(i) == 0)
			Drives[i] = NULL;
	for (Bit8u i = 0; i < DOS_DRIVES; i++)
		if (Drives[i] && DriveManager::UnmountDrive(i) == 0)
			Drives[i] = NULL;
}

void DRIVES_Init(Section* sec) {
	DriveManager::Init(sec);
	sec->AddDestroyFunction(&DRIVES_ShutDown,false);
}

//DBP: Added these helper utility functions
void DrivePathRemoveEndingDots(const char** path, char path_buf[DOS_PATHLENGTH])
{
	// Remove trailing dots that aren't at the start or in a series of dots
	// I.e "aaa.\bbb.\.\..\ccc." becomes "aaa\bbb\.\..\ccc"
	const char* dot = *path - 2;
	if (!dot[2] || !dot[3]) return;
	while ((dot = strchr(dot + 3, '.')) != NULL)
	{
		if (dot[1] != '\\' && dot[1] != '\0') continue;
		if (dot[-1] == '\\' || dot[-1] == '.') { dot--; continue; }
		const char* last = *path;
		for (char* out = path_buf;;)
		{
			if (dot - *path >= DOS_PATHLENGTH) return;
			memcpy(out, last, dot - last);
			out += (dot - last);
			if (!dot[0] || !dot[1])
			{
				*out = '\0';
				*path = path_buf;
				return;
			}
			last = dot + 1;
			for (;;)
			{
				dot = strchr(dot + 3, '.');
				if (!dot) dot = last + strlen(last);
				else if (dot[1] != '\\' && dot[1] != '\0') continue;
				else if (dot[-1] == '\\' || dot[-1] == '.') { dot--; continue; }
				break;
			}
		}
	}
}

Bit8u DriveGetIndex(DOS_Drive* drv)
{
	struct Local { static bool Compare(DOS_Drive *outer, DOS_Drive *inner)
	{
		DOS_Drive *a, *b;
		return (outer == inner || (outer->GetShadows(a, b) && (Compare(a, inner) || Compare(b, inner))));
	}};
	for (Bit8u i = 0; i < DOS_DRIVES; i++) if (Drives[i] && Local::Compare(Drives[i], drv)) return i;
	return DOS_DRIVES;
}

bool DriveForceCloseFile(DOS_Drive* drv, const char* name)
{
	Bit8u drive = DriveGetIndex(drv);
	if (drive == DOS_DRIVES) return false;
	DOSPATH_REMOVE_ENDINGDOTS(name);
	bool found_file = false;
	for (Bit8u i = 0; i < DOS_FILES; i++) {
		DOS_File *f = Files[i];
		if (!f || f->GetDrive() != drive || !f->name) continue;
		const char* fname = f->name;
		DOSPATH_REMOVE_ENDINGDOTS(fname);
		if (strcasecmp(name, fname)) continue;
		DBP_ASSERT((Files[i]->refCtr > 0) == Files[i]->open); // closed files can hang around while the DOS program still holds the handle
		while (f->refCtr > 0) { if (f->IsOpen()) f->Close(); f->RemoveRef(); }
		found_file = true;
	}
	return found_file;
}

bool DriveFindDriveVolume(DOS_Drive* drv, char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	Bit8u attr;char pattern[DOS_NAMELENGTH_ASCII];const char* label;
	dta.GetSearchParams(attr,pattern);
	if (!(attr & DOS_ATTR_VOLUME) || !*(label = drv->GetLabel())) return false;
	if ((attr & ~DOS_ATTR_VOLUME) && (*dir_path || fcb_findfirst || !WildFileCmp(label, pattern))) return false;
	dta.SetResult(label,0,0,0,DOS_ATTR_VOLUME);
	return true;
}

Bit32u DBP_Make8dot3FileName(char* target, Bit32u target_len, const char* source, Bit32u source_len)
{
	struct Func
	{
		static void AppendFiltered(char*& trg, const char* trg_end, const char* src, Bit32u len)
		{
			char DOS_ToUpperAndFilter(char c);
			for (; trg < trg_end && len--; trg++)
				*trg = DOS_ToUpperAndFilter(*(src++));
		}
	};
	const char *target_start = target, *target_end = target + target_len, *source_end = source + source_len, *sDot;
	for (sDot = source_end - 1; *sDot != '.' && sDot > source; sDot--);
	Bit32u baseLen = (Bit32u)((*sDot == '.' ? sDot : source_end) - source);
	Bit32u extLen = (Bit32u)(*sDot == '.' ? source_end - 1 - sDot : 0);
	if (baseLen <= 8 && extLen <= 3 && target_len >= source_len)
	{
		extern const Bit8u DOS_ValidCharBits[32];
		for (const char* p = source; p != source_end; p++)
			if (!(DOS_ValidCharBits[((Bit8u)*p)/8] & (1<<(((Bit8u)*p)%8))) && p != sDot)
				goto need_filter;
		memcpy(target, source, source_len);
		return source_len;
		need_filter:;
	}
	Bit32u baseLeft = (baseLen > 8 ? 4 : baseLen), baseRight = (baseLen > 8 ? 4 : 0);
	Func::AppendFiltered(target, target_end, source, baseLeft);
	Func::AppendFiltered(target, target_end, source + baseLen - baseRight, baseRight);
	if (!baseLen && target < target_end) *(target++) = '-';
	if (extLen && target < target_end) *(target++) = '.';
	Func::AppendFiltered(target, target_end, sDot + 1, (extLen > 3 ? 3 : extLen));
	return (Bit32u)(target - target_start);
}

DOS_File *FindAndOpenDosFile(char const* filename, Bit32u *bsize, bool* writable, char const* relative_to)
{
	if (!filename || !*filename) return NULL;
	if (relative_to && *relative_to)
	{
		const char *lastfs = strrchr(relative_to, '/'), *lastbs = strrchr(relative_to, '\\');
		const char *delim = (lastfs > lastbs ? lastfs : lastbs);
		if (!delim && relative_to[1] == ':') delim = relative_to + 1;
		if (delim)
		{
			std::string merge;
			merge.append(relative_to, delim + 1 - relative_to).append(filename);
			DOS_File *merge_file = FindAndOpenDosFile(merge.c_str(), bsize, writable);
			if (merge_file) return merge_file;
		}
	}

	bool force_mounted = (filename[0] == '$');
	if (force_mounted) filename++;

	DOS_File *dos_file;
	Bit8u drive = (filename[1] == ':' ? ((filename[0]|0x20)-'a') : (control ? DOS_GetDefaultDrive() : DOS_DRIVES));
	if (drive < DOS_DRIVES && Drives[drive])
	{
		char dos_path[DOS_PATHLENGTH + 2], *p_dos = dos_path, *p_dos_end = p_dos + DOS_PATHLENGTH;
		const char* n = filename + (filename[1] == ':' ? 2 : 0);
		if (*n == '\\' || *n == '/') n++; // absolute path
		else if (*Drives[drive]->curdir) // relative path
		{
			strcpy(p_dos, Drives[drive]->curdir);
			p_dos += strlen(p_dos);
		}
		bool transformed = (p_dos != dos_path);
		if (!transformed)
		{
			// try open path untransformed (works on localDrive and with paths without long file names)
			if (writable && Drives[drive]->FileOpen(&dos_file, (char*)n, OPEN_READWRITE))
				goto get_file_size;
			if (Drives[drive]->FileOpen(&dos_file, (char*)n, OPEN_READ))
				goto get_file_size_write_protected;
		}
		for (const char *nDir = n, *nEnd = n + strlen(n); n != nEnd + 1 && p_dos < p_dos_end; nDir = ++n)
		{
			while (*n != '/' && *n != '\\' && n != nEnd) n++;
			if (n == nDir || (nDir[0] == '.' && n == nDir + 1)) { transformed = true; continue; }
			if (nDir[0] == '.' && nDir[1] == '.' && n == nDir + 2)
			{
				// Remove the last parent directory in dos_path on ..
				transformed = true;
				while (p_dos > dos_path && *(--p_dos) != '\\') {}
				continue;
			}

			// Create a 8.3 filename from a 4 char prefix and a suffix if filename is too long
			if (p_dos != dos_path) *(p_dos++) = '\\';
			Bit32u nLen = (Bit32u)(n - nDir), tLen = DBP_Make8dot3FileName(p_dos, (Bit32u)(p_dos_end - p_dos), nDir, nLen);
			transformed = transformed || (tLen != nLen) || memcmp(p_dos, nDir, nLen) || *n == '/';
			p_dos += tLen;
		}
		if (transformed)
		{
			*p_dos = '\0';
			if (writable && Drives[drive]->FileOpen(&dos_file, dos_path, OPEN_READWRITE))
				goto get_file_size;
			if (Drives[drive]->FileOpen(&dos_file, dos_path, OPEN_READ))
				goto get_file_size_write_protected;
		}
	}

	if (!force_mounted) {
		//File not found on mounted filesystem. Try regular filesystem
		std::string filename_s(filename);
		Cross::ResolveHomedir(filename_s);
		#ifdef C_DBP_HAVE_FPATH_NOCASE
		if (!fpath_nocase(&filename_s[0])) return NULL;
		#endif
		FILE* raw_file_h;
		if (writable && (raw_file_h = fopen_wrap(filename_s.c_str(), "rb+")) != NULL) {
			dos_file = new rawFile(raw_file_h, true);
			goto get_file_size;
		}
		if ((raw_file_h = fopen_wrap(filename_s.c_str(), "rb")) != NULL) {
			dos_file = new rawFile(raw_file_h, false);
			goto get_file_size_write_protected;
		}
	}
	return NULL;

	get_file_size_write_protected:
	if (writable) { *writable = false; writable = NULL; }
	get_file_size:
	if (writable) *writable = true;
	dos_file->AddRef();
	if (bsize) {
		bool can_seek = dos_file->Seek(&(*bsize = 0), DOS_SEEK_END);
		DBP_ASSERT(can_seek);
		Bit32u seekzero = 0;
		dos_file->Seek(&seekzero, DOS_SEEK_SET);
	}
	return dos_file;
}

bool ReadAndClose(DOS_File *df, std::string& out, Bit32u maxsize)
{
	if (!df) return false;
	Bit32u curlen = (Bit32u)out.size(), filesize = 0, seekzero = 0;
	df->Seek(&filesize, DOS_SEEK_END);
	df->Seek(&seekzero, DOS_SEEK_SET);
	if (!filesize || filesize > maxsize) { df->Close(); delete df; return false; }
	out.resize(curlen + filesize);
	Bit8u* buf = (Bit8u*)&out[curlen];
	for (Bit16u read; filesize; filesize -= read, buf += read)
	{
		read = (Bit16u)(filesize > 0xFFFF ? 0xFFFF : filesize);
		if (!df->Read(buf, &read)) { DBP_ASSERT(0); }
	}
	df->Close();
	delete df;
	return true;
}

Bit16u DriveReadFileBytes(DOS_Drive* drv, const char* path, Bit8u* outbuf, Bit16u numbytes)
{
	if (!drv) return 0;
	DOS_File *df = nullptr;
	if (!drv->FileOpen(&df, (char*)path, OPEN_READ)) return 0;
	df->AddRef();
	if (!df->Read(outbuf, &numbytes)) numbytes = 0;
	df->Close();
	delete df;
	return numbytes;
}

bool DriveCreateFile(DOS_Drive* drv, const char* path, const Bit8u* buf, Bit32u numbytes)
{
	DOS_File *df;
	if (!drv || !drv->FileCreate(&df, (char*)path, DOS_ATTR_ARCHIVE)) return false;
	df->AddRef();
	for (Bit16u wrote; numbytes; numbytes -= wrote, buf += wrote)
	{
		wrote = (Bit16u)(numbytes > 0xFFFF ? 0xFFFF : numbytes);
		if (!df->Write((Bit8u*)buf, &wrote)) { DBP_ASSERT(0); }
	}
	df->Close();
	delete df;
	return true;
}

Bit32u DriveCalculateCRC32(const Bit8u *ptr, size_t len, Bit32u crc)
{
	// Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/
	static const Bit32u s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
	Bit32u crcu32 = (Bit32u)~crc;
	while (len--) { Bit8u b = *ptr++; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)]; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)]; }
	return ~crcu32;
}

//DBP: utility function to evaluate an entire drives filesystem
void DriveFileIterator(DOS_Drive* drv, void(*func)(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data), Bitu data)
{
	if (!drv) return;
	struct Iter
	{
		static void ParseDir(DOS_Drive* drv, const std::string& dir, std::vector<std::string>& dirs, void(*func)(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data), Bitu data)
		{
			size_t dirlen = dir.length();
			if (dirlen + DOS_NAMELENGTH >= DOS_PATHLENGTH) return;
			char full_path[DOS_PATHLENGTH+4];
			if (dirlen)
			{
				memcpy(full_path, &dir[0], dirlen);
				full_path[dirlen++] = '\\';
			}
			full_path[dirlen] = '\0';

			RealPt save_dta = dos.dta();
			dos.dta(dos.tables.tempdta);
			DOS_DTA dta(dos.dta());
			dta.SetupSearch(255, (Bit8u)(0xffff & ~DOS_ATTR_VOLUME), (char*)"*.*");
			for (bool more = drv->FindFirst((char*)dir.c_str(), dta); more; more = drv->FindNext(dta))
			{
				char dta_name[DOS_NAMELENGTH_ASCII]; Bit32u dta_size; Bit16u dta_date, dta_time; Bit8u dta_attr;
				dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
				
				strcpy(full_path + dirlen, dta_name);
				bool is_dir = !!(dta_attr & DOS_ATTR_DIRECTORY);
				//if (is_dir) printf("[%s] [%s] %s (size: %u - date: %u - time: %u - attr: %u)\n", (const char*)data, (dta_attr == 8 ? "V" : (is_dir ? "D" : "F")), full_path, dta_size, dta_date, dta_time, dta_attr);
				if (dta_name[0] == '.' && dta_name[dta_name[1] == '.' ? 2 : 1] == '\0') continue;
				if (is_dir) dirs.emplace_back(full_path);
				func(full_path, is_dir, dta_size, dta_date, dta_time, dta_attr, data);
			}
			dos.dta(save_dta);
		}
	};
	std::vector<std::string> dirs;
	dirs.emplace_back("");
	std::string dir;
	while (dirs.size())
	{
		std::swap(dirs.back(), dir);
		dirs.pop_back();
		Iter::ParseDir(drv, dir.c_str(), dirs, func, data);
	}
}

#include <dbp_serialize.h>

void DBPSerialize_Drives(DBPArchive& ar)
{
	Bit8u drive_count = 0;
	for (Bit8u i = 0; i < DOS_DRIVES; i++)
		if (Drives[i])
			drive_count++;

	Bit8u current_drive_count = drive_count;
	ar << drive_count;
	if (ar.mode == DBPArchive::MODE_MAXSIZE) drive_count = DOS_DRIVES;
	if (ar.mode == DBPArchive::MODE_LOAD && current_drive_count != drive_count)
		ar.warnings |= DBPArchive::WARN_WRONGDRIVES;

	for (Bit8u i = (Bit8u)-1; drive_count--;)
	{
		Bit8u curdir_len;
		if (ar.mode == DBPArchive::MODE_SAVE || ar.mode == DBPArchive::MODE_SIZE)
		{
			while (!Drives[++i]) { }
			curdir_len = (Bit8u)strlen(Drives[i]->curdir);
		}
		ar << i << curdir_len;
		if (ar.mode == DBPArchive::MODE_MAXSIZE) ar.SerializeBytes(NULL, sizeof(Drives[i]->curdir));
		else if (ar.mode != DBPArchive::MODE_LOAD) ar.SerializeBytes(Drives[i]->curdir, curdir_len);
		else
		{
			if (!Drives[i]) { ar.Discard(curdir_len); ar.warnings |= DBPArchive::WARN_WRONGDRIVES; continue; }
			ar.SerializeBytes(Drives[i]->curdir, curdir_len);
			Drives[i]->curdir[curdir_len] = '\0';
		}
	}
}
