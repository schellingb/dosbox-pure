/*
 *  Copyright (C) 2020-2023 Bernhard Schelling
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

#include <time.h>
#include <vector>

struct Memory_Entry
{
protected:
	Memory_Entry(Bit16u _attr, const char* _name, Bit16u _date = 0, Bit16u _time = 0) : date(_date), time(_time), attr(_attr)
	{
		if (date == 0 && time == 0)
		{
			time_t curtime = ::time(NULL);
			tm* t = localtime(&curtime);
			time = (t ? DOS_PackTime((Bit16u)t->tm_hour,(Bit16u)t->tm_min,(Bit16u)t->tm_sec) : 0);
			date = (t ? DOS_PackDate((Bit16u)(t->tm_year+1900),(Bit16u)(t->tm_mon+1),(Bit16u)t->tm_mday) : 0);
		}
		SetName(_name);
	}
	~Memory_Entry() {}

public:
	inline bool IsFile()                          { return ((attr & DOS_ATTR_DIRECTORY) == 0); }
	inline bool IsDirectory()                     { return ((attr & DOS_ATTR_DIRECTORY) != 0); }
	inline struct Memory_File*      AsFile()      { return (Memory_File*)this;      }
	inline struct Memory_Directory* AsDirectory() { return (Memory_Directory*)this; }
	void SetName(const char* _name)
	{
		size_t namesize = strlen(_name) + 1;
		if (namesize > sizeof(name)) { DBP_ASSERT(false); namesize = sizeof(name); }
		memcpy(name, _name, namesize);
	}

	Bit16u date;
	Bit16u time;
	Bit16u attr;
	char name[DOS_NAMELENGTH_ASCII];
};

struct Memory_File : Memory_Entry
{
	std::vector<Bit8u> mem_data;
	Bit32u refs;

	Memory_File(Bit16u _attr, const char* filename, Bit16u _date = 0, Bit16u _time = 0) : Memory_Entry(_attr, filename, _date, _time), refs(0) { DBP_ASSERT(IsFile()); }

	inline Bit32u Size() { return (Bit32u)mem_data.size(); }
};

struct Memory_Handle : public DOS_File
{
	Memory_Handle(Memory_File* _src, Bit32u _flags, const char* path) : mem_pos(0), src(_src)
	{
		_src->refs++;
		date = _src->date;
		time = _src->time;
		attr = _src->attr;
		flags = _flags;
		SetName(path);
		open = true;
	}

	~Memory_Handle()
	{
		DBP_ASSERT(!open && !src);
	}

	virtual bool Read(Bit8u* data, Bit16u* size)
	{
		if (!OPEN_IS_READING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (!*size) return true;
		if (mem_pos >= (Bit32u)src->mem_data.size())
		{
			*size = 0;
			return true;
		}
		Bit32u left = (Bit32u)src->mem_data.size() - mem_pos;
		if (left < *size) *size = (Bit16u)left;
		memcpy(data, &src->mem_data.operator[](mem_pos), *size);
		mem_pos += *size;
		return true;
	}

	virtual bool Write(Bit8u* data, Bit16u* size)
	{
		if (!OPEN_IS_WRITING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (!*size)
		{
			// file resizing/truncating
			src->mem_data.resize(mem_pos);
			return true;
		}
		size_t newsize = mem_pos + *size;
		if (newsize > src->mem_data.size()) src->mem_data.resize(newsize);
		memcpy(&src->mem_data.operator[](mem_pos), data, *size);
		mem_pos += *size;
		return true;
	}

	virtual bool Seek(Bit32u* pos, Bit32u type)
	{
		Bit32s seekto=0;
		switch(type)
		{
			case DOS_SEEK_SET: seekto = (Bit32s)*pos; break;
			case DOS_SEEK_CUR: seekto = (Bit32s)*pos + (Bit32s)mem_pos; break;
			case DOS_SEEK_END: seekto = (Bit32s)src->mem_data.size() + (Bit32s)*pos; break;
			default: return FALSE_SET_DOSERR(FUNCTION_NUMBER_INVALID);
		}
		if (seekto < 0) seekto = 0;
		*pos = mem_pos = (Bit32u)seekto;
		return true;
	}

	virtual bool Close()
	{
		if (refCtr == 1)
		{
			src->refs--;
			open = false;
			src = NULL;
		}
		return true;
	}

	Bit16u GetInformation(void)
	{
		return 0; //writable storage
	}

	Bit32u mem_pos;
	Memory_File* src;
};

struct Memory_Directory: Memory_Entry
{
	StringToPointerHashMap<Memory_Entry> entries;

	Memory_Directory(Bit16u _attr, const char* dirname, Bit16u _date = 0, Bit16u _time = 0) : Memory_Entry(_attr, dirname, _date, _time) { DBP_ASSERT(IsDirectory()); }

	~Memory_Directory()
	{
		for (Memory_Entry* e : entries)
		{
			if (e->IsDirectory()) delete e->AsDirectory();
			else delete e->AsFile();
		}
	}
};

struct Memory_Search
{
	Memory_Directory* dir;
	Bit32u index;
};

struct memoryDriveImpl
{
	Memory_Directory root;
	StringToPointerHashMap<Memory_Directory> directories;
	std::vector<Memory_Search> searches;
	std::vector<Bit16u> free_search_ids;

	memoryDriveImpl() : root(DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY, "") { }

	Memory_Directory* GetParentDir(const char* path, const char** out_name)
	{
		const char *lastslash = strrchr(path, '\\');
		if (!lastslash) { *out_name = path; return &root; }
		*out_name = lastslash + 1;
		return directories.Get(path, (Bit16u)(lastslash - path));
	}

	Memory_Entry* Get(const char* path, Memory_Directory** out_dir = NULL, const char** out_name = NULL)
	{
		if (!*path)
		{
			if (out_dir) *out_dir = NULL; // hopefully no one tries to access this
			if (out_name) *out_name = root.name;
			return &root;
		}
		const char* name;
		Memory_Directory* dir = GetParentDir(path, &name);
		if (out_dir) *out_dir = dir;
		if (out_name) *out_name = name;
		if (!dir) return NULL;
		return dir->entries.Get(name);
	}
};

memoryDrive::memoryDrive() : impl(new memoryDriveImpl()) { }

memoryDrive::~memoryDrive()
{
	ForceCloseAll();
	delete impl;
}

bool memoryDrive::FileOpen(DOS_File * * file, char * name, Bit32u flags)
{
	if (!OPEN_CHECK_ACCESS_CODE(flags)) return FALSE_SET_DOSERR(ACCESS_CODE_INVALID);
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(name);
	Memory_Entry* e = impl->Get(name);
	if (!e || e->IsDirectory()) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
	*file = new Memory_Handle(e->AsFile(), flags, name_org);
	return true;
}

bool memoryDrive::FileCreate(DOS_File * * file, char * path, Bit16u attributes)
{
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(path);
	if ((attributes & DOS_ATTR_DIRECTORY) || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Memory_Directory* dir;
	const char* filename;
	Memory_Entry* e = impl->Get(path, &dir, &filename);
	if (!dir) return FALSE_SET_DOSERR(PATH_NOT_FOUND);
	if (e && e->IsDirectory()) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Memory_File* f = (e ? e->AsFile() : new Memory_File(attributes, filename));
	f->mem_data.clear();
	dir->entries.Put(filename, f);
	*file = new Memory_Handle(f, OPEN_READWRITE, path_org);
	return true;
}

bool memoryDrive::Rename(char * oldpath, char * newpath)
{
	DOSPATH_REMOVE_ENDINGDOTS(oldpath);
	DOSPATH_REMOVE_ENDINGDOTS(newpath);
	Memory_Directory *old_dir, *new_dir;
	const char *old_filename, *new_filename;
	Memory_Entry* e = impl->Get(oldpath, &old_dir, &old_filename);
	if (!e) return FALSE_SET_DOSERR(FILE_NOT_FOUND);

	Memory_Entry* existing = impl->Get(newpath, &new_dir, &new_filename);
	if (existing) return (e == existing || FALSE_SET_DOSERR(FILE_ALREADY_EXISTS));
	if (!new_dir) return FALSE_SET_DOSERR(PATH_NOT_FOUND);

	if (e->IsDirectory() && old_dir != new_dir) return FALSE_SET_DOSERR(ACCESS_DENIED); //can't move directory into another directory

	if (e->IsFile() && e->AsFile()->refs)
	{
		DriveForceCloseFile(this, oldpath);
		DBP_ASSERT(e->AsFile()->refs == 0);
	}
	e->SetName(new_filename);
	old_dir->entries.Remove(old_filename);
	new_dir->entries.Put(new_filename, e);
	return true;
}

bool memoryDrive::FileUnlink(char * path)
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	Memory_Directory *dir;
	const char *filename;
	Memory_Entry* e = impl->Get(path, &dir, &filename);
	if (!e || (e->attr & (DOS_ATTR_DIRECTORY|DOS_ATTR_READ_ONLY))) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
	if (e->AsFile()->refs)
	{
		DriveForceCloseFile(this, path);
	 	DBP_ASSERT(e->AsFile()->refs == 0);
	}
	dir->entries.Remove(filename);
	delete e->AsFile();
	return true;
}

bool memoryDrive::FileExists(const char* name)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Memory_Entry* p = impl->Get(name);
	return (p && p->IsFile());
}

bool memoryDrive::RemoveDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	Memory_Directory* dir = impl->directories.Get(dir_path);
	if (!dir) return FALSE_SET_DOSERR(PATH_NOT_FOUND);
	if (dir->entries.Len()) return FALSE_SET_DOSERR(ACCESS_DENIED); // not empty
	const char* dirname = NULL;
	impl->GetParentDir(dir_path, &dirname)->entries.Remove(dirname);
	impl->directories.Remove(dir_path);
	delete dir;
	return true;
}

bool memoryDrive::MakeDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	Memory_Directory *parent;
	const char *dirname;
	if (impl->Get(dir_path, &parent, &dirname)) return FALSE_SET_DOSERR(FILE_ALREADY_EXISTS);
	if (!parent) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Memory_Directory* d = new Memory_Directory(DOS_ATTR_DIRECTORY, dirname);
	parent->entries.Put(dirname, d);
	impl->directories.Put(dir_path, d);
	return true;
}

bool memoryDrive::TestDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	return (!dir_path[0] || impl->directories.Get(dir_path));
}

bool memoryDrive::FindFirst(char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	Memory_Directory* dir = (!dir_path[0] ? &impl->root : impl->directories.Get(dir_path));
	if (!dir) return FALSE_SET_DOSERR(PATH_NOT_FOUND);

	Memory_Search s = { dir, 0 };
	if (impl->free_search_ids.empty())
	{
		dta.SetDirID((Bit16u)impl->searches.size());
		impl->searches.push_back(s);
	}
	else
	{
		dta.SetDirID(impl->free_search_ids.back());
		impl->free_search_ids.pop_back();
		impl->searches[dta.GetDirID()] = s;
	}

	if (DriveFindDriveVolume(this, dir_path, dta, fcb_findfirst)) return true;
	return FindNext(dta);
}

bool memoryDrive::FindNext(DOS_DTA & dta)
{
	if (dta.GetDirID() >= impl->searches.size()) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Memory_Search& s = impl->searches[dta.GetDirID()];
	if (!s.dir) return FALSE_SET_DOSERR(NO_MORE_FILES);
	Bit8u attr;char pattern[DOS_NAMELENGTH_ASCII];
	dta.GetSearchParams(attr,pattern);
	while (s.index < 2)
	{
		const char* dotted = (s.index++ ? ".." : ".");
		if (!WildFileCmp(dotted, pattern) || (s.dir->attr & DOS_ATTR_VOLUME)) continue;
		if (~attr & (Bit8u)s.dir->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
		dta.SetResult(dotted, 0, s.dir->date, s.dir->time, (Bit8u)s.dir->attr);
		return true;
	}
	for (Bit32u i = s.dir->entries.Capacity() - (s.index++ - 2); i--; s.index++) // iterate reverse to better deal with "DEL *.*"
	{
		Memory_Entry* e = s.dir->entries.GetAtIndex(i);
		if (!e || !WildFileCmp(e->name, pattern)) continue;
		if (~attr & (Bit8u)e->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
		dta.SetResult(e->name, (e->IsFile() ? e->AsFile()->Size() : 0), e->date, e->time, (Bit8u)e->attr);
		return true;
	}
	s.dir = NULL;
	impl->free_search_ids.push_back(dta.GetDirID());
	return FALSE_SET_DOSERR(NO_MORE_FILES);
}

bool memoryDrive::FileStat(const char* name, FileStat_Block * const stat_block)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Memory_Entry* p = impl->Get(name);
	if (!p) return false;
	stat_block->attr = p->attr;
	stat_block->size = (p->IsFile() ? p->AsFile()->Size() : 0);
	stat_block->date = p->date;
	stat_block->time = p->time;
	return true;
}

bool memoryDrive::GetFileAttr(char * name, Bit16u * attr)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Memory_Entry* p = impl->Get(name);
	if (!p) return false;
	*attr = p->attr;
	return true;
}

bool memoryDrive::AllocationInfo(Bit16u * _bytes_sector, Bit8u * _sectors_cluster, Bit16u * _total_clusters, Bit16u * _free_clusters)
{
	// return dummy numbers (not above 0x7FFF which seems to be needed for some games)
	*_bytes_sector = 512;
	*_sectors_cluster = 32;
	*_total_clusters = 32765; // 512MB
	*_free_clusters = 16000; // 250MB
	return true;
}

Bit8u memoryDrive::GetMediaByte(void) { return 0xF8;  } //Hard Disk
bool memoryDrive::isRemote(void) { return false; }
bool memoryDrive::isRemovable(void) { return false; }
Bits memoryDrive::UnMount(void) { delete this; return 0;  }

bool memoryDrive::CloneEntry(DOS_Drive* src_drv, const char* src_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(src_path);
	FileStat_Block stat;
	if (!src_drv->FileStat(src_path, &stat)) return false;

	const char* name;
	Memory_Directory* dir = impl->GetParentDir(src_path, &name);
	if (!dir) return false;

	Memory_Entry* e;
	if (stat.attr & DOS_ATTR_DIRECTORY)
	{
		e = new Memory_Directory(stat.attr, name, stat.date, stat.time);
		impl->directories.Put(src_path, e->AsDirectory());
	}
	else
	{
		e = new Memory_File(stat.attr, name, stat.date, stat.time);
		DOS_File* df;
		if (stat.size && src_drv->FileOpen(&df, (char*)src_path, 0))
		{
			df->AddRef();
			e->AsFile()->mem_data.resize(stat.size);
			Bit8u* buf = &e->AsFile()->mem_data[0];
			for (Bit16u read; stat.size; stat.size -= read, buf += read)
			{
				read = (Bit16u)(stat.size > 0xFFFF ? 0xFFFF : stat.size);
				if (!df->Read(buf, &read)) { DBP_ASSERT(0); }
			}
			df->Close();
			delete df;
		}
	}
	dir->entries.Put(name, e);
	return true;
}
