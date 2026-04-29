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

struct DBP_WheelItem { Bit8u port, key_count, k[4]; };
static std::vector<DBP_WheelItem> dbp_wheelitems;
static std::vector<Bit8u> dbp_custom_mapping;
static Bit16s dbp_bind_mousewheel, dbp_yml_mousewheel;
static bool dbp_analog_buttons;
static char dbp_map_osd, dbp_mouse_input, dbp_auto_mapping_mode;
static const Bit8u* dbp_auto_mapping;
static const char *dbp_auto_mapping_names, *dbp_auto_mapping_title;
static bool dbp_yml_directmouse, dbp_yml_mapping;
static float dbp_yml_mousespeed = 1, dbp_yml_mousexfactor = 1, dbp_yml_padmousespeed = 1;
static float dbp_mouse_speed = 1, dbp_mouse_speed_x = 1, dbp_joymouse_speed = .0003f;

struct DBP_PadMapping
{
	enum { DBP_PADMAP_MAXSIZE_PORT = (1 + (16 * (1 + 4)) + (4 * (1 + 8))), WHEEL_ID = 20 };
	enum EPreset : Bit8u { PRESET_NONE, PRESET_AUTOMAPPED, PRESET_GENERICKEYBOARD, PRESET_MOUSE_LEFT_ANALOG, PRESET_MOUSE_RIGHT_ANALOG, PRESET_GRAVIS_GAMEPAD, PRESET_BASIC_JOYSTICK_1, PRESET_BASIC_JOYSTICK_2, PRESET_THRUSTMASTER_FLIGHTSTICK, PRESET_BOTH_DOS_JOYSTICKS, PRESET_CUSTOM };
	enum EPortMode : Bit8u { MODE_DISABLED, MODE_MAPPER, MODE_PRESET_AUTOMAPPED, MODE_PRESET_GENERICKEYBOARD, MODE_PRESET_LAST = MODE_PRESET_AUTOMAPPED + (PRESET_CUSTOM - PRESET_AUTOMAPPED) - 1, MODE_KEYBOARD, MODE_KEYBOARD_MOUSE1, MODE_KEYBOARD_MOUSE2 };

	INLINE static EPreset DefaultPreset(Bit8u port) { return ((port || !dbp_auto_mapping) ? PRESET_GENERICKEYBOARD : PRESET_AUTOMAPPED); }
	INLINE static bool IsCustomized(Bit8u port) { return (CalcPortMode(port) == MODE_MAPPER && GetPreset(port, DefaultPreset(port)) == PRESET_CUSTOM); }
	INLINE static const char* GetPortPresetName(Bit8u port) { return GetPresetName(GetPreset(port)); }
	INLINE static void FillGenericKeys(Bit8u port) { Apply(port, PresetBinds(PRESET_GENERICKEYBOARD, port), true, true); }
	INLINE static void SetPreset(Bit8u port, EPreset preset) { ClearBinds(port); Apply(port, PresetBinds(preset, port), true); }
	INLINE static const char* GetKeyAutoMapButtonLabel(Bit8u key) { return FindAutoMapButtonLabel(1, &key); }
	INLINE static const char* GetWheelAutoMapButtonLabel(const DBP_WheelItem& wheel_item) { return FindAutoMapButtonLabel(wheel_item.key_count, wheel_item.k); }

	static Bit8u CalcPortMode(Bit8u port)
	{
		if (Bit8u m = dbp_port_mode[port]) return m;
		for (DBP_InputBind& b : dbp_input_binds) if (b.evt == DBPET_SHIFTPORT && b.meta == port) return MODE_MAPPER;
		return MODE_DISABLED;
	}

	static void Load()
	{
		DOS_File *padmap = nullptr;
		if (!Drives['C'-'A'] || !Drives['C'-'A']->FileOpen(&padmap, (char*)"PADMAP.DBP", OPEN_READ))
			return;

		Bit8u version; Bit16u version_length = sizeof(version), padmap_length; Bit32u file_length, seek_zero;
		padmap->AddRef();
		padmap->Seek(&(file_length = 0), DOS_SEEK_END);
		padmap->Seek(&(seek_zero = 0), DOS_SEEK_SET);
		DBP_ASSERT(file_length <= 0xFFFF);
		dbp_custom_mapping.resize((Bit16u)file_length);
		padmap->Read(&version, &version_length);
		padmap->Read(&dbp_custom_mapping[0], &(padmap_length = (Bit16u)file_length));
		if (!version_length || version != 0 || !padmap_length)
		{
			retro_notify(0, RETRO_LOG_ERROR, "Corrupt gamepad mapping data in %c:\\%s", 'C', "PADMAP.DBP");
			DBP_ASSERT(0);
			dbp_custom_mapping.clear();
		}
		dbp_custom_mapping.resize(padmap_length);
		padmap->Close();
		delete padmap;
	}

	static void Save()
	{
		Bit8u last_port = DBP_MAX_PORTS - 1;
		for (; last_port != 0xFF && !IsCustomized(last_port); last_port--) {}
		dbp_custom_mapping.clear();
		if (last_port == 0xFF)
		{
			if (Drives['C'-'A']) Drives['C'-'A']->FileUnlink((char*)"PADMAP.DBP");
		}
		else
		{
			dbp_custom_mapping.resize((DBP_PADMAP_MAXSIZE_PORT * (last_port + 1)) + (dbp_wheelitems.size() * 5));
			Bit8u *data = &dbp_custom_mapping[0], *p = data, *pCount;
			for (Bit8u port = 0; port <= last_port; port++)
			{
				*(pCount = p++) = 0;
				for (Bit8u btn_id = 0; btn_id != WHEEL_ID; btn_id++)
				{
					bool isAnalog = (btn_id >= 16); Bit8u key_count;
					if ((key_count = FillBinds(p+1, PortDeviceIndexIdForBtn(port, btn_id), isAnalog)) == 0) continue;
					*p = btn_id | (Bit8u)((key_count - 1)<<6);
					p += 1 + key_count * (isAnalog ? 2 : 1);
					(*pCount)++;
				}
				for (const DBP_WheelItem& wi : dbp_wheelitems)
				{
					if (wi.port != port || !wi.key_count) continue;
					*p = (Bit8u)WHEEL_ID | (Bit8u)((wi.key_count - 1)<<6);
					memcpy(p+1, wi.k, wi.key_count);
					p += 1 + wi.key_count;
					(*pCount)++;
				}
			}
			dbp_custom_mapping.resize(p - data);

			DOS_File *padmap = nullptr;
			if (!Drives['C'-'A'] || !Drives['C'-'A']->FileCreate(&padmap, (char*)"PADMAP.DBP", DOS_ATTR_ARCHIVE))
			{
				retro_notify(0, RETRO_LOG_ERROR, "Unable to write gamepad mapping data %c:\\%s", 'C', "PADMAP.DBP");
				DBP_ASSERT(0);
				return;
			}
			Bit8u version = 0;
			Bit16u version_length = sizeof(version), padmap_length = (Bit16u)dbp_custom_mapping.size();
			padmap->AddRef();
			padmap->Write(&version, &version_length);
			padmap->Write(data, &padmap_length);
			padmap->Close();
			delete padmap;
		}
	}

