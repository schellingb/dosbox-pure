enum DBP_OSDMode { DBPOSD_MAIN, DBPOSD_OSK, DBPOSD_MAPPER, _DBPOSD_COUNT, DBPOSD_CLOSED };
static void DBP_StartOSD(DBP_OSDMode mode, struct DBP_PureMenuState* in_main = NULL);
static void DBP_CloseOSD();
static bool DBP_OSDIsStartmenu;

struct DBP_BufferDrawing : DBP_Buffer
{
	enum { CW = 8 }; // char width
	enum { MWIDTH = 234 + 10*4 + 2*3 };
	enum EColors : Bit32u
	{
		BGCOL_SELECTION = 0x117EB7, BGCOL_SCROLL = 0x093F5B, BGCOL_MENU = 0x1A1E20, BGCOL_HEADER = 0x582204, BGCOL_STARTMENU = 0xFF111111,
		COL_MENUTITLE = 0xFFFBD655, COL_CONTENT = 0xFFFFAB91,
		COL_LINEBOX = 0xFFFF7126,
		COL_HIGHLIGHT = 0xFFBDCDFB, COL_NORMAL = 0xFF4DCCF5, COL_WHITE = 0xFFFFFFFF, COL_WARN = 0xFFFF7126, COL_HEADER = 0xFF9ECADE,
		BGCOL_BTNON = 0xAB6037, BGCOL_BTNOFF = 0x5F3B27, BGCOL_BTNHOVER = 0x895133, COL_BTNTEXT = 0xFFFBC6A3,
		BGCOL_KEY = 0x5E3A26, BGCOL_KEYHOVER = 0xAA5F36, BGCOL_KEYPRESS = 0xE46E2E, BGCOL_KEYHELD = 0xC9CB35, BGCOL_KEYOUTLINE = 0x000000, COL_KEYTEXT = 0xFFF8EEE8,
	};

	Bit32u GetThickness() { return (width < (MWIDTH + 10) ? 1 : ((int)width - 10) / MWIDTH); }

	void PrintCenteredOutlined(int lh, int x, int w, int y, const char* msg, Bit32u col = 0xFFFFFFFF)
	{
		x += (int)((w-strlen(msg)*CW)/2);
		for (int i = 0; i < 9; i++) if (i != 4) Print(lh, x+((i%3)-1), y+((i/3)-1), msg, 0xFF000000);
		Print(lh, x, y, msg, col);
	}

	void Print(int lh, int x, int y, const char* msg, Bit32u col = 0xFFFFFFFF)
	{
		DBP_ASSERT((col & 0xFF000000) && y >= 0 && y < (int)height);
		const Bit8u* fnt = (lh == 8 ? int10_font_08 : int10_font_14);
		const int ch = (lh == 8 ? 8 : 14);
		for (const char* p = msg; *p; p++)
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

	bool DrawButton(Bit32u blend, int btny, int lh, int i, int n, bool on, const struct DBP_MenuMouse& m, const char* txt);
};
DBP_STATIC_ASSERT(sizeof(DBP_BufferDrawing) == sizeof(DBP_Buffer)); // drawing is just for function expansions, we're just casting one to the other

struct DBP_MenuMouse
{
	float x, y, jx, jy; Bit16u bw, bh; Bit16s mx, my; Bit8s kx, ky, mspeed; Bit8u realmouse : 1, left_pressed : 1, left_up : 1, right_up : 1, wheel_down : 1, wheel_up : 1;
	DBP_MenuMouse() { memset(this, 0, sizeof(*this)); }
	void Reset() { left_pressed = false; if (realmouse) mx = dbp_mouse_x, my = dbp_mouse_y; }

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
				mx = (Bit16s)dbp_mouse_x;
				my = (Bit16s)dbp_mouse_y;
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
		if (!mspeed)
		{
			if (dbp_framecount)
			{
				// Do only when framecount > 0 because dbp_mouse_x/dbp_mouse_y are only valid after drawing one frame
				mx = dbp_mouse_x;
				my = dbp_mouse_y;
			}
			if (!realmouse)
			{
				if (!mx && !my && !joykbd) return false;
				mspeed = 2;
			}
		}

