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

enum { DBPS_OSD_WIDTH = 640, DBPS_OSD_HEIGHT = 480 };
extern int DBPS_SaveSlotIndex;
extern std::string DBPS_BrowsePath;
void DBPS_OnContentLoad(const char* name, const char* dir, size_t dirlen);
void DBPS_SubmitOSDFrame(const void *data, unsigned width, unsigned height);
bool DBPS_IsGameRunning();
bool DBPS_IsShowingOSD();
void DBPS_OpenContent(const char* path);
void DBPS_ToggleOSD();
void DBPS_GetMouse(short& mx, short& my, bool osd);
void DBPS_AddDisc(const char* path);
void DBPS_StartCaptureJoyBind(unsigned port, unsigned device, unsigned index, unsigned id, bool axispos);
bool DBPS_HaveJoy();
bool DBPS_GetJoyBind(unsigned port, unsigned device, unsigned index, unsigned id, bool axispos, std::string& outJoyName, std::string& outBind, const char* prefix);
void DBPS_RequestSaveLoad(bool save, bool load);
bool DBPS_HaveSaveSlot();
const std::string& DBPS_GetContentName();
retro_time_t dbp_cpu_features_get_time_usec(void);
