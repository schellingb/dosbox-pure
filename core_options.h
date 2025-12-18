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

namespace DBP_OptionCat
{
	static const char* General     = "General";
	static const char* Input       = "Input";
	static const char* Performance = "Performance";
	static const char* Video       = "Video";
	static const char* System      = "System";
	static const char* Audio       = "Audio";
};

static retro_core_option_v2_category option_cats[] =
{
	#ifndef DBP_STANDALONE
	{ DBP_OptionCat::General,     DBP_OptionCat::General,     "General settings (save states, start menu, fixed FPS)." },
	#else
	{ DBP_OptionCat::General,     DBP_OptionCat::General,     "General settings (hotkeys, start menu, fixed FPS)." },
	#endif
	{ DBP_OptionCat::Input,       DBP_OptionCat::Input,       "Keyboard, mouse and joystick settings." },
	{ DBP_OptionCat::Performance, DBP_OptionCat::Performance, "Adjust the performance of the emulated CPU." },
	{ DBP_OptionCat::Video,       DBP_OptionCat::Video,       "Settings for the emulated graphics card and aspect ratio." },
	{ DBP_OptionCat::System,      DBP_OptionCat::System,      "Other hardware emulation settings for RAM, CPU and OS." },
	{ DBP_OptionCat::Audio,       DBP_OptionCat::Audio,       "MIDI, SoundBlaster and other audio settings." },
	{ NULL, NULL, NULL }
};

namespace DBP_Option
{
	enum Index
	{
		#ifdef DBP_STANDALONE
		// Interface
		_interface_hotkeymod,
		_interface_speedtoggle,
		_interface_fastrate,
		_interface_slowrate,
		_interface_systemhotkeys,
		_interface_middlemouse,
		_interface_lockmouse,
		#endif
		// General
		forcefps,
		#ifndef DBP_STANDALONE
		savestate,
		#endif
		strict_mode,
		conf,
		menu_time,
		menu_transparency,
		// Input
		map_osd_hotkey,
		map_osd,
		mouse_input,
		mouse_wheel,
		mouse_speed_factor,
		mouse_speed_factor_x,
		actionwheel_inputs,
		auto_mapping,
		keyboard_layout,
		joystick_analog_deadzone,
		joystick_timed,
		// Performance
		cycles,
		cycles_max,
		cycles_scale,
		cycle_limit,
		perfstats,
		// Video
		machine,
		cga,
		hercules,
		svga,
		svgamem,
		voodoo,
		voodoo_perf,
		voodoo_scale,
		voodoo_gamma,
		#ifdef DBP_STANDALONE
		interface_scaling,
		interface_crtfilter,
		interface_crtscanline,
		interface_crtblur,
		interface_crtmask,
		interface_crtcurvature,
		interface_crtcorner,
		#endif
		aspect_correction,
		overscan,
		// System
		memory_size,
		modem,
		cpu_type,
		cpu_core,
		bootos_ramdisk,
		bootos_dfreespace,
		bootos_forcenormal,
		// Audio
		#ifndef DBP_STANDALONE
		audiorate,
		#else
		_interface_audiolatency,
		#endif
		sblaster_conf,
		midi,
		sblaster_type,
		sblaster_adlib_mode,
		sblaster_adlib_emu,
		gus,
		tandysound,
		swapstereo,
		_OPTIONS_NULL_TERMINATOR, _OPTIONS_TOTAL,
	};

	const char* Get(Index idx, bool* was_modified = NULL);
	bool Apply(Section& section, const char* var_name, const char* new_value, bool disallow_in_game = false, bool need_restart = false, bool user_modified = false);
	bool GetAndApply(Section& section, const char* var_name, Index idx, bool disallow_in_game = false, bool need_restart = false);
	void SetDisplay(Index idx, bool visible);
	bool GetHidden(const retro_core_option_v2_definition& d);
};

