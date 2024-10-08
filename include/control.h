/*
 *  Copyright (C) 2002-2021  The DOSBox Team
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


#ifndef DOSBOX_CONTROL_H
#define DOSBOX_CONTROL_H

#ifdef _MSC_VER
#pragma warning ( disable : 4786 )
#pragma warning ( disable : 4290 )
#endif

#ifndef DOSBOX_PROGRAMS_H
#include "programs.h"
#endif
#ifndef DOSBOX_SETUP_H
#include "setup.h"
#endif

#ifndef CH_LIST
#define CH_LIST
#include <list>
#endif

#ifndef CH_VECTOR
#define CH_VECTOR
#include <vector>
#endif

#ifndef CH_STRING
#define CH_STRING
#include <string>
#endif




class Config{
public:
#ifdef C_DBP_NATIVE_CONFIGFILE
	CommandLine * cmdline;
#endif
private:
	std::list<Section*> sectionlist;
	typedef std::list<Section*>::iterator it;
	typedef std::list<Section*>::reverse_iterator reverse_it;
	typedef std::list<Section*>::const_iterator const_it;
	typedef std::list<Section*>::const_reverse_iterator const_reverse_it;
	void (* _start_function)(void);
	bool secure_mode; //Sandbox mode
public:
	bool initialised;
#ifdef C_DBP_NATIVE_CONFIGFILE
	std::vector<std::string> startup_params;
	std::vector<std::string> configfiles;
	Config(CommandLine * cmd):cmdline(cmd),secure_mode(false) {
		startup_params.push_back(cmdline->GetFileName());
		cmdline->FillVector(startup_params);
		initialised=false;
	}
#else
	friend class CONFIG;
	Config():secure_mode(false),initialised(false) {}
#endif
	~Config();

	Section_line * AddSection_line(char const * const _name,void (*_initfunction)(Section*));
	Section_prop * AddSection_prop(char const * const _name,void (*_initfunction)(Section*),bool canchange=false);
	
	Section* GetSection(int index);
#ifdef C_DBP_LIBRETRO
	Section* GetSection(char const * const _sectionname) const;
	Property* GetProp(char const * const _sectionname, char const * const _property) const;
#endif
	Section* GetSection(std::string const&_sectionname) const;
	Section* GetSectionFromProperty(char const * const prop) const;

	void SetStartUp(void (*_function)(void));
	void Init();
	void ShutDown();
	void StartUp();
	bool PrintConfig(char const * const configfilename) const;
	bool ParseConfigFile(char const * const configfilename);
	void ParseEnv(char ** envp);
	bool SecureMode() const { return secure_mode; }
	void SwitchToSecureMode() { secure_mode = true; }//can't be undone
};

#endif
