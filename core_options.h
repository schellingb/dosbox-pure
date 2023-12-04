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

static retro_core_option_v2_category option_cats[] =
{
	{ "Emulation",   "Emulation",   "Core specific settings (latency, save states, start menu)." },
	{ "Input",       "Input",       "Keyboard, mouse and joystick settings." },
	{ "Performance", "Performance", "Adjust the performance of the emulated CPU." },
	{ "Video",       "Video",       "Settings for the emulated graphics card and aspect ratio." },
	{ "System",      "System",      "Other system settings for the emulated RAM and CPU." },
	{ "Audio",       "Audio",       "MIDI, SoundBlaster and other audio settings." },
	{ NULL, NULL, NULL }
};

static retro_core_option_v2_definition option_defs[] =
{
	{
		"dosbox_pure_advanced",
		"Show Advanced Options", NULL,
		"Close and re-open the menu to refresh this options page.", NULL,
		NULL,
		{ { "false", "Off" }, { "true", "On" } },
		"false"
	},
	// Emulation
	{
		"dosbox_pure_force60fps",
		"Force 60 FPS Output", NULL,
		"Enable this to force output at 60FPS. Use this if you encounter screen tearing or vsync issues.", NULL,
		"Emulation",
		{
			{ "false", "Off" },
			{ "true", "On" },
		},
		"false"
	},
	{
		"dosbox_pure_perfstats",
		"Show Performance Statistics", NULL,
		"Enable this to show statistics about performance and framerate and check if emulation runs at full speed.", NULL,
		"Emulation",
		{
			{ "none",     "Disabled" },
			{ "simple",   "Simple" },
			{ "detailed", "Detailed information" },
		},
		"none"
	},
	{
		"dosbox_pure_savestate",
		"Save States Support", NULL,
		"Make sure to test it in each game before using it. Complex late era DOS games might have problems." "\n"
		"Be aware that states saved with different video, CPU or memory settings are not loadable." "\n"
		"Rewind support comes at a high performance cost and needs at least 40MB of rewind buffer.", NULL,
		"Emulation",
		{
			{ "on",       "Enable save states" },
			{ "rewind",   "Enable save states with rewind" },
			{ "disabled", "Disabled" },
		},
		"on"
	},
	{
		"dosbox_pure_conf",
		"Loading of dosbox.conf", NULL,
		"DOSBox Pure is meant to be configured via core options but optionally supports loading of legacy .conf files." "\n\n", NULL, //end of Emulation section
		"Emulation",
		{
			{ "false", "Disabled conf support (default)" },
			{ "inside", "Try 'dosbox.conf' in the loaded content (ZIP or folder)" },
			{ "outside", "Try '.conf' with same name as loaded content (next to ZIP or folder)" },
		},
		"false"
	},
	{
		"dosbox_pure_strict_mode",
		"Use Strict Mode", NULL,
		"Disable the command line, running installed operating systems and using .BAT/.COM/.EXE/DOS.YML files from the save game.", NULL,
		"Emulation",
		{
			{ "false", "Off" },
			{ "true", "On" },
		},
		"false"
	},
	{
		"dosbox_pure_menu_time",
		"Advanced > Start Menu", NULL,
		"Set the behavior of the start menu before and after launching a game." "\n"
		"You can also force it to open by holding shift or L2/R2 when selecting 'Restart'.", NULL,
		"Emulation",
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
		"dosbox_pure_latency",
		"Advanced > Input Latency", NULL,
		"By default the core operates in a high performance mode with good input latency." "\n"
		"There is a special mode available which minimizes input latency further requiring manual tweaking.", NULL,
		"Emulation",
		{
			{ "default", "Default" },
			{ "low", "Lowest latency - See CPU usage setting below!" },
			{ "variable", "Irregular latency - Might improve performance on low-end devices" },
		},
		"default"
	},
	{
		"dosbox_pure_auto_target",
		"Advanced > Low latency CPU usage", NULL,
		"In low latency mode when emulating DOS as fast as possible, how much time per frame should be used by the emulation." "\n"
		"If the video is stuttering, lower this or improve render performance in the frontend (for example by disabling vsync or video processing)." "\n"
		"Use the performance statistics to easily find the maximum that still hits the emulated target framerate." "\n\n", NULL, //end of Emulation > Advanced section
		"Emulation",
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
		},
		"0.9",
	},

	// Input
	{
		"dosbox_pure_on_screen_keyboard",
		"Enable On Screen Keyboard", NULL,
		"Enable the On Screen Keyboard feature which can be activated with the L3 button on the controller.", NULL,
		"Input",
		{ { "true", "On" }, { "false", "Off" } },
		"true"
	},
	{
		"dosbox_pure_mouse_input",
		"Mouse Input Mode", NULL,
		"You can disable input handling from a mouse or a touchscreen (emulated mouse through joypad will still work)." "\n"
		"In touchpad mode use drag to move, tap to click, two finger tap to right-click and press-and-hold to drag", NULL,
		"Input",
		{
			{ "true", "Virtual mouse (default)" },
			{ "direct", "Direct controlled mouse (not supported by all games)" },
			{ "pad", "Touchpad mode (see description, best for touch screens)" },
			{ "false", "Off (ignore mouse inputs)" },
		},
		"true"
	},
	{
		"dosbox_pure_mouse_wheel",
		"Bind Mouse Wheel To Key", NULL,
		"Bind mouse wheel up and down to two keyboard keys to be able to use it in DOS games.", NULL,
		"Input",
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
		"Input",
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
		"Advanced > Horizontal Mouse Sensitivity.", NULL,
		"Experiment with this value if the mouse is too fast/slow when moving left/right.", NULL,
		"Input",
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
		"dosbox_pure_auto_mapping",
		"Advanced > Automatic Game Pad Mappings", NULL,
		"DOSBox Pure can automatically apply a gamepad control mapping scheme when it detects a game." "\n"
		"These button mappings are provided by the Keyb2Joypad Project (by Jemy Murphy and bigjim).", NULL,
		"Input",
		{ { "true", "On (default)" }, { "notify", "Enable with notification on game detection" }, { "false", "Off" } },
		"true"
	},
	{
		"dosbox_pure_keyboard_layout",
		"Advanced > Keyboard Layout", NULL,
		"Select the keyboard layout (will not change the On Screen Keyboard).", NULL,
		"Input",
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
		"dosbox_pure_menu_transparency",
		"Advanced > Menu Transparency", NULL,
		"Set the transparency level of the On Screen Keyboard and the Gamepad Mapper.", NULL,
		"Input",
		{
			{ "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" },
		},
		"70"
	},
	{
		"dosbox_pure_joystick_analog_deadzone",
		"Advanced > Joystick Analog Deadzone", NULL,
		"Set the deadzone of the joystick analog sticks. May be used to eliminate drift caused by poorly calibrated joystick hardware.", NULL,
		"Input",
		{
			{ "0",  "0%" }, { "5",  "5%" }, { "10", "10%" }, { "15", "15%" }, { "20", "20%" }, { "25", "25%" }, { "30", "30%" }, { "35", "35%" }, { "40", "40%" },
		},
		"15"
	},
	{
		"dosbox_pure_joystick_timed",
		"Advanced > Enable Joystick Timed Intervals", NULL,
		"Enable timed intervals for joystick axes. Experiment with this option if your joystick drifts." "\n\n", NULL, //end of Input > Advanced section
		"Input",
		{ { "true", "On (default)" }, { "false", "Off" } },
		"true"
	},

	// Performance
	{
		"dosbox_pure_cycles",
		"Emulated Performance", NULL,
		"The raw performance that DOSBox will try to emulate." "\n\n", NULL, //end of Performance section
		"Performance",
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
		"dosbox_pure_cycles_scale",
		"Detailed > Performance Scale", NULL,
		"Fine tune the emulated performance for specific needs." "\n\n", NULL, //end of Performance > Detailed section
		"Performance",
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
		"Performance",
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
		},
		"1.0",
	},

	// Video
	{
		"dosbox_pure_machine",
		"Emulated Graphics Chip (restart required)", NULL,
		"The type of graphics chip that DOSBox will emulate.", NULL,
		"Video",
		{
			{ "svga",     "SVGA (Super Video Graphics Array) (default)" },
			{ "vga",      "VGA (Video Graphics Array)" },
			{ "ega",      "EGA (Enhanced Graphics Adapter" },
			{ "cga",      "CGA (Color Graphics Adapter)" },
			{ "tandy",    "Tandy (Tandy Graphics Adapter" },
			{ "hercules", "Hercules (Hercules Graphics Card)" },
			{ "pcjr",     "PCjr" },
		},
		"svga"
	},
	{
		"dosbox_pure_cga",
		"CGA Mode", NULL,
		"The CGA variation that is being emulated.", NULL,
		"Video",
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
		"Video",
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
		"Video",
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
		"Video",
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
		"Video",
		{
			{ "12mb", "Enabled - 12MB memory (default)" },
			{ "4mb", "Enabled - 4MB memory" },
			{ "off", "Disabled" },
		},
		"12mb",
	},
	{
		"dosbox_pure_voodoo_perf",
		"3dfx Voodoo Performance Settings", NULL,
		"Options to tweak the behavior of the 3dfx Voodoo emulation.", NULL,
		"Video",
		{
			{ "1", "Multi-threading (default)" },
			{ "3", "Multi-threading, low quality" },
			{ "2", "Low quality" },
			{ "0", "None" },
		},
		"1",
	},
	{
		"dosbox_pure_aspect_correction",
		"Aspect Ratio Correction", NULL,
		"When enabled, the core's aspect ratio is set to what a CRT monitor would display.", NULL,
		"Video",
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},
	{
		"dosbox_pure_overscan",
		"Overscan Border Size", NULL,
		"When enabled, show a border around the display. Some games use the color of the border to convey information." "\n\n", NULL, //end of Video section
		"Video",
		{ { "0", "Off (default)" }, { "1", "Small" }, { "2", "Medium" }, { "3", "Large" } },
		"0"
	},

	// System
	{
		"dosbox_pure_memory_size",
		"Memory Size (restart required)", NULL,
		"The amount of (high) memory that the emulated machine has. You can also disable extended memory (EMS/XMS)." "\n"
		"Using more than the default is not recommended, due to incompatibility with certain games and applications.", NULL,
		"System",
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
		"System",
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
		"System",
		{
			{ "auto", "Auto - Mixed feature set with maximum performance and compatibility" },
			{ "386", "386 - 386 instruction with fast memory access" },
			{ "386_slow", "386 (slow) - 386 instruction set with memory privilege checks" },
			{ "386_prefetch", "386 (prefetch) - With prefetch queue emulation (only on 'auto' and 'normal' core)" },
			{ "486_slow", "486 (slow) - 486 instruction set with memory privilege checks" },
			{ "pentium_slow", "Pentium (slow) - 586 instruction set with memory privilege checks" },
		},
		"auto"
	},
	{
		"dosbox_pure_cpu_core",
		"Advanced > CPU Core", NULL,
		"Emulation method (DOSBox CPU core) used.", NULL,
		"System",
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
		"System",
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
		"System",
		{ { "1024", "1GB (default)" }, { "2048", "2GB" }, { "4096", "4GB" }, { "8192", "8GB" } },
		"1024"
	},
	{
		"dosbox_pure_bootos_forcenormal",
		"Advanced > Force Normal Core in OS", NULL,
		"The normal core can be more stable when running an installed operating system." "\n"
		"This can be toggled on and off to navigate around crashes." "\n\n", NULL, //end of System > Advanced section
		"System",
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},

	// Audio
	{
		"dosbox_pure_audiorate",
		"Audio Sample Rate (restart required)", NULL,
		"This should match the frontend audio output rate (Hz) setting.", NULL,
		"Audio",
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
	{
		"dosbox_pure_sblaster_conf",
		"SoundBlaster Settings", NULL,
		"Set the address, interrupt, low 8-bit and high 16-bit DMA.", NULL,
		"Audio",
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
		"To add SoundFonts or ROM files, copy them into the 'system' directory of the frontend." "\n"
		"To use the frontend MIDI driver, make sure it's set up correctly." "\n\n", NULL, //end of Audio section
		"Audio",
		{
			// dynamically filled in retro_init
		},
		"none"
	},
	{
		"dosbox_pure_sblaster_type",
		"Advanced > SoundBlaster Type", NULL,
		"Type of emulated SoundBlaster card.", NULL,
		"Audio",
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
		"Audio",
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
		"Audio",
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
		"Audio",
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},
	{
		"dosbox_pure_swapstereo",
		"Advanced > Swap Stereo Channels", NULL,
		"Swap the left and the right audio channel." "\n\n", NULL, //end of Audio > Advanced section
		"Audio",
		{ { "false", "Off (default)" }, { "true", "On" } },
		"false"
	},

	{ NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL }
};
