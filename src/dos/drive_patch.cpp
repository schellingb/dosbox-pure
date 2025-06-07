/*
 *  Copyright (C) 2024-2025 Bernhard Schelling
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

static Bit16s ActiveVariantIndex = -2;
std::string patchDrive::dos_yml;
StringToObjectHashMap<std::string> patchDrive::variants;

struct Patch_Entry
{
protected:
	Patch_Entry(Bit8u _layer, Bit16u _attr, const char* _name, Bit16u _date, Bit16u _time) : date(_date), time(_time), attr(_attr), layer(_layer)
	{
		size_t namesize = strlen(_name) + 1;
		if (namesize > sizeof(name)) { DBP_ASSERT(false); namesize = sizeof(name); }
		memcpy(name, _name, namesize);
	}
	~Patch_Entry() {}

public:
	INLINE bool IsFile()      { return ((attr & DOS_ATTR_DIRECTORY) == 0); }
	INLINE bool IsDirectory() { return ((attr & DOS_ATTR_DIRECTORY) != 0); }
	INLINE struct Patch_File*      AsFile()      { return (Patch_File*)this;      }
	INLINE struct Patch_Directory* AsDirectory() { return (Patch_Directory*)this; }

	Bit16u date, time, attr;
	Bit8u variantlen, layer;
	char name[DOS_NAMELENGTH_ASCII], zippath[DOS_PATHLENGTH+1];
};

struct Patch_File : Patch_Entry
{
	std::vector<Bit8u> mem_data;
	Bit32u refs;
	enum EType { TYPE_RAW, TYPE_PATCH } type;
	bool patched;

	Patch_File(Bit8u _layer, EType _type, Bit16u _attr, const char* filename, Bit16u _date, Bit16u _time) : Patch_Entry(_layer, _attr, filename, _date, _time), refs(0), type(_type), patched(false) { DBP_ASSERT(IsFile()); }

	inline Bit32u Size(DOS_Drive& under, zipDrive* patchzip)
	{
		if (type == TYPE_RAW)
		{
			FileStat_Block stat;
			if (!patchzip->FileStat(zippath, &stat)) { DBP_ASSERT(false); return 0; }
			return stat.size;
		}

		if (!patched) DoPatch(under, *patchzip);

		return (Bit32u)mem_data.size();
	}

	void DoPatch(DOS_Drive& under, zipDrive& patchzip)
	{
		patched = true;
		char underpath[DOS_PATHLENGTH+1];
		strcpy(underpath, zippath + variantlen);
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
				for (Bit16u read; sz; sz -= read, p += read) { read = (Bit16u)(sz > 0xFFFF ? 0xFFFF : sz); if (!df->Read(p, &read) || !read) return false; }
				return true;
			}
			static bool LoadFile(DOS_Drive& drv, char* path, std::vector<Bit8u>& res)
			{
				FileStat_Block stat;
				DOS_File* df;
				if (!drv.FileStat(path, &stat) || !drv.FileOpen(&df, path, 0)) return false;
				res.resize(stat.size);
				df->AddRef();
				if (!Read(df, (stat.size ? &res[0] : NULL), stat.size)) return false;
				df->Close();
				delete df;
				return true;
			}
		};

		struct XORPatch
		{
			static bool Process(DOS_Drive& underdrv, char* trgpath, std::vector<Bit8u>& res, DOS_File* xorf, Patch_File* self)
			{
				char basepath[DOS_PATHLENGTH]; Bit32u filesize, ofs; Bit8u b; Bit64u baseofs = 0, targsz = 0;
				xorf->Seek(&(filesize = 0), DOS_SEEK_END); xorf->Seek(&(ofs = 4), DOS_SEEK_SET); // skip over header
				for (; Local::GetU8(xorf, b);) { ++ofs; if ((basepath[ofs-5] = (char)(ofs == sizeof(basepath)+4 ? '\0' : b == '/' ? '\\' : b)) == '\0') break; } // read source path
				for (Bit8u num = 0; Local::GetU8(xorf, b);) { baseofs |= (Bit64u)(b & 0x7F) << (7 * (num++)); ++ofs; if (b < 0x80) break; }
				for (Bit8u num = 0; Local::GetU8(xorf, b);) { targsz  |= (Bit64u)(b & 0x7F) << (7 * (num++)); ++ofs; if (b < 0x80) break; }
				if (baseofs > 0xFFFFFFFF || targsz > 0xFFFFFFFF) return false;

				Bit8u defdrv = DOS_GetDefaultDrive(), oldcurdir = *Drives[defdrv]->curdir;
				DOS_Drive* olddrv = Drives[defdrv];
				Drives[defdrv] = &underdrv; *olddrv->curdir = '\0';
				DOS_File* basef = FindAndOpenDosFile(basepath, NULL, NULL, trgpath);
				Drives[defdrv] = olddrv; *olddrv->curdir = oldcurdir;
				if (!basef) return false;
				basef->Seek64(&baseofs, DOS_SEEK_SET);

				res.resize((size_t)targsz);
				Local::Read(xorf, &res[0], (filesize - ofs));
				Bit8u buf[1024]; Bit16u bufp = 0, bufn = 0;
				for (Bit8u& r : res)
				{
					if (bufp == bufn) // read more from base
						{ bufp = 0; if (!basef->Read(buf, &(bufn = 1024)) || !bufn) { basef->Seek(&(ofs = 0), DOS_SEEK_SET); if (!basef->Read(buf, &(bufn = 1024)) || !bufn) break; } }
					r ^= buf[bufp++];
				}
				self->time = basef->time;
				self->date = basef->date;
				basef->Close();
				delete basef;
				return (bufp != 0);
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
				in_data.swap(out_data);
				return true;
			}
		};

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
				in_data.swap(out_data);
				return true;
			}
		};

		DOS_File* df;
		if (!patchzip.FileOpen(&df, zippath, 0)) { DBP_ASSERT(false); return; }
		df->AddRef();
		Bit32u hdr = 0;
		Local::GetU24(df, hdr);
		bool success = false;
		if (hdr == 0x584f52) // XOR patch
			success = XORPatch::Process(under, underpath, mem_data, df, this);
		else if (!Local::LoadFile(under, underpath, mem_data))
			success = false;
		else if (hdr == 0x504154) // IPS file
			success = IPSPatch::Process(mem_data, df);
		else if (hdr == 0x425053) // BPS file
			success = BPSPatch::Process(mem_data, df);
		else if (hdr == 0xd6c3c4) // VCDIFF file
		{
			VCDiff* vcd = new VCDiff; // too large for stack
			success = vcd->Process(mem_data, df);
			delete vcd;
		}
		df->Close();
		delete df;
		if (!success) { DBP_ASSERT(false); LOG_MSG("[DOSBOX] ERROR: Failed to patch '%s' with invalid patch file '%s'", underpath, zippath); }
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

	Patch_Directory(Bit8u _layer, Bit16u _attr, const char* dirname, Bit16u _date, Bit16u _time) : Patch_Entry(_layer, _attr, dirname, _date, _time) { DBP_ASSERT(IsDirectory()); }

	static void DeleteEntry(Patch_Entry* e) { if (e->IsDirectory()) delete e->AsDirectory(); else delete e->AsFile(); }

	~Patch_Directory()
	{
		for (Patch_Entry* e : entries) DeleteEntry(e);
	}
};

struct Patch_Layer
{
	DOS_Drive& under;
	zipDrive* patchzip;
	bool autodelete_under;
	Patch_Layer(DOS_Drive& _under, bool _autodelete_under, zipDrive* _patchzip) : under(_under), patchzip(_patchzip), autodelete_under(_autodelete_under) {}
};

struct Patch_Search
{
	StringToPointerHashMap<void> dones;
	Patch_Directory* dir;
	Patch_Layer* layer;
	Bit32u dir_index;
	Bit16u layer_id;
	bool fcb_findfirst;
	char dir_path[DOS_PATHLENGTH + 1];
};

struct patchDriveImpl
{
	Patch_Directory root;
	StringToPointerHashMap<Patch_Directory> directories;
	std::vector<Patch_Search*> searches;
	std::vector<Patch_Layer> layers;
	Patch_Layer *layer_top, *layer_bottom;
	Bit8u IterateLayer;
	bool IterateGetVariant, IterateHadVariant, IterateYMLOnly;

	patchDriveImpl() : root(255, DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY, "", 0, 0), layer_top(NULL), layer_bottom(NULL) { }

	~patchDriveImpl()
	{
		for (Patch_Search* s : searches) delete s;
		for (Patch_Layer& l : layers) 
		{
			if (l.patchzip) delete l.patchzip;
			if (l.autodelete_under) delete &l.under;
		}
	}

	static bool AppendYML(DOS_Drive* drv, const char* path = "DOS.YML")
	{
		DOS_File *df;
		if (!drv->FileOpen(&df, (char*)path, OPEN_READ)) return false;
		if (patchDrive::dos_yml.length()) patchDrive::dos_yml += '\n';
		return ReadAndClose(df, patchDrive::dos_yml);
	}

	void Reload(bool ymlOnly = false)
	{
		if (layers.empty()) { DBP_ASSERT(false); return; }
		if (!ymlOnly)
		{
			for (Patch_Entry* e : root.entries) Patch_Directory::DeleteEntry(e);
			root.entries.Clear();
			directories.Clear();
		}

		DBP_ASSERT(!layer_top || layer_top == &layers.back()); // fixed once first used
		layer_top = &layers.back();
		layer_bottom = &layers.front();
		if (ActiveVariantIndex == -2) ActiveVariantIndex = -1; // because we fill out dos_yml for the default config now

		Bit32u ignoreLayers = 0, layerLast = (Bit32u)(layer_top - layer_bottom);
		for (IterateLayer = 0, IterateGetVariant = false, IterateYMLOnly = ymlOnly;;)
		{
			if (!(ignoreLayers & (1 << IterateLayer)))
			{
				Patch_Layer& layer = layer_bottom[IterateLayer];
				if (!IterateGetVariant) AppendYML(&layer.under);
				IterateHadVariant = false;
				DriveFileIterator(layer.patchzip, LoadFiles, (Bitu)this);
				if (!IterateHadVariant) ignoreLayers |= (1 << IterateLayer);
			}
			if (IterateLayer++ == layerLast)
			{
				if (IterateGetVariant) break;
				IterateLayer = 0;
				IterateGetVariant = true;
			}
		}
	}

	static bool RootOrVariant(zipDrive& patchzip, const char*& in_out_path)
	{
		const char* path = in_out_path, *slash, *dirEnd;
		if (path[0] != DBP_8DOT3_INVALID_CHAR || (dirEnd = ((slash = strchr(path, '\\')) == NULL ? path + strlen(path) : slash))[-1] != DBP_8DOT3_INVALID_CHAR) // check for [VARIANT]
			return true; // not a [variant]

		char base[DOS_NAMELENGTH_ASCII], full[256], *fullEnd;
		if (slash) sprintf(base, "%.*s", (int)(slash - path), path); // zero terminate the directory name
		if (!patchzip.GetLongFileName((slash ? base : path), full) || full[0] != '[' || (fullEnd = (full+strlen(full)))[-1] != ']')
			return true; // not a [variant] after all

		if (!slash) return false; // ignore, directory of variant, only care for files in it

		if (const std::string* v = patchDrive::variants.Get(full + 1, (Bit32u)(fullEnd - 2 - full)))
		{
			if (patchDrive::variants.GetStorageIndex(v) != ActiveVariantIndex) return false; // inactive
			in_out_path = slash + 1; // cut off variant
			return true;
		}
		patchDrive::variants.Add(full + 1, (Bit32u)(fullEnd - 2 - full)).assign(full + 1, (size_t)(fullEnd - 2 - full));
		return false; // ignore, variant not yet active
	}

	static void LoadFiles(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
	{
		patchDriveImpl& self = *(patchDriveImpl*)data;
		Patch_Layer& layer = self.layer_bottom[self.IterateLayer];

		const char* orgpath = path, *ext = NULL;
		if (!RootOrVariant(*layer.patchzip, path)) return; // ignore other variants

		const bool isVariant = (path != orgpath);
		if (self.IterateGetVariant != isVariant)
		{
			self.IterateHadVariant = isVariant;
			return; // not looking for this in this iteration
		}

		if (!is_dir && (ext = strrchr(path, '.')) != NULL && *++ext == 'Y' && !strcmp(path, "DOS.YML"))
		{
			AppendYML(layer.patchzip, orgpath);
			return; // DOS.YML of patch drives is not exposed to the DOS file system
		}
		else if (self.IterateYMLOnly) return;

		const char* name;
		Patch_Directory* dir = self.GetParentDir(path, &name);
		if (!dir) return;

		FileStat_Block stat = { size, time, date, attr }, dummystat;
		char fullname[256], patchsrc[DOS_PATHLENGTH+1];
		const char* underpath = path;

		if (ext && (!strcmp(ext, "IPS") || !strcmp(ext, "BPS") || !strcmp(ext, "XDE") || !strcmp(ext, "VCD") || !strcmp(ext, "XOR")))
		{
			if (!layer.patchzip->GetLongFileName(orgpath, fullname)) strcpy(fullname, name);
			int undernamelen = (int)(strrchr(fullname, '.') - fullname), dirlen = (int)(name - path);
			if (undernamelen < 1 || undernamelen > DOS_NAMELENGTH) { LOG_MSG("[DOSBOX] ERROR: Patch file name '%s' too long", fullname); return; };

			memcpy(patchsrc, path, dirlen);
			memcpy(patchsrc + dirlen, fullname, undernamelen);
			patchsrc[dirlen + undernamelen] = '\0';
			underpath = patchsrc;
		}
		else if (!is_dir)
		{
			// Ignore any overlay files existing in the layers above (but allow binary patches)
			for (Patch_Layer* l = &layer + 1; l <= self.layer_top; l++)
				if (l->under.FileStat(underpath, &dummystat))
					return;
		}

		// Don't query under file stats for .XOR patches
		if (!ext || ext[1] != 'O' /* from .XOR */) layer.under.FileStat(underpath, &stat);

		Patch_Entry* e;
		if (is_dir || (stat.attr & DOS_ATTR_DIRECTORY))
		{
			if (dir->entries.Get(name)) return; // don't have a variant overwrite entire directories
			e = new Patch_Directory(self.IterateLayer, stat.attr, name, stat.date, stat.time);
			self.directories.Put(path, e->AsDirectory());
		}
		else
		{
			Patch_File::EType typ = (underpath == path ? Patch_File::TYPE_RAW : Patch_File::TYPE_PATCH);
			const char*       nam = (underpath == path ? name : underpath + (name - path));
			e = new Patch_File(self.IterateLayer, typ, stat.attr, nam, stat.date, stat.time);

			if (Patch_Entry* existing = dir->entries.Get(e->name))
				Patch_Directory::DeleteEntry(existing);
		}

		strcpy(e->zippath, orgpath);
		e->variantlen = (Bit8u)(path - orgpath);
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