	static void EditBind(DBP_InputBind& b, bool isNew, bool isEdit, bool isDelete, Bit8u bind_part, Bit8u bind_key)
	{
		DBP_ASSERT((int)isNew + (int)isEdit + (int)isDelete == 1);
		dbp_binds_changed |= (1 << b.port);
		if (isNew || isEdit)
		{
			Bit8u k0 = bind_key, k1 = 0;
			if (b.device == RETRO_DEVICE_ANALOG) // Binding to an axis
			{
				Bit16s oldmeta = ((b.evt != DBPET_AXISMAPPAIR && b.evt != _DBPET_MAX) ? GetAxisSpecialMappingMeta(b.evt) : b.meta);
				Bit8u other_key = DBP_MAPPAIR_GET((bind_part ? -1 : 1), oldmeta);
				k0 = (bind_part ? other_key : bind_key);
				k1 = (bind_part ? bind_key : other_key);
			}
			if (!SetBindMetaFromPair(b, k0, k1)) { DBP_ASSERT(0); }
			if (isNew) DBP_PadMapping::InsertBind(b);
		}
		if (isDelete) dbp_input_binds.erase(dbp_input_binds.begin() + (&b - &dbp_input_binds[0]));
	}

	static const char* GetPresetName(EPreset preset)
	{
		static const char* presets[] = { "Generic Keyboard", "Mouse w/ Left Analog", "Mouse w/ Right Analog", "Gravis Gamepad (4 Buttons)", "First 2 Button Joystick", "Second 2 Button Joystick", "Thrustmaster Flight Stick", "Both DOS Joysticks", "Custom Mapping" };
		return (preset == PRESET_AUTOMAPPED ? dbp_auto_mapping_title : preset <= PRESET_CUSTOM ? presets[preset - 2] : NULL);
	}

	static EPreset GetPreset(Bit8u port, EPreset check_one = PRESET_NONE)
	{
		const Bit8u* checkPresets[PRESET_CUSTOM];
		int nBegin = (check_one ? check_one : PRESET_AUTOMAPPED + (dbp_auto_mapping ? 0 : 1)), nEnd = (check_one ? check_one + 1 : PRESET_CUSTOM);
		for (int n = nBegin; n != nEnd; n++) checkPresets[n] = PresetBinds((EPreset)n, port);

		for (Bit8u btn_id = 0; btn_id != WHEEL_ID; btn_id++)
		{
			Bit8u bind_buf[4*2], bind_count = FillBinds(bind_buf, PortDeviceIndexIdForBtn(port, btn_id), (btn_id >= 16));

			if (btn_id == RETRO_DEVICE_ID_JOYPAD_L3 && port == 0 && dbp_map_osd && bind_buf[0] == DBP_SPECIALMAPPINGS_OSD && bind_count == 1) continue; // skip OSK bind
			bool oskshift = (btn_id == RETRO_DEVICE_ID_JOYPAD_R3 && port == 0 && dbp_map_osd); // handle shifting due to OSK with generic keyboard

			for (int n = nBegin; n != nEnd; n++)
			{
				if (!checkPresets[n]) { if (n == nBegin) nBegin++; continue; }
				Bit8u match_id = (!oskshift || n != PRESET_GENERICKEYBOARD ? btn_id : RETRO_DEVICE_ID_JOYPAD_L3), match = (bind_count == 0);;
				for (const BindDecoder& it : BindDecoder(checkPresets[n]))
				{
					if (it.BtnID != match_id) continue;
					match = (it.KeyCount == bind_count && !memcmp(it.P, bind_buf, it.KeyCount * (it.IsAnalog ? 2 : 1)));
					if (!match) checkPresets[n] = NULL;
					break;
				}
				if (check_one && !match) return PRESET_CUSTOM;
			}
		}
		if (nBegin <= PRESET_AUTOMAPPED && nEnd > PRESET_AUTOMAPPED && checkPresets[PRESET_AUTOMAPPED])
		{
			int haveItems = 0, presetItems = 0;
			for (const DBP_WheelItem& wi : dbp_wheelitems)
			{
				if (wi.port != port || !wi.key_count) continue;
				bool match = false;
				for (const BindDecoder& it : BindDecoder(checkPresets[PRESET_AUTOMAPPED]))
					if (it.BtnID == WHEEL_ID && it.KeyCount == wi.key_count && !memcmp(it.P, wi.k, wi.key_count)) { match = true; break; }
				if (!match) goto invalidAutomap;
				haveItems++;
			}
			for (const BindDecoder& it : BindDecoder(checkPresets[PRESET_AUTOMAPPED])) if (it.BtnID == WHEEL_ID) presetItems++;
			if (haveItems != presetItems) { invalidAutomap: checkPresets[PRESET_AUTOMAPPED] = NULL; }
		}
		for (int n = nBegin; n != nEnd; n++) if (checkPresets[n]) return (EPreset)n;
		return PRESET_CUSTOM;
	}

	static const char* GetBoundAutoMapButtonLabel(Bit32u port_device_index_id, bool isAnalog)
	{
		if (!dbp_auto_mapping || !dbp_auto_mapping_names) return NULL;
		Bit8u bind_buf[4*2], bind_count = FillBinds(bind_buf, port_device_index_id, isAnalog);
		return FindAutoMapButtonLabel(bind_count, bind_buf, isAnalog);
	}

	static void SetPortMode(unsigned port, unsigned device)
	{
		Bit8u devtype = (Bit8u)(device & RETRO_DEVICE_MASK), subclass = (Bit8u)((device >> RETRO_DEVICE_TYPE_SHIFT) - 1), mode = MODE_DISABLED;
		bool is_joy = (devtype == RETRO_DEVICE_JOYPAD || devtype == RETRO_DEVICE_ANALOG), is_key = (devtype == RETRO_DEVICE_KEYBOARD);
		if      (is_joy && subclass == 99 && dbp_auto_mapping) mode = MODE_PRESET_AUTOMAPPED;
		else if (is_joy && subclass < (PRESET_CUSTOM - PRESET_GENERICKEYBOARD)) mode = MODE_PRESET_GENERICKEYBOARD + subclass;
		else if (is_joy) mode = MODE_MAPPER;
		else if (is_key) mode = (subclass == 1 ? MODE_KEYBOARD_MOUSE1 : subclass == 2 ? MODE_KEYBOARD_MOUSE2 : MODE_KEYBOARD);
		if (port >= DBP_MAX_PORTS || dbp_port_mode[port] == mode) return;
		dbp_port_mode[port] = mode;
		if (dbp_state <= DBPSTATE_SHUTDOWN) return;
		if (mode) SetInputDescriptors(true);
		else if (!CalcPortMode(port)) ClearBinds((Bit8u)port);
	}

