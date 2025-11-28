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

#ifndef DBP_STANDALONE
enum DBP_OSDMode { DBPOSD_MAIN, DBPOSD_OSK, DBPOSD_MAPPER, _DBPOSD_COUNT, _DBPOSD_OPEN, _DBPOSD_CLOSE };
#else
enum DBP_OSDMode { DBPOSD_MAIN, DBPOSD_OSK, DBPOSD_MAPPER, DBPOSD_SETTINGS, _DBPOSD_COUNT, _DBPOSD_OPEN, _DBPOSD_CLOSE };
#include "dosbox_pure_sta.h"
static void (*dbps_emu_thread_func)();
static DBP_Buffer dbp_osdbuf[3] = {{0,DBPS_OSD_WIDTH,DBPS_OSD_HEIGHT,0,0,0,0,(float)DBPS_OSD_WIDTH/DBPS_OSD_HEIGHT},{0,DBPS_OSD_WIDTH,DBPS_OSD_HEIGHT,0,0,0,0,(float)DBPS_OSD_WIDTH/DBPS_OSD_HEIGHT},{0,DBPS_OSD_WIDTH,DBPS_OSD_HEIGHT,0,0,0,0,(float)DBPS_OSD_WIDTH/DBPS_OSD_HEIGHT}};
#endif
static void DBP_StartOSD(DBP_OSDMode mode = _DBPOSD_OPEN, struct DBP_PureMenuState* in_main = NULL);
static void DBP_CloseOSD();
static bool DBP_FullscreenOSD;
static bool DBP_MenuInterceptorRefreshSystem;

struct DBP_BufferDrawing : DBP_Buffer
{
	enum { CW = 8 }; // char width
	enum { MWIDTH = 234 + 10*4 + 2*3 };
	enum EColors : Bit32u
	{
		BGCOL_SELECTION = 0x117EB7, BGCOL_SCROLL = 0x093F5B, BGCOL_MENU = 0x1A1E20, BGCOL_HEADER = 0x582204, BGCOL_STARTMENU = 0xFF111111,
		COL_MENUTITLE = 0xFFFBD655, COL_CONTENT = 0xFFFFAB91,
		COL_LINEBOX = 0xFFFF7126,
		COL_HIGHLIGHT = 0xFFBDCDFB, COL_NORMAL = 0xFF4DCCF5, COL_DIM = 0xFF4B7A93, COL_WHITE = 0xFFFFFFFF, COL_WARN = COL_LINEBOX, COL_HEADER = 0xFF9ECADE,
		BGCOL_BTNOFF = 0x5F3B27, BGCOL_BTNON = 0xAB6037, BGCOL_BTNHOVER = 0x895133, COL_BTNTEXT = 0xFFFBC6A3,
		BGCOL_KEY = BGCOL_BTNOFF, BGCOL_KEYHOVER = BGCOL_BTNON, BGCOL_KEYPRESS = 0xE46E2E, BGCOL_KEYHELD = 0xC9CB35, BGCOL_KEYOUTLINE = 0x000000, COL_KEYTEXT = 0xFFF8EEE8,
	};

	Bit32u GetThickness() { return (width < (MWIDTH + 10) ? 1 : ((int)width - 10) / MWIDTH); }

	void PrintOutlined(int lh, int x, int y, const char* msg, Bit32u col = 0xFFFFFFFF, Bit32u colol = 0xFF000000)
	{
		if ((colol & 0xFF000000) != 0xFF000000) colol = (((colol >> 24) / 2) << 24) | (colol & 0xFFFFFF);
		for (int i = 0; i < 9; i++) if (i != 4) Print(lh, x+((i%3)-1), y+((i/3)-1), msg, colol);
		Print(lh, x, y, msg, col);
	}

	void PrintCenteredOutlined(int lh, int x, int w, int y, const char* msg, Bit32u col = 0xFFFFFFFF, Bit32u colol = 0xFF000000)
	{
		PrintOutlined(lh, x + (int)((w-strlen(msg)*CW)/2), y, msg, col, colol);
	}

	void Print(int lh, int x, int y, const char* msg, Bit32u col = 0xFFFFFFFF, int maxw = 0xFFFFFF)
	{
		DBP_ASSERT(y >= 0 && y < (int)height);
		const Bit8u* fnt = (lh == 8 ? int10_font_08 : int10_font_14);
		const int ch = (lh == 8 ? 8 : 14);
		for (const char* p = msg, *pEnd = p + (maxw / CW); *p && p != pEnd; x += CW)
		{
			unsigned char c = *(p++);
			if (c >= 128) { c = (unsigned char)15; } // substitute and show each UTF8 byte
			//if (c >= 192) { p++; if (c >= 224 && p != pEnd) p++; if (c >= 240 && p != pEnd) p++; c = (unsigned char)15; } // show UTF8 code points as one character
			DrawChar(fnt, ch, x, y, (char)c, col);
		}
	}

	void DrawChar(const Bit8u* fnt, int ch, int x, int y, int c, Bit32u col = 0xFFFFFFFF)
	{
		if (x < 0 || x + CW >= (int)width) return;
		const Bit8u* ltr = &fnt[(Bit8u)c * ch], *ltrend = ltr + ch;
		Bit32u w = width, *py = video + (w * y) + (x);
		if ((col & 0xFF000000) == 0xFF000000)
			{ for (; ltr != ltrend; py += w, ltr++) for (Bit32u *px = py, bit = 256; bit; px++) if (*ltr & (bit>>=1)) *px = col; }
		else
			{ for (AlphaBlendPrep a(col); ltr != ltrend; py += w, ltr++) for (Bit32u *px = py, bit = 256; bit; px++) if (*ltr & (bit>>=1)) AlphaBlend(px, a); }
	}

	void DrawBox(int x, int y, int w, int h, Bit32u colfill, Bit32u colline)
	{
		if (w < 8) { DBP_ASSERT(false); return; }
		int y1 = y, y2 = y1+4, y4 = y1+h, y3 = y4-4, yBox = y3-y2, yAll = y4-y1;
		int x1 = x, x2 = x1+4, x4 = x1+w, x3 = x4-4, xBox = x3-x2, xAll = x4-x1;
		Bit32u *v = video + width * y1 + x1;
		AlphaBlendFillRect(x1,y1,xAll,yAll, colfill);

		AlphaBlendPrep lna(colline);
		for (Bit32u *p = v+4,       *pEnd = p + xBox, *p2 = p + width * (yAll-1); p != pEnd; p++, p2++) { AlphaBlend(p, lna); AlphaBlend(p2, lna); }
		for (Bit32u *p = v+4*width, *pEnd = p + yBox*width; p != pEnd; p+=width) { AlphaBlend(p, lna); AlphaBlend(p+xAll-1, lna); }

		for (int i = 0; i != 16; i++)
		{
			int a = i % 4, b = i/4, c = a*b<3;
			if (!c) continue;
			AlphaBlend(v+a+b*width, lna);
			AlphaBlend(v+xAll-1-a+b*width, lna);
			AlphaBlend(v+xAll-1-a+(yAll-1-b)*width, lna);
			AlphaBlend(v+a+(yAll-1-b)*width, lna);
		}
	}

	void DrawRect(int x, int y, int w, int h, Bit32u col)
	{
		for (Bit32u *p = video + width * y + x, *pEnd = p + w, *p2 = p + width * (h-1); p != pEnd; p++, p2++) *p = *p2 = col;
		for (Bit32u *p = video + width * y + x, *pEnd = p + width * h; p != pEnd; p+=width) *p = p[w-1] = col;
	}

	void FillRect(int x, int y, int w, int h, Bit32u col)
	{
		for (Bit32u *py = video + width * y + x; h--; py += width)
			for (Bit32u *p = py, *pEnd = p + w; p != pEnd; p++)
				*p = col;
	}

	void AlphaBlendFillRect(int x, int y, int w, int h, Bit32u col)
	{
		AlphaBlendPrep a((((col >> 24) ? (col >> 24) : dbp_alphablend_base) << 24) | (col & 0xFFFFFF));
		for (Bit32u *py = video + width * y + x; h--; py += width)
			for (Bit32u *p = py, *pEnd = p + w; p != pEnd; p++)
				AlphaBlend(p, a);
	}

	void AlphaBlendDrawRect(int x, int y, int w, int h, Bit32u col)
	{
		DBP_ASSERT((col >> 24) == 0xFF || (col >> 24) == 0);
		AlphaBlendPrep a((((col >> 24) ? (col >> 24) : dbp_alphablend_base) << 24) | (col & 0xFFFFFF));
		for (Bit32u *p = video + width * y + x, *pEnd = p + w, *p2 = p + width * (h-1); p != pEnd; p++, p2++) AlphaBlend(p, a), AlphaBlend(p2, a);
		for (Bit32u *p = video + width * y + x, *pEnd = p + width * h; p != pEnd; p+=width) AlphaBlend(p, a), AlphaBlend(p+w-1, a);
	}

	void AlphaBlendFillCircle(int cx, int cy, int rad, Bit32u col)
	{
		AlphaBlendPrep a(col);
		for (int radsq = rad*rad, w = width, y = (cy - rad < 0 ? 0 : cy - rad), ymax = (cy + rad >= (int)height ? (int)height : cy + rad); y < ymax; y++)
			for (int x = (cx - rad < 0 ? 0 : cx - rad), xmax = (cx + rad >= (int)width ? (int)width : cx + rad); x < xmax; x++)
			{
				int dx = abs(x - cx), dy = abs(y - cy), dsq = (dx*dx)+(dy*dy);
				if (dsq >= radsq) continue;
				AlphaBlend(video + y*w + x, a);
			}
	}

	void AlphaBlendDrawCircle(int cx, int cy, int rad1, int rad2, Bit32u col)
	{
		AlphaBlendPrep a(col);
		for (int rad1sq = rad1*rad1, rad2sq = rad2*rad2, w = width, y = (cy - rad2 < 0 ? 0 : cy - rad2), ymax = (cy + rad2 >= (int)height ? (int)height : cy + rad2); y < ymax; y++)
			for (int x = (cx - rad2 < 0 ? 0 : cx - rad2), xmax = (cx + rad2 >= (int)width ? (int)width : cx + rad2); x < xmax; x++)
			{
				int dx = abs(x - cx), dy = abs(y - cy), dsq = (dx*dx)+(dy*dy);
				if (dsq < rad1sq || dsq >= rad2sq) continue;
				AlphaBlend(video + y*w + x, a);
			}
	}

	struct AlphaBlendPrep
	{
		Bit32u na, arb, aag;
		explicit INLINE AlphaBlendPrep(Bit32u col) { Bit32u a = (col & 0xFF000000) >> 24; na = (255 - a); arb = (a * (col & 0x00FF00FF)); aag = (a * (0x01000000 | ((col & 0x0000FF00) >> 8))); }
	};

	static INLINE void AlphaBlend(Bit32u* p1, const AlphaBlendPrep& a)
	{
		*p1 = (((((a.na * (*p1 & 0x00FF00FF)) + a.arb) >> 8) & 0x00FF00FF) | (((a.na * ((*p1 & 0xFF00FF00) >> 8)) + a.aag) & 0xFF00FF00));
	}

	bool DrawButtonAt(Bit32u blend, int btny, int lh, int padu, int padd, int btnx, int btnr, bool on, const struct DBP_MenuMouse& m, const char* txt, Bit32u blendtxt = 0xFF000000);
	INLINE bool DrawButton(Bit32u blend, int btny, int lh, int i, int j, int n, int mrgn, bool on, const struct DBP_MenuMouse& m, const char* txt, Bit32u blendtxt = 0xFF000000)
		{ int w = width-mrgn*2; return DrawButtonAt(blend, btny, lh, 4, 4, (!i ? mrgn : mrgn + (w*i/n + 2)), (j == n ? mrgn + w : mrgn + (w*j/n - 2)), on, m, txt, blendtxt); }
};
DBP_STATIC_ASSERT(sizeof(DBP_BufferDrawing) == sizeof(DBP_Buffer)); // drawing is just for function expansions, we're just casting one to the other

struct DBP_MenuMouse
{
	float x, y, jx1, jy1, jx2, jy2; Bit16u bw, bh; Bit16s mx, my; Bit8s kx, ky, mspeed; Bit8u realmouse : 1, left_pressed : 1, click : 1, right_up : 1, wheel_down : 1, wheel_up : 1, ignoremove : 1;
	DBP_MenuMouse() { memset(this, 0, sizeof(*this)); }
	void Reset() { mspeed = 2; left_pressed = false; if (realmouse) Input(DBPET_MOUSEMOVE, 0, 0, false); }

	void Input(DBP_Event_Type type, int val, int val2, bool joykbd)
	{
		switch (type)
		{
			case DBPET_MOUSEUP:
				if (val == 0) left_pressed = false, click = true; //left
				if (val == 1) right_up = true; //right
				break;
			case DBPET_MOUSEDOWN:
				if (val == 0) left_pressed = true; //left
				else break;
				x = 99999999.f; // force update menu selection on left click
				/* fall through */
			case DBPET_MOUSEMOVE:
				#ifndef DBP_STANDALONE
				mx = dbp_mouse_x;
				my = dbp_mouse_y;
				#else
				DBPS_GetMouse(mx, my, true);
				#endif
				break;
			case DBPET_KEYDOWN:
				switch ((KBD_KEYS)val)
				{
					case KBD_left:  case KBD_kp4: kx = -1; break;
					case KBD_right: case KBD_kp6: kx =  1; break;
					case KBD_up:    case KBD_kp8: ky = -1; break;
					case KBD_down:  case KBD_kp2: ky =  1; break;
					case KBD_kpminus:  wheel_up = true; break; // mouse wheel up
					case KBD_kpplus:   wheel_down = true; break; // mouse wheel down
				}
				break;
			case DBPET_KEYUP:
				switch ((KBD_KEYS)val)
				{
					case KBD_left:  case KBD_kp4: if (kx == -1) kx = 0; break;
					case KBD_right: case KBD_kp6: if (kx ==  1) kx = 0; break;
					case KBD_up:    case KBD_kp8: if (ky == -1) ky = 0; break;
					case KBD_down:  case KBD_kp2: if (ky ==  1) ky = 0; break;
					case KBD_enter: case KBD_kpenter: case KBD_space: if (joykbd) { click = true; } break;
				}
				break;
			case DBPET_JOY1X: jx1 = DBP_GET_JOY_ANALOG_VALUE(val); break;
			case DBPET_JOY1Y: jy1 = DBP_GET_JOY_ANALOG_VALUE(val); break;
			case DBPET_JOY2X: jx2 = DBP_GET_JOY_ANALOG_VALUE(val); break;
			case DBPET_JOY2Y: jy2 = DBP_GET_JOY_ANALOG_VALUE(val); break;
			case DBPET_JOY2DOWN: if (joykbd) { click = true; } break;
			case DBPET_MOUSESETSPEED: mspeed = (val > 0 ? 4 : 1); break;
			case DBPET_MOUSERESETSPEED: mspeed = 2; break;
		}
	}

	bool Update(DBP_BufferDrawing& buf, bool joykbd)
	{
		if (bw != buf.width || bh != buf.height)
		{
			x = (bw ? (x * buf.width / bw) : (buf.width * .5f));
			y = (bh ? (y * buf.height / bh) : (buf.height * .75f));
			bw = buf.width, bh = buf.height;
		}

		if (mx || my)
		{
			float newx = (float)(mx+0x7fff)*buf.width/0xFFFE, newy = (float)(my+0x7fff)*buf.height/0xFFFE;
			mx = my = 0;
			realmouse = true;
			if (newx == x && newy == y) { if (ignoremove) ignoremove = false; return false; }
			x = newx, y = newy;
		}
		else if (jx1 || jx2 || kx || jy1 || jy2 || ky)
		{
			realmouse = false;
			if (!joykbd) { if (ignoremove) ignoremove = false; return false; }
			x += (jx1 + jx2 + kx) * mspeed * buf.width / 320;
			y += (jy1 + jy2 + ky) * mspeed * buf.height / 240;
		}
		else { if (ignoremove) ignoremove = false; return false; }
		if (x <            1) x = (float)1;
		if (x >  buf.width-2) x = (float)(buf.width-2);
		if (y <            1) y = (float)1;
		if (y > buf.height-2) y = (float)(buf.height-2);
		if (ignoremove) { ignoremove = false; return false; }
		return true;
	}

	void Draw(DBP_BufferDrawing& buf, bool joykbd)
	{
		// Draw white mouse cursor with black outline
		click = right_up = wheel_up = wheel_down = false;
		if (!realmouse && !joykbd) return;
		for (Bit32u thick = buf.GetThickness(), midc = 6*thick, maxc = 8*thick, *v = buf.video, w = buf.width, h = buf.height, *pendy = v + w * h, i = 0; i != 9; i++)
		{
			Bit32u n = (i < 4 ? i : (i < 8 ? i+1 : 4)), px = (Bit32u)x + (n%3)-1, py = (Bit32u)y + (n/3)-1, ccol = (n == 4 ? 0xFFFFFFFF : 0xFF000000), *p = v + py * w + px;
			if (p < pendy) for (Bit32u *pp, *pendx = p - px + w, c = 0; c != maxc; c++)
			{
				if (c < midc && (pp = (p + c * w)) < pendy) *pp = ccol; // line down
				if ((pp = (p + c)) < pendx)
				{
					if (c < midc) *pp = ccol; // line right
					if ((pp += c * w) < pendy) *pp = ccol; // line diagonal
				}
			}
		}
	}
};

bool DBP_BufferDrawing::DrawButtonAt(Bit32u blend, int btny, int lh, int padu, int padd, int btnx, int btnr, bool on, const DBP_MenuMouse& m, const char* txt, Bit32u blendtxt)
{
	int btnd = btny+lh+padu+padd, btnw = btnr - btnx;
	bool hover = (m.y >= btny && m.y < btnd && m.x >= btnx && m.x < btnr && m.realmouse);
	DrawBox(btnx, btny, btnw, btnd - btny, (on ? BGCOL_BTNON : (hover ? BGCOL_BTNHOVER : BGCOL_BTNOFF)) | blend, (on ? BGCOL_BTNOFF : BGCOL_BTNON) | blendtxt);
	PrintCenteredOutlined(lh, btnx, btnw, btny+padu, txt, (COL_BTNTEXT & 0xFFFFFF) | blendtxt, blendtxt);
	return (hover && !on);
}

struct DBP_MenuState
{
	enum ItemType : Bit8u { IT_NONE, _IT_CUSTOM, };
	enum Result : Bit8u { RES_NONE, RES_OK, RES_CANCEL, RES_CLOSESCREENKEYBOARD, RES_CHANGEMOUNTS, RES_REFRESHSYSTEM };
	int sel, scroll, joyx, joyy, scroll_jump, mouse_sel, click_sel;
	bool refresh_mousesel, scroll_unlocked, hide_sel, show_popup, toggleosd_down;
	DBP_Event_Type held_event; KBD_KEYS held_key; Bit32u held_ticks, open_ticks;
	struct Item { Bit8u type, pad; Bit16s info; std::string str; INLINE Item() {} INLINE Item(Bit8u t, Bit16s i = 0, const char* s = "") : type(t), info(i), str(s) {} };
	std::vector<Item> list;

	DBP_MenuState() : sel(0), scroll(-1), joyx(0), joyy(0), scroll_jump(0), mouse_sel(-1), click_sel(-1), refresh_mousesel(true), scroll_unlocked(false), hide_sel(false), show_popup(false), toggleosd_down(false), held_event(_DBPET_MAX), open_ticks(DBP_GetTicks()) { }