patchDrive::patchDrive() : impl(new patchDriveImpl()) { }

void patchDrive::AddLayer(DOS_Drive& under, bool autodelete_under, DOS_File* patchzip, bool enable_crc_check, bool is_final)
{
	impl->layers.emplace_back(under, autodelete_under, (patchzip ? new zipDrive(patchzip, enable_crc_check) : NULL));
	if (impl->layers.size() == 1) label.SetLabel(under.GetLabel(), false, true);
	if (is_final) { patchDrive::dos_yml.clear(); impl->Reload(); }
}

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
	if (Patch_Entry* e = impl->Get(name))
	{
		if (e->IsDirectory()) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		Patch_Layer& layer = impl->layer_bottom[e->layer];
		if (e->AsFile()->type == Patch_File::TYPE_RAW)
			return layer.patchzip->FileOpen(file, e->zippath, flags);
		if (!e->AsFile()->patched) e->AsFile()->DoPatch(layer.under, *layer.patchzip);
		*file = new Patch_Handle(e->AsFile(), flags, name_org);
		return true;
	}
	const Bit16u save_errorcode = dos.errorcode;
	for (Patch_Layer* l = impl->layer_top; l >= impl->layer_bottom; l--)
		if (l->under.FileOpen(file, name, flags))
			return (dos.errorcode = save_errorcode, true);
	return false;
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
	if (Patch_Entry* p = impl->Get(name)) return p->IsFile();
	for (Patch_Layer* l = impl->layer_bottom; l <= impl->layer_top; l++) if (l->under.FileExists(name)) return true;
	return false;
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
	if (!dir_path[0]) return true;
	if (Patch_Entry* p = impl->Get(dir_path)) return p->IsDirectory();
	for (Patch_Layer* l = impl->layer_bottom; l <= impl->layer_top; l++) if (l->under.TestDir(dir_path)) return true;
	return false;
}

