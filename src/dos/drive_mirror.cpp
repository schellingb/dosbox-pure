/*
 *  Copyright (C) 2024 Bernhard Schelling
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
#include "dos_inc.h"
#include "drives.h"

struct Mirror_Handle : public DOS_File
{
	DOS_File* underfile;

	Mirror_Handle(DOS_File* _underfile, const char* path) : underfile(_underfile)
	{
		DBP_ASSERT(underfile->open);
		underfile->AddRef();
		date = underfile->date;
		time = underfile->time;
		attr = underfile->attr;
		flags = underfile->flags;
		SetName(path);
		open = true;
	}

	~Mirror_Handle()
	{
		DBP_ASSERT(!open && !underfile);
	}

	virtual bool Read(Bit8u* data, Bit16u* size) { return underfile->Read(data, size); }
	virtual bool Write(Bit8u* data, Bit16u* size) { return underfile->Write(data, size); }
	virtual bool Seek(Bit32u* pos, Bit32u type) { return underfile->Seek(pos, type); }
	virtual Bit16u GetInformation(void) { return underfile->GetInformation(); }
	virtual bool UpdateDateTimeFromHost() { return underfile->UpdateDateTimeFromHost(); }

	virtual bool Close()
	{
		if (refCtr == 1)
		{
			if (newtime)
			{
				underfile->time = time;
				underfile->date = date;
				underfile->newtime = true;
				newtime = false;
			}
			open = false;
			underfile->Close();
			delete underfile;
			underfile = NULL;
		}
		return true;
	}
};

struct mirrorDriveImpl
{
	DOS_Drive& under;
	bool autodeleteUnder, writable;
	Bit8u lenFrom, lenTo;
	char dirFrom[DOS_PATHLENGTH], dirTo[DOS_PATHLENGTH];

	mirrorDriveImpl(DOS_Drive& _under, bool _autodeleteUnder, const char* _mirrorFrom, const char* _mirrorTo) : under(_under), autodeleteUnder(_autodeleteUnder)
	{
		Bit16u bytes_sector; Bit8u sectors_cluster; Bit16u total_clusters; Bit16u free_clusters;
		_under.AllocationInfo(&bytes_sector, &sectors_cluster, &total_clusters, &free_clusters);
		writable = (free_clusters > 0);
		size_t szFrom = (_mirrorFrom ? strlen(_mirrorFrom) : 0), szTo = (_mirrorTo ? strlen(_mirrorTo) : 0);
		if (szFrom && _mirrorFrom[szFrom-1] == '\\') szFrom--;
		if (szTo && _mirrorTo[szTo-1] == '\\') szTo--;
		if (szFrom > DOS_PATHLENGTH - 2) { DBP_ASSERT(0); szFrom = DOS_PATHLENGTH - 2; }
		if (szTo > DOS_PATHLENGTH - 2) { DBP_ASSERT(0); szTo = DOS_PATHLENGTH - 2; }
		lenFrom = (Bit8u)szFrom;
		lenTo = (Bit8u)szTo;
		if (lenFrom) { memcpy(dirFrom, _mirrorFrom, lenFrom); dirFrom[lenFrom++] = '\\'; }
		if (lenTo) { memcpy(dirTo, _mirrorTo, lenTo); dirTo[lenTo++] = '\\'; }
	}

	~mirrorDriveImpl()
	{
		if (autodeleteUnder) delete &under;
	}

	bool FixSubdir(const char*& name, char name_buf[DOS_PATHLENGTH], bool is_path = false, bool canBeRoot = false)
	{
		if ((name[0] == 'A' && name[1] == 'U' && !strcmp(name, "AUTOBOOT.DBP")) || (name[0] == 'P' && name[1] == 'A' && !strcmp(name, "PADMAP.DBP")))
			return true; // use these system file names as is in the root
		if (lenTo)
		{
			if (strncmp(name, dirTo, lenTo - (canBeRoot ? 1 : 0))) return (is_path ? FALSE_SET_DOSERR(PATH_NOT_FOUND) : FALSE_SET_DOSERR(FILE_NOT_FOUND));
			name += ((!canBeRoot || name[lenTo - 1]) ? lenTo : (lenTo - 1));
		}
		if (lenFrom)
		{
			size_t len = strlen(name);
			if ((!canBeRoot && !len) || ((lenFrom + len) >= DOS_PATHLENGTH)) return FALSE_SET_DOSERR(ACCESS_DENIED);
			memcpy(name_buf, dirFrom, lenFrom);
			memmove(name_buf + lenFrom, name, len + 1); // copy null terminator
			name = name_buf;
			if (canBeRoot && !len) name_buf[lenFrom - 1] = '\0';
		}
		return true;
	}
};

mirrorDrive::mirrorDrive(DOS_Drive& under, bool autodelete_under, const char* mirrorFrom, const char* mirrorTo) : impl(new mirrorDriveImpl(under, autodelete_under, mirrorFrom, mirrorTo))
{
	label.SetLabel(under.GetLabel(), false, true);
}

mirrorDrive::~mirrorDrive()
{
	ForceCloseAll();
	delete impl;
}

bool mirrorDrive::FileOpen(DOS_File * * file, char * name, Bit32u flags)
{
	if (!OPEN_CHECK_ACCESS_CODE(flags)) return FALSE_SET_DOSERR(ACCESS_CODE_INVALID);
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(name);
	if (!impl->FixSubdir((const char*&)name, name_buf) || !impl->under.FileOpen(file, name, flags)) return false;
	*file = new Mirror_Handle(*file, name_org);
	return true;
}

bool mirrorDrive::FileCreate(DOS_File * * file, char * path, Bit16u attributes)
{
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(path);
	if ((attributes & DOS_ATTR_DIRECTORY) || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
	if (!impl->FixSubdir((const char*&)path, path_buf) || !impl->under.FileCreate(file, path, attributes)) return false;
	*file = new Mirror_Handle(*file, path_org);
	return true;
}

bool mirrorDrive::Rename(char * oldpath, char * newpath)
{
	DOSPATH_REMOVE_ENDINGDOTS(oldpath);
	DOSPATH_REMOVE_ENDINGDOTS(newpath);
	if (!impl->writable || !*oldpath || !*newpath) return FALSE_SET_DOSERR(ACCESS_DENIED);
	if (!strcmp(oldpath, newpath)) return true; //rename with same name is always ok
	DriveForceCloseFile(this, oldpath);
	return (impl->FixSubdir((const char*&)oldpath, oldpath_buf) && impl->FixSubdir((const char*&)newpath, newpath_buf) && impl->under.Rename(oldpath, newpath));
}

bool mirrorDrive::FileUnlink(char * path)
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	if (!impl->writable || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
	DriveForceCloseFile(this, path);
	return (impl->FixSubdir((const char*&)path, path_buf) && impl->under.FileUnlink(path));
}

bool mirrorDrive::FileExists(const char* name)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	return (impl->FixSubdir(name, name_buf) && impl->under.FileExists(name));
}

bool mirrorDrive::RemoveDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	if (!impl->writable || !*dir_path) return FALSE_SET_DOSERR(ACCESS_DENIED);
	return (impl->FixSubdir((const char*&)dir_path, dir_path_buf, true) && impl->under.RemoveDir(dir_path));
}

bool mirrorDrive::MakeDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	return (impl->FixSubdir((const char*&)dir_path, dir_path_buf, true) && impl->under.MakeDir(dir_path));
}

bool mirrorDrive::TestDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	Bit8u len = (Bit8u)strlen(dir_path);
	if (len < impl->lenTo - 1 && !memcmp(impl->dirTo, dir_path, len) && (!len || impl->dirTo[len] == '\\'))
		return true;
	return (impl->FixSubdir((const char*&)dir_path, dir_path_buf, true, true) && impl->under.TestDir(dir_path));
}

bool mirrorDrive::FindFirst(char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(dir_path);
	Bit8u len = (Bit8u)strlen(dir_path);
	if (len < impl->lenTo - 1 && !memcmp(impl->dirTo, dir_path, len) && (!len || impl->dirTo[len] == '\\'))
	{
		dta.SetDirID(0xEEEE + len);
		if (!len && DriveFindDriveVolume(this, dir_path, dta, fcb_findfirst)) return true;
		return FindNext(dta);
	}
	return (impl->FixSubdir((const char*&)dir_path, dir_path_buf, true, true) && impl->under.FindFirst(dir_path, dta, fcb_findfirst));
}

bool mirrorDrive::FindNext(DOS_DTA & dta)
{
	const Bit16u dir_id = dta.GetDirID();
	char *pStart = impl->dirTo, *pEnd = pStart + impl->lenTo, *p = pStart + (dir_id - 0xEEEE);
	if (p >= pStart && p < pEnd)
	{
		if (p == pStart || *p == '\\')
		{
			Bit8u attr;char pattern[DOS_NAMELENGTH_ASCII], *slash = strchr((p == pStart ? p : ++p), '\\');
			dta.GetSearchParams(attr,pattern);
			*slash = '\0';
			bool match = DTA_PATTERN_MATCH(p, pattern);
			if (match && (attr & DOS_ATTR_DIRECTORY))
				dta.SetResult(p, 0, 8600, 48128, (Bit8u)DOS_ATTR_DIRECTORY);
			dta.SetDirID(dir_id + 1);
			*slash = '\\';
			return match || FALSE_SET_DOSERR(NO_MORE_FILES);
		}
		if (p == pStart + 1 || p[-1] == '\\') return FALSE_SET_DOSERR(NO_MORE_FILES);
	}
	return impl->under.FindNext(dta);
}

bool mirrorDrive::FileStat(const char* name, FileStat_Block * const stat_block)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	return (impl->FixSubdir(name, name_buf) && impl->under.FileStat(name, stat_block));
}

bool mirrorDrive::GetFileAttr(char * name, Bit16u * attr)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	return (impl->FixSubdir((const char*&)name, name_buf) && impl->under.GetFileAttr(name, attr));
}

bool mirrorDrive::GetLongFileName(const char* path, char longname[256])
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	return (impl->FixSubdir((const char*&)path, path_buf) && impl->under.GetLongFileName(path, longname));
}

bool mirrorDrive::AllocationInfo(Bit16u * _bytes_sector, Bit8u * _sectors_cluster, Bit16u * _total_clusters, Bit16u * _free_clusters)
{
	return impl->under.AllocationInfo(_bytes_sector, _sectors_cluster, _total_clusters, _free_clusters);
}

DOS_Drive* mirrorDrive::GetShadow(int n, bool only_owned) { return ((n == 0 && (!only_owned || impl->autodeleteUnder)) ? &impl->under : NULL); }
Bit8u mirrorDrive::GetMediaByte(void) { return impl->under.GetMediaByte(); }
bool mirrorDrive::isRemote(void) { return impl->under.isRemote(); }
bool mirrorDrive::isRemovable(void) { return impl->under.isRemovable(); }
Bits mirrorDrive::UnMount(void) { delete this; return 0; }
