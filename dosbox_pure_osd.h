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

enum DBP_OSDMode { DBPOSD_CLOSED, DBPOSD_MAIN, DBPOSD_OSK, DBPOSD_MAPPER, _DBPOSD_COUNT };
static void DBP_StartOSD(DBP_OSDMode mode, struct DBP_PureMenuState* in_main = NULL);
static void DBP_CloseOSD();
static bool DBP_FullscreenOSD;

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

	void PrintCenteredOutlined(int lh, int x, int w, int y, const char* msg, Bit32u col = 0xFFFFFFFF)
	{
		x += (int)((w-strlen(msg)*CW)/2);
		for (int i = 0; i < 9; i++) if (i != 4) Print(lh, x+((i%3)-1), y+((i/3)-1), msg, 0xFF000000);
		Print(lh, x, y, msg, col);
	}

	void Print(int lh, int x, int y, const char* msg, Bit32u col = 0xFFFFFFFF, int maxw = 0xFFFFFF)
	{
		DBP_ASSERT((col & 0xFF000000) && y >= 0 && y < (int)height);
		const Bit8u* fnt = (lh == 8 ? int10_font_08 : int10_font_14);
		const int ch = (lh == 8 ? 8 : 14);
		for (const char* p = msg, *pEnd = p + (maxw / CW); *p && p != pEnd; p++)
			DrawChar(fnt, ch, x+CW*(int)(p - msg), y, *p, col);
	}

	void DrawChar(const Bit8u* fnt, int ch, int x, int y, int c, Bit32u col = 0xFFFFFFFF)
	{
		if (x < 0 || x + CW >= (int)width) return;
		const Bit8u* ltr = &fnt[(Bit8u)c * ch], *ltrend = ltr + ch;
		for (Bit32u w = width, *py = video + (w * y) + (x); ltr != ltrend; py += w, ltr++)
			for (Bit32u *px = py, bit = 256; bit; px++)
				if (*ltr & (bit>>=1))
					*px = col;
	}

	void DrawBox(int x, int y, int w, int h, Bit32u colfill, Bit32u colline)
	{
		DBP_ASSERT((colline >> 24) == 0xFF); // && ((colfill >> 24) == 0xFF || (colfill >> 24) == 0));
		if (w < 8) { DBP_ASSERT(false); return; }
		int y1 = y, y2 = y1+4, y4 = y1+h, y3 = y4-4, yBox = y3-y2, yAll = y4-y1;
		int x1 = x, x2 = x1+4, x4 = x1+w, x3 = x4-4, xBox = x3-x2, xAll = x4-x1;
		Bit32u *v = video + width * y1 + x1;
		AlphaBlendFillRect(x1,y1,xAll,yAll, colfill);

		for (Bit32u *p = v+4,       *pEnd = p + xBox, *p2 = p + width * (yAll-1); p != pEnd; p++, p2++) *p = *p2 = colline;
		for (Bit32u *p = v+4*width, *pEnd = p + yBox*width; p != pEnd; p+=width) *p = p[xAll-1] = colline;

		for (int i = 0; i != 16; i++)
		{
			int a = i % 4, b = i/4, c = a*b<3;
			if (!c) continue;
			*(v+a+b*width) = colline;
			*(v+xAll-1-a+b*width) = colline;
			*(v+xAll-1-a+(yAll-1-b)*width) = colline;
			*(v+a+(yAll-1-b)*width) = colline;
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
		//DBP_ASSERT((col >> 24) == 0xFF || (col >> 24) == 0);
		Bit32u alpha = ((col >> 24) ? (col >> 24) : dbp_alphablend_base);
		col = (alpha << 24) | (col & 0xFFFFFF);
		for (Bit32u *py = video + width * y + x; h--; py += width)
			for (Bit32u *p = py, *pEnd = p + w; p != pEnd; p++)
				AlphaBlend(p, col);
	}

	void AlphaBlendDrawRect(int x, int y, int w, int h, Bit32u col)
	{
		DBP_ASSERT((col >> 24) == 0xFF || (col >> 24) == 0);
		Bit32u alpha = ((col >> 24) ? (col >> 24) : dbp_alphablend_base);
		col = (alpha << 24) | (col & 0xFFFFFF);
		for (Bit32u *p = video + width * y + x, *pEnd = p + w, *p2 = p + width * (h-1); p != pEnd; p++, p2++) AlphaBlend(p, col), AlphaBlend(p2, col);
		for (Bit32u *p = video + width * y + x, *pEnd = p + width * h; p != pEnd; p+=width) AlphaBlend(p, col), AlphaBlend(p+w-1, col);
	}

	static void AlphaBlend(Bit32u* p1, Bit32u p2)
	{
		Bit32u a = (p2 & 0xFF000000) >> 24, na = 255 - a;
		Bit32u rb = ((na * (*p1 & 0x00FF00FF)) + (a * (p2 & 0x00FF00FF))) >> 8;
		Bit32u ag = (na * ((*p1 & 0xFF00FF00) >> 8)) + (a * (0x01000000 | ((p2 & 0x0000FF00) >> 8)));
		*p1 = ((rb & 0x00FF00FF) | (ag & 0xFF00FF00));
	}

	bool DrawButtonAt(Bit32u blend, int btny, int lh, int padu, int padd, int btnx, int btnr, bool on, const struct DBP_MenuMouse& m, const char* txt);
	INLINE bool DrawButton(Bit32u blend, int btny, int lh, int i, int n, bool on, const struct DBP_MenuMouse& m, const char* txt)
		{ int w = width; return DrawButtonAt(blend, btny, lh, 4, 4, (!i ? 8 : (w*i/n + 2)), (i == (n-1) ? w - 8 : (w*(i+1)/n - 2)), on, m, txt); }
};
DBP_STATIC_ASSERT(sizeof(DBP_BufferDrawing) == sizeof(DBP_Buffer)); // drawing is just for function expansions, we're just casting one to the other

struct DBP_MenuMouse
{
	float x, y, jx, jy; Bit16u bw, bh; Bit16s mx, my; Bit8s kx, ky, mspeed; Bit8u realmouse : 1, left_pressed : 1, left_up : 1, right_up : 1, wheel_down : 1, wheel_up : 1, ignoremove : 1;
	DBP_MenuMouse() { memset(this, 0, sizeof(*this)); }
	void Reset() { mspeed = 2; left_pressed = false; if (realmouse) mx = dbp_mouse_x, my = dbp_mouse_y; }

