# DOSBox Pure

DOSBox Pure is a fork of [DOSBox](https://www.dosbox.com/), an emulator for DOS games,
built for RetroArch/Libretro aiming for simplicity and ease of use.

![Logo](images/logo.png)

* [Download](#download)
* [Install](#install)
* [Features](#features)
    * [Load games from ZIP](#load-games-from-zip)
    * [Store modifications in separate save files](#store-modifications-in-separate-save-files)
    * [Mount disk images from inside ZIP files](#mount-disk-images-from-inside-zip-files)
    * [Start menu with auto start](#start-menu-with-auto-start)
    * [Automated controller mappings](#automated-controller-mappings)
    * [Mouse emulation](#mouse-emulation)
    * [Keyboard emulation](#keyboard-emulation)
    * [Joystick emulation](#joystick-emulation)
    * [On-screen keyboard](#on-screen-keyboard)
    * [MIDI playback with SoundFonts](#midi-playback-with-soundfonts)
    * [Cheats support](#cheats-support)
    * [Save states](#save-states)
    * [Rewind support](#rewind-support)
    * [Loading M3U8 files](#loading-m3u8-files)
* [Tips](#tips)
    * [Playing with keyboard and mouse](#playing-with-keyboard-and-mouse)
    * [ZIP files can be renamed to DOSZ](#zip-files-can-be-renamed-to-dosz)
    * [Force opening the start menu](#force-opening-the-start-menu)
    * [Mount ZIP as A or D drive](#mount-zip-as-a-or-d-drive)
    * [Change disk label with label command](#change-disk-label-with-label-command)
    * [Keyboard layout defaults to US](#keyboard-layout-defaults-to-us)
* [Features not yet implemented](#features-not-yet-implemented)
    * [Load dosbox.conf](#load-dosboxconf)
    * [Store ZIP seek index into save file](#store-zip-seek-index-into-save-file)
* [Unsupported features](#unsupported-features)
* [Building](#building)
    * [Windows](#windows)
    * [Linux](#linux)
* [License](#license)

## Download
You can find a binary download for Windows, Linux and Raspberry Pi (Arm7) under the [Releases page](https://github.com/schellingb/dosbox-pure/releases/latest).

## Install
Locate the directory where your libretro frontend stores its cores.  
In RetroArch, you can open the main menu and go to `Configuration File` -> `Load Configuration` -> `Parent Directory` -> `cores` to find it.  
The release ZIP contains 2 files, `dosbox_pure_libretro.dll` (or `.so` for Linux) and `dosbox_pure_libretro.info`.  
The `.dll` or `.so` file is to be extracted into the cores directory. The `.info` file goes into the `info` directory or into `cores` if it doesn't exist.

## Features

### Load games from ZIP
DOSBox Pure can load games directly from ZIP files without the need to extract them.

### Store modifications in separate save files
Changes made to a loaded ZIP file will be stored as a separate ZIP file into the libretro saves directory.  
If a game is loaded directly without using a ZIP file the saves directory is not used.

### Mount disc images from inside ZIP files
CD images (ISO or CUE) and floppy disk images (IMG/IMA/VHD) can be mounted directly from inside ZIP files.  
The system will automatically mount the first found disk image as the A: or D: drive.  
Additional disks can be loaded or swapped by using the `Disc Control` menu in RetroArch.  
The start menu also offers the option to mount or unmount an image.

### Start menu with auto start
![Start Menu](images/startmenu.png)

This is the first screen that appears when loading a game. It offers a gamepad controllable list
of all executable files for the loaded game. By pressing right an item can be selected as the default
which will skip the menu on the next launch.  
By pressing right multiple times, a number of frames can be specified that will automatically be skipped
on start. This can be used to skip over loading screens or start-up sequences.  
If there is only a single executable, the menu will not show and directly run the file.  
To force the menu to be shown, hold shift on the keyboard or L2 or R2 on
the gamepad while selecting `Restart` in the core menu.

### Automated controller mappings
When a game is loaded, DOSBox Pure will try to detect the game and apply a controller mapping.  
To see the applied mapping, check the `Port 1 Controls` screen in the RetroArch menu.
It will show `Detected Automatic Key Mapping: <GAMENAME>`. Additionally you can set the core option
`Input > Advanced > Automatic Game Pad Mappings` to `Enable with notification on game detection`.

### Mouse emulation
Under the `Controls` screen in the RetroArch menu there are 2 mouse emulation modes available by
switching the `Device Type` setting of any port with left/right. There is `Mouse with Left Analog Stick` and
`Mouse with Right Analog Stick`.  
When choosing left stick, the face buttons (B/A) will be used as left/right mouse buttons.  
For the right stick the shoulder buttons L/R will be used as left/right mouse buttons.  
The X button is the middle mouse button and L2/R2 can be used to speed up or slow down mouse movement.  
There is also the core option `Input > Mouse Sensitivity` to increase/decrease mouse movement speed.

### Keyboard emulation
For games that don't have automated controller mappings or are not detected successfully,
by default the option `Input > Bind Unused Buttons` will assign all unused buttons on the game pad with
some default key.  
If the `Device Type` under the `Controls` screen in the RetroArch menu of any port is set to
`Generic Keyboard Bindings` all buttons will be assigned with a keyboard key.
Additionally it can be set to `Custom Keyboard Bindings` which will allow fully customizable mappings.

### Joystick emulation
There are multiple DOS era joysticks available as mappings under the `Controls` screen in the RetroArch menu.  
`Gravis GamePad (1 D-Pad, 4 Buttons)`, `Basic joystick (2 Axes, 2 Buttons)`,
`ThrustMaster Flight Stick (3 axes, 4 buttons, 1 hat)` and `Control both DOS joysticks (4 axes, 4 buttons)`.  
These can be assigned to any port and the button layout can be remapped as with any other device type.

### On-screen keyboard
![On-Screen Keyboard](images/onscreenkeyboard.png)

By pressing L3 on the gamepad (usually by pushing in the left analog stick) the on-screen keyboard will open.
The cursor can be controlled with the controller (or mouse or keyboard) and L2/R2 will speed up or slow down
the move speed.  
If the cursor is moved above the middle of the screen the keyboard will move to the top.
The button can be remapped in the controls menu and there is also a core options to disable it entirely.

### MIDI playback with SoundFonts
If DOSBox Pure finds one or more `.SF2` sound font file in the `system` directory of the frontend, one of them
can be selected via the `Audio > MIDI SoundFont` core option.  
This sound font will then be used to play General Midi and Sound Canvas music.

### Cheats support
DOSBox Pure exposes its memory for cheats in the libretro frontend.  
For details how to use it and how to find new cheats while playing the game,
check the [documentation on the RetroArch website](https://docs.libretro.com/guides/cheat-codes/).  
This can also be used to add controller rumble support to DOS games.

### Save states
The DOSBox Pure core fully supports libretro save states.  
Make sure to test it in each game before using it. Complex late era DOS games might have problems.  
Be aware that states saved with different video or cpu settings are not loadable.  
Save states might not be compatible across different versions of DOSBox Pure.

### Rewind support
Using the core option `Save States Support`, rewinding can be enabled.  
Keep in mind that rewind support comes at a high performance cost.

### Loading M3U8 files
If the core gets loaded with a `.m3u8` file, all files listed in it will be added to the
disc swap menu. The first image will automatically get mounted as A: or D: drive depending
if it is a CD or floppy disk image.

## Tips

### Playing with keyboard and mouse
To play not with a gamepad but with keyboard and mouse, be sure to use the 'Game Focus'
mode available in RetroArch. By default you can toggle game focus by pressing the
scroll lock key. While game focus is active, the hotkeys are disabled and keyboard will
not cause retro pad button presses (which could cause multiple keys to be pressed at once).

### ZIP files can be renamed to DOSZ
If your libretro frontend wants to load the content of `.ZIP` files instead of sending it to
DOSBox Pure to load, the files can be renamed from `.ZIP` to `.DOSZ`.  
This is especially useful for CD images in ZIP format which RetroArch refuses to append through its
`Disc Control` menu. Using an M3U8 file also avoids this problem.

### Force opening the start menu
If you have assigned an auto start item in the start menu but want to go back to it,
hold shift on the keyboard or L2 or R2 on the gamepad while selecting `Restart` in the core menu.

### Mount ZIP as A or D drive
If you have a ZIP file you want to load as a fake floppy disk or fake CD-ROM, there are multiple options.  
The easiest is to rename the file from `.ZIP` to `.D.ZIP` (to use the D: drive).  
You can also edit the RetroArch `.LPL` playlist file to add a `#D` after the file like `game.zip#D`.  
A third option is available inside DOSBox Pure with a new remount command that can be called with
`REMOUNT C: D:` to remount the C: drive to D:. This can for example be used in a startup batch file.

### Change disk label with label command
DOSBox Pure by default uses the first word of the ZIP file as the label of the mounted disk.  
Some games require a specific label on a floppy or a CD-ROM so DOSBox Pure offers a new command
to change the label of a mounted disk. For example, `LABEL C: HELLO` changes the label of the C: drive.  
This label is not saved anywhere and needs to be reapplied on every launch so it's best to add the
command in a startup batch file.
You can run the `MOUNT` command to check all mounted disks and their disk label.  

### Keyboard layout defaults to US
The keyboard layout defaults to the US Layout (QWERTY).  
If you need a different layout, you can change the core option `Input > Advanced > Keyboard Layout`.

## Features not yet implemented

### Load dosbox.conf
It would be nice to be able to load a `dosbox.conf` file if it exists in the loaded
game directory (for example inside the ZIP file). Ideally this would then hide all the options that
have been overwritten by that `.conf` file in the core options list.

### Store ZIP seek index into save file
When a DOS games opens a large file and wants to read some data from near the end of the file,
DOSBox Pure needs to decompress the entire file to do that. This can be most noticeable when mounting
CD-ROM images from inside ZIP files.  
Afterwards there is an index buffer which will be used to decompress random locations of the file
and file access will be much faster.  
This index buffer should be stored into the game save file to avoid having to slowly rebuild it
every time the same game is launched.

## Unsupported features
For now, serial port and IPX emulation from base DOSBox have been removed.

## Building
DOSBox Pure has no dependency requirements besides a C++ compiler and the standard library.

### Windows
Open `dosbox_pure_libretro.sln` in Visual Studio (all versions from 2012 and up are supported) and press
build to build the `dosbox_pure_libretro.dll` core file.

### Linux
To build under linux, make sure GCC 9 (package g++-9) and GNU Make (package make) are installed.  
Change into the project directory then run `make -j4` to build it.  
The makefile explicitly is set to use g++-9 because there are codegen bugs in GCC 10.2 that affect
any version of DOSBox on certain platforms.  
You can edit the simple Makefile to set a different compiler or add hardware specific compiler flags.

## License
DOSBox Pure, as well as original DOSBox, is available under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