	static void SetInputDescriptors(bool regenerate_bindings = false)
	{
		DBP_ASSERT(regenerate_bindings || dbp_binds_changed); // shouldn't be called otherwise
		if (regenerate_bindings)
		{
			dbp_input_binds.clear();
			if (dbp_mouse_input != 'f')
			{
				if (dbp_mouse_input != 'p')
				{
					dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   DBPET_MOUSEDOWN, 0 });
					dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  DBPET_MOUSEDOWN, 1 });
					dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, DBPET_MOUSEDOWN, 2 });
				}
				if (dbp_bind_mousewheel)
				{
					dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,   DBPET_KEYDOWN, DBP_MAPPAIR_GET(-1, dbp_bind_mousewheel) });
					dbp_input_binds.push_back({ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN, DBPET_KEYDOWN, DBP_MAPPAIR_GET( 1, dbp_bind_mousewheel) });
				}
			}
			const Bit8u* mapping = (!dbp_custom_mapping.empty() ? &dbp_custom_mapping[0] : NULL), *mapping_end = mapping + dbp_custom_mapping.size();
			for (Bit8u port = 0, mode; port != DBP_MAX_PORTS; port++)
			{
				if ((mode = CalcPortMode(port)) == MODE_MAPPER)
				{
					if (mapping && mapping < mapping_end) mapping = Apply(port, mapping, false);
					else if (port == 0 && dbp_auto_mapping) Apply(port, dbp_auto_mapping, true);
					else Apply(port, PresetBinds(PRESET_GENERICKEYBOARD, port), true);
				}
				else
				{
					if (mapping && mapping < mapping_end) mapping = SkipMapping(mapping);
					bool preset_mode = (mode >= MODE_PRESET_AUTOMAPPED && mode <= MODE_PRESET_LAST), bind_osd = (mode != MODE_DISABLED);
					EPreset preset = (preset_mode) ? (EPreset)(PRESET_AUTOMAPPED + (mode - MODE_PRESET_AUTOMAPPED))
					               : (mode == MODE_KEYBOARD_MOUSE1) ? PRESET_MOUSE_LEFT_ANALOG
					               : (mode == MODE_KEYBOARD_MOUSE2) ? PRESET_MOUSE_RIGHT_ANALOG
					               : PRESET_NONE;
					if (bind_osd) Apply(port, PresetBinds(preset, port), true);
					if (preset_mode) FillGenericKeys(port);
				}
			}
		}

		RefreshDosJoysticks(); // do after bindings change
		dbp_binds_changed = 0; // this contains which ports had changes but we're just rebuilding all descriptors anyway here

		static std::vector<std::string> input_names;
		input_names.reserve(dbp_input_binds.size() + DBP_MAX_PORTS); // otherwise strings move their data in memory when the vector reallocates
		input_names.clear();
		std::vector<retro_input_descriptor> input_descriptor;
		for (DBP_InputBind *b = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *bEnd = b + dbp_input_binds.size(), *prev = NULL; b != bEnd; prev = b++)
			if (b->device != RETRO_DEVICE_MOUSE && b->port < DBP_MAX_PORTS && (!prev || PORT_DEVICE_INDEX_ID(*prev) != PORT_DEVICE_INDEX_ID(*b)))
				if (const char* desc = GenerateDesc(input_names, PORT_DEVICE_INDEX_ID(*b), b->device == RETRO_DEVICE_ANALOG))
					input_descriptor.push_back( { b->port, b->device, b->index, b->id, desc } );
		input_descriptor.push_back( { 0 } );
		environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &input_descriptor[0]);

		enum { TYPES_COUNT = 2 + (PRESET_CUSTOM - PRESET_AUTOMAPPED) + 3 };
		static retro_controller_info ports[DBP_MAX_PORTS+1];
		static retro_controller_description descs[DBP_MAX_PORTS*TYPES_COUNT];
		for (Bit8u port = 0; port != DBP_MAX_PORTS; port++)
		{
			if (dbp_port_mode[port] == MODE_MAPPER) { input_names.push_back("[Pad Mapper] "); input_names.back().append(GetPortPresetName(port)); }

			retro_controller_description *types = descs + port * TYPES_COUNT, *type = types;
			*(type++) = { "Disabled", (unsigned)RETRO_DEVICE_NONE };
			*(type++) = { (dbp_port_mode[port] == MODE_MAPPER ? input_names.back().c_str() : "Use Gamepad Mapper"), (unsigned)RETRO_DEVICE_JOYPAD };
			if (dbp_auto_mapping) *(type++) = { dbp_auto_mapping_title, (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 99) };
			for (int i = PRESET_GENERICKEYBOARD; i != PRESET_CUSTOM; i++)
				*(type++) = { GetPresetName((EPreset)i), (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, i - PRESET_GENERICKEYBOARD) };
			*(type++) = { "Custom Keyboard Bindings", (unsigned)RETRO_DEVICE_KEYBOARD };
			*(type++) = { "Custom Keyboard + Mouse on Left Stick and B/A/X",  (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 1) };
			*(type++) = { "Custom Keyboard + Mouse on Right Stick and L/R/X", (unsigned)RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 2) };

			ports[port].num_types = (unsigned)(type - types);
			ports[port].types = types;
		}
		ports[DBP_MAX_PORTS] = {0};
		environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
	}

	static void RefreshDosJoysticks()
	{
		// Enable DOS joysticks only when mapped
		// This helps for games which by default react to the joystick without calibration
		// This can cause problems in other games that expect the joystick to respond (but hopefully these games have a setup program that can disable that)
		bool useJoy1 = false, useJoy2 = false, useAnalogButtons = false;
		for (const DBP_InputBind *b = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *bEnd = b + dbp_input_binds.size(); b != bEnd; b++)
			for (Bit16s bevt = b->evt, dir = 1;; dir -= 2)
			{
				Bit16s map = DBP_MAPPAIR_GET(dir, b->meta), evt = ((map >= DBP_SPECIALMAPPINGS_KEY && bevt == DBPET_AXISMAPPAIR) ? DBP_SPECIALMAPPING(map).evt : bevt);
				useJoy1 |= (evt == DBPET_JOY1X || evt == DBPET_JOY1Y || evt == DBPET_JOY1DOWN);
				useJoy2 |= (evt == DBPET_JOY2X || evt == DBPET_JOY2Y || evt == DBPET_JOY2DOWN || evt == DBPET_JOYHATSETBIT);
				useAnalogButtons |= (evt <= _DBPET_JOY_AXIS_MAX && b->device == RETRO_DEVICE_JOYPAD);
				if (bevt != DBPET_AXISMAPPAIR || dir < 0) break;
			}
		JOYSTICK_Enable(0, useJoy1);
		JOYSTICK_Enable(1, useJoy2);
		dbp_analog_buttons = useAnalogButtons;
	}

	static DBP_InputBind BindForWheel(Bit8u port, Bit8u k)
	{
		DBP_InputBind bnd = { port, RETRO_DEVICE_JOYPAD, 0, WHEEL_ID };
		if (!SetBindMetaFromPair(bnd, k)) { DBP_ASSERT(0); bnd.device = RETRO_DEVICE_NONE; }
		return bnd;
	}

	static void CheckInputVariables()
	{
		Bit16s bind_mousewheel = dbp_yml_mousewheel;
		if (!bind_mousewheel)
		{
			const char* mouse_wheel = DBP_Option::Get(DBP_Option::mouse_wheel);
			const char* mouse_wheel2 = (mouse_wheel ? strchr(mouse_wheel, '/') : NULL);
			int wkey1 = (mouse_wheel ? atoi(mouse_wheel) : 0);
			int wkey2 = (mouse_wheel2 ? atoi(mouse_wheel2 + 1) : 0);
			bind_mousewheel = (wkey1 > KBD_NONE && wkey1 < KBD_LAST && wkey2 > KBD_NONE && wkey2 < KBD_LAST ? DBP_MAPPAIR_MAKE(wkey1, wkey2) : 0);
		}
		char map_osd = DBP_Option::Get(DBP_Option::map_osd)[0];
		char mouse_input = DBP_Option::Get(DBP_Option::mouse_input)[0];
		if (mouse_input == 't' && dbp_yml_directmouse) mouse_input = 'd';
		if (map_osd == 'f') map_osd = 0; // "false" becomes 0
		if (map_osd != dbp_map_osd || mouse_input != dbp_mouse_input || bind_mousewheel != dbp_bind_mousewheel)
		{
			dbp_map_osd = map_osd;
			dbp_mouse_input = mouse_input;
			dbp_bind_mousewheel = bind_mousewheel;
			if (dbp_state > DBPSTATE_SHUTDOWN) DBP_PadMapping::SetInputDescriptors(true);
		}
		dbp_mouse_speed = (float)atof(DBP_Option::Get(DBP_Option::mouse_speed_factor)) * dbp_yml_mousespeed;
		dbp_mouse_speed_x = (float)atof(DBP_Option::Get(DBP_Option::mouse_speed_factor_x)) * dbp_yml_mousexfactor;
		dbp_joymouse_speed = .0003f * dbp_yml_padmousespeed;
	}

	static void ResetYML()
	{
		dbp_yml_mousewheel = 0;
		dbp_yml_directmouse = false;
		dbp_yml_mousespeed = dbp_yml_mousexfactor = dbp_yml_padmousespeed = 1;
		if (dbp_yml_mapping) { dbp_auto_mapping = NULL; dbp_yml_mapping = false; }
	}

	static void PostYML()
	{
		if (dbp_yml_mousewheel || dbp_yml_directmouse || dbp_yml_mousespeed != 1 || dbp_yml_mousexfactor != 1 || dbp_yml_padmousespeed != 1 || dbp_yml_mapping)
			CheckInputVariables();
	}

	static bool ParseInputYML(const char *Key, const char *KeyX, const char *Val, const char *ValX)
	{
		size_t keyLen = (size_t)(KeyX - Key);
		if (keyLen == (sizeof("input_directmouse") - 1) && !strncmp("input_directmouse", Key, (size_t)(sizeof("input_directmouse") - 1)))
		{
			dbp_yml_directmouse = (Val[0]|0x20) == 't';
			return true;
		}
		if (   (keyLen == (sizeof("input_mousespeed")    - 1) && !strncmp("input_mousespeed",    Key, (size_t)(sizeof("input_mousespeed")    - 1)))
			|| (keyLen == (sizeof("input_mousexfactor")  - 1) && !strncmp("input_mousexfactor",  Key, (size_t)(sizeof("input_mousexfactor")  - 1)))
			|| (keyLen == (sizeof("input_padmousespeed") - 1) && !strncmp("input_padmousespeed", Key, (size_t)(sizeof("input_padmousespeed") - 1))))
		{
			int percent = atoi(Val);
			if (percent <= 0) return false;
			(Key[6] == 'p' ? dbp_yml_padmousespeed : (Key[11] == 'x' ? dbp_yml_mousexfactor : dbp_yml_mousespeed)) = percent / 100.0f;
			return true;
		}
		if (   (keyLen == (sizeof("input_mousewheelup")   - 1) && !strncmp("input_mousewheelup",   Key, (size_t)(sizeof("input_mousewheelup")   - 1)))
			|| (keyLen == (sizeof("input_mousewheeldown") - 1) && !strncmp("input_mousewheeldown", Key, (size_t)(sizeof("input_mousewheeldown") - 1))))
		{
			const Bit8u mapid = GetYMLMapId(Val, ValX - Val);
			if (mapid == 255) return false;
			dbp_yml_mousewheel = DBP_MAPPAIR_MAKE((Key[16] == 'u' ? mapid : DBP_MAPPAIR_GET(-1, dbp_yml_mousewheel)), (Key[16] == 'd' ? mapid : DBP_MAPPAIR_GET(1, dbp_yml_mousewheel)));
			return true;
		}

		if (keyLen > (sizeof("input_pad_") - 1) && !strncmp("input_pad_", Key, (size_t)(sizeof("input_pad_") - 1))) {}
		else if (keyLen > (sizeof("input_wheel_") - 1) && !strncmp("input_wheel_", Key, (size_t)(sizeof("input_wheel_") - 1))) {}
		else return false;

		static const char *padnames[24] = { "b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r", "l2", "r2", "l3", "r3", "lstick_left", "lstick_right", "lstick_up", "lstick_down", "rstick_left", "rstick_right", "rstick_up", "rstick_down" };
		const bool iswheel = (Key[6] == 'w');
		int padwheelnum;
		if (!iswheel)
		{
			const char* padname = Key + 10;
			size_t padlen = KeyX - padname;
			for (padwheelnum = 0; padwheelnum != 24; padwheelnum++) if (!strncmp(padname, padnames[padwheelnum], padlen) && padnames[padwheelnum][padlen] == '\0') goto gotnum;
			return false;
			gotnum:;
		}
		else
		{
			padwheelnum = atoi(Key + 12) - 1;
			if (padwheelnum < 0 || padwheelnum > 99) return false;
		}

		const char *split, *txt;
		for (split = Val; split != ValX && *split != ' ';) split++;
		for (txt = split; txt != ValX && *txt == ' ';) txt++;
		Bit8u maps[8] = { 0 }, keyCount = 0;

		const Bit8u btnID = (Bit8u)(iswheel ? DBP_PadMapping::WHEEL_ID : (padwheelnum < 16 ? padwheelnum : (16 + (padwheelnum - 16) / 2)));
		const char *name = (txt != ValX ? txt : NULL), *nameEnd = ValX;
		const bool isAnalog = ((btnID >> 2) == 4); // 16 - 19
		const Bit8u analogpart = (isAnalog ? (padwheelnum & 1) : 0);

		for (const char* p = Val, *pid = Val; p <= split; p++)
		{
			if (p != split && *p != '+') continue;
			const Bit8u mapid = GetYMLMapId(pid, p - pid);
			if (mapid == 255 || keyCount == 4) return false;
			if (mapid) maps[(keyCount++) * (isAnalog ? 2 : 1) + analogpart] = mapid;
			pid = p+1;
		}

		static std::string YMLNames;
		static std::vector<Bit8u> YMLMapping;
		size_t appendName = (size_t)-1, overwriteIndex = 0;
		for (const BindDecoder& it : BindDecoder(dbp_yml_mapping ? &YMLMapping[0] : NULL))
		{
			if (it.BtnID != btnID) continue;
			if (isAnalog && !it.P[analogpart]) // configure other analog part
			{
				if (it.HasActionName) appendName = (size_t)it.NameOffset;
				if (it.KeyCount > keyCount) keyCount = it.KeyCount;
				for (int i = 0; i != it.KeyCount; i++)
					maps[i * 2 + (int)(!analogpart)] = it.P[i * 2 + (int)(!analogpart)];
			}
			else if (iswheel && padwheelnum) { padwheelnum--; continue; } // wrong wheel index

			// overwrite existing entry
			const Bit8u* itStart = it.P - (it.HasActionName ? (it.NameOffset >= 2097152 ? 4 : it.NameOffset >= 16384 ? 3 : it.NameOffset >= 128 ? 2 : 1) : 0) - 1;
			const Bit8u* itEnd = it.P + (it.KeyCount * (isAnalog ? 2 : 1));
			YMLMapping.erase(YMLMapping.begin() + (overwriteIndex = (itStart - &YMLMapping[0])), YMLMapping.begin() + (itEnd - &YMLMapping[0]));
			if (!(--YMLMapping[0])) { dbp_auto_mapping = NULL; dbp_yml_mapping = false; }
			break;
		}
		if (keyCount == 0) return true; // entry was unbound with none
		if (iswheel && padwheelnum) return false; // wheel index in wrong order

		if (!dbp_yml_mapping)
		{
			YMLNames.clear();
			YMLMapping.clear();
			YMLMapping.push_back(0);
		}

		const size_t nameofs = YMLNames.length();
		const bool hasActionName = (name || appendName != (size_t)-1);
		const int actionNameBytes = (hasActionName ? (nameofs >= 2097152 ? 4 : nameofs >= 16384 ? 3 : nameofs >= 128 ? 2 : 1) : 0);
		const size_t ymlofs = (overwriteIndex ? overwriteIndex : YMLMapping.size());

		YMLMapping[0]++;
		YMLMapping.insert(YMLMapping.begin() + ymlofs, 1 + actionNameBytes + keyCount * (isAnalog ? 2 : 1), 0);
		Bit8u* p = &YMLMapping[ymlofs];
		*(p++) = (Bit8u)(((keyCount - 1) << 6) | (hasActionName ? 32 : 0) | btnID);
		for (int i = actionNameBytes - 1; i != -1; i--)
			*(p++) = (Bit8u)(((nameofs >> (7 * i)) & 127) | (i ? 128 : 0));
		memcpy(p, maps, keyCount * (isAnalog ? 2 : 1));

		if (hasActionName)
		{
			if (appendName != (size_t)-1) YMLNames.append(&YMLNames[appendName]).append(name ? " / " : "");
			if (name) YMLNames.append(name, (nameEnd - name));
			YMLNames += '\0';
		}

		dbp_yml_mapping = true;
		dbp_auto_mapping = &YMLMapping[0];
		dbp_auto_mapping_names = YMLNames.c_str();
		dbp_auto_mapping_title = "Content Provided Mapping";
		return true;
	}