bool patchDrive::FindFirst(char* dir_path, DOS_DTA & dta, bool fcb_findfirst)
{
	DOSPATH_REMOVE_ENDINGDOTS(dir_path);
	if (!TestDir(dir_path)) return FALSE_SET_DOSERR(PATH_NOT_FOUND);

	size_t dir_len = strlen(dir_path), snum;
	if (dir_len >= sizeof(((Patch_Search*)0)->dir_path)) { DBP_ASSERT(false); return false; }
	for (snum = 0; snum != impl->searches.size(); snum++) if (!impl->searches[snum]->dir && impl->searches[snum]->layer > impl->layer_top) break;
	if (snum == impl->searches.size()) impl->searches.push_back(new Patch_Search());

	dta.SetDirID((Bit16u)snum);
	Patch_Search& s = *impl->searches[snum];
	Patch_Directory* dir = (!dir_path[0] ? &impl->root : impl->directories.Get(dir_path));
	s.dones.Clear();
	s.dir = dir;
	s.layer = impl->layer_bottom;
	s.dir_index = (dir == &impl->root ? (Bit32u)2 : (Bit32u)0);
	s.layer_id = 0xFFFF;
	s.fcb_findfirst = fcb_findfirst;
	memcpy(s.dir_path, dir_path, dir_len + 1); // with '\0' terminator

	if (DriveFindDriveVolume(this, dir_path, dta, fcb_findfirst)) return true;
	return FindNext(dta);
}

