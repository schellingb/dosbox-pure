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
#include "pic.h"

#include <time.h>
#include <vector>

#define TRUE_RESET_DOSERR (dos.errorcode = save_errorcode, true)

static void CreateParentDirs(DOS_Drive& drv, const char* path)
{
	char dir_path[DOS_PATHLENGTH + 1], *p_dir_path = dir_path;
	for (const char *p_path = path; *p_path; p_path++, p_dir_path++)
	{
		if (*p_path == '\\')
		{
			*p_dir_path = '\0';
			drv.MakeDir(dir_path);
		}
		*p_dir_path = *p_path;
	}
}

struct Union_Search
{
	int step;
	char dir[DOS_PATHLENGTH + 1];
	Bit8u dir_len;
	bool fcb_findfirst;
	Bit16u sub_dirID;
	Bit32u dir_hash;
};

struct Union_Modification
{
	enum Type { TDIR = 'D', TFILE = 'F', TDELETE = 'x', TNONE = 0 };

private:
	Bit8u type, target_lastslash;
	char target[DOS_PATHLENGTH+1];
	char source[DOS_PATHLENGTH+1];
	Union_Modification() {}

public:
	Union_Modification(const char* _newpath, const char* _oldpath, bool is_file)
	{
		size_t target_len = strlen(_newpath), source_len = strlen(_oldpath);
		if (target_len > DOS_PATHLENGTH) { DBP_ASSERT(false); target_len = DOS_PATHLENGTH; }
		if (source_len > DOS_PATHLENGTH) { DBP_ASSERT(false); source_len = DOS_PATHLENGTH; }
		const char* _newpath_lastslash = strrchr(_newpath, '\\');
		type = (is_file ? (Bit8u)TFILE : (Bit8u)TDIR);
		target_lastslash = (Bit8u)(_newpath_lastslash ? (_newpath_lastslash - _newpath) : 0);
		memcpy(target, _newpath, target_len); target[target_len] = '\0';
		memcpy(source, _oldpath, source_len); source[source_len] = '\0';
	}

	Union_Modification(const char* _delpath) : type((Bit8u)TDELETE)
	{
		size_t delpath_len = strlen(_delpath);
		if (delpath_len > DOS_PATHLENGTH) { DBP_ASSERT(false); delpath_len = DOS_PATHLENGTH; }
		memcpy(target, _delpath, delpath_len); target[delpath_len] = '\0';
		source[0] = '\0';
	}

	bool IsRedirect() { return type != TDELETE; }
	bool IsDelete()   { return type == TDELETE; }
	Type  RedirectType()   { DBP_ASSERT(type != TDELETE); return (Type)type; }
	Bit8u RedirectDirLen() { DBP_ASSERT(type != TDELETE); return target_lastslash; }
	char* RedirectTarget() { DBP_ASSERT(type != TDELETE); return target; }
	char* RedirectSource() { DBP_ASSERT(type != TDELETE); return source; }

	void  RedirectSetNewPath(const char* _newpath)
	{
		DBP_ASSERT(type != TDELETE);
		size_t newpath_len = strlen(target);
		if (newpath_len > DOS_PATHLENGTH) { DBP_ASSERT(false); newpath_len = DOS_PATHLENGTH; }
		memcpy(target, _newpath, newpath_len); target[newpath_len] = '\0';
	}

	void Serialize(std::string& mods)
	{
		switch (type)
		{
			case Union_Modification::TDIR:    mods += "REDIRECTDIR|"; break;
			case Union_Modification::TFILE:   mods += "REDIRECTFILE|"; break;
			case Union_Modification::TDELETE: mods += "DELETE|"; break;
		}
		mods += target;
		if (*source) { mods += '|'; mods += source; }
		mods += '\r';
		mods += '\n';
	}

	static bool Deserialize(const char*& p, StringToPointerHashMap<Union_Modification>& modifications)
	{
		if (!*p) return false;
		const char* nlptr = strchr(p, '\n'), *nl = (nlptr ? nlptr : p + strlen(p));
		while (nl > p && nl[-1] <= ' ') nl--;

		Type t = TNONE;
		if      (nl - p > sizeof("REDIRECTDIR|" ) && !memcmp(p, "REDIRECTDIR|",  sizeof("REDIRECTDIR|" )-1)) { t = TDIR;    p += sizeof("REDIRECTDIR|" )-1; }
		else if (nl - p > sizeof("REDIRECTFILE|") && !memcmp(p, "REDIRECTFILE|", sizeof("REDIRECTFILE|")-1)) { t = TFILE;   p += sizeof("REDIRECTFILE|")-1; }
		else if (nl - p > sizeof("DELETE|"      ) && !memcmp(p, "DELETE|",       sizeof("DELETE|"      )-1)) { t = TDELETE; p += sizeof("DELETE|"      )-1; }
		const char* split = (t == TDIR || t == TFILE ? strchr(p, '|') : NULL);
		if (t != TNONE && (!split || split + 1 < nl))
		{
			size_t target_len = (split ? split : nl) - p, source_len = (split ? nl - (split + 1) : 0);
			Union_Modification* m = new Union_Modification();
			m->type = t;
			memcpy(m->target, p, target_len);
			m->target[target_len] = '\0';
			const char* target_lastslash = strrchr(m->target, '\\');
			m->target_lastslash = (Bit8u)(target_lastslash ? (target_lastslash - m->target) : 0);
			if (split) memcpy(m->source, split+1, source_len);
			m->source[source_len] = '\0';
			modifications.Put(m->target, m);
		}
		while (*nl && *nl <= ' ') nl++;
		return (*(p = nl) != '\0');
	}
};

