/*
 *  Copyright (C) 2023 Bernhard Schelling
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

struct Patch_Entry
{
protected:
	Patch_Entry(Bit16u _attr, const char* _name, Bit16u _date, Bit16u _time) : date(_date), time(_time), attr(_attr)
	{
		size_t namesize = strlen(_name) + 1;
		if (namesize > sizeof(name)) { DBP_ASSERT(false); namesize = sizeof(name); }
		memcpy(name, _name, namesize);
	}
	~Patch_Entry() {}

public:
	inline bool IsFile()      { return ((attr & DOS_ATTR_DIRECTORY) == 0); }
	inline bool IsDirectory() { return ((attr & DOS_ATTR_DIRECTORY) != 0); }
	inline struct Patch_File*      AsFile()      { return (Patch_File*)this;      }
	inline struct Patch_Directory* AsDirectory() { return (Patch_Directory*)this; }

	Bit16u date, time, attr;
	char name[DOS_NAMELENGTH_ASCII];
};

struct Patch_File : Patch_Entry
{
	std::vector<Bit8u> mem_data;
	Bit32u refs;
	enum EType { TYPE_RAW, TYPE_PATCH } type;
	bool patched;
	char patchpath[DOS_PATHLENGTH+1];

	Patch_File(EType _type, Bit16u _attr, const char* filename, Bit16u _date, Bit16u _time) : Patch_Entry(_attr, filename, _date, _time), refs(0), type(_type), patched(false) { DBP_ASSERT(IsFile()); }

	inline Bit32u Size(DOS_Drive& under, zipDrive* patchzip)
	{
		if (type == TYPE_RAW)
		{
			FileStat_Block stat;
			patchzip->FileStat(patchpath, &stat);
			return stat.size;
		}

		if (!patched) DoPatch(under, *patchzip);

		return (Bit32u)mem_data.size();
	}

	void DoPatch(DOS_Drive& under, zipDrive& patchzip)
	{
		patched = true;
		char underpath[DOS_PATHLENGTH+1];
		strcpy(underpath, patchpath);
		char *lastslash = strrchr(underpath, '\\');
		strcpy((lastslash ? lastslash + 1 : underpath), name);

		struct Local
		{
			static bool GetU24(DOS_File* df, Bit32u& res)
			{
				Bit8u buf[3]; Bit16u num = 3;
				if (df->Read(buf, &num) && num == 3) { res = (Bit32u)((buf[0] << 16) | (buf[1] << 8) | buf[2]); return true; } else { return false; }
			}
			static bool GetU16(DOS_File* df, Bit16u& res)
			{
				Bit8u buf[2]; Bit16u num = 2;
				if (df->Read(buf, &num) && num == 2) { res = (Bit16u)((buf[0] << 8) | buf[1]); return true; } else { return false; }
			}
			static bool GetU8(DOS_File* df, Bit8u& res)
			{
				Bit8u buf; Bit16u num = 1;
				if (df->Read(&buf, &num) && num == 1) { res = buf; return true; } else { return false; }
			}
			static bool Read(DOS_File* df, Bit8u* p, Bit32u sz)
			{
				for (Bit16u read; sz; sz -= read, p += read) { read = (Bit16u)(sz > 0xFFFF ? 0xFFFF : sz); if (!df->Read(p, &read)) return false; }
				return true;
			}
		};

		FileStat_Block stat;
		DOS_File* df;
		if (!under.FileStat(underpath, &stat) || !under.FileOpen(&df, underpath, 0)) { DBP_ASSERT(false); return; }
		mem_data.resize(stat.size);
		df->AddRef();
		if (!Local::Read(df, (stat.size ? &mem_data[0] : NULL), stat.size)) { DBP_ASSERT(0); }
		df->Close();
		delete df;

		struct VCDiff
		{
			static bool Get7BitInt(DOS_File* df, Bit32u& res)
			{
				Bit8u buf; Bit16u num = 1; res = 0;
				do { if (!df->Read(&buf, &num) || num != 1) return false; res = (res << 7) + (buf & 0x7f); } while(buf & 0x80);
				return true;
			}
			static Bit32u Get7BitInt(Bit8u*& ptr, Bit8u* end)
			{
				Bit32u res = 0; Bit8u buf;
				do { if (ptr >= end) return false; buf = *(ptr++); res = (res << 7) + (buf & 0x7f); } while(buf & 0x80);
				return res;
			}

			enum { VCD_DECOMPRESS = 1, VCD_CODETABLE = 2, VCD_METADATA = 4, VCD_SOURCE = 1, VCD_TARGET = 2, VCD_ADLER32 = 4 };
			enum { VCD_NOOP = 0, VCD_ADD = 1, VCD_RUN = 2, VCD_COPY = 3,  VCD_NEAR_SIZE = 4, VCD_SAME_SIZE = 3, VCD_SELF = 0, VCD_HERE = 1 };

			struct VCDWindow
			{
				Bit8u indicator, *datas, *datas_end, *instructions, *instructions_end, *addresses, *addresses_end;
				Bit32u source_len, source_pos, target_len;
				std::vector<Bit8u> buf;
			};

			// Big tables, allocate on heap not on stack
			struct VCDInstruction { Bit8u type, size, mode; } codetable[256][2];
			struct { Bit32u arrnear[VCD_NEAR_SIZE], arrsame[VCD_SAME_SIZE*256], next_slot; } cache;

			VCDiff()
			{
				// Build default instruction code table
				memset(codetable, 0, sizeof(codetable));
				codetable[0][0] = { VCD_RUN, 0, 0 };
				Bit8u idx = 1;
				for (; idx != 19; idx++) codetable[idx][0] = { VCD_ADD, (Bit8u)(idx - 1), 0 };
				for (Bit8u mode = 0; mode < 9; mode++)
				{
					codetable[idx++][0] = { VCD_COPY, 0, mode };
					for (Bit8u size = 4; size < 19; size++) codetable[idx++][0] = { VCD_COPY, size, mode };
				}
				for (Bit8u mode = 0; mode < 6; mode++)
					for (Bit8u add_size = 1; add_size < 5; add_size++)
						for (Bit8u copy_size = 4; copy_size < 7; copy_size++)
							codetable[idx][0] = { VCD_ADD, add_size, 0 }, codetable[idx++][1] = { VCD_COPY, copy_size, mode };
				for (Bit8u mode = 6; mode < 9; mode++)
					for (Bit8u add_size = 1; add_size < 5; add_size++)
						codetable[idx][0] = { VCD_ADD, add_size, 0 }, codetable[idx++][1] = { VCD_COPY, 4, mode };
				for (Bit8u mode = 0; mode < 9; mode++)
					codetable[idx][0] = { VCD_COPY, 4, mode }, codetable[idx++][1] = { VCD_ADD, 1, 0 };
			}

			static bool GetWindow(VCDWindow& w, DOS_File* df)
			{
				Bit8u compress_mode; Bit32u delta_len, add_run_data_len, instructions_len, addresses_len;
				if (!Local::GetU8(df, w.indicator)) return false;
				if ((w.indicator & (VCD_SOURCE | VCD_TARGET)) && (!Get7BitInt(df, w.source_len) || !Get7BitInt(df, w.source_pos))) return false;
				if (!Get7BitInt(df, delta_len) || !Get7BitInt(df, w.target_len)) return false;
				if (!Local::GetU8(df, compress_mode)) return false;
				if (!Get7BitInt(df, add_run_data_len) || !Get7BitInt(df, instructions_len) || !Get7BitInt(df, addresses_len)) return false;
				if (w.indicator & VCD_ADLER32) { Bit32u skip = 4; df->Seek(&skip, DOS_SEEK_CUR); }

				Bit32u len = add_run_data_len + instructions_len + addresses_len;
				if (w.buf.size() <= len) w.buf.resize(len+1);
				if (!Local::Read(df, &w.buf[0], len)) return false;
				w.datas = &w.buf[0];
				w.instructions = w.datas_end = w.datas + add_run_data_len;
				w.addresses = w.instructions_end = w.instructions + instructions_len;
				w.addresses_end = w.addresses + addresses_len;
				return true;
			}

			Bit32u CacheAddress(Bit32u here, Bit8u mode, Bit8u*& addresses, Bit8u* addresses_end)
			{
				Bit32u addr;
				if      (mode == VCD_SELF)         addr = Get7BitInt(addresses, addresses_end);
				else if (mode == VCD_HERE)         addr = here - Get7BitInt(addresses, addresses_end);
				else if (mode < (VCD_NEAR_SIZE+2)) addr = cache.arrnear[mode - 2] + Get7BitInt(addresses, addresses_end);
				else                               addr = cache.arrsame[(mode - (2 + VCD_NEAR_SIZE)) * 256 + (addresses < addresses_end ? *(addresses++) : 0)];

				if (VCD_NEAR_SIZE > 0) { cache.arrnear[cache.next_slot] = addr; cache.next_slot = (cache.next_slot + 1) % VCD_NEAR_SIZE; }
				if (VCD_SAME_SIZE > 0) { cache.arrsame[addr % (VCD_SAME_SIZE * 256)] = addr; }

				return addr;
			}

			bool Process(std::vector<Bit8u>& in_data, DOS_File* df)
			{
				Bit8u indicator = 0; Bit32u num;
				df->Seek(&(num = 4), DOS_SEEK_SET); // skip past header
				Local::GetU8(df, indicator);
				if (0) { invalid_vcdiff: LOG_MSG("[DOSBOX] VCDIFF/XDELTA file or source was invalid"); return false; }
				if (indicator & VCD_DECOMPRESS) { LOG_MSG("[DOSBOX] VCDIFF/XDELTA secondary compression not supported"); return false; }
				if ((indicator & VCD_CODETABLE) && Get7BitInt(df, num) && num) { LOG_MSG("[DOSBOX] VCDIFF/XDELTA application-defined code table not supported"); return false; }
				if ((indicator & VCD_METADATA) && Get7BitInt(df, num) && num) df->Seek(&num, DOS_SEEK_CUR); // ignore

				if (!in_data.size()) in_data.resize(1); // make sure [] operator doesn't fail
				std::vector<Bit8u> out_data;
				out_data.resize(in_data.size());

				Bit32u out_pos = 0;
				for (VCDWindow win; GetWindow(win, df); out_pos += win.target_len)
				{
					Bit32u written_pos = 0;
					memset(&cache, 0, sizeof(cache));

					while (win.instructions < win.instructions_end)
					{
						for (Bit8u instruction_idx = *(win.instructions++), side = 0; side != 2; side++)
						{
							VCDInstruction instr = codetable[instruction_idx][side];
							if (instr.type == VCD_NOOP) continue;
							const Bit32u size = (instr.size ? (Bit32u)instr.size : Get7BitInt(win.instructions, win.instructions_end));
							const Bit32u out_ofs = out_pos + written_pos;
							if (out_ofs + size > out_data.size()) out_data.resize(out_ofs + size);

							if (instr.type == VCD_ADD)
							{
								if (win.datas + size > win.datas_end) { DBP_ASSERT(false); goto invalid_vcdiff; }
								memcpy(&out_data[out_ofs], win.datas, size);
								win.datas += size;
							}
							else if (instr.type == VCD_COPY)
							{
								Bit32u addr = CacheAddress(written_pos + win.source_len, instr.mode, win.addresses, win.addresses_end);
								if ((win.indicator & (VCD_SOURCE | VCD_TARGET)) && addr < win.source_len)
								{
									memcpy(&out_data[out_ofs], &((win.indicator & VCD_SOURCE) ? in_data : out_data)[win.source_pos + addr], size);
								}
								else
								{
									Bit8u *trg = &out_data[out_ofs], *src = &out_data[out_pos + (addr - win.source_len)], *src_end = src + size;
									while (src != src_end) *(trg++) = *(src++);
								}
							}
							else if (instr.type == VCD_RUN)
							{
								if (win.datas == win.datas_end) { DBP_ASSERT(false); goto invalid_vcdiff; }
								memset(&out_data[out_ofs], *(win.datas++), size);
							}
							else { DBP_ASSERT(false); goto invalid_vcdiff; }

							written_pos += size;
						}
					}
				}
				out_data.resize(out_pos);
				std::swap(in_data, out_data);
				return true;
			}
		};

		struct IPSPatch
		{
			static bool Process(std::vector<Bit8u>& in_data, DOS_File* df)
			{
				Bit32u ofs, trunc; Bit16u len, rlelen; Bit8u rlebyte;
				df->Seek(&(ofs = 5), DOS_SEEK_SET); // skip over header
				while (Local::GetU24(df, ofs))
				{
					if (ofs == 0x454f46) // EOF marker
					{
						if (Local::GetU24(df, trunc)) in_data.resize(trunc);
						return true;
					}
					if (!Local::GetU16(df, len)) return false;
					if (!len) // RLE
					{
						if (!Local::GetU16(df, rlelen) || !Local::GetU8(df, rlebyte)) return false;
						if (ofs + rlelen > in_data.size()) in_data.resize(ofs + rlelen);
						memset(&in_data[ofs], rlebyte, rlelen);
					}
					else
					{
						if (ofs + len > in_data.size()) in_data.resize(ofs + len);
						df->Read(&in_data[ofs], &len);
					}
				}
				return true; // no EOF marker but still success?
			}
		};

		struct BPSPatch
		{
			static bool GetVarLenInt(DOS_File* df, Bit64u& res)
			{
				Bit8u x; Bit16u num = 1; Bit64u shift = 1; res = 0;
				for (;;) { if (!df->Read(&x, &num) || num != 1) return false; res += (x & 0x7f) * shift; if (x & 0x80) return true; res += (shift <<= 7); }
			}

			static bool Process(std::vector<Bit8u>& in_data, DOS_File* df)
			{
				Bit32u ofs, dfLen; Bit64u sourceLen, targetLen, metaLen, data, len, outputOffset = 0, sourceRelativeOffset = 0, targetRelativeOffset = 0;
				df->Seek(&(dfLen = 0), DOS_SEEK_END);
				df->Seek(&(ofs = 4), DOS_SEEK_SET); // skip over header
				if (!GetVarLenInt(df, sourceLen) || sourceLen != in_data.size() || !GetVarLenInt(df, targetLen) || !GetVarLenInt(df, metaLen) || metaLen > (Bit32u)-1) return false;
				df->Seek(&(ofs = (Bit32u)metaLen), DOS_SEEK_CUR); // skip over meta data
				std::vector<Bit8u> out_data;
				out_data.resize((size_t)targetLen);
				for (Bit8u *source = (sourceLen ? &in_data[0] : NULL), *target = (targetLen ? &out_data[0] : NULL), *p, *pEnd; df->Seek(&(ofs = 0), DOS_SEEK_CUR) && ofs < dfLen - 12; outputOffset += len)
				{
					if (!GetVarLenInt(df, data) || outputOffset + (len = ((data >> 2) + 1)) > targetLen) return false;
					switch (data & 3)
					{
						case 0: // SourceRead
							if (outputOffset + len > sourceLen) return false;
							memcpy(&target[outputOffset], &source[outputOffset], (size_t)len);
							break;
						case 1: // TargetRead
							if (len > (Bit32u)-1 || !Local::Read(df, &target[outputOffset], (Bit32u)len)) return false;
							break;
						case 2: // SourceCopy
							if (!GetVarLenInt(df, data) || (sourceRelativeOffset += (Bit64s)(data >> 1) * ((data & 1) ? -1 : 1)) + len > sourceLen) return false;
							memcpy(&target[outputOffset], &source[sourceRelativeOffset], (size_t)len);
							sourceRelativeOffset += len;
							break;
						case 3: // TargetCopy
							if (!GetVarLenInt(df, data) || (targetRelativeOffset += (Bit64s)(data >> 1) * ((data & 1) ? -1 : 1)) >= outputOffset) return false;
							for (p = &target[outputOffset], pEnd = p + len; p != pEnd;) *(p++) = target[targetRelativeOffset++];
							break;
					}
				}
				if (outputOffset != targetLen) return false;
				std::swap(in_data, out_data);
				return true;
			}
		};

		if (!patchzip.FileOpen(&df, patchpath, 0)) { DBP_ASSERT(false); return; }
		df->AddRef();
		Bit32u hdr = 0;
		Local::GetU24(df, hdr);
		bool success = false;
		if (hdr == 0xd6c3c4) // VCDIFF file
		{
			VCDiff* vcd = new VCDiff; // too large for stack
			success = vcd->Process(mem_data, df);
			delete vcd;
		}
		else if (hdr == 0x504154) // IPS file
		{
			success = IPSPatch::Process(mem_data, df);
		}
		else if (hdr == 0x425053) // BPS file
		{
			success = BPSPatch::Process(mem_data, df);
		}
		df->Close();
		delete df;
		if (!success) { DBP_ASSERT(false); LOG_MSG("[DOSBOX] ERROR: Failed to patch '%s' with invalid patch file '%s'", underpath, patchpath); }
	}
};

struct Patch_Handle : public DOS_File
{
	Patch_Handle(Patch_File* _src, Bit32u _flags, const char* path) : mem_pos(0), src(_src)
	{
		DBP_ASSERT(_src->type == Patch_File::TYPE_PATCH);
		_src->refs++;
		date = _src->date;
		time = _src->time;
		attr = _src->attr;
		flags = _flags;
		SetName(path);
		open = true;
	}

	~Patch_Handle()
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
		return FALSE_SET_DOSERR(ACCESS_DENIED);
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
		return 0x40; // read-only drive
	}

	Bit32u mem_pos;
	Patch_File* src;
};

struct Patch_Directory: Patch_Entry
{
	StringToPointerHashMap<Patch_Entry> entries;

	Patch_Directory(Bit16u _attr, const char* dirname, Bit16u _date, Bit16u _time) : Patch_Entry(_attr, dirname, _date, _time) { DBP_ASSERT(IsDirectory()); }

	~Patch_Directory()
	{
		for (Patch_Entry* e : entries)
		{
			if (e->IsDirectory()) delete e->AsDirectory();
			else delete e->AsFile();
		}
	}
};

struct Patch_Search
{
	Patch_Directory* dir;
	Bit32u index;
	Bit16u over_id;
};

struct patchDriveImpl
{
	Patch_Directory root;
	StringToPointerHashMap<Patch_Directory> directories;
	std::vector<Patch_Search> searches;
	std::vector<Bit16u> free_search_ids;
	DOS_Drive& under;
	zipDrive* patchzip;
	bool autodelete_under;

	patchDriveImpl(DOS_Drive& _under, bool _autodelete_under, DOS_File* _patchzip) : root(DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY, "", 0, 0), under(_under), patchzip(_patchzip ? new zipDrive(_patchzip, false) : NULL), autodelete_under(_autodelete_under)
	{
		DriveFileIterator(patchzip, LoadFiles, (Bitu)this);
	}

	~patchDriveImpl()
	{
		if (patchzip) delete patchzip;
		if (autodelete_under) delete &under;
	}

	static void LoadFiles(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
	{
		patchDriveImpl& self = *(patchDriveImpl*)data;
		
		const char* name;
		Patch_Directory* dir = self.GetParentDir(path, &name);
		if (!dir) return;
		const char* ext = strrchr(name, '.');

		FileStat_Block stat;
		bool on_under = self.under.FileStat(path, &stat);

		Patch_Entry* e;
		if (is_dir || (on_under && (stat.attr & DOS_ATTR_DIRECTORY)))
		{
			e = (on_under ? new Patch_Directory(stat.attr, name, stat.date, stat.time) : new Patch_Directory(attr, name, date, time));
			self.directories.Put(path, e->AsDirectory());
		}
		else if (ext && (!strcmp(ext + 1, "IPS") || !strcmp(ext + 1, "BPS") || !strcmp(ext + 1, "XDE") || !strcmp(ext + 1, "VCD")))
		{
			char fullname[256], underpath[DOS_PATHLENGTH+1];
			if (!self.patchzip->GetLongFileName(path, fullname)) strcpy(fullname, name);
			int undernamelen = (int)(strrchr(fullname, '.') - fullname), dirlen = (int)(name - path);
			if (undernamelen < 1 || undernamelen > DOS_NAMELENGTH) return; // to be patched file name length longer than 8.3?

			memcpy(underpath, path, dirlen);
			memcpy(underpath + dirlen, fullname, undernamelen);
			underpath[dirlen + undernamelen] = '\0';
			if (dir->entries.Get(underpath + dirlen)) return; // already exists (raw or patched)
			if (!self.under.FileStat(underpath, &stat)) { LOG_MSG("[DOSBOX] ERROR: Failed to open base file '%s' to patch with '%s'", underpath, fullname); return; }

			e = new Patch_File(Patch_File::TYPE_PATCH, stat.attr, underpath + dirlen, stat.date, stat.time);
			strcpy(e->AsFile()->patchpath, path);
		}
		else
		{
			if (dir->entries.Get(name)) return; // already exists (patched)
			e = (on_under ? new Patch_File(Patch_File::TYPE_RAW, stat.attr, name, stat.date, stat.time) : new Patch_File(Patch_File::TYPE_RAW, attr, name, date, time));
			strcpy(e->AsFile()->patchpath, path);
		}
		dir->entries.Put(e->name, e);
	}

	Patch_Directory* GetParentDir(const char* path, const char** out_name)
	{
		const char *lastslash = strrchr(path, '\\');
		if (!lastslash) { *out_name = path; return &root; }
		*out_name = lastslash + 1;
		return directories.Get(path, (Bit16u)(lastslash - path));
	}

	Patch_Entry* Get(const char* path, Patch_Directory** out_dir = NULL, const char** out_name = NULL)
	{
		if (!*path)
		{
			if (out_dir) *out_dir = NULL; // hopefully no one tries to access this
			if (out_name) *out_name = root.name;
			return &root;
		}
		const char* name;
		Patch_Directory* dir = GetParentDir(path, &name);
		if (out_dir) *out_dir = dir;
		if (out_name) *out_name = name;
		if (!dir) return NULL;
		return dir->entries.Get(name);
	}
};

patchDrive::patchDrive(DOS_Drive* under, bool autodelete_under, DOS_File* patchzip) : impl(new patchDriveImpl(*under, autodelete_under, patchzip)) { }

patchDrive::~patchDrive()
{
	ForceCloseAll();
	delete impl;
}

bool patchDrive::FileOpen(DOS_File * * file, char * name, Bit32u flags)
{
	if (!OPEN_CHECK_ACCESS_CODE(flags)) return FALSE_SET_DOSERR(ACCESS_CODE_INVALID);
	if (OPEN_IS_WRITING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
	DOSPATH_REMOVE_ENDINGDOTS_KEEP(name);
	Patch_Entry* e = impl->Get(name);
	if (!e || e->IsDirectory()) return impl->under.FileOpen(file, name, flags);
	if (e->AsFile()->type == Patch_File::TYPE_RAW)
		return impl->patchzip->FileOpen(file, name, flags);
	
	if (!e->AsFile()->patched) e->AsFile()->DoPatch(impl->under, *impl->patchzip);
	*file = new Patch_Handle(e->AsFile(), flags, name_org);
	return true;
}

bool patchDrive::FileCreate(DOS_File * * file, char * path, Bit16u attributes)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool patchDrive::Rename(char * oldpath, char * newpath)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool patchDrive::FileUnlink(char * path)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool patchDrive::FileExists(const char* name)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Patch_Entry* p = impl->Get(name);
	return (p ? p->IsFile() : impl->under.FileExists(name));
}

bool patchDrive::RemoveDir(char* dir_path)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool patchDrive::MakeDir(char* dir_path)
{
	return FALSE_SET_DOSERR(ACCESS_DENIED);
}

bool patchDrive::TestDir(char* dir_path)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	return (!dir_path[0] || impl->directories.Get(dir_path) || impl->under.TestDir(dir_path));
}

bool patchDrive::FindFirst(char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	const Bit16u save_errorcode = dos.errorcode;
	Patch_Directory* dir = (!dir_path[0] ? &impl->root : impl->directories.Get(dir_path));
	Bit16u over_id = (impl->under.FindFirst(dir_path, dta, fcb_findfirst) ? dta.GetDirID() : 0xFFFF);
	if (!dir && over_id == 0xFFFF) return FALSE_SET_DOSERR(PATH_NOT_FOUND);
	dos.errorcode = save_errorcode;

	Patch_Search s = { dir, (over_id == 0xFFFF ? (Bit32u)0 : (Bit32u)2), over_id };
	if (impl->free_search_ids.empty())
	{
		dta.SetDirID((Bit16u)impl->searches.size());
		impl->searches.push_back(s);
	}
	else
	{
		dta.SetDirID(impl->free_search_ids.back());
		impl->searches[impl->free_search_ids.back()] = s;
		impl->free_search_ids.pop_back();
	}

	if (s.index || DriveFindDriveVolume(this, dir_path, dta, fcb_findfirst)) return true;
	return FindNext(dta);
}

bool patchDrive::FindNext(DOS_DTA & dta)
{
	const Bit16u my_dir_id = dta.GetDirID();
	if (my_dir_id >= impl->searches.size()) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Patch_Search& s = impl->searches[my_dir_id];
	if (s.index == 0xFFFFFFFF) return FALSE_SET_DOSERR(NO_MORE_FILES);

	if (s.over_id != 0xFFFF) for (;;)
	{
		const Bit16u save_errorcode = dos.errorcode;
		dta.SetDirID(s.over_id);
		bool have_more = impl->under.FindNext(dta);
		s.over_id = dta.GetDirID();
		dta.SetDirID(my_dir_id);
		dos.errorcode = save_errorcode;
		if (!have_more) { s.over_id = 0xFFFF; break; }

		char dta_name[DOS_NAMELENGTH_ASCII];Bit32u dta_size;Bit16u dta_date;Bit16u dta_time;Bit8u dta_attr;
		dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
		if ((dta_attr & DOS_ATTR_VOLUME) || (dta_name[0] == '.' && dta_name[dta_name[1] == '.' ? 2 : 1] == '\0') || !s.dir || !s.dir->entries.Get(dta_name))
			return true;
	}

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
	if (s.dir) while (s.index++ - 2 < s.dir->entries.Capacity())
	{
		Patch_Entry* e = s.dir->entries.GetAtIndex(s.index - 3);
		if (!e || !WildFileCmp(e->name, pattern)) continue;
		if (~attr & (Bit8u)e->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
		dta.SetResult(e->name, (e->IsFile() ? e->AsFile()->Size(impl->under, impl->patchzip) : 0), e->date, e->time, (Bit8u)e->attr);
		return true;
	}

	s.index = 0xFFFFFFFF;
	impl->free_search_ids.push_back(my_dir_id);
	return FALSE_SET_DOSERR(NO_MORE_FILES);
}

bool patchDrive::FileStat(const char* name, FileStat_Block * const stat_block)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Patch_Entry* p = impl->Get(name);
	if (!p) return impl->under.FileStat(name, stat_block);
	stat_block->attr = p->attr;
	stat_block->size = (p->IsFile() ? p->AsFile()->Size(impl->under, impl->patchzip) : 0);
	stat_block->date = p->date;
	stat_block->time = p->time;
	return true;
}

bool patchDrive::GetFileAttr(char * name, Bit16u * attr)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	Patch_Entry* p = impl->Get(name);
	if (!p) return impl->under.GetFileAttr(name, attr);
	*attr = p->attr;
	return true;
}

bool patchDrive::AllocationInfo(Bit16u * _bytes_sector, Bit8u * _sectors_cluster, Bit16u * _total_clusters, Bit16u * _free_clusters)
{
	impl->under.AllocationInfo(_bytes_sector, _sectors_cluster, _total_clusters, _free_clusters);
	*_free_clusters = 0;
	return true;
}

bool patchDrive::GetShadows(DOS_Drive*& a, DOS_Drive*& b) { a = &impl->under; b = impl->patchzip; return true; }
Bit8u patchDrive::GetMediaByte(void) { return 0xF8;  } //Hard Disk
bool patchDrive::isRemote(void) { return false; }
bool patchDrive::isRemovable(void) { return false; }
Bits patchDrive::UnMount(void) { delete this; return 0;  }
