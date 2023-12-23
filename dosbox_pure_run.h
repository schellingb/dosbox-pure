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

static const char* DBP_MachineNames[] = { "SVGA (Super Video Graphics Array)", "VGA (Video Graphics Array)", "EGA (Enhanced Graphics Adapter", "CGA (Color Graphics Adapter)", "Tandy (Tandy Graphics Adapter", "Hercules (Hercules Graphics Card)", "PCjr" };

struct DBP_Run
{
	static void RunBatchFile(BatchFile* bf)
	{
		DBP_ASSERT(!dbp_game_running);
		const bool inAutoexec = (first_shell->bf && first_shell->bf->filename[0] == 'Z');
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
		BatchFileExec(const std::string& _exe) : BatchFile(first_shell,"Z:\\AUTOEXEC.BAT","","") { filename = _exe; }
		virtual bool ReadLine(char * line)
		{
			*(line++) = '@';
			switch (location++)
			{
				case 0:
				{
					ConsoleClearScreen();

					char *p = (char*)filename.c_str(), *f = strrchr(p, '\\') + 1, *fext;
					DOS_SetDefaultDrive(p[0]-'A');
					if (f - p > 3)
					{
						memcpy(Drives[p[0]-'A']->curdir,p + 3, f - p - 4);
						Drives[p[0]-'A']->curdir[f - p - 4] = '\0';
					}
					else Drives[p[0]-'A']->curdir[0] = '\0';

					bool isbat = ((fext = strrchr(f, '.')) && !strcasecmp(fext, ".bat"));
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

		static bool HaveCDImage()
		{
			for (DBP_Image& i : dbp_images) if (DBP_Image_IsCD(i)) return true;
			return false;
		}

		static bool MountOSIMG(char drive, const char* path, const char* type, bool needwritable, bool complainnotfound)
		{
			FILE* raw_file_h = NULL;
			if (needwritable && (raw_file_h = fopen_wrap(path, "rb+")) != NULL) goto openok;
			if ((raw_file_h = fopen_wrap(path, "rb")) == NULL)
			{
				if (complainnotfound)
					retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s%s", type, path, "");
				return false;
			}
			if (needwritable)
			{
				retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s%s", type, path, " (file is read-only!)");
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
			DBP_Mount(); // make sure something is mounted

			// If hard disk image was mounted to D:, swap it to be the bootable C: drive
			std::swap(imageDiskList['D'-'A'], imageDiskList['C'-'A']);

			// If there is no mounted hard disk image but a D: drive, setup the CDROM IDE controller
			if (!imageDiskList['C'-'A'] && Drives['D'-'A'])
				IDE_SetupControllers(BatchFileBoot::HaveCDImage() ? 'D' : 0);

			// Install the NE2000 network card
			NET_SetupEthernet();
		}

		RunBatchFile(new BatchFileBoot(imageDiskList['A'-'A'] ? 'A' : 'C'));
	}

	static void BootOS(bool is_install, int osidx_or_size)
	{
		// Make sure we have at least 32 MB of RAM, if not set it to 64
		if ((MEM_TotalPages() / 256) < 32)
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
			if (!f) { retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s%s", "OS image", path.c_str(), " (create file failed)"); return; }
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

		const bool have_cd_image = BatchFileBoot::HaveCDImage();
		if (!path.empty())
		{
			// When booting an external disk image as C:, use whatever is C: in DOSBox DOS as the second hard disk in the booted OS (it being E: in Drives[] doesn't matter)
			char newC = ((have_cd_image || DBP_IsMounted('D')) ? 'E' : 'D'); // alternative would be to do DBP_Remount('D', 'E'); and always use 'D'
			if (imageDiskList['C'-'A'])
				imageDiskList[newC-'A'] = imageDiskList['C'-'A'];
			else if (!BatchFileBoot::MountOSIMG(newC, (dbp_content_path + ".img").c_str(), "D: drive image", true, false) && Drives['C'-'A'])
			{
				Bit32u save_hash = 0;
				DBP_SetDriveLabelFromContentPath(Drives['C'-'A'], dbp_content_path.c_str(), 'C', NULL, NULL, true);
				std::string save_path = DBP_GetSaveFile(SFT_VIRTUALDISK, NULL, &save_hash);
				imageDiskList[newC-'A'] = new imageDisk(Drives['C'-'A'], atoi(retro_get_variable("dosbox_pure_bootos_dfreespace", "1024")), save_path.c_str(), save_hash, &dbp_vdisk_filter);
			}

			// Ramdisk setting must be false while installing os
			char ramdisk = (is_install ? 'f' : retro_get_variable("dosbox_pure_bootos_ramdisk", "false")[0]);

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
			DBP_Mount(DBP_AppendImage("$Y:\\CDBOOT.IMG", false), true); // this mounts the image as the A drive
			//// Generate autoexec bat that starts the OS setup program?
			//DriveCreateFile(Drives['A'-'A'], "CONFIG.SYS", (const Bit8u*)"", 0);
			//DriveCreateFile(Drives['A'-'A'], "AUTOEXEC.BAT", (const Bit8u*)"DIR\r\n", 5);
		}

		// Setup IDE controllers for the hard drives and one CDROM drive (if any CDROM image is mounted)
		IDE_SetupControllers(have_cd_image ? 'D' : 0);

		// Install the NE2000 network card
		NET_SetupEthernet();

		// Switch cputype to highest feature set (needed for Windows 9x) and increase real mode CPU cycles
		Section* section = control->GetSection("cpu");
		section->ExecuteDestroy(false);
		section->HandleInputline("cputype=pentium_slow");
		if (retro_get_variable("dosbox_pure_bootos_forcenormal", "false")[0] == 't') section->HandleInputline("core=normal");
		section->ExecuteInit(false);
		if (Property* p = section->GetProp("cputype")) p->OnChangedByConfigProgram();
		if (dbp_content_year < 1993) { dbp_content_year = 1993; DBP_SetRealModeCycles(); }

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
		if (!zip_file_h) { retro_notify(0, RETRO_LOG_ERROR, "Unable to open %s file: %s", "System Shell", path.c_str()); return; }
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

	enum EMode { RUN_NONE, RUN_EXEC, RUN_BOOTIMG, RUN_BOOTOS, RUN_INSTALLOS, RUN_SHELL, RUN_COMMANDLINE };
	static struct Startup { EMode mode; int info; std::string str; } startup;
	static struct Autoboot { bool have, use; int skip, hash; } autoboot;
	static struct Autoinput { std::string str; const char* ptr; } autoinput;

	static void Run(EMode mode, int info, std::string& str, bool write_auto_boot = false)
	{
		startup.mode = mode;
		startup.info = info;
		std::swap(startup.str, str); // remember to set cursor again and for rebooting a different IT_RUN

		if (write_auto_boot)
			WriteAutoBoot();

		char mchar;
		if (dbp_game_running || (mode == RUN_BOOTIMG && info && info != (mchar = GetDosBoxMachineChar())))
		{
			if (mode == RUN_BOOTIMG) dbp_reboot_machine = (info ? (char)info : mchar);
			DBP_OnBIOSReboot();
			return;
		}

		if (autoboot.use && autoboot.skip)
		{
			autoinput.str.assign(31, ' ');
			autoinput.str.resize(sprintf(&autoinput.str[0], "(WAIT:%d)", autoboot.skip * 15));
		}

		autoinput.ptr = ((mode != RUN_COMMANDLINE && autoinput.str.size()) ? autoinput.str.c_str() : NULL);

		// if a booted OS does a bios reboot, auto reboot that OS from now on
		if (mode == RUN_EXEC || mode == RUN_COMMANDLINE)
			startup.mode = RUN_NONE;

		if (mode == RUN_EXEC)
			Exec(startup.str);
		else if (mode == RUN_BOOTIMG)
			BootImage();
		else if (mode == RUN_BOOTOS || mode == RUN_INSTALLOS)
			BootOS(mode == RUN_INSTALLOS, startup.info);
		else if (mode == RUN_SHELL)
			RunShell(startup.info);
	}

	INLINE static void ResetStartup() { startup.mode = RUN_NONE; }
	INLINE static void ResetAutoboot() { autoboot.use = autoboot.have = false; }
	static char GetDosBoxMachineChar() { return *((const char*)control->GetSection("dosbox")->GetProp("machine")->GetValue()); }

	static bool HandleStartup(bool is_boot)
	{
		if (startup.mode == RUN_NONE) ReadAutoBoot();
		if (startup.mode == RUN_NONE || !is_boot) return false;
		Run(startup.mode, startup.info, startup.str);
		return true;
	}

	static void ReadAutoBoot()
	{
		char buf[DOS_PATHLENGTH + 32 + 256 + 1];
		Bit16u autostrlen = DriveReadFileBytes(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)buf, (Bit16u)(sizeof(buf)-1));
		autoboot.have = !!autostrlen;

		const char* cpath = (autostrlen ? NULL : strrchr(dbp_content_path.c_str(), '#'));
		if (cpath && (dbp_content_path.c_str() + dbp_content_path.length() - cpath) <= DOS_PATHLENGTH)
			autostrlen = (Bit16u)sprintf(buf, "%s%s", (cpath[1] && cpath[2] == ':' ? "" : "C:\\"), cpath + 1);

		for (char *p = buf, *pEnd = p + autostrlen, *line, line_no = 1; p != pEnd; line_no++)
		{
			while (p != pEnd && *p <= ' ') p++;
			if (p == pEnd) break;
			for (line = p; p != pEnd && *p >= ' ';) p++;
			*p = '\0'; // for strcmp/atoi/DOS_FileExists/assign
			if (line_no == 1)
			{
				const char linetype = (line[1] == '*' ? line[0] : 0), *startup_str = line + (linetype ? 2 : 0);
				if (linetype == 0)
				{
					startup.mode = RUN_EXEC;
					if (DOS_FileExists(startup_str)) goto auto_ok;
				}
				else if (linetype == 'O')
				{
					startup.mode = RUN_BOOTOS;
					for (const std::string& im : dbp_osimages)
						if (im.size() == (p - startup_str) + 4 && !memcmp(startup_str, im.c_str(), im.size() - 4))
							{ startup.info = (int)(&im - &dbp_osimages[0]); goto auto_ok; }
				}
				else if (linetype == 'S')
				{
					startup.mode = RUN_SHELL;
					for (const std::string& im : dbp_shellzips)
						if (im.size() == (p - startup_str) + 5 && !memcmp(startup_str, im.c_str(), im.size() - 5))
							{ startup.info = (int)(&im - &dbp_shellzips[0]); goto auto_ok; }
				}
				else if (linetype == 'I')
				{
					startup.mode = RUN_BOOTIMG;
					for (const char* it : DBP_MachineNames)
						if (!strcmp(it, startup_str))
							{ startup.info = (Bit16s)(it[0]|0x20); goto auto_ok; }
				}
				startup.mode = RUN_NONE;
				continue;
				auto_ok:
				startup.str.assign(startup_str);
			}
			else if (line_no == 2)
			{
				autoboot.skip = atoi(line);
			}
			else if (line_no == 3)
			{
				for (const DBP_Image& i : dbp_images)
					if (!strcmp(DBP_Image_Label(i), line))
						{ if (!i.mounted) DBP_Mount((unsigned)(&i - &dbp_images[0]), true); break; }
			}
		}
		autoboot.use = (startup.mode != RUN_NONE);
		autoboot.hash = AutobootHash();
	}

	static void WriteAutoBoot()
	{
		if (autoboot.have && !autoboot.use)
		{
			autoinput.str.clear();
			autoboot.have = false;
			Drives['C'-'A']->FileUnlink((char*)"AUTOBOOT.DBP");
		}
		else if (autoboot.use && (!autoboot.have || autoboot.hash != AutobootHash()) && startup.mode != RUN_INSTALLOS && startup.mode != RUN_COMMANDLINE)
		{
			DBP_ASSERT(startup.mode == RUN_EXEC || startup.mode == RUN_BOOTOS || startup.mode == RUN_SHELL || startup.mode == RUN_BOOTIMG);
			autoboot.hash = AutobootHash();
			autoboot.have = true;
			const char* img = NULL;
			for (const DBP_Image& i : dbp_images) { if (i.mounted) { if (&i != &dbp_images[0]) img = DBP_Image_Label(i); break; } }
			char buf[DOS_PATHLENGTH + 32 + 256], *p = buf;
			if (startup.mode != RUN_EXEC) { *(p++) = (startup.mode == RUN_BOOTOS ? 'O' : (startup.mode == RUN_SHELL ? 'S' : 'I')); *(p++) = '*'; }
			p += snprintf(p, (&buf[sizeof(buf)] - p), "%s", startup.str.c_str()); // line 1
			if (img || autoboot.skip) p += snprintf(p, (&buf[sizeof(buf)] - p), "\r\n%d", autoboot.skip); // line 2
			if (img) p += snprintf(p, (&buf[sizeof(buf)] - p), "\r\n%s", img); // line 3
			if (!DriveCreateFile(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)buf, (Bit32u)(p - buf))) { DBP_ASSERT(false); }
		}
	}

	static int AutobootHash()
	{
		return (13 * autoboot.skip) ^ (int)StringToPointerHashMap<void>::Hash(startup.str.c_str()) ^ (93911 * dbp_image_index);
	}

	static void ProcessAutoInput()
	{
		extern Bitu PIC_Ticks;
		static Bitu InpTickStart, InpNextTick; static Bit32u InpDelay, InpReleaseKey;
		if (autoinput.ptr == autoinput.str.c_str())
			InpTickStart = PIC_Ticks, InpNextTick = 0, InpDelay = 70, InpReleaseKey = 0;

		Bitu InpDoneTicks = PIC_Ticks - InpTickStart;
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
				KBD_KEYS mappedkey = KBD_NONE;
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

			static const char* DBP_Commands[KBD_LAST+2] =
			{
				"","1","2","3","4","5","6","7","8","9","0","q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l","z","x","c","v","b","n","m",
				"f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","esc","tab","backspace","enter","space","leftalt","rightalt","leftctrl","rightctrl","leftshift","rightshift",
				"capslock","scrolllock","numlock","grave","minus","equals","backslash","leftbracket","rightbracket","semicolon","quote","period","comma","slash","extra_lt_gt",
				"printscreen","pause","insert","home","pageup","delete","end","pagedown","left","up","down","right","kp1","kp2","kp3","kp4","kp5","kp6","kp7","kp8","kp9","kp0",
				"kpdivide","kpmultiply","kpminus","kpplus","kpenter","kpperiod",
				"wait","delay",
			};
			if (i == 0)
				for (; i != KBD_LAST+2; i++)
					if (!strncasecmp(DBP_Commands[i], cmd, cmdlen) && DBP_Commands[i][cmdlen] == '\0')
						break;

			if (i == KBD_LAST+0 && cmdColon) // wait command
			{
				InpNextTick += atoi(cmdColon+1);
			}
			else if (i == KBD_LAST+1 && cmdColon) // delay command
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

		if (autoinput.ptr)
		{
			// Disable line rendering (without using VGA frameskipping which affects the emulation)
			struct Local { static void EmptyLineHandler(const void*) { } };
			RENDER_DrawLine = Local::EmptyLineHandler;
			// Scrap mixed audio instead of accumulating it while skipping frames
			int mixSamples = (int)DBP_MIXER_DoneSamplesCount();
			if (mixSamples > DBP_MAX_SAMPLES) mixSamples = DBP_MAX_SAMPLES;
			if (mixSamples > 200) MIXER_CallBack(0, (Bit8u*)dbp_audio, (mixSamples - 100) * 4);
		}
		else DBP_KEYBOARD_ReleaseKeys(); // done
	}
};

DBP_Run::Startup DBP_Run::startup;
DBP_Run::Autoinput DBP_Run::autoinput;
DBP_Run::Autoboot DBP_Run::autoboot;