bool patchDrive::FindNext(DOS_DTA & dta)
{
	const Bit16u my_dir_id = dta.GetDirID();
	if (my_dir_id >= impl->searches.size()) return FALSE_SET_DOSERR(ACCESS_DENIED);
	Patch_Search& s = *impl->searches[my_dir_id];
	if (s.dir)
	{
		Bit8u attr;char pattern[DOS_NAMELENGTH_ASCII];
		dta.GetSearchParams(attr,pattern);
		while (s.dir_index < 2)
		{
			const char* dotted = (s.dir_index++ ? ".." : ".");
			if (!DTA_PATTERN_MATCH(dotted, pattern) || (s.dir->attr & DOS_ATTR_VOLUME)) continue;
			if (~attr & (Bit8u)s.dir->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
			dta.SetResult(dotted, 0, s.dir->date, s.dir->time, (Bit8u)s.dir->attr);
			s.dones.Put(dotted, (void*)(size_t)-1);
			return true;
		}
		while (s.dir_index++ - 2 < s.dir->entries.Capacity())
		{
			Patch_Entry* e = s.dir->entries.GetAtIndex(s.dir_index - 3);
			if (!e || !DTA_PATTERN_MATCH(e->name, pattern)) continue;
			if (~attr & (Bit8u)e->attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
			Patch_Layer* file_layer = (e->IsFile() ? (impl->layer_bottom + e->layer) : NULL);
			dta.SetResult(e->name, (file_layer ? e->AsFile()->Size(file_layer->under, file_layer->patchzip) : 0), e->date, e->time, (Bit8u)e->attr);
			s.dones.Put(e->name, (void*)(size_t)-1);
			return true;
		}
		s.dir = NULL;
	}
	const Bit16u save_errorcode = dos.errorcode;
	while (s.layer <= impl->layer_top)
	{
		bool have_more;
		if (s.layer_id == 0xFFFF)
		{
			have_more = s.layer->under.FindFirst(s.dir_path, dta, s.fcb_findfirst);
		}
		else
		{
			dta.SetDirID(s.layer_id);
			have_more = s.layer->under.FindNext(dta);
		}
		s.layer_id = dta.GetDirID();
		dta.SetDirID(my_dir_id);
		dos.errorcode = save_errorcode;
		if (!have_more) { s.layer_id = 0xFFFF; s.layer++; continue; }
		char dta_name[DOS_NAMELENGTH_ASCII];Bit32u dta_size;Bit16u dta_date;Bit16u dta_time;Bit8u dta_attr;
		dta.GetResult(dta_name, dta_size, dta_date, dta_time, dta_attr);
		if (!(dta_attr & DOS_ATTR_VOLUME) && s.dones.Put(dta_name, (void*)(size_t)-1))
			return true;
	}
	return FALSE_SET_DOSERR(NO_MORE_FILES);
}

bool patchDrive::FileStat(const char* name, FileStat_Block * const stat_block)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	if (Patch_Entry* p = impl->Get(name))
	{
		Patch_Layer* file_layer = (p->IsFile() ? (impl->layer_bottom + p->layer) : NULL);
		stat_block->attr = p->attr;
		stat_block->size = (file_layer ? p->AsFile()->Size(file_layer->under, file_layer->patchzip) : 0);
		stat_block->date = p->date;
		stat_block->time = p->time;
		return true;
	}
	for (Patch_Layer* l = impl->layer_top; l >= impl->layer_bottom; l--)
		if (l->under.FileStat(name, stat_block))
			return true;
	return false;
}

bool patchDrive::GetFileAttr(char * name, Bit16u * attr)
{
	DOSPATH_REMOVE_ENDINGDOTS(name);
	if (Patch_Entry* p = impl->Get(name)) { *attr = p->attr; return true; }
	for (Patch_Layer* l = impl->layer_top; l >= impl->layer_bottom; l--)
		if (l->under.GetFileAttr(name, attr))
			return true;
	return false;
}

bool patchDrive::GetLongFileName(const char* path, char longname[256])
{
	DOSPATH_REMOVE_ENDINGDOTS(path);
	if (!*path) return false;
	if (Patch_Entry* p = impl->Get(path))
		if (p->IsDirectory() || p->AsFile()->type == Patch_File::TYPE_RAW)
			return impl->layer_bottom[p->layer].patchzip->GetLongFileName(p->zippath, longname);
	for (Patch_Layer* l = impl->layer_top; l >= impl->layer_bottom; l--)
		if (l->under.GetLongFileName(path, longname))
			return true;
	return false;
}

bool patchDrive::AllocationInfo(Bit16u * _bytes_sector, Bit8u * _sectors_cluster, Bit16u * _total_clusters, Bit16u * _free_clusters)
{
	impl->layer_bottom->under.AllocationInfo(_bytes_sector, _sectors_cluster, _total_clusters, _free_clusters);
	*_free_clusters = 0;
	return true;
}

DOS_Drive* patchDrive::GetShadow(int n, bool only_owned)
{
	for (Patch_Layer* l = impl->layer_top; l >= impl->layer_bottom; l--)
		if ((!only_owned || l->autodelete_under) && !(n--))
			return &l->under;
	return NULL;
}

Bit8u patchDrive::GetMediaByte(void) { return 0xF8; } //Hard Disk
bool patchDrive::isRemote(void) { return false; }
bool patchDrive::isRemovable(void) { return false; }
Bits patchDrive::UnMount(void) { delete this; return 0;  }

bool patchDrive::ActivateVariant(int variant_number, bool ymlonly)
{
	int newVariantIndex = variant_number - 1;
	if (ActiveVariantIndex == newVariantIndex) return false;
	ActiveVariantIndex = newVariantIndex;

	struct Local
	{
		static void ReloadAll(DOS_Drive *drv, bool ymlonl)
		{
			if (patchDrive* pd = dynamic_cast<patchDrive*>(drv))
			{
				DBP_ASSERT(dos_yml.empty()); // it is assumed that there is only ever one drive with a YML
				pd->impl->Reload(ymlonl);
				return;
			}
			if (dynamic_cast<memoryDrive*>(drv)) return; // DOS.YML is not allowed to be put into save file
			for (int n = 0;; n++)
			{
				if (DOS_Drive* shadow = drv->GetShadow(n, true)) ReloadAll(shadow, ymlonl);
				else { if (n) return; break; }
			}
			DBP_ASSERT(dos_yml.empty()); // it is assumed that there is only ever one drive with a YML
			patchDriveImpl::AppendYML(drv);
		}
	};
	dos_yml.clear();
	if (Drives['C'-'A']) Local::ReloadAll(Drives['C'-'A'], ymlonly);
	return true;
}

void patchDrive::ResetVariants()
{
	ActiveVariantIndex = -2;
	patchDrive::dos_yml.clear();
	patchDrive::variants.Clear();
}

std::vector<std::string> patchDrive::VariantConflictFiles(int variant_number, bool reset_conflicts)
{
	struct Local
	{
		static void GetDrives(DOS_Drive *drv, Local& l)
		{
			if (patchDrive* ppd = dynamic_cast<patchDrive*>(drv)) { l.PatchDrive = ppd; return; }
			if (memoryDrive* pmemd = dynamic_cast<memoryDrive*>(drv)) { l.MemoryDrive = pmemd; return; }
			for (int n = 0;; n++) { if (DOS_Drive* shadow = drv->GetShadow(n, true)) { GetDrives(shadow, l); } else break; }
		}
		static void CheckFiles(const char* path, bool is_dir, Bit32u size, Bit16u date, Bit16u time, Bit8u attr, Bitu data)
		{
			if (is_dir) return;
			Local& l = *(Local*)data;

			FileStat_Block stat;
			if (!l.MemoryDrive->FileStat(path, &stat)) return;
			if (stat.size == size)
			{
				if (!size) return;
				l.Buf.clear();
				DriveGetFileContent(l.MemoryDrive, path, l.Buf);
				DriveGetFileContent(l.PatchDrive, path, l.Buf);
				if (!memcmp(&l.Buf[0], &l.Buf[size], size)) return;
			}
			l.Result.push_back(path);
		}
		patchDrive* PatchDrive = NULL;
		memoryDrive* MemoryDrive = NULL;
		std::vector<std::string> Result;
		std::vector<Bit8u> Buf;
	} l;

	if (Drives['C'-'A']) Local::GetDrives(Drives['C'-'A'], l);
	if (l.PatchDrive && l.MemoryDrive)
	{
		int oldVariantNumber = ActiveVariantIndex + 1;
		ActivateVariant(variant_number);
		DriveFileIterator(l.PatchDrive, Local::CheckFiles, (Bitu)&l);
		ActivateVariant(oldVariantNumber);
		if (reset_conflicts)
		{
			for (const std::string& path : l.Result) l.MemoryDrive->FileUnlink((char*)path.c_str());
			l.Result.clear();
		}
	}
	return l.Result;
}

/*
void patchDrive::Patch()
{
	const char* underpath = "VIKINGS.EXE";

	FileStat_Block stat;
	DOS_File* df;
	if (!impl->under.FileStat(underpath, &stat) || !impl->under.FileOpen(&df, (char*)underpath, 0)) { DBP_ASSERT(false); return; }

	Patch_File* e = new Patch_File(Patch_File::TYPE_PATCH, stat.attr, underpath, stat.date, stat.time);
	e->patched = true;
	e->mem_data.resize(stat.size);
	df->AddRef();
	struct Local
	{
		static bool Read(DOS_File* df, Bit8u* p, Bit32u sz)
		{
			for (Bit16u read; sz; sz -= read, p += read) { read = (Bit16u)(sz > 0xFFFF ? 0xFFFF : sz); if (!df->Read(p, &read)) return false; }
			return true;
		}
	};
	if (!Local::Read(df, (stat.size ? &e->mem_data[0] : NULL), stat.size)) { DBP_ASSERT(0); }
	df->Close();
	delete df;
	impl->root.entries.Put(e->name, e);

	//CHANGE EXE @ 0x3C3B FROM 0x75 0x7B to 0x90 0x90
	DBP_ASSERT(e->mem_data[0x3C3B + 0] == 0x75);
	DBP_ASSERT(e->mem_data[0x3C3B + 1] == 0x7B);
	e->mem_data[0x3C3B + 0] = 0x90;
	e->mem_data[0x3C3B + 1] = 0x90;
}
*/