	void Input(DBP_Event_Type type, int val, int val2)
	{
		Result res = RES_NONE;
		int sel_change = 0, x_change = 0;
		switch (type)
		{
			case DBPET_KEYDOWN:
				switch ((KBD_KEYS)val)
				{
					case KBD_left:  case KBD_kp4: x_change--; break;
					case KBD_right: case KBD_kp6: x_change++; break;
					case KBD_up:    case KBD_kp8: sel_change--; break;
					case KBD_down:  case KBD_kp2: sel_change++; break;
					case KBD_pageup:   sel_change -=    12; break;
					case KBD_pagedown: sel_change +=    12; break;
					case KBD_home:     sel_change -= 99999; break;
					case KBD_end:      sel_change += 99999; break;
					default: if (val <= KBD_m)
					{
						int lckey = (DBP_KBDNAMES[val][0] | 0x20), first = -1;
						for (const Item& it : list)
							if ((it.str.c_str()[0] | 0x20) == lckey)
								{ int i = (int)(&it - &list[0]); if (i > sel) { sel_change = i - sel; break; } else if (first == -1) { first = i; } }
						if (!sel_change && first != -1) sel_change = first - sel;
						type = _DBPET_MAX; // prevent held
						hide_sel = false; // show immediately
					}
				}
				break;
			case DBPET_KEYUP:
				switch ((KBD_KEYS)val)
				{
					case KBD_enter: case KBD_kpenter: case KBD_space: res = RES_OK; break;
					case KBD_esc: case KBD_backspace: res = RES_CANCEL; break;
				}
				if (held_event == DBPET_KEYDOWN) held_event = _DBPET_MAX;
				break;
			case DBPET_TOGGLEOSD: toggleosd_down = true; break;
			case DBPET_TOGGLEOSDUP: if (toggleosd_down) { res = RES_CLOSESCREENKEYBOARD; toggleosd_down = false; } break;
			case DBPET_CHANGEMOUNTS: res = RES_CHANGEMOUNTS; break;
			case DBPET_REFRESHSYSTEM: res = RES_REFRESHSYSTEM; break;
			case DBPET_MOUSEMOVE:
				scroll_unlocked = true;
				break;
			case DBPET_MOUSEDOWN:
				if (val == 0) click_sel = mouse_sel; // can be different than sel
				break;
			case DBPET_MOUSEUP:
				if (val == 0 && (click_sel == sel || show_popup)) res = RES_OK; // left
				if (val == 1) res = RES_CANCEL; // right
				break;
			case DBPET_JOY1X: case DBPET_JOY2X:
				if (joyx <  16000 && val >=  16000) x_change++;
				if (joyx > -16000 && val <= -16000) x_change--;
				if (held_event == type && val > -16000 && val < 16000) held_event = _DBPET_MAX;
				joyx = val;
				break;
			case DBPET_JOY1Y: case DBPET_JOY2Y:
				if (joyy <  16000 && val >=  16000) sel_change += (type == DBPET_JOY1Y ? 1 : 12);
				if (joyy > -16000 && val <= -16000) sel_change -= (type == DBPET_JOY1Y ? 1 : 12);
				if (held_event == type && val > -16000 && val < 16000) held_event = _DBPET_MAX;
				joyy = val;
				break;
			case DBPET_JOY1DOWN: case DBPET_JOY2DOWN: if (val == 0) res = RES_OK; break; // B/Y button
			case DBPET_JOY1UP:   case DBPET_JOY2UP:   if (val == 1) res = RES_CANCEL; break; // A/X button
		}

		if (res && (DBP_GetTicks() - open_ticks) < 200U)
			res = RES_NONE; // ignore already pressed buttons when opening

		if ((sel_change || x_change) && type != _DBPET_MAX)
		{
			if (held_event == _DBPET_MAX) { held_event = type; held_ticks = DBP_GetTicks() + 300; }
			if      (sel_change ==  -1) held_key = KBD_up;
			else if (sel_change ==   1) held_key = KBD_down;
			else if (sel_change == -12) held_key = KBD_pageup;
			else if (sel_change ==  12) held_key = KBD_pagedown;
			else if (x_change   ==  -1) held_key = KBD_left;
			else if (x_change   ==   1) held_key = KBD_right;
			else held_event = _DBPET_MAX;
		}

		DBP_ASSERT(list.size());
		for (int count = (int)list.size(); !res && sel_change && !show_popup;)
		{
			scroll_unlocked = false;
			if (hide_sel) { hide_sel = false; if (list[sel].type != RES_NONE) { break; } }
			sel += sel_change;
			if (sel >= 0 && sel < count) { }
			else if (sel_change > 1) sel = count - 1;
			else if (sel_change == -1) sel = count - 1;
			else sel = scroll = 0;
			if (list[sel].type != RES_NONE) break;
			sel_change = ((sel_change == -1 || sel_change > 1) ? -1 : 1);
		}

		if (hide_sel && res != RES_CANCEL && res != RES_CLOSESCREENKEYBOARD && res != RES_CHANGEMOUNTS && res != RES_REFRESHSYSTEM) return;
		if (sel_change || x_change || res) DoInput(res, (res == RES_OK ? list[sel].type : IT_NONE), x_change);
	}

	void ResetSel(int setsel, bool do_refresh_mousesel = false)
	{
		sel = setsel;
		scroll = -1;
		hide_sel = false;
		refresh_mousesel = do_refresh_mousesel;
	}

	virtual void DoInput(Result res, Bit8u ok_type, int x_change) = 0;

	int DrawMenuBase(DBP_BufferDrawing& buf, Bit32u blend, const int lh, const int rows, const DBP_MenuMouse& m, bool mouseMoved, int menul, int menur, int menuu, bool centerv = false)
	{
		const int count = (int)list.size(), xtra = (lh == 8 ? 0 : 1), scrx = menur - 11, menuh = rows * lh + xtra;
		const bool scrollbar = (count > rows);
		int listu = menuu + ((centerv && rows > count) ? ((rows - count) * lh) / 3 : 0);

		// Update Scroll
		if (scroll == -1)
		{
			scroll = sel - rows/2;
			scroll = ((scroll < 0 || count <= rows) ? 0 : scroll > count - rows ? count - rows : scroll);
			if (!show_popup && m.realmouse) mouseMoved = refresh_mousesel; // refresh when switching tab
		}
		else if (count <= rows) scroll = 0;

		if (!show_popup)
		{
			if (scrollbar)
			{
				if (m.left_pressed && (m.x >= scrx || click_sel == -2) && m.y >= menuu && m.y < menuu+menuh && scroll != -1)
				{
					int scrollh = menuh * rows / count / 2;
					scroll_jump = (((count - rows) * ((int)m.y - menuu - scrollh) / (menuh-scrollh-scrollh)) - scroll);
					click_sel = -2;
				}

				if (m.realmouse && m.y >= menuu && m.y < menuu+menuh)
				{
					if (m.wheel_up)   { scroll_unlocked = true; scroll_jump -= 4; }
					if (m.wheel_down) { scroll_unlocked = true; scroll_jump += 4; }
				}
				if (scroll_jump)
				{
					int old_scroll = scroll;
					scroll += scroll_jump;
					scroll_jump = 0;
					if (scroll < 0) scroll = 0; 
					if (scroll > count - rows) scroll = count - rows;
					sel += (scroll - old_scroll);
				}
				if (!scroll_unlocked)
				{
					if (sel < scroll+     4) scroll = (sel <         4 ?            0 : sel -        4);
					if (sel > scroll+rows-5) scroll = (sel > count - 5 ? count - rows : sel - rows + 5);
				}
			}

			int my = (int)(m.y+0.499f);
			mouse_sel = scroll + (((int)my - listu) / lh);
			if (mouseMoved)
			{
				int mx = (int)(m.x+0.499f);
				sel = mouse_sel;
				if (my < listu) { sel = scroll; hide_sel = true; }
				else if (sel >= count) { sel = count - 1; hide_sel = true; }
				else if (mx >= scrx && scrollbar) { hide_sel = true; }
				else if (my >= listu+rows*lh) { sel = scroll+rows-1; hide_sel = true; }
				else { hide_sel = false; }
				scroll_unlocked = true;
			}
		}

		buf.DrawBox(menul, menuu - 3, menur - menul, menuh + 6, buf.BGCOL_MENU | blend, buf.COL_LINEBOX);

		if (list[sel].type != IT_NONE && !hide_sel && sel >= scroll && (sel - scroll) < rows)
			buf.AlphaBlendFillRect(menul+3, listu + (sel - scroll)*lh, menur - menul - 6 - (scrollbar ? 10 : 0), lh + xtra, buf.BGCOL_SELECTION | blend);

		if (scrollbar)
		{
			int scrollu = menuh * scroll / count, scrolld = menuh * (scroll+rows) / count;
			buf.AlphaBlendFillRect(scrx, menuu, 8, menuh, buf.BGCOL_SCROLL | blend);
			buf.AlphaBlendFillRect(scrx, menuu + scrollu, 8, scrolld - scrollu, buf.BGCOL_SELECTION | blend);
		}
		return listu;
	}

	void UpdateHeld()
	{
		if (held_event == _DBPET_MAX) return;
		Bit32u t = DBP_GetTicks();
		if ((Bit32s)(t - held_ticks) < 60) return;
		held_ticks = (t - held_ticks > 120 ? t : held_ticks + 60);
		Input(DBPET_KEYDOWN, (int)held_key, 1);
	}

	void AddWrappedText(int lineLength, const char* prefix, const char* str, Bit16s info = 0)
	{
		for (const char* line = str, *p, *pmax, *lastspace; *line; line = (*lastspace ? lastspace + 1 : lastspace))
		{
			for (lastspace = NULL, p = line, pmax = p + lineLength; *p && p != pmax;) { if (*(++p) <= ' ' && *(lastspace = p) == '\n') break; }
			if (*line == '\n' && !line[1]) break;
			list.emplace_back(IT_NONE, info, prefix);
			list.back().str.append(line, (size_t)(*line == '\n' ? 0 : (lastspace - line)));
		}
	}
};

#ifdef DBP_STANDALONE
struct DBP_SettingsMenuState final : DBP_MenuState
{
	enum ItemType : Bit8u { IT_CANCEL = _IT_CUSTOM, IT_CATEGORIES, IT_CATEGORY, IT_OPTION, IT_TOGGLE, IT_VALUE };
	Bit8u curmenu;
	DBP_SettingsMenuState() { menu(IT_CATEGORIES); }

	void menu(Bit8u menu_type)
	{
		int setsel = 0;
		Bit16s topinfo    = (     list.size() <= 0   ? 0 : list[0].info);
		Bit16s secondinfo = (     list.size() <= 1   ? 0 : list[1].info);
		Bit16s selinfo    = ((int)list.size() <= sel ? 0 : list[sel].info);
		list.clear();
		if (menu_type == IT_CATEGORIES)
		{
			for (const retro_core_option_v2_category& oc : option_cats)
			{
				if (!oc.key) break;
				if (curmenu == IT_CATEGORY && topinfo == (Bit16s)(&oc - option_cats)) setsel = (int)list.size();
				list.emplace_back(IT_CATEGORY, (Bit16s)(&oc - option_cats), oc.desc);
				AddWrappedText(66, "  ", oc.info);
				list.emplace_back(IT_NONE);
			}
		}
		else if (menu_type == IT_CATEGORY)
		{
			const retro_core_option_v2_category& oc = option_cats[curmenu == IT_CATEGORIES ? selinfo : topinfo];
			list.emplace_back(IT_NONE, (curmenu == IT_CATEGORIES ? selinfo : topinfo), oc.desc);
			AddWrappedText(66, "  ", oc.info);
			for (const retro_core_option_v2_definition& od : option_defs)
			{
				if (od.category_key != oc.key || DBP_Option::GetHidden(od)) continue;
				if ((Bit16s)(&od - option_defs) == (curmenu == IT_CATEGORIES ? 0 : secondinfo)) setsel = (int)(list.size() + 1);
				list.emplace_back(IT_NONE);
				list.emplace_back((od.values[2].value ? IT_OPTION : IT_TOGGLE), (Bit16s)(&od - option_defs), od.desc);
				if (od.info) AddWrappedText(66, "  ", od.info);
			}
			if (curmenu == IT_CATEGORY) setsel = sel;
		}
		else if (menu_type == IT_OPTION)
		{
			const retro_core_option_v2_definition& od = option_defs[selinfo];
			list.emplace_back(IT_NONE, topinfo, od.desc);
			if (od.info) AddWrappedText(66, "  ", od.info, selinfo);
			list.emplace_back(IT_NONE, selinfo);
			retro_variable var = { od.key, od.default_value };
			environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
			for (const retro_core_option_value& ov : od.values)
			{
				if (!ov.value) break;
				if (!strcmp(var.value, ov.value)) setsel = (int)list.size();
				list.emplace_back(IT_VALUE, (Bit16s)(&ov - od.values), (setsel != (int)list.size() ? "( ) " : "(\x7) "));
				list.back().str.append(ov.label ? ov.label : ov.value);
			}
		}
		curmenu = menu_type;
		ResetSel(setsel, (menu_type == IT_CATEGORIES));
	};

	virtual void DoInput(Result res, Bit8u ok_type, int x_change)
	{
		if (res == RES_CANCEL) ok_type = IT_CANCEL;
		if      (ok_type == IT_CATEGORY) menu(IT_CATEGORY);
		else if (ok_type == IT_OPTION) menu(IT_OPTION);
		else if ((ok_type == IT_NONE && curmenu == IT_CATEGORY && x_change && (list[sel].type == IT_OPTION || list[sel].type == IT_TOGGLE)) || ok_type == IT_TOGGLE)
		{
			if (ok_type == IT_TOGGLE) x_change = 1;
			const retro_core_option_v2_definition& od = option_defs[list[sel].info];
			retro_variable var = { od.key, od.default_value };
			environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
			for (const retro_core_option_value& ov : od.values)
			{
				if (ov.value) { if (!strcmp(var.value, ov.value)) { x_change += (int)(&ov - od.values); } continue; }
				var.value = (x_change < 0 ? (&ov)[-1] : od.values[(od.values + x_change) >= &ov ? 0 : x_change]).value;
				if (!strcmp(var.value, od.default_value)) var.value = NULL;
				environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
				break;
			}
		}
		else if (ok_type == IT_VALUE)
		{
			Item& itsel = list[sel];
			const retro_core_option_v2_definition& od = option_defs[list[1].info];
			retro_variable var = { od.key, od.values[itsel.info].value };
			if (!strcmp(var.value, od.default_value)) var.value = NULL;
			environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
			for (Item& it : list)
				if (it.type == IT_VALUE)
					it.str[1] = (it.info == itsel.info ? '\x7' : ' ');
		}
		else if (ok_type == IT_CANCEL && curmenu == IT_OPTION) menu(IT_CATEGORY);
		else if (ok_type == IT_CANCEL && curmenu == IT_CATEGORY) menu(IT_CATEGORIES);
		else if ((ok_type == IT_CANCEL || res == RES_CLOSESCREENKEYBOARD) && !DBP_FullscreenOSD)
		{
			DBP_CloseOSD();
		}
	}

	void DrawMenu(DBP_BufferDrawing& buf, Bit32u blend, int lh, int w, int h, int ftr, bool mouseMoved, const DBP_MenuMouse& m)
	{
		UpdateHeld();
		if (curmenu == IT_CATEGORY)
		{
			Bit16s topinfo = list[0].info;
			const Item *plist = &list[0], *plistend = plist + list.size();
			const retro_core_option_v2_category& oc = option_cats[topinfo];
			for (const retro_core_option_v2_definition& od : option_defs)
			{
				if (od.category_key && (od.category_key != oc.key || DBP_Option::GetHidden(od))) continue;
				while (plist != plistend && (plist->type != IT_OPTION && plist->type != IT_TOGGLE)) plist++;
				if (od.category_key ? (plist != plistend && plist->info == (Bit16s)(&od - option_defs)) : plist == plistend) { plist++; continue; }
				menu(IT_CATEGORY); // visibility changed, refresh
				break;
			}
		}

		int hdr = lh*2, rows = (h - hdr - ftr) / lh, count = (int)list.size(), l = 30, r = w-30;
		if (l < 0) { l = 0, r = w; }
		buf.DrawBox(l + 60, hdr-6-lh, r-l-120, lh+4, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
		buf.PrintCenteredOutlined(lh, 0, w, hdr-lh-4, "DOSBox Pure Settings Menu", buf.COL_MENUTITLE);
		
		DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, l, r, hdr);
		for (int i = scroll, maxw = r-l-27; i != count && i != (scroll + rows); i++)
		{
			Bit8u itype = list[i].type;
			if (itype == IT_OPTION || itype == IT_TOGGLE)
			{
				const retro_core_option_v2_definition& od = option_defs[list[i].info];
				retro_variable var = { od.key, od.default_value };
				environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
				for (const retro_core_option_value& ov : od.values)
				{
					if (!ov.value) break;
					if (strcmp(var.value, ov.value)) continue;
					buf.Print(lh, l+16 + maxw-130, hdr + (i - scroll)*lh, "\x1D", buf.COL_HEADER, 120);
					buf.Print(lh, l+16 + maxw-120, hdr + (i - scroll)*lh, ov.label, buf.COL_NORMAL, 120);
					break;
				}
				buf.Print(lh, l+16, hdr + (i - scroll)*lh, list[i].str.c_str(), buf.COL_NORMAL, maxw - 140);
			}
			else
				buf.Print(lh, l+16, hdr + (i - scroll)*lh, list[i].str.c_str(), (itype != IT_NONE ? buf.COL_NORMAL : buf.COL_HEADER), maxw);
		}
	}
};
#endif

static const Bit8u DBP_MapperJoypadNums[] = { RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3 };
static const char* DBP_MapperJoypadNames[] = { "Up", "Down", "Left", "Right", "B (Down)", "A (Right)", "Y (Left)", "X (Up)", "SELECT", "START", "L", "R", "L2", "R2", "L3", "R3" };

struct DBP_MapperMenuState final : DBP_MenuState
{
	#ifndef DBP_STANDALONE
	enum ItemType : Bit8u { IT_CANCEL = _IT_CUSTOM, IT_PRESET, IT_SELECT, IT_EDIT, IT_ADD, IT_DEL, IT_DEVICE, IT_DIVIDER };
	#else
	enum ItemType : Bit8u { IT_CANCEL = _IT_CUSTOM, IT_PRESET, IT_SELECT, IT_EDIT, IT_ADD, IT_DEL, IT_DEVICE, IT_DIVIDER, IT_BINDJOY };
	#endif
	enum EditMode : Bit8u { NOT_EDITING, EDIT_EXISTING, EDIT_NEW, EDIT_ADDITIONAL };
	int main_sel = 0;
	Bit16s edit_info;
	EditMode edit_mode;
	Bit8u bind_port = 0, bind_dev, changed = 0;

	DBP_MapperMenuState() { menu_top(); }

	~DBP_MapperMenuState() { if (changed) DBP_PadMapping::Save(); }

	enum { JOYPAD_MAX = (sizeof(DBP_MapperJoypadNums)/sizeof(DBP_MapperJoypadNums[0])) };

	DBP_InputBind BindFromPadNum(Bit8u i)
	{
		if (i < JOYPAD_MAX) return { bind_port, RETRO_DEVICE_JOYPAD, 0, DBP_MapperJoypadNums[i], _DBPET_MAX };
		else { Bit8u n = i-JOYPAD_MAX; return { bind_port, RETRO_DEVICE_ANALOG, (Bit8u)(n/4), (Bit8u)(1-(n/2)%2), _DBPET_MAX }; }
	}