struct unionDriveImpl
{
	memoryDrive* save_mem;
	DOS_Drive *under, *over;
	StringToPointerHashMap<Union_Modification> modifications;
	std::vector<Union_Search> searches;
	std::vector<Bit16u> free_search_ids;
	std::string save_file;
	Bit32u save_size;
	bool writable, autodelete_under, autodelete_over, dirty;

	unionDriveImpl(DOS_Drive* _under, DOS_Drive* _over, const char* _save_file, bool _autodelete_under, bool _autodelete_over = false, bool strict_mode = false)
		: save_mem(_over ? NULL : new memoryDrive()), under(_under), over(_over ? _over : save_mem), save_size(0),
		  autodelete_under(_autodelete_under), autodelete_over(_autodelete_over || save_mem), dirty(false)
	{
		Bit16u bytes_sector; Bit8u sectors_cluster; Bit16u total_clusters; Bit16u free_clusters;
		over->AllocationInfo(&bytes_sector, &sectors_cluster, &total_clusters, &free_clusters);
		writable = (free_clusters > 0);
		if (_save_file)
		{
			DBP_ASSERT(!_over && writable);
			save_file = _save_file;
			ReadSaveFile(strict_mode);
		}
	}

	~unionDriveImpl()
	{
		if (dirty)
			WriteSaveFile((Bitu)this);
		if (dirty)
			PIC_RemoveSpecificEvents(WriteSaveFile, (Bitu)this);
		for (Union_Modification* m : modifications)
			delete m;
		if (autodelete_under)
			delete under;
		if (autodelete_over)
			delete over;
	}

	bool ExistInOverOrUnder(char* path, bool* out_is_file, bool* out_in_under)
	{
		bool file_in_under = under->FileExists(path), dir_in_under = under->TestDir(path), is_file = (over->FileExists(path) || file_in_under);
		*out_is_file = is_file;
		*out_in_under = file_in_under || dir_in_under;
		return (is_file || file_in_under || dir_in_under || over->TestDir(path));
	}

	bool ExistInOverOrUnder(char* path)
	{
		return (under->FileExists(path) || over->FileExists(path) || over->TestDir(path) || under->TestDir(path));
	}

