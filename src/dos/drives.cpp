/*
 *  Copyright (C) 2002-2020  The DOSBox Team
 *  Copyright (C) 2020-2020  Bernhard Schelling
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
#include "mapper.h"
#include "support.h"

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
				DBP_ASSERT(Files[i]->open && Files[i]->refCtr > 0); //files shouldn't hang around closed
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

void DRIVES_Init(Section* sec) {
	DriveManager::Init(sec);
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

bool DriveForceCloseFile(DOS_Drive* drv, const char* name) {
	Bit8u i, drive = DOS_DRIVES;
	for (i = 0; i < DOS_DRIVES; i++) {
		if (!Drives[i]) continue;
		if (Drives[i] == drv) {
			drive = i;
			break;
		}
		unionDrive* ud = dynamic_cast<unionDrive*>(Drives[i]);
		if (ud && ud->IsShadowedDrive(drv)) {
			drive = i;
			break;
		}
	}
	DOSPATH_REMOVE_ENDINGDOTS(name);
	bool found_file = false;
	if (drive != DOS_DRIVES) {
		for (i = 0; i < DOS_FILES; i++) {
			DOS_File *f = Files[i];
			if (!f || f->GetDrive() != drive || !f->name) continue;
			const char* fname = f->name;
			DOSPATH_REMOVE_ENDINGDOTS(fname);
			if (strcasecmp(name, fname)) continue;
			DBP_ASSERT(f->open && f->refCtr > 0); //files shouldn't hang around closed
			while (f->refCtr > 0) { if (f->IsOpen()) f->Close(); f->RemoveRef(); }
			delete f;
			Files[i] = NULL;
			found_file = true;
		}
	}
	return found_file;
}

Bit32u DBP_Make8dot3FileName(char* target, Bit32u target_len, const char* source, Bit32u source_len)
{
	struct Func
	{
		static void AppendFiltered(char*& trg, const char* trg_end, const char* src, Bit32u len)
		{
			for (; trg < trg_end && len--; trg++)
			{
				char DOS_ToUpper(char);
				*trg = *(src++);
				if (*trg <= ' ' || *trg == '.') *trg = '-';
				*trg = DOS_ToUpper(*trg);
			}
		}
	};
	const char *target_start = target, *target_end = target + target_len, *source_end = source + source_len, *sDot;
	for (sDot = source_end - 1; *sDot != '.' && sDot >= source; sDot--);
	Bit32u baseLen = (Bit32u)((*sDot == '.' ? sDot : source_end) - source);
	Bit32u extLen = (Bit32u)(*sDot == '.' ? source_end - 1 - sDot : 0);
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
	Bit8u drive = (filename[1] == ':' ? ((filename[0]|0x20)-'a') : DOS_GetDefaultDrive());
	if (drive < DOS_DRIVES && Drives[drive])
	{
		char dos_path[DOS_PATHLENGTH + 1], *p_dos = dos_path, *p_dos_end = p_dos + DOS_PATHLENGTH;
		const char* n = filename + (filename[1] == ':' ? 2 : 0);
		if (*n == '\\' || *n == '/') n++; // absolute path
		else // relative path
		{
			strcpy(p_dos, Drives[drive]->curdir);
			p_dos += strlen(p_dos);
			*(p_dos++) = '\\';
		}
		for (const char *nDir = n, *nEnd = n + strlen(n); n != nEnd + 1 && p_dos != p_dos_end; nDir = ++n)
		{
			while (*n != '/' && *n != '\\' && n != nEnd) n++;
			if (n == nDir) continue;

			// Create a 8.3 filename from a 4 char prefix and a suffix if filename is too long
			p_dos += DBP_Make8dot3FileName(p_dos, (Bit32u)(p_dos_end - p_dos), nDir, (Bit32u)(n - nDir));
			*(p_dos++) = (n == nEnd ? '\0' : '\\');
		}
		if (writable && Drives[drive]->FileOpen(&dos_file, dos_path, OPEN_READWRITE))
			goto get_file_size;
		if (Drives[drive]->FileOpen(&dos_file, dos_path, OPEN_READ))
			goto get_file_size_write_protected;
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

//DBP: utility function to evaluate an entire drives filesystem
void DriveFileIterator(DOS_Drive* drv, void(*func)(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data), Bitu data)
{
	if (!drv) return;
	struct Iter
	{
		static void ParseDir(DOS_Drive* drv, std::string dir, std::vector<std::string>& dirs, void(*func)(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data), Bitu data)
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
				if (dta_name[0] == '.' && (dta_name[1] == '\0' || (dta_name[1] == '.' && dta_name[2] == '\0'))) continue;
				if (is_dir) dirs.push_back(full_path);
				func(full_path, is_dir, dta_size, dta_date, dta_time, dta_attr, data);
			}
			dos.dta(save_dta);
		}
	};
	std::vector<std::string> dirs;
	dirs.push_back("");
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