	void Add(const DBP_InputBind& b, Bit16s item_info, Bit8u apart = 0, bool* boundActionWheel = NULL)
	{
		int key = -1;
		if (b.evt == DBPET_KEYDOWN)
			key = b.meta;
		else if (b.evt == DBPET_AXISMAPPAIR)
			key = DBP_MAPPAIR_GET(apart?1:-1, b.meta);
		else for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
			if (sm.evt == b.evt && sm.meta == (b.device == RETRO_DEVICE_ANALOG ? (apart ? 1 : -1) : b.meta))
				{ key = DBP_SPECIALMAPPINGS_KEY + (int)(&sm - DBP_SpecialMappings); break; }
		if (key < 0) { DBP_ASSERT(false); return; }

		const char *desc_dev = DBP_GETKEYDEVNAME(key);
		list.emplace_back(IT_EDIT, item_info, "  [Edit]");
		(((list.back().str += (desc_dev ? " " : "")) += (desc_dev ? desc_dev : "")) += ' ') += DBP_GETKEYNAME(key);

		if (boundActionWheel && key == DBP_SPECIALMAPPINGS_ACTIONWHEEL) *boundActionWheel = true;
	}

	void menu_top(int x_change = 0)
	{
		if (x_change)
		{
			int maxport = 1;
			while (maxport != DBP_MAX_PORTS && dbp_port_mode[maxport]) maxport++;
			for (DBP_InputBind& b : dbp_input_binds) if (b.evt == DBPET_SHIFTPORT && b.meta >= maxport) maxport = b.meta + 1;
			bind_port = (bind_port + maxport + x_change) % maxport;
			main_sel = 0;
		}

		list.clear();
		if (DBP_PadMapping::CalcPortMode(bind_port) != DBP_PadMapping::MODE_MAPPER)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, 11, "    Gamepad Mapper is disabled");
			list.emplace_back(IT_NONE, 11, "    for this controller port");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, 11, "    Set 'Use Gamepad Mapper'");
			list.emplace_back(IT_NONE, 11, "    in the Controls menu");
		}
		else
		{
			list.emplace_back(IT_NONE, 0, "Preset: ");
			list.emplace_back(IT_PRESET, 0, "  "); list.back().str += DBP_PadMapping::GetPortPresetName(bind_port);
			list.emplace_back(IT_NONE, 2);

			bool boundActionWheel = false, haveWheelOptions = false;
			for (Bit8u i = 0; i != JOYPAD_MAX + 8; i++)
			{
				bool a = (i>=JOYPAD_MAX); Bit8u apart = (a ? (i-JOYPAD_MAX)%2 : 0);
				const DBP_InputBind pad = BindFromPadNum(i);
				const Bit32u padpdii = PORT_DEVICE_INDEX_ID(pad);
				list.emplace_back(IT_NONE);
				if (!a) list.back().str = DBP_MapperJoypadNames[i];
				else  ((list.back().str = DBP_MapperJoypadNames[2+pad.index]) += " Analog ") += DBP_MapperJoypadNames[(i-JOYPAD_MAX)%4];

				size_t numBefore = list.size();
				for (const DBP_InputBind& b : dbp_input_binds)
					if (PORT_DEVICE_INDEX_ID(b) == padpdii)
						Add(b, (Bit16s)(((&b - &dbp_input_binds[0])<<1)|apart), apart, &boundActionWheel);
				if (list.size() - numBefore == 0) list.emplace_back(IT_ADD, i, "  [Create Binding]");

				if (const char* action = DBP_PadMapping::GetBoundAutoMapButtonLabel(padpdii, a))
				{
					list.emplace_back(IT_NONE, 1, "    Function: ");
					list.back().str.append(action);
				}
			}

			list.emplace_back(IT_NONE, 2);
			list.emplace_back(IT_NONE, 0, "Wheel Options: ");
			for (const DBP_WheelItem& it : dbp_wheelitems)
			{
				if (it.port != bind_port) continue;
				for (int i = 0; i != it.key_count; i++)
				{
					DBP_InputBind bnd = DBP_PadMapping::BindForWheel(bind_port, it.k[i]);
					if (bnd.device == RETRO_DEVICE_NONE) continue;
					Add(bnd, (Bit16s)(&it - &dbp_wheelitems[0] + 1) * -4 - ((4 - i) & 3)); // encode so (info & 3) gives i
				}
				if (const char* action = DBP_PadMapping::GetWheelAutoMapButtonLabel(it))
				{
					list.emplace_back(IT_NONE, 1, "    Function: ");
					list.back().str.append(action);
				}
				list.emplace_back(IT_NONE, 0);
				haveWheelOptions = true;
			}
			list.emplace_back(IT_ADD, -1, "  Add Option");
			if (haveWheelOptions != boundActionWheel)
			{
				list.emplace_back(IT_NONE, 0, "Warning:");
				list.emplace_back(IT_NONE, -1, "  Wheel is inaccessible because no");
				list.emplace_back(IT_NONE, -1, (haveWheelOptions ? "  button was bound to Action Wheel" : "  options have been defined here"));
			}
			list.emplace_back(IT_NONE, 0);
		}
		if (!DBP_FullscreenOSD)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_CANCEL, 0, "    Close Mapper");
		}
		if (main_sel >= (int)list.size()) main_sel = (int)list.size()-1;
		while (main_sel && list[main_sel].type == IT_NONE) main_sel--;
		ResetSel((main_sel < 1 ? 1 : main_sel), (main_sel < 1)); // skip top IT_NONE entry
		edit_mode = NOT_EDITING;
		bind_dev = 0;
	}

	INLINE bool is_mapper_disabled_top() { return list[1].info == 11; } // see menu_top
	INLINE bool is_presets_menu() { return list[0].info == 22; } // see menu_presets

	void menu_presets(Bit16s info)
	{
		main_sel = 0;
		if (info)
		{
			if (info == 9999)
				DBP_PadMapping::FillGenericKeys(bind_port);
			else
				DBP_PadMapping::SetPreset(bind_port, (DBP_PadMapping::EPreset)info);
			changed = true;
			menu_top();
			return;
		}
		bool have_add = false;
		for (Item& it : list) { if (it.type == IT_ADD) { have_add = true; break; } }
		list.clear();
		list.emplace_back(IT_NONE, 22, "Select Preset");
		list.emplace_back(IT_NONE);
		Bit16s off = (dbp_auto_mapping ? 0 : 1), n = 1 + off;
		for (const char* p; (p = DBP_PadMapping::GetPresetName((DBP_PadMapping::EPreset)n)) != NULL;) list.emplace_back(IT_PRESET, n++, p);
		if (have_add)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_PRESET, 9999, "Fill Unbound with Generic Keys");
		}
		if (DBP_PadMapping::IsCustomized(bind_port))
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_DEL, 0, "[Reset Mapping]");
		}
		ResetSel(2 + DBP_PadMapping::GetPreset(bind_port) - 1 - off);
	}

	void menu_devices(Bit8u ok_type)
	{
		if (edit_mode == NOT_EDITING)
		{
			main_sel = sel;
			edit_mode = (ok_type == IT_ADD ? EDIT_NEW : EDIT_EXISTING);
			edit_info = list[sel].info;
			int sel_header = sel - 1; while (list[sel_header].type != IT_NONE) sel_header--;
			(list[0].str = list[sel_header].str);
			list[1].str = std::string(" >") += list[sel].str;
			list[0].type = list[1].type = IT_NONE;
		}
		else if (ok_type == IT_ADD)
		{
			edit_mode = EDIT_ADDITIONAL;
			(list[1].str = " >") += "  [Additional Binding]";
		}
		list.resize(2);
		list.emplace_back(IT_NONE);
		list.emplace_back(IT_DEVICE,   1, "  "); list.back().str += DBPDEV_Keyboard;
		list.emplace_back(IT_DEVICE,   2, "  "); list.back().str += DBPDEV_Mouse;
		list.emplace_back(IT_DEVICE,   3, "  "); list.back().str += DBPDEV_Joystick;
		for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
		{
			if (sm.dev || (sm.evt != DBPET_TOGGLEOSD && edit_info < 0)) continue; // no wheel no port shift as wheel options
			if (sm.evt == DBPET_SHIFTPORT && sm.meta == bind_port) continue;
			list.emplace_back(IT_SELECT, (Bit16s)(DBP_SPECIALMAPPINGS_KEY + (&sm - DBP_SpecialMappings)), "  ");
			list.back().str += sm.name;
		}
		if (edit_mode == EDIT_EXISTING)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_DEL, 0, "  [Remove Binding]");
			int count = 0;
			if (edit_info >= 0) // editing bind
			{
				const DBP_InputBind& edit = dbp_input_binds[edit_info>>1];
				for (const DBP_InputBind& b : dbp_input_binds)
					if (PORT_DEVICE_INDEX_ID(b) == PORT_DEVICE_INDEX_ID(edit))
						count++;
			}
			else // editing wheel
			{
				count = dbp_wheelitems[edit_info / -4 - 1].key_count;
			}
			if (count < 4)
			{
				list.emplace_back(IT_NONE);
				list.emplace_back(IT_ADD, 0, "  [Additional Binding]");
			}
		}
		#ifdef DBP_STANDALONE
		if (edit_info >= 0 && edit_mode != EDIT_ADDITIONAL && DBPS_HaveJoy()) // editing bind (not wheel)
		{
			DBP_InputBind bnd = (edit_mode == EDIT_NEW ? BindFromPadNum((Bit8u)edit_info) : dbp_input_binds[edit_info>>1]);
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_BINDJOY, 0, "  [ Change Controller Input ]");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE);
			if (!DBPS_GetJoyBind(bnd.port, bnd.device, bnd.index, bnd.id, (bool)(edit_info&1), list[list.size()-2].str, list[list.size()-1].str, "  "))
			{
				list.resize(list.size() - 2);
				list.back().str.assign("  [ Assign Controller Input ]");
			}
		}
		#endif
		list.emplace_back(IT_NONE);
		list.emplace_back(IT_CANCEL, 0, "Cancel");

		char device = *(list[1].str.c_str() + (sizeof(" >  [Edit] ")-1));
		ResetSel(device == 'J' ? 5 : (device == 'M' ? 4 : 3));
		bind_dev = 0;
	}

	void menu_keys()
	{
		bind_dev = (Bit8u)list[sel].info;
		(list[2].str = "   >")  += list[sel].str;
		list.resize(3);
		list.emplace_back(IT_NONE);
		if (bind_dev == 1) for (Bit8u i = KBD_NONE + 1; i != KBD_LAST; i++)
		{
			static Bit8u sortedKeys[KBD_f1] = { KBD_NONE,
				KBD_a, KBD_b, KBD_c, KBD_d, KBD_e, KBD_f, KBD_g, KBD_h, KBD_i, KBD_j, KBD_k,
				KBD_l, KBD_m, KBD_n, KBD_o, KBD_p, KBD_q, KBD_r, KBD_s, KBD_t, KBD_u, KBD_v,
				KBD_w, KBD_x, KBD_y, KBD_z,
				KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7, KBD_8, KBD_9, KBD_0,
			};
			Bit8u key = (i < KBD_f1 ? sortedKeys[i] : i);
			list.emplace_back(IT_SELECT, key, "  ");
			list.back().str += DBP_KBDNAMES[key];

			if (const char* mapname = DBP_PadMapping::GetKeyAutoMapButtonLabel(key))
			{
				list.emplace_back(IT_NONE, 0, "    Function: ");
				list.back().str += mapname;
				list.emplace_back(IT_NONE, 0);
			}
		}
		else for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
		{
			if (sm.dev != (bind_dev == 2 ? DBPDEV_Mouse : DBPDEV_Joystick)) continue;
			list.emplace_back(IT_SELECT, (Bit16s)(DBP_SPECIALMAPPINGS_KEY + (&sm - DBP_SpecialMappings)), "  ");
			list.back().str += sm.name;
		}
		list.emplace_back(IT_NONE);
		list.emplace_back(IT_CANCEL, 0, "Cancel");

		ResetSel(4, true); // skip top IT_NONE entry
		if (!strncmp(list[1].str.c_str() + (sizeof(" >  [Edit] ")-1), list[2].str.c_str() + (sizeof("   >   ")-1), list[2].str.size() - (sizeof("   >   ")-1)))
		{
			const char* keyname = list[1].str.c_str() + (sizeof(" >  [Edit] ")-1) + list[2].str.size() - (sizeof("   >   ")-1) + 1;
			for (const Item& it : list)
				if (it.str.size() > 2 && !strcmp(it.str.c_str() + 2, keyname))
					{ ResetSel((int)(&it - &list[0])); break; }
		}
	}

	virtual void DoInput(Result res, Bit8u ok_type, int x_change)
	{
		if (res == RES_CANCEL) ok_type = IT_CANCEL;

		if (x_change && edit_mode == NOT_EDITING)
		{
			menu_top(x_change);
		}
		if ((ok_type == IT_SELECT || ok_type == IT_DEL) && edit_mode != NOT_EDITING)
		{
			if (edit_info >= 0) // editing bind
			{
				bool isNew = (ok_type == IT_SELECT && (edit_mode == EDIT_NEW || edit_mode == EDIT_ADDITIONAL)); // add new entry
				bool isEdit = (ok_type == IT_SELECT && edit_mode == EDIT_EXISTING); // add new entry
				bool isDelete = (ok_type == IT_DEL); // deleting entry
				DBP_InputBind copy = (edit_mode == EDIT_NEW ? BindFromPadNum((Bit8u)edit_info) : dbp_input_binds[edit_info>>1]);
				DBP_PadMapping::EditBind((isNew ? copy : dbp_input_binds[edit_info>>1]), isNew, isEdit, isDelete, (Bit8u)(edit_info&1), (Bit8u)list[sel].info);
			}
			else // editing wheel
			{
				DBP_WheelItem* edit = (edit_mode == EDIT_NEW ? NULL : &dbp_wheelitems[edit_info / -4 - 1]);
				if (ok_type == IT_SELECT && edit_mode == EDIT_NEW) // add new entry
				{
					dbp_wheelitems.push_back({ bind_port, 1, { (Bit8u)list[sel].info } });
				}
				else if (ok_type == IT_SELECT && edit_mode == EDIT_ADDITIONAL)
				{
					edit->k[edit->key_count++] = (Bit8u)list[sel].info;
				}
				else if (ok_type == IT_SELECT && edit_mode == EDIT_EXISTING) // edit entry
				{
					edit->k[edit_info & 3] = (Bit8u)list[sel].info;
				}
				else if (ok_type == IT_DEL) // deleting entry
				{
					if (!--edit->key_count) dbp_wheelitems.erase(dbp_wheelitems.begin() + (edit_info / -4 - 1));
					else memmove(edit->k + (edit_info & 3), edit->k + (edit_info & 3) + 1, 3 - (edit_info & 3));
				}
			}
			changed = true;
			menu_top();
		}
		else if (ok_type == IT_EDIT || ok_type == IT_ADD)
		{
			menu_devices(ok_type);
		}
		else if (ok_type == IT_DEVICE)
		{
			menu_keys();
		}
		#ifdef DBP_STANDALONE
		else if (ok_type == IT_BINDJOY)
		{
			DBP_InputBind bnd = (edit_mode == EDIT_NEW ? BindFromPadNum((Bit8u)edit_info) : dbp_input_binds[edit_info>>1]);
			DBPS_StartCaptureJoyBind(bnd.port, bnd.device, bnd.index, bnd.id, (bool)(edit_info&1));
			list.resize(2);
			list.emplace_back(IT_NONE, 99, "   >  Assign New Input");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, 0, "  Press an input on the controller");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_CANCEL, 0, "Cancel");
			ResetSel(2);
		}
		else if (ok_type == IT_CANCEL && list.size() >= 3 && list[2].info == 99)
		{
			sel = main_sel;
			menu_devices(ok_type);
		}
		#endif
		else if (ok_type == IT_CANCEL && bind_dev)
		{
			menu_devices(ok_type);
		}
		else if (ok_type == IT_CANCEL && edit_mode == EDIT_ADDITIONAL)
		{
			menu_top();
			menu_devices(IT_EDIT);
		}
		else if (ok_type == IT_CANCEL && (edit_mode != NOT_EDITING || is_presets_menu()))
		{
			menu_top();
		}
		else if (ok_type == IT_DEL)
		{
			menu_presets((Bit16s)(DBP_PadMapping::DefaultPreset(bind_port)));
		}
		else if (ok_type == IT_PRESET)
		{
			menu_presets(list[sel].info);
		}
		else if ((ok_type == IT_CANCEL || res == RES_CLOSESCREENKEYBOARD) && !DBP_FullscreenOSD)
		{
			DBP_CloseOSD();
		}
	}

	void DrawMenu(DBP_BufferDrawing& buf, Bit32u blend, int lh, int w, int h, int ftr, bool mouseMoved, const DBP_MenuMouse& m)
	{
		UpdateHeld();
		if ((DBP_PadMapping::CalcPortMode(bind_port) == DBP_PadMapping::MODE_MAPPER) == is_mapper_disabled_top())
			menu_top();

		int hdr = lh*3, rows = (h - hdr - ftr) / lh-1, count = (int)list.size(), l = w/2 - 150, r = w/2 + 150, xtra = (lh == 8 ? 0 : 1), wide = edit_mode == NOT_EDITING && !is_presets_menu() && w > 500;
		if (l < 0) { l = 0, r = w; }
		buf.DrawBox(l, hdr-7-lh*2, r-l, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
		buf.PrintCenteredOutlined(lh, 0, w, hdr-lh*2-5, "Gamepad Mapper", buf.COL_MENUTITLE);

		char num[32];
		sprintf(num, "Controller Port %d", bind_port + 1);
		buf.DrawBox(l-(wide?50:0), hdr-5-lh, r-l+(wide?100:0), lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
		buf.PrintCenteredOutlined(lh, 0, w, hdr-lh-3, num, buf.COL_CONTENT);
		
		if (wide)
		{
			buf.DrawBox(l-100, hdr - 3, 201, rows * lh + 6 + xtra, buf.BGCOL_MENU | blend, buf.COL_LINEBOX);
			DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, l + 100, r + 100, hdr);
			for (int ihdr = -1, i = scroll, inxt, se = (hide_sel ? -1 : sel), maxw = r-l-11; i != count && i != (scroll + rows); i++)
			{
				Bit8u itype = list[i].type;
				if (itype == IT_NONE && !list[i].info) { ihdr = -1; continue; }
				int y = hdr + (i - scroll)*lh;
				if (ihdr == -1)
				{
					for (ihdr = i - 1; list[ihdr].type != IT_NONE; ihdr--) {}
					for (inxt = i + 1; inxt < (int)list.size() && list[inxt].type != IT_NONE; inxt++) {}
					if (list[sel].type != IT_NONE && !hide_sel && sel > ihdr && sel < inxt)
						buf.AlphaBlendFillRect(l-97, y, 195, lh+xtra, buf.BGCOL_SELECTION | blend);
					buf.Print(lh, l-84, y, list[ihdr].str.c_str(), buf.COL_HEADER);
				}
				buf.Print(lh, l+100, y, list[i].str.c_str(), ((i == se || itype == IT_NONE) ? buf.COL_HIGHLIGHT : itype == IT_ADD ? buf.COL_DIM : buf.COL_NORMAL), maxw);
				if (itype == IT_NONE && list[i].info == 2) // draw separator line
				{
					Bit32u *v = buf.video + buf.width * (y + lh) + l - 100;
					for (Bit32u *p = v, *pEnd = p + r+189-l; p != pEnd; p++) *p = buf.COL_LINEBOX;
				}
			}
		}
		else
		{
			DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, l, r, hdr);
			for (int i = scroll, se = (hide_sel ? -1 : sel), maxw = r-l-27; i != count && i != (scroll + rows); i++)
			{
				Bit8u itype = list[i].type;
				buf.Print(lh, l+16, hdr + (i - scroll)*lh, list[i].str.c_str(), (itype != IT_NONE ? (itype == IT_DEL ? buf.COL_WARN : i == se ? buf.COL_HIGHLIGHT : itype == IT_ADD ? buf.COL_DIM : buf.COL_NORMAL) : buf.COL_HEADER), maxw);
				if (itype == IT_NONE && list[i].info == 2) 
				{
					Bit32u *v = buf.video + buf.width * ((hdr + (i - scroll)*lh) + lh/2) + l;
					for (Bit32u *p = v, *pEnd = p + r-12-l; p != pEnd; p++) *p = buf.COL_LINEBOX;
				}
			}
		}

		if (edit_mode == NOT_EDITING && !is_presets_menu())
		{
			int x_change = 0, x1 = l-(wide?50:0), x2 = r-25+(wide?50:0);
			if (buf.DrawButtonAt(blend, hdr-lh-6, lh, 3, 2, x1, x1+25, false, m, "\x1B") && m.click) x_change = -1;
			if (buf.DrawButtonAt(blend, hdr-lh-6, lh, 3, 2, x2, x2+25, false, m, "\x1A") && m.click) x_change = 1;
			if (x_change) menu_top(x_change);

			if (m.y >= 0 && m.y <= hdr)
			{
				if (m.wheel_up) DoInput(RES_NONE, IT_NONE, 1);
				if (m.wheel_down) DoInput(RES_NONE, IT_NONE, -1);
			}
		}
	}
};

