/*
 *  Copyright (C) 2020-2025 Bernhard Schelling
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

static const char* DBP_MachineNames[] = { "SVGA (Super Video Graphics Array)", "VGA (Video Graphics Array)", "EGA (Enhanced Graphics Adapter", "CGA (Color Graphics Adapter)", "Tandy (Tandy Graphics Adapter", "Hercules (Hercules Graphics Card)", "PCjr" };

struct DBP_Run
{
	static void RunBatchFile(BatchFile* bf)
	{
		DBP_ASSERT(!dbp_game_running);
		const bool inAutoexec = (first_shell->bf && first_shell->bf->IsAutoexec());
		while (first_shell->bf) delete first_shell->bf;
		bf->prev = NULL; // was just deleted
		bf->echo = true; // always want this back after returning
		first_shell->bf = bf;
		first_shell->echo = false;
		if (!inAutoexec)
		{
			// Sending this key sequence makes sure DOS_Shell::Run will run our batch file immediately
			// It also clears anything typed already on the command line or finishes DOS_Shell::CMD_PAUSE or DOS_Shell::CMD_CHOICE
			KEYBOARD_AddKey(KBD_esc, true);
			KEYBOARD_AddKey(KBD_esc, false);
			KEYBOARD_AddKey(KBD_enter, true);
			KEYBOARD_AddKey(KBD_enter, false);
		}
		dbp_lastmenuticks = DBP_GetTicks();
	}

	static void ConsoleClearScreen()
	{
		DBP_ASSERT(!dbp_game_running);
		reg_ax = 0x0003;
		CALLBACK_RunRealInt(0x10);
	}

	struct BatchFileExec : BatchFile
	{
		BatchFileExec(const std::string& _exe) : BatchFile(first_shell,"Z:\\AUTOEXEC.BAT","","") { filename = _exe; if (!_exe.length()) location = 1; }
		virtual bool ReadLine(char * line)
		{
			*(line++) = '@';
			switch (location++)
			{
				case 0:
				{
					ConsoleClearScreen();
					char *fn = (char*)filename.c_str(), *r = fn + ((fn[0] && fn[1] == ':') ? 2 : 0), *p = r + (*r == '\\' ? 1 : 0), *param = strchr(p, ' '), *sl;
					if (param) { *param = '\0'; sl = strrchr(p, '\\'); *param = ' '; } else { sl = strrchr(p, '\\'); };
					const Bit8u drive = ((((fn[0] >= 'A' && fn[0] <= 'Z') || (fn[0] >= 'a' && fn[0] <= 'z')) && fn[1] == ':') ? (fn[0] & 0x5F) : 'C') - 'A';
					if (Drives[drive])
					{
						DOS_SetDefaultDrive(drive);
						if (sl)
						{
							memcpy(Drives[drive]->curdir,p, sl - p);
							Drives[drive]->curdir[sl - p] = '\0';
						}
						else Drives[drive]->curdir[0] = '\0';
					}
					else { sl = NULL; p = fn; } // try call full string which will likely show an error to tell the user auto start is wrong

					const char* f = (sl  ? sl + 1 : p), *fext = strchr(f, '.');
					bool isbat = (fext && (fext[1]|0x20) == 'b');
					int call_cmd_len = (isbat ? 5 : 0), flen = (int)strlen(f);
					memcpy(line, "call ", call_cmd_len);
					memcpy(line+call_cmd_len, f, flen);
					memcpy(line+call_cmd_len+flen, "\n", 2);
					break;
				}
				case 1:
					memcpy(line, "Z:PUREMENU", 10);
					memcpy(line+10, " -FINISH\n", 10);
					delete this;
					break;
			}
			return true;
		}
	};

	struct BatchFileBoot : BatchFile
	{
		BatchFileBoot(char drive) : BatchFile(first_shell,"Z:\\AUTOEXEC.BAT","","") { file_handle = drive; }

		virtual bool ReadLine(char * line)
		{
			if (location++)
			{
				// This function does not do `delete this;` instead it calls DBP_OnBIOSReboot to eventually do that
				memcpy(line, "@PAUSE\n", 8);
				if (location > 2) { startup.mode = RUN_NONE; DBP_OnBIOSReboot(); }
				return true;
			}
			updateDPT(); // reflect imageDisk geometry in BIOS memory
			ConsoleClearScreen();
			memcpy(line, "@Z:BOOT -l  \n", 14);
			line[11] = (char)file_handle; // drive letter
			if (machine == MCH_PCJR && file_handle == 'A' && !dbp_images.empty())
			{
				// The path to the image needs to be passed to boot for pcjr carts
				const std::string& imgpath = dbp_images[dbp_image_index].path;
				line[12] = ' ';
				memcpy(line+13, imgpath.c_str(), imgpath.size());
				memcpy(line+13+imgpath.size(), "\n", 2);
			}
			return true;
		}

		static bool MountOSIMG(char drive, const char* path, const char* type, bool needwritable, bool complainnotfound)
		{
			FILE* raw_file_h = NULL;
			if (needwritable && (raw_file_h = fopen_wrap(path, "rb+")) != NULL) goto openok;
			if ((raw_file_h = fopen_wrap(path, "rb")) == NULL)
			{
				if (complainnotfound)
					emuthread_notify(0, LOG_ERROR, "Unable to open %s file: %s%s", type, path, "");
				return false;
			}
			if (needwritable)
			{
				emuthread_notify(0, LOG_ERROR, "Unable to open %s file: %s%s", type, path, " (file is read-only!)");
				fclose(raw_file_h);
				return false;
			}
			openok:
			DOS_File* df = new rawFile(raw_file_h, needwritable);
			df->AddRef();
			imageDiskList[drive-'A'] = new imageDisk(df, "", 0, true);
			imageDiskList[drive-'A']->Set_GeometryForHardDisk();
			return true;
		}
	};

	static void Exec(const std::string& _exe)
	{
		RunBatchFile(new BatchFileExec(_exe));
	}

	static void BootImage()
	{
		DBP_ASSERT(!dbp_images.empty()); // IT_BOOTIMG should only be available if this is true
		if (!dbp_images.empty())
		{
			DBP_Mount(0, false); // make sure something is mounted

			// If hard disk image was mounted to D:, swap it to be the bootable C: drive
			std::swap(imageDiskList['D'-'A'], imageDiskList['C'-'A']);

			// If there is no mounted hard disk image but a D: drive, setup the CDROM IDE controller
			if (!imageDiskList['C'-'A'] && Drives['D'-'A'])
				IDE_SetupControllers(true);

			// Install the NE2000 network card
			NET_SetupEthernet();
		}

		RunBatchFile(new BatchFileBoot(imageDiskList['A'-'A'] ? 'A' : 'C'));
	}

	static void BootOS(bool is_install, int osidx_or_size)
	{
		// Make sure we have at least 32 MB of RAM, if not set it to 64
		if ((MEM_TotalPages() / 256) < 32 && !control->GetProp("dosbox", "memsize")->IsFixed())
		{
			dbp_reboot_set64mem = true;
			DBP_OnBIOSReboot();
			return;
		}

		std::string path;
		if (!is_install)
		{
			path = DBP_GetSaveFile(SFT_SYSTEMDIR).append(dbp_osimages[osidx_or_size]);
		}
		else if (osidx_or_size)
		{
			const char* filename;
			path = DBP_GetSaveFile(SFT_NEWOSIMAGE, &filename);

			// Create a new empty hard disk image of the requested size
			memoryDrive* memDrv = new memoryDrive();
			DBP_SetDriveLabelFromContentPath(memDrv, path.c_str(), 'C', filename, path.c_str() + path.size() - 3);
			imageDisk* memDsk = new imageDisk(memDrv, (Bit32u)(osidx_or_size*8));
			Bit32u heads, cyl, sect, sectSize;
			memDsk->Get_Geometry(&heads, &cyl, &sect, &sectSize);
			FILE* f = fopen_wrap(path.c_str(), "wb");
			if (!f) { emuthread_notify(0, LOG_ERROR, "Unable to open %s file: %s%s", "OS image", path.c_str(), " (create file failed)"); return; }
			for (Bit32u i = 0, total = heads * cyl * sect; i != total; i++) { Bit8u data[512]; memDsk->Read_AbsoluteSector(i, data); fwrite(data, 512, 1, f); }
			fclose(f);
			delete memDsk;
			delete memDrv;

			// If using system directory index cache, append the new OS image to that now
			if (dbp_system_cached)
				if (FILE* fc = fopen_wrap(DBP_GetSaveFile(SFT_SYSTEMDIR).append("DOSBoxPureMidiCache.txt").c_str(), "a"))
					{ fprintf(fc, "%s\n", filename); fclose(fc); }

			// Set last_info to this new image to support BIOS rebooting with it
			startup.mode = RUN_BOOTOS;
			startup.info = (int)dbp_osimages.size();
			dbp_osimages.emplace_back(filename);
		}

		if (!path.empty())
		{
			// When booting an external disk image as C:, use whatever is C: or D: in DOSBox DOS as the third IDE drive in the booted OS
			const char newC = 'E'; // Third IDE drive (if it were D: the IDE CD-ROM drive wouldn't show up in Windows 9x)
			if      (imageDiskList['C'-'A']) std::swap(imageDiskList['C'-'A'], imageDiskList[newC-'A']); // Loaded content is FAT12/FAT16 disk image
			else if (imageDiskList['E'-'A'] && dbp_content_path == imageDiskList['E'-'A']->diskname) {}  // Loaded content is FAT32/other disk image
			else if (!BatchFileBoot::MountOSIMG(newC, (dbp_content_path + ".img").c_str(), "D: drive image", true, false) && Drives['C'-'A'])
			{
				Bit32u save_hash = 0;
				DBP_SetDriveLabelFromContentPath(Drives['C'-'A'], dbp_content_path.c_str(), 'C', NULL, NULL, true);
				const char* dfreespace = DBP_Option::Get(DBP_Option::bootos_dfreespace); // can also be "discard" or "hide"
				if (dfreespace && dfreespace[0] != 'h')
				{
					std::string save_path = DBP_GetSaveFile(SFT_VIRTUALDISK, NULL, &save_hash); // always call to fill out save_hash and dbp_vdisk_filter
					Bit32u freeSpace = (Bit32u)atoi(dfreespace);
					imageDiskList[newC-'A'] = new imageDisk(Drives['C'-'A'], (freeSpace ? freeSpace : 1024), (freeSpace ? save_path.c_str() : NULL) , save_hash, &dbp_vdisk_filter);
				}
			}

			// Ramdisk setting must be false while installing os
			char ramdisk = (is_install ? 'f' : DBP_Option::Get(DBP_Option::bootos_ramdisk)[0]);

			// Now mount OS hard disk image as C: drive
			if (BatchFileBoot::MountOSIMG('C', path.c_str(), "OS image", (ramdisk == 'f'), true) && ramdisk == 'd')
				imageDiskList['C'-'A']->SetDifferencingDisk(DBP_GetSaveFile(SFT_DIFFDISK).c_str());
		}
		else if (!imageDiskList['C'-'A'] && Drives['C'-'A'])
		{
			// Running without hard disk image uses the DOS C: drive as a read-only C: hard disk
			imageDiskList['C'-'A'] = new imageDisk(Drives['C'-'A'], 0);
		}

		// Try reading a boot disk image off from an ISO file
		Bit8u* bootdisk_image; Bit32u bootdisk_size;
		if (!Drives['A'-'A'] && Drives['D'-'A'] && dynamic_cast<isoDrive*>(Drives['D'-'A']) && ((isoDrive*)(Drives['D'-'A']))->CheckBootDiskImage(&bootdisk_image, &bootdisk_size))
		{
			Drives['Y'-'A'] = new memoryDrive();
			DriveCreateFile(Drives['Y'-'A'], "CDBOOT.IMG", bootdisk_image, bootdisk_size);
			free(bootdisk_image);
			DBP_Mount(DBP_AppendImage("$Y:\\CDBOOT.IMG", false)); // this mounts the image as the A drive
			//// Generate autoexec bat that starts the OS setup program?
			//DriveCreateFile(Drives['A'-'A'], "CONFIG.SYS", (const Bit8u*)"", 0);
			//DriveCreateFile(Drives['A'-'A'], "AUTOEXEC.BAT", (const Bit8u*)"DIR\r\n", 5);
		}

		// Setup IDE controllers for the CDROM drive
		IDE_SetupControllers(true);

		// Install the NE2000 network card
		NET_SetupEthernet();

		// Switch cputype to highest feature set (needed for Windows 9x) and increase real mode CPU cycles
		Section* section = control->GetSection("cpu");
		section->ExecuteDestroy(false);
		#if C_MMX
		section->HandleInputline("cputype=pentium_mmx");
		#else
		section->HandleInputline("cputype=pentium_slow");
		#endif
		if (DBP_Option::Get(DBP_Option::bootos_forcenormal)[0] == 't') section->HandleInputline("core=normal");
		section->ExecuteInit(false);
		section->GetProp("cputype")->MarkFixed();
		if (dbp_content_year < 1993 && (CPU_CycleAutoAdjust || (CPU_AutoDetermineMode & (CPU_AUTODETERMINE_CYCLES|(CPU_AUTODETERMINE_CYCLES<<CPU_AUTODETERMINE_SHIFT))))) DBP_SetCyclesByYear(1993, 1993);

		RunBatchFile(new BatchFileBoot(!is_install ? 'C' : 'A'));
	}

	static void RunShell(int shellidx)
	{
		if (!Drives['C'-'A']) return;
		if (dbp_had_game_running) { DBP_OnBIOSReboot(); return; }
		dbp_had_game_running = true;

		unionDrive* base_drive = dynamic_cast<unionDrive*>(Drives['C'-'A']);
		if (!base_drive) return;
		std::string path = DBP_GetSaveFile(SFT_SYSTEMDIR).append(dbp_shellzips[shellidx]);
		FILE* zip_file_h = fopen_wrap(path.c_str(), "rb");
		if (!zip_file_h) { emuthread_notify(0, LOG_ERROR, "Unable to open %s file: %s", "System Shell", path.c_str()); return; }
		base_drive->AddUnder(*new zipDrive(new rawFile(zip_file_h, false), false), true);

		const char* exes[] = { "C:\\WINDOWS.BAT", "C:\\AUTOEXEC.BAT", "C:\\WINDOWS\\WIN.COM" };
		for (const char* exe : exes)
			if (Drives['C'-'A']->FileExists(exe + 3))
				{ RunBatchFile(new BatchFileExec(exe)); return; }

		ConsoleClearScreen();
		Bit16u sz;
		DOS_WriteFile(STDOUT, (Bit8u*)"To auto run the shell, make sure one of these files exist:\r\n", &(sz = sizeof("To auto run the shell, make sure one of these files exist:\r\n")-1));
		for (const char* exe : exes) { DOS_WriteFile(STDOUT, (Bit8u*)"\r\n- ", &(sz = 4)); DOS_WriteFile(STDOUT, (Bit8u*)exe, &(sz = (Bit16u)strlen(exe))); }
		DOS_WriteFile(STDOUT, (Bit8u*)"\r\n\r\n", &(sz = 4));
		KEYBOARD_AddKey(KBD_enter, true);
		KEYBOARD_AddKey(KBD_enter, false);
	}

	enum EMode : Bit8u { RUN_NONE, RUN_EXEC, RUN_BOOTIMG, RUN_BOOTOS, RUN_INSTALLOS, RUN_SHELL, RUN_VARIANT, RUN_COMMANDLINE };
	static struct Startup { EMode mode = RUN_NONE; bool reboot = false; int info = 0; std::string exec; } startup;
	static struct Autoboot { Startup startup; bool have = false, use = false; int skip = 0; Bit32u hash = 0; } autoboot;
	static struct Autoinput { std::string str; const char* ptr = NULL; Bit32s oldcycles = 0; Bit8u oldchange = 0; Bit16s oldyear = 0; } autoinput;
	static struct Patch { int enabled_variant = 0; bool show_default = false; } patch;

	static bool Run(EMode mode, int info, std::string& str, bool from_osd = false)
	{
		DBP_ASSERT(from_osd || mode != RUN_VARIANT); // only the OSD can switch the variant
		if (from_osd) autoinput.str.clear();

		// Always reboot if variant is changed to revert to YML defaults 
		// We only refresh YML here because we reboot anyway (which will restart the patch drive)
		// Also we actually don't want to switch underlying files before saving union drive save file differences.
		startup.reboot |= patchDrive::ActivateVariant((mode == RUN_VARIANT ? info : patch.enabled_variant), true);

		if (mode == RUN_VARIANT)
		{
			patch.enabled_variant = info;
			DOSYMLLoader ymlload(true);
			startup.reboot |= ymlload.reboot; // read startup and autoinput from YML
			mode = startup.mode;
			info = startup.info;
			if (mode == RUN_NONE) return false; // YML had no startup
			autoboot.use = !ymlload.is_utility; // disable autoboot for utility config
			autoboot.skip = 0; // otherwise force enable auto start when switching variant
			WriteAutoBoot(RUN_VARIANT, patch.enabled_variant, str);
		}
		else
		{
			if (from_osd) WriteAutoBoot(mode, info, str);
			startup.mode = mode;
			startup.info = info;
			if (mode == RUN_EXEC) startup.exec.swap(str); // remember to set cursor again and for rebooting a different IT_RUN
		}

		const Property* bootImgMachine = ((mode == RUN_BOOTIMG) ? control->GetProp("dosbox", "machine") : NULL);
		if (startup.reboot || dbp_game_running || !control || (from_osd && first_shell->bf && !first_shell->bf->IsAutoexec()) || (bootImgMachine && info && info != *(const char*)bootImgMachine->GetValue()))
		{
			startup.reboot = false;
			if (bootImgMachine) dbp_reboot_machine = (info ? (char)info : *(const char*)bootImgMachine->GetValue());
			DBP_OnBIOSReboot();
			return true;
		}

		if (autoboot.use && autoboot.skip)
		{
			autoinput.str.assign(31, ' ');
			autoinput.str.resize(sprintf(&autoinput.str[0], (autoboot.skip == -1 ? "(WAITMODECHANGE)" : "(WAIT:%d)"), autoboot.skip * 15));
		}

		autoinput.ptr = ((mode != RUN_COMMANDLINE && autoinput.str.size()) ? autoinput.str.c_str() : NULL);
		autoinput.oldcycles = 0;
		if (autoinput.ptr && dbp_content_year > 1970 && (CPU_CycleAutoAdjust || (CPU_AutoDetermineMode & (CPU_AUTODETERMINE_CYCLES|(CPU_AUTODETERMINE_CYCLES<<CPU_AUTODETERMINE_SHIFT)))))
		{
			// enforce cycle rate during auto input (but limited to 1994 CPU speed, above will likely just waste time waiting for rendering out the skipped frames)
			autoinput.oldcycles = CPU_CycleMax;
			autoinput.oldchange = (Bit8u)control->GetProp("cpu", "cycles")->getChange();
			autoinput.oldyear = dbp_content_year;
			if (dbp_content_year > 1994) dbp_content_year = 1994;
			DBP_SetCyclesByYear(dbp_content_year, 1994);
		}

		// if a booted OS does a bios reboot, auto reboot that OS from now on
		if (mode == RUN_EXEC || mode == RUN_COMMANDLINE)
			startup.mode = RUN_NONE;

		if (mode == RUN_EXEC)
			Exec(startup.exec);
		else if (mode == RUN_BOOTIMG)
			BootImage();
		else if (mode == RUN_BOOTOS || mode == RUN_INSTALLOS)
			BootOS(mode == RUN_INSTALLOS, startup.info);
		else if (mode == RUN_SHELL)
			RunShell(startup.info);
		return true;
	}

	struct DOSYMLLoader
	{
		const char *Key, *End, *Next, *KeyX, *Val, *ValX, *first_startup_mode_key = NULL;
		int cpu_cycles = 0, cpu_hz = 0, cpu_year = 0, cpu_set_max = 0;
		bool reboot, is_utility = false;
		bool Parse(const char *yml_key, const char* db_section, const char* db_key, ...)
		{
			if (yml_key && (strncmp(yml_key, Key, (size_t)(KeyX - Key)) || yml_key[KeyX - Key])) return false;
			va_list ap; va_start(ap, db_key); std::string val;
			for (;;)
			{
				const char* mapFrom = va_arg(ap, const char*);
				if (!*mapFrom) { va_end(ap); return false; }
				if (*mapFrom == '~')
				{
					val.append(Val, (size_t)(ValX - Val));
				}
				else if (*mapFrom == '/')
				{
					char buf[32];
					sprintf(buf, "%d", (atoi(Val) / 1024));
					val = buf;
				}
				else if (*mapFrom == '^')
				{
					if (db_key[1] == 'p') // called by ProcessKey() with 'mpu401'
					{
						Parse(yml_key, "midi", "midiconfig", mapFrom);
						val.assign("intelligent");
					}
					else // recursively called from above with 'midiconfig'
					{
						((val += '^') += (yml_key[7] == 't' ? 'M' : 'S')).append(Val, (size_t)(ValX - Val));
					}
				}
				else
				{
					const char* mapTo = va_arg(ap, const char*);
					if (strncmp(mapFrom, Val, (size_t)(ValX - Val))) continue;
					val.append(mapTo);
				}
				va_end(ap);
				Section* section = control->GetSection(db_section);
				Property* prop = section->GetProp(db_key);
				const Value& propVal = prop->GetValue();
				const char* set_val = val.c_str();
				bool set = !strcmp(set_val, (propVal.type == Value::V_STRING ? (const char*)propVal : propVal.ToString().c_str()));
				if (!set)
				{
					bool will_reboot = (reboot || (prop->getChange() > Property::Changeable::WhenIdle));
					if (!will_reboot) section->ExecuteDestroy(false);
					set = (!set && prop->SetValue(val) && !strcmp(set_val, (propVal.type == Value::V_STRING ? (const char*)propVal : propVal.ToString().c_str())));
					if (!will_reboot) section->ExecuteInit(false);
					if (set) reboot = will_reboot;
				}
				if (set) prop->MarkFixed();
				return set;
			}
		}
		bool ParseCPU(const char *yml_key)
		{
			if (strncmp(yml_key, Key, (size_t)(KeyX - Key)) || yml_key[KeyX - Key]) return false;
			again: switch (yml_key[4])
			{
				case 'm': cpu_set_max = 1; yml_key += 4; goto again; // cpu_max_*
				case 'c': return ((cpu_cycles = atoi(Val)) >=  100); // cpu_cycles
				case 'h': return ((cpu_hz     = atoi(Val)) >=  500); // cpu_hz
				case 'y': return ((cpu_year   = atoi(Val)) >= 1970); // cpu_year
			}
			return false;
		}
		bool ParseRun(const char *yml_key)
		{
			if (strncmp(yml_key, Key, (size_t)(KeyX - Key)) || yml_key[KeyX - Key]) return false;
			switch (yml_key[4])
			{
				case 'i': // run_input
					autoinput.ptr = NULL;
					autoinput.str.clear();
					autoinput.str.append(Val, (size_t)(ValX - Val));
					break;
				case 'p': // run_path
					startup.exec = std::string(Val, (size_t)(ValX - Val));
					if (startup.mode == RUN_BOOTIMG) goto exec2bootimg;
					if (!first_startup_mode_key) first_startup_mode_key = Key;
					startup.mode = RUN_EXEC;
					break;
				case 'b': // run_boot
				case 'm': // run_mount
					{
						int imgidx = -1;
						for (DBP_Image& i : dbp_images)
							if ((i.path.size() == (size_t)(4+(ValX - Val)) && i.path[0] == '$' && !strncasecmp(&i.path[4], Val, (ValX - Val)))
								|| (i.longpath.size() == (size_t)(ValX - Val) &&  !strncasecmp(&i.longpath[0], Val, (ValX - Val))))
								{ imgidx = (int)(&i - &dbp_images[0]); break; }
						if (imgidx == -1) return false;
						dbp_images[imgidx].remount = true;
					}
					if (yml_key[4] == 'm') break; // run_mount
					if (startup.mode == RUN_EXEC)
					{
						exec2bootimg:
						((static_cast<Section_line*>(control->GetSection("autoexec"))->data += '@') += startup.exec) += '\n';
					}
					if (!first_startup_mode_key) first_startup_mode_key = Key;
					startup.mode = RUN_BOOTIMG;
					startup.info = 0;
					break;
				case 'u': // run_utility
					is_utility = ((Val[0]|0x20) == 't');
					break;
			}
			return true;
		}
		bool ProcessKey()
		{
			switch (*Key)
			{
				case 'c':
					return (0
						#if C_MMX
						||Parse("cpu_type", "cpu", "cputype" , "auto","auto" , "generic_386","386" , "generic_486","486_slow" , "generic_pentium","pentium_slow" , "generic_pentium_mmx","pentium_mmx" , "")
						#else
						||Parse("cpu_type", "cpu", "cputype" , "auto","auto" , "generic_386","386" , "generic_486","486_slow" , "generic_pentium","pentium_slow" , "")
						#endif
						||ParseCPU("cpu_cycles")||ParseCPU("cpu_hz")||ParseCPU("cpu_year")||ParseCPU("cpu_max_cycles")||ParseCPU("cpu_max_hz")||ParseCPU("cpu_max_year")
					);
				case 'm':
					return (0
						||Parse("mem_size", "dosbox", "memsize", "/")
						||Parse("mem_xms", "dos", "xms" , "true","true" , "false","false" , "")
						||Parse("mem_ems", "dos", "ems" , "true","true" , "false","false" , "")
						||Parse("mem_umb", "dos", "umb" , "true","true" , "false","false" , "")
						||Parse("mem_doslimit", "dos", "memlimit", "~")
					);
				case 'v':
					return (0
						||Parse("video_card", "dosbox", "machine" , "generic_svga","svga_s3" , "generic_hercules","hercules" , "generic_cga","cga" , "generic_ega","ega" , "generic_vga","vgaonly" , "generic_tandy","tandy" , "generic_pcjr","pcjr" , "tandy","tandy" , "pcjr","pcjr" , "svga_s3_trio","svga_s3", "svga_tseng_et3000","svga_et3000" , "svga_tseng_et4000","svga_et4000" , "svga_paradise_pvga1a","svga_paradise" , "")
						||Parse("video_memory", "dosbox", "vmemsize", "/")
						||Parse("video_voodoo", "pci", "voodoo" , "v1_8mb","8mb" , "v1_4mb","4mb" , "none","false" , "")
					);
				case 's':
					return (0
						||Parse("sound_card", "sblaster", "sbtype" , "sb16","sb16" , "sb1","sb1" , "sb2","sb2" , "sbpro1","sbpro1" , "sbpro2","sbpro2" , "gameblaster","gb" , "none","none" , "")
						||Parse("sound_port", "sblaster", "sbbase" , "~")
						||Parse("sound_irq", "sblaster", "irq", "~")
						||Parse("sound_dma", "sblaster", "dma", "~")
						||Parse("sound_hdma", "sblaster", "hdma", "~")
						||Parse("sound_midi", "midi", "mpu401" , "true","intelligent" , "false","none" , "^")
						||Parse("sound_mt32", "midi", "mpu401" , "true","intelligent" , "false","none" , "^")
						||Parse("sound_gus", "gus", "gus" , "true","true" , "false","false" , "")
						||Parse("sound_tandy", "speaker", "tandy" , "true","on" , "false","auto" , "")
					);
				case 'r':
					return (0
						||ParseRun("run_path")
						||ParseRun("run_boot")
						||ParseRun("run_mount")
						||ParseRun("run_input")
						||ParseRun("run_utility")
					);
				case 'i':
					return DBP_PadMapping::ParseInputYML(Key, KeyX, Val, ValX);
			}
			return false;
		}
		DOSYMLLoader(bool parseRun, bool isPreInit = false) : reboot(isPreInit)
		{
			if (parseRun) startup.mode = RUN_NONE;
			DBP_PadMapping::ResetYML();
			for (Key = patchDrive::dos_yml.c_str(), End = Key+patchDrive::dos_yml.size(); Key < End; Key = Next + 1)
			{
				for (Next = Key; *Next != '\n' && *Next != '\r' && *Next; Next++) {}
				if (Next == Key || *Key == '#') continue;
				for (KeyX = Key; *KeyX && *KeyX != ':' && *KeyX > ' '; KeyX++) {}
				if (*KeyX != ':' || KeyX == Key || KeyX[1] != ' ' ) goto syntaxerror;
				for (Val = KeyX + 2; *Val == ' '; Val++) {}
				for (ValX = Val; *ValX && *ValX != '\r' && *ValX != '\n' && (*ValX != '#' || ValX[-1] != ' '); ValX++) {}
				while (ValX[-1] == ' ') ValX--;
				if (ValX <= Val) goto syntaxerror;
				if ((*Key == 'r' && !parseRun) || ProcessKey()) continue;
				syntaxerror:
				emuthread_notify(0, LOG_ERROR, "Error in DOS.YML: %.*s", (int)(Next-Key), Key);
				continue;
			}
			if (cpu_cycles || cpu_year || cpu_hz)
			{
				if (cpu_cycles) {}
				else if (cpu_year) cpu_cycles = (int)DBP_CyclesForYear(cpu_year);
				else
				{
					float cycle_per_hz = .3f; // default with auto (just a bad guess)
					switch (*(const char*)control->GetProp("cpu", "cputype")->GetValue())
					{
						case 'p': cycle_per_hz = .55700f; break; // Pentium (586):  Mhz * 557.00
						case '4': cycle_per_hz = .38000f; break; // 486:            Mhz * 380.00
						case '3': cycle_per_hz = .18800f; break; // 386:            Mhz * 188.00
						case '2': cycle_per_hz = .09400f; break; // AT (286):       Mhz *  94.00
						case '8': cycle_per_hz = .05828f; break; // XT (8088/8086): Mhz *  58.28
					}
					cpu_cycles = (int)(cpu_hz * cycle_per_hz + .4999f);
				}
				char buf[32];
				ValX = (Val = buf) + sprintf(buf, "%s%d", (cpu_set_max ? "max limit " : ""), (int)cpu_cycles);
				if (Parse(NULL, "cpu", "cycles", "~") && cpu_cycles >= 8192) // Switch to dynamic core for newer real mode games
					{ ValX = (Val = "dynamic") + 7; Parse(NULL, "cpu", "core", "~"); }
			}
			if (!reboot) DBP_PadMapping::PostYML();
		}
	};

	static bool PostInitFirstTime()
	{
		ReadAutoBoot();
		size_t root_yml_len = patchDrive::dos_yml.size();
		patchDrive::ActivateVariant(patch.enabled_variant);
		DOSYMLLoader ymlload(true);

		// Show default variant as an option only if there is a auto run setting in the base root yml
		patch.show_default = (ymlload.first_startup_mode_key && ymlload.first_startup_mode_key < patchDrive::dos_yml.c_str() + root_yml_len);

		// reset and re-run PreInit to set all settings of yml
		if (ymlload.reboot) return true;

		if (autoboot.use && autoboot.startup.mode != RUN_VARIANT) startup = autoboot.startup;
		else if (startup.mode != RUN_NONE && !autoboot.use && patchDrive::variants.Len()) startup.mode = RUN_NONE;
		return false;
	}

	static void PreInit(bool newcontent)
	{
		if (newcontent)
		{
			startup = Startup();
			autoboot = Autoboot();
			autoinput = Autoinput();
			patch = Patch();
			patchDrive::ResetVariants();
		}
		if (!dbp_biosreboot) startup.mode = RUN_NONE;
		if (patchDrive::dos_yml.size())
		{
			DOSYMLLoader(!dbp_biosreboot && (patchDrive::variants.Len() == 0 || autoboot.startup.mode == RUN_VARIANT), true); // ignore run keys on bios reboot
		}
		if (!dbp_biosreboot && autoboot.use && autoboot.startup.mode != RUN_VARIANT) startup = autoboot.startup;
	}

	static void ReadAutoBoot()
	{
		char buf[DOS_PATHLENGTH + 32 + 256 + 256 + 2];
		Bit16u autostrlen = DriveReadFileBytes(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)buf, (Bit16u)(sizeof(buf)-1));
		autoboot.have = !!autostrlen;

		const char* cpath = (autostrlen ? NULL : strrchr(dbp_content_path.c_str(), '#'));
		if (cpath && (dbp_content_path.c_str() + dbp_content_path.length() - cpath) <= DOS_PATHLENGTH)
			autostrlen = (Bit16u)sprintf(buf, "%s%s", (cpath[1] && cpath[2] == ':' ? "" : "C:\\"), cpath + 1);

		for (char *p = buf, *pEnd = p + autostrlen, *line, line_no = 1; p != pEnd; line_no++)
		{
			if ((p += (!*p ? (!p[1] ? 2 : 1) : 0)) >= pEnd) break;
			for (line = p; p != pEnd && *p >= ' ';) p++;
			if (*p == '\r' && p[1] == '\n') p[1] = '\0';
			*p = '\0'; // for strcmp/atoi/DOS_FileExists/assign
			if (line_no == 1)
			{
				char linetype = (line[1] == '*' ? line[0] : 0), *str = line + (linetype ? 2 : 0);
				if (linetype == 0)
				{
					char *param = strchr(str, ' ');
					if (param) *param = '\0';
					bool exists = DOS_FileExists(str);
					if (param) *param = ' ';
					if (exists) { autoboot.startup.mode = RUN_EXEC; autoboot.startup.exec.assign(str); }
				}
				else if (linetype == 'O' || linetype == 'S')
				{
					const size_t suffix_len              = (linetype == 'O' ?             4:             5);
					const std::vector<std::string>& strs = (linetype == 'O' ? dbp_osimages : dbp_shellzips);
					for (const std::string& it : strs)
						if (it.size() == (p - str) + suffix_len && !memcmp(str, it.c_str(), it.size() - suffix_len))
							{ autoboot.startup.mode = (linetype == 'O' ? RUN_BOOTOS : RUN_SHELL); autoboot.startup.info = (int)(&it - &strs[0]); break; }
				}
				else if (linetype == 'V' && patchDrive::variants.Len()) { autoboot.startup.mode = RUN_VARIANT; autoboot.skip = 0; line += 2; goto parseVariant; }
				else if (linetype == 'I')
				{
					for (const char* it : DBP_MachineNames)
						if (!strcmp(it, str))
							{ autoboot.startup.mode = RUN_BOOTIMG; autoboot.startup.info = (Bit16s)(it[0]|0x20); break; }
				}
			}
			else if (line_no == 2)
			{
				autoboot.skip = atoi(line);
			}
			else if (line_no == 3 && *line)
			{
				for (const DBP_Image& i : dbp_images)
					if (!strcmp(DBP_Image_Label(i), line))
						{ if (!i.mounted) DBP_Mount((unsigned)(&i - &dbp_images[0])); break; }
			}
			else if (line_no == 4)
			{
				parseVariant:
				if (const std::string* v = patchDrive::variants.Get(line))
					patch.enabled_variant = patchDrive::variants.GetStorageIndex(v) + 1;
			}
		}
		autoboot.use = (autoboot.startup.mode != RUN_NONE);
		autoboot.hash = HashAutoBoot();
	}

	static void WriteAutoBoot(EMode mode, int info, const std::string& str)
	{
		if (!autoboot.use || mode == RUN_NONE || mode == RUN_INSTALLOS || mode == RUN_COMMANDLINE)
		{
			if (autoboot.have) Drives['C'-'A']->FileUnlink((char*)"AUTOBOOT.DBP");
			autoboot.startup.mode = RUN_NONE; autoboot.skip = 0; autoboot.have = autoboot.use = false;
			return;
		}
		DBP_ASSERT(mode == RUN_EXEC || mode == RUN_BOOTOS || mode == RUN_SHELL || mode == RUN_VARIANT || mode == RUN_BOOTIMG);
		autoboot.startup.mode = mode;
		autoboot.startup.info = info;
		autoboot.startup.exec.assign(mode == RUN_EXEC ? str.c_str() : "");
		if (HashAutoBoot() == autoboot.hash) return;
		autoboot.have = true;
		autoboot.hash = HashAutoBoot();
		const char* varname = (patch.enabled_variant ? patchDrive::variants.GetStorage()[patch.enabled_variant - 1].c_str() : NULL), *img = NULL;
		const char *var = ((varname && mode != RUN_VARIANT) ? varname : NULL), *line1 = (mode != RUN_VARIANT ? str.c_str() : (varname ? varname : ""));
		for (const DBP_Image& i : dbp_images) { if (i.mounted) { if (&i != &dbp_images[0]) img = DBP_Image_Label(i); break; } }
		char buf[DOS_PATHLENGTH + 32 + 256 + 256], *p = buf;
		if (mode != RUN_EXEC) { *(p++) = (mode == RUN_BOOTOS ? 'O' : mode == RUN_SHELL ? 'S' : mode == RUN_VARIANT ? 'V' : 'I'); *(p++) = '*'; }
		if (1)                           p += snprintf(p, (&buf[sizeof(buf)] - p), "%s", line1);                // line 1
		if (var || img || autoboot.skip) p += snprintf(p, (&buf[sizeof(buf)] - p), "\r\n%d", autoboot.skip);    // line 2
		if (var || img)                  p += snprintf(p, (&buf[sizeof(buf)] - p), "\r\n%s", (img ? img : "")); // line 3
		if (var)                         p += snprintf(p, (&buf[sizeof(buf)] - p), "\r\n%s", var);              // line 4
		if (!DriveCreateFile(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)buf, (Bit32u)(p - buf))) { DBP_ASSERT(false); }
	}

	static Bit32u HashAutoBoot() { return DriveCalculateCRC32((Bit8u*)&autoboot, sizeof(autoboot), BaseStringToPointerHashMap::Hash(autoboot.startup.exec.c_str())); } 

	static Bit32u ModeHash() { return (Bit32u)((render.src.width * 2100781) ^ (render.src.height * 65173) ^ ((Bitu)(render.src.fps * 521)) ^ (render.src.bpp * 31) ^ ((Bitu)vga.mode + 1)); }

	static void ProcessAutoInput()
	{
		extern Bitu PIC_Ticks;
		static Bitu InpTickStart, InpNextTick; static Bit32u InpDelay, InpReleaseKey, InpSkipMode;
		if (autoinput.ptr == autoinput.str.c_str())
			InpTickStart = PIC_Ticks, InpNextTick = 0, InpDelay = 70, InpReleaseKey = InpSkipMode = 0;

		const Bitu InpDoneTicks = PIC_Ticks - InpTickStart;
		if (InpSkipMode && !vga.draw.resizing)
		{
			Bit32u mode = ModeHash();
			if (InpSkipMode == mode) { } // video mode unchanged
			else if (InpSkipMode < 31) { InpSkipMode = mode; } // initial resolution was set (before it was size 0)
			else { InpSkipMode = 0; InpNextTick = InpDoneTicks; } // new video mode
		}

		while (InpDoneTicks >= InpNextTick)
		{
			if (InpReleaseKey)
			{
				if (InpReleaseKey & 0x100) { KEYBOARD_AddKey(KBD_rightalt, false); InpReleaseKey &= 0xFF; }
				if (InpReleaseKey & 0x80) { KEYBOARD_AddKey(KBD_leftshift, false); InpReleaseKey &= 0x7F; }
				KEYBOARD_AddKey((KBD_KEYS)InpReleaseKey, false);
				InpReleaseKey = 0;
				if (*autoinput.ptr) { InpNextTick += InpDelay; continue; }
			}
			if (!*autoinput.ptr) { autoinput.ptr = NULL; break; }

			const char *cmd = autoinput.ptr, *cmdNext = cmd + 1, *cmdColon = NULL;
			bool bShift = false, bAltGr = false;
			char tmp;
			Bit32u i = 0, cmdlen = 1;

			if (cmd[0] != '(' || cmd[1] == '(')
			{
				if (!(cmd[0] != '(')) cmdNext++; // Treat (( as textinput (
				char DBP_DOS_KeyboardLayout_MapChar(char c, bool& bShift, bool& bAltGr);
				switch ((tmp = DBP_DOS_KeyboardLayout_MapChar(cmd[0], bShift, bAltGr)))
				{
					case '\x1B': i = KBD_esc;          break;
					case '-':    i = KBD_minus;        break;
					case '=':    i = KBD_equals;       break;
					case '\b':   i = KBD_backspace;    break;
					case '\t':   i = KBD_tab;          break;
					case '[':    i = KBD_leftbracket;  break;
					case ']':    i = KBD_rightbracket; break;
					case ';':    i = KBD_semicolon;    break;
					case '\'':   i = KBD_quote;        break;
					case '`':    i = KBD_grave;        break;
					case '\\':   i = KBD_backslash;    break;
					case ',':    i = KBD_comma;        break;
					case '.':    i = KBD_period;       break;
					case '/':    i = KBD_slash;        break;
					default: cmd = &tmp;
				}
			}
			else if ((cmdNext = strchr(cmdNext, ')')) != NULL)
			{
				if ((cmdColon = strchr(++cmd, ':')) != NULL && cmdColon >= cmdNext-1) cmdColon = NULL;
				cmdlen = (Bit32u)((cmdColon ? cmdColon : cmdNext) - cmd);
				cmdNext++;
			}

			if (i == 0)
				for (; i != KBD_LAST+3; i++)
					if (!strncasecmp(DBP_YMLKeyCommands[i], cmd, cmdlen) && DBP_YMLKeyCommands[i][cmdlen] == '\0')
						break;

			if (i == KBD_LAST+0 && cmdColon) // wait command
			{
				InpNextTick += atoi(cmdColon+1);
			}
			else if (i == KBD_LAST+1) // waitmodechange command
			{
				if (vga.draw.resizing && InpDoneTicks && (InpDoneTicks - InpNextTick < 5000)) break; // don't start while vga is resizing
				InpNextTick += 30000; // wait max 30 seconds (if the game crashes, auto input is aborted below)
				InpSkipMode = ModeHash();
			}
			else if (i == KBD_LAST+2 && cmdColon) // delay command
			{
				InpDelay = (Bit32u)atoi(cmdColon+1);
			}
			else if (i < KBD_LAST && cmdColon && (!strncasecmp(cmdColon+1, "down", 4) || strncasecmp(cmdColon+1, "up", 2))) // key command
			{
				KEYBOARD_AddKey((KBD_KEYS)i, (cmdColon[1]|0x20) == 'd');
			}
			else if (i < KBD_LAST) // key press
			{
				if (bShift) KEYBOARD_AddKey(KBD_leftshift, true);
				if (bAltGr) KEYBOARD_AddKey(KBD_rightalt, true);
				KEYBOARD_AddKey((KBD_KEYS)i, true);
				InpReleaseKey = (i | (bShift ? 0x80 : 0) | (bAltGr ? 0x100 : 0));
				InpNextTick += 70; // fixed press duration
			}
			else
			{
				log_cb(RETRO_LOG_INFO, "[DOSBOX ERROR] Unknown command in run_input string: '%s'\n", cmd);
				autoinput.ptr = NULL;
				break;
			}
			autoinput.ptr = cmdNext;
		}

		// Check if done, dbp_game_running should switch to true at tick 1 unless the game crashes or exits but give it 5 seconds to be sure
		if (autoinput.ptr && (dbp_game_running || InpDoneTicks < 5000))
		{
			// Disable line rendering (without using VGA frameskipping which affects the emulation)
			struct Local { static void EmptyLineHandler(const void*) { } };
			RENDER_DrawLine = Local::EmptyLineHandler;
			// Scrap mixed audio instead of accumulating it while skipping frames
			DBP_MIXER_ScrapAudio();
		}
		else
		{
			// done
			autoinput.ptr = NULL; // reset on game crash/exit (dbp_game_running is false)
			DBP_KEYBOARD_ReleaseKeys();
			if (autoinput.oldcycles)
			{
				if (!CPU_CycleAutoAdjust && CPU_CycleMax == DBP_CyclesForYear(dbp_content_year, 1994) && control->GetProp("cpu", "cycles")->getChange() == autoinput.oldchange)
					CPU_CycleMax = autoinput.oldcycles; // revert from Run()
				else if (CPU_CycleAutoAdjust && cpu.pmode && (CPU_AutoDetermineMode & (CPU_AUTODETERMINE_CORE<<CPU_AUTODETERMINE_SHIFT)))
					CPU_OldCycleMax = autoinput.oldcycles; // we switched to protected mode since auto input, fix up old cycles
				dbp_content_year = autoinput.oldyear;
				DBP_SetRealModeCycles(); // if still in real mode reset the defaults
			}
		}
	}
};

DBP_Run::Startup DBP_Run::startup;
DBP_Run::Autoinput DBP_Run::autoinput;
DBP_Run::Autoboot DBP_Run::autoboot;
DBP_Run::Patch DBP_Run::patch;