	bool UnionUnlink(DOS_Drive* drv, char* path, Union_Modification::Type type, const Bit16u save_errorcode)
	{
		if (!writable || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
		Union_Modification* m = modifications.Get(path);
		if (m && m->IsDelete()) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		if (m && m->IsRedirect() && m->RedirectType() != type) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		if (m && m->IsRedirect())
		{
			ForceCloseFileAndScheduleSave(drv, path);
			delete m;
			bool in_under = (under->FileExists(path) || under->TestDir(path));
			if (in_under) modifications.Put(path, new Union_Modification(path)); //re-mark deletion
			else modifications.Remove(path); //remove redirect
			return TRUE_RESET_DOSERR;
		}
		if (type == Union_Modification::TFILE ? over->FileUnlink(path) : over->RemoveDir(path))
		{
			ForceCloseFileAndScheduleSave(drv, path);
			bool in_under = (under->FileExists(path) || under->TestDir(path));
			if (in_under) modifications.Put(path, new Union_Modification(path)); //mark deletion
			return TRUE_RESET_DOSERR;
		}
		if (type == Union_Modification::TFILE ? under->FileExists(path) : under->TestDir(path))
		{
			ForceCloseFileAndScheduleSave(drv, path);
			modifications.Put(path, new Union_Modification(path)); //mark deletion
			return TRUE_RESET_DOSERR;
		}
		return FALSE_SET_DOSERR(FILE_NOT_FOUND);
	}

	bool UnionTest(char* path, Union_Modification::Type type)
	{
		if (!*path) return (type == Union_Modification::TDIR);
		Union_Modification* m = modifications.Get(path);
		if (m)
		{
			return (m->IsRedirect() && m->RedirectType() == type ? true : FALSE_SET_DOSERR(FILE_NOT_FOUND));
		}
		return (type == Union_Modification::TFILE ? (over->FileExists(path) || under->FileExists(path)) : (over->TestDir(path) || under->TestDir(path)));
	}

	bool UnionPrepareCreate(char* path, bool can_overwrite)
	{
		if (!writable || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
		Union_Modification* m = modifications.Get(path);
		if (!m) return (can_overwrite || (!under->FileExists(path) && !under->TestDir(path)) || FALSE_SET_DOSERR(FILE_ALREADY_EXISTS));
		if (!can_overwrite && m->IsRedirect()) return FALSE_SET_DOSERR(FILE_ALREADY_EXISTS);
		delete m;
		modifications.Remove(path);
		return true;
	}

	void ReadSaveFile(bool strict_mode)
	{
		struct Loader
		{
			zipDrive* zip;
			unionDriveImpl* impl;
			bool strict_mode;
			Loader(zipDrive* _zip, unionDriveImpl* _impl, bool _strict_mode) : zip(_zip), impl(_impl), strict_mode(_strict_mode) {}
			static void LoadFiles(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
			{
				Loader& l = *(Loader*)data;
				DOS_File* df;
				if (path[0] == 'F' && size && !strcmp(path, "FILEMODS.DBP") && l.zip->FileOpen(&df, (char*)path, 0))
				{
					df->AddRef();
					std::vector<char> mods;
					mods.resize(size+sizeof('\0'));
					Bit8u* buf = (Bit8u*)&mods[0];
					for (Bit16u read; size; size -= read, buf += read)
					{
						read = (Bit16u)(size > 0xFFFF ? 0xFFFF : size);
						if (!df->Read(buf, &read)) { DBP_ASSERT(0); }
					}
					df->Close();
					delete df;

					const char* ptr = &mods[0];
					while (Union_Modification::Deserialize(ptr, l.impl->modifications)) {}
					return;
				}
				if (l.strict_mode)
				{
					size_t pathlen = strlen(path);
					const char* ext = (pathlen > 4 ? path+pathlen-4 : NULL);
					if (ext && (!memcmp(ext, ".EXE", 4) || !memcmp(ext, ".COM", 4) || !memcmp(ext, ".BAT", 4) || !strcmp(path, "DOS.YML"))) return;
				}
				CreateParentDirs(*l.impl->save_mem, path);
				if (!l.impl->save_mem->CloneEntry(l.zip, path)) { DBP_ASSERT(0); }
				l.impl->save_size += size;
			}
		};
		FILE* zip_file_h = fopen_wrap(save_file.c_str(), "rb");
		if (!zip_file_h) return;

		Loader l(new zipDrive(new rawFile(zip_file_h, false), false), this, strict_mode);
		const Bit16u save_errorcode = dos.errorcode;
		DriveFileIterator(l.zip, Loader::LoadFiles, (Bitu)&l);
		dos.errorcode = save_errorcode;
		delete l.zip; // calls fclose
	}

	static void WriteSaveFile(Bitu implPtr)
	{
		#define ZIP_WRITE_LE16(b,v) { (b)[0] = (Bit8u)((Bit16u)(v) & 0xFF); (b)[1] = (Bit8u)((Bit16u)(v) >> 8); }
		#define ZIP_WRITE_LE32(b,v) { (b)[0] = (Bit8u)((Bit32u)(v) & 0xFF); (b)[1] = (Bit8u)(((Bit32u)(v) >> 8) & 0xFF); (b)[2] = (Bit8u)(((Bit32u)(v) >> 16) & 0xFF); (b)[3] = (Bit8u)((Bit32u)(v) >> 24); }
		struct Saver
		{
			FILE* f;
			DOS_Drive* drv;
			Bit32u local_file_offset, save_size;
			Bit16u file_count;
			bool failed;
			std::vector<Bit8u> central_dir;
			std::string mods;

			static void WriteFiles(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
			{
				Saver& s = *(Saver*)data;
				Bit8u buf[4096];
				Bit16u pathLen = (Bit16u)(strlen(path) + (is_dir ? 1 : 0));
				Bit32u crc32 = 0, extAttr = (is_dir ? 0x10 : 0);

				if (!is_dir && size)
				{
					fseek_wrap(s.f, 30 + pathLen, SEEK_CUR);
					if (s.mods.size())
					{
						crc32 = DriveCalculateCRC32((Bit8u*)&s.mods[0], size, crc32);
						s.failed |= !fwrite(&s.mods[0], size, 1, s.f);
					}
					else
					{
						// Write file and calculate CRC32 along the way
						DOS_File* df = nullptr;
						bool opened = s.drv->FileOpen(&df, (char*)path, 0);
						DBP_ASSERT(opened);
						df->AddRef();
						Bit32u remain = size;
						for (Bit16u read; remain && df->Read(buf, &(read = sizeof(buf))) && read; remain -= read)
						{
							crc32 = DriveCalculateCRC32(buf, read, crc32);
							s.failed |= !fwrite(buf, read, 1, s.f);
						}
						DBP_ASSERT(remain == 0);
						df->Close();
						delete df;
						s.save_size += size;
					}
					fseek_wrap(s.f, s.local_file_offset, SEEK_SET);
				}

				ZIP_WRITE_LE32(buf+ 0, 0x04034b50); // Local file header signature
				ZIP_WRITE_LE16(buf+ 4, 0);          // Version needed to extract (minimum)
				ZIP_WRITE_LE16(buf+ 6, 0);          // General purpose bit flag 
				ZIP_WRITE_LE16(buf+ 8, 0);          // Compression method
				ZIP_WRITE_LE16(buf+10, time);       // File last modification time
				ZIP_WRITE_LE16(buf+12, date);       // File last modification date
				ZIP_WRITE_LE32(buf+14, crc32);      // CRC-32 of uncompressed data
				ZIP_WRITE_LE32(buf+18, size);       // Compressed size
				ZIP_WRITE_LE32(buf+22, size);       // Uncompressed size
				ZIP_WRITE_LE16(buf+26, pathLen);    // File name length
				ZIP_WRITE_LE16(buf+28, 0);          // Extra field length

				//File name (with \ changed to /)
				for (char* pIn = (char*)path, *pOut = (char*)(buf+30); *pIn; pIn++, pOut++)
					*pOut = (*pIn == '\\' ? '/' : *pIn);
				if (is_dir)
					buf[30 + pathLen - 1] = '/';

				s.failed |= !fwrite(buf, 30 + pathLen, 1, s.f);
				if (size) { fseek_wrap(s.f, size, SEEK_CUR); }

				size_t centralDirPos = s.central_dir.size();
				s.central_dir.resize(centralDirPos + 46 + pathLen);
				Bit8u* cd = &s.central_dir[0] + centralDirPos;

				ZIP_WRITE_LE32(cd+0, 0x02014b50);         // Central directory file header signature 
				ZIP_WRITE_LE16(cd+4, 0);                  // Version made by (0 = DOS)
				memcpy(cd+6, buf+4, 26);                  // copy middle section shared with local file header
				ZIP_WRITE_LE16(cd+32, 0);                 // File comment length
				ZIP_WRITE_LE16(cd+34, 0);                 // Disk number where file starts
				ZIP_WRITE_LE16(cd+36, 0);                 // Internal file attributes
				ZIP_WRITE_LE32(cd+38, extAttr);           // External file attributes
				ZIP_WRITE_LE32(cd+42, s.local_file_offset); // Relative offset of local file header
				memcpy(cd + 46, buf + 30, pathLen);       // File name

				s.local_file_offset += 30 + pathLen + size;
				s.file_count++;
			}
		};

		Saver s;
		unionDriveImpl* impl = (unionDriveImpl*)implPtr;
		LOG_MSG("[DOSBOX] Saving filesystem modifications to %s", impl->save_file.c_str());
		s.f = fopen_wrap(impl->save_file.c_str(), "wb");
		if (!s.f)
		{
			LOG_MSG("[DOSBOX] Opening file %s for writing failed", impl->save_file.c_str());
			impl->ScheduleSave(5000.f);
			return;
		}
		s.drv = impl->over;
		s.local_file_offset = s.save_size = 0;
		s.file_count = 0;
		s.failed = false;
		DriveFileIterator(s.drv, Saver::WriteFiles, (Bitu)&s);

		for (StringToPointerHashMap<Union_Modification>::Iterator it = impl->modifications.begin(), end = impl->modifications.end(); it != end; ++it)
			(*it)->Serialize(s.mods);
		if (s.mods.size())
			Saver::WriteFiles("FILEMODS.DBP", false, (Bit32u)s.mods.size(), 0, 0, 0, (Bitu)&s);

		if (s.file_count)
		{
			s.failed |= !fwrite(&s.central_dir[0], s.central_dir.size(), 1, s.f);
		}

		Bit8u eocd[22];
		ZIP_WRITE_LE32(eocd+ 0, 0x06054b50);                   // End of central directory signature
		ZIP_WRITE_LE16(eocd+ 4, 0);                            // Number of this disk
		ZIP_WRITE_LE16(eocd+ 6, 0);                            // Disk where central directory starts
		ZIP_WRITE_LE16(eocd+ 8, s.file_count);                 // Number of central directory records on this disk
		ZIP_WRITE_LE16(eocd+10, s.file_count);                 // Total number of central directory records
		ZIP_WRITE_LE32(eocd+12, (Bit32u)s.central_dir.size()); // Size of central directory (bytes)
		ZIP_WRITE_LE32(eocd+16, s.local_file_offset);          // Offset of start of central directory, relative to start of archive
		ZIP_WRITE_LE16(eocd+20, 0);                            // Comment length (n)
		s.failed |= !fwrite(eocd, 22, 1, s.f);
		fclose(s.f);

		if (s.failed)
		{
			LOG_MSG("[DOSBOX] Error while writing file %s", impl->save_file.c_str());
			impl->ScheduleSave(5000.f);
			return;
		}
		impl->save_size = s.save_size;
		impl->dirty = false;
	}

	void ScheduleSave(float delay_ms = 0)
	{
		if (save_file.empty()) return;
		if (!delay_ms)
		{
			// The larger the save data, the bigger the delay until we write it to disk (1 up to 60 seconds)
			delay_ms = 1000.f + 1000.f * (save_size / (float)(1024 * 1024));
			if (delay_ms > 60000.f) delay_ms = 60000.f;
		}
		PIC_RemoveSpecificEvents(WriteSaveFile, (Bitu)this);
		PIC_AddEvent(WriteSaveFile, delay_ms, (Bitu)this);
		dirty = true;
	}

	void ForceCloseFileAndScheduleSave(DOS_Drive* drv, const char* path)
	{
		DriveForceCloseFile(drv, path);
		ScheduleSave();
	}
};

struct Union_WriteHandle : public DOS_File
{
	unionDriveImpl* impl;
	DOS_File *real_file;
	bool need_copy_on_write, dirty;

	Union_WriteHandle(unionDriveImpl* _impl, DOS_File *_real_file, Bit32u _flags, const char* path, bool _need_copy_on_write)
		: impl(_impl), real_file(_real_file), need_copy_on_write(_need_copy_on_write), dirty(false)
	{
		real_file->AddRef();
		DBP_ASSERT(real_file->refCtr == 1);
		date = real_file->date;
		time = real_file->time;
		attr = real_file->attr;
		flags = _flags;
		SetName(path);
		open = true;
	}

	~Union_WriteHandle()
	{
		DBP_ASSERT(!open && !real_file);
	}

	virtual bool Close()
	{
		if (dirty)
		{
			impl->ScheduleSave();
			dirty = false;
		}
		if (refCtr == 1)
		{
			open = false;
			if (real_file) { real_file->Close(); delete real_file; real_file = NULL; }
		}
		return true;
	}

	virtual bool Read(Bit8u* data, Bit16u* size)
	{
		if (!OPEN_IS_READING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (!real_file) return FALSE_SET_DOSERR(INVALID_HANDLE);
		return real_file->Read(data, size);
	}

	virtual bool Write(Bit8u* data, Bit16u* size)
	{
		if (!OPEN_IS_WRITING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (need_copy_on_write)
		{
			if (!real_file) return FALSE_SET_DOSERR(INVALID_HANDLE);

			Bit32u org_pos = 0, start_pos = 0;
			real_file->Seek(&org_pos, SEEK_CUR);
			if (!*size)
			{
				// size 0 resizes/truncates the file, not needed if seek pos is already at end
				Bit32u realfilesize = 0;
				real_file->Seek(&realfilesize, SEEK_END);
				if (realfilesize == org_pos) return true;
			}
			real_file->Seek(&start_pos, SEEK_SET);

			const Bit16u save_errorcode = dos.errorcode;
			DOS_File *clone_write;
			if (!impl->over->FileCreate(&clone_write, name, DOS_ATTR_ARCHIVE))
			{
				CreateParentDirs(*impl->over, name);
				if (!impl->over->FileCreate(&clone_write, name, DOS_ATTR_ARCHIVE))
				{
					// Should not happen, maybe disk is full
					return FALSE_SET_DOSERR(ACCESS_DENIED);
				}
			}
			clone_write->AddRef();

			Bit8u buf[4096];
			for (Bit16u read; real_file->Read(buf, &(read = sizeof(buf))) && read;)
			{
				Bit16u write = read;
				if (!clone_write->Write(buf, &write) || write != read)
				{
					// Should not happen, maybe disk full
					clone_write->Close();
					real_file->Close();
					delete clone_write;
					delete real_file;
					impl->over->FileUnlink(name);
					real_file = NULL;
					return FALSE_SET_DOSERR(ACCESS_DENIED);
				}
			}

			real_file->Close();
			delete real_file;
			real_file = clone_write;
			real_file->Seek(&org_pos, SEEK_SET);
			real_file->flags = flags;
			need_copy_on_write = false;
			dos.errorcode = save_errorcode;
		}
		if (!dirty) dirty = true;
		return real_file->Write(data, size);
	}

	virtual bool Seek(Bit32u* pos, Bit32u type)
	{
		if (real_file) return real_file->Seek(pos, type);
		*pos = 0;
		return false;
	}

	virtual Bit16u GetInformation(void)
	{
		return 0; //writable storage
	}
};

unionDrive::unionDrive(DOS_Drive& under, DOS_Drive& over, bool autodelete_under, bool autodelete_over) : impl(new unionDriveImpl(&under, &over, NULL, autodelete_under, autodelete_over))
{
	label.SetLabel(under.GetLabel(), false, true);
}

unionDrive::unionDrive(DOS_Drive& under, const char* save_file, bool autodelete_under, bool strict_mode) : impl(new unionDriveImpl(&under, NULL, save_file, autodelete_under, false, strict_mode))
{
	label.SetLabel(under.GetLabel(), false, true);
}

void unionDrive::AddUnder(DOS_Drive& add_under, bool autodelete_under)
{
	impl->under = new unionDrive(add_under, *impl->under, autodelete_under, impl->autodelete_under);
	impl->autodelete_under = true;
}

unionDrive::~unionDrive()
{
	ForceCloseAll();
	delete impl;
}

bool unionDrive::FileOpen(DOS_File * * file, char * path, Bit32u flags)
{
	if (!OPEN_CHECK_ACCESS_CODE(flags)) return FALSE_SET_DOSERR(ACCESS_CODE_INVALID);
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(path);
	if (!*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Union_Modification* m = impl->modifications.Get(path);
	if (m && m->IsRedirect() && m->RedirectType() == Union_Modification::TDIR) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
	if (m && m->IsDelete()) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
	const Bit16u save_errorcode = dos.errorcode;
	if (OPEN_IS_WRITING(flags))
	{
		if (!impl->writable) return FALSE_SET_DOSERR(ACCESS_DENIED);
		DOS_File *real_file;
		bool need_copy_on_write;
		if (impl->over->FileOpen(&real_file, path, flags))
		{
			DBP_ASSERT(!m);
			need_copy_on_write = false;
		}
		else
		{
			if (impl->over->TestDir(path)) { DBP_ASSERT(0); return FALSE_SET_DOSERR(FILE_NOT_FOUND); }
			if (!impl->under->FileOpen(&real_file, (m ? m->RedirectSource() : path), OPEN_READ))
			{
				if (m)
				{
					// File disappeared, maybe removed in mounted outer filesystem
					delete m;
					impl->modifications.Remove(path);
				}
				return FALSE_SET_DOSERR(FILE_NOT_FOUND);
			}
			#if 0
			// Copy entire file to overlay now on open
			DOS_File *clone_write;
			if (!impl->over->FileCreate(&clone_write, path, DOS_ATTR_ARCHIVE))
			{
				CreateParentDirs(*impl->over, path);
				if (!impl->over->FileCreate(&clone_write, path, DOS_ATTR_ARCHIVE))
				{
					// Should not happen, maybe disk is full
					delete real_file;
					return FALSE_SET_DOSERR(ACCESS_DENIED);
				}
			}
			Bit8u buf[1024];
			for (Bit16u read; real_file->Read(buf, &(read = sizeof(buf))) && read;)
			{
				Bit16u write = read;
				if (!clone_write->Write(buf, &write) || write != read)
				{
					// Should not happen, maybe disk full
					delete real_file;
					delete clone_write;
					impl->over->FileUnlink(path);
					return FALSE_SET_DOSERR(ACCESS_DENIED);
				}
			}
			delete real_file;
			delete clone_write;
			if (!impl->over->FileOpen(&real_file, path, flags)) { DBP_ASSERT(false); return false; }
			#endif
			// Only copy file to overlay on first write operation
			need_copy_on_write = true;
		}
		*file = new Union_WriteHandle(impl, real_file, flags, path_org, need_copy_on_write); 
		return TRUE_RESET_DOSERR;
	}
	else // open readable
	{
		// No need to call AddRef on the opened file here, it will be done by our caller
		if (m && m->IsRedirect()) return impl->under->FileOpen(file, m->RedirectSource(), flags);
		if (!impl->over->FileOpen(file, path, flags) && !impl->under->FileOpen(file, path, flags)) return false;
		return TRUE_RESET_DOSERR;
	}
}

bool unionDrive::FileCreate(DOS_File** file, char * path, Bit16u attributes)
{
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(path);
	if ((attributes & DOS_ATTR_DIRECTORY) || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
	if (!impl->UnionPrepareCreate(path, true)) return false;

	const Bit16u save_errorcode = dos.errorcode;
	DOS_File *real_file;
	if (!impl->over->FileCreate(&real_file, path, attributes))
	{
		CreateParentDirs(*impl->over, path);
		if (!impl->over->FileCreate(&real_file, path, attributes))
		{
			// Should not happen, maybe disk is full
			return FALSE_SET_DOSERR(ACCESS_DENIED);
		}
	}
	*file = new Union_WriteHandle(impl, real_file, OPEN_READWRITE, path_org, false);
	impl->ScheduleSave();
	return TRUE_RESET_DOSERR;
}

bool unionDrive::Rename(char * oldpath, char * newpath)
{
	DOSPATH_REMOVE_ENDINGDOTS(oldpath);
	DOSPATH_REMOVE_ENDINGDOTS(newpath);
	if (!impl->writable || !*oldpath || !*newpath) return FALSE_SET_DOSERR(ACCESS_DENIED);
	if (!strcmp(oldpath, newpath)) return true; //rename with same name is always ok

	// Table of possible scenarios
	// v OLD | NEW >|  REDIRECTED  |    DELETED    |    EITHER    |   UNIQUE  
	// ---------------------------------------------------------------------------
	// REDIRECTED   | ERR EXISTS   | FIXUP         | ERR EXISTS   | FIXUP
	// DELETED      | ERR NOTFOUND | ERR NOTFOUND  | ERR NOTFOUND | ERR NOTFOUND
	// BOTH         | ERR EXISTS   | REM+MDEL+MRED | ERR EXISTS   | MDEL+MRED
	// OVER         | ERR EXISTS   | REM+MRED      | ERR EXISTS   | MRED
	// UNDER        | ERR EXISTS   | REM+MDEL+MRED | ERR EXISTS   | MDEL+MRED
	// UNIQUE       | ERR NOTFOUND | ERR NOTFOUND  | ERR NOTFOUND | ERR NOTFOUND
	bool is_file, in_under;
	Union_Modification* old_m = impl->modifications.Get(oldpath);
	Union_Modification* new_m = impl->modifications.Get(newpath);
	if ((old_m && old_m->IsDelete())   || (!old_m && !impl->ExistInOverOrUnder(oldpath, &is_file, &in_under))) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
	if ((new_m && new_m->IsRedirect()) || (!new_m && impl->ExistInOverOrUnder(newpath))) return FALSE_SET_DOSERR(FILE_ALREADY_EXISTS);
	if ((old_m && old_m->RedirectType() == Union_Modification::TDIR) || (!old_m && !is_file))
	{
		//deny access if this rename tries to move a directory into another directory
		const char *oldlastslash = strrchr(oldpath, '\\'), *newlastslash = strrchr(newpath, '\\');
		if ((oldlastslash || newlastslash) && (oldlastslash - oldpath) != (newlastslash - newpath) && memcmp(oldpath, newpath, (newlastslash - newpath))) return FALSE_SET_DOSERR(ACCESS_DENIED);
	}
	impl->ForceCloseFileAndScheduleSave(this, oldpath);
	if (new_m) //means (new_m->IsDelete())
	{
		delete new_m;
		impl->modifications.Remove(newpath); //REM
	}
	if (old_m) //means (old_m->IsRedirect())
	{
		impl->modifications.Remove(oldpath);
		if (strcmp(old_m->RedirectSource(), newpath) == 0)
		{
			delete old_m; // renamed back to original
		}
		else
		{
			old_m->RedirectSetNewPath(newpath);
			impl->modifications.Put(newpath, old_m);
		}
		return true;
	}
	if (in_under)
	{
		// mark deletion
		impl->modifications.Put(oldpath, new Union_Modification(oldpath)); //MDEL
	}
	const Bit16u save_errorcode = dos.errorcode;
	if (!impl->over->Rename(oldpath, newpath))
	{
		// mark redirect
		DBP_ASSERT(in_under);
		impl->modifications.Put(newpath, new Union_Modification(newpath, oldpath, is_file)); //MRED
	}
	return TRUE_RESET_DOSERR;
}

bool unionDrive::FileUnlink(char * path)
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	return impl->UnionUnlink(this, path, Union_Modification::TFILE, dos.errorcode);
}

bool unionDrive::FileExists(const char* path)
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	// The DOS_Drive API should be fixed so everything is const char* then this cast wouldn't be needed
	return impl->UnionTest((char*)path, Union_Modification::TFILE);
}

bool unionDrive::MakeDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	const Bit16u save_errorcode = dos.errorcode;
	if (!impl->UnionPrepareCreate(dir_path, false) || !impl->over->MakeDir(dir_path)) return false;
	impl->ScheduleSave();
	return TRUE_RESET_DOSERR;
}

bool unionDrive::RemoveDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	const Bit16u save_errorcode = dos.errorcode;
	bool not_empty = false;
	RealPt save_dta = dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	dta.SetupSearch(255, (Bit8u)(0xffff & ~DOS_ATTR_VOLUME), (char*)"*.*");
	for (bool more = FindFirst(dir_path, dta); more; more = FindNext(dta))
	{
		char dta_name[DOS_NAMELENGTH_ASCII]; Bit32u dta_size; Bit16u dta_date, dta_time; Bit8u dta_attr;
		dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
		if (dta_name[0] == '.' && dta_name[dta_name[1] == '.' ? 2 : 1] == '\0') continue;
		not_empty = true;
	}
	dos.dta(save_dta);
	if (not_empty) return FALSE_SET_DOSERR(ACCESS_DENIED); // not empty
	return impl->UnionUnlink(this, dir_path, Union_Modification::TDIR, save_errorcode);
}

bool unionDrive::TestDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	return impl->UnionTest(dir_path, Union_Modification::TDIR);
}

bool unionDrive::FindFirst(char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	if (!TestDir(dir_path)) return FALSE_SET_DOSERR(PATH_NOT_FOUND);
	Union_Search* s;
	size_t dir_len = strlen(dir_path);
	if (dir_len >= sizeof(s->dir)) { DBP_ASSERT(false); return false; }

	if (impl->free_search_ids.empty())
	{
		dta.SetDirID((Bit16u)impl->searches.size());
		impl->searches.resize(impl->searches.size() + 1);
		s = &impl->searches.back();
	}
	else
	{
		dta.SetDirID(impl->free_search_ids.back());
		impl->free_search_ids.pop_back();
		s = &impl->searches[dta.GetDirID()];
	}
	s->step = 0;
	s->dir_len = (Bit8u)dir_len;
	s->fcb_findfirst = fcb_findfirst;
	s->dir_hash = impl->modifications.Hash(dir_path);
	if (dir_len) s->dir_hash = impl->modifications.Hash("\\", 1, s->dir_hash); // also hash a \ at the end
	memcpy(s->dir, dir_path, dir_len + 1); // with '\0' terminator

	if (DriveFindDriveVolume(this, dir_path, dta, fcb_findfirst)) return true;
	return FindNext(dta);
}

bool unionDrive::FindNext(DOS_DTA & dta)
{
	if (dta.GetDirID() >= impl->searches.size()) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Bit16u my_dir_id = dta.GetDirID();
	Union_Search& s = impl->searches[my_dir_id];
	char dta_path[sizeof(s.dir) + 1 + DOS_NAMELENGTH_ASCII];Bit32u dta_size;Bit16u dta_date;Bit16u dta_time;Bit8u dta_attr;
	char* dta_name = dta_path + (s.dir_len ? s.dir_len + 1 : 0);
	Bit8u attr;char pattern[DOS_NAMELENGTH_ASCII];

	if (s.step < 2)
	{
		dta.GetSearchParams(attr,pattern);
		while (s.step < 2)
		{
			const char* dotted = (s.step++ ? ".." : ".");
			if (!WildFileCmp(dotted, pattern) || !s.dir_len) continue;
			FileStat_Block stat;
			FileStat(s.dir, &stat); // both '.' and '..' return the stats from the current dir
			if (~attr & (Bit8u)stat.attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
			dta.SetResult(dotted, 0, stat.date, stat.time, (Bit8u)stat.attr);
			return true;
		}
	}

	const Bit16u save_errorcode = dos.errorcode;
	switch (s.step)
	{
		case 2:
			if (!impl->under->FindFirst(s.dir, dta, s.fcb_findfirst)) goto case_over_find_first;
			s.sub_dirID = dta.GetDirID();
			/* fall through */
		case 3:
			if (s.dir_len) { memcpy(dta_path, s.dir, s.dir_len); dta_path[s.dir_len] = '\\'; }
			for (;;)
			{
				if (s.step == 2) s.step = 3;
				else
				{
					dta.SetDirID(s.sub_dirID);
					if (!impl->under->FindNext(dta)) goto case_over_find_first;
					s.sub_dirID = dta.GetDirID();
				}
				dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
				if (dta_attr & DOS_ATTR_VOLUME) continue;
				if (dta_name[0] == '.' && dta_name[dta_name[1] == '.' ? 2 : 1] == '\0') continue;
				if (impl->over->FileExists(dta_path) || impl->over->TestDir(dta_path)) continue;
				if (impl->modifications.Get(dta_name, DOS_NAMELENGTH_ASCII, s.dir_hash)) continue;
				dta.SetDirID(my_dir_id);
				return TRUE_RESET_DOSERR;
			}

		case_over_find_first:
			if (!impl->over->FindFirst(s.dir, dta, s.fcb_findfirst)) goto case_over_done;
			s.sub_dirID = dta.GetDirID();
			/* fall through */
		case 4:
			for (;;)
			{
				if (s.step <= 3) s.step = 4;
				else
				{
					dta.SetDirID(s.sub_dirID);
					if (!impl->over->FindNext(dta)) goto case_over_done;
					s.sub_dirID = dta.GetDirID();
				}
				dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
				if (dta_attr & DOS_ATTR_VOLUME) continue;
				if (dta_name[0] == '.' && dta_name[dta_name[1] == '.' ? 2 : 1] == '\0') continue;
				dta.SetDirID(my_dir_id);
				return TRUE_RESET_DOSERR;
			}

		case_over_done:
			dta.SetDirID(my_dir_id);
			s.step = 5;
			/* fall through */
		default:
			if (s.step < 0) return FALSE_SET_DOSERR(NO_MORE_FILES);
			dta.GetSearchParams(attr,pattern);
			for (Bit32u i = impl->modifications.Capacity() - (s.step++ - 5); i--; s.step++) // iterate reverse to better deal with "DEL *.*"
			{
				Union_Modification* m = impl->modifications.GetAtIndex(i);
				if (!m || !m->IsRedirect()) continue;
				if (m->RedirectDirLen() != s.dir_len) continue;
				const char *redirect_target = m->RedirectTarget(), *redirect_newname = redirect_target + (s.dir_len ? s.dir_len + 1 : 0);
				if (memcmp(redirect_target, s.dir, s.dir_len) || !WildFileCmp(redirect_newname, pattern)) continue;
				FileStat_Block filestat;
				if (!impl->under->FileStat(m->RedirectSource(), &filestat)) continue;
				if (~attr & (Bit8u)filestat.attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
				dta.SetResult(redirect_newname, filestat.size, filestat.date, filestat.time, (Bit8u)filestat.attr);
				return TRUE_RESET_DOSERR;
			}
			s.step = -1;
			impl->free_search_ids.push_back(dta.GetDirID());
			return FALSE_SET_DOSERR(NO_MORE_FILES);
	}
}

bool unionDrive::FileStat(const char* path, FileStat_Block * const stat_block)
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	if (!*path) return impl->under->FileStat(path, stat_block); //get time stamps for root directory from underlying drive
	Union_Modification* m = impl->modifications.Get(path);
	if (m && m->IsDelete())   return false;
	if (m && m->IsRedirect()) return impl->under->FileStat(m->RedirectSource(), stat_block);
	return (impl->over->FileStat(path, stat_block) || impl->under->FileStat(path, stat_block));
}

bool unionDrive::GetFileAttr(char * path, Bit16u * attr)
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	Union_Modification* m = impl->modifications.Get(path);
	if (m && m->IsDelete())   return false;
	if (m && m->IsRedirect()) return impl->under->GetFileAttr(m->RedirectSource(), attr);
	return (impl->over->GetFileAttr(path, attr) || impl->under->GetFileAttr(path, attr));
}

bool unionDrive::GetLongFileName(const char* path, char longname[256])
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	Union_Modification* m = impl->modifications.Get(path);
	if (m && m->IsDelete())   return false;
	if (m && m->IsRedirect()) return impl->under->GetLongFileName(m->RedirectSource(), longname);
	return (impl->over->GetLongFileName(path, longname) || impl->under->GetLongFileName(path, longname));
}

bool unionDrive::AllocationInfo(Bit16u * _bytes_sector, Bit8u * _sectors_cluster, Bit16u * _total_clusters, Bit16u * _free_clusters)
{
	Bit16u under_bytes_sector; Bit8u under_sectors_cluster; Bit16u under_total_clusters; Bit16u under_free_clusters;
	Bit16u over_bytes_sector;  Bit8u over_sectors_cluster;  Bit16u over_total_clusters;  Bit16u over_free_clusters;
	impl->under->AllocationInfo(&under_bytes_sector, &under_sectors_cluster, &under_total_clusters, &under_free_clusters);
	impl->over->AllocationInfo( &over_bytes_sector,  &over_sectors_cluster,  &over_total_clusters,  &over_free_clusters );
	Bit32u under_bytes = under_total_clusters * under_sectors_cluster * under_bytes_sector;
	Bit32u over_bytes  = over_total_clusters  * over_sectors_cluster  * over_bytes_sector;
	Bit32u free_bytes  = over_free_clusters   * over_sectors_cluster  * over_bytes_sector;
	*_bytes_sector    = (under_bytes_sector    > over_bytes_sector    ? under_bytes_sector    : over_bytes_sector   );
	*_sectors_cluster = (under_sectors_cluster > over_sectors_cluster ? under_sectors_cluster : over_sectors_cluster);
	Bit32u cluster_div = (*_bytes_sector && *_sectors_cluster ? *_bytes_sector * *_sectors_cluster : 1);
	*_total_clusters = (under_bytes > over_bytes ? under_bytes : over_bytes) / cluster_div;
	*_free_clusters  = free_bytes / cluster_div;
	return true;
}

bool unionDrive::GetShadows(DOS_Drive*& a, DOS_Drive*& b) { a = impl->under; b = impl->over; return true; }
Bit8u unionDrive::GetMediaByte(void) { return impl->over->GetMediaByte(); }
bool unionDrive::isRemote(void) { return false; }
bool unionDrive::isRemovable(void) { return false; }
Bits unionDrive::UnMount(void) { delete this; return 0;  }

#include <dbp_serialize.h>
DBP_SERIALIZE_SET_POINTER_LIST(PIC_EventHandler, unionDrive, unionDriveImpl::WriteSaveFile);