private:
	static Bit8u GetYMLMapId(const char* str, size_t strlen)
	{
		for (Bit8u i = 1; i != KBD_LAST; i++)
			if (!strncasecmp(DBP_YMLKeyCommands[i], str, strlen) && DBP_YMLKeyCommands[i][strlen] == '\0')
				return i;
		for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
			if (sm.ymlid && !strncasecmp(str, sm.ymlid, strlen) && sm.ymlid[strlen] == '\0')
				return (Bit8u)DBP_SPECIALMAPPINGS_KEY + (Bit8u)(&sm - DBP_SpecialMappings);
		return ((strlen == 4 && !strncasecmp("none", str, 4)) ? 0 : 255);
	}

	static int InsertBind(const DBP_InputBind& b)
	{
		const DBP_InputBind *pEnd = (dbp_input_binds.size() ? &dbp_input_binds[0]-1 : NULL), *pBegin = pEnd + dbp_input_binds.size(), *p = pBegin;
		const Bit32u b_sort_key = (Bit32u)((b.port<<24)|(b.device<<16)|(b.index<<8)|(b.id));
		for (; p != pEnd; p--)
			if (p->device == RETRO_DEVICE_MOUSE || (Bit32u)((p->port<<24)|(p->device<<16)|(p->index<<8)|(p->id)) <= b_sort_key)
				break;
		int insert_idx = (int)(p - pEnd);
		dbp_input_binds.insert(dbp_input_binds.begin() + insert_idx, b);
		return insert_idx;
	}

	static void ClearBinds(Bit8u port)
	{
		DBP_InputBind *binds = (dbp_input_binds.empty() ? NULL : &dbp_input_binds[0]), *b = binds, *bEnd = b + dbp_input_binds.size(), *shift = b;
		for (; b != bEnd; b++, shift++) { if (b->port == port && (b->device & 3) == 1) shift--; else if (shift != b) *shift = *b; }
		if (shift != b) dbp_input_binds.resize(shift - binds);
	}

	static const char* GenerateDesc(std::vector<std::string>& input_names, Bit32u port_device_index_id, bool isAnalog)
	{
		input_names.emplace_back();
		std::string& name = input_names.back();

		Bit8u bind_buf[4*2], bind_count = FillBinds(bind_buf, port_device_index_id, isAnalog), *p = bind_buf;
		const char* amn = FindAutoMapButtonLabel(bind_count, bind_buf, isAnalog);
		if (amn) ((name = amn) += ' ') += '(';

		for (const char *desc_lastdev = NULL; bind_count--;)
		{
			for (int i = 0; i <= (int)isAnalog; i++)
			{
				if (i) name += '/';
				Bit8u k =  *(p++);
				const char *desc_dev = DBP_GETKEYDEVNAME(k);
				if (desc_lastdev != desc_dev) { if (desc_dev) (name += desc_dev) += ' ';  desc_lastdev = desc_dev; }
				name += DBP_GETKEYNAME(k);
			}
			if (bind_count) name += '+';
		}
		if (amn) name += ')';
		return name.c_str();
	}

	#define DBP_ANALOGBINDID2(INDEX,ID) (16+((INDEX)*2)+(ID))
	#define DBP_ANALOGBINDID(SIDE,AXIS) DBP_ANALOGBINDID2(RETRO_DEVICE_INDEX_ANALOG_##SIDE, RETRO_DEVICE_ID_ANALOG_##AXIS)

	INLINE static DBP_InputBind BindForBtn(Bit8u port, Bit8u id) { if (id>>2==4) return { port, RETRO_DEVICE_ANALOG, (Bit8u)(id >= 18), (Bit8u)(id & 1) }; else return { port, RETRO_DEVICE_JOYPAD, 0, id }; }
	INLINE static Bit32u PortDeviceIndexIdForBtn(Bit8u port, Bit8u id) { DBP_InputBind bnd = BindForBtn(port, id); return PORT_DEVICE_INDEX_ID(bnd); }

	static const Bit8u* Apply(Bit8u port, const Bit8u* mapping, bool is_preset, bool only_unbound = false)
	{
		static const Bit8u bindUsedToNext[20] = { RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_X, 0xFF, 0xFF, 0xFF, 0xFF, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, 0xFF, DBP_ANALOGBINDID(LEFT,Y), DBP_ANALOGBINDID(RIGHT,X), DBP_ANALOGBINDID(RIGHT,Y), 0xFF };
		bool bound_buttons[20] = {false};
		if (only_unbound) for (const DBP_InputBind& b : dbp_input_binds)
		{
			if (b.port != port) continue;
			if (b.device == RETRO_DEVICE_JOYPAD && b.id <= RETRO_DEVICE_ID_JOYPAD_R3) bound_buttons[b.id] = true;
			else if (b.device == RETRO_DEVICE_ANALOG) bound_buttons[DBP_ANALOGBINDID2(b.index,b.id)] = true;
		}
		bool bind_osd = (port == 0 && dbp_map_osd && !bound_buttons[RETRO_DEVICE_ID_JOYPAD_L3]);
		if (bind_osd && is_preset) bound_buttons[RETRO_DEVICE_ID_JOYPAD_L3] = true;

		for (size_t i = dbp_wheelitems.size(); i--;)
			if (dbp_wheelitems[i].port == port)
				dbp_wheelitems.erase(dbp_wheelitems.begin() + i);

		for (const BindDecoder& it : BindDecoder(&mapping))
		{
			Bit8u btnId = it.BtnID;
			if (btnId == WHEEL_ID)
			{
				dbp_wheelitems.emplace_back();
				DBP_WheelItem& wi = dbp_wheelitems.back();
				wi.port = port;
				wi.key_count = it.KeyCount;
				memcpy(wi.k, it.P, it.KeyCount);
				continue;
			}

			if (btnId > WHEEL_ID) { DBP_ASSERT(0); goto err; }
			while (btnId != 0xFF && bound_buttons[btnId]) btnId = bindUsedToNext[btnId];
			if (btnId == 0xFF) continue;
			bound_buttons[btnId] = true;

			DBP_InputBind bnd = BindForBtn(port, btnId);
			for (int i = 0, istep = (it.IsAnalog ? 2 : 1), iend = it.KeyCount * istep; i != iend; i += istep)
			{
				if (!SetBindMetaFromPair(bnd, it.P[i], (it.IsAnalog ? it.P[i+1] : (Bit8u)0))) { DBP_ASSERT(0); goto err; }
				if (bnd.evt == DBPET_TOGGLEOSD) bind_osd = false;
				InsertBind(bnd);
			}
		}

		if (bind_osd && (is_preset || !bound_buttons[RETRO_DEVICE_ID_JOYPAD_L3]))
			InsertBind({ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, DBPET_TOGGLEOSD });

		dbp_binds_changed |= (1 << port);
		return mapping;
		err: emuthread_notify(0, LOG_ERROR, "Gamepad mapping data is invalid"); return mapping+(DBP_PADMAP_MAXSIZE_PORT*DBP_MAX_PORTS);
	}

	static const Bit8u* SkipMapping(const Bit8u* mapping)
	{
		for (const BindDecoder& it : BindDecoder(&mapping)) if (it.BtnID > WHEEL_ID) { DBP_ASSERT(0); return mapping+(DBP_PADMAP_MAXSIZE_PORT*DBP_MAX_PORTS); }
		return mapping;
	}

	static Bit16s GetAxisSpecialMappingMeta(Bit16s evt)
	{
		for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
		{
			if (sm.evt != evt || sm.meta != -1) continue;
			DBP_ASSERT((&sm)[1].evt == sm.evt && (&sm)[1].meta == -sm.meta);
			int key = DBP_SPECIALMAPPINGS_KEY + (int)(&sm - DBP_SpecialMappings);
			return DBP_MAPPAIR_MAKE(key, key+1);
		}
		DBP_ASSERT(false); return 0;
	}

	static bool SetBindMetaFromPair(DBP_InputBind& b, Bit8u k0, Bit8u k1 = 0)
	{
		if (b.device != RETRO_DEVICE_ANALOG)
		{
			if (k0 < KBD_LAST && k0 != KBD_NONE) // Binding a key to a joypad button
			{
				b.evt = DBPET_KEYDOWN;
				b.meta = k0;
			}
			else if (k0 >= DBP_SPECIALMAPPINGS_KEY && k0 < DBP_SPECIALMAPPINGS_MAX) // Binding a special mapping to a joypad button
			{
				b.evt = DBP_SPECIALMAPPING(k0).evt;
				b.meta = DBP_SPECIALMAPPING(k0).meta;
			}
			else return false;
		}
		else // Binding to an axis
		{
			if (k1 == k0 + 1 && k0 >= DBP_SPECIALMAPPINGS_KEY && k1 < DBP_SPECIALMAPPINGS_MAX && DBP_SPECIALMAPPING(k0).evt <= _DBPET_JOY_AXIS_MAX && DBP_SPECIALMAPPING(k0).evt == DBP_SPECIALMAPPING(k1).evt)
			{
				DBP_ASSERT(DBP_SPECIALMAPPING(k0).meta == -1 && DBP_SPECIALMAPPING(k1).meta == 1);
				b.evt = DBP_SPECIALMAPPING(k0).evt;
				b.meta = 0;
			}
			else if ((k0 < KBD_LAST || (k0 >= DBP_SPECIALMAPPINGS_KEY && k0 < DBP_SPECIALMAPPINGS_MAX)) && (k1 < KBD_LAST || (k1 >= DBP_SPECIALMAPPINGS_KEY && k1 < DBP_SPECIALMAPPINGS_MAX)) && (k0 != KBD_NONE || k1 != KBD_NONE))
			{
				b.evt = DBPET_AXISMAPPAIR;
				b.meta = DBP_MAPPAIR_MAKE(k0, k1);
			}
			else return false;
		}
		return true;
	}

	static Bit8u FillBinds(Bit8u* p, Bit32u port_device_index_id, bool isAnalog)
	{
		Bit8u key_count = 0;
		for (const DBP_InputBind& b : dbp_input_binds)
		{
			if (PORT_DEVICE_INDEX_ID(b) != port_device_index_id) continue;
			p[0] = p[1] = (Bit8u)KBD_NONE;
			if (isAnalog)
			{
				Bit16s meta = ((b.evt != DBPET_AXISMAPPAIR && b.evt != _DBPET_MAX) ? GetAxisSpecialMappingMeta(b.evt) : b.meta);
				p[0] = DBP_MAPPAIR_GET(-1, meta), p[1] = DBP_MAPPAIR_GET(1, meta);
			}
			else if (b.evt == DBPET_KEYDOWN)
				p[0] = (Bit8u)b.meta;
			else for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
				if (sm.evt == b.evt && sm.meta == b.meta)
					{ p[0] = (Bit8u)DBP_SPECIALMAPPINGS_KEY+(Bit8u)(&sm - DBP_SpecialMappings); break; }
			if (p[0] == KBD_NONE && p[1] == KBD_NONE) continue;
			p += (isAnalog ? 2 : 1);
			if (++key_count == 4) break;
		}
		return key_count;
	}

	static const Bit8u* PresetBinds(EPreset preset, Bit8u port)
	{
		static const Bit8u
			arrPRESET_MOUSE_LEFT_ANALOG[]          = {  7,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           204,     //   Left Click
				RETRO_DEVICE_ID_JOYPAD_A,           205,     //   Right Click
				RETRO_DEVICE_ID_JOYPAD_X,           206,     //   Middle Click
				RETRO_DEVICE_ID_JOYPAD_L2,          207,     //   Speed Up
				RETRO_DEVICE_ID_JOYPAD_R2,          208,     //   Slow Down
				DBP_ANALOGBINDID(LEFT,X),           202,203, //   Move Left / Move Right
				DBP_ANALOGBINDID(LEFT,Y),           200,201, //   Move Up / Move Down
			}, arrPRESET_MOUSE_RIGHT_ANALOG[]       = {  7,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_L,           204,     //   Left Click
				RETRO_DEVICE_ID_JOYPAD_R,           205,     //   Right Click
				RETRO_DEVICE_ID_JOYPAD_X,           206,     //   Middle Click
				RETRO_DEVICE_ID_JOYPAD_L2,          207,     //   Speed Up
				RETRO_DEVICE_ID_JOYPAD_R2,          208,     //   Slow Down
				DBP_ANALOGBINDID(RIGHT,X),          202,203, //   Move Left / Move Right
				DBP_ANALOGBINDID(RIGHT,Y),          200,201, //   Move Up / Move Down
			}, arrPRESET_GRAVIS_GAMEPAD[]           = { 10,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_Y,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_UP,          209,     //   Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        210,     //   Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        211,     //   Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       212,     //   Right
				RETRO_DEVICE_ID_JOYPAD_X,           216,     //   Button 4
				RETRO_DEVICE_ID_JOYPAD_A,           214,     //   Button 2
				DBP_ANALOGBINDID(LEFT,X),           211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT,Y),           209,210, //   Up / Down
			}, arrPRESET_BASIC_JOYSTICK_1[]         = {  8,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_Y,           214,     //   Button 2
				RETRO_DEVICE_ID_JOYPAD_UP,          209,     //   Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        210,     //   Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        211,     //   Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       212,     //   Right
				DBP_ANALOGBINDID(LEFT,X),           211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT,Y),           209,210, //   Up / Down
			}, arrPRESET_BASIC_JOYSTICK_2[]         = {  8,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_Y,           216,     //   Button 4
				RETRO_DEVICE_ID_JOYPAD_UP,          221,     //   Joy 2 Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        222,     //   Joy 2 Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        223,     //   Joy 2 Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       224,     //   Joy 2 Right
				DBP_ANALOGBINDID(LEFT,X),           223,224, //   Joy 2 Left/Right
				DBP_ANALOGBINDID(LEFT,Y),           221,222, //   Joy 2 Up/Down
			}, arrPRESET_THRUSTMASTER_FLIGHTSTICK[] = { 11,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_Y,           214,     //   Button 2
				RETRO_DEVICE_ID_JOYPAD_UP,          217,     //   Hat Up
				RETRO_DEVICE_ID_JOYPAD_DOWN,        218,     //   Hat Down
				RETRO_DEVICE_ID_JOYPAD_LEFT,        219,     //   Hat Left
				RETRO_DEVICE_ID_JOYPAD_RIGHT,       220,     //   Hat Right
				RETRO_DEVICE_ID_JOYPAD_A,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_X,           216,     //   Button 4
				DBP_ANALOGBINDID(LEFT, X),          211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT, Y),          209,210, //   Up / Down
				DBP_ANALOGBINDID(RIGHT,X),          223,224, //   Joy 2 Left/Right
			}, arrPRESET_BOTH_DOS_JOYSTICKS[]       = {  8,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_B,           213,     //   Button 1
				RETRO_DEVICE_ID_JOYPAD_Y,           214,     //   Button 2
				RETRO_DEVICE_ID_JOYPAD_A,           215,     //   Button 3
				RETRO_DEVICE_ID_JOYPAD_X,           216,     //   Button 4
				DBP_ANALOGBINDID(LEFT, X),          211,212, //   Left / Right
				DBP_ANALOGBINDID(LEFT, Y),          209,210, //   Up / Down
				DBP_ANALOGBINDID(RIGHT,X),          223,224, //   Joy 2 Left/Right
				DBP_ANALOGBINDID(RIGHT,Y),          221,222, //   Joy 2 Up/Down
			}, arrPRESET_GENERICKEYBOARD_0[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_up,                            RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_down,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_left,                          RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_right,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_esc,                           RETRO_DEVICE_ID_JOYPAD_START,       KBD_enter,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_space,                         RETRO_DEVICE_ID_JOYPAD_Y,           KBD_leftshift,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_leftctrl,                      RETRO_DEVICE_ID_JOYPAD_A,           KBD_leftalt,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_1,                             RETRO_DEVICE_ID_JOYPAD_R,           KBD_2,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_3,                             RETRO_DEVICE_ID_JOYPAD_R2,          KBD_4,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_f1,                            RETRO_DEVICE_ID_JOYPAD_R3,          KBD_f2,
				DBP_ANALOGBINDID(LEFT, X),          KBD_left,   KBD_right,             DBP_ANALOGBINDID(LEFT, Y),          KBD_up,     KBD_down,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_home,   KBD_end,               DBP_ANALOGBINDID(RIGHT,Y),          KBD_pageup, KBD_pagedown,
			}, arrPRESET_GENERICKEYBOARD_1[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_kp8,                           RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_kp2,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_kp4,                           RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_kp6,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_kpperiod,                      RETRO_DEVICE_ID_JOYPAD_START,       KBD_kpenter,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_kp5,                           RETRO_DEVICE_ID_JOYPAD_Y,           KBD_kp1,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_kp0,                           RETRO_DEVICE_ID_JOYPAD_A,           KBD_kp3,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_kp7,                           RETRO_DEVICE_ID_JOYPAD_R,           KBD_kp9,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_kpminus,                       RETRO_DEVICE_ID_JOYPAD_R2,          KBD_kpplus,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_kpdivide,                      RETRO_DEVICE_ID_JOYPAD_R3,          KBD_kpmultiply,
				DBP_ANALOGBINDID(LEFT, X),          KBD_kp4,      KBD_kp6,             DBP_ANALOGBINDID(LEFT, Y),          KBD_kp8,      KBD_kp2,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_kpminus,  KBD_kpplus,          DBP_ANALOGBINDID(RIGHT,Y),          KBD_kpdivide, KBD_kpmultiply,
			}, arrPRESET_GENERICKEYBOARD_2[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_q,                             RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_a,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_z,                             RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_x,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_g,                             RETRO_DEVICE_ID_JOYPAD_START,       KBD_h,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_d,                             RETRO_DEVICE_ID_JOYPAD_Y,           KBD_f,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_c,                             RETRO_DEVICE_ID_JOYPAD_A,           KBD_s,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_w,                             RETRO_DEVICE_ID_JOYPAD_R,           KBD_e,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_r,                             RETRO_DEVICE_ID_JOYPAD_R2,          KBD_t,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_v,                             RETRO_DEVICE_ID_JOYPAD_R3,          KBD_b,
				DBP_ANALOGBINDID(LEFT, X),          KBD_z, KBD_x,                      DBP_ANALOGBINDID(LEFT, Y),          KBD_q, KBD_a,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_j, KBD_l,                      DBP_ANALOGBINDID(RIGHT,Y),          KBD_i, KBD_k,
			}, arrPRESET_GENERICKEYBOARD_3[]        = {  20,  // count --------------
				RETRO_DEVICE_ID_JOYPAD_UP,          KBD_backspace,                     RETRO_DEVICE_ID_JOYPAD_DOWN,        KBD_backslash,
				RETRO_DEVICE_ID_JOYPAD_LEFT,        KBD_semicolon,                     RETRO_DEVICE_ID_JOYPAD_RIGHT,       KBD_quote,
				RETRO_DEVICE_ID_JOYPAD_SELECT,      KBD_o,                             RETRO_DEVICE_ID_JOYPAD_START,       KBD_p,
				RETRO_DEVICE_ID_JOYPAD_X,           KBD_slash,                         RETRO_DEVICE_ID_JOYPAD_Y,           KBD_rightshift,
				RETRO_DEVICE_ID_JOYPAD_B,           KBD_rightctrl,                     RETRO_DEVICE_ID_JOYPAD_A,           KBD_rightalt,
				RETRO_DEVICE_ID_JOYPAD_L,           KBD_leftbracket,                   RETRO_DEVICE_ID_JOYPAD_R,           KBD_rightbracket,
				RETRO_DEVICE_ID_JOYPAD_L2,          KBD_comma,                         RETRO_DEVICE_ID_JOYPAD_R2,          KBD_period,
				RETRO_DEVICE_ID_JOYPAD_L3,          KBD_minus,                         RETRO_DEVICE_ID_JOYPAD_R3,          KBD_equals,
				DBP_ANALOGBINDID(LEFT, X),          KBD_semicolon,   KBD_quote,        DBP_ANALOGBINDID(LEFT, Y),          KBD_backspace,   KBD_backslash,
				DBP_ANALOGBINDID(RIGHT,X),          KBD_leftbracket, KBD_rightbracket, DBP_ANALOGBINDID(RIGHT,Y),          KBD_minus,       KBD_equals,
			};

		switch (preset)
		{
			case PRESET_AUTOMAPPED:               return dbp_auto_mapping;
			case PRESET_GENERICKEYBOARD:
				switch (port & 3)
				{
					case 0: return arrPRESET_GENERICKEYBOARD_0;
					case 1: return arrPRESET_GENERICKEYBOARD_1;
					case 2: return arrPRESET_GENERICKEYBOARD_2;
					case 3: return arrPRESET_GENERICKEYBOARD_3;
				}
			case PRESET_MOUSE_LEFT_ANALOG:        return arrPRESET_MOUSE_LEFT_ANALOG;
			case PRESET_MOUSE_RIGHT_ANALOG:       return arrPRESET_MOUSE_RIGHT_ANALOG;
			case PRESET_GRAVIS_GAMEPAD:           return arrPRESET_GRAVIS_GAMEPAD;
			case PRESET_BASIC_JOYSTICK_1:         return arrPRESET_BASIC_JOYSTICK_1;
			case PRESET_BASIC_JOYSTICK_2:         return arrPRESET_BASIC_JOYSTICK_2;
			case PRESET_THRUSTMASTER_FLIGHTSTICK: return arrPRESET_THRUSTMASTER_FLIGHTSTICK;
			case PRESET_BOTH_DOS_JOYSTICKS:       return arrPRESET_BOTH_DOS_JOYSTICKS;
		}
		return NULL;
	}

	static const char* FindAutoMapButtonLabel(Bit8u bind_count, const Bit8u* bind_buf, bool bind_analog = false)
	{
		if (!bind_count || !dbp_auto_mapping || !dbp_auto_mapping_names) return NULL;
		for (const BindDecoder& it : BindDecoder(dbp_auto_mapping))
			if (it.HasActionName && it.KeyCount == bind_count && bind_analog == it.IsAnalog && !memcmp(it.P, bind_buf, it.KeyCount * (it.IsAnalog ? 2 : 1)))
				return dbp_auto_mapping_names + it.NameOffset;
		return NULL;
	}

	struct BindDecoder
	{
		INLINE BindDecoder(const Bit8u *ptr) : P(ptr), OutPtr(NULL), Remain(P ? *(P++)+1 : 1), KeyCount(0), IsAnalog(false) { ++*this; }
		INLINE BindDecoder(const Bit8u **ptr) : P(*ptr), OutPtr(ptr), Remain(P ? *(P++)+1 : 1), KeyCount(0), IsAnalog(false) { ++*this; }
		INLINE const BindDecoder& begin() const { return *this; }
		INLINE const BindDecoder& end() const { return *this; }
		INLINE const BindDecoder& operator*() const { return *this; }
		INLINE bool operator!=(const BindDecoder& other) const { if (Remain) return true; if (OutPtr) *OutPtr = P; return false; }
		void operator++()
		{
			P += KeyCount * (1 + (int)IsAnalog);
			if (!--Remain) return;
			Bit8u v = *(P++);
			KeyCount = 1 + (v >> 6);
			BtnID = (Bit8u)(v & 31);
			IsAnalog = ((BtnID >> 2) == 4); // 16 - 19
			HasActionName = !!(v & 32);
			DBP_ASSERT(BtnID <= WHEEL_ID && Remain >= 0);
			if (HasActionName) { NameOffset = 0; do { NameOffset = (NameOffset<<7)|(*P&127); } while (*(P++) & 128); }
		}
		const Bit8u *P, **OutPtr; Bit8u Remain, KeyCount, BtnID; bool IsAnalog, HasActionName; Bit32u NameOffset;
	};

	#ifndef NDEBUG // Can be used in a debuggers watch window
	public:
	static std::string PadMappingToString(const Bit8u* mapping, const char* names = NULL)
	{
		std::string res;
		char buf[1024];
		static const char *padnames[] = { "b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r", "l2", "r2", "l3", "r3", "lstick_x", "lstick_y", "rstick_x", "rstick_y", "wheel", "???????" };
		for (const BindDecoder& it : BindDecoder(mapping))
		{
			res.append((sprintf(buf, "Remain: %2d, KeyCount: %2d, BtnID: %2d (%-10s), IsAnalog: %d, HasActionName: %d, NameOffset: %4u (%s):", it.Remain, it.KeyCount, it.BtnID, padnames[it.BtnID], it.IsAnalog, it.HasActionName, it.NameOffset, ((it.HasActionName && names) ? names+it.NameOffset : "")), buf));
			for (int i = 0, istep = (it.IsAnalog ? 2 : 1), iend = it.KeyCount * istep; i != iend; i += istep)
			{
				if (it.IsAnalog) res.append((sprintf(buf, " [%s (%d) / %s (%d)]", DBP_GETKEYNAME(it.P[i]), it.P[i], DBP_GETKEYNAME(it.P[i + 1]), it.P[i + 1]), buf));
				else             res.append((sprintf(buf, " [%s (%d)]", DBP_GETKEYNAME(it.P[i]), it.P[i]), buf));
			}
			res.append("\n");
		}
		return res;
	}
	#endif
};

#ifndef NDEBUG // Can be used in a debuggers watch window
std::string PadMappingToString(const Bit8u* mapping, const char* names = NULL) { return DBP_PadMapping::PadMappingToString(mapping, names); }
#endif