		if (mx || my)
		{
			x = (float)(mx+0x7fff)*buf.width/0xFFFE;
			y = (float)(my+0x7fff)*buf.height/0xFFFE;
			mx = my = 0;
			realmouse = true;
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

bool DBP_BufferDrawing::DrawButton(Bit32u blend, int btny, int lh, int i, int n, bool on, const DBP_MenuMouse& m, const char* txt)
{
	int w = width, btnd = btny+lh+8, btnx = (!i ? 8 : (w*i/n + 2)), btnr = (i == (n-1) ? w - 8 : (w*(i+1)/n - 2)), btnw = btnr - btnx;
	bool hover = (m.y >= btny && m.y < btnd && m.x >= btnx && m.x < btnr && m.realmouse);
	DrawBox(btnx, btny, btnw, btnd - btny, (on ? BGCOL_BTNON : (hover ? BGCOL_BTNHOVER : BGCOL_BTNOFF)) | blend, (on ? (0xFF000000|BGCOL_BTNOFF) : (0xFF000000|BGCOL_BTNON)));
	PrintCenteredOutlined(lh, btnx, btnw, btny+4, txt, COL_BTNTEXT);
	return (hover && !on);
}

struct DBP_MenuState
{
	enum ItemType : Bit8u { IT_NONE, _IT_CUSTOM, };
	enum Result : Bit8u { RES_NONE, RES_OK, RES_CANCEL, RES_CLOSESCREENKEYBOARD, RES_CHANGEMOUNTS };
	bool refresh_mousesel, scroll_unlocked, hide_sel, show_popup;
	int sel, scroll, joyx, joyy, scroll_jump;
	Bit32u open_ticks;
	DBP_Event_Type held_event; KBD_KEYS held_key; Bit32u held_ticks;
	struct Item { Bit8u type; Bit16s info; std::string str; INLINE Item() {} INLINE Item(Bit8u t, Bit16s i = 0, const char* s = "") : type(t), info(i), str(s) {} };
	std::vector<Item> list;

	DBP_MenuState() : refresh_mousesel(true), scroll_unlocked(false), hide_sel(false), show_popup(false), sel(0), scroll(-1), joyx(0), joyy(0), scroll_jump(0), open_ticks(DBP_GetTicks()), held_event(_DBPET_MAX) { }

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
					case KBD_kpminus:  if (!show_popup) { scroll_unlocked = true; scroll_jump -= 4; } break; // mouse wheel up
					case KBD_kpplus:   if (!show_popup) { scroll_unlocked = true; scroll_jump += 4; } break; // mouse wheel down
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
			case DBPET_MOUSEUP:
				if (val == 0) res = RES_OK; // left
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

		if (hide_sel && res != RES_CANCEL && res != RES_CLOSESCREENKEYBOARD) return;
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
			if (scrollbar && m.left_pressed && m.x >= scrx && m.y >= menuu && m.y < menuu+menuh && scroll != -1)
				scroll_jump = (((count - rows) * ((int)m.y - menuu - 50) / (menuh-100)) - scroll);

			if (scroll == -1 && m.realmouse && refresh_mousesel) mouseMoved = true; // refresh when tabbing back

			// Update Scroll
			if (count <= rows) scroll = 0;
			else if (scroll == -1)
			{
				scroll = sel - rows/2;
				scroll = (scroll < 0 ? 0 : scroll > count - rows ? count - rows : scroll);
			}
			else
			{
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
				if (my < menuu) { sel = 0; hide_sel = true; }
				else if (sel >= count) { sel = count - 1; hide_sel = true; }
				else if (mx >= scrx && scrollbar) { hide_sel = true; }
				else if (my >= menuu+rows*lh) { hide_sel = true; }
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

static const Bit8u DBP_MapperJoypadNums[] = { RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_R3 };
static const char* DBP_MapperJoypadNames[] = { "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "SELECT", "START", "L", "R", "L2", "R2", "R3" };

struct DBP_MapperMenuState : DBP_MenuState
{
	enum ItemType : Bit8u { IT_CANCEL = _IT_CUSTOM, IT_SELECT, IT_EDIT, IT_ADD, IT_DEL, IT_DEVICE };
	int main_sel;
	Bit8u bind_dev, bind_part, changed;
	DBP_InputBind* edit;

	DBP_MapperMenuState() : main_sel(0), changed(0) { menu_top(); }

	~DBP_MapperMenuState() { if (changed) DBP_PadMapping::Save(); }

	enum { JOYPAD_MAX = (sizeof(DBP_MapperJoypadNums)/sizeof(DBP_MapperJoypadNums[0])) };

	static DBP_InputBind BindFromPadNum(Bit8u i)
	{
		Bit8u a = (i>=JOYPAD_MAX), n, device, index, id;
		if (a) { n = i-JOYPAD_MAX,   device = RETRO_DEVICE_ANALOG, index = n/4, id = 1-(n/2)%2; }
		else   { n = DBP_MapperJoypadNums[i], device = RETRO_DEVICE_JOYPAD, index = 0,   id = n;         }
		return { 0, device, index, id, NULL, _DBPET_MAX };
	}

	void menu_top()
	{
		list.clear();
		for (Bit8u i = 0; i != JOYPAD_MAX + 8; i++)
		{
			Bit8u a = (i>=JOYPAD_MAX), apart = (a ? (i-JOYPAD_MAX)%2 : 0);
			DBP_InputBind pad = BindFromPadNum(i);
			list.emplace_back(IT_NONE);
			if (!a) list.back().str = DBP_MapperJoypadNames[i];
			else  ((list.back().str = DBP_MapperJoypadNames[2+pad.index]) += " Analog ") += DBP_MapperJoypadNames[(i-JOYPAD_MAX)%4];

			size_t numBefore = list.size();
			for (const DBP_InputBind& b : dbp_input_binds)
			{
				if (b.port != 0 || b.device != pad.device || b.index != pad.index || b.id != pad.id) continue;
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
		}
		if (!dbp_custom_mapping.empty() || changed)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_DEL, 0, "[Delete Custom Mapping]");
		}
		if (!DBP_OSDIsStartmenu)
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

	void menu_devices(Bit8u ok_type)
	{
		if (!edit)
		{
			main_sel = sel;
			int main_info = list[sel].info;

			if (ok_type == IT_ADD)
			{
				dbp_input_binds.push_back(BindFromPadNum((Bit8u)main_info));
				main_info = (Bit16u)((dbp_input_binds.size()-1)<<1);
			}
			edit = &dbp_input_binds[main_info>>1];
			bind_part = (Bit8u)(main_info&1);

			int sel_header = sel - 1; while (list[sel_header].type != IT_NONE) sel_header--;
			(list[0].str = list[sel_header].str);
			list[1].str = std::string(" >") += list[sel].str;
			list[0].type = list[1].type = IT_NONE;
		}
		else if (ok_type == IT_ADD)
		{
			dbp_input_binds.push_back(*edit);
			edit = &dbp_input_binds.back();
			edit->desc = NULL; edit->evt = _DBPET_MAX; edit->meta = edit->lastval = 0;
			(list[1].str = " >") += "  [Additional Binding]";
		}
		list.resize(2);
		list.emplace_back(IT_NONE);
		list.emplace_back(IT_DEVICE, 1, "  "); list.back().str += DBPDEV_Keyboard;
		list.emplace_back(IT_DEVICE, 2, "  "); list.back().str += DBPDEV_Mouse;
		list.emplace_back(IT_DEVICE, 3, "  "); list.back().str += DBPDEV_Joystick;
		if (edit->evt != _DBPET_MAX)
		{
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_DEL, 0, "  [Remove Binding]");
			int count = 0;
			for (const DBP_InputBind& b : dbp_input_binds)
				if (b.port == 0 && b.device == edit->device && b.index == edit->index && b.id == edit->id)
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
		if (bind_dev == 1) for (Bit16s i = KBD_NONE + 1; i != KBD_LAST; i++)
		{
			list.emplace_back(IT_SELECT, i, "  ");
			list.back().str += DBP_KBDNAMES[i];
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

		if ((ok_type == IT_SELECT || ok_type == IT_DEL) && edit)
		{
			Bit16u bind_key = list[sel].info;
			if (bind_key == 0) // deleting entry
			{
				dbp_input_binds.erase(dbp_input_binds.begin() + (edit - &dbp_input_binds[0]));
			}
			else if (edit->device == RETRO_DEVICE_ANALOG) // Binding to an axis
			{
				if (edit->evt != DBPET_AXISMAPPAIR && edit->evt != _DBPET_MAX) DBP_PadMapping::ForceAxisMapPair(*edit);
				edit->evt = DBPET_AXISMAPPAIR;
				int other_key = DBP_MAPPAIR_GET((bind_part ? -1 : 1), edit->meta);
				edit->meta = (bind_part ? DBP_MAPPAIR_MAKE(other_key, bind_key) : DBP_MAPPAIR_MAKE(bind_key, other_key));
			}
			else if (bind_key < DBP_SPECIALMAPPINGS_KEY) // Binding a key to a joypad button
			{
				edit->evt = DBPET_KEYDOWN;
				edit->meta = (Bit16s)bind_key;
			}
			else // Binding a special mapping to a joypad button
			{
				edit->evt = DBP_SPECIALMAPPING(bind_key).evt;
				edit->meta = DBP_SPECIALMAPPING(bind_key).meta;
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
		else if (ok_type == IT_CANCEL && bind_dev)
		{
			menu_devices(ok_type);
		}
		else if (ok_type == IT_CANCEL && edit)
		{
			if (edit->evt == _DBPET_MAX) dbp_input_binds.pop_back();
			menu_top();
		}
		else if (ok_type == IT_DEL)
		{
			main_sel = 0;
			DBP_PadMapping::Delete();
			changed = false;
			menu_top();
		}
		else if ((ok_type == IT_CANCEL || res == RES_CLOSESCREENKEYBOARD) && !DBP_OSDIsStartmenu)
		{
			DBP_CloseOSD();
		}
	}

	void DrawMenu(DBP_BufferDrawing& buf, Bit32u blend, int lh, int w, int h, int ftr, bool mouseMoved, const DBP_MenuMouse& m)
	{
		UpdateHeld();

		int hdr = lh*2, rows = (h - hdr - ftr) / lh-1, count = (int)list.size(), l = w/2 - 150, r = w/2 + 150, xtra = (lh == 8 ? 0 : 1);
		if (l < 0) { l = 0, r = w; }
		buf.DrawBox(l, hdr-5-lh, r-l, lh+3, buf.BGCOL_HEADER | blend, buf.COL_LINEBOX);
		buf.PrintCenteredOutlined(lh, 0, w, hdr-lh-3, "Gamepad Mapper", buf.COL_MENUTITLE);

		if (!edit && w > 500)
		{
			buf.DrawBox(l-100, hdr - 3, 201, rows * lh + 6 + xtra, buf.BGCOL_MENU | blend, buf.COL_LINEBOX);
			DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, l + 100, r + 100, hdr);
			for (int ihdr = -1, i = scroll, inxt, se = (hide_sel ? -1 : sel); i != count && i != (scroll + rows); i++)
			{
				if (list[i].type == IT_NONE) { ihdr = -1; continue; }
				int y = hdr + (i - scroll)*lh;
				if (list[i].type == IT_DEL)
				{
					buf.Print(lh, l+140, y, list[i].str.c_str(), buf.COL_WARN);
					continue;
				}
				if (ihdr == -1)
				{
					for (ihdr = i - 1; list[ihdr].type != IT_NONE; ihdr--) {}
					for (inxt = i + 1; inxt < list.size() && list[inxt].type != IT_NONE; inxt++) {}
					if (list[sel].type != IT_NONE && !hide_sel && sel > ihdr && sel < inxt)
						buf.AlphaBlendFillRect(l-97, y, 195, lh+xtra, buf.BGCOL_SELECTION | blend);
					buf.Print(lh, l-84, y, list[ihdr].str.c_str(), buf.COL_HEADER);
					ihdr = ihdr;
				}
				buf.Print(lh, l+100, y, list[i].str.c_str(), (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
			}
		}
		else
		{
			DrawMenuBase(buf, blend, lh, rows, m, mouseMoved, l, r, hdr);
			for (int i = scroll, se = (hide_sel ? -1 : sel); i != count && i != (scroll + rows); i++)
				buf.Print(lh, l+16, hdr + (i - scroll)*lh, list[i].str.c_str(), (list[i].type != IT_NONE ? (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL) : buf.COL_HEADER));
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
	enum ItemType : Bit8u { IT_RUN = _IT_CUSTOM, IT_MOUNT, IT_BOOTIMG, IT_BOOTIMG_MACHINE, IT_BOOTOSLIST, IT_BOOTOS, IT_INSTALLOSSIZE, IT_INSTALLOS, IT_CANCEL, IT_COMMANDLINE, IT_CLOSEOSD };
	enum { INFO_HEADER = 0x0B, INFO_WARN = 0x0A };

	int exe_count, fs_count, init_autoboothash, autoskip;
	bool have_autoboot, use_autoboot, multidrive;
	Bit8u popupsel;

	DBP_PureMenuState() : exe_count(0), fs_count(0), init_autoboothash(0), autoskip(0), have_autoboot(false), use_autoboot(false), multidrive(false), popupsel(0)
	{
		if (dbp_game_running) INT10_SetCursorShape(0x1e, 0); // hide blinking cursor
		RefreshFileList(true);
		ReadAutoBoot();
		if (!use_autoboot && reboot.str.length())
		{
			int idx = MenuIndexByString(reboot.str.c_str());
			if (idx != -1) { ResetSel(idx); }
		}
	}

	~DBP_PureMenuState()
	{
		if (dbp_game_running) INT10_SetCursorShape((Bit8u)(CurMode->cheight - (CurMode->cheight>=14 ? 3 : 2)), (Bit8u)(CurMode->cheight - (CurMode->cheight>=14 ? 2 : 1))); // reset blinking cursor
	}

	void RefreshFileList(bool initial_scan)
	{
		list.clear();
		exe_count = fs_count = 0; int iso_count = 0, img_count = 0; bool bootimg = false;

		for (DBP_Image& image : dbp_images)
		{
			list.emplace_back(IT_MOUNT, (Bit16s)(&image - &dbp_images[0]));
			DBP_GetImageLabel(image, list.back().str);
			((list.back().str[list.back().str.size()-2]|0x25) == 'm' ? img_count : iso_count)++; //0x25 recognizes IMG/IMA/VHD but not ISO/CUE/INS
			fs_count++;
			if (image.image_disk) bootimg = true;
		}
		if (bootimg)
		{
			list.emplace_back(IT_BOOTIMG, 0, "[ Boot from Disk Image ]");
			fs_count++;
		}
		if (dbp_osimages.size())
		{
			list.emplace_back(IT_BOOTOSLIST, 0, "[ Run Installed Operating System ]");
			fs_count++;
		}
		if ((Drives['D'-'A'] && dynamic_cast<isoDrive*>(Drives['D'-'A']) && ((isoDrive*)(Drives['D'-'A']))->CheckBootDiskImage()) || (img_count == 1 && iso_count == 1))
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
		if (DBP_OSDIsStartmenu) list.emplace_back(IT_CLOSEOSD, 0, "Go to Command Line");
		else if (dbp_game_running) list.emplace_back(IT_COMMANDLINE, 0, "Go to Command Line");
		if (!DBP_OSDIsStartmenu) list.emplace_back(IT_CLOSEOSD, 0, "Close Menu");
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

	static int ReadAutoSkip()
	{
		char autostr[DOS_PATHLENGTH + 32] = {0,1}, *pautostr = autostr;
		Bit16u autostrlen = DriveReadFileBytes(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)autostr, (Bit16u)(sizeof(autostr) - 1));
		if (!autostrlen) return 0;
		autostr[autostrlen] = '\0';
		char *skip = strchr(pautostr, '\n');
		while (skip && *skip && *skip <= ' ') skip++;
		return (skip ? atoi(skip) : 0);
	}

	void ReadAutoBoot()
	{
		char autostr[DOS_PATHLENGTH + 32] = {0,1}, *pautostr = autostr;
		Bit16u autostrlen = DriveReadFileBytes(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)autostr, (Bit16u)(sizeof(autostr) - 1));
		autostr[autostrlen] = '\0';
		have_autoboot = (autostrlen != 0);
		if (have_autoboot)
		{
			if (autostr[1] == '*')
			{
				if      (autostr[0] == 'O') { GoToSubMenu(IT_BOOTOSLIST); pautostr += 2; }
				else if (autostr[0] == 'I') { GoToSubMenu(IT_BOOTIMG); pautostr += 2; }
			}
			char *nameend = strchr(pautostr, '\n'), *skip = nameend;
			while (skip && *skip && *skip <= ' ') skip++;
			if (skip) autoskip = atoi(skip);
			while (nameend > pautostr && *nameend <= ' ') nameend--;
			if (nameend) nameend[1] = '\0';
		}
		else if (strrchr(dbp_content_path.c_str(), '#'))
		{
			memcpy(pautostr, "C:\\", 3);
			safe_strncpy(pautostr + 3, strrchr(dbp_content_path.c_str(), '#') + 1, DOS_PATHLENGTH + 16);
		}
		if (!pautostr[0]) return;
		int idx = MenuIndexByString(pautostr);
		if (idx == -1) return;
		ResetSel(idx);
		use_autoboot = have_autoboot = true;
		init_autoboothash = AutoBootHash();
	}

	int AutoBootHash()
	{
		return 1 | ((14 * autoskip) ^ (254 * sel) ^ (4094 * (int)list[sel].type));
	}

	void UpdateAutoBootFile(Bit8u ok_type)
	{
		DBP_ASSERT(ok_type == IT_RUN || ok_type == IT_BOOTIMG || ok_type == IT_BOOTIMG_MACHINE || ok_type == IT_BOOTOS || ok_type == IT_INSTALLOS || ok_type == IT_COMMANDLINE);
		if (have_autoboot && !use_autoboot)
		{
			Drives['C'-'A']->FileUnlink((char*)"AUTOBOOT.DBP");
		}
		else if (use_autoboot && (!have_autoboot || init_autoboothash != AutoBootHash()) && ok_type != IT_INSTALLOS && ok_type != IT_COMMANDLINE)
		{
			char autostr[DOS_PATHLENGTH + 32], *pautostr = autostr;
			if (ok_type != IT_RUN) { autostr[0] = (ok_type == IT_BOOTOS ? 'O' : 'I'); autostr[1] = '*'; pautostr = autostr + 2; }
			char* autoend = pautostr + snprintf(pautostr, (&autostr[sizeof(autostr)] - pautostr), "%s", list[sel].str.c_str());
			if (autoskip) autoend += snprintf(autoend, (&autostr[sizeof(autostr)] - autoend), "\r\n%d", autoskip);
			if (!DriveCreateFile(Drives['C'-'A'], "AUTOBOOT.DBP", (Bit8u*)autostr, (Bit32u)(autoend - autostr))) { DBP_ASSERT(false); }
		}
	}

	int MenuIndexByString(const char* findit)
	{
		for (Item& it : list)
			if ((it.type == IT_RUN || it.type == IT_BOOTOS || it.type == IT_BOOTIMG_MACHINE) && it.str == findit)
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
			DoInput(RES_OK, type, 0);
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

		bool autostart_info = false;
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
			else if (item.type == IT_RUN || item.type == IT_BOOTOS || item.type == IT_BOOTIMG_MACHINE)
			{
				int off = ((item.type != IT_RUN || multidrive) ? 0 : 3), len = strlen - off, lblx = (w - buf.CW*len) / 2;
				buf.Print(lh, lblx,       y, item.str.c_str() + off, (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL));
				if (i != se) continue;
				buf.Print(lh, lblx - buf.CW*(2      ), y, "*", buf.COL_WHITE);
				buf.Print(lh, lblx + buf.CW*(len + 1), y, "*", buf.COL_WHITE);
				autostart_info = use_autoboot;

				if (use_autoboot)
				{
					buf.Print(lh, lblx + buf.CW*(len + 1), y, "* [SET AUTO START]", buf.COL_WHITE);
				}
			}
			else buf.Print(lh, (w - buf.CW*strlen) / 2, y, item.str.c_str(), (item.type != IT_NONE ? (i == se ? buf.COL_HIGHLIGHT : buf.COL_NORMAL) : (item.info == INFO_HEADER ? buf.COL_HEADER : (item.info == INFO_WARN ? buf.COL_WARN : buf.COL_NORMAL))));
		}

		if (inforow)
		{
			char skiptext[38];
			if (!use_autoboot) skiptext[0] = '\0';
			else if (autoskip) snprintf(skiptext, sizeof(skiptext), "Skip showing first %d frames", autoskip);
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
			
			if (w < 640 && use_autoboot)
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
		if (use_autoboot && auto_change > 0) autoskip += (autoskip < 50 ? 10 : (autoskip < 150 ? 25 : (autoskip < 300 ? 50 : 100)));
		if (!use_autoboot && auto_change > 0) use_autoboot = true;
		if (auto_change < 0) autoskip -= (autoskip <= 50 ? 10 : (autoskip <= 150 ? 25 : (autoskip <= 300 ? 50 : 100)));
		if (autoskip < 0) { use_autoboot = false; autoskip = 0; }

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
			if (!have_autoboot && DBP_OSDIsStartmenu && control->GetSection("dosbox")->GetProp("machine")->getChange() == Property::Changeable::OnlyByConfigProgram)
			{
				// Machine property was fixed by DOSBOX.CONF and cannot be modified here, so automatically boot the image as is
				goto handle_result;
			}
			list.clear();
			list.emplace_back(IT_NONE, INFO_HEADER, "Select Boot System Mode");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_BOOTIMG_MACHINE, 's', "SVGA (Super Video Graphics Array)");
			list.emplace_back(IT_BOOTIMG_MACHINE, 'v', "VGA (Video Graphics Array)");
			list.emplace_back(IT_BOOTIMG_MACHINE, 'e', "EGA (Enhanced Graphics Adapter");
			list.emplace_back(IT_BOOTIMG_MACHINE, 'c', "CGA (Color Graphics Adapter)");
			list.emplace_back(IT_BOOTIMG_MACHINE, 't', "Tandy (Tandy Graphics Adapter");
			list.emplace_back(IT_BOOTIMG_MACHINE, 'h', "Hercules (Hercules Graphics Card)");
			list.emplace_back(IT_BOOTIMG_MACHINE, 'p', "PCjr");
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_CANCEL, 0, "Cancel");
			const std::string& img = (!dbp_images.empty() ? dbp_images[dbp_image_index].path : list[1].str);
			bool isPCjrCart = (img.size() > 3 && (img[img.size()-3] == 'J' || img[img.size()-2] == 'T'));
			for (const Item& it : list)
				if (it.info == (isPCjrCart ? 'p' : dbp_last_machine))
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
			const char* filename;
			std::string savefile = DBP_GetSaveFile(SFT_VIRTUALDISK, &filename);

			list.emplace_back(IT_NONE, INFO_HEADER, "Select Operating System Disk Image");
			list.emplace_back(IT_NONE);
			for (const std::string& im : dbp_osimages)
				{ list.emplace_back(IT_BOOTOS, (Bit16s)(&im - &dbp_osimages[0])); list.back().str.assign(im.c_str(), im.size()-4); }
			if (dbp_system_cached) { list.emplace_back(IT_NONE); list.emplace_back(IT_NONE, INFO_WARN, "To Refresh: Audio Options > MIDI Output > Scan System Directory"); }
			list.emplace_back(IT_NONE);
			list.emplace_back(IT_NONE, INFO_WARN, "Changes made to the D: drive will be stored in the following location:");
			if (filename > &savefile[0]) { list.emplace_back(IT_NONE, INFO_WARN); list.back().str.assign(&savefile[0], filename - &savefile[0]); }
			list.emplace_back(IT_NONE, INFO_WARN, filename);
			ResetSel(2, true);
		}
		else if (((res == RES_CANCEL && list.back().type == IT_CLOSEOSD) || res == RES_CLOSESCREENKEYBOARD) && !DBP_OSDIsStartmenu)
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
			if (ok_type != IT_CLOSEOSD)
			{
				if (!show_popup && dbp_game_running)
				{
					popupsel = 0;
					show_popup = true;
					return;
				}
				UpdateAutoBootFile(ok_type);
				Run(item, autoskip);
			}
			DBP_CloseOSD();
		}
	}

	static Item reboot;

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

	struct BatchFileRun : BatchFile
	{
		BatchFileRun(const std::string& _exe) : BatchFile(first_shell,_exe.c_str(),"","") { }
		virtual bool ReadLine(char * line)
		{
			char *p = (char*)filename.c_str(), *f = strrchr(p, '\\') + 1, *fext;
			*(line++) = '@';
			switch (location++)
			{
				case 0:
				{
					ConsoleClearScreen();

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
				if (location > 2) { reboot.type = IT_NONE; DBP_OnBIOSReboot(); }
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

		static bool HaveISO()
		{
			for (DBP_Image& i : dbp_images) if ((i.path[i.path.size()-2]|0x25) != 'm') return true; //0x25 recognizes IMG/IMA/VHD but not ISO/CUE/INS
			return false;
		}

		static void BootImage(char want_machine)
		{
			if (dbp_last_machine != want_machine)
			{
				dbp_reboot_machine = want_machine;
				DBP_OnBIOSReboot();
				return;
			}

			DBP_ASSERT(!dbp_images.empty()); // IT_BOOTIMG should only be available if this is true
			if (!dbp_images.empty())
			{
				DBP_Mount(); // make sure something is mounted

				// If hard disk image was mounted to D:, swap it to be the bootable C: drive
				std::swap(imageDiskList['D'-'A'], imageDiskList['C'-'A']);

				// If there is no mounted hard disk image but a D: drive, setup the CDROM IDE controller
				if (!imageDiskList['C'-'A'] && Drives['D'-'A'])
					IDE_SetupControllers(HaveISO() ? 'D' : 0);
			}

			RunBatchFile(new BatchFileBoot(imageDiskList['A'-'A'] ? 'A' : 'C'));
		}

		static bool BootOSMountIMG(char drive, const char* path, const char* type, bool needwritable, bool complainnotfound)
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

		static void BootOS(Bit8u result, int info)
		{
			// Make sure we have at least 32 MB of RAM, if not set it to 64
			if ((MEM_TotalPages() / 256) < 32)
			{
				dbp_reboot_set64mem = true;
				DBP_OnBIOSReboot();
				return;
			}

			std::string path;
			if (result == IT_BOOTOS)
			{
				path = DBP_GetSaveFile(SFT_SYSTEMDIR).append(dbp_osimages[info]);
			}
			else if (info) //IT_INSTALLOS
			{
				const char* filename;
				path = DBP_GetSaveFile(SFT_NEWOSIMAGE, &filename);

				// Create a new empty hard disk image of the requested size
				memoryDrive* memDrv = new memoryDrive();
				DBP_SetDriveLabelFromContentPath(memDrv, path.c_str(), 'C', filename, path.c_str() + path.size() - 3);
				imageDisk* memDsk = new imageDisk(memDrv, (Bit32u)(info*8));
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
				reboot.type = IT_BOOTOS;
				reboot.info = (int)dbp_osimages.size();
				dbp_osimages.emplace_back(filename);
			}

			const bool have_iso = HaveISO();
			if (!path.empty())
			{
				// When booting an external disk image as C:, use whatever is C: in DOSBox DOS as the second hard disk in the booted OS (it being E: in Drives[] doesn't matter)
				char newC = ((have_iso || DBP_IsMounted('D')) ? 'E' : 'D'); // alternative would be to do DBP_Remount('D', 'E'); and always use 'D'
				if (imageDiskList['C'-'A'])
					imageDiskList[newC-'A'] = imageDiskList['C'-'A'];
				else if (!BootOSMountIMG(newC, (dbp_content_path + ".img").c_str(), "D: drive image", true, false) && Drives['C'-'A'])
				{
					Bit32u save_hash = 0;
					DBP_SetDriveLabelFromContentPath(Drives['C'-'A'], dbp_content_path.c_str(), 'C', NULL, NULL, true);
					std::string save_path = DBP_GetSaveFile(SFT_VIRTUALDISK, NULL, &save_hash);
					imageDiskList[newC-'A'] = new imageDisk(Drives['C'-'A'], atoi(retro_get_variable("dosbox_pure_bootos_dfreespace", "1024")), save_path.c_str(), save_hash, &dbp_vdisk_filter);
				}

				// Ramdisk setting must be false while installing os
				char ramdisk = (result == IT_INSTALLOS ? 'f' : retro_get_variable("dosbox_pure_bootos_ramdisk", "false")[0]);

				// Now mount OS hard disk image as C: drive
				BootOSMountIMG('C', path.c_str(), "OS image", (ramdisk != 't'), true);
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
			IDE_SetupControllers(have_iso ? 'D' : 0);

			// Switch cputype to highest feature set (needed for Windows 9x) and increase real mode CPU cycles
			Section* section = control->GetSection("cpu");
			section->ExecuteDestroy(false);
			section->HandleInputline("cputype=pentium_slow");
			if (retro_get_variable("dosbox_pure_bootos_forcenormal", "false")[0] == 't') section->HandleInputline("core=normal");
			section->ExecuteInit(false);
			if (Property* p = section->GetProp("cputype")) p->OnChangedByConfigProgram();
			if (dbp_content_year < 1993) { dbp_content_year = 1993; DBP_SetRealModeCycles(); }

			RunBatchFile(new BatchFileBoot(result == IT_BOOTOS ? 'C' : 'A'));
		}
	};

	static void Run(Item& it, int doautoskip)
	{
		DBP_ASSERT(it.type == IT_RUN || it.type == IT_BOOTIMG || it.type == IT_BOOTIMG_MACHINE || it.type == IT_BOOTOS || it.type == IT_INSTALLOS || it.type == IT_COMMANDLINE);
		Bit8u res = it.type;
		reboot.type = ((res == IT_RUN || res == IT_COMMANDLINE) ? IT_NONE : res); // if a booted OS does a bios reboot, auto reboot that OS from now on
		reboot.info = it.info;
		std::swap(reboot.str, it.str); // remember to set cursor again and for rebooting a different IT_RUN

		if (dbp_game_running)
		{
			if (res == IT_RUN || res == IT_COMMANDLINE) reboot.type = res;
			if (res == IT_BOOTIMG_MACHINE && dbp_last_machine != (char)reboot.info) dbp_reboot_machine = (char)reboot.info;
			if (res == IT_BOOTIMG && dbp_last_machine) dbp_reboot_machine = dbp_last_machine;
			DBP_OnBIOSReboot();
			return;
		}

		if (doautoskip)
			DBP_UnlockSpeed(true, doautoskip, true);

		if (res == IT_RUN)
		{
			RunBatchFile(new BatchFileRun(reboot.str));
		}
		else if (res == IT_BOOTIMG || res == IT_BOOTIMG_MACHINE)
		{
			BatchFileBoot::BootImage(res == IT_BOOTIMG ? dbp_last_machine : (char)reboot.info);
		}
		else if (res == IT_BOOTOS || res == IT_INSTALLOS)
		{
			BatchFileBoot::BootOS(res, reboot.info);
		}
	}
};

DBP_MenuState::Item DBP_PureMenuState::reboot;

struct DBP_OnScreenDisplay
{
	DBP_OSDMode mode;
	DBP_PureMenuState* main;
	DBP_OnScreenKeyboardState* osk;
	DBP_MapperMenuState* mapper;
	DBP_MenuMouse mouse;

	DBP_OnScreenDisplay() : main(NULL), osk(NULL), mapper(NULL) {}

	void SetMode(DBP_OSDMode in_mode, DBP_PureMenuState* in_main = NULL)
	{
		mode = in_mode;
		delete main; main = NULL;
		delete osk; osk = NULL;
		delete mapper; mapper = NULL;
		dbp_intercept_data = this;
		dbp_intercept_gfx = DBP_OnScreenDisplay::gfx;
		dbp_intercept_input = DBP_OnScreenDisplay::input;
		switch (mode)
		{
			case DBPOSD_MAIN:
				main = (in_main ? in_main : new DBP_PureMenuState());
				break;
			case DBPOSD_OSK:
				osk = new DBP_OnScreenKeyboardState();
				break;
			case DBPOSD_MAPPER:
				mapper = new DBP_MapperMenuState();
				break;
			case DBPOSD_CLOSED:
				dbp_intercept_data = NULL;
				dbp_intercept_gfx = NULL;
				dbp_intercept_input = NULL;
		}
		DBP_KEYBOARD_ReleaseKeys();
	}

	void GFX(DBP_BufferDrawing& buf)
	{
		int w = buf.width, h = buf.height, lh = (h >= 400 ? 14 : 8), ftr = lh+20;
		bool isOSK = (mode == DBPOSD_OSK);
		bool mouseMoved = mouse.Update(buf, isOSK);

		Bit32u blend = (DBP_OSDIsStartmenu ? 0xFF000000 : 0x00000000);
		if (DBP_OSDIsStartmenu || !isOSK)
		{
			int btny = buf.height - 13 - lh;
			int n = (DBP_OSDIsStartmenu ? 2 : 3);
			if (n == 2) buf.FillRect(0, 0, w, h, buf.BGCOL_STARTMENU);
			if (          buf.DrawButton(blend, btny, lh, 0,   n, mode == DBPOSD_MAIN,   mouse, (w < 500 ? "STARTMENU" : "START MENU"))        && mouse.left_up) SetMode(DBPOSD_MAIN);
			if (n == 3 && buf.DrawButton(blend, btny, lh, 1,   n, mode == DBPOSD_OSK,    mouse, (w < 500 ? "KEYBOARD" : "ON-SCREEN KEYBOARD")) && mouse.left_up) SetMode(DBPOSD_OSK);
			if (          buf.DrawButton(blend, btny, lh, n-1, n, mode == DBPOSD_MAPPER, mouse, (w < 500 ? "CONTROLS" : "CONTROLLER MAPPER"))  && mouse.left_up) SetMode(DBPOSD_MAPPER);
		}

		switch (mode)
		{
			case DBPOSD_MAIN:
				main->DrawMenu(buf, blend, lh, w, h, ftr, mouseMoved, mouse);
				break;
			case DBPOSD_OSK:
				osk->GFX(buf, mouse);
				break;
			case DBPOSD_MAPPER:
				mapper->DrawMenu(buf, blend, lh, w, h, ftr, mouseMoved, mouse);
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
				main->Input(type, val, val2);
				break;
			case DBPOSD_OSK:
				osk->Input(type, val, val2);
				break;
			case DBPOSD_MAPPER:
				mapper->Input(type, val, val2);
				break;
		}
		if (type == DBPET_KEYUP && ((KBD_KEYS)val == KBD_tab || (KBD_KEYS)val == KBD_grave))
		{
			int add = (((KBD_KEYS)val == KBD_tab && !DBP_IsKeyDown(KBD_leftshift) && !DBP_IsKeyDown(KBD_rightshift)) ? 1 : 2);
			SetMode(DBP_OSDIsStartmenu ? (mode == DBPOSD_MAIN ? DBPOSD_MAPPER : DBPOSD_MAIN) : (DBP_OSDMode)((mode + add) % _DBPOSD_COUNT));
		}
	}

	static void gfx(DBP_Buffer& buf, void* data) { ((DBP_OnScreenDisplay*)data)->GFX(static_cast<DBP_BufferDrawing&>(buf)); }
	static void input(DBP_Event_Type type, int val, int val2, void* data) { ((DBP_OnScreenDisplay*)data)->Input(type, val, val2); }
};

static DBP_OnScreenDisplay DBP_OSD;
static void DBP_StartOSD(DBP_OSDMode mode, DBP_PureMenuState* in_main)
{
	DBP_OSDIsStartmenu = (in_main != NULL);
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
		Bit32u opentime;
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
			if (type == DBPET_KEYUP || type == DBPET_MOUSEUP || type == DBPET_JOY1UP || type == DBPET_JOY2UP)
				if ((DBP_GetTicks() - ((Menu*)self)->opentime) > 300)
					((Menu*)self)->pressedAnyKey = true;
		}

		bool WaitAnyKeyPress(Bit32u tick_limit = 0)
		{
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
			enum { M_NORMAL, M_BOOT, M_FINISH, M_REBOOT } m = (cmd->FindExist("-BOOT") ? M_BOOT : cmd->FindExist("-FINISH") ? M_FINISH : cmd->FindExist("-REBOOT") ? M_REBOOT : M_NORMAL);

			if (m == M_REBOOT && DBP_PureMenuState::reboot.type)
			{
				DBP_PureMenuState::Run(DBP_PureMenuState::reboot, DBP_PureMenuState::ReadAutoSkip());
				return;
			}

			opentime = DBP_GetTicks();

			DBP_OSDIsStartmenu = true;
			DBP_PureMenuState* ms = new DBP_PureMenuState();
			bool always_show_menu = (dbp_menu_time == (char)-1 || (m == M_FINISH && (opentime - dbp_lastmenuticks) < 500));
			bool auto_boot = (!always_show_menu && ((ms->exe_count == 1 && ms->fs_count <= 1) || ms->use_autoboot));

			#ifndef STATIC_LINKING
			if (m == M_FINISH && auto_boot)
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

			// Show menu on image auto boot when there are EXE files (some games need to run a FIX.EXE before booting)
			if (m == M_BOOT && auto_boot)
				ms->Run(ms->list[ms->sel], ms->autoskip);
			else if (m != M_BOOT || ms->exe_count != 0 || ms->fs_count != 0 || Drives['C'-'A'] || Drives['A'-'A'] || Drives['D'-'A'])
				DBP_StartOSD(DBPOSD_MAIN, ms);
		}
	};
	*make = new Menu;
}