struct DBP_OnScreenKeyboardState
{
	enum { KWR = 10, KWTAB = 15, KWCAPS = 20, KWLS = 17, KWRSHIFT = 33, KWCTRL = 16, KWZERO = 22, KWBS = 28, KWSPACEBAR = 88, KWENTR = 18, KWPLUS, KWMAPPER = KWR*4+2*3 };
	enum { KXX = 100+KWR+2, SPACEFF = 109, KSPLIT = 255, KSPLIT1 = 192, KSPLIT2 = 234, KWIDTH = KSPLIT2 + KWR*4 + 2*3 };

	Bit32u pressed_time;
	KBD_KEYS hovered_key, pressed_key;
	bool held[KBD_LAST+1];

	void GFX(DBP_BufferDrawing& buf, const DBP_MenuMouse& mo)
	{
		static const Bit8u keyboard_rows[6][25] = 
		{
			{ KWR, KXX ,KWR,KWR,KWR,KWR,   SPACEFF,   KWR,KWR,KWR,KWR,   SPACEFF,   KWR,KWR,KWR,KWR , KSPLIT , KWR,KWR,KWR , KSPLIT , KWMAPPER },
			{ KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,     KWBS , KSPLIT , KWR,KWR,KWR , KSPLIT , KWR,KWR,KWR,KWR    },
			{ KWTAB, KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWENTR , KSPLIT , KWR,KWR,KWR , KSPLIT , KWR,KWR,KWR,KWPLUS },
			{ KWCAPS, KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,          KSPLIT        ,        KSPLIT , KWR,KWR,KWR        },
			{ KWLS, KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,  KWR,       KWRSHIFT , KSPLIT , KXX,KWR,KXX , KSPLIT , KWR,KWR,KWR,KWPLUS },
			{ KWCTRL, KXX, KWCTRL,                  KWSPACEBAR,                 KWCTRL, KXX, KWCTRL , KSPLIT , KWR,KWR,KWR , KSPLIT , KWZERO ,KWR        },
		};
		static const KBD_KEYS keyboard_keys[6][25] =
		{
			{ KBD_esc,KBD_NONE,KBD_f1,KBD_f2,KBD_f3,KBD_f4,KBD_NONE,KBD_f5,KBD_f6,KBD_f7,KBD_f8,KBD_NONE,KBD_f9,KBD_f10,KBD_f11,KBD_f12,KBD_NONE,KBD_printscreen,KBD_scrolllock,KBD_pause,KBD_NONE,KBD_LAST },
			{ KBD_grave, KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7, KBD_8, KBD_9, KBD_0, KBD_minus, KBD_equals,    KBD_backspace ,KBD_NONE,KBD_insert,KBD_home,KBD_pageup ,KBD_NONE,KBD_numlock,KBD_kpdivide,KBD_kpmultiply,KBD_kpminus },
			{ KBD_tab,KBD_q,KBD_w,KBD_e,KBD_r,KBD_t,KBD_y,KBD_u,KBD_i,KBD_o,KBD_p,KBD_leftbracket,KBD_rightbracket,          KBD_enter ,KBD_NONE,KBD_delete,KBD_end,KBD_pagedown,KBD_NONE,KBD_kp7,KBD_kp8,KBD_kp9,KBD_kpplus },
			{ KBD_capslock,KBD_a,KBD_s,KBD_d,KBD_f,KBD_g,KBD_h,KBD_j,KBD_k,KBD_l,KBD_semicolon,KBD_quote,KBD_backslash                 ,KBD_NONE               ,                 KBD_NONE,KBD_kp4,KBD_kp5,KBD_kp6 },
			{ KBD_leftshift,KBD_extra_lt_gt,KBD_z,KBD_x,KBD_c,KBD_v,KBD_b,KBD_n,KBD_m,KBD_comma,KBD_period,KBD_slash,KBD_rightshift    ,KBD_NONE,   KBD_NONE,KBD_up,KBD_NONE    ,KBD_NONE,KBD_kp1,KBD_kp2,KBD_kp3,KBD_kpenter },
			{ KBD_leftctrl,KBD_NONE,KBD_leftalt,                        KBD_space,                 KBD_rightalt,KBD_NONE,KBD_rightctrl ,KBD_NONE,  KBD_left,KBD_down,KBD_right  ,KBD_NONE,KBD_kp0,KBD_kpperiod },
		};

		DBP_STATIC_ASSERT((int)KWIDTH == (int)buf.MWIDTH);
		int thickness = buf.GetThickness();
		float fx = (buf.width < KWIDTH ? (buf.width - 10) / (float)KWIDTH : (float)thickness);
		float fy = fx * buf.ratio * buf.height / buf.width; if (fy < 1) fy = 1;
		int thicknessy = (int)(fy + .95f);

		int oskx = (int)(buf.width / fx / 2) - (KWIDTH / 2);
		int osky = (mo.y && mo.y < (buf.height / 2) ? 3 : (int)(buf.height / fy) - 3 - 85);

		if (pressed_key && (int)(DBP_GetTicks() - pressed_time) > 500 && pressed_key != KBD_LAST)
		{
			held[pressed_key] = true;
			pressed_key = KBD_NONE;
		}

		// Draw keys and check hovered state
		int cX = (int)mo.x, cY = (int)mo.y;
		hovered_key = KBD_NONE;
		for (int row = 0; row != 6; row++)
		{
			int x = 0, y = (row ? 3 + (row * 10) : 0);
			for (const Bit8u *k = keyboard_rows[row], *k_end = k + 25; k != k_end; k++)
			{
				int draww = *k, drawh = 8;
				switch (*k)
				{
					case KWENTR:
						x += 5;
						drawh = 18;
						break;
					case KWPLUS:
						draww = KWR;
						drawh = 18;
						break;
					case KXX:case SPACEFF:
						x += (*k - 100);
						continue;
					case KSPLIT:
						x = (x < KSPLIT1 ? KSPLIT1 : KSPLIT2);
						continue;
					case 0: continue;
					default: break;
				}

				DBP_ASSERT(draww);
				int rl = (int)((oskx + x) * fx), rr = (int)((oskx + x + draww) * fx), rt = (int)((osky + y) * fy), rb = (int)((osky + y + drawh) * fy);
				bool hovered = (cX >= rl-1 && cX <= rr && cY >= rt-1 && cY <= rb);

				KBD_KEYS kbd_key = keyboard_keys[row][k - keyboard_rows[row]];
				if (hovered) hovered_key = kbd_key;

				buf.AlphaBlendFillRect(rl, rt, rr-rl, rb-rt, (pressed_key == kbd_key ? buf.BGCOL_KEYPRESS : (held[kbd_key] ? buf.BGCOL_KEYHELD : (hovered ? buf.BGCOL_KEYHOVER : buf.BGCOL_KEY))));
				buf.AlphaBlendDrawRect(rl-1, rt-1, rr-rl+2, rb-rt+2, buf.BGCOL_KEYOUTLINE);

				x += (draww + 2);
			}
		}

		// Draw key letters
		static Bit32u keyboard_letters[] = { 1577058307U,1886848880,3790471177U,216133148,985906176,3850940,117534959,1144626176,456060646,34095104,19009569,1078199360,2147632160U,1350912080,85984328,2148442146U,1429047626,77381,3692151160U,3087023553U,2218277763U,250609715,2332749995U,96707584,693109268,3114968401U,553648172,138445064,276889604,152060169,354484736,2148081986U,2072027207,2720394752U,85530487,285483008,8456208,555880480,1073816068,675456032,135266468,1074335764,580142244,112418,3793625220U,3288338464U,1078204481,2265448472U,1954875508,518111744,1955070434,633623176,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,117454849,1879539784,2150631296U,15367,3221282816U,537001987,1208036865,8392705,2102016,151060488,2147549200U,2156923136U,234881028,252228004,1092891456,2818085,2415940610U,8389633,235003932,3222274272U,9444864,1132462094,2818649873U,78141314,2098592,2147497991U,67110912,604110880,2359552,4610,170823736,2429878333U,2751502090U,10486784,2148532224U,67141632,268730432,1077937280,2,10536274,559026848,1075085330,8704,15729152,117473294,1610678368,7868160,968884224,1409292203,25432643,528016,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,58749752,469823556,1078200256,25169922,939638844,0,3087238168U,805797891,2449475456U,142641170,537165826,4720768,75515906,262152,3036676096U,9766672,2416509056U,30556160,62984209,2684616816U,4196608,16814089,128,772972544,268440225,1966272,44059592,301991978,537395328,18876417,268443678,0,1545880276,604045314,1224737280,88089609,268582913,2359552,4203521,3758227460U,1249902720,4752520,1074036752,15278080,31477771,537002056,2097920,58722307,298057840,2534092803U,16779024,983136,0,0,0,0,0,2575232,0,0,262144,0,0,0,0,268435456,1097,0,0,448,0,0,0,0,2300706816U,0,0,268435456,0,0,0,0,0,1451456,0,0,12582912,503341056,3223191664U,2178945537U,4100,131136,0,0,470007826,250848256,302006290,1074004032,5251074,134217730,64,0,37748736,2147500040U,37769856,2013413496,7865984,4195844,268435464,0,0,117471232,3725590584U,134248507,2415984712U,1082132736,2049,131072,0,0,151060488,67785216,151060489,538050592,4723201,8193,128,0,16777216,2147557408U,18932089,67166268,2149843328U,31459585,268435460,0,0,58728448,24,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,538182528,8916736,117475334,1114256,8388608,1515896,12582912,2148532224U,532690947,131665,18878721,369172497,864,553652224,528,15360,8389120,3626977288U,1074790432,35652609,1409499164,0,4005421057U,3221225472U,1073741839,14682112,134831401,2148532480U,75514880,557128,2097152,545952,6291456,2148007936U,2684362752U,268566826,9438464,151031813,537002256,2483028480U,266,3072,524544,1163361284,270401536,4197377,570499086,1073741888,3243438080U,2147483648U,536870913,7343872,8,0,0,0,0,0,0,0,16777216,0,0,0,0,0,0,0,0,7680,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1279262720,1275068480,2,0,0,9408,268451916,2097920,61440,185105920,4866048,0,0,2751463424U,138412036,1610809355,3072,536870930,70537,7496,0,0,30703616,18057216,4027319280U,37748739,553910272,788529186,1,0,0,4848,2113937953,8259552,18432,71573504,2433024,0,0,1375731712,1142947842,2013364228,1536,4026531849U,35609,6308,0,0,25837568,9115648,1074135072,31457280,2097280 };
		for (Bit32u p = 0; p != 59*277; p++)
		{
			if (!(keyboard_letters[p>>5] & (1<<(p&31)))) continue;
			int lx = (int)((oskx + 1 + (p%277)) * fx), ly = (int)((osky + 1 + (p/277)) * fy);
			for (int y = ly; y != ly + thicknessy; y++)
				for (int x = lx; x != lx + thickness; x++)
					*(buf.video + y * buf.width + x) = buf.COL_KEYTEXT;
		}
	}

	void Input(DBP_Event_Type type, int val, int val2)
	{
		switch (type)
		{
			case DBPET_MOUSEDOWN:
				if (val == 0) goto case_ADDKEYDOWN; // only left mouse button
				break;
			case DBPET_MOUSEUP:
				if (val == 0) goto case_ADDKEYUP; // only left mouse button
				break;
			case DBPET_JOY1DOWN:
				if (val == 0 && pressed_key == KBD_NONE) // left face button presses backspace
				{
					pressed_time = DBP_GetTicks() + 10000000u; // don't hold if pressed for 500 milliseconds
					pressed_key = KBD_backspace;
					KEYBOARD_AddKey(KBD_backspace, true);
				}
				else if (val == 1) // top face button toggles shift
				{
					KEYBOARD_AddKey(KBD_leftshift, (held[KBD_leftshift] ^= true));
				}
				break;
			case DBPET_JOY2DOWN: case_ADDKEYDOWN:
				if (pressed_key == KBD_NONE && hovered_key != KBD_NONE)
				{
					if (held[hovered_key])
					{
						held[hovered_key] = false;
						KEYBOARD_AddKey(hovered_key, false);
					}
					else if (hovered_key >= KBD_leftalt && hovered_key <= KBD_rightshift)
					{
						held[hovered_key] = true;
						KEYBOARD_AddKey(hovered_key, true);
					}
					else
					{
						pressed_time = DBP_GetTicks();
						pressed_key = hovered_key;
						if (pressed_key != KBD_LAST) KEYBOARD_AddKey(pressed_key, true);
					}
				}
				break;
			case DBPET_JOY1UP: case DBPET_JOY2UP: case_ADDKEYUP:
				if (pressed_key != KBD_NONE && pressed_key != KBD_LAST)
				{
					KEYBOARD_AddKey(pressed_key, false);
					pressed_key = KBD_NONE;
				}
				else if (pressed_key == KBD_LAST)
				{
					DBP_StartOSD(DBPOSD_MAPPER); // deletes 'this'
					return;
				}
				break;
			case DBPET_KEYDOWN:
				switch ((KBD_KEYS)val)
				{
					case KBD_enter: case KBD_kpenter: case KBD_space: goto case_ADDKEYDOWN;
				}
				break;
			case DBPET_KEYUP:
				switch ((KBD_KEYS)val)
				{
					case KBD_enter: case KBD_kpenter: case KBD_space: goto case_ADDKEYUP;
					case KBD_esc: goto case_CLOSEOSK;
				}
				break;
			case DBPET_TOGGLEOSD: case_CLOSEOSK:
				pressed_key = KBD_NONE;
				memset(held, 0, sizeof(held));
				DBP_CloseOSD();
				break;
		}
	}
};

struct DBP_PureMenuState final : DBP_MenuState
{
	#ifndef DBP_STANDALONE
	enum ItemType : Bit8u { IT_RUN = _IT_CUSTOM, IT_MOUNT, IT_BOOTIMG, IT_BOOTIMG_MACHINE, IT_BOOTOSLIST, IT_BOOTOS, IT_INSTALLOSSIZE, IT_INSTALLOS, IT_SHELLLIST, IT_RUNSHELL, _IT_NO_AUTOBOOT, IT_MAINMENU, IT_COMMANDLINE, IT_CLOSEOSD, IT_VARIANTLIST, IT_VARIANTTOGGLE, IT_VARIANTRUN, IT_VARIANTRUN_RESET_CONFLICTS, IT_VARIANTRUN_KEEP_CONFLICTS, IT_SYSTEMREFRESH };
	#else
	enum ItemType : Bit8u { IT_RUN = _IT_CUSTOM, IT_MOUNT, IT_BOOTIMG, IT_BOOTIMG_MACHINE, IT_BOOTOSLIST, IT_BOOTOS, IT_INSTALLOSSIZE, IT_INSTALLOS, IT_SHELLLIST, IT_RUNSHELL, _IT_NO_AUTOBOOT, IT_MAINMENU, IT_COMMANDLINE, IT_CLOSEOSD, IT_VARIANTLIST, IT_VARIANTTOGGLE, IT_VARIANTRUN, IT_VARIANTRUN_RESET_CONFLICTS, IT_VARIANTRUN_KEEP_CONFLICTS, IT_SYSTEMREFRESH, IT_FILEBROWSE, IT_SAVELOAD, IT_EXITDBPS };
	#endif
	enum { INFO_HEADER = 0x0A, INFO_WARN = 0x0B, INFO_DIM = 0xC };

	int exe_count, fs_rows, windowlinelength;
	bool multidrive;
	Bit8u popupsel;
	ItemType listmode;

	DBP_PureMenuState() : exe_count(0), fs_rows(0), windowlinelength(60), multidrive(false), popupsel(0)
	{
		if (dbp_game_running) INT10_SetCursorShape(0x1e, 0); // hide blinking cursor
		RefreshList(IT_MAINMENU); // always read exe_count and fs_rows, even if switching menu below
		if (DBP_Run::autoboot.startup.mode != DBP_Run::RUN_NONE)
		{
			if      (DBP_Run::autoboot.startup.mode == DBP_Run::RUN_EXEC)    GoToSubMenu(IT_MAINMENU,    IT_RUN, 0, &DBP_Run::autoboot.startup.exec);
			else if (DBP_Run::autoboot.startup.mode == DBP_Run::RUN_BOOTOS)  GoToSubMenu(IT_BOOTOSLIST,  IT_BOOTOS,          DBP_Run::autoboot.startup.info);
			else if (DBP_Run::autoboot.startup.mode == DBP_Run::RUN_SHELL)   GoToSubMenu(IT_SHELLLIST,   IT_RUNSHELL,        DBP_Run::autoboot.startup.info);
			else if (DBP_Run::autoboot.startup.mode == DBP_Run::RUN_BOOTIMG) GoToSubMenu(IT_BOOTIMG,     IT_BOOTIMG_MACHINE, DBP_Run::autoboot.startup.info);
			else if (DBP_Run::autoboot.startup.mode == DBP_Run::RUN_VARIANT) RefreshList(IT_VARIANTLIST);
			else { DBP_ASSERT(false); }
			if (DBP_Run::autoboot.startup.mode == DBP_Run::RUN_VARIANT) DBP_Run::autoboot.use = false; // DBP_Run::WriteAutoBoot force enables auto boot for RUN_VARIANT
		}
		else if (patchDrive::variants.Len()) RefreshList(IT_VARIANTLIST);
	}

	~DBP_PureMenuState()
	{
		if (dbp_game_running) INT10_SetCursorShape((Bit8u)(CurMode->cheight - (CurMode->cheight>=14 ? 3 : 2)), (Bit8u)(CurMode->cheight - (CurMode->cheight>=14 ? 2 : 1))); // reset blinking cursor
	}