static retro_core_option_v2_definition option_defs[DBP_Option::_OPTIONS_TOTAL] =
{
	// General
	#ifdef DBP_STANDALONE
	{
		"interface_hotkeymod",
		"Hotkey Modifier", NULL,
		"Set which modifier keys need to be held to use hotkeys." "\n"
			"   F1  - Pause/Resume (F12 to step a frame while paused)" "\n"
			"   F2  - Slow Motion (toggle/while holding)" "\n"
			"   F3  - Fast Forward (toggle/while holding)" "\n"
			"   F5  - Quick Save" "\n"
			"   F7  - Full Screen/Window" "\n"
			"   F9  - Quick Load" "\n"
			"   F11 - Lock Mouse" "\n"
			"   F12 - Toggle On-Screen Menu", NULL,
		DBP_OptionCat::General,
		{
			{ "1", "CTRL" },
			{ "2", "ALT" },
			{ "4", "SHIFT" },
			{ "3", "CTRL+ALT" },
			{ "5", "CTRL+SHIFT" },
			{ "6", "ALT+SHIFT" },
			{ "7", "CTRL+ALT+SHIFT" },
			{ "8", "WIN" },
			{ "16", "MENU" },
			{ "0", "None" },
		},
		"1"
	},
	{
		"interface_speedtoggle",
		"Fast Forward/Slow Motion Mode", NULL,
		"Set if fast forward and slow motion is a toggle or hold.", NULL,
		DBP_OptionCat::General,
		{
			{ "toggle", "Toggle" },
			{ "hold", "Hold" },
		},
		"toggle"
	},
	{
		"interface_fastrate",
		"Fast Forward Limit", NULL,
		"Set the limit of fast forwarding.", NULL,
		DBP_OptionCat::General,
		{
			{ "1.1" , "110%" }, { "1.2" , "120%" }, { "1.3" , "130%" }, { "1.5" , "150%" }, { "1.75" , "175%" }, { "2" , "200%" }, { "2.5" , "250%" }, { "3" , "300%" },
			{ "4" , "400%" }, { "5" , "500%" }, { "6" , "600%" }, { "7" , "700%" }, { "8" , "800%" }, { "9" , "900%" }, { "10" , "1000%" }, { "0" , "As fast as possible" }, 
		},
		"5"
	},
	{
		"interface_slowrate",
		"Slow Motion Rate", NULL,
		"Set the speed while slow motion is active.", NULL,
		DBP_OptionCat::General,
		{
			{ "0.1", "10%" }, { "0.2", "20%" }, { "0.3", "30%" }, { "0.4", "40%" }, { "0.5", "50%" }, { "0.6", "60%" }, 
			{ "0.7", "70%" }, { "0.75", "75%" }, { "0.8", "80%" }, { "0.85", "85%" }, { "0.9", "90%" }, { "0.95", "95%" },
		},
		"0.3"
	},
	{
		"interface_systemhotkeys",
		"Always Enable System Hotkeys", NULL,
		"Set if ALT+F4 (Quit) and ALT+Enter (Full Screen) are handled even while a game is running.", NULL,
		DBP_OptionCat::General,
		{
			{ "false", "Off" },
			{ "true", "On" },
		},
		"true"
	},
	{
		"interface_middlemouse",
		"Middle Mouse Button Open Menu", NULL,
		"If enabled the middle mouse button will open/close the On-Screen Menu.", NULL,
		DBP_OptionCat::General,
		{
			{ "false", "Off" },
			{ "true", "On" },
		},
		"false"
	},
	{
		"interface_lockmouse",
		"Mouse Lock Default Status", NULL,
		"Will have the mouse locked at program start if enabled.", NULL,
		DBP_OptionCat::General,
		{
			{ "false", "Off" },
			{ "true", "On" },
		},
		"false"
	},
	#endif
	{
		"dosbox_pure_force60fps", // legacy name
		"Force Output FPS", NULL,
		"Enable this to force output at a fixed rate. Try 60 FPS if you encounter screen tearing or vsync issues." "\n"
		"Output will have frames skipped at lower rates and frames duplicated at higher rates.", NULL,
		DBP_OptionCat::General,
		{
			{ "false", "Off" },
			{ "10",   "On (10 FPS)" },
			{ "15",   "On (15 FPS)" },
			{ "20",   "On (20 FPS)" },
			{ "30",   "On (30 FPS)" },
			{ "35",   "On (35 FPS)" },
			{ "50",   "On (50 FPS)" },
			{ "true", "On (60 FPS)" },
			{ "70",   "On (70 FPS)" },
			{ "90",   "On (90 FPS)" },
			{ "120",  "On (120 FPS)" },
			{ "144",  "On (144 FPS)" },
			{ "240",  "On (240 FPS)" },
			{ "360",  "On (360 FPS)" },
		},
		"false"
	},
	#ifndef DBP_STANDALONE
	{
		"dosbox_pure_savestate",
		"Save States Support", NULL,
		"Make sure to test it in each game before using it. Complex late era DOS games might have problems." "\n"
		"Be aware that states saved with different video, CPU or memory settings are not loadable." "\n"
		"Rewind support comes at a high performance cost and needs at least 40MB of rewind buffer.", NULL,
		DBP_OptionCat::General,
		{
			{ "on",       "Enable save states" },
			{ "rewind",   "Enable save states with rewind" },
			{ "disabled", "Disabled" },
		},
		"on"
	},
	#endif
	{
		"dosbox_pure_strict_mode",
		"Advanced > Use Strict Mode", NULL,
		"Disable the command line, running installed operating systems and using .BAT/.COM/.EXE/DOS.YML files from the save game.", NULL,
		DBP_OptionCat::General,
		{
			{ "false", "Off" },
			{ "true", "On" },
		},
		"false"
	},
	{
		"dosbox_pure_conf",
		"Advanced > Loading of dosbox.conf", NULL,
		"DOSBox Pure is meant to be configured via core options but optionally supports loading of legacy .conf files.", NULL,
		DBP_OptionCat::General,
		{
			{ "false", "Disabled conf support (default)" },
			{ "inside", "Try 'dosbox.conf' in the loaded content (ZIP or folder)" },
			{ "outside", "Try '.conf' with same name as loaded content (next to ZIP or folder)" },
		},
		"false"
	},
	{
		"dosbox_pure_menu_time",
		"Advanced > Start Menu", NULL,
		"Set the behavior of the start menu before and after launching a game." "\n"
		"You can also force it to open by holding shift or L2/R2 when selecting 'Restart'.", NULL,
		DBP_OptionCat::General,
		{
			{ "99", "Show at start, show again after game exit (default)" },
#ifndef STATIC_LINKING
			{ "5", "Show at start, shut down core 5 seconds after auto started game exit" },
			{ "3", "Show at start, shut down core 3 seconds after auto started game exit" },
			{ "0", "Show at start, shut down core immediately after auto started game exit" },
#endif
			{ "-1", "Always show menu on startup and after game exit, ignore auto start setting" },
		},
		"99"
	},
	{
		"dosbox_pure_menu_transparency",
		"Advanced > Menu Transparency", NULL,
		"Set the transparency level of the Menu and the On-Screen Keyboard.", NULL,
		DBP_OptionCat::General,
		{
			{ "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" },
		},
		"70"
	},

	// Input
	{
		"dosbox_pure_menu_action",
		"Menu Activation Inputs", NULL,
		"Choose whether the DOSBox Pure menu can be opened using the L3 button, Ctrl+Home hotkey, both, or neither.", NULL,
		DBP_OptionCat::Input,
		{
			{ "true",   "L3 and Ctrl+Home (default)" },
			{ "L3",     "L3 button only" },
			{ "hotkey", "Ctrl+Home only" },
			{ "false",  "Off (disable both inputs)" },
		},
		"true"
	},
	{
		"dosbox_pure_on_screen_keyboard", // legacy name
		"Menu Behavior for L3 Button & Hotkey", NULL,
		"Select which menu is opened by the L3 controller button and Ctrl+Home keyboard hotkey." "\n"
		"The default setting reopens the previously viewed menu. You can swap CDs/disks on the Start Menu." "\n"
		"The On-Screen Keyboard is for controllers and touchscreens. Gamepad Mapper can setup controller mapping.", NULL,
		DBP_OptionCat::Input,
		{
			{ "true",      "Open previous menu (default)" },
			{ "startmenu", "Always open Start Menu (swap CDs/discs)" },
			{ "keyboard",  "Always open On-Screen Keyboard" },
			{ "mapper",    "Always open Gamepad Mapper" },
		},
		"true"
	},
	{
		"dosbox_pure_mouse_input",
		"Mouse Input Mode", NULL,
		"You can disable input handling from a mouse or a touchscreen (emulated mouse through joypad will still work)." "\n"
		"In touchpad mode use drag to move, tap to click, two finger tap to right-click and press-and-hold to drag", NULL,
		DBP_OptionCat::Input,
#if defined(ANDROID) || defined(DBP_IOS) || defined(HAVE_LIBNX) || defined(_3DS) || defined(WIIU) || defined(VITA)
		{
			{ "pad", "Touchpad mode (default, see description, best for touch screens)" },
			{ "true", "Auto (virtual or direct)" },
			{ "virtual", "Virtual mouse movement" },
			{ "direct", "Direct controlled mouse (not supported by all games)" },
			{ "false", "Off (ignore mouse inputs)" },
		},
		"pad"
#else
		{
			{ "true", "Auto (default)" },
			{ "virtual", "Virtual mouse movement" },
			{ "direct", "Direct controlled mouse (not supported by all games)" },
			{ "pad", "Touchpad mode (see description, best for touch screens)" },
			{ "false", "Off (ignore mouse inputs)" },
		},
		"true"
#endif
	},
	{
		"dosbox_pure_mouse_wheel",
		"Bind Mouse Wheel To Key", NULL,
		"Bind mouse wheel up and down to two keyboard keys to be able to use it in DOS games.", NULL,
		DBP_OptionCat::Input,
		{
			{ "67/68", "Left-Bracket/Right-Bracket" },
			{ "72/71", "Comma/Period" },
			{ "79/82", "Page-Up/Page-Down" },
			{ "78/81", "Home/End" },
			{ "80/82", "Delete/Page-Down" },
			{ "64/65", "Minus/Equals" },
			{ "69/70", "Semicolon/Quote" },
			{ "99/100", "Numpad Minus/Plus" },
			{ "97/98", "Numpad Divide/Multiply" },
			{ "84/85", "Up/Down" },
			{ "83/86", "Left/Right" },
			{ "11/13", "Q/E" },
			{ "none", "Disable" },
		},
		"67/68"
	},
	{
		"dosbox_pure_mouse_speed_factor",
		"Mouse Sensitivity", NULL,
		"Sets the overall mouse cursor movement speed." "\n\n", NULL, //end of Input section
		DBP_OptionCat::Input,
		{
			{ "0.2",  "20%" }, { "0.25",  "25%" }, { "0.3",  "30%" }, { "0.35",  "35%" }, { "0.4",  "40%" }, { "0.45",  "45%" },
			{ "0.5",  "50%" }, { "0.55",  "55%" }, { "0.6",  "60%" }, { "0.65",  "65%" }, { "0.7",  "70%" }, { "0.75",  "75%" },
			{ "0.8",  "80%" }, { "0.85",  "85%" }, { "0.9",  "90%" }, { "0.95",  "95%" }, { "1.0", "100%" }, { "1.1" , "110%" },
			{ "1.2", "120%" }, { "1.3" , "130%" }, { "1.4", "140%" }, { "1.5" , "150%" }, { "1.6", "160%" }, { "1.7" , "170%" },
			{ "1.8", "180%" }, { "1.9" , "190%" }, { "2.0", "200%" }, { "2.2" , "220%" }, { "2.4", "240%" }, { "2.6" , "260%" },
			{ "2.8", "280%" }, { "3.0" , "300%" }, { "3.2", "320%" }, { "3.4" , "340%" }, { "3.6", "360%" }, { "3.8" , "380%" },
			{ "4.0", "400%" }, { "4.2" , "420%" }, { "4.4", "440%" }, { "4.6",  "460%" }, { "4.8", "480%" }, { "5.0",  "500%" },
		},
		"1.0"
	},
	{
		"dosbox_pure_mouse_speed_factor_x",
		"Advanced > Horizontal Mouse Sensitivity", NULL,
		"Experiment with this value if the mouse is too fast/slow when moving left/right.", NULL,
		DBP_OptionCat::Input,
		{
			{ "0.2",  "20%" }, { "0.25",  "25%" }, { "0.3",  "30%" }, { "0.35",  "35%" }, { "0.4",  "40%" }, { "0.45",  "45%" },
			{ "0.5",  "50%" }, { "0.55",  "55%" }, { "0.6",  "60%" }, { "0.65",  "65%" }, { "0.7",  "70%" }, { "0.75",  "75%" },
			{ "0.8",  "80%" }, { "0.85",  "85%" }, { "0.9",  "90%" }, { "0.95",  "95%" }, { "1.0", "100%" }, { "1.1" , "110%" },
			{ "1.2", "120%" }, { "1.3" , "130%" }, { "1.4", "140%" }, { "1.5" , "150%" }, { "1.6", "160%" }, { "1.7" , "170%" },
			{ "1.8", "180%" }, { "1.9" , "190%" }, { "2.0", "200%" }, { "2.2" , "220%" }, { "2.4", "240%" }, { "2.6" , "260%" },
			{ "2.8", "280%" }, { "3.0" , "300%" }, { "3.2", "320%" }, { "3.4" , "340%" }, { "3.6", "360%" }, { "3.8" , "380%" },
			{ "4.0", "400%" }, { "4.2" , "420%" }, { "4.4", "440%" }, { "4.6",  "460%" }, { "4.8", "480%" }, { "5.0",  "500%" },
		},
		"1.0"
	},
	{
		"dosbox_pure_actionwheel_inputs",
		"Advanced > Action Wheel Inputs", NULL,
		"Sets which inputs control the action wheel.", NULL,
		DBP_OptionCat::Input,
		{
			{ "14", "Right Stick, D-Pad, Mouse (Default)" }, { "6",  "Right Stick, D-Pad" }, { "10", "Right Stick, Mouse" }, { "2",  "Right Stick" },
			{ "15", "Both Sticks, D-Pad, Mouse" }, { "7",  "Both Sticks, D-Pad" }, { "11", "Both Sticks, Mouse" }, { "3",  "Both Sticks" },
			{ "13", "Left Stick, D-Pad, Mouse" }, { "5",  "Left Stick, D-Pad" }, { "9",  "Left Stick, Mouse" }, { "1",  "Left Stick" },
			{ "12", "D-Pad, Mouse" }, { "4",  "D-Pad" }, { "8",  "Mouse" },
		},
		"14"
	},
	{
		"dosbox_pure_auto_mapping",
		"Advanced > Automatic Game Pad Mappings", NULL,
		"DOSBox Pure can automatically apply a gamepad control mapping scheme when it detects a game." "\n"
		"These button mappings are provided by the Keyb2Joypad Project (by Jemy Murphy and bigjim).", NULL,
		DBP_OptionCat::Input,
		{ { "true", "On (default)" }, { "notify", "Enable with notification on game detection" }, { "false", "Off" } },
		"true"
	},
	{
		"dosbox_pure_keyboard_layout",
		"Advanced > Keyboard Layout", NULL,
		"Select the keyboard layout (will not change the On-Screen Keyboard).", NULL,
		DBP_OptionCat::Input,
		{
			{ "us",    "US (default)" },
			{ "uk",    "UK" },
			{ "be",    "Belgium" },
			{ "br",    "Brazil" },
			{ "hr",    "Croatia" },
			{ "cz243", "Czech Republic" },
			{ "dk",    "Denmark" },
			{ "su",    "Finland" },
			{ "fr",    "France" },
			{ "gr",    "Germany" },
			{ "gk",    "Greece" },
			{ "hu",    "Hungary" },
			{ "is161", "Iceland" },
			{ "it",    "Italy" },
			{ "nl",    "Netherlands" },
			{ "no",    "Norway" },
			{ "pl",    "Poland" },
			{ "po",    "Portugal" },
			{ "ru",    "Russia" },
			{ "sk",    "Slovakia" },
			{ "si",    "Slovenia" },
			{ "sp",    "Spain" },
			{ "sv",    "Sweden" },
			{ "sg",    "Switzerland (German)" },
			{ "sf",    "Switzerland (French)" },
			{ "tr",    "Turkey" },
		},
		"us"
	},
	{
		"dosbox_pure_joystick_analog_deadzone",
		"Advanced > Joystick Analog Deadzone", NULL,
		"Set the deadzone of the joystick analog sticks. May be used to eliminate drift caused by poorly calibrated joystick hardware.", NULL,
		DBP_OptionCat::Input,
		{
			{ "0",  "0%" }, { "5",  "5%" }, { "10", "10%" }, { "15", "15%" }, { "20", "20%" }, { "25", "25%" }, { "30", "30%" }, { "35", "35%" }, { "40", "40%" },
		},
		"15"
	},
	{
		"dosbox_pure_joystick_timed",
		"Advanced > Enable Joystick Timed Intervals", NULL,
		"Enable timed intervals for joystick axes. Experiment with this option if your joystick drifts." "\n\n", NULL, //end of Input > Advanced section
		DBP_OptionCat::Input,
		{ { "true", "On (default)" }, { "false", "Off" } },
		"true"
	},

	// Performance
	{
		"dosbox_pure_cycles",
		"Emulated Performance", NULL,
		"The raw performance that DOSBox will try to emulate." "\n\n", NULL, //end of Performance section
		DBP_OptionCat::Performance,
		{
			{ "auto",    "AUTO - DOSBox will try to detect performance needs (default)" },
			{ "max",     "MAX - Emulate as many instructions as possible" },
			{ "315",     "8086/8088, 4.77 MHz from 1980 (315 cps)" },
			{ "1320",    "286, 6 MHz from 1982 (1320 cps)" },
			{ "2750",    "286, 12.5 MHz from 1985 (2750 cps)" },
			{ "4720",    "386, 20 MHz from 1987 (4720 cps)" },
			{ "7800",    "386DX, 33 MHz from 1989 (7800 cps)" },
			{ "13400",   "486DX, 33 MHz from 1990 (13400 cps)" },
			{ "26800",   "486DX2, 66 MHz from 1992 (26800 cps)" },
			{ "77000",   "Pentium, 100 MHz from 1995 (77000 cps)" },
			{ "200000",  "Pentium II, 300 MHz from 1997 (200000 cps)" },
			{ "500000",  "Pentium III, 600 MHz from 1999 (500000 cps)" },
			{ "1000000", "AMD Athlon, 1.2 GHz from 2000 (1000000 cps)" },
		},
		"auto"
	},
	{
		"dosbox_pure_cycles_max",
		"Detailed > Maximum Emulated Performance", NULL,
		"With dynamic CPU speed (AUTO or MAX above), the maximum emulated performance level.", NULL,
		DBP_OptionCat::Performance,
		{
			{ "none",    "Unlimited" },
			{ "315",     "8086/8088, 4.77 MHz from 1980 (315 cps)" },
			{ "1320",    "286, 6 MHz from 1982 (1320 cps)" },
			{ "2750",    "286, 12.5 MHz from 1985 (2750 cps)" },
			{ "4720",    "386, 20 MHz from 1987 (4720 cps)" },
			{ "7800",    "386DX, 33 MHz from 1989 (7800 cps)" },
			{ "13400",   "486DX, 33 MHz from 1990 (13400 cps)" },
			{ "26800",   "486DX2, 66 MHz from 1992 (26800 cps)" },
			{ "77000",   "Pentium, 100 MHz from 1995 (77000 cps)" },
			{ "200000",  "Pentium II, 300 MHz from 1997 (200000 cps)" },
			{ "500000",  "Pentium III, 600 MHz from 1999 (500000 cps)" },
			{ "1000000", "AMD Athlon, 1.2 GHz from 2000 (1000000 cps)" },
		},
		"none"
	},
	{
		"dosbox_pure_cycles_scale",
		"Detailed > Performance Scale", NULL,
		"Fine tune the emulated performance for specific needs.", NULL,
		DBP_OptionCat::Performance,
		{
			{ "0.2",  "20%" }, { "0.25",  "25%" }, { "0.3",  "30%" }, { "0.35",  "35%" }, { "0.4",  "40%" }, { "0.45",  "45%" },
			{ "0.5",  "50%" }, { "0.55",  "55%" }, { "0.6",  "60%" }, { "0.65",  "65%" }, { "0.7",  "70%" }, { "0.75",  "75%" },
			{ "0.8",  "80%" }, { "0.85",  "85%" }, { "0.9",  "90%" }, { "0.95",  "95%" }, { "1.0", "100%" }, { "1.05", "105%" },
			{ "1.1", "110%" }, { "1.15", "115%" }, { "1.2", "120%" }, { "1.25", "125%" }, { "1.3", "130%" }, { "1.35", "135%" },
			{ "1.4", "140%" }, { "1.45", "145%" }, { "1.5", "150%" }, { "1.55", "155%" }, { "1.6", "160%" }, { "1.65", "165%" },
			{ "1.7", "170%" }, { "1.75", "175%" }, { "1.8", "180%" }, { "1.85", "185%" }, { "1.9", "190%" }, { "1.95", "195%" },
			{ "2.0", "200%" },
		},
		"1.0",
	},
	{
		"dosbox_pure_cycle_limit",
		"Detailed > Limit CPU Usage", NULL,
		"When emulating DOS as fast as possible, how much time per frame should be used by the emulation." "\n"
		"Lower this if your device becomes hot while using this core." "\n\n", NULL, //end of Performance > Detailed section
		DBP_OptionCat::Performance,
		{
			//{ "0.2", "20%" }, { "0.21", "21%" }, { "0.22", "22%" }, { "0.23", "23%" }, { "0.24", "24%" }, { "0.25", "25%" }, { "0.26", "26%" }, { "0.27", "27%" }, { "0.28", "28%" }, { "0.29", "29%" },
			//{ "0.3", "30%" }, { "0.31", "31%" }, { "0.32", "32%" }, { "0.33", "33%" }, { "0.34", "34%" }, { "0.35", "35%" }, { "0.36", "36%" }, { "0.37", "37%" }, { "0.38", "38%" }, { "0.39", "39%" },
			//{ "0.4", "40%" }, { "0.41", "41%" }, { "0.42", "42%" }, { "0.43", "43%" }, { "0.44", "44%" }, { "0.45", "45%" }, { "0.46", "46%" }, { "0.47", "47%" }, { "0.48", "48%" }, { "0.49", "49%" },
			{ "0.5", "50%" }, { "0.51", "51%" }, { "0.52", "52%" }, { "0.53", "53%" }, { "0.54", "54%" }, { "0.55", "55%" }, { "0.56", "56%" }, { "0.57", "57%" }, { "0.58", "58%" }, { "0.59", "59%" },
			{ "0.6", "60%" }, { "0.61", "61%" }, { "0.62", "62%" }, { "0.63", "63%" }, { "0.64", "64%" }, { "0.65", "65%" }, { "0.66", "66%" }, { "0.67", "67%" }, { "0.68", "68%" }, { "0.69", "69%" },
			{ "0.7", "70%" }, { "0.71", "71%" }, { "0.72", "72%" }, { "0.73", "73%" }, { "0.74", "74%" }, { "0.75", "75%" }, { "0.76", "76%" }, { "0.77", "77%" }, { "0.78", "78%" }, { "0.79", "79%" },
			{ "0.8", "80%" }, { "0.81", "81%" }, { "0.82", "82%" }, { "0.83", "83%" }, { "0.84", "84%" }, { "0.85", "85%" }, { "0.86", "86%" }, { "0.87", "87%" }, { "0.88", "88%" }, { "0.89", "89%" },
			{ "0.9", "90%" }, { "0.91", "91%" }, { "0.92", "92%" }, { "0.93", "93%" }, { "0.94", "94%" }, { "0.95", "95%" }, { "0.96", "96%" }, { "0.97", "97%" }, { "0.98", "98%" }, { "0.99", "99%" },
			{ "1.0", "100%" },
			//{ "1.01", "101%" }, { "1.02", "102%" }, { "1.1", "110%" }, { "1.2", "120%" } 
		},
		"1.0",
	},
	{
		"dosbox_pure_perfstats",
		"Advanced > Show Performance Statistics", NULL,
		"Enable this to show statistics about performance and framerate and check if emulation runs at full speed.", NULL,
		DBP_OptionCat::Performance,
		{
			{ "none",     "Disabled" },
			{ "simple",   "Simple" },
			{ "detailed", "Detailed information" },
		},
		"none"
	},

	// Video
	{
		"dosbox_pure_machine",
		"Emulated Graphics Chip (restart required)", NULL,
		"The type of graphics chip that DOSBox will emulate.", NULL,
		DBP_OptionCat::Video,
		{
			{ "svga",     "SVGA (Super Video Graphics Array) (default)" },
			{ "vga",      "VGA (Video Graphics Array)" },
			{ "ega",      "EGA (Enhanced Graphics Adapter)" },
			{ "cga",      "CGA (Color Graphics Adapter)" },
			{ "tandy",    "Tandy (Tandy Graphics Adapter)" },
			{ "hercules", "Hercules (Hercules Graphics Card)" },
			{ "pcjr",     "PCjr" },
		},
		"svga"
	},
	{
		"dosbox_pure_cga",
		"CGA Mode", NULL,
		"The CGA variation that is being emulated.", NULL,
		DBP_OptionCat::Video,
		{
			{ "early_auto", "Early model, composite mode auto (default)" },
			{ "early_on",   "Early model, composite mode on" },
			{ "early_off",  "Early model, composite mode off" },
			{ "late_auto", "Late model, composite mode auto" },
			{ "late_on",   "Late model, composite mode on" },
			{ "late_off",  "Late model, composite mode off" },
		},
		"early_auto"
	},
	{
		"dosbox_pure_hercules",
		"Hercules Color Mode", NULL,
		"The color scheme for Hercules emulation.", NULL,
		DBP_OptionCat::Video,
		{
			{ "white", "Black & white (default)" },
			{ "amber", "Black & amber" },
			{ "green", "Black & green" },
		},
		"white"
	},
	{
		"dosbox_pure_svga",
		"SVGA Mode (restart required)", NULL,
		"The SVGA variation that is being emulated. Try changing this if you encounter graphical glitches.", NULL,
		DBP_OptionCat::Video,
		{
			{ "svga_s3",       "S3 Trio64 (default)" },
			{ "vesa_nolfb",    "S3 Trio64 no-line buffer hack (reduces flickering in some games)" },
			{ "vesa_oldvbe",   "S3 Trio64 VESA 1.3" },
			{ "svga_et3000",   "Tseng Labs ET3000" },
			{ "svga_et4000",   "Tseng Labs ET4000" },
			{ "svga_paradise", "Paradise PVGA1A" },
		},
		"svga_s3"
	},
	{
		"dosbox_pure_svgamem",
		"SVGA Memory (restart required)", NULL,
		"The amount of memory available to the emulated SVGA card.", NULL,
		DBP_OptionCat::Video,
		{
			{ "0",  "512KB" },
			{ "1", "1MB" },
			{ "2", "2MB (default)" },
			{ "3", "3MB" },
			{ "4", "4MB" },
			{ "8", "8MB (not always recognized)" },
		},
		"2"
	},
	{
		"dosbox_pure_voodoo",
		"3dfx Voodoo Emulation", NULL,
		"Enables certain games with support for the Voodoo 3D accelerator." "\n"
		"3dfx Voodoo Graphics SST-1/2 emulator by Aaron Giles and the MAME team (license: BSD-3-Clause)", NULL,
		DBP_OptionCat::Video,
		{
			{ "8mb", "Enabled - 8MB memory (default)" },
			{ "12mb", "Enabled - 12MB memory, Dual Texture" },
			{ "4mb", "Enabled - 4MB memory, Low Resolution Only" },
			{ "off", "Disabled" },
		},
		"8mb",
	},
	{
		"dosbox_pure_voodoo_perf",
		"3dfx Voodoo Performance", NULL,
		#ifndef DBP_STANDALONE
		"Options to tweak the behavior of the 3dfx Voodoo emulation." "\n"
		"Switching to OpenGL requires a restart." "\n"
		"If OpenGL is available, host-side 3D acceleration is used which can make 3D rendering much faster.\n"
		"Auto will use OpenGL if it is the active video driver in the frontend.", NULL,
		#else
		"Options to tweak the behavior of the 3dfx Voodoo emulation.", NULL,
		#endif
		DBP_OptionCat::Video,
		{
			#ifndef DBP_STANDALONE
			{ "auto", "Auto (default)" },
			{ "4", "Hardware OpenGL" },
			#else
			{ "auto", "Hardware OpenGL" },
			#endif
			{ "1", "Software Multi Threaded" },
			{ "3", "Software Multi Threaded, low quality" },
			{ "2", "Software Single Threaded, low quality" },
			{ "0", "Software Single Threaded" },
		},
		"auto",
	},
	{
		"dosbox_pure_voodoo_scale",
		"3dfx Voodoo OpenGL Scaling", NULL,
		"Increase the native resolution of the rendered image.", NULL,
		DBP_OptionCat::Video,
		{
			{ "1", "1x" }, { "2", "2x" }, { "3", "3x" }, { "4", "4x" }, { "5", "5x" }, { "6", "6x" }, { "7", "7x" }, { "8", "8x" },
		},
		"1",
	},
	{
		"dosbox_pure_voodoo_gamma",
		"3dfx Voodoo Gamma Correction", NULL,
		"Change brightness of rendered 3dfx output.", NULL,
		DBP_OptionCat::Video,
		{
			{ "-10", "-10" }, { "-9", "-9" }, { "-8", "-8" }, { "-7", "-7" }, { "-6", "-6" }, { "-5", "-5" }, { "-4", "-4" }, { "-3", "-3" }, { "-2", "-2" }, { "-1", "-1" },
			{ "0", "None" },
			{ "1", "+1" }, { "2", "+2" }, { "3", "+3" }, { "4", "+4" }, { "5", "+5" }, { "6", "+6" }, { "7", "+7" }, { "8", "+8" }, { "9", "+9" }, { "10", "+10" },
			{ "999", "Disable Gamma Correction" },
		},
		"-2",
	},
	#ifdef DBP_STANDALONE
	{
		"interface_scaling",
		"Scaling", NULL,
		"Choose how to scale the game display to the window/fullscreen resolution. Integer scaling will enforce all pixels to be the same size but may add a border.", NULL,
		DBP_OptionCat::Video,
		{
			{ "default", "Sharp Scaling (default)" },
			{ "nearest", "Simple Scaling (nearest neighbor)" },
			{ "bilinear", "Bilinear Scaling" },
			{ "integer", "Integer Scaling" },
		},
		"default"
	},
	{
		"interface_crtfilter",
		"CRT Filter", NULL,
		"Enable CRT filter effect on displayed screen (works best on high resolution displays and without integer scaling).", NULL,
		DBP_OptionCat::Video,
		{
			{ "false", "Off" },
			{ "1", "Only Scanlines" },
			{ "2", "TV style phosphors" },
			{ "3", "Aperture-grille phosphors" },
			{ "4", "Stretched VGA style phosphors" },
			{ "5", "VGA style phosphors" },
		},
		"false"
	},
	{
		"interface_crtscanline",
		"CRT Filter Scanline Intensity", NULL,
		NULL, NULL,
		DBP_OptionCat::Video,
		{
			{ "0", "No scanline gaps" },
			{ "1", "Weaker gaps" },
			{ "2", "Weak gaps" },
			{ "3", "Normal gaps" },
			{ "4", "Strong gaps" },
			{ "5", "Stronger gaps" },
			{ "8", "Strongest gaps" },
		},
		"2"
	},
	{
		"interface_crtblur",
		"CRT Filter Blur/Sharpness", NULL,
		NULL, NULL,
		DBP_OptionCat::Video,
		{
			{ "0", "Blurry" },
			{ "1", "Smooth" },
			{ "2", "Default" },
			{ "3", "Pixely" },
			{ "4", "Sharper" },
			{ "7", "Sharpest" },
		},
		"2"
	},
	{
		"interface_crtmask",
		"CRT Filter Phosphor Mask Strength", NULL,
		NULL, NULL,
		DBP_OptionCat::Video,
		{
			{ "0", "Disabled" },
			{ "1", "Weak" },
			{ "2", "Default" },
			{ "3", "Strong" },
			{ "4", "Very Strong" },
		},
		"2"
	},
	{
		"interface_crtcurvature",
		"CRT Filter Curvature", NULL,
		NULL, NULL,
		DBP_OptionCat::Video,
		{
			{ "0", "Disabled" },
			{ "1", "Weak" },
			{ "2", "Default" },
			{ "3", "Strong" },
			{ "4", "Very Strong" },
		},
		"2"
	},
	{
		"interface_crtcorner",
		"CRT Filter Rounded Corner", NULL,
		NULL, NULL,
		DBP_OptionCat::Video,
		{
			{ "0", "Disabled" },
			{ "1", "Weak" },
			{ "2", "Default" },
			{ "3", "Strong" },
			{ "4", "Very Strong" },
		},
		"2"
	},
	#endif
	{
		"dosbox_pure_aspect_correction",
		"Aspect Ratio Correction", NULL,
		"Adjust the aspect ratio to approximate what a CRT monitor would display (works best on high resolution displays and without integer scaling).", NULL,
		DBP_OptionCat::Video,
		{
			{ "false", "Off (default)" },
			{ "true", "On (single-scan)" },
			{ "doublescan", "On (double-scan when applicable)" },
			{ "padded", "Padded to 4:3 (single-scan)" },
			{ "padded-doublescan", "Padded to 4:3 (double-scan when applicable)" },
			#ifdef DBP_STANDALONE
			{ "fill", "Stretch the display to fill the window, ignoring any content aspect ratio" },
			#endif
		},
		"false"
	},
	{
		"dosbox_pure_overscan",
		"Overscan Border Size", NULL,
		"When enabled, show a border around the display. Some games use the color of the border to convey information." "\n\n", NULL, //end of Video section
		DBP_OptionCat::Video,
		{ { "0", "Off (default)" }, { "1", "Small" }, { "2", "Medium" }, { "3", "Large" } },
		"0"
	},

	// System
	{
		"dosbox_pure_memory_size",
		"Memory Size (restart required)", NULL,
		"The amount of (high) memory that the emulated machine has. You can also disable extended memory (EMS/XMS)." "\n"
		"Using more than the default is not recommended, due to incompatibility with certain games and applications.", NULL,
		DBP_OptionCat::System,
		{
			{ "none", "Disable extended memory (no EMS/XMS)" },
			{ "4",  "4 MB" },
			{ "8",  "8 MB" },
			{ "16", "16 MB (default)" },
			{ "24", "24 MB" },
			{ "32", "32 MB" },
			{ "48", "48 MB" },
			{ "64", "64 MB" },
			{ "96", "96 MB" },
			{ "128", "128 MB" },
			{ "224", "224 MB" },
			{ "256", "256 MB" },
			{ "512", "512 MB" },
			{ "1024", "1024 MB" },
		},
		"16"
	},
	{
		"dosbox_pure_modem",
		"Modem Type", NULL,
		"Type of emulated modem on COM1 for netplay. With the dial-up modem, one side needs to dial any number to connect.", NULL,
		DBP_OptionCat::System,
		{
			{ "null", "Null Modem (Direct Serial)" },
			{ "dial", "Dial-Up Modem (Hayes Standard)" },
		},
		"null"
	},
	{
		"dosbox_pure_cpu_type",
		"CPU Type (restart required)", NULL,
		"Emulated CPU type. Auto is the fastest choice." "\n"
			"Games that require specific CPU type selection:" "\n"
			"386 (prefetch): X-Men: Madness in The Murderworld, Terminator 1, Contra, Fifa International Soccer 1994" "\n"
			"486 (slow): Betrayal in Antara" "\n"
			"Pentium (slow): Fifa International Soccer 1994, Windows 95/Windows 3.x games" "\n\n", NULL, //end of System section
		DBP_OptionCat::System,
		{
			{ "auto", "Auto - Mixed feature set with maximum performance and compatibility" },
			{ "386", "386 - 386 instruction with fast memory access" },
			{ "386_slow", "386 (slow) - 386 instruction set with memory privilege checks" },
			{ "386_prefetch", "386 (prefetch) - With prefetch queue emulation (only on 'auto' and 'normal' core)" },
			{ "486_slow", "486 (slow) - 486 instruction set with memory privilege checks" },
			{ "pentium_slow", "Pentium (slow) - 586 instruction set with memory privilege checks" },
			#if C_MMX
			{ "pentium_mmx", "Pentium MMX (slow) - 586 instruction set with MMX extension" },
			#endif
		},
		"auto"
	},
	{
		"dosbox_pure_cpu_core",
		"Advanced > CPU Core", NULL,
		"Emulation method (DOSBox CPU core) used.", NULL,
		DBP_OptionCat::System,
		{
			#if defined(C_DYNAMIC_X86)
			{ "auto", "Auto - Real-mode games use normal, protected-mode games use dynamic" },
			{ "dynamic", "Dynamic - Dynamic recompilation (fast, using dynamic_x86 implementation)" },
			#elif defined(C_DYNREC)
			{ "auto", "Auto - Real-mode games use normal, protected-mode games use dynamic" },
			{ "dynamic", "Dynamic - Dynamic recompilation (fast, using dynrec implementation)" },
			#endif
			{ "normal", "Normal (interpreter)" },
			{ "simple", "Simple (interpreter optimized for old real-mode games)" },
		},
		#if defined(C_DYNAMIC_X86) || defined(C_DYNREC)
		"auto"
		#else
		"normal"
		#endif
	},
	{
		"dosbox_pure_bootos_ramdisk",
		"Advanced > OS Disk Modifications (restart required)", NULL,
		"When running an installed operating system, modifications to the C: drive will be made on the disk image by default." "\n"
		"Setting it to 'Discard' allows the content to be closed any time without worry of file system or registry corruption." "\n"
		"When using 'Save Difference Per Content' the disk image must never be modified again, otherwise existing differences become unusable.", NULL,
		DBP_OptionCat::System,
		{
			{ "false", "Keep (default)" },
			{ "true", "Discard" },
			{ "diff", "Save Difference Per Content" },
		},
		"false"
	},
	{
		"dosbox_pure_bootos_dfreespace",
		"Advanced > Free Space on D: in OS (restart required)", NULL,
		"Controls the amount of free space available on the D: drive when running an installed operating system." "\n"
		"If the total size of the D: drive (data + free space) exceeds 2 GB, it can't be used in earlier versions of Windows 95." "\n"
		"WARNING: Created save files are tied to this setting, so changing this will hide all existing D: drive changes.", NULL,
		DBP_OptionCat::System,
		{ { "1024", "1GB (default)" }, { "2048", "2GB" }, { "4096", "4GB" }, { "8192", "8GB" }, { "discard", "Discard Changes to D:" }, { "hide", "Disable D: Hard Disk (use only CD-ROM)" } },
		"1024"
	},
	{
		"dosbox_pure_bootos_forcenormal",
		"Advanced > Force Normal Core in OS", NULL,
		"The normal core can be more stable when running an installed operating system." "\n"
		"This can be toggled on and off to navigate around crashes." "\n\n", NULL, //end of System > Advanced section
		DBP_OptionCat::System,
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},

	// Audio
	#ifndef DBP_STANDALONE
	{
		"dosbox_pure_audiorate",
		"Audio Sample Rate (restart required)", NULL,
		"This should match the frontend audio output rate (Hz) setting.", NULL,
		DBP_OptionCat::Audio,
		{
			{ "48000", NULL },
			{ "44100", NULL },
			#ifdef _3DS
			{ "32730", NULL },
			#endif
			{ "32000", NULL },
			{ "22050", NULL },
			{ "16000", NULL },
			{ "11025", NULL },
			{  "8000", NULL },
			{ "49716", NULL }, //for perfect OPL emulation
		},
		DBP_DEFAULT_SAMPLERATE_STRING
	},
	#else
	{
		"interface_audiolatency",
		"Audio Latency", NULL,
		"If set too low, audio dropouts can occur. Value is for internal processing and the actually perceived latency will be higher.", NULL,
		DBP_OptionCat::Audio,
		{
			{ "10", "10 ms" }, { "15", "15 ms" }, { "20", "20 ms" }, { "25", "25 ms" }, { "30", "30 ms" }, { "35", "35 ms" }, { "40", "40 ms" }, { "45", "45 ms" }, { "50", "50 ms" },
			{ "55", "55 ms" }, { "60", "60 ms" }, { "65", "65 ms" }, { "70", "70 ms" }, { "75", "75 ms" }, { "80", "80 ms" }, { "85", "85 ms" }, { "90", "90 ms" }, { "95", "95 ms" }, { "100", "100 ms" },
		},
		"25"
	},
	#endif
	{
		"dosbox_pure_sblaster_conf",
		"SoundBlaster Settings", NULL,
		"Set the address, interrupt, low 8-bit and high 16-bit DMA.", NULL,
		DBP_OptionCat::Audio,
		{
			// Some common (and less common) port, irq, low and high dma settings (based on a very scientific web search)
			{ "A220 I7 D1 H5",  "Port 0x220, IRQ 7, 8-Bit DMA 1, 16-bit DMA 5"  },
			{ "A220 I5 D1 H5",  "Port 0x220, IRQ 5, 8-Bit DMA 1, 16-bit DMA 5"  },
			{ "A240 I7 D1 H5",  "Port 0x240, IRQ 7, 8-Bit DMA 1, 16-bit DMA 5"  },
			{ "A240 I7 D3 H7",  "Port 0x240, IRQ 7, 8-Bit DMA 3, 16-bit DMA 7"  },
			{ "A240 I2 D3 H7",  "Port 0x240, IRQ 2, 8-Bit DMA 3, 16-bit DMA 7"  },
			{ "A240 I5 D3 H5",  "Port 0x240, IRQ 5, 8-Bit DMA 3, 16-bit DMA 5"  },
			{ "A240 I5 D1 H5",  "Port 0x240, IRQ 5, 8-Bit DMA 1, 16-bit DMA 5"  },
			{ "A240 I10 D3 H7", "Port 0x240, IRQ 10, 8-Bit DMA 3, 16-bit DMA 7" },
			{ "A280 I10 D0 H6", "Port 0x280, IRQ 10, 8-Bit DMA 0, 16-bit DMA 6" },
			{ "A280 I5 D1 H5",  "Port 0x280, IRQ 5, 8-Bit DMA 1, 16-bit DMA 5"  },
		},
		"A220 I7 D1 H5"
	},
	{
		"dosbox_pure_midi",
		"MIDI Output", NULL,
		"Select the .SF2 SoundFont file, .ROM file or interface used for MIDI output." "\n"
		#ifndef DBP_STANDALONE
		"To add SoundFonts or ROM files, copy them into the 'system' directory of the frontend." "\n"
		"To use the frontend MIDI driver, make sure it's set up correctly."
		#else
		"To add SoundFonts or ROM files, copy them into the 'system' directory of DOSBox Pure." "\n"
		#endif
		"\n\n", NULL, //end of Audio section
		DBP_OptionCat::Audio,
		{
			// dynamically filled in retro_init
		},
		"disabled"
	},
	{
		"dosbox_pure_sblaster_type",
		"Advanced > SoundBlaster Type", NULL,
		"Type of emulated SoundBlaster card.", NULL,
		DBP_OptionCat::Audio,
		{
			{ "sb16", "SoundBlaster 16 (default)" },
			{ "sbpro2", "SoundBlaster Pro 2" },
			{ "sbpro1", "SoundBlaster Pro" },
			{ "sb2", "SoundBlaster 2.0" },
			{ "sb1", "SoundBlaster 1.0" },
			{ "gb", "GameBlaster" },
			{ "none", "none" },
		},
		"sb16"
	},
	{
		"dosbox_pure_sblaster_adlib_mode",
		"Advanced > SoundBlaster Adlib/FM Mode", NULL,
		"The SoundBlaster emulated FM synth mode. All modes are Adlib compatible except CMS.", NULL,
		DBP_OptionCat::Audio,
		{
			{ "auto",     "Auto (select based on the SoundBlaster type) (default)" },
			{ "cms",      "CMS (Creative Music System / GameBlaster)" },
			{ "opl2",     "OPL-2 (AdLib / OPL-2 / Yamaha 3812)" },
			{ "dualopl2", "Dual OPL-2 (Dual OPL-2 used by SoundBlaster Pro 1.0 for stereo sound)" },
			{ "opl3",     "OPL-3 (AdLib / OPL-3 / Yamaha YMF262)" },
			{ "opl3gold", "OPL-3 Gold (AdLib Gold / OPL-3 / Yamaha YMF262)" },
			{ "none",     "Disabled" },
		},
		"auto"
	},
	{
		"dosbox_pure_sblaster_adlib_emu",
		"Advanced > SoundBlaster Adlib Provider", NULL,
		"Provider for the Adlib emulation. Default has good quality and low performance requirements.", NULL,
		DBP_OptionCat::Audio,
		{
			{ "default", "Default" },
			{ "nuked", "High quality Nuked OPL3" },
		},
		"default"
	},
	{
		"dosbox_pure_gus",
		"Advanced > Enable Gravis Ultrasound (restart required)", NULL,
		"Enable Gravis Ultrasound emulation. Settings are fixed at port 0x240, IRQ 5, DMA 3." "\n"
		"If the ULTRADIR variable needs to be different than the default 'C:\\ULTRASND' you need to issue 'SET ULTRADIR=...' in the command line or in a batch file.", NULL,
		DBP_OptionCat::Audio,
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},
	{
		"dosbox_pure_tandysound",
		"Advanced > Enable Tandy Sound Device (restart required)", NULL,
		"Enable Tandy Sound Device emulation even when running without Tandy Graphics Adapter emulation.", NULL,
		DBP_OptionCat::Audio,
		{ { "auto", "Off (default)" }, { "on", "On" } },
		"auto"
	},
	{
		"dosbox_pure_swapstereo",
		"Advanced > Swap Stereo Channels", NULL,
		"Swap the left and the right audio channel." "\n\n", NULL, //end of Audio > Advanced section
		DBP_OptionCat::Audio,
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},

	{ NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL }
};