	void Input(DBP_Event_Type type, int val, int val2)
	{
		switch (type)
		{
			case DBPET_MOUSEUP:
				if (val == 0) left_pressed = false, left_up = true; //left
				if (val == 1) right_up = true; //right
				break;
			case DBPET_MOUSEDOWN:
				if (val == 0) left_pressed = true; //left
				/* fall through */
			case DBPET_MOUSEMOVE:
				mx = dbp_mouse_x;
				my = dbp_mouse_y;
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
					case KBD_left:  case KBD_kp4: case KBD_right: case KBD_kp6: kx = 0; break;
					case KBD_up:    case KBD_kp8: case KBD_down:  case KBD_kp2: ky = 0; break;
				}
				break;
			case DBPET_JOY1X: case DBPET_JOY2X: case DBPET_JOYMX: jx = DBP_GET_JOY_ANALOG_VALUE(val); break;
			case DBPET_JOY1Y: case DBPET_JOY2Y: case DBPET_JOYMY: jy = DBP_GET_JOY_ANALOG_VALUE(val); break;
			case DBPET_MOUSESETSPEED: mspeed = (val > 0 ? 4 : 1); break;
			case DBPET_MOUSERESETSPEED: mspeed = 2; break;
		}
	}

	bool Update(DBP_BufferDrawing& buf, bool joykbd)
	{
		float oldmx = x, oldmy = y;
		Bit32u newres = buf.width * buf.height;
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
			if (newx == x && newy == y) return false;
			x = newx, y = newy;
		}
		else if (jx || kx || jy || ky)
		{
			if (!joykbd) { realmouse = false; return false; }
			x += (jx + kx) * mspeed;
			y += (jy + ky) * mspeed;
		}
		else return false;
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
		left_up = right_up = wheel_up = wheel_down = false;
		if (!realmouse && !joykbd) return;
		for (Bit32u thick = buf.GetThickness(), midc = 6*thick, maxc = 8*thick, *v = buf.video, w = buf.width, h = buf.height, i = 0; i != 9; i++)
		{
			Bit32u n = (i < 4 ? i : (i < 8 ? i+1 : 4)), px = (Bit32u)x + (n%3)-1, py = (Bit32u)y + (n/3)-1, ccol = (n == 4 ? 0xFFFFFFFF : 0xFF000000);
			Bit32u *pp, *p = v + py * w + px, *pendx = p - px + w, *pendy = v + w * h;
			for (Bit32u c = 0; c != maxc; c++)
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

bool DBP_BufferDrawing::DrawButtonAt(Bit32u blend, int btny, int lh, int padu, int padd, int btnx, int btnr, bool on, const DBP_MenuMouse& m, const char* txt)
{
	int btnd = btny+lh+padu+padd, btnw = btnr - btnx;
	bool hover = (m.y >= btny && m.y < btnd && m.x >= btnx && m.x < btnr && m.realmouse);
	DrawBox(btnx, btny, btnw, btnd - btny, (on ? BGCOL_BTNON : (hover ? BGCOL_BTNHOVER : BGCOL_BTNOFF)) | blend, (on ? (0xFF000000|BGCOL_BTNOFF) : (0xFF000000|BGCOL_BTNON)));
	PrintCenteredOutlined(lh, btnx, btnw, btny+padu, txt, COL_BTNTEXT);
	return (hover && !on);
}

struct DBP_MenuState
{
	enum ItemType : Bit8u { IT_NONE, _IT_CUSTOM, };
	enum Result : Bit8u { RES_NONE, RES_OK, RES_CANCEL, RES_CLOSESCREENKEYBOARD, RES_CHANGEMOUNTS };
	bool refresh_mousesel, scroll_unlocked, hide_sel, show_popup;
	int sel, scroll, joyx, joyy, scroll_jump, click_sel;
	Bit32u open_ticks;
	DBP_Event_Type held_event; KBD_KEYS held_key; Bit32u held_ticks;
	struct Item { Bit8u type; Bit16s info; std::string str; INLINE Item() {} INLINE Item(Bit8u t, Bit16s i = 0, const char* s = "") : type(t), info(i), str(s) {} };
	std::vector<Item> list;

	DBP_MenuState() : refresh_mousesel(true), scroll_unlocked(false), hide_sel(false), show_popup(false), sel(0), scroll(-1), joyx(0), joyy(0), scroll_jump(0), click_sel(-1), open_ticks(DBP_GetTicks()), held_event(_DBPET_MAX) { }

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
				}
				break;
			case DBPET_KEYUP:
				switch ((KBD_KEYS)val)
				{
					case KBD_enter: case KBD_kpenter: res = RES_OK; break;
					case KBD_esc: res = RES_CANCEL; break;
				}
				if (held_event == DBPET_KEYDOWN) held_event = _DBPET_MAX;
				break;
			case DBPET_ONSCREENKEYBOARDUP: res = RES_CLOSESCREENKEYBOARD; break;
			case DBPET_CHANGEMOUNTS: res = RES_CHANGEMOUNTS; break;
			case DBPET_MOUSEMOVE:
				scroll_unlocked = true;
				break;
			case DBPET_MOUSEDOWN:
				if (val == 0) click_sel = (hide_sel ? -1 : sel);
				break;
			case DBPET_MOUSEUP:
				if (val == 0 && click_sel == sel) res = RES_OK; // left
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

		if (sel_change || x_change)
		{
			if (held_event == _DBPET_MAX) { held_event = type; held_ticks = DBP_GetTicks() + 300; }
			if      (sel_change ==  -1) held_key = KBD_up;
			else if (sel_change ==   1) held_key = KBD_down;
			else if (sel_change == -12) held_key = KBD_pageup;
			else if (sel_change ==  12) held_key = KBD_pagedown;
			else if (x_change   ==  -1) held_key = KBD_left;
			else if (x_change   ==   1) held_key = KBD_right;
			else held_event = _DBPET_MAX;
			scroll_unlocked = false;
		}

		DBP_ASSERT(list.size());
		for (int count = (int)list.size(); !res && sel_change && !show_popup;)
		{
			if (hide_sel) { hide_sel = false; break; }
			sel += sel_change;
			if (sel >= 0 && sel < count) { }
			else if (sel_change > 1) sel = count - 1;
			else if (sel_change == -1) sel = count - 1;
			else sel = scroll = 0;
			if (list[sel].type != RES_NONE) break;
			sel_change = (sel_change == -1 ? -1 : 1);
		}

		if (hide_sel && res != RES_CANCEL && res != RES_CLOSESCREENKEYBOARD && res != RES_CHANGEMOUNTS) return;
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

	void DrawMenuBase(DBP_BufferDrawing& buf, Bit32u blend, int lh, int rows, const DBP_MenuMouse& m, bool mouseMoved, int menul, int menur, int menuu)
	{
		int count = (int)list.size(), xtra = (lh == 8 ? 0 : 1), scrx = menur - 11, menuh = rows * lh + xtra;
		bool scrollbar = (count > rows);

		if (!show_popup)
		{
			if (scrollbar && m.left_pressed && (m.x >= scrx || click_sel == -2) && m.y >= menuu && m.y < menuu+menuh && scroll != -1)
			{
				int scrollh = menuh * rows / count / 2;
				scroll_jump = (((count - rows) * ((int)m.y - menuu - scrollh) / (menuh-scrollh-scrollh)) - scroll);
				click_sel = -2;
			}

			if (scroll == -1 && m.realmouse) mouseMoved = refresh_mousesel; // refresh when switching tab

			// Update Scroll
			if (count <= rows) scroll = 0;
			else if (scroll == -1)
			{
				scroll = sel - rows/2;
				scroll = (scroll < 0 ? 0 : scroll > count - rows ? count - rows : scroll);
			}
			else
			{
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

			if (mouseMoved)
			{
				int my = (int)(m.y+0.499f), mx = (int)(m.x+0.499f);
				sel = scroll + (((int)my - menuu) / lh);
				if (my < menuu) { sel = scroll; hide_sel = true; }
				else if (sel >= count) { sel = count - 1; hide_sel = true; }
				else if (mx >= scrx && scrollbar) { hide_sel = true; }
				else if (my >= menuu+rows*lh) { sel = scroll+rows-1; hide_sel = true; }
				else { hide_sel = false; }
				scroll_unlocked = true;
			}
		}

		buf.DrawBox(menul, menuu - 3, menur - menul, menuh + 6, buf.BGCOL_MENU | blend, buf.COL_LINEBOX);

		if (list[sel].type != IT_NONE && !hide_sel)
			buf.AlphaBlendFillRect(menul+3, menuu + (sel - scroll)*lh, menur - menul - 6 - (scrollbar ? 10 : 0), lh + xtra, buf.BGCOL_SELECTION | blend);

		if (scrollbar)
		{
			int scrollu = menuh * scroll / count, scrolld = menuh * (scroll+rows) / count;
			buf.AlphaBlendFillRect(scrx, menuu, 8, menuh, buf.BGCOL_SCROLL | blend);
			buf.AlphaBlendFillRect(scrx, menuu + scrollu, 8, scrolld - scrollu, buf.BGCOL_SELECTION | blend);
		}
	}

	void UpdateHeld()
	{
		if (held_event == _DBPET_MAX) return;
		Bit32u t = DBP_GetTicks();
		if ((Bit32s)(t - held_ticks) < 60) return;
		held_ticks = (t - held_ticks > 120 ? t : held_ticks + 60);
		Input(DBPET_KEYDOWN, (int)held_key, 1);
	}
};

static const Bit8u DBP_MapperJoypadNums[] = { RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3 };
static const char* DBP_MapperJoypadNames[] = { "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "SELECT", "START", "L", "R", "L2", "R2", "L3", "R3" };

struct DBP_MapperMenuState : DBP_MenuState
{
	enum ItemType : Bit8u { IT_CANCEL = _IT_CUSTOM, IT_PRESET, IT_SELECT, IT_EDIT, IT_ADD, IT_DEL, IT_DEVICE, IT_DIVIDER };
	int main_sel = 0;
	Bit8u bind_port = 0, bind_dev, bind_part, changed = 0;
	DBP_InputBind* edit;

	DBP_MapperMenuState() { menu_top(); }

	~DBP_MapperMenuState() { if (changed) DBP_PadMapping::Save(); }

	enum { JOYPAD_MAX = (sizeof(DBP_MapperJoypadNums)/sizeof(DBP_MapperJoypadNums[0])) };

	DBP_InputBind BindFromPadNum(Bit8u i)
	{
		if (i < JOYPAD_MAX) return { bind_port, RETRO_DEVICE_JOYPAD, 0, DBP_MapperJoypadNums[i], _DBPET_MAX };
		else { Bit8u n = i-JOYPAD_MAX; return { bind_port, RETRO_DEVICE_ANALOG, (Bit8u)(n/4), (Bit8u)(1-(n/2)%2), _DBPET_MAX }; }
	}

	void menu_top(int x_change = 0)
	{
		if (x_change)
		{
			int maxport = 1;
			while (maxport != DBP_MAX_PORTS && dbp_port_mode[maxport]) maxport++;
			bind_port = (bind_port + maxport + x_change) % maxport;
			main_sel = 0;
		}

		list.clear();
		if (dbp_port_mode[bind_port] != DBP_PadMapping::MODE_MAPPER)
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

			for (Bit8u i = 0; i != JOYPAD_MAX + 8; i++)
			{
				Bit8u a = (i>=JOYPAD_MAX), apart = (a ? (i-JOYPAD_MAX)%2 : 0);
				const DBP_InputBind pad = BindFromPadNum(i);
				const Bit32u padpdii = PORT_DEVICE_INDEX_ID(pad);
				list.emplace_back(IT_NONE);
				if (!a) list.back().str = DBP_MapperJoypadNames[i];
				else  ((list.back().str = DBP_MapperJoypadNames[2+pad.index]) += " Analog ") += DBP_MapperJoypadNames[(i-JOYPAD_MAX)%4];

				size_t numBefore = list.size();
				for (const DBP_InputBind& b : dbp_input_binds)
				{
					if (PORT_DEVICE_INDEX_ID(b) != padpdii) continue;
				
					int key = -1;
					if (b.evt == DBPET_KEYDOWN)
						key = b.meta;
					else if (b.evt == DBPET_AXISMAPPAIR)
						key = DBP_MAPPAIR_GET(apart?1:-1, b.meta);
					else for (const DBP_SpecialMapping& sm : DBP_SpecialMappings)
						if (sm.evt == b.evt && sm.meta == (a ? (apart ? 1 : -1) : b.meta))
							{ key = DBP_SPECIALMAPPINGS_KEY + (int)(&sm - DBP_SpecialMappings); break; }
					if (key < 0) { DBP_ASSERT(false); continue; }

					const char *desc_dev = DBP_GETKEYDEVNAME(key);
					list.emplace_back(IT_EDIT, (Bit16s)(((&b - &dbp_input_binds[0])<<1)|apart), "  [Edit]");
					(((list.back().str += (desc_dev ? " " : "")) += (desc_dev ? desc_dev : "")) += ' ') += DBP_GETKEYNAME(key);
				}
				if (list.size() - numBefore == 0) list.emplace_back(IT_ADD, i, "  [Create Binding]");

				if (const char* action = DBP_PadMapping::GetBoundAutoMapButtonLabel(padpdii, a))
				{
					list.emplace_back(IT_NONE, 1, "    Function: ");
					list.back().str.append(action);
				}
			}
		}
		if (!DBP_FullscreenOSD)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_CANCEL, 0, "    Close Mapper");
		}
		if (main_sel >= (int)list.size()) main_sel = (int)list.size()-1;
		while (main_sel && list[main_sel].type == IT_NONE) main_sel--;
		ResetSel((main_sel < 1 ? 1 : main_sel), (main_sel < 1)); // skip top IT_NONE entry
		edit = NULL;
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
		int main_info = list[sel].info;
		if (ok_type == IT_ADD)
		{
			const DBP_InputBind b = (edit ? *edit : BindFromPadNum((Bit8u)main_info)), *pBegin = (dbp_input_binds.size() ? &dbp_input_binds[0] : NULL), *p = pBegin, *pEnd = p + dbp_input_binds.size();
			int insert_idx = DBP_PadMapping::InsertBind(b);
			if (edit) edit = &dbp_input_binds[insert_idx];
			else main_info = (Bit16u)(insert_idx<<1);
		}

		if (!edit)
		{
			main_sel = sel;
			edit = &dbp_input_binds[main_info>>1];
			bind_part = (Bit8u)(main_info&1);

			int sel_header = sel - 1; while (list[sel_header].type != IT_NONE) sel_header--;
			(list[0].str = list[sel_header].str);
			list[1].str = std::string(" >") += list[sel].str;
			list[0].type = list[1].type = IT_NONE;
		}
		else if (ok_type == IT_ADD)
		{
			edit->evt = _DBPET_MAX; edit->meta = edit->lastval = 0;
			(list[1].str = " >") += "  [Additional Binding]";
		}
		list.resize(2);
		list.emplace_back(IT_NONE);
		list.emplace_back(IT_DEVICE,   1, "  "); list.back().str += DBPDEV_Keyboard;
		list.emplace_back(IT_DEVICE,   2, "  "); list.back().str += DBPDEV_Mouse;
		list.emplace_back(IT_DEVICE,   3, "  "); list.back().str += DBPDEV_Joystick;
		list.emplace_back(IT_SELECT, 225, "  "); list.back().str += DBP_SpecialMappings[225-DBP_SPECIALMAPPINGS_KEY].name;
		if (edit->evt != _DBPET_MAX)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_DEL, 0, "  [Remove Binding]");
			int count = 0;
			for (const DBP_InputBind& b : dbp_input_binds)
				if (b.port == edit->port && b.device == edit->device && b.index == edit->index && b.id == edit->id)
					count++;
			if (count < 4)
			{
				list.emplace_back(IT_NONE);
				list.emplace_back(IT_ADD, 0, "  [Additional Binding]");
			}
		}
		list.emplace_back(IT_NONE);
		list.emplace_back(IT_CANCEL, 0, "Cancel");

		char device = *(list[1].str.c_str() + (sizeof(" >  [Edit] ")-1));
		ResetSel(device == 'J' ? 5 : (device == 'M' ? 4 : 3));
		bind_dev = 0;
	}

	void menu_keys()
	{
		bind_dev = (Bit8u)list[sel].info;
		(list[2].str = "   > ")  += list[sel].str;
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

		if (x_change && !edit)
		{
			menu_top(x_change);
		}
		if ((ok_type == IT_SELECT || ok_type == IT_DEL) && edit)
		{
			DBP_PadMapping::AssignBindEvent(*edit, bind_part, (Bit8u)list[sel].info);
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
		else if (ok_type == IT_CANCEL && bind_dev)
		{
			menu_devices(ok_type);
		}
		else if (ok_type == IT_CANCEL && (edit || is_presets_menu()))
		{
			if (edit && edit->evt == _DBPET_MAX) dbp_input_binds.erase(dbp_input_binds.begin() + (edit - &dbp_input_binds[0]));
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
		if ((dbp_port_mode[bind_port] == DBP_PadMapping::MODE_MAPPER) == is_mapper_disabled_top())
			menu_top();

		int hdr = lh*3, rows = (h - hdr - ftr) / lh-1, count = (int)list.size(), l = w/2 - 150, r = w/2 + 150, xtra = (lh == 8 ? 0 : 1), wide = !edit && !is_presets_menu() && w > 500;
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
					ihdr = ihdr;
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

		if (!edit && !is_presets_menu())
		{
			int x_change = 0, x1 = l-(wide?50:0), x2 = r-25+(wide?50:0);
			if (buf.DrawButtonAt(0x80000000, hdr-lh-6, lh, 3, 2, x1, x1+25, false, m, "\x1B") && m.left_up) x_change = -1;
			if (buf.DrawButtonAt(0x80000000, hdr-lh-6, lh, 3, 2, x2, x2+25, false, m, "\x1A") && m.left_up) x_change = 1;
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
		int osky = (mo.y && mo.y < (buf.height / 2) ? 3 : (int)(buf.height / fy) - 3 - 65);

		if (pressed_key && (DBP_GetTicks() - pressed_time) > 500 && pressed_key != KBD_LAST)
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
			case DBPET_MOUSEDOWN: case DBPET_JOY1DOWN: case DBPET_JOY2DOWN: case_ADDKEYDOWN:
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
			case DBPET_MOUSEUP: case DBPET_JOY1UP: case DBPET_JOY2UP: case_ADDKEYUP:
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
			case DBPET_ONSCREENKEYBOARD: case_CLOSEOSK:
				pressed_key = KBD_NONE;
				memset(held, 0, sizeof(held));
				DBP_CloseOSD();
				break;
		}
	}
};

struct DBP_PureMenuState : DBP_MenuState
{
	enum ItemType : Bit8u { IT_RUN = _IT_CUSTOM, IT_MOUNT, IT_BOOTIMG, IT_BOOTIMG_MACHINE, IT_BOOTOSLIST, IT_BOOTOS, IT_INSTALLOSSIZE, IT_INSTALLOS, IT_SHELLLIST, IT_RUNSHELL, IT_CANCEL, IT_COMMANDLINE, IT_CLOSEOSD };
	enum { INFO_HEADER = 0x0B, INFO_WARN = 0x0A };

	int exe_count, fs_count;
	bool multidrive;
	Bit8u popupsel;

	DBP_PureMenuState() : exe_count(0), fs_count(0), multidrive(false), popupsel(0)
	{
		if (dbp_game_running) INT10_SetCursorShape(0x1e, 0); // hide blinking cursor
		RefreshFileList(true);
		if (DBP_Run::autoboot.use)
		{
			if (DBP_Run::startup.mode == DBP_Run::RUN_BOOTOS) GoToSubMenu(IT_BOOTOSLIST);
			if (DBP_Run::startup.mode == DBP_Run::RUN_SHELL) GoToSubMenu(IT_SHELLLIST);
			if (DBP_Run::startup.mode == DBP_Run::RUN_BOOTIMG) GoToSubMenu(IT_BOOTIMG);
			int idx = MenuIndexByString(DBP_Run::startup.str.c_str());
			if (idx != -1) ResetSel(idx);
		}
	}

	~DBP_PureMenuState()
	{
		if (dbp_game_running) INT10_SetCursorShape((Bit8u)(CurMode->cheight - (CurMode->cheight>=14 ? 3 : 2)), (Bit8u)(CurMode->cheight - (CurMode->cheight>=14 ? 2 : 1))); // reset blinking cursor
	}

	void RefreshFileList(bool initial_scan)
	{
		list.clear();
		exe_count = fs_count = 0; int cd_count = 0, hd_count = 0; bool bootimg = false;

		for (DBP_Image& image : dbp_images)
		{
			list.emplace_back(IT_MOUNT, (Bit16s)(&image - &dbp_images[0]), DBP_Image_Label(image));
			(DBP_Image_IsCD(image) ? cd_count : hd_count)++;
			fs_count++;
			if (image.image_disk) bootimg = true;
		}
		if (bootimg)
		{
			list.emplace_back(IT_BOOTIMG, 0, "[ Boot from Disk Image ]");
			fs_count++;
		}
		if (!dbp_strict_mode && dbp_osimages.size())
		{
			list.emplace_back(IT_BOOTOSLIST, 0, "[ Run Installed Operating System ]");
			fs_count++;
		}
		if (!dbp_strict_mode && dbp_shellzips.size())
		{
			list.emplace_back(IT_SHELLLIST, 0, "[ Run System Shell ]");
			fs_count++;
		}
		if (!dbp_strict_mode && ((Drives['D'-'A'] && dynamic_cast<isoDrive*>(Drives['D'-'A']) && ((isoDrive*)(Drives['D'-'A']))->CheckBootDiskImage()) || (hd_count == 1 && cd_count == 1)))
		{
			list.emplace_back(IT_INSTALLOSSIZE, 0, "[ Boot and Install New Operating System ]");
			fs_count++;
		}
		if (fs_count) list.emplace_back(IT_NONE);

		// Scan drive C first, any others after
		int old_sel = sel;
		sel = ('C'-'A'); // use sel to have access to it in FileIter
		DriveFileIterator(Drives[sel], FileIter, (Bitu)this);
		for (sel = 0; sel != ('Z'-'A'); sel++)
		{
			if (sel == ('C'-'A') || !Drives[sel]) continue;
			DriveFileIterator(Drives[sel], FileIter, (Bitu)this);
			multidrive = true;
		}
		if (exe_count) list.emplace_back(IT_NONE);

		sel = (fs_count && exe_count ? fs_count + 1 : 0);

		if (list.empty()) { list.emplace_back(IT_NONE, 0, "No executable file found"); list.emplace_back(IT_NONE); sel = 2; }
		if (DBP_FullscreenOSD && !dbp_strict_mode) list.emplace_back(IT_CLOSEOSD, 0, "Go to Command Line");
		else if (dbp_game_running && !dbp_strict_mode) list.emplace_back(IT_COMMANDLINE, 0, "Go to Command Line");
		if (!DBP_FullscreenOSD) list.emplace_back(IT_CLOSEOSD, 0, "Close Menu");
		if (list.back().type == IT_NONE) list.pop_back();
		if (!initial_scan) { if (old_sel < (int)list.size()) sel = old_sel; return; }
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
		int insert_index = (m->fs_count ? m->fs_count + 1 : 0);
		for (; insert_index != (int)m->list.size(); insert_index++)
			if (m->list[insert_index].str > entry) break;
		m->list.insert(m->list.begin() + insert_index, item);
		std::swap(m->list[insert_index].str, entry);
	}

	int MenuIndexByString(const char* findit)
	{
		for (Item& it : list)
			if ((it.type == IT_RUN || it.type == IT_BOOTOS || it.type == IT_BOOTIMG_MACHINE || it.type == IT_RUNSHELL) && it.str == findit)
				return (int)(&it - &list[0]);
		return -1;
	}

	void GoToSubMenu(ItemType type)
	{
		for (const Item& it : list)
		{
			if (it.type != type) continue;
			sel = (int)(&it - &list[0]);
			open_ticks -= 1000;
			DoInput(RES_NONE, type, 0);
			return;
		}
		DBP_ASSERT(false);
	}

	void DrawMenu(DBP_BufferDrawing& buf, Bit32u blend, int lh, int w, int h, int ftr, bool mouseMoved, const DBP_MenuMouse& m)
	{
		UpdateHeld();

		buf.DrawBox( w/10, 5, w-w/5, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
		buf.DrawBox( 8, lh+7, w-16, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);

		buf.PrintCenteredOutlined(lh, 0, w, 7, "DOSBOX PURE START MENU", buf.COL_MENUTITLE);
		buf.PrintCenteredOutlined(lh, 0, w, 7+lh+2, (!dbp_content_name.empty() ? dbp_content_name.c_str() : "no content loaded!"), buf.COL_CONTENT);

		int inforow = (w > 319), hdr = lh*2+12, rows = (h - hdr - ftr) / lh - inforow, count = (int)list.size(), bot = hdr + rows * lh + 3 - (lh == 8 ? 1 : 0);
		DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, 8, w - 8, hdr);

		for (int i = scroll, se = (hide_sel ? -1 : sel); i != count && i != (scroll + rows); i++)
		{
			const DBP_MenuState::Item& item = list[i];
			int y = hdr + (i - scroll)*lh, strlen = (int)item.str.length();
			if (item.type == IT_MOUNT) // mountable file system
			{
				bool mounted = dbp_images[item.info].mounted;
				int lbllen = (mounted ? sizeof("UNMOUNT") : sizeof("MOUNT")), len = strlen + lbllen;
				int lblx = (w - buf.CW*(lbllen + strlen)) / 2;
				buf.Print(lh, lblx, y, (mounted ? "UNMOUNT " : "MOUNT "), (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
				buf.Print(lh, lblx + buf.CW*lbllen, y, item.str.c_str(), (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
			}
			else if (item.type == IT_RUN || item.type == IT_BOOTOS || item.type == IT_BOOTIMG_MACHINE || item.type == IT_RUNSHELL)
			{
				int off = ((item.type != IT_RUN || multidrive) ? 0 : 3), len = strlen - off, lblx = (w - buf.CW*len) / 2;
				buf.Print(lh, lblx,       y, item.str.c_str() + off, (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
				if (i != se) continue;
				buf.Print(lh, lblx - buf.CW*(2      ), y, "*", buf.COL_WHITE);
				buf.Print(lh, lblx + buf.CW*(len + 1), y, "*", buf.COL_WHITE);
				if (DBP_Run::autoboot.use)
				{
					buf.Print(lh, lblx + buf.CW*(len + 1), y, "* [SET AUTO START]", buf.COL_WHITE);
				}
			}
			else buf.Print(lh, (w - buf.CW*strlen) / 2, y, item.str.c_str(), (item.type != IT_NONE ? (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL) : (item.info == INFO_HEADER ? buf.COL_HEADER : (item.info == INFO_WARN ? buf.COL_WARN : buf.COL_NORMAL))));
		}

		if (inforow)
		{
			char skiptext[38];
			if (!DBP_Run::autoboot.use) skiptext[0] = '\0';
			else if (DBP_Run::autoboot.skip) snprintf(skiptext, sizeof(skiptext), "Skip showing first %d frames", DBP_Run::autoboot.skip);
			else snprintf(skiptext, sizeof(skiptext), "SHIFT/L2/R2 + Restart to come back");

			if (w > 639)
			{
				buf.DrawBox(8, bot, w-319, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
				buf.PrintCenteredOutlined(lh, 8, w-319, bot+2, skiptext, buf.COL_BTNTEXT);
			}
			else if (w > 320)
			{
				buf.DrawBox(8, bot, w-319, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
			}
			
			if (w < 640 && DBP_Run::autoboot.use)
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
				if (m.left_up || m.wheel_up) DoInput(RES_NONE, IT_NONE, 1);
				if (m.right_up || m.wheel_down) DoInput(RES_NONE, IT_NONE, -1);
			}
		}

		if (show_popup)
		{
			int halfw = w/2, boxw = (w < 640 ? halfw-16 : halfw/2+8);
			buf.DrawBox(halfw-boxw, h/2-lh*3, boxw*2, lh*6+8, buf.BGCOL_HEADER | 0xFF000000, buf.COL_LINEBOX);
			buf.PrintCenteredOutlined(lh, 0, w, h/2-lh*2, (w < 320 ? "Reset DOS to" : "Are you sure you want to reset DOS"), buf.COL_BTNTEXT);
			buf.PrintCenteredOutlined(lh, 0, w, h/2-lh+2, (w < 320 ? "start this?" : "to start the selected application?"), buf.COL_BTNTEXT);
			if (m.realmouse) popupsel = 0;
			if (buf.DrawButton(0x80000000, h/2+lh*1, lh, 1, 4, !m.realmouse && popupsel == 1, m, "OK"))     popupsel = 1;
			if (buf.DrawButton(0x80000000, h/2+lh*1, lh, 2, 4, !m.realmouse && popupsel == 2, m, "CANCEL")) popupsel = 2;
		}
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
		//if (item.type != IT_RUN && item.type != IT_BOOTOS && item.type != IT_BOOTIMG_MACHINE) auto_change = 0;
		if (DBP_Run::autoboot.use && auto_change > 0) DBP_Run::autoboot.skip += (DBP_Run::autoboot.skip < 50 ? 10 : (DBP_Run::autoboot.skip < 150 ? 25 : (DBP_Run::autoboot.skip < 300 ? 50 : 100)));
		if (!DBP_Run::autoboot.use && auto_change > 0) DBP_Run::autoboot.use = true;
		if (auto_change < 0) DBP_Run::autoboot.skip -= (DBP_Run::autoboot.skip <= 50 ? 10 : (DBP_Run::autoboot.skip <= 150 ? 25 : (DBP_Run::autoboot.skip <= 300 ? 50 : 100)));
		if (DBP_Run::autoboot.skip < 0) { DBP_Run::autoboot.use = false; DBP_Run::autoboot.skip = 0; }

		if (ok_type == IT_MOUNT)
		{
			if (dbp_images[item.info].mounted)
				DBP_Unmount(dbp_images[item.info].drive);
			else
				DBP_Mount((unsigned)item.info, true);
			RefreshFileList(false);
		}
		else if (ok_type == IT_BOOTIMG)
		{
			// Machine property was fixed by dbp_reboot_machine or DOSBOX.CONF and cannot be modified here, so automatically boot the image as is (RES_NONE check is for GoToSubMenu)
			if (res != RES_NONE && DBP_FullscreenOSD && control->GetSection("dosbox")->GetProp("machine")->getChange() == Property::Changeable::OnlyByConfigProgram)
				goto handle_result;
			list.clear();
			list.emplace_back(IT_NONE, INFO_HEADER, "Select Boot System Mode");
			list.emplace_back(IT_NONE);
			for (const char* it : DBP_MachineNames) list.emplace_back(IT_BOOTIMG_MACHINE, (Bit16s)(it[0]|0x20), it);
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_CANCEL, 0, "Cancel");
			const std::string& img = (!dbp_images.empty() ? dbp_images[dbp_image_index].path : list[1].str);
			bool isPCjrCart = (img.size() > 3 && (img[img.size()-3] == 'J' || img[img.size()-2] == 'T'));
			char wantmchar = (isPCjrCart ? 'p' : DBP_Run::GetDosBoxMachineChar());
			for (const Item& it : list)
				if (it.info == wantmchar)
					{ ResetSel((int)(&it - &list[0])); break; }
		}
		else if (ok_type == IT_INSTALLOSSIZE)
		{
			const char* filename;
			std::string osimg = DBP_GetSaveFile(SFT_NEWOSIMAGE, &filename);
			list.clear();
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
			ResetSel(filename > &osimg[0] ? 11 : 10);
		}
		else if (ok_type == IT_BOOTOSLIST)
		{
			list.clear();
			list.emplace_back(IT_NONE, INFO_HEADER, "Select Operating System Disk Image");
			list.emplace_back(IT_NONE);
			for (const std::string& im : dbp_osimages)
				{ list.emplace_back(IT_BOOTOS, (Bit16s)(&im - &dbp_osimages[0])); list.back().str.assign(im.c_str(), im.size()-4); }
			if (dbp_system_cached) { list.emplace_back(IT_NONE); list.emplace_back(IT_NONE, INFO_WARN, "To Refresh: Audio Options > MIDI Output > Scan System Directory"); }
			char ramdisk = retro_get_variable("dosbox_pure_bootos_ramdisk", "false")[0];
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
			const char *save_d_name; std::string save_d = DBP_GetSaveFile(SFT_VIRTUALDISK, &save_d_name);
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, INFO_WARN, "Changes made to the D: drive will be stored in the following location:");
			if (save_d_name > &save_d[0]) { list.emplace_back(IT_NONE, INFO_WARN); list.back().str.assign(&save_d[0], save_d_name - &save_d[0]); }
			list.emplace_back(IT_NONE, INFO_WARN, save_d_name);
			ResetSel(2, true);
		}
		else if (ok_type == IT_SHELLLIST)
		{
			list.clear();
			list.emplace_back(IT_NONE, INFO_HEADER, "Select System Shell");
			list.emplace_back(IT_NONE);
			for (const std::string& im : dbp_shellzips)
				{ list.emplace_back(IT_RUNSHELL, (Bit16s)(&im - &dbp_shellzips[0])); list.back().str.assign(im.c_str(), im.size()-5); }
			if (dbp_system_cached) { list.emplace_back(IT_NONE); list.emplace_back(IT_NONE, INFO_WARN, "To Refresh: Audio Options > MIDI Output > Scan System Directory"); }
			ResetSel(2, true);
		}
		else if (((res == RES_CANCEL && list.back().type == IT_CLOSEOSD) || res == RES_CLOSESCREENKEYBOARD) && !DBP_FullscreenOSD)
		{
			ok_type = IT_CLOSEOSD;
			goto handle_result;
		}
		else if (ok_type == IT_CANCEL || (res == RES_CANCEL && list.back().type != IT_CLOSEOSD))
		{
			// Go to top menu (if in submenu) or refresh list
			ResetSel(0, true);
			RefreshFileList(false);
		}
		else if (ok_type)
		{
			handle_result:
			if (dbp_strict_mode && (ok_type == IT_BOOTOS || ok_type == IT_INSTALLOS || ok_type == IT_RUNSHELL || ok_type == IT_COMMANDLINE || (ok_type == IT_CLOSEOSD && DBP_FullscreenOSD))) return;
			if (ok_type != IT_CLOSEOSD)
			{
				DBP_ASSERT(item.type == ok_type && (ok_type == IT_RUN || ok_type == IT_BOOTIMG || ok_type == IT_BOOTIMG_MACHINE || ok_type == IT_BOOTOS || ok_type == IT_INSTALLOS || ok_type == IT_RUNSHELL || ok_type == IT_COMMANDLINE));
				if (!show_popup && dbp_game_running)
				{
					popupsel = 0;
					show_popup = true;
					return;
				}

				DBP_Run::Run(
					(ok_type == IT_RUN ? DBP_Run::RUN_EXEC :
					(ok_type == IT_BOOTIMG ? DBP_Run::RUN_BOOTIMG:
					(ok_type == IT_BOOTIMG_MACHINE ? DBP_Run::RUN_BOOTIMG :
					(ok_type == IT_BOOTOS ? DBP_Run::RUN_BOOTOS :
					(ok_type == IT_INSTALLOS ? DBP_Run::RUN_INSTALLOS :
					(ok_type == IT_RUNSHELL ? DBP_Run::RUN_SHELL :
					(ok_type == IT_COMMANDLINE ? DBP_Run::RUN_COMMANDLINE : DBP_Run::RUN_NONE))))))),
					item.info, item.str, true);

				// Show menu again on image boot when machine needs to change but there are EXE files (some games need to run a FIX.EXE before booting)
				if (ok_type == IT_BOOTIMG_MACHINE && dbp_reboot_machine && exe_count && !DBP_Run::autoboot.use)
					DBP_Run::startup.mode = DBP_Run::RUN_NONE;
			}
			DBP_CloseOSD();
		}
		else if (res == RES_CHANGEMOUNTS)
		{
			RefreshFileList(false);
		}
	}
};

struct DBP_OnScreenDisplay
{
	DBP_OSDMode mode;
	union { DBP_PureMenuState* main; DBP_OnScreenKeyboardState* osk; DBP_MapperMenuState* mapper; void* _all; } ptr;
	DBP_MenuMouse mouse;

	void SetMode(DBP_OSDMode in_mode, DBP_PureMenuState* in_main = NULL)
	{
		switch (mode)
		{
			case DBPOSD_MAIN:   delete ptr.main; break;
			case DBPOSD_OSK:    delete ptr.osk; break;
			case DBPOSD_MAPPER: delete ptr.mapper; break;
		}
		ptr._all = NULL;
		mode = in_mode;
		dbp_intercept_data = this;
		dbp_intercept_gfx = DBP_OnScreenDisplay::gfx;
		dbp_intercept_input = DBP_OnScreenDisplay::input;
		switch (mode)
		{
			case DBPOSD_MAIN:
				ptr.main = (in_main ? in_main : new DBP_PureMenuState());
				if (!ptr.main->refresh_mousesel) mouse.ignoremove = true;
				break;
			case DBPOSD_OSK:
				ptr.osk = new DBP_OnScreenKeyboardState();
				break;
			case DBPOSD_MAPPER:
				ptr.mapper = new DBP_MapperMenuState();
				break;
			case DBPOSD_CLOSED:
				dbp_intercept_data = NULL;
				dbp_intercept_gfx = NULL;
				dbp_intercept_input = NULL;
				break;
		}
		DBP_KEYBOARD_ReleaseKeys();
	}

	void GFX(DBP_BufferDrawing& buf)
	{
		int w = buf.width, h = buf.height, lh = (h >= 400 ? 14 : 8), ftr = lh+20;
		bool isOSK = (mode == DBPOSD_OSK);
		bool mouseMoved = mouse.Update(buf, isOSK);

		Bit32u blend = (DBP_FullscreenOSD ? 0xFF000000 : 0x00000000);
		if (DBP_FullscreenOSD || !isOSK)
		{
			int btny = buf.height - 13 - lh;
			int n = (DBP_FullscreenOSD ? 2 : 3);
			if (n == 2) buf.FillRect(0, 0, w, h, buf.BGCOL_STARTMENU);
			if (          buf.DrawButton(blend, btny, lh, 0,   n, mode == DBPOSD_MAIN,   mouse, (w < 500 ? "STARTMENU" : "START MENU"))        && mouse.left_up) SetMode(DBPOSD_MAIN);
			if (n == 3 && buf.DrawButton(blend, btny, lh, 1,   n, mode == DBPOSD_OSK,    mouse, (w < 500 ? "KEYBOARD" : "ON-SCREEN KEYBOARD")) && mouse.left_up) SetMode(DBPOSD_OSK);
			if (          buf.DrawButton(blend, btny, lh, n-1, n, mode == DBPOSD_MAPPER, mouse, (w < 500 ? "CONTROLS" : "CONTROLLER MAPPER"))  && mouse.left_up) SetMode(DBPOSD_MAPPER);
		}

		switch (mode)
		{
			case DBPOSD_MAIN:
				ptr.main->DrawMenu(buf, blend, lh, w, h, ftr, mouseMoved, mouse);
				break;
			case DBPOSD_OSK:
				ptr.osk->GFX(buf, mouse);
				break;
			case DBPOSD_MAPPER:
				ptr.mapper->DrawMenu(buf, blend, lh, w, h, ftr, mouseMoved, mouse);
				break;
		}

		mouse.Draw(buf, isOSK);
	}

	void Input(DBP_Event_Type type, int val, int val2)
	{
		mouse.Input(type, val, val2);
		switch (mode)
		{
			case DBPOSD_MAIN:
				ptr.main->Input(type, val, val2);
				break;
			case DBPOSD_OSK:
				ptr.osk->Input(type, val, val2);
				break;
			case DBPOSD_MAPPER:
				ptr.mapper->Input(type, val, val2);
				break;
		}
		if (type == DBPET_KEYUP && ((KBD_KEYS)val == KBD_tab || (KBD_KEYS)val == KBD_grave))
		{
			int add = (((KBD_KEYS)val == KBD_tab && !DBP_IsKeyDown(KBD_leftshift) && !DBP_IsKeyDown(KBD_rightshift)) ? 1 : 2);
			SetMode(DBP_FullscreenOSD ? (mode == DBPOSD_MAIN ? DBPOSD_MAPPER : DBPOSD_MAIN) : (DBP_OSDMode)(1 + ((mode - 1 + add) % (_DBPOSD_COUNT-1))));
		}
	}

	static void gfx(DBP_Buffer& buf, void* data) { ((DBP_OnScreenDisplay*)data)->GFX(static_cast<DBP_BufferDrawing&>(buf)); }
	static void input(DBP_Event_Type type, int val, int val2, void* data) { ((DBP_OnScreenDisplay*)data)->Input(type, val, val2); }
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
	DBP_OSD.SetMode(DBPOSD_CLOSED);
}

static void DBP_PureMenuProgram(Program** make)
{
	struct Menu : Program
	{
		Bit32u opentime, pressedKey;
		char msgbuf[100];
		bool pressedAnyKey;

		static void InterceptDrawMsg(DBP_Buffer& _buf, void* self)
		{
			DBP_BufferDrawing& buf = static_cast<DBP_BufferDrawing&>(_buf);
			int lh = (buf.height >= 400 ? 14 : 8), w = buf.width, h = buf.height, y = h - lh*5/2;
			buf.DrawBox(8, y-3, w-16, lh+6, buf.BGCOL_MENU, buf.COL_LINEBOX);
			buf.PrintCenteredOutlined(lh, 0, w, y, ((Menu*)self)->msgbuf);
		}

		static void InterceptInputAnyPress(DBP_Event_Type type, int val, int val2, void* self)
		{
			bool down = (type == DBPET_KEYDOWN || type == DBPET_MOUSEDOWN || type == DBPET_JOY1DOWN || type == DBPET_JOY2DOWN);
			bool up = (type == DBPET_KEYUP || type == DBPET_MOUSEUP || type == DBPET_JOY1UP || type == DBPET_JOY2UP);
			if ((!down && !up) || (DBP_GetTicks() - ((Menu*)self)->opentime) < 300) return;
			const Bit32u key = ((Bit32u)type + (down ? 1 : 0) + (((Bit32u)val + 1) << 8));
			if (down) ((Menu*)self)->pressedKey = key;
			else if (((Menu*)self)->pressedKey == key) ((Menu*)self)->pressedAnyKey = true;
		}

		bool WaitAnyKeyPress(Bit32u tick_limit = 0)
		{
			pressedKey = 0;
			pressedAnyKey = false;
			DBP_KEYBOARD_ReleaseKeys(); // any unintercepted CALLBACK_* can set a key down
			dbp_intercept_data = this;
			dbp_intercept_gfx = InterceptDrawMsg;
			dbp_intercept_input = InterceptInputAnyPress;
			while (!pressedAnyKey && !first_shell->exit)
			{
				CALLBACK_Idle();
				if (tick_limit && DBP_GetTicks() >= tick_limit) first_shell->exit = true;
			}
			dbp_intercept_gfx = NULL;
			dbp_intercept_input = NULL;
			INT10_ReloadFont();
			return !first_shell->exit;
		}

		virtual void Run() override
		{
			enum { M_NORMAL, M_BOOT, M_FINISH } m = (cmd->FindExist("-BOOT") ? M_BOOT : cmd->FindExist("-FINISH") ? M_FINISH : M_NORMAL);

			if (DBP_Run::HandleStartup(m == M_BOOT && dbp_menu_time >= 0))
				return;

			opentime = DBP_GetTicks();
			DBP_FullscreenOSD = true;
			DBP_PureMenuState* ms = new DBP_PureMenuState();
			bool runsoloexe = (ms->exe_count == 1 && ms->fs_count <= 1);

			#ifndef STATIC_LINKING
			if (m == M_FINISH && dbp_menu_time >= 0 && dbp_menu_time < 99 && (runsoloexe || DBP_Run::autoboot.use) && (opentime - dbp_lastmenuticks) >= 500)
			{
				if (dbp_menu_time == 0) { first_shell->exit = true; return; }
				sprintf(msgbuf, "* GAME ENDED - EXITTING IN %d SECONDS - PRESS ANY KEY TO CONTINUE *", dbp_menu_time);
				if (!WaitAnyKeyPress(DBP_GetTicks() + (dbp_menu_time * 1000))) return;
				m = M_NORMAL;
			}
			#endif

			if (m == M_FINISH)
			{
				// ran without auto start or only for a very short time (maybe crash), wait for user confirmation
				sprintf(msgbuf, "* PRESS ANY KEY TO RETURN TO START MENU *");
				if (!WaitAnyKeyPress()) return;
				m = M_NORMAL;
			}

			if (m == M_BOOT && runsoloexe && dbp_menu_time != (char)-1)
				ms->DoInput(DBP_MenuState::RES_OK, ms->list[ms->sel].type, 0);
			else if (m != M_BOOT || ms->exe_count != 0 || ms->fs_count != 0 || Drives['C'-'A'] || Drives['A'-'A'] || Drives['D'-'A'])
				DBP_StartOSD(DBPOSD_MAIN, ms);
		}
	};
	*make = new Menu;
}