	void DrawMenu(DBP_BufferDrawing& buf, Bit32u blend, int lh, int w, int h, int ftr, bool mouseMoved, const DBP_MenuMouse& m)
	{
		UpdateHeld();

		buf.DrawBox( w/10, 5, w-w/5, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
		buf.DrawBox( 8, lh+7, w-16, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);

		buf.PrintCenteredOutlined(lh, 0, w, 7, "DOSBOX PURE START MENU", buf.COL_MENUTITLE);
		buf.PrintCenteredOutlined(lh, 0, w, 7+lh+2, (!dbp_content_name.empty() ? dbp_content_name.c_str() : "- No Content Loaded -"), buf.COL_CONTENT);

		int inforow = (w > 319), hdr = lh*2+12, rows = (h - hdr - ftr) / lh - inforow, count = (int)list.size(), bot = hdr + rows * lh + 3 - (lh == 8 ? 1 : 0);
		int listu = DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, 8, w - 8, hdr, (list[0].type == IT_NONE));
		bool autoboot_show = (DBP_Run::autoboot.use && listmode != IT_VARIANTLIST && (list[sel].type < _IT_NO_AUTOBOOT || hide_sel) && first_shell);
		windowlinelength = (w - 80) / buf.CW;

		for (int i = scroll, se = (hide_sel ? -1 : sel); i != count && i != (scroll + rows); i++)
		{
			const DBP_MenuState::Item& item = list[i];
			int y = listu + (i - scroll)*lh, strlen = (int)item.str.length();
			if (item.type == IT_MOUNT) // mountable file system
			{
				bool mounted = dbp_images[item.info].mounted;
				int xdiff = (w - buf.CW*(strlen + 7)), mntx = (xdiff >= 0 ? xdiff / 2 : w - (buf.CW*9)), strx = (xdiff >= 0 ? mntx + (buf.CW*7) : xdiff - (buf.CW*3));
				buf.Print(lh, mntx + ((mounted && xdiff >= 0) ? buf.CW : 0), y, (mounted ? "EJECT" : "INSERT"), (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
				buf.Print(lh, strx, y, item.str.c_str(), (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
			}
			else if (item.type == IT_RUN || item.type == IT_BOOTOS || item.type == IT_BOOTIMG_MACHINE || item.type == IT_RUNSHELL)
			{
				int off = ((item.type != IT_RUN || multidrive) ? 0 : 3), len = strlen - off, lblx = (w - buf.CW*len) / 2;
				buf.Print(lh, lblx,       y, item.str.c_str() + off, (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
				if (i != se) continue;
				buf.Print(lh, lblx - buf.CW*(2      ), y, "*", buf.COL_WHITE);
				buf.Print(lh, lblx + buf.CW*(len + 1), y, "*", buf.COL_WHITE);
				if (autoboot_show) buf.Print(lh, lblx + buf.CW*(len + 1), y, "* [SET AUTO START]", buf.COL_WHITE);
			}
			else buf.Print(lh, (w - buf.CW*strlen) / 2, y, item.str.c_str(), (item.type != IT_NONE ? (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL) : (item.info == INFO_HEADER ? buf.COL_HEADER : (item.info == INFO_WARN ? buf.COL_WARN : (item.info == INFO_DIM ? buf.COL_DIM : buf.COL_NORMAL)))));
		}

		if (inforow)
		{
			char skiptext[38];
			if (!autoboot_show) skiptext[0] = '\0';
			else if (DBP_Run::autoboot.skip == -1) snprintf(skiptext, sizeof(skiptext), "Skip showing the text console");
			else if (DBP_Run::autoboot.skip) snprintf(skiptext, sizeof(skiptext), "Skip showing first %d frames", DBP_Run::autoboot.skip);
			#ifndef DBP_STANDALONE
			else snprintf(skiptext, sizeof(skiptext), "SHIFT/L2/R2 + Restart to come back");
			#else
			else snprintf(skiptext, sizeof(skiptext), "Auto Start is Enabled");
			#endif

			if (w > 639)
			{
				buf.DrawBox(8, bot, w-319, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
				buf.PrintCenteredOutlined(lh, 8, w-319, bot+2, skiptext, buf.COL_BTNTEXT);
			}
			else if (w > 320)
			{
				buf.DrawBox(8, bot, w-319, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
			}
			
			if (w < 640 && autoboot_show)
			{
				buf.DrawBox(8, bot, w-16, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
				buf.PrintCenteredOutlined(lh, 0, w, bot+2, skiptext, buf.COL_BTNTEXT);
			}
			else
			{
				buf.DrawBox(w-68, bot, 60, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
				buf.DrawBox(w-217, bot, 150, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
				buf.DrawBox(w-312, bot, 96, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
				buf.PrintCenteredOutlined(lh, w-68, 60, bot+2, "\x7 Run", buf.COL_BTNTEXT);
				buf.PrintCenteredOutlined(lh, w-217, 150, bot+2, "\x1A\x1B Set Auto Start", buf.COL_BTNTEXT);
				buf.PrintCenteredOutlined(lh, w-312, 96, bot+2, "\x18\x19 Scroll", buf.COL_BTNTEXT);
			}

			if (m.y >= bot && m.y <= bot+lh+3)
			{
				if (m.click || m.wheel_up) DoInput(RES_NONE, IT_NONE, 1);
				if (m.right_up || m.wheel_down) DoInput(RES_NONE, IT_NONE, -1);
			}
		}

		if (show_popup)
		{
			int halfw = w/2, boxw = (w < 640 ? halfw-16 : halfw/2+8);
			buf.DrawBox(halfw-boxw, h/2-lh*3, boxw*2, lh*6+8, buf.BGCOL_HEADER | 0xFF000000, buf.COL_LINEBOX);
			buf.PrintCenteredOutlined(lh, 0, w, h/2-lh*2, (w < 320 ? "Reset DOS to" : "Are you sure you want to reset DOS"), buf.COL_BTNTEXT);
			#ifdef DBP_STANDALONE
			if (list[sel].type == IT_EXITDBPS)
				buf.PrintCenteredOutlined(lh, 0, w, h/2-lh+2, (w < 320 ? "exit DOSBox?" : "and shut down DOSBox Pure?"), buf.COL_BTNTEXT);
			else
			#endif
			buf.PrintCenteredOutlined(lh, 0, w, h/2-lh+2, (w < 320 ? "start this?" : "to start the selected application?"), buf.COL_BTNTEXT);
			if (m.realmouse) popupsel = 0;
			if (buf.DrawButton(0x80000000, h/2+lh*1, lh, 1, 2, 4, 0, !m.realmouse && popupsel == 1, m, "OK"))     popupsel = 1;
			if (buf.DrawButton(0x80000000, h/2+lh*1, lh, 2, 3, 4, 0, !m.realmouse && popupsel == 2, m, "CANCEL")) popupsel = 2;
		}
	}

	bool GoToSubMenu(ItemType mode, ItemType subtype, int find_info = -1, std::string* find_string = nullptr)
	{
		if (mode != listmode) RefreshList(mode);
		for (const Item& it : list)
			if (it.type == subtype && (find_string ? (it.str == *find_string) : (it.info == find_info)))
				{ ResetSel((int)(&it - &list[0])); return true; }
		return false;
	}

	static const char* GetVariantSection(const std::string& v, int get_sec, int& out_len, int& out_total_secs)
	{
		int sec = 1; for (const char *p = v.c_str(), *l, *n, *res = NULL;; p = n, sec++)
		{
			for (; *p != '\0' && (*p <= ' ' || *p == '+'); p++) {}
			for (n = p, l = n; *n != '\0' && *n != '+'; n++) { if (*n != ' ') l = n; }
			if (get_sec-- == 0) { res = p; out_len = (int)(l + 1 - p); }
			if (*n == '\0') { out_total_secs = sec - (int)(n == p); return res; }
		}
	}

	void AddFSRow(Bit8u type, Bit16s info, const char* str, bool add_spacer = false)
	{
		if (add_spacer && fs_rows) { list.emplace_back(IT_NONE); fs_rows++; }
		list.emplace_back(type, info, str);
		fs_rows++;
	}

	static void ClearSortOrderPrefix(std::string& str)
	{
		for (size_t i = str.find('#'), j; i != std::string::npos; i = str.find('#', j))
		{
			for (j = i; j && str[j-1] >= '0' && str[j-1] <= '9';) j--;
			if (j != i) str.erase(j, i-j+1); else j++;
		}
	}

	void RefreshList(ItemType mode, bool keep_sel_pos = false)
	{
		int old_sel = sel, setsel = 0;
		listmode = mode;
		list.clear();
		#ifndef DBP_STANDALONE
		if (!DBP_FullscreenOSD && mode != IT_MAINMENU && dbp_images.size())
		#else
		if (!DBP_FullscreenOSD && mode != IT_MAINMENU && mode != IT_FILEBROWSE && dbp_images.size())
		#endif
		{
			for (DBP_Image& image : dbp_images) list.emplace_back(IT_MOUNT, (Bit16s)(&image - &dbp_images[0]), DBP_Image_Label(image));
			list.emplace_back(IT_NONE);
		}
		if (!first_shell) // Show menu after DOS crashed
		{
			list.emplace_back(IT_NONE, INFO_HEADER, "DOS Crashed");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_MAINMENU, 0, "Reset");
		}
		else if (mode == IT_MAINMENU)
		{
			int cd_count = 0, hd_count = 0, boot_rows = 0; bool bootimg = false;

			fs_rows = 0;
			for (DBP_Image& image : dbp_images)
			{
				AddFSRow(IT_MOUNT, (Bit16s)(&image - &dbp_images[0]), DBP_Image_Label(image));
				(DBP_Image_IsCD(image) ? cd_count : hd_count)++;
				if (image.image_disk) bootimg = true;
			}

			bool bootinstall = ((Drives['D'-'A'] && dynamic_cast<isoDrive*>(Drives['D'-'A']) && ((isoDrive*)(Drives['D'-'A']))->CheckBootDiskImage()) || (hd_count == 1 && cd_count == 1));
			if (bootimg)                                  AddFSRow(IT_BOOTIMG,       0, "[ Boot from Disk Image ]",                  (!boot_rows++));
			if (!dbp_strict_mode && dbp_osimages.size())  AddFSRow(IT_BOOTOSLIST,    0, "[ Run Installed Operating System ]",        (!boot_rows++));
			if (!dbp_strict_mode && dbp_shellzips.size()) AddFSRow(IT_SHELLLIST,     0, "[ Run System Shell ]",                      (!boot_rows++));
			if (!dbp_strict_mode && bootinstall)          AddFSRow(IT_INSTALLOSSIZE, 0, "[ Boot and Install New Operating System ]", (!boot_rows++));

			#ifdef DBP_STANDALONE
			AddFSRow(IT_FILEBROWSE,    0, "[ Load Content ]",     (!boot_rows++));
			if (dbp_game_running) AddFSRow(IT_SAVELOAD,  0, "[ Saving & Loading ]", (!boot_rows++)); // todo
			#endif

			if (patchDrive::variants.Len())
			{
				AddFSRow(IT_NONE, INFO_WARN, "Active Configuration: ", true);
				list.back().str.append(!DBP_Run::patch.enabled_variant ? "Default" : patchDrive::variants.GetStorage()[DBP_Run::patch.enabled_variant - 1].c_str());
				if (DBP_Run::patch.enabled_variant) ClearSortOrderPrefix(list.back().str);
				AddFSRow(IT_VARIANTLIST, 0, "[ Select Game Configuration ]");
			}

			if (fs_rows) { list.emplace_back(IT_NONE); fs_rows++; }

			// Scan drive C first, any others after
			sel = ('C'-'A'); // use sel to have access to it in FileIter
			exe_count = 0;
			DriveFileIterator(Drives[sel], FileIter, (Bitu)this);
			for (sel = ('A'-'A'); sel != ('Z'-'A'); sel++)
			{
				if (sel == ('C'-'A') || !Drives[sel]) continue;
				DriveFileIterator(Drives[sel], FileIter, (Bitu)this);
				multidrive = true;
			}
			if (exe_count) list.emplace_back(IT_NONE);

			if (DBP_FullscreenOSD || (cd_count + hd_count) <= 1) setsel = (patchDrive::variants.Len() ? (fs_rows - 2) : (exe_count ? fs_rows : (int)dbp_images.size()));
			if (!exe_count) { list.emplace_back(IT_NONE, INFO_DIM, "No executable file found"); list.emplace_back(IT_NONE); }

			if (!dbp_strict_mode) list.emplace_back(IT_COMMANDLINE, 0, "Go to Command Line");
			if (!DBP_FullscreenOSD) list.emplace_back(IT_CLOSEOSD, 0, "Close Menu");
			if (list.back().type == IT_NONE) list.pop_back();

			#ifdef DBP_STANDALONE
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_EXITDBPS, 0, "Exit DOSBox Pure");
			#endif
		}
		else if (mode == IT_BOOTIMG)
		{
			list.emplace_back(IT_NONE, INFO_HEADER, "Select Boot System Mode");
			list.emplace_back(IT_NONE);
			for (const char* it : DBP_MachineNames) list.emplace_back(IT_BOOTIMG_MACHINE, (Bit16s)(it[0]|0x20), it);
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_MAINMENU, 0, "Cancel");
			const std::string& img = (!dbp_images.empty() ? dbp_images[dbp_image_index].path : list[1].str);
			bool isPCjrCart = (img.size() > 3 && (img[img.size()-3] == 'J' || img[img.size()-2] == 'T'));
			char wantmchar = (isPCjrCart ? 'p' : *(const char*)control->GetProp("dosbox", "machine")->GetValue());
			for (const Item& it : list) { if (it.info == wantmchar) { setsel = (int)(&it - &list[0]); break; } }
		}
		else if (mode == IT_INSTALLOSSIZE)
		{
			const char* filename;
			std::string osimg = DBP_GetSaveFile(SFT_NEWOSIMAGE, &filename);
			list.emplace_back(IT_NONE, INFO_HEADER, "Hard Disk Size For Install");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, INFO_WARN, "Create a new hard disk image in the following location:");
			if (filename > &osimg[0]) { list.emplace_back(IT_NONE, INFO_WARN); list.back().str.assign(&osimg[0], filename - &osimg[0]); }
			list.emplace_back(IT_NONE, INFO_WARN, filename);
			list.emplace_back(IT_NONE);
			char buf[128];
			for (Bit16s sz = 16/8; sz <= 64*1024/8; sz += (sz < 4096/8 ? sz : (sz < 32*1024/8 ? 4096/8 : 8192/8)))
			{
				list.emplace_back(IT_INSTALLOS, sz, (sprintf(buf, "%3d %cB Hard Disk", (sz < 1024/8 ? sz*8 : sz*8/1024), (sz < 1024/8 ? 'M' : 'G')),buf));
				if (sz == 2048/8)
				{
					list.emplace_back(IT_NONE);
					list.emplace_back(IT_NONE, INFO_WARN, "Hard disk images over 2GB will be formatted with FAT32");
					list.emplace_back(IT_NONE, INFO_WARN, "NOTE: FAT32 is only supported in Windows 95C and newer");
					list.emplace_back(IT_NONE);
				}
			}
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_INSTALLOS, 0, "[ Boot Only Without Creating Hard Disk Image ]");
			setsel = (filename > &osimg[0] ? 11 : 10);
		}
		else if (mode == IT_BOOTOSLIST)
		{
			list.emplace_back(IT_NONE, INFO_HEADER, "Select Operating System Disk Image");
			list.emplace_back(IT_NONE);
			for (const std::string& im : dbp_osimages)
				{ list.emplace_back(IT_BOOTOS, (Bit16s)(&im - &dbp_osimages[0])); list.back().str.assign(im.c_str(), im.size()-4); }
			if (dbp_system_cached) { list.emplace_back(IT_NONE); list.emplace_back(IT_SYSTEMREFRESH, 0, "[ Refresh List ]"); }
			char ramdisk = DBP_Option::Get(DBP_Option::bootos_ramdisk)[0];
			if (ramdisk == 't')
			{
				list.emplace_back(IT_NONE);
				list.emplace_back(IT_NONE, INFO_WARN, "Changes made to the C: drive will be discarded");
			}
			else if (ramdisk == 'd')
			{
				const char *save_c_name; std::string save_c = DBP_GetSaveFile(SFT_DIFFDISK, &save_c_name);
				list.emplace_back(IT_NONE);
				list.emplace_back(IT_NONE, INFO_WARN, "Changes made to the C: drive will be stored in the following location:");
				if (save_c_name > &save_c[0]) { list.emplace_back(IT_NONE, INFO_WARN); list.back().str.assign(&save_c[0], save_c_name - &save_c[0]); }
				list.emplace_back(IT_NONE, INFO_WARN, save_c_name);
			}
			list.emplace_back(IT_NONE);
			const char* dfreespace = DBP_Option::Get(DBP_Option::bootos_dfreespace); // can also be "discard" or "hide"
			bool mountedHDIMage = false;
			for (DBP_Image& i : dbp_images) { if (!DBP_Image_IsCD(i)) { mountedHDIMage = true; break; } }
			if (mountedHDIMage && (imageDiskList['D'-'A'] || imageDiskList['E'-'A'] || (!DOSBox_Boot && imageDiskList['C'-'A'])))
				list.emplace_back(IT_NONE, INFO_WARN, "D: will be based on the loaded .IMG file");
			else if (atoi(dfreespace))
			{
				const char *save_d_name; std::string save_d = DBP_GetSaveFile(SFT_VIRTUALDISK, &save_d_name);
				list.emplace_back(IT_NONE, INFO_WARN, "Changes made to the D: drive will be stored in the following location:");
				if (save_d_name > &save_d[0]) { list.emplace_back(IT_NONE, INFO_WARN); list.back().str.assign(&save_d[0], save_d_name - &save_d[0]); }
				list.emplace_back(IT_NONE, INFO_WARN, save_d_name);
			}
			else if (dfreespace[0] != 'h')
				list.emplace_back(IT_NONE, INFO_WARN, "Changes made to the D: drive will be discarded");
			else
				list.emplace_back(IT_NONE, INFO_WARN, "D: hard drive was disabled, use only mounted CDs");
		}
		else if (mode == IT_SHELLLIST)
		{
			list.emplace_back(IT_NONE, INFO_HEADER, "Select System Shell");
			list.emplace_back(IT_NONE);
			for (const std::string& im : dbp_shellzips)
				{ list.emplace_back(IT_RUNSHELL, (Bit16s)(&im - &dbp_shellzips[0])); list.back().str.assign(im.c_str(), im.size()-5); }
			if (dbp_system_cached) { list.emplace_back(IT_NONE); list.emplace_back(IT_SYSTEMREFRESH, 0, "[ Refresh List ]"); }
		}
		else if (mode == IT_VARIANTLIST)
		{
			#ifdef DBP_STANDALONE
			list.emplace_back(IT_FILEBROWSE, 0, "[ Load Content ]");
			list.emplace_back(IT_SAVELOAD, 0, "[ Saving & Loading ]");
			list.emplace_back(IT_NONE);
			#endif

			list.emplace_back(IT_NONE, INFO_HEADER, "Select Game Configuration");
			list.emplace_back(IT_NONE);
			const std::vector<std::string>& vars = patchDrive::variants.GetStorage();
			int list_base = (int)list.size(), max_secs = 1, sec, secbegin, secend, i, enabled_var = DBP_Run::patch.enabled_variant, expect_vars = 1;
			if (0) { sections_are_imbalanced: max_secs = 0; list.resize((unsigned)list_base); }
			for (sec = 0, secbegin = secend = (int)list.size();; secbegin = secend, sec++)
			{
				int sec_len, total_secs, max_sec_len = 0, have_enabled_toggle = 0;
				for (const std::string& v : vars)
				{
					const int var_num = patchDrive::variants.GetStorageIndex(&v) + 1;
					const char* sec_name = GetVariantSection(v, sec, sec_len, total_secs);
					if (total_secs <= 1 || max_secs == 0)
					{
						if (!total_secs || !sec_name) continue;
						for (i = secend; i != (int)list.size(); i++)
						{
							int str_len = (int)list[i].str.length(), c = memcmp(list[i].str.c_str(), sec_name, (sec_len < str_len ? sec_len : str_len));
							if (c > 0 || (!c && str_len > sec_len)) break; // found sorted insert index i
						}
						list.emplace(list.begin() + i, IT_VARIANTRUN, (Bit16s)var_num, sec_name);
					}
					else
					{
						if (total_secs != max_secs) { if (max_secs != 1) goto sections_are_imbalanced; max_secs = total_secs; }
						for (i = secbegin; i != secend; i++)
						{
							int str_len = (int)list[i].str.length() - 4, c = memcmp(list[i].str.c_str() + 4, sec_name, (sec_len < str_len ? sec_len : str_len));
							if (!c && sec_len == str_len) { list[i].pad++; goto skipadd; } // count uses
							if (c > 0 || (!c && str_len > sec_len)) break; // found sorted insert index i
						}
						if (sec_len > max_sec_len) max_sec_len = sec_len;
						{ Item& it = *list.emplace(list.begin() + i, IT_VARIANTTOGGLE, (Bit16s)(var_num - 1), "( ) "); it.pad = 1; it.str.append(sec_name, sec_len); }
						secend++;
						skipadd:
						if (var_num == enabled_var) { list[i].str[1] = '\x7'; have_enabled_toggle = 1; }
					}
				}
				if (max_secs > 1)
				{
					if (secend - secbegin < 2) goto sections_are_imbalanced;
					int defaultpos = secbegin, minuse = 0xFFFF, have_vars = 1; // have var starts at 1 due to Default config
					for (i = secbegin; i != secend; i++) { int use = list[i].pad; if (use < minuse) { minuse = use; defaultpos = i; } have_vars += use; }
					std::swap(list[defaultpos], list[secbegin]);
					if (!have_enabled_toggle) list[secbegin].str[1] = '\x7';
					for (i = secbegin; i != secend; i++) { Item& it = list[i]; it.pad = (Bit8u)sec; it.str.append(max_sec_len + 4 - it.str.length(), ' '); }
					if ((expect_vars *= (secend - secbegin)) != have_vars && sec == max_secs - 1) goto sections_are_imbalanced;
					list.emplace(list.begin() + secend++, IT_NONE);
				}
				if (sec >= max_secs - 1) break;
			}
			int list_size = (int)list.size();
			for (setsel = 0, i = secend; i != list_size; i++) { if (list[i].info == enabled_var) { setsel = i; break; } }
			for (i = list_base; i != list_size; i++) ClearSortOrderPrefix(list[i].str); // Remove N# or NN# fixed sort order prefixes
			if (sec)
			{
				list.emplace(list.begin() + secend, IT_VARIANTRUN, (setsel ? 0 : DBP_Run::patch.enabled_variant), "Run Game");
				if (secend != list_size) list.emplace(list.begin() + (secend + 1), IT_NONE); // mix of sections and other options, add divider
				setsel = (setsel ? (setsel + 2) : secend); // fix for added rows
			}
			else if (DBP_Run::patch.show_default)
			{
				list.emplace(list.begin() + secbegin, IT_VARIANTRUN, 0, "Default"); // no sections, add default start
				setsel = (setsel ? (setsel + 1) : secbegin); // fix for added row
			}
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_MAINMENU, 0, "Run Other Program");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, INFO_DIM, "The configuration can be switched with the On-Screen Menu");
			#ifndef DBP_STANDALONE
			list.emplace_back(IT_NONE, INFO_DIM, "or by holding shift or L2/R2 when restarting the core");
			#endif
		}
		#ifdef DBP_STANDALONE
		else if (mode == IT_FILEBROWSE)
		{
			struct retro_vfs_interface_info vfs = { 3, NULL };
			environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs);

			#ifdef WIN32
			if (DBPS_BrowsePath.length() == 1 && *DBPS_BrowsePath.c_str() == '$') // Windows drive selection
			{
				char drvpath[4] = { 'C', ':', '\\', 0 };
				for (unsigned drives = GetLogicalDrives(); *drvpath <= 'Z'; (*drvpath)++)
				{
					if (!(drives & (1 << (*drvpath - 'A')))) continue;
					list.emplace_back(IT_FILEBROWSE, -2); list.back().str.assign(drvpath, 2);
				}
			}
			else
			#endif
			{
				retro_system_info info;
				retro_get_system_info(&info);
				const char* validexts = info.valid_extensions;
				std::string strext;

				if (DBPS_BrowsePath.empty()) Cross::MakePathAbsolute(DBPS_BrowsePath);
				else Cross::NormalizePath(DBPS_BrowsePath);
				struct retro_vfs_dir_handle *dir = vfs.iface->opendir((DBPS_BrowsePath.empty() ? "." : DBPS_BrowsePath.c_str()), false);
				bool have_up = false;
				setsel = -1;
				while (dir && vfs.iface->readdir(dir))
				{
					const char* name = vfs.iface->dirent_get_name(dir), *ext, *valid;
					size_t ln = strlen(name);
					bool go_up;
					Bit16s info;
					if (vfs.iface->dirent_is_dir(dir) && (ln != 1 || *name != '.'))
						{ have_up |= (go_up = !strcmp(name, "..")); info = (go_up ? 1 : 2); if (go_up) { name = "[ Go Up ]"; } }
					else if ((ext = strrchr(name, '.')) != NULL && (valid = strstr(validexts, lowcase(&strext.assign(ext + 1)[0]))) != NULL && (valid == validexts || valid[-1] == '|') && (valid[strext.size()] == '|' || valid[strext.size()] == '\0'))
						info = ((setsel == -1 && ext - name == dbp_content_name.size() && !strncasecmp(name, dbp_content_name.c_str(), dbp_content_name.size())) ? 4 : 3);
					else continue;
					int insert_index = (int)list.size();
					for (const Item& it : list) { if (it.info > info || (it.info == info && strcasecmp(it.str.c_str(), name) > 0)) { insert_index = (int)(&it - &list[0]); break; } }
					list.emplace(list.begin() + insert_index, IT_FILEBROWSE, (info == 4 ? 3 : info), name);
					if (info == 4) setsel = insert_index; else if (setsel >= insert_index) setsel++;
				}
				if (dir) vfs.iface->closedir(dir);
				#ifdef WIN32
				if (!have_up && DBPS_BrowsePath.length() != 1) list.emplace(list.begin(), IT_FILEBROWSE, -1, "[ Go Up ]"); // go to drive selection
				#endif
				if (setsel == -1) setsel = 1;
				list.emplace_back(IT_FILEBROWSE, 4, "[ Load this folder as content ]");
			}
			if (list.empty()) list.emplace_back(IT_NONE, INFO_HEADER, "No files");
		}
		else if (mode == IT_SAVELOAD)
		{
			bool have = DBPS_HaveSaveSlot();
			list.emplace_back(IT_NONE, INFO_HEADER, "Saving and Loading");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, INFO_DIM, "Save Name: "); list.back().str.append(dbp_content_name);
			list.emplace_back(IT_SAVELOAD, 2, "Save Slot: "); list.back().str += '0' + DBPS_SaveSlotIndex;
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_SAVELOAD, 3, "Save");
			list.emplace_back((have ? (Bit8u)IT_SAVELOAD : (Bit8u)IT_NONE), (have ? 4 : INFO_DIM), "Load");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_MAINMENU, 0, "Back");
		}
		#endif
		else { DBP_ASSERT(false); }

		if (keep_sel_pos && old_sel < (int)list.size()) sel = old_sel;
		else { while (setsel < (int)list.size() - 1 && list[setsel].type == IT_NONE) setsel++; ResetSel(setsel); }
	}

	static void FileIter(const char* path, bool is_dir, Bit32u size, Bit16u, Bit16u, Bit8u, Bitu data)
	{
		if (is_dir) return;
		DBP_PureMenuState* m = (DBP_PureMenuState*)data;
		const char* fext = strrchr(path, '.');
		if (!fext++ || (strcmp(fext, "EXE") && strcmp(fext, "COM") && strcmp(fext, "BAT"))) return;
		m->exe_count++;

		std::string entry;
		entry.reserve(3 + (fext - path) + 3 + 1);
		(((entry += ('A' + m->sel)) += ':') += '\\') += path;

		// insert into menu list ordered alphabetically
		Item item = { IT_RUN };
		int insert_index = m->fs_rows;
		for (; insert_index != (int)m->list.size(); insert_index++)
			if (m->list[insert_index].str > entry) break;
		m->list.insert(m->list.begin() + insert_index, item);
		m->list[insert_index].str.swap(entry);
	}

	virtual void DoInput(Result res, Bit8u ok_type, int auto_change)
	{
		if (show_popup)
		{
			if (auto_change) popupsel = (Bit8u)(auto_change < 0 ? 1 : 2);
			if (res == DBP_MenuState::RES_CANCEL) show_popup = false;
			if (!ok_type) return;
			if (popupsel != 1) { show_popup = false; return; }
		}
		Item& item = list[sel];
		if (auto_change && listmode != IT_VARIANTLIST && (item.type < _IT_NO_AUTOBOOT|| hide_sel))
		{
			int& skip = DBP_Run::autoboot.skip; // skip steps 0, -1, 10, 20, 30, ...
			if (DBP_Run::autoboot.use && auto_change > 0) skip += (!skip ? -1 : skip == -1 ? 11 : skip < 50 ? 10 : skip < 150 ? 25 : skip < 300 ? 50 : 100);
			if (!DBP_Run::autoboot.use && auto_change > 0) DBP_Run::autoboot.use = true;
			if (auto_change < 0) skip -= (!skip ? 2 : skip == -1 ? -1 : skip == 10 ? 11 : skip <= 50 ? 10 : skip <= 150 ? 25 : skip <= 300 ? 50 : 100);
			if (skip < -1) { DBP_Run::autoboot.use = false; skip = 0; }
		}

		if (!first_shell) // Show menu after DOS crashed
		{
			if (ok_type) { DBP_CloseOSD(); DBP_ForceReset(true); return; }
		}
		else if (ok_type == IT_MOUNT)
		{
			if (dbp_images[item.info].mounted)
				DBP_Unmount(dbp_images[item.info].drive);
			else
				DBP_Mount((unsigned)item.info);
			RefreshList(listmode, true);
		}
		else if (ok_type == IT_INSTALLOSSIZE || ok_type == IT_BOOTOSLIST || ok_type == IT_SHELLLIST || ok_type == IT_VARIANTLIST)
		{
			RefreshList((ItemType)ok_type);
		}
		#ifdef DBP_STANDALONE
		else if (ok_type == IT_FILEBROWSE)
		{
			#ifdef WIN32
			if      (item.info == -1) DBPS_BrowsePath.assign("$"); // Windows drive selection
			else if (item.info == -2) DBPS_BrowsePath.assign(item.str); // Windows drive root
			else if (item.info == 1 && DBPS_BrowsePath.length() == 2 && DBPS_BrowsePath[1] == ':') DBPS_BrowsePath.assign("$"); // Root of mapped network drive
			else
			#endif
			if      (item.info == 1) // parent dir
			{
				DBPS_BrowsePath.append((DBPS_BrowsePath.empty() ? 0 : 1), CROSS_FILESPLIT).append("..");
			}
			else if (item.info == 2) // subdir
			{
				DBPS_BrowsePath.append((DBPS_BrowsePath.empty() ? 0 : 1), CROSS_FILESPLIT).append(item.str);
			}
			else if (item.info >= 3) // file or current directory
			{
				const char* fname = (item.info >= 5 ? list[1] : item).str.c_str(), *fext;
				if (item.info == 5) // mount cd/disk image
				{
					std::string imgpath; imgpath.assign(DBPS_BrowsePath).append((DBPS_BrowsePath.empty() ? 0 : 1), CROSS_FILESPLIT).append(fname);
					DBP_Mount(DBP_AppendImage(imgpath.c_str(), false));
					RefreshList(IT_MAINMENU);
				}
				else if (item.info != 6 && !dbp_content_name.empty() && (fext = strrchr(fname, '.'))++ && (!strcasecmp(fext, "ISO") || !strcasecmp(fext, "CHD") || !strcasecmp(fext, "CUE") || !strcasecmp(fext, "INS") || !strcasecmp(fext, "IMG") || !strcasecmp(fext, "IMA") || !strcasecmp(fext, "VHD") || !strcasecmp(fext, "JRC")))
				{
					std::string fname; fname.swap(item.str);
					list.clear();
					list.emplace_back(IT_NONE);
					list.emplace_back(IT_NONE, INFO_WARN); fname.swap(list.back().str);
					list.emplace_back(IT_NONE);
					list.emplace_back(IT_FILEBROWSE, 5, "[ Mount as CD/DISK Image ]");
					list.emplace_back(IT_FILEBROWSE, 6, "[ Load as Content ]");
					ResetSel(3);
				}
				else
				{
					if (!show_popup && (dbp_game_running || (first_shell->bf && !first_shell->bf->IsAutoexec()))) { popupsel = 0; show_popup = true; return; } // confirm
					if (item.info == 3 || item.info == 6) // load file
						dbp_content_path.assign(DBPS_BrowsePath).append((DBPS_BrowsePath.empty() ? 0 : 1), CROSS_FILESPLIT).append(fname);
					else // load current directory
						dbp_content_path.assign(DBPS_BrowsePath.empty() ? "." : DBPS_BrowsePath);
					dbp_wasloaded = false; // signal new content to init_dosbox
					DBP_OnBIOSReboot();
				}
				return;
			}
			RefreshList(IT_FILEBROWSE);
		}
		else if (ok_type == IT_SAVELOAD)
		{
			if (item.info == 2) // save slot selection
				DBPS_SaveSlotIndex = (DBPS_SaveSlotIndex + 1) % 10;
			else if (item.info == 3) // save
				{ DBPS_RequestSaveLoad(true, false); DBP_CloseOSD(); return; }
			else if (item.info == 4) // load
				{ DBPS_RequestSaveLoad(false, true); DBP_CloseOSD(); return; }
			RefreshList(IT_SAVELOAD, (listmode == IT_SAVELOAD));
		}
		else if (listmode == IT_SAVELOAD && auto_change)
		{
			DBPS_SaveSlotIndex = (DBPS_SaveSlotIndex + 10000 + auto_change) % 10;
			RefreshList(IT_SAVELOAD, true);
		}
		else if (ok_type == IT_EXITDBPS)
		{
			if (!show_popup && (dbp_game_running || (first_shell->bf && !first_shell->bf->IsAutoexec()))) { popupsel = 0; show_popup = true; return; } // confirm
			DBP_DOSBOX_ForceShutdown();
		}
		#endif
		else if (ok_type == IT_BOOTIMG)
		{
			// Machine property was fixed by dbp_reboot_machine or DOSBOX.CONF and cannot be modified here, so automatically boot the image as is
			if (control->GetProp("dosbox", "machine")->IsFixed()) goto handle_result;
			RefreshList(IT_BOOTIMG);
		}
		else if (ok_type == IT_VARIANTTOGGLE)
		{
			for (Item& it : list) if (it.pad == item.pad && it.type == IT_VARIANTTOGGLE) it.str[1] = ' ';
			item.str[1] = '\x7';
			DBP_Run::patch.enabled_variant = 0; // default config
			for (const std::string& v : patchDrive::variants.GetStorage())
			{
				const char* sec_name;
				for (int sec = 0, sec_len, it_len, total_secs; (sec_name = GetVariantSection(v, sec, sec_len, total_secs)) != NULL && total_secs > 1; sec++)
				{
					for (Item& it : list)
					{
						if (it.type != IT_VARIANTTOGGLE || it.pad != sec || it.str[1] != '\x7') continue;
						const char* it_name = GetVariantSection(patchDrive::variants.GetStorage()[it.info], sec, it_len, total_secs);
						if (!it_name || sec_len != it_len || memcmp(it_name, sec_name, sec_len)) goto next_variant;
						break;
					}
					if (sec == total_secs - 1) { DBP_Run::patch.enabled_variant = (Bit16s)(patchDrive::variants.GetStorageIndex(&v) + 1); goto matched_variant; }
				}
				next_variant:;
			}
			matched_variant:;
			for (Item& it : list) { if (it.type == IT_VARIANTRUN) { it.info = DBP_Run::patch.enabled_variant; break; } }
		}
		else if (ok_type == IT_SYSTEMREFRESH)
		{
			DBP_MenuInterceptorRefreshSystem = true;
		}
		else if (res == RES_REFRESHSYSTEM || (res == RES_CANCEL && list.back().type == IT_VARIANTLIST))
		{
			RefreshList(listmode);
		}
		else if (((res == RES_CANCEL && list.back().type == IT_CLOSEOSD) || res == RES_CLOSESCREENKEYBOARD) && !DBP_FullscreenOSD)
		{
			ok_type = IT_CLOSEOSD;
			goto handle_result;
		}
		else if (ok_type == IT_MAINMENU || (res == RES_CANCEL && list.back().type != IT_CLOSEOSD))
		{
			// Go to top menu (if in submenu) or refresh list
			RefreshList(IT_MAINMENU);
		}
		else if (ok_type == IT_VARIANTRUN)
		{
			Bit16s variant = item.info; // store before clear
			std::vector<std::string> conflicts = patchDrive::VariantConflictFiles(variant, false);
			if (conflicts.empty()) goto handle_result;

			list.clear();
			AddWrappedText(windowlinelength, "", "The selected configuration can't be applied because the settings in the files below have been customized:", INFO_WARN);
			list.emplace_back(IT_NONE);
			for (const std::string& path : conflicts) list.emplace_back(IT_NONE, 0, path.c_str());
			list.emplace_back(IT_NONE);
			AddWrappedText(windowlinelength, "", "To use the configuration as intended the files need to be reset, but any changes made to them by you will be lost.", INFO_DIM);
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_VARIANTRUN_RESET_CONFLICTS, variant, "[ Yes, reset these files ]");
			list.emplace_back(IT_VARIANTRUN_KEEP_CONFLICTS, variant, "[ No, keep my custom files as is ]");
			list.emplace_back(IT_VARIANTLIST, 0, "[ Cancel ]");
			ResetSel((int)list.size() - 2);
		}
		else if (ok_type)
		{
			handle_result:
			if (dbp_strict_mode && (ok_type == IT_BOOTOS || ok_type == IT_INSTALLOS || ok_type == IT_RUNSHELL || ok_type == IT_COMMANDLINE || (ok_type == IT_CLOSEOSD && DBP_FullscreenOSD))) return;
			if (ok_type != IT_CLOSEOSD)
			{
				DBP_ASSERT(item.type == ok_type && (ok_type == IT_RUN || ok_type == IT_BOOTIMG || ok_type == IT_BOOTIMG_MACHINE || ok_type == IT_BOOTOS || ok_type == IT_INSTALLOS || ok_type == IT_RUNSHELL || ok_type == IT_COMMANDLINE || ok_type == IT_VARIANTRUN || ok_type == IT_VARIANTRUN_RESET_CONFLICTS || ok_type == IT_VARIANTRUN_KEEP_CONFLICTS));
				if (!show_popup && (dbp_game_running || (first_shell->bf && !first_shell->bf->IsAutoexec()))) { popupsel = 0; show_popup = true; return; } // confirm
				if (ok_type == IT_VARIANTRUN_RESET_CONFLICTS) { ok_type = IT_VARIANTRUN; patchDrive::VariantConflictFiles(item.info, true); }
				if (ok_type == IT_VARIANTRUN_KEEP_CONFLICTS) { ok_type = IT_VARIANTRUN; }

				if (!DBP_Run::Run(
					ok_type == IT_RUN             ? DBP_Run::RUN_EXEC :
					ok_type == IT_BOOTIMG         ? DBP_Run::RUN_BOOTIMG:
					ok_type == IT_BOOTIMG_MACHINE ? DBP_Run::RUN_BOOTIMG :
					ok_type == IT_BOOTOS          ? DBP_Run::RUN_BOOTOS :
					ok_type == IT_INSTALLOS       ? DBP_Run::RUN_INSTALLOS :
					ok_type == IT_RUNSHELL        ? DBP_Run::RUN_SHELL :
					ok_type == IT_VARIANTRUN      ? DBP_Run::RUN_VARIANT :
					ok_type == IT_COMMANDLINE     ? DBP_Run::RUN_COMMANDLINE :
						DBP_Run::RUN_NONE, item.info, item.str, true)) { show_popup = false; return RefreshList(IT_MAINMENU); }

				// Show menu again on image boot when machine needs to change but there are EXE files (some games need to run a FIX.EXE before booting)
				if (ok_type == IT_BOOTIMG_MACHINE && dbp_reboot_machine && exe_count && !DBP_Run::autoboot.use)
					DBP_Run::startup.mode = DBP_Run::RUN_NONE;
			}
			DBP_CloseOSD();
		}
		else if (res == RES_CHANGEMOUNTS)
		{
			RefreshList(listmode, true);
		}
	}
};

static bool DBP_MenuInterceptorFirstTime;
struct DBP_MenuInterceptor : DBP_Interceptor
{
	void StartIntercept() { if (dbp_intercept_next != this) { DBP_MenuInterceptorFirstTime = true; DBP_SetIntercept(this); } }
	void EndIntercept() { if (dbp_intercept_next == this) { DBP_SetIntercept(NULL); } }

	virtual void input() override
	{
		// Use fixed mappings using only port 0 to use in the menu and the on-screen keyboard
		static DBP_InputBind intercept_binds[] =
		{
			{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   DBPET_MOUSEDOWN, 0 },
			{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  DBPET_MOUSEDOWN, 1 },
			{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, DBPET_MOUSEDOWN, 2 },
			{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,   DBPET_KEYDOWN, KBD_kpminus },
			{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN, DBPET_KEYDOWN, KBD_kpplus  },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, DBPET_TOGGLEOSD },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    DBPET_KEYDOWN, KBD_up },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  DBPET_KEYDOWN, KBD_down },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  DBPET_KEYDOWN, KBD_left },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, DBPET_KEYDOWN, KBD_right },
			{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, DBPET_JOY1X },
			{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, DBPET_JOY1Y },
			{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, DBPET_JOY2X },
			{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, DBPET_JOY2Y },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, DBPET_JOY1DOWN, 0 },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, DBPET_JOY1DOWN, 1 },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, DBPET_JOY2DOWN, 0 },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, DBPET_JOY2DOWN, 1 },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, DBPET_KEYDOWN, KBD_grave },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, DBPET_KEYDOWN, KBD_tab },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, DBPET_KEYDOWN, KBD_esc },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  DBPET_KEYDOWN, KBD_enter },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, DBPET_MOUSESETSPEED,  1 },
			{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, DBPET_MOUSESETSPEED, -1 },
		};

		if (DBP_MenuInterceptorFirstTime)
		{
			DBP_MenuInterceptorFirstTime = false;

			// Just query values (without emitting events) when entering intercepted screen
			for (DBP_InputBind& b : intercept_binds)
				b.lastval = input_state_cb(b.port, b.device, b.index, b.id);

			// Release all keys when switching between key event intercepting
			DBP_ReleaseKeyEvents(false);
			return;
		}

		static bool warned_game_focus;
		if (!warned_game_focus && input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK))
		{
			for (Bit8u i = KBD_NONE + 1; i != KBD_LAST; i++)
			{
				if (!input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, dbp_keymap_dos2retro[i])) continue;
				warned_game_focus = true;
				emuthread_notify(10000, LOG_WARN,
					"Detected keyboard and joypad being pressed at the same time.\n"
					"To freely use the keyboard without hotkeys enable 'Game Focus' (Scroll Lock key by default) if available.");
				break;
			}
		}

		// query input states and generate input events (ignore mouse on touchpad mode)
		for (DBP_InputBind* b = intercept_binds + (dbp_mouse_input == 'p' ? 3 : 0), *bEnd = intercept_binds + sizeof(intercept_binds)/sizeof(*intercept_binds); b != bEnd; b++)
		{
			Bit16s val = input_state_cb(b->port, b->device, b->index, b->id);
			if (val != b->lastval) b->Update(val);
		}

		if (DBP_MenuInterceptorRefreshSystem)
		{
			DBP_ScanSystem(true);
			DBP_MenuInterceptorRefreshSystem = false;
		}

		// Instantly refresh input binds when modified
		if (dbp_binds_changed)
			DBP_PadMapping::SetInputDescriptors();
	}

	virtual void close() override
	{
		// Get latest values (without emitting events) when leaving intercepted screen
		for (DBP_InputBind& b : dbp_input_binds)
			b.lastval = input_state_cb(b.port, b.device, b.index, b.id);

		// Release all keys when switching between key event intercepting
		DBP_ReleaseKeyEvents(false);
	}
};

struct DBP_OnScreenDisplay : DBP_MenuInterceptor
{
	DBP_OSDMode mode;
	char last_map, default_shown;
	#ifndef DBP_STANDALONE
	union { DBP_PureMenuState* main; DBP_OnScreenKeyboardState* osk; DBP_MapperMenuState* mapper; void* _all; } ptr;
	#else
	union { DBP_PureMenuState* main; DBP_OnScreenKeyboardState* osk; DBP_MapperMenuState* mapper; DBP_SettingsMenuState* settings; void* _all; } ptr;
	#endif
	DBP_MenuMouse mouse;

	void SetMode(DBP_OSDMode in_mode, DBP_PureMenuState* in_main = NULL)
	{
		switch (mode)
		{
			case DBPOSD_MAIN:     delete ptr.main; break;
			case DBPOSD_OSK:      delete ptr.osk; break;
			case DBPOSD_MAPPER:   delete ptr.mapper; break;
			#ifdef DBP_STANDALONE
			case DBPOSD_SETTINGS: delete ptr.settings; break;
			#endif
		}
		if (in_mode != _DBPOSD_OPEN && in_mode != _DBPOSD_CLOSE) { mode = in_mode; if (!ptr._all) default_shown = 0; }
		else if (in_mode == _DBPOSD_OPEN && (!default_shown || last_map != dbp_map_osd)) { mode = ((last_map = dbp_map_osd) == 'k' ? DBPOSD_OSK : DBPOSD_MAIN); default_shown = true; }
		ptr._all = NULL;
		StartIntercept();
		switch (in_mode == _DBPOSD_CLOSE ? _DBPOSD_CLOSE : mode)
		{
			case DBPOSD_MAIN:
				ptr.main = (in_main ? in_main : new DBP_PureMenuState());
				if (!ptr.main->refresh_mousesel) mouse.ignoremove = true;
				break;
			case DBPOSD_OSK: ptr.osk = new DBP_OnScreenKeyboardState(); break;
			case DBPOSD_MAPPER: ptr.mapper = new DBP_MapperMenuState(); break;
			#ifdef DBP_STANDALONE
			case DBPOSD_SETTINGS: ptr.settings = new DBP_SettingsMenuState(); break;
			#endif
			case _DBPOSD_CLOSE:
				EndIntercept();
				break;
		}
		DBP_KEYBOARD_ReleaseKeys();
	}

	virtual void gfx(DBP_Buffer& _buf) override
	{
		DBP_BufferDrawing& buf = static_cast<DBP_BufferDrawing&>(_buf);
		int w = buf.width, h = buf.height, lh = (h >= 400 ? 14 : 8), ftr = lh+20;
		bool isOSK = (mode == DBPOSD_OSK);
		bool mouseMoved = mouse.Update(buf, isOSK);

		if (1) // now always show button bar, was (DBP_FullscreenOSD || !isOSK) before
		{
			int btny = h - 13 - lh, btnmrgn = (w >= 296 ? 34 : 8), ydist = (isOSK ? (int)(127 - (btny - mouse.y) / (h*0.001f)) : 0xFF);
			Bit32u btnblend = (DBP_FullscreenOSD ? 0xFF000000 : ((ydist > dbp_alphablend_base ? dbp_alphablend_base : (ydist < 30 ? 30 : ydist)) << 24));
			Bit32u txtblend = (DBP_FullscreenOSD ? 0xFF000000 : ((ydist*2 > 0xFF ? 0xFF : (ydist < 30 ? 30 : ydist*2)) << 24));
			Bit8u orgrealmouse = mouse.realmouse;
			mouse.realmouse |= isOSK; // allow button presses with joystick in OSK
			if (DBP_FullscreenOSD) buf.FillRect(0, 0, w, h, buf.BGCOL_STARTMENU);
			if (w >= 296 && buf.DrawButtonAt(btnblend, btny, lh, 4, 4, 8, 30, false,                 mouse, "L",                                           txtblend) && mouse.click) evnt(DBPET_KEYUP, KBD_grave, 0);
			#ifndef DBP_STANDALONE
			int osk = (int)(first_shell && !DBP_FullscreenOSD), mpr = (int)!!first_shell, n = 1+osk+mpr;
			if (       buf.DrawButton(btnblend, btny, lh,   0, 1, n, btnmrgn, mode == DBPOSD_MAIN,   mouse, (w < 500 ? "START"    : "START MENU"),         txtblend) && mouse.click) SetMode(DBPOSD_MAIN);
			if (osk && buf.DrawButton(btnblend, btny, lh,   1, 2, n, btnmrgn, mode == DBPOSD_OSK,    mouse, (w < 500 ? "KEYBOARD" : "ON-SCREEN KEYBOARD"), txtblend) && mouse.click) SetMode(DBPOSD_OSK);
			if (mpr && buf.DrawButton(btnblend, btny, lh, n-1, n, n, btnmrgn, mode == DBPOSD_MAPPER, mouse, (w < 500 ? "CONTROLS" : "CONTROLLER MAPPER"),  txtblend) && mouse.click) SetMode(DBPOSD_MAPPER);
			#else
			int osk = 3*(int)(first_shell && !DBP_FullscreenOSD), mpr = 3*(int)!!first_shell, n = 4+osk+mpr;
			if (       buf.DrawButton(btnblend, btny, lh,   0,   2, n, btnmrgn, mode == DBPOSD_MAIN,     mouse, "START MENU",                              txtblend) && mouse.click) SetMode(DBPOSD_MAIN);
			if (osk && buf.DrawButton(btnblend, btny, lh,   2,   5, n, btnmrgn, mode == DBPOSD_OSK,      mouse, "ON-SCREEN KEYBOARD",                      txtblend) && mouse.click) SetMode(DBPOSD_OSK);
			if (mpr && buf.DrawButton(btnblend, btny, lh, n-5, n-2, n, btnmrgn, mode == DBPOSD_MAPPER,   mouse, "CONTROLLER MAPPER",                       txtblend) && mouse.click) SetMode(DBPOSD_MAPPER);
			if (       buf.DrawButton(btnblend, btny, lh, n-2, n  , n, btnmrgn, mode == DBPOSD_SETTINGS, mouse, "SETTINGS",                                txtblend) && mouse.click) SetMode(DBPOSD_SETTINGS);
			#endif
			if (w >= 296 && buf.DrawButtonAt(btnblend, btny, lh, 4, 4, w-30, w-8, false,             mouse, "R",                                           txtblend) && mouse.click) evnt(DBPET_KEYUP, KBD_tab, 0);
			if (mouse.y >= btny && (mouse.wheel_up || mouse.wheel_down)) evnt(DBPET_KEYUP, (mouse.wheel_down ? KBD_grave : KBD_tab), 0);
			mouse.realmouse = orgrealmouse;
		}

		Bit32u menublend = (DBP_FullscreenOSD ? 0xFF000000 : 0x00000000);
		switch (mode)
		{
			case DBPOSD_MAIN:     ptr.main->DrawMenu(buf, menublend, lh, w, h, ftr, mouseMoved, mouse); break;
			case DBPOSD_OSK:      ptr.osk->GFX(buf, mouse); break;
			case DBPOSD_MAPPER:   ptr.mapper->DrawMenu(buf, menublend, lh, w, h, ftr, mouseMoved, mouse); break;
			#ifdef DBP_STANDALONE
			case DBPOSD_SETTINGS: ptr.settings->DrawMenu(buf, menublend, lh, w, h, ftr, mouseMoved, mouse); break;
			#endif
		}

		mouse.Draw(buf, isOSK);
	}

	virtual bool evnt(DBP_Event_Type type, int val, int val2) override
	{
		mouse.Input(type, val, val2, (mode == DBPOSD_OSK));
		switch (mode)
		{
			case DBPOSD_MAIN:     ptr.main->Input(type, val, val2); break;
			case DBPOSD_OSK:      ptr.osk->Input(type, val, val2); break;
			case DBPOSD_MAPPER:   ptr.mapper->Input(type, val, val2); break;
			#ifdef DBP_STANDALONE
			case DBPOSD_SETTINGS: ptr.settings->Input(type, val, val2); break;
			#endif
		}
		if (type == DBPET_KEYUP && ((KBD_KEYS)val == KBD_tab || (KBD_KEYS)val == KBD_grave))
		{
			int add = (((KBD_KEYS)val == KBD_tab && !DBP_IsKeyDown(KBD_leftshift) && !DBP_IsKeyDown(KBD_rightshift)) ? 1 : -1), next = (int)mode;
			int skip1 = ((first_shell && !DBP_FullscreenOSD) ? -1 : DBPOSD_OSK), skip2 = (first_shell ? -1 : DBPOSD_MAPPER);
			do { next = (next + (int)_DBPOSD_COUNT + add) % _DBPOSD_COUNT; } while (next == skip1 || next == skip2);
			SetMode((DBP_OSDMode)next);
		}
		return true;
	}
};

static DBP_OnScreenDisplay DBP_OSD;
static void DBP_StartOSD(DBP_OSDMode mode, DBP_PureMenuState* in_main)
{
	DBP_FullscreenOSD = !!in_main;
	DBP_OSD.mouse.Reset();
	DBP_OSD.SetMode(mode, in_main);
};

static void DBP_CloseOSD()
{
	DBP_OSD.SetMode(_DBPOSD_CLOSE);
}

static void DBP_PureMenuProgram(Program** make)
{
	static struct AnyKeyWaiter : DBP_MenuInterceptor
	{
		Bit32u opentime, pressedKey;
		Bit8u msgType;
		bool pressedAnyKey;

		virtual void gfx(DBP_Buffer& _buf) override
		{
			char msgbuf[100];
			if      (msgType == 1) sprintf(msgbuf, "* GAME ENDED - EXITTING IN %u SECONDS - PRESS ANY KEY TO CONTINUE *", ((Bit32u)dbp_menu_time - ((DBP_GetTicks() - opentime) / 1000)));
			else if (msgType == 2) sprintf(msgbuf, "* PRESS ANY KEY TO RETURN TO START MENU *");
			DBP_BufferDrawing& buf = static_cast<DBP_BufferDrawing&>(_buf);
			int lh = (buf.height >= 400 ? 14 : 8), w = buf.width, h = buf.height, y = h - lh*5/2;
			buf.DrawBox(8, y-3, w-16, lh+6, buf.BGCOL_MENU, buf.COL_LINEBOX);
			buf.PrintCenteredOutlined(lh, 0, w, y, msgbuf);
		}

		virtual bool evnt(DBP_Event_Type type, int val, int) override
		{
			bool down = (type == DBPET_KEYDOWN || type == DBPET_MOUSEDOWN || type == DBPET_JOY1DOWN || type == DBPET_JOY2DOWN);
			bool up = (type == DBPET_KEYUP || type == DBPET_MOUSEUP || type == DBPET_JOY1UP || type == DBPET_JOY2UP);
			if ((!down && !up) || (DBP_GetTicks() - opentime) < 300) return true;
			const Bit32u key = ((Bit32u)type + (down ? 1 : 0) + (((Bit32u)val + 1) << 8));
			if (down) pressedKey = key;
			else if (pressedKey == key) pressedAnyKey = true;
			return true;
		}

		bool WaitAnyKeyPress(Bit8u msg_type)
		{
			opentime = DBP_GetTicks();
			pressedKey = 0;
			msgType = msg_type;
			pressedAnyKey = false;
			DBP_KEYBOARD_ReleaseKeys(); // any unintercepted CALLBACK_* can set a key down
			StartIntercept();
			while (!pressedAnyKey && !first_shell->exit)
			{
				CALLBACK_Idle();
				if (msgType == 1 && (DBP_GetTicks() - opentime) > ((Bit32u)dbp_menu_time * 1000)) first_shell->exit = true;
			}
			EndIntercept();
			INT10_SetVideoMode(0x3);
			return !first_shell->exit;
		}
	} interceptor;

	struct Menu : Program
	{
		virtual void Run() override
		{
			enum { M_NORMAL, M_BOOT, M_FINISH } m = (cmd->FindExist("-BOOT") ? M_BOOT : cmd->FindExist("-FINISH") ? M_FINISH : M_NORMAL);

			if (m == M_BOOT && dbp_menu_time >= 0 && DBP_Run::startup.mode != DBP_Run::RUN_NONE && DBP_Run::Run(DBP_Run::startup.mode, DBP_Run::startup.info, DBP_Run::startup.exec))
				return;

			DBP_FullscreenOSD = true;
			DBP_PureMenuState* ms = new DBP_PureMenuState();
			bool runsoloexe = (ms->exe_count == 1 && ms->fs_rows <= (int)(dbp_images.size() + 1) && !patchDrive::variants.Len());

			#ifndef STATIC_LINKING
			if (m == M_FINISH && dbp_menu_time >= 0 && dbp_menu_time < 99 && (runsoloexe || DBP_Run::autoboot.use) && (DBP_GetTicks() - dbp_lastmenuticks) >= 500)
			{
				if (dbp_menu_time == 0) { first_shell->exit = true; return; }
				if (!interceptor.WaitAnyKeyPress(1)) return;
				m = M_NORMAL;
			}
			#endif

			if (m == M_FINISH)
			{
				// ran without auto start or only for a very short time (maybe crash), wait for user confirmation
				if (!interceptor.WaitAnyKeyPress(2)) return;
				m = M_NORMAL;
			}

			if (m == M_BOOT && runsoloexe && dbp_menu_time != (signed char)-1)
			{
				ms->DoInput(DBP_MenuState::RES_OK, ms->list[ms->sel].type, 0);
				delete ms;
			}
			else if (m != M_BOOT || ms->exe_count != 0 || ms->fs_rows != 0 || patchDrive::variants.Len() || Drives['C'-'A'] || Drives['A'-'A'] || Drives['D'-'A'])
				DBP_StartOSD(DBPOSD_MAIN, ms);
			else
				delete ms;
		}
	};
	*make = new Menu;
}

static void DBP_WheelShiftOSD(Bit8u _port, bool _on, Bit8u _shift_port = (Bit8u)-1)
{
	struct Wheel
	{
		enum EFlags : Bit8u { FLAG_OPENWHEEL = 1, FLAG_CLOSEWHEEL = 2, FLAG_SETSHIFT = 4, FLAG_CLEARSHIFT = 8 };
		enum EState : Bit8u { STATE_CLOSED, STATE_OPEN, STATE_OPEN_PRESSED, STATE_CLOSING_PRESSED, STATE_CLOSING_RELEASED };
		Bit8u State, Flags, Shift;
		bool ButtonDown, UseMouse;
		Bit16s PosX, PosY, Result;
		Bit32u Tick;

		static bool InAngleRange(float a, float b, float halfstep)
		{
			float off = a - b;
			if (off < -3.14159265358979323846f) off += 6.28318530717958647692f;
			if (off >  3.14159265358979323846f) off -= 6.28318530717958647692f;
			return (off > -halfstep && off < halfstep);
		}

		void Draw(Bit8u port, DBP_BufferDrawing& drw)
		{
			Bit8u wheelport = (Shift ? Shift - 1 : port);
			int n = 0;
			for (const DBP_WheelItem& wi : dbp_wheelitems)
				if (wi.port == wheelport)
					n++;
			if (!n) return;

			int alpha = 0;
			Bit32u tickd = (DBP_GetTicks() - Tick);
			if (State == STATE_OPEN) alpha = tickd*2;
			if (State == STATE_OPEN_PRESSED) alpha = 0xFF;
			if (State == STATE_CLOSING_PRESSED || State == STATE_CLOSING_RELEASED) alpha = 0xFF - (Result == -1 ? tickd*2 : tickd/2);
			if (alpha <= 0) return;
			if (alpha >= 0xFF) alpha = 0xFF;

			int cx = drw.width * (port ? 2 : 5) / 8, cy = drw.height * 2 / 3, rad = (drw.height > 900 ? 150 : (drw.height / 6)), rad4 = rad + 4, radtxt = rad + 7;
			int srad = rad / 9, maxdist = (rad-srad+3), maxdistsq = maxdist * maxdist, seldist = rad / 2, seldistsq = seldist * seldist;

			int wx = PosX/(32400/rad), wy = PosY/(32400/rad);
			int wdistsq = ((wx*wx) + (wy*wy));
			float wa = (wdistsq ? (float)atan2(wy, wx) : 0.0f);
			if (wdistsq > maxdistsq) { wx = (int)(cos(wa) * maxdist); wy = (int)(sin(wa) * maxdist); }

			float astart = ((n < 8 || (n & 1)) ? -3.14159265358979323846f : -3.0f), a = astart, astep = 6.28318530717958647692f / n, astephalf = astep/2+0.01f;
			float sela = (wdistsq ? -999.f : -777.f); // large number like 999 works because InAngleRange only fixes -4*PI to +4*PI
			if (State == STATE_OPEN) Result = -1;
			for (const DBP_WheelItem& wi : dbp_wheelitems)
			{
				if (wi.port != wheelport) continue;
				if (sela == -999.f && (State == STATE_OPEN ? InAngleRange(a, wa, astephalf) : Result == (Bit16s)(&wi - &dbp_wheelitems[0]))) sela = a;
				if (sela == a && wdistsq > seldistsq && State == STATE_OPEN) Result = (Bit16s)(&wi - &dbp_wheelitems[0]);
				a += astep;
			}

			const Bit32u white20 = ((alpha*30/100)<<24) | 0xFFFFFF;
			const Bit32u white30 = ((alpha*40/100)<<24) | 0xFFFFFF;
			const Bit32u white50 = ((alpha*50/100)<<24) | 0xFFFFFF;
			const Bit32u white80 = ((alpha*80/100)<<24) | 0xFFFFFF;
			const Bit32u black = ((alpha/1)<<24);
			const Bit32u white = black | 0xFFFFFF;
			const Bit32u selcolor = black | (((State == STATE_OPEN) || (tickd & 64)) ? 0xFFFF00 : 0xFF8000);
			const Bit32u whiteHighlight = ((alpha*(State == STATE_OPEN ? (40+15*wdistsq/maxdistsq) : 55)/100)<<24) | ((State != STATE_OPEN || wdistsq > seldistsq) ? 0xFFFF40 : 0xFFFFFF);

			Bit32u* video = drw.video;
			const int sqrOut = rad*rad, sqrIn = sqrOut/4, sqrOut1 = (rad+1)*(rad+1), sqrOut3 = (rad+3)*(rad+3), sqrOut4 = (rad4)*(rad4);
			DBP_BufferDrawing::AlphaBlendPrep white80a(white80), white20a(white20), whiteHighlighta(whiteHighlight), white30a(white30), white50a(white50);
			for (int w = drw.width, y = (cy - rad4 < 0 ? 0 : cy - rad4), ymax = (cy + rad4 >= (int)drw.height ? (int)drw.height : cy + rad4); y < ymax; y++)
				for (int x = (cx - rad4 < 0 ? 0 : cx - rad4), xmax = (cx + rad4 >= (int)w ? (int)w : cx + rad4); x < xmax; x++)
				{
					int dx = (x - cx), dy = (y - cy), dsq = (dx*dx)+(dy*dy);
					if (dsq >= sqrOut4) continue;
					drw.AlphaBlend(video + y*w + x,
						dsq < (2*2)   ? white80a :
						dsq < sqrIn   ? white20a :
						dsq < sqrOut  ? (InAngleRange((float)atan2(dy, dx), sela, astephalf) ? whiteHighlighta : white30a) :
						dsq < sqrOut1 ? white50a :
						dsq < sqrOut3 ? white80a : white50a);
				}

			drw.AlphaBlendDrawCircle(cx+wx, cy+wy, srad+2, srad+3, black);
			drw.AlphaBlendFillCircle(cx+wx, cy+wy, srad, white50);
			drw.AlphaBlendDrawCircle(cx+wx, cy+wy, srad, srad+2, white80);

			a = astart;
			int selnx = 0, selny = 0, lh = (drw.height >= 400 ? 14 : 8); const char* sellbl = NULL;
			for (const DBP_WheelItem& wi : dbp_wheelitems)
			{
				if (wi.port != wheelport) continue;

				const char* lbl = DBP_PadMapping::GetWheelAutoMapButtonLabel(wi);
				if (!lbl) lbl = DBP_GETKEYNAME(wi.k[wi.key_count - 1]);
				int lbllen = (int)strlen(lbl);

				int nx = (int)(cos(a) * radtxt) + 1, ny = (int)(sin(a) * (radtxt+5));
				if (fabs(fabs(a)-1.5708f) < 0.2f) nx -= (lbllen * DBP_BufferDrawing::CW) / 2;
				else if (nx < -5) nx -= (lbllen * DBP_BufferDrawing::CW);

				const bool sel = (sela == a && wdistsq > seldistsq);
				if (sel) { sellbl = lbl; selnx = nx; selny = ny; }
				else drw.PrintOutlined(lh, cx + nx, cy + ny - 3, lbl, white, black);
				a += astep;
			}
			if (sellbl) drw.PrintOutlined(lh, cx + selnx, cy + selny - 3, sellbl, selcolor, black);
		}

		void Update(Bit8u port)
		{
			if ((Flags & (FLAG_OPENWHEEL|FLAG_CLOSEWHEEL)) == (FLAG_OPENWHEEL|FLAG_CLOSEWHEEL)) Flags &= ~(FLAG_OPENWHEEL|FLAG_CLOSEWHEEL);
			if (Flags & FLAG_OPENWHEEL) { SetState(port, STATE_OPEN); Flags &= ~FLAG_OPENWHEEL; }
			if (Flags & FLAG_CLOSEWHEEL) { SetState(port, STATE_CLOSING_PRESSED); Flags &= ~FLAG_CLOSEWHEEL; }
			if (State == STATE_CLOSING_PRESSED && (DBP_GetTicks() - Tick) > 70) SetState(port, STATE_CLOSING_RELEASED);
			if (State == STATE_CLOSING_RELEASED && (DBP_GetTicks() - Tick) > 300) SetState(port, STATE_CLOSED);
			if (State != STATE_OPEN && State != STATE_OPEN_PRESSED) return;

			const Bit8u aw = dbp_actionwheel_inputs;
			Bit16s x0 = ((aw&1) ? input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) : 0);
			Bit16s y0 = ((aw&1) ? input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) : 0);
			Bit16s x1 = ((aw&2) ? input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) : 0);
			Bit16s y1 = ((aw&2) ? input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) : 0);
			Bit16s x2 = ((aw&4) ? (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) ? -0x7FFF : 0) + (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT) ? 0x7FFF : 0) : 0);
			Bit16s y2 = ((aw&4) ? (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) ? -0x7FFF : 0) + (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN) ? 0x7FFF : 0) : 0);
			int d0 = (x0*x0+y0*y0), d1 = (x1*x1+y1*y1);
			if (d0 < 45000000) { d0 = x0 = y0 = 0; }
			if (d1 < 45000000) { d1 = x1 = y1 = 0; }
			Bit16s use0 = (d0 > d1), x3 = (use0 ? x0 : x1), y3 = (use0 ? y0 : y1);
			Bit16s use3 = ((x3*x3+y3*y3) > (x2*x2+y2*y2)), x = (use3 ? x3 : x2), y = (use3 ? y3 : y2);

			if (abs(x) > abs(PosX) || abs(y) > abs(PosY))
			{
				PosX = x;
				PosY = y;
				UseMouse = false;
			}
			else if (!UseMouse)
			{
				PosX = ((PosX * 5) + x) / 6;
				PosY = ((PosY * 5) + y) / 6;
			}

			bool down = (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) || (UseMouse && input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)));
			if (down && !ButtonDown) SetState(port, STATE_OPEN_PRESSED);
			if (!down && ButtonDown) SetState(port, STATE_OPEN);
			ButtonDown = down;

			if (port != 0 || !(aw&8)) return;
			Bit16s mx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X), my = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
			if (mx || my)
			{
				UseMouse = true;
				int nx = PosX + mx*(int)(dbp_mouse_speed*350);
				int ny = PosY + my*(int)(dbp_mouse_speed*350);
				PosX = (Bit16s)(nx > 0x7FFF ? 0x7FFF : (nx < -0x7FFF ? -0x7FFF : nx));
				PosY = (Bit16s)(ny > 0x7FFF ? 0x7FFF : (ny < -0x7FFF ? -0x7FFF : ny));
			}
		}

		void SetState(Bit8u port, EState newstate)
		{
			//static const char *statename[] = { "CLOSED", "OPEN", "OPEN_PRESSED", "CLOSING_PRESSED", "CLOSING_RELEASED" };
			//log_cb(RETRO_LOG_INFO, "[WHEEL%d] Setting state from %s to %s (current result: %d)\n", port, statename[(int)State], statename[(int)newstate], Result);
			if (newstate == State || (newstate == STATE_CLOSING_PRESSED && (State == STATE_CLOSED || State == STATE_CLOSING_RELEASED))) return;

			if (newstate == STATE_OPEN && State == STATE_CLOSED)
			{
				PosX = PosY = 0;
				Result = -1;
				if (port == 0 && (dbp_actionwheel_inputs & 8)) dbp_mouse_input = 'f';
			}
			if (newstate == STATE_CLOSED && port == 0 && (dbp_actionwheel_inputs & 8))
			{
				dbp_mouse_input = DBP_Option::Get(DBP_Option::mouse_input)[0];
				if (dbp_mouse_input == 't' && dbp_yml_directmouse) dbp_mouse_input = 'd';
			}

			if (newstate != STATE_CLOSING_PRESSED || State != STATE_OPEN_PRESSED)
			{
				const DBP_WheelItem* wi = (Result >= 0 ? &dbp_wheelitems[Result] : NULL);
				if ((newstate == STATE_OPEN_PRESSED || newstate == STATE_CLOSING_PRESSED) && wi)
					for (Bit8u i = 0; i != wi->key_count; i++)
						DBP_PadMapping::BindForWheel(wi->port, wi->k[i]).Update(1);
				else if ((State == STATE_OPEN_PRESSED || State == STATE_CLOSING_PRESSED) && wi)
					for (Bit8u i = 0; i != wi->key_count; i++)
						DBP_PadMapping::BindForWheel(wi->port, wi->k[i]).Update(0);
			}

			if (State == STATE_OPEN_PRESSED)
				Tick -= 1000;
			else if (newstate != STATE_CLOSING_RELEASED)
				Tick = DBP_GetTicks();

			State = newstate;
		}

		void ApplyShift(Bit8u port)
		{
			if ((Flags & (FLAG_SETSHIFT|FLAG_CLEARSHIFT)) == (FLAG_SETSHIFT|FLAG_CLEARSHIFT)) { Flags &= ~(FLAG_SETSHIFT|FLAG_CLEARSHIFT); return; }
			if (!(Flags & FLAG_SETSHIFT)) { Flags &= ~FLAG_CLEARSHIFT; ClearShift(port); return; }
			Flags &= ~FLAG_SETSHIFT;
			Bit8u shift_port = Shift - 1;
			for (size_t i = 0, iEnd = dbp_input_binds.size(); i != iEnd; i++)
			{
				DBP_InputBind b = dbp_input_binds[i];
				if (b.port != shift_port || b.device == RETRO_DEVICE_MOUSE || b.evt == DBPET_SHIFTPORT) continue;
				b.port = port; // for PORT_DEVICE_INDEX_ID
				for (DBP_InputBind &b2 : dbp_input_binds)
					if (PORT_DEVICE_INDEX_ID(b) == PORT_DEVICE_INDEX_ID(b2) && b2.evt == DBPET_SHIFTPORT)
						goto skip_bind;
				b.port = port | DBP_SHIFT_PORT_BIT;
				b.lastval = 0;
				dbp_input_binds.push_back(b);
				skip_bind:;
			}
		}

		void ClearShift(Bit8u port)
		{
			if (!Shift) return;
			Shift = 0;
			for (size_t i = dbp_input_binds.size(); i--;)
			{
				DBP_InputBind& b = dbp_input_binds[i];
				if (!(b.port & DBP_SHIFT_PORT_BIT) || (b.port & DBP_PORT_MASK) != port) continue;
				if (b.lastval) b.Update(0);
				dbp_input_binds.erase(dbp_input_binds.begin() + i);
			}
		}
	};

	static Wheel Wheels[DBP_MAX_PORTS];
	static struct WheelInterceptor : DBP_Interceptor
	{
		virtual bool usegfx() override
		{
			for (Bit8u port = 0; port != DBP_MAX_PORTS; port++) if (Wheels[port].State) return true;
			for (Bit8u port = 0; port != DBP_MAX_PORTS; port++) if (Wheels[port].Shift || Wheels[port].Flags) return false;
			DBP_SetIntercept(NULL);
			return false;
		}

		virtual void gfx(DBP_Buffer& buf) override
		{
			for (Bit8u port = 0; port != DBP_MAX_PORTS; port++) if (Wheels[port].State) Wheels[port].Draw(port, static_cast<DBP_BufferDrawing&>(buf));
		}

		virtual void input() override
		{
			for (Bit8u port = 0; port != DBP_MAX_PORTS; port++)
			{
				Wheel& wheel = Wheels[port];
				if ((wheel.Flags & (Wheel::FLAG_OPENWHEEL | Wheel::FLAG_CLOSEWHEEL)) || wheel.State) wheel.Update(port);
				if (wheel.Flags & (Wheel::FLAG_SETSHIFT | Wheel::FLAG_CLEARSHIFT)) wheel.ApplyShift(port);
			}

			for (DBP_InputBind& b : dbp_input_binds)
			{
				Bit8u port = (b.port & DBP_PORT_MASK);
				Wheel& wheel = Wheels[port];
				Bit16s val = 0;
				if (!wheel.Shift || (b.port & DBP_SHIFT_PORT_BIT) || b.device == RETRO_DEVICE_MOUSE || b.evt == DBPET_SHIFTPORT)
				{
					val = input_state_cb(port, b.device, b.index, b.id);
					if (dbp_analog_buttons && b.device == RETRO_DEVICE_JOYPAD && b.evt <= _DBPET_JOY_AXIS_MAX)
					{
						Bit16s aval = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, b.id);
						if (aval) val = aval; else val = (val ? (Bit16s)32767 : (Bit16s)0); // old frontend fallback
					}

					if (val && (wheel.State == Wheel::STATE_OPEN || wheel.State == Wheel::STATE_OPEN_PRESSED || wheel.State == Wheel::STATE_CLOSING_PRESSED || wheel.State == Wheel::STATE_CLOSING_RELEASED) && b.evt != DBPET_ACTIONWHEEL && (
						(b.device == RETRO_DEVICE_ANALOG && ((b.index == RETRO_DEVICE_INDEX_ANALOG_LEFT && (dbp_actionwheel_inputs&1)) || (b.index == RETRO_DEVICE_INDEX_ANALOG_RIGHT && (dbp_actionwheel_inputs&2))))  || 
						(b.device == RETRO_DEVICE_JOYPAD && (b.id == RETRO_DEVICE_ID_JOYPAD_B || ((b.id == RETRO_DEVICE_ID_JOYPAD_UP || b.id == RETRO_DEVICE_ID_JOYPAD_DOWN || b.id == RETRO_DEVICE_ID_JOYPAD_LEFT || b.id == RETRO_DEVICE_ID_JOYPAD_RIGHT) && (dbp_actionwheel_inputs&4)))) || 
						(b.device == RETRO_DEVICE_MOUSE && (dbp_actionwheel_inputs&8))))
						val = 0;
				}
				if (val != b.lastval) b.Update(val);
			}
		}

		virtual void close() override
		{
			for (Bit8u port = 0; port != DBP_MAX_PORTS; port++)
			{
				Wheels[port].SetState(port, Wheel::STATE_CLOSED);
				Wheels[port].ClearShift(port);
				Wheels[port].Flags = 0;
			}
		}
	} interceptor;

	if (!_on && dbp_intercept_next != &interceptor) return;
	Wheel& wheel = Wheels[_port & DBP_PORT_MASK];
	if (_shift_port == (Bit8u)-1)
		wheel.Flags = ((wheel.Flags & ~(Wheel::FLAG_OPENWHEEL & Wheel::FLAG_CLOSEWHEEL)) | (_on ? Wheel::FLAG_OPENWHEEL : Wheel::FLAG_CLOSEWHEEL));
	else
	{
		wheel.Flags = ((wheel.Flags & ~(Wheel::FLAG_SETSHIFT & Wheel::FLAG_CLEARSHIFT)) | (_on ? Wheel::FLAG_SETSHIFT : Wheel::FLAG_CLEARSHIFT));
		if (_on) wheel.Shift = _shift_port + 1;
	}
	DBP_SetIntercept(&interceptor);
}

#ifdef DBP_STANDALONE
void DBPS_ToggleOSD()
{
	DBP_QueueEvent(DBPET_TOGGLEOSD, 0);
	DBP_QueueEvent(DBPET_TOGGLEOSDUP, 0);
}

bool DBPS_IsGameRunning()
{
	return (dbp_game_running || (first_shell && first_shell->bf && !first_shell->bf->IsAutoexec()));
}

bool DBPS_IsShowingOSD()
{
	return (dbp_intercept && dbp_intercept->usegfx());
}

void DBPS_OpenContent(const char* path)
{
	DBPS_BrowsePath.assign(path);
	struct Local { static void Do()
	{
		const char *path = DBPS_BrowsePath.c_str(), *path_fs = strrchr(path, '/'), *path_bs = strrchr(path, '\\'), *path_s = (path_bs > path_fs ? path_bs : path_fs);
		if (!path_s) return;
		std::string fname = DBPS_BrowsePath.substr((size_t)(path_s + 1 - path));
		DBPS_BrowsePath.erase((size_t)(path_s - path));
		if (fname.size() > 5 && !strncasecmp(&fname[fname.size()-5], ".dosc", 5)) fname[fname.size()-1] = 'Z' | (fname[fname.size()-1] & 0x20);
		if (DBP_OSD.ptr._all) { DBP_OSD.SetMode(DBPOSD_MAIN, NULL); } else { DBP_StartOSD(DBPOSD_MAIN); }
		if (DBP_OSD.ptr.main->GoToSubMenu(DBP_PureMenuState::IT_FILEBROWSE, DBP_PureMenuState::IT_FILEBROWSE, -1, &fname))
			DBP_OSD.ptr.main->DoInput(DBP_MenuState::RES_OK, DBP_PureMenuState::IT_FILEBROWSE, 0);
		else
			emuthread_notify(0, LOG_WARN, "Unsupported file type %s", fname.c_str());
	}};
	dbps_emu_thread_func = Local::Do;
}

void DBPS_AddDisc(const char* path)
{
	DBP_Mount(DBP_AppendImage(path, false), false);
}

const std::string& DBPS_GetContentName()
{
	return dbp_content_name;
}
#endif
