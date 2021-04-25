#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "id.h"
#include "ebmusv2.h"

HWND hwndTracker;
static HWND hwndOrder;
static HWND hwndState;

#define IDC_CHANSTATE_CAPTION 1 // child of hwndState
static char cs_title[] = "Channel state (0)";

#define IDC_ORDER 1
#define IDC_REP_CAPTION 2
#define IDC_REPEAT 3
#define IDC_REP_POS_CAPTION 4
#define IDC_REPEAT_POS 5
#define IDC_PAT_LIST_CAPTION 6
#define IDC_PAT_LIST 7
#define IDC_PAT_ADD 8
#define IDC_PAT_INS 9
#define IDC_PAT_DEL 10
#define IDC_TRACKER 15
#define IDC_STATE 16
#define IDC_EDITBOX_CAPTION 17
#define IDC_EDITBOX 18
#define IDC_ENABLE_CHANNEL_0 20

static const struct control_desc editor_controls[] = {
// Upper
	{ "Static",          10, 10, 35, 20, "Order:", 0, 0 },
	{ "ebmused_order",   50, 10,-420,20, NULL, IDC_ORDER, WS_BORDER },
	{ "Static",        -365, 10, 45, 20, "Repeat", IDC_REP_CAPTION, 0 },
	{ "Edit",          -315, 10, 30, 20, NULL, IDC_REPEAT, WS_BORDER | ES_NUMBER },
	{ "Static",        -280, 10, 45, 20, "Rep. pos", IDC_REP_POS_CAPTION, 0 },
	{ "Edit",          -230, 10, 30, 20, NULL, IDC_REPEAT_POS, WS_BORDER | ES_NUMBER },
	{ "Static",        -195, 10, 45, 20, "Pattern:", IDC_PAT_LIST_CAPTION, 0 },
	{ "ComboBox",      -145, 10, 40,300, NULL, IDC_PAT_LIST, CBS_DROPDOWNLIST | WS_VSCROLL },
	{ "Button",        -100, 10, 30, 20, "Add", IDC_PAT_ADD, 0 },
	{ "Button",         -70, 10, 30, 20, "Ins", IDC_PAT_INS, 0 },
	{ "Button",         -40, 10, 30, 20, "Del", IDC_PAT_DEL, 0 },
	{ "ebmused_tracker", 10, 60,-20,-70, NULL, IDC_TRACKER, WS_BORDER | WS_VSCROLL },
// Lower
	{ "ebmused_state",   10,  0,430,-10, NULL, IDC_STATE, 0 },
	{ "Static",         450,  0,100, 15, NULL, IDC_EDITBOX_CAPTION, 0 },
	{ "Edit",           450,15,-460,-25, NULL, IDC_EDITBOX, WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL },
};
static struct window_template editor_template = {
	15, 3, 0, 0, editor_controls
};

static const struct control_desc state_controls[] = {
	{ "Button",           0,  0,150,  0, "Global state", 0, BS_GROUPBOX },
	{ "Button",         160,  0,270,  0, cs_title, IDC_CHANSTATE_CAPTION, BS_GROUPBOX },
};
static struct window_template state_template = { 2, 2, 0, 0, state_controls };

static int pos_width, font_height;
static const BYTE zoom_levels[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 96 };
static int zoom = 6, zoom_idx = 4;
static int tracker_width = 0;
static int tracker_height;
static BOOL editbox_had_focus;

static int cursor_chan;
// the following 4 variables are all set by cursor_moved()
// current track or subroutine cursor is in
static struct track *cursor_track;
static BYTE *sel_from;
static BYTE *sel_start, *sel_end;
// these are what must be updated before calling cursor_moved()
int cursor_pos;
static struct parser cursor;

static int pat_length;
static PAINTSTRUCT ps;

void tracker_scrolled() {
	SetScrollPos(hwndTracker, SB_VERT, state.patpos, TRUE);
	InvalidateRect(hwndTracker, NULL, FALSE);
	InvalidateRect(hwndState, NULL, FALSE);
}

static void scroll_to(int new_pos) {
	if (new_pos == state.patpos) return;
	if (new_pos < state.patpos)
		state = pattop_state;
	while (state.patpos < new_pos && do_cycle_no_sound(&state));
	tracker_scrolled();
}

static COLORREF get_bkcolor(int sub_loops) {
	if (sub_loops == 0)
		return 0xFFFFFF;
	int c = 0x808080;
	if (sub_loops & 1) c += 0x550000;
	if (sub_loops & 2) c += 0x005500;
	if (sub_loops & 4) c += 0x000055;
	if (sub_loops & 8) c += 0x2A2A2A;
	return c;
}

static void get_font_size(HWND hWnd) {
	TEXTMETRIC tm;
	HDC hdc = GetDC(hWnd);
	HFONT oldfont = SelectObject(hdc, hfont);
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, oldfont);
	ReleaseDC(hWnd, hdc);
	pos_width = tm.tmAveCharWidth * 6;
	font_height = tm.tmHeight;
}

static void get_sel_range() {
	BYTE *s = sel_from;
	BYTE *e = cursor.ptr;

	if (e == NULL) {
		sel_start = NULL;
		sel_end = NULL;
	} else {
		if (s > e) { BYTE *tmp = s; s = e; e = tmp; }
		if (*e != 0) e = next_code(e);
		sel_start = s;
		sel_end = e;
	}
}

static void show_track_text() {
	char *txt = NULL;
	struct track *t = cursor_track;
	if (t->size) {
		txt = malloc(text_length(t->track, t->track + t->size));
		track_to_text(txt, t->track, t->size);
	}
	SetDlgItemText(hwndEditor, IDC_EDITBOX, txt);
	free(txt);
}

static void cursor_moved(BOOL select) {
	char caption[23];
	struct track *t;

	if (!cur_song.order_length) return;

	int ycoord = (cursor_pos - state.patpos) * font_height / zoom;
	if (ycoord < 0) {
		scroll_to(cursor_pos);
	} else if (ycoord + font_height > tracker_height) {
		scroll_to((cursor_pos + zoom) - (tracker_height * zoom / font_height));
	}

	if (cursor.sub_count) {
		t = &cur_song.sub[cursor.sub_start];
		sprintf(caption, "Subroutine %d", cursor.sub_start);
	} else {
		int ch = cursor_chan;
		t = &cur_song.pattern[cur_song.order[state.ordnum]][ch];
		sprintf(caption, "Track %d", ch);
		if (cursor.ptr == NULL)
			strcat(caption, " (not present)");
	}
	printf("t = %p\n", t);
	if (t != cursor_track) {
		SetDlgItemText(hwndEditor, IDC_EDITBOX_CAPTION, caption);
		cursor_track = t;
		show_track_text();
	}

	if (!select) sel_from = cursor.ptr;
	get_sel_range();
	if (cursor.ptr != NULL) {
		int esel_start = text_length(t->track, sel_start);
		int esel_end = esel_start + text_length(sel_start, sel_end) - 1;
		SendDlgItemMessage(hwndEditor, IDC_EDITBOX, EM_SETSEL, esel_start, esel_end);
		SendDlgItemMessage(hwndEditor, IDC_EDITBOX, EM_SCROLLCARET, 0, 0);
	}
	InvalidateRect(hwndTracker, NULL, FALSE);
}

static void set_cur_chan(int ch) {
	cursor_chan = ch;
	cs_title[15] = '0' + ch;
	SetDlgItemText(hwndState, IDC_CHANSTATE_CAPTION, cs_title);
	InvalidateRect(hwndState, NULL, FALSE);
}

void load_pattern_into_tracker() {
	if (hwndTracker == NULL) return;

	InvalidateRect(hwndOrder, NULL, FALSE);
	InvalidateRect(hwndState, NULL, FALSE);
	SendDlgItemMessage(hwndEditor, IDC_PAT_LIST,
		CB_SETCURSEL, cur_song.order[pattop_state.ordnum], 0);

	parser_init(&cursor, &state.chan[cursor_chan]);
	cursor_pos = state.patpos + state.chan[cursor_chan].next;
	cursor_track = NULL;
	cursor_moved(FALSE);

	pat_length = 0;
	for (int ch = 0; ch < 8; ch++) {
		if (pattop_state.chan[ch].ptr == NULL) continue;
		struct parser p;
		parser_init(&p, &pattop_state.chan[ch]);
		do {
			if (*p.ptr >= 0x80 && *p.ptr < 0xE0)
				pat_length += p.note_len;
		} while (parser_advance(&p));
		break;
	}
/*	{	SCROLLINFO si;
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_PAGE | SIF_RANGE;
		si.nPage = 96;
		si.nMin = 0;
		si.nMax = pat_length + 95;
		SetScrollInfo(hwndTracker, SB_VERT, &si, TRUE);
	}*/
	SetScrollRange(hwndTracker, SB_VERT, 0, pat_length, TRUE);

}

static void pattern_changed() {
	int pos = state.patpos;
	scroll_to(0);
	state.ordnum--;
	load_pattern();
	scroll_to(pos);
	load_pattern_into_tracker();
	cur_song.changed = TRUE;
}

static BOOL cursor_home(BOOL select);
static BOOL cursor_fwd(BOOL select);
static void restore_cursor(struct track *t, int offset) {
	BYTE *target_ptr = t->track + offset;
	cursor_home(FALSE);
	do {
		if (cursor.ptr == target_ptr)
			break;
	} while (cursor_fwd(FALSE));
	cursor_moved(FALSE);
}

BOOL CALLBACK TransposeDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_INITDIALOG:
		// need to return TRUE to set default focus
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
			EndDialog(hWnd, GetDlgItemInt(hWnd, 3, NULL, TRUE));
		else if (LOWORD(wParam) == IDCANCEL)
			EndDialog(hWnd, 0);
		break;
	default: return FALSE;
	}
	return TRUE;
}

static WNDPROC EditWndProc;
// Custom window procedure for the track/subroutine Edit control
static LRESULT CALLBACK TrackEditWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_SETFOCUS) {
		editbox_had_focus = TRUE;
	} else if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE) {
		SetFocus(hwndTracker);
		return 0;
	} else if (uMsg == WM_CHAR && wParam == '\r') {
		int len = GetWindowTextLength(hWnd) + 1;
		char *p = malloc(len);
		struct parser c = cursor;
		GetWindowText(hWnd, p, len);
		if (text_to_track(p, cursor_track, c.sub_count)) {
			// Find out where the editbox's caret was, and
			// move the tracker cursor appropriately.
			DWORD start;
			SendMessage(hWnd, EM_GETSEL, (WPARAM)&start, 0);
			p[start] = '\0';
			struct track *t = cursor_track;
			int new_pos = calc_track_size_from_text(p);
			pattern_changed();
			// XXX: may point to middle of a code
			restore_cursor(t, new_pos);
			SetFocus(hwndTracker);
		}
		free(p);
		return 0;
	}
	return CallWindowProc(EditWndProc, hWnd, uMsg, wParam, lParam);
}

static void goto_order(int pos) {
	int i;
	initialize_state();
	for (i = 0; i < pos; i++) {
		int ch;
		for (ch = 0; ch < 8 && state.chan[ch].ptr != NULL; ch++)
		// an empty pattern will loop forever, so skip it
		if (ch != 8) {
			while (do_cycle_no_sound(&state));
		}
		load_pattern();
	}
	load_pattern_into_tracker();
}

static void pattern_added() {
	char buf[6];
	sprintf(buf, "%d", cur_song.patterns - 1);
	SendDlgItemMessage(hwndEditor, IDC_PAT_LIST, CB_ADDSTRING,
		0, (LPARAM)buf);
}

static void pattern_deleted() {
	SendDlgItemMessage(hwndEditor, IDC_PAT_LIST, CB_DELETESTRING,
		cur_song.patterns, 0);
}

static void show_repeat() {
	SetDlgItemInt(hwndEditor, IDC_REPEAT, cur_song.repeat, FALSE);
	SetDlgItemInt(hwndEditor, IDC_REPEAT_POS, cur_song.repeat_pos, FALSE);
}

LRESULT CALLBACK EditorWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static const BYTE editor_menu_cmds[] = {
		ID_CUT, ID_COPY, ID_PASTE, ID_DELETE,
		ID_SPLIT_PATTERN, ID_JOIN_PATTERNS,
		ID_MAKE_SUBROUTINE, ID_UNMAKE_SUBROUTINE, ID_TRANSPOSE,
		ID_CLEAR_SONG,
		ID_ZOOM_IN, ID_ZOOM_OUT,
		ID_INCREMENT_DURATION, ID_DECREMENT_DURATION,
		ID_SET_DURATION_1, ID_SET_DURATION_2,
		ID_SET_DURATION_3, ID_SET_DURATION_4,
		ID_SET_DURATION_5, ID_SET_DURATION_6,
		0
	};
	switch (uMsg) {
	case WM_CREATE:
		get_font_size(hWnd);
		editor_template.divy = ((CREATESTRUCT *)lParam)->cy - (font_height * 7 + 17);
		create_controls(hWnd, &editor_template, lParam);
		for (int i = 0; i < 8; i++) {
			char buf[2] = { '0' + i, 0 };
			HWND b = CreateWindow("Button", buf,
				WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0,
				hWnd, (HMENU)(IDC_ENABLE_CHANNEL_0 + i), hinstance, NULL);
			SendMessage(b, BM_SETCHECK, chmask >> i & 1, 0);
		}
		EditWndProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hWnd, IDC_EDITBOX), GWLP_WNDPROC, (LONG_PTR)TrackEditWndProc);
		break;
	case WM_SONG_IMPORTED:
	case WM_SONG_LOADED:
		EnableWindow(hWnd, TRUE);
		enable_menu_items(editor_menu_cmds, MF_ENABLED);
		show_repeat();
		HWND cb = GetDlgItem(hWnd, IDC_PAT_LIST);
		SendMessage(cb, CB_RESETCONTENT, 0, 0);
		for (int i = 0; i < cur_song.patterns; i++) {
			char buf[5];
			sprintf(buf, "%d", i);
			SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)buf);
		}
		load_pattern_into_tracker();
		break;
	case WM_ROM_CLOSED:
	case WM_SONG_NOT_LOADED:
		EnableWindow(hWnd, FALSE);
		enable_menu_items(editor_menu_cmds, MF_GRAYED);
		break;
	case WM_DESTROY:
		save_cur_song_to_pack();
		enable_menu_items(editor_menu_cmds, MF_GRAYED);
		break;
	case WM_COMMAND: {
		int id = LOWORD(wParam);
		if (id == IDC_REPEAT || id == IDC_REPEAT_POS) {
			if (HIWORD(wParam) != EN_KILLFOCUS) break;
			BOOL success;
			UINT n = GetDlgItemInt(hWnd, id, &success, FALSE);
			int *p = id == IDC_REPEAT ? &cur_song.repeat : &cur_song.repeat_pos;
			if (success) {
				UINT limit = (id == IDC_REPEAT ? 256 : cur_song.order_length);
				if (n < limit && *p != n) {
					*p = n;
					cur_song.changed = TRUE;
				}
			}
			SetDlgItemInt(hWnd, id, *p, FALSE);
		} else if (id == IDC_PAT_LIST) {
			if (HIWORD(wParam) != CBN_SELCHANGE) break;
			cur_song.order[state.ordnum] =
				SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
			scroll_to(0);
			pattern_changed();
		} else if (id == IDC_PAT_ADD || id == IDC_PAT_INS) {
			int pat = id == IDC_PAT_ADD ? cur_song.patterns : cur_song.order[state.ordnum];
			int ord = cur_song.order_length;
			if (id == IDC_PAT_ADD)
			{
				struct track *t = pattern_insert(pat);
				memset(t, 0, sizeof(struct track) * 8);
				pattern_added();
			}
			order_insert(ord, pat);
			goto_order(ord);
			cur_song.changed = TRUE;
		} else if (id == IDC_PAT_DEL) {
			if (cur_song.patterns == 1) break;
			pattern_delete(cur_song.order[state.ordnum]);
			pattern_deleted();
			goto_order(state.ordnum + 1);
			cur_song.changed = TRUE;
			show_repeat();
		} else if (id >= IDC_ENABLE_CHANNEL_0) {
			chmask ^= 1 << (id - IDC_ENABLE_CHANNEL_0);
		}
		break;
	}
	case WM_SIZE:
		editor_template.divy = HIWORD(lParam) - (font_height * 7 + 17);
		move_controls(hWnd, &editor_template, lParam);
		int start = 10 + GetSystemMetrics(SM_CXBORDER) + pos_width;
		int right = start;
		for (int i = 0; i < 8; i++) {
			int left = right + 1;
			right = start + (tracker_width * (i + 1) >> 3);
			MoveWindow(GetDlgItem(hWnd, IDC_ENABLE_CHANNEL_0+i),
				left, 40, right - left, 20, TRUE);
		}
		break;
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK OrderWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE:
		hwndOrder = hWnd;
		break;
	case WM_LBUTTONDOWN: {
		int pos = LOWORD(lParam) / 25;
		if (pos >= cur_song.order_length) break;
		SetFocus(hWnd);
		goto_order(pos);
		break;
	}
	case WM_KILLFOCUS: InvalidateRect(hWnd, NULL, FALSE); break;
	case WM_KEYDOWN:
		if (wParam == VK_LEFT) {
			goto_order(state.ordnum - 1);
		} else if (wParam == VK_RIGHT) {
			goto_order(state.ordnum + 1);
		} else if (wParam == VK_INSERT) {
			order_insert(state.ordnum + 1, cur_song.order[state.ordnum]);
			show_repeat();
			InvalidateRect(hWnd, NULL, FALSE);
			cur_song.changed = TRUE;
		} else if (wParam == VK_DELETE) {
			if (cur_song.order_length <= 1) break;
			order_delete(state.ordnum);
			show_repeat();
			goto_order(state.ordnum);
			cur_song.changed = TRUE;
		}
		break;
	case WM_PAINT: {
		HDC hdc = BeginPaint(hWnd, &ps);
		RECT rc;
		GetClientRect(hWnd, &rc);
		for (int i = 0; i < cur_song.order_length; i++) {
			char buf[6];
			int len = sprintf(buf, "%d", cur_song.order[i]);
			rc.right = rc.left + 25;
			COLORREF tc, bc;
			if (i == pattop_state.ordnum) {
				tc = SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
				bc = SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
			}
			ExtTextOut(hdc, rc.left, rc.top, ETO_OPAQUE, &rc, buf, len, NULL);
			if (i == pattop_state.ordnum) {
				SetTextColor(hdc, tc);
				SetBkColor(hdc, bc);
				if (GetFocus() == hWnd)
					DrawFocusRect(hdc, &rc);
			}
			rc.left = rc.right;
		}
		rc.right = ps.rcPaint.right;
		FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
		EndPaint(hWnd, &ps);
		break;
	}

	default: return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static void tracker_paint(HWND hWnd) {
	HDC hdc = BeginPaint(hWnd, &ps);
	RECT rc;
	char codes[8];
	int length;
	set_up_hdc(hdc);

	if (cur_song.order_length == 0) {
		static const char str[] = "No song is currently loaded.";
		GetClientRect(hWnd, &rc);
		SetTextAlign(hdc, TA_CENTER);
		int x = (rc.left + rc.right) >> 1;
		int y = (rc.top + rc.bottom - font_height) >> 1;
		ExtTextOut(hdc, x, y, ETO_OPAQUE, &rc, str, sizeof(str) - 1, NULL);
		if (get_cur_block() != NULL && decomp_error) {
			y += font_height;
			TextOut(hdc, x, y, "Additional information:", 23);
			y += font_height;
			TextOut(hdc, x, y, decomp_error, strlen(decomp_error));
		}
		goto paint_end;
	}

	SetTextColor(hdc, 0xFFFFFF);
	SetBkColor(hdc, 0x808080);
	rc.left = 0;
	rc.right = pos_width;
	int pos = state.patpos;
	rc.top = -(pos % zoom);
	pos += rc.top;
	// simulate rounding towards zero, so these numbers
	// will be properly aligned with the notes
	rc.top = (rc.top + zoom) * font_height / zoom - font_height;
	while (rc.top < ps.rcPaint.bottom) {
		int len = sprintf(codes, "%d", pos);
		rc.bottom = rc.top + font_height;
		ExtTextOut(hdc, rc.left, rc.top, ETO_OPAQUE, &rc, codes, len, NULL);
		rc.top = rc.bottom;
		pos += zoom;
	}

	for (int chan = 0; chan < 8; chan++) {
		struct channel_state *cs = &state.chan[chan];
		struct parser p;
		parser_init(&p, cs);
		pos = state.patpos + cs->next;

		rc.left = rc.right + 1; // skip divider
		rc.right = pos_width + (tracker_width * (chan + 1) >> 3);
		rc.top = 0;
		int chan_xleft = rc.left;
		int next_y;

		if (p.ptr == NULL) {
			rc.bottom = ps.rcPaint.bottom;
			FillRect(hdc, &rc, (HBRUSH)(COLOR_GRAYTEXT + 1));
			goto draw_divider;
		}

		rc.bottom = cs->next * font_height / zoom;
		SetTextColor(hdc, 0);
		SetBkColor(hdc, get_bkcolor(p.sub_count));
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
		rc.top = rc.bottom;
		while (rc.top < ps.rcPaint.bottom) {
			// the [00] at the end of the track is not considered part
			// of the selection, but we want to highlight it anyway
			BOOL highlight = (p.ptr >= sel_start && p.ptr < sel_end)
				|| p.ptr == cursor.ptr;
			BOOL real_highlight =
				p.sub_count == cursor.sub_count &&
				p.sub_ret == cursor.sub_ret;

			BYTE chr = *p.ptr;
			SIZE extent;
			if (chr >= 0x80 && chr < 0xE0) {
				length = 3;
				if (chr >= 0xCA)
					length = sprintf(codes, "%02X", chr);
				else if (chr == 0xC9)
					memcpy(codes, "---", 3);
				else if (chr == 0xC8)
					memcpy(codes, "...", 3);
				else {
					chr &= 0x7F;
					memcpy(codes, "C-C#D-D#E-F-F#G-G#A-A#B-"+2*(chr%12), 2);
					codes[2] = '1' + chr/12;
				}

				pos += p.note_len;
				next_y = (pos - state.patpos)*font_height/zoom;
note:			GetTextExtentPoint32(hdc, codes, length, &extent);
				rc.bottom = rc.top + extent.cy;
				SetTextAlign(hdc, TA_RIGHT);
				ExtTextOut(hdc, rc.right - 1, rc.top, ETO_OPAQUE, &rc,
					codes, length, NULL);
				if (highlight) {
					COLORREF bc;
					if (real_highlight) {
						SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
						bc = SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
					} else {
						SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
						bc = SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
					}
					rc.left = rc.right - extent.cx - 2;
					ExtTextOut(hdc, rc.right - 1, rc.top, ETO_OPAQUE, &rc,
						codes, length, NULL);
					SetTextColor(hdc, 0);
					SetBkColor(hdc, bc);
					if (p.ptr == cursor.ptr && GetFocus() == hWnd)
						DrawFocusRect(hdc, &rc);
				}
				SetTextAlign(hdc, TA_LEFT);

				rc.left = chan_xleft;
				rc.top = rc.bottom;
				rc.bottom = next_y;
				ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
				rc.top = rc.bottom;
			} else if (chr == 0) {
				if (p.sub_count == 0) {
					next_y = ps.rcPaint.bottom;
					SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
					SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
					strcpy(codes, "End");
					length = 3;
					goto note;
				}
			} else {
				length = sprintf(codes, "%02X", chr);
				if (chr < 0x80 && p.ptr[1] < 0x80)
					length += sprintf(codes + 2, "%02X", p.ptr[1]);

				if (highlight) {
					if (real_highlight) {
						SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
						SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
					} else {
						SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
						SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
					}
				}
				int r = rc.right;
				GetTextExtentPoint32(hdc, codes, length, &extent);
				rc.right = rc.left + extent.cx + 2;
				rc.bottom = rc.top + extent.cy;
				ExtTextOut(hdc, rc.left + 1, rc.top, ETO_OPAQUE, &rc,
					codes, length, NULL);
				if (highlight) {
					SetTextColor(hdc, 0);
					SetBkColor(hdc, get_bkcolor(p.sub_count));
					if (p.ptr == cursor.ptr && GetFocus() == hWnd)
						DrawFocusRect(hdc, &rc);
				}
				rc.left = rc.right;
				rc.right = r;
			}
			parser_advance(&p);
			if (chr == 0 || chr == 0xEF)
				SetBkColor(hdc, get_bkcolor(p.sub_count));
		}
draw_divider:
		// Why is this all the way down here? Well, it turns out that
		// when ClearType is enabled, TextOut draws one pixel to both
		// the left and right of where you'd expect it to. If this line
		// is drawn before doing the column - as would logically make sense -
		// it gets overwritten, and ugliness ensues.
		MoveToEx(hdc, chan_xleft - 1, ps.rcPaint.top, NULL);
		LineTo(hdc, chan_xleft - 1, ps.rcPaint.bottom);
	}
paint_end:
	reset_hdc(hdc);
	EndPaint(hWnd, &ps);
}

static BOOL cursor_fwd(BOOL select) {
	int byte = *cursor.ptr;
	if (select) {
		// Don't select past end of subroutine
		if (byte == 0x00) return FALSE;
		// Skip subroutines
		if (byte == 0xEF) {
			do
				cursor_fwd(FALSE);
			while (cursor.sub_count != 0);
			return TRUE;
		}
	}
	if (byte >= 0x80 && byte < 0xE0)
		cursor_pos += cursor.note_len;
	return parser_advance(&cursor);
}

static BOOL cursor_home(BOOL select) {
	if (select && cursor.sub_count) {
		// Go to the top of the subroutine
		if (cursor.ptr == cursor_track->track)
			return FALSE;
		// Start from the top of the track, and search down
		struct parser target = cursor;
		if (!cursor_home(FALSE))
			return FALSE;
		do {
			if (!cursor_fwd(FALSE)) {
				// This should never happen
				cursor = target;
				return FALSE;
			}
		} while (cursor.sub_ret != target.sub_ret
		      || cursor.sub_count != target.sub_count);
	} else {
		// Go to the top of the track
		if (cursor.ptr == pattop_state.chan[cursor_chan].ptr)
			return FALSE;
		parser_init(&cursor, &pattop_state.chan[cursor_chan]);
		cursor_pos = 0;
	}
	return TRUE;
}

/// \brief Attempts to move the cursor back by one control code.
/// \return Returns false if the cursor cannot be moved backwards due
/// to already being at the top of the track, otherwise returns true.
static BOOL cursor_back(BOOL select) {
	int prev_pos;
	struct parser prev;
	struct parser target = cursor;
	if (!cursor_home(select))
		return FALSE;
	do {
		prev_pos = cursor_pos;
		prev = cursor;
		if (!cursor_fwd(select)) break;
	} while (cursor.ptr != target.ptr
	      || cursor.sub_ret != target.sub_ret
	      || cursor.sub_count != target.sub_count);
	cursor_pos = prev_pos;
	cursor = prev;
	return TRUE;
}

static BOOL cursor_end(BOOL select) {
	while (cursor_fwd(select));
	return TRUE;
}

static BOOL cursor_on_note() {
	// Consider the ending [00] on a track/subroutine as a note, since it's
	// displayed on the right (for end of track) and you can insert notes there.
	return *cursor.ptr == 0 || (*cursor.ptr >= 0x80 && *cursor.ptr < 0xE0);
}

static BOOL cursor_up(BOOL select) {
	BOOL on_note = cursor_on_note();
	struct parser target = cursor;
	if (!cursor_home(select))
		return FALSE;
	int prev_pos;
	struct parser prev;
	prev.ptr = NULL;
	if (on_note) {
		// find previous note
		do {
			if (cursor_on_note()) {
				prev_pos = cursor_pos;
				prev = cursor;
			}
			if (!cursor_fwd(select)) break;
		} while (cursor.ptr != target.ptr
		      || cursor.sub_ret != target.sub_ret
		      || cursor.sub_count != target.sub_count);
	} else {
		// find previous start-of-line code
		BOOL at_start = TRUE;
		do {
			if (cursor_on_note()) {
				at_start = TRUE;
			} else if (at_start) {
				prev_pos = cursor_pos;
				prev = cursor;
				at_start = FALSE;
			}
			if (!cursor_fwd(select)) break;
		} while (cursor.ptr != target.ptr
		      || cursor.sub_ret != target.sub_ret
		      || cursor.sub_count != target.sub_count);
	}
	if (prev.ptr == NULL)
		return FALSE;
	cursor_pos = prev_pos;
	cursor = prev;
	return TRUE;
}

static BOOL cursor_down(BOOL select) {
	BOOL on_note = cursor_on_note();
	while (cursor_fwd(select) && !cursor_on_note());
	if (!on_note)
		while (cursor_fwd(select) && cursor_on_note());
	return TRUE;
}

static void cursor_to_xy(int x, int y, BOOL select) {
	x -= pos_width;
	int ch = x * 8 / tracker_width;
	if (ch < 0 || ch > 7) return;
	if (select && ch != cursor_chan) return;

	struct channel_state *cs = &state.chan[ch];
	struct parser p;
	int pos = 0;
	parser_init(&p, cs);
	if (p.ptr != NULL) {
		char codes[8];
		int chan_xleft  = (tracker_width * ch       >> 3) + 1;
//		int chan_xright = (tracker_width * (ch + 1) >> 3);

		HDC hdc = GetDC(hwndTracker);
		HFONT oldfont = SelectObject(hdc, hfont);

		int target_pos = state.patpos + y * zoom / font_height;
		pos = state.patpos + cs->next;

		int px = chan_xleft;
		struct parser maybe_new_cursor;
		maybe_new_cursor.ptr = NULL;
		do {
			BYTE chr = *p.ptr;
			SIZE extent;
			if (chr >= 0x80 && chr < 0xE0) {
				int nextpos = pos + p.note_len;
				if (nextpos >= target_pos) {
					if (maybe_new_cursor.ptr != NULL)
						p = maybe_new_cursor;
					break;
				}
				pos = nextpos;
				px = chan_xleft;
				maybe_new_cursor.ptr = NULL;
			} else if (chr == 0) {
				/* nothing */
			} else {
				int length = sprintf(codes, "%02X", chr);
				if (chr < 0x80 && p.ptr[1] < 0x80)
					length += sprintf(codes + 2, "%02X", p.ptr[1]);
				GetTextExtentPoint32(hdc, codes, length, &extent);
				px += extent.cx + 2;
				if (x < px && maybe_new_cursor.ptr == NULL)
					maybe_new_cursor = p;
			}
		} while (parser_advance(&p));
		SelectObject(hdc, oldfont);
		ReleaseDC(hwndTracker, hdc);
	}
	if (select) {
		if (p.sub_count != cursor.sub_count) return;
		if (p.sub_count && p.sub_ret != cursor.sub_ret) return;
		// Avoid excessive repainting
		if (p.ptr == cursor.ptr) return;
	} else {
		set_cur_chan(ch);
	}
	cursor_pos = pos;
	cursor = p;
	printf("cursor_pos = %d\n", cursor_pos);
	cursor_moved(select);
}

BOOL move_cursor(BOOL (*func)(BOOL select), BOOL select) {
	if (cursor.ptr == NULL) return FALSE;
	if (func(select)) {
		cursor_moved(select);
		return TRUE;
	}
	return FALSE;
}

// Inserts code at the cursor
static void track_insert(int size, const BYTE *data) {
	struct track *t = cursor_track;
	int off = cursor.ptr - t->track;
	t->size += size;
	t->track = realloc(t->track, t->size + 1);
	BYTE *ins = t->track + off;
	memmove(ins + size, ins, t->size - (off + size));
	t->track[t->size] = '\0';
	memcpy(ins, data, size);
	pattern_changed();
	restore_cursor(t, off);
}

static BOOL copy_sel() {
	BYTE *start = sel_start, *end = sel_end;
	if (start == end) return FALSE;
	if (!validate_track(start, end - start, cursor.sub_count))
		return FALSE;
	if (!OpenClipboard(hwndMain)) return FALSE;
	EmptyClipboard();
	HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, text_length(start, end));
	track_to_text(GlobalLock(hglb), start, end - start);
	GlobalUnlock(hglb);
	SetClipboardData(CF_TEXT, hglb);
	CloseClipboard();
	return TRUE;
}

static void paste_sel() {
	if (!OpenClipboard(hwndMain)) return;
	HGLOBAL hglb = GetClipboardData(CF_TEXT);
	if (hglb) {
		char *txt = GlobalLock(hglb);
		struct track temp_track = { 0, NULL };
		if (text_to_track(txt, &temp_track, cursor.sub_count)) {
			track_insert(temp_track.size, temp_track.track);
			free(temp_track.track);
		}
		GlobalUnlock(hglb);
	}
	CloseClipboard();
}

static void delete_sel(BOOL cut) {
	struct track *t = cursor_track;
	if (t->track == NULL) return;
	BYTE *start = sel_start, *end = sel_end;
	if (end == t->track + t->size) {
		// Don't let the track end with a note-length code
		if (!validate_track(t->track, start - t->track, cursor.sub_count))
			return;
	}
	if (cut) {
		if (!copy_sel()) return;
	}
	memmove(start, end, t->track + (t->size + 1) - end);
	t->size -= (end - start);
	if (t->size == 0 && !cursor.sub_count) {
		free(t->track);
		t->track = NULL;
		start = NULL;
	}
	pattern_changed();
	restore_cursor(t, start - t->track);
}

static void updateOrInsertDuration(BYTE(*callback)(BYTE, int), int durationOrOffset)
{
	// We cannot insert a duration code before an 0x00 code,
	// so ensure that's not the case before proceeding.
	if (cursor_track->track != NULL
		&& *cursor.ptr != 0)
	{
		BYTE* original_pos = cursor.ptr;
		struct track* t = cursor_track;
		int off = cursor.ptr - t->track;
		if (*cursor.ptr >= 0x01 && *cursor.ptr <= 0x7F)
		{
			BYTE duration = callback(*cursor.ptr, durationOrOffset);
			if (duration != 0)
			{
				*cursor.ptr = duration;
				cur_song.changed = TRUE;
				InvalidateRect(hwndTracker, NULL, FALSE);
			}
		}
		else
		{
			// A duration code isn't selected so
			// find the last duration and last note, if any.
			BYTE* last_duration_pos = NULL;
			BYTE* last_note_pos = NULL;
			cursor_home(FALSE);
			while (cursor.ptr != original_pos)
			{
				if (*cursor.ptr >= 0x01 && *cursor.ptr <= 0x7F)
					last_duration_pos = cursor.ptr;
				else if (*cursor.ptr >= 0x80 && *cursor.ptr <= 0xDF)
					last_note_pos = cursor.ptr;

				// Advance the cursor if possible and continue, otherwise break.
				if (!cursor_fwd(FALSE))
					break;
			}

			// If we found a duration and a note doesn't follow it (meaning
			// the selected note follows the duration code), set the duration.
			if (last_duration_pos != NULL
				&& last_duration_pos > last_note_pos)
			{
				BYTE duration = callback(*last_duration_pos, durationOrOffset);
				if (duration != 0)
				{
					*last_duration_pos = duration;

					// restore the cursor position.
					//cursor.ptr = original_pos;
					pattern_changed();
					restore_cursor(t, off);
				}
			}
			else if (last_note_pos != NULL)
			{
				// Else if we found a note position and it comes after the last
				// duration code (or there is none), insert a duration.
				BYTE duration = callback(last_duration_pos == NULL ? state.chan[cursor_chan].note_len : *last_duration_pos, durationOrOffset);
				if (duration != 0)
				{
					track_insert(1, (BYTE *)&duration);
					cursor_fwd(FALSE);
				}
			}
		}
	}
}

static BYTE setDurationOffsetCallback(BYTE originalDuration, int offset)
{
	BYTE newDuration = min(max(0x00, originalDuration + offset), 0xFF);
	return (newDuration >= 0x01 && newDuration <= 0x7F) ? newDuration : 0;
}

static BYTE setDurationCallback(BYTE originalDuration, int duration)
{
	return (duration >= 0x01 && duration <= 0x7F) ? duration : 0;
}

static void incrementDuration()
{
	updateOrInsertDuration(setDurationOffsetCallback, 1);
}

static void decrementDuration()
{
	updateOrInsertDuration(setDurationOffsetCallback, -1);
}

static void setDuration(BYTE duration)
{
	updateOrInsertDuration(setDurationCallback, duration);
}

void editor_command(int id) {
	switch (id) {
	case ID_CUT: delete_sel(TRUE); break;
	case ID_COPY: copy_sel(); break;
	case ID_PASTE: paste_sel(); break;
	case ID_DELETE: delete_sel(FALSE); break;
	case ID_CLEAR_SONG:
		free_song(&cur_song);
		cur_song.changed = TRUE;
		cur_song.order_length = 1;
		cur_song.order = malloc(sizeof(int));
		cur_song.order[0] = 0;
		cur_song.repeat = 0;
		cur_song.repeat_pos = 0;
		cur_song.patterns = 1;
		cur_song.pattern = calloc(sizeof(struct track), 8);
		cur_song.subs = 0;
		cur_song.sub = NULL;
		initialize_state();
		SendMessage(hwndEditor, WM_SONG_IMPORTED, 0, 0);
		break;
	case ID_SPLIT_PATTERN:
		if (split_pattern(cursor_pos)) {
			pattern_added();
			pattern_changed();
			show_repeat();
		}
		break;
	case ID_JOIN_PATTERNS:
		if (join_patterns()) {
			pattern_deleted();
			pattern_changed();
			show_repeat();
		}
		break;
	case ID_MAKE_SUBROUTINE: {
		if (cursor.sub_count) {
			MessageBox2("Cursor is already in a subroutine",
				"Make subroutine", MB_ICONEXCLAMATION);
			break;
		}
		BYTE *start = sel_start, *end = sel_end;
		int count;
		int sub = create_sub(start, end, &count);
		if (sub < 0) break;
		struct track *t = cursor_track;
		int old_size = t->size;

		if (start >= (t->track + 4)
			&& start[-4] == 0xEF
			&& *(WORD *)&start[-3] == sub
			&& count + start[-1] <= 255)
		{
			count += start[-1];
			start -= 4;
		}
		if (end[0] == 0xEF
			&& *(WORD *)&end[1] == sub
			&& count + end[3] <= 255)
		{
			count += end[3];
			end += 4;
		}
		memmove(start + 4, end, t->track + (old_size + 1) - end);
		t->size = old_size + 4 - (end - start);
		start[0] = 0xEF;
		start[1] = sub & 255;
		start[2] = sub >> 8;
		start[3] = count;
		pattern_changed();
		restore_cursor(&cur_song.sub[sub], 0);
		break;
	}
	// Substitute a subroutine back into the main track
	case ID_UNMAKE_SUBROUTINE: {
		if (!cursor.sub_count) break;
		BYTE *src = cursor_track->track;
		int subsize = cursor_track->size;
		struct track *t = &cur_song.pattern[cur_song.order[state.ordnum]][cursor_chan];
		int off = cursor.sub_ret - t->track;
		int count = cursor.sub_ret[-1];
		int old_size = t->size;
		t->size = (old_size - 4 + (subsize * count));
		t->track = realloc(t->track, t->size + 1);
		memmove(t->track + (off - 4) + (subsize * count), t->track + off,
			(old_size + 1) - off);
		BYTE *dest = t->track + (off - 4);
		for (int i = 0; i < count; i++) {
			memcpy(dest, src, subsize);
			dest += subsize;
		}
		pattern_changed();
		break;
	}
	case ID_TRANSPOSE: {
		int delta = DialogBox(hinstance, MAKEINTRESOURCE(IDD_TRANSPOSE),
			hwndMain, TransposeDlgProc);
		if (delta == 0) break;
		for (BYTE *p = sel_start; p < sel_end; p = next_code(p)) {
			int note = *p - 0x80;
			if (note < 0 || note >= 0x48) continue;
			note += delta;
			note %= 0x48;
			if (note < 0) note += 0x48;
			*p = 0x80 + note;
		}
		cur_song.changed = TRUE;
		show_track_text();
		InvalidateRect(hwndTracker, NULL, FALSE);
		break;
	}
	case ID_ZOOM_IN:
		if (zoom == 1) break;
		zoom = zoom_levels[--zoom_idx];
		InvalidateRect(hwndTracker, NULL, FALSE);
		break;
	case ID_ZOOM_OUT:
		if (zoom >= 96) break;
		zoom = zoom_levels[++zoom_idx];
		InvalidateRect(hwndTracker, NULL, FALSE);
		break;
	case ID_INCREMENT_DURATION:
		incrementDuration();
		break;
	case ID_DECREMENT_DURATION:
		decrementDuration();
		break;
	case ID_SET_DURATION_1:
		setDuration(0x60);
		break;
	case ID_SET_DURATION_2:
		setDuration(0x30);
		break;
	case ID_SET_DURATION_3:
		setDuration(0x18);
		break;
	case ID_SET_DURATION_4:
		setDuration(0x0C);
		break;
	case ID_SET_DURATION_5:
		setDuration(0x06);
		break;
	case ID_SET_DURATION_6:
		setDuration(0x03);
		break;
	}
}

static void addOrInsertNote(int note)
{
	if (note > 0x0 && note < 0x70) {
		note |= 0x80;
		if (cursor.ptr == cursor_track->track + cursor_track->size) {
			track_insert(1, (BYTE *)&note);
		} else if (*cursor.ptr >= 0x80 && *cursor.ptr < 0xE0) {
			*cursor.ptr = note;
			cur_song.changed = TRUE;
			show_track_text();
		} else {
			return;
		}
		move_cursor(cursor_fwd, FALSE);
	}
}

static void tracker_keydown(WPARAM wParam) {
	int control = GetKeyState(VK_CONTROL) & 0x8000;
	int shift = GetKeyState(VK_SHIFT) & 0x8000;
	switch (wParam) {
	case VK_PRIOR: scroll_to(state.patpos - 96); break;
	case VK_NEXT:  scroll_to(state.patpos + 96); break;
	case VK_HOME:  move_cursor(cursor_home, shift); break;
	case VK_END:   move_cursor(cursor_end, shift); break;
	case VK_LEFT:  move_cursor(cursor_back, shift); break;
	case VK_RIGHT: move_cursor(cursor_fwd, shift); break;
	case VK_TAB:
		set_cur_chan((cursor_chan + (shift ? -1 : 1)) & 7);
		parser_init(&cursor, &state.chan[cursor_chan]);
		cursor_pos = state.patpos + state.chan[cursor_chan].next;
		cursor_moved(FALSE);
		break;
	case VK_UP:
		if (control)
			scroll_to(state.patpos - zoom);
		else
			move_cursor(cursor_up, shift);
		break;
	case VK_DOWN:
		if (control)
			scroll_to(state.patpos + zoom);
		else
			move_cursor(cursor_down, shift);
		break;
	case VK_OEM_4: { // left bracket - insert code
		HWND ed = GetDlgItem(hwndEditor, IDC_EDITBOX);
		DWORD start;
		SendMessage(ed, EM_GETSEL, (WPARAM)&start, 0);
		SendMessage(ed, EM_SETSEL, start, start);
		SendMessage(ed, EM_REPLACESEL, 0, (LPARAM)"[ ");
		SendMessage(ed, EM_SETSEL, start+1, start+1);
		SetFocus(ed);
		break;
	}
	case VK_INSERT:
		if (shift)
			paste_sel();
		else
			track_insert(1, (BYTE *)"\xC9");
		break;
	case VK_BACK:
		if (!move_cursor(cursor_back, FALSE))
			break;
		shift = 0;
	case VK_DELETE:
		delete_sel(shift);
		break;
	case VK_ADD:
		incrementDuration();
		break;
	case VK_SUBTRACT:
		decrementDuration();
		break;
	case VK_NUMPAD1:
		setDuration(0x60);
		break;
	case VK_NUMPAD2:
		setDuration(0x30);
		break;
	case VK_NUMPAD3:
		setDuration(0x18);
		break;
	case VK_NUMPAD4:
		setDuration(0x0C);
		break;
	case VK_NUMPAD5:
		setDuration(0x06);
		break;
	case VK_NUMPAD6:
		setDuration(0x03);
		break;
	default:
		if (control) {
			if (wParam == 'C') copy_sel();
			else if (wParam == 'V') paste_sel();
			else if (wParam == 'X') delete_sel(TRUE);
			else if (wParam == VK_OEM_COMMA) decrementDuration();
			else if (wParam == VK_OEM_PERIOD) incrementDuration();
			else if (wParam == '1') setDuration(0x60);
			else if (wParam == '2') setDuration(0x30);
			else if (wParam == '3') setDuration(0x18);
			else if (wParam == '4') setDuration(0x0C);
			else if (wParam == '5') setDuration(0x06);
			else if (wParam == '6') setDuration(0x03);
		}
		else
		{
			int note = note_from_key(wParam, shift);
			addOrInsertNote(note);
		}
		break;
	}
}

LRESULT CALLBACK TrackerWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE: hwndTracker = hWnd; break;
	case WM_DESTROY: hwndTracker = NULL; break;
	case WM_KEYDOWN: tracker_keydown(wParam); break;
	case WM_MOUSEWHEEL:
		scroll_to(state.patpos - (zoom * (short)HIWORD(wParam)) / WHEEL_DELTA);
		break;
	case WM_VSCROLL:
		switch (LOWORD(wParam)) {
			case SB_LINEUP: scroll_to(state.patpos - zoom); break;
			case SB_LINEDOWN: scroll_to(state.patpos + zoom); break;
			case SB_PAGEUP: scroll_to(state.patpos - 96); break;
			case SB_PAGEDOWN: scroll_to(state.patpos + 96); break;
			case SB_THUMBTRACK: scroll_to(HIWORD(wParam)); break;
		}
		break;
	case WM_SIZE:
		tracker_width = LOWORD(lParam) - pos_width;
		tracker_height = HIWORD(lParam);
		break;
	case WM_LBUTTONDOWN:
		SetFocus(hWnd);
		cursor_to_xy(LOWORD(lParam), HIWORD(lParam),
			GetKeyState(VK_SHIFT) & 0x8000);
		break;
	case WM_MOUSEMOVE:
		if (wParam & MK_LBUTTON)
			cursor_to_xy(LOWORD(lParam), HIWORD(lParam), TRUE);
		break;
	case WM_CONTEXTMENU:
		TrackPopupMenu(GetSubMenu(hcontextmenu, 0), 0,
			LOWORD(lParam), HIWORD(lParam), 0, hwndMain, NULL);
		break;
	case WM_SETFOCUS:
		if (editbox_had_focus) {
			editbox_had_focus = FALSE;
			cursor_track = NULL; // force update of editbox text
			cursor_moved(FALSE);
		} else
			// fallthrough
	case WM_KILLFOCUS:
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	case WM_PAINT: tracker_paint(hWnd); break;
	default: return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static HDC hdcState;

static void show_state(int pos, const char *buf) {
	static const WORD xt[] = { 20, 80, 180, 240, 300, 360 };
	RECT rc;
	rc.left = xt[pos >> 4];
	rc.top = (pos & 15) * font_height + 1;
	rc.right = rc.left + 60;
	rc.bottom = rc.top + font_height;
	ExtTextOut(hdcState, rc.left, rc.top, ETO_OPAQUE, &rc, buf, strlen(buf), NULL);
}

static void show_simple_state(int pos, BYTE value) {
	char buf[3];
	sprintf(buf, "%02X", value);
	show_state(pos, buf);
}

static void show_slider_state(int pos, struct slider *s) {
	char buf[9];
	if (s->cycles)
		sprintf(buf, "%02X -> %02X", s->cur >> 8, s->target);
	else
		sprintf(buf, "%02X", s->cur >> 8);
	show_state(pos, buf);
}

static void show_oscillator_state(int pos, BYTE start, BYTE speed, BYTE range) {
	char buf[9];
	if (range)
		sprintf(buf, "%02X %02X %02X", start, speed, range);
	else
		strcpy(buf, "Off");
	show_state(pos, buf);
}

static void CALLBACK MidiInProc(HMIDIIN handle, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	if (wMsg == MIM_DATA)
	{
		unsigned char
			eventType = (dwParam1 & 0xFF),
			param1 = (dwParam1 >> 8) & 0xFF,
			param2 = (dwParam1 >> 16) & 0xFF;

		int note = param1 + (octave - 4)*12;

		if ((eventType & 0x80) && eventType < 0xF0) {	// if not a system exclusive message
			switch (eventType & 0xF0) {
			case 0x90:	// Note On event
				if (param2 > 0	// Make sure volume is not zero. Some devices use this instead of the Note Off event.
					&& note > 0 && note < 0x48)	// Make sure it's within range.
					addOrInsertNote(note);
				break;
			}
		}
	}
}

LRESULT CALLBACK StateWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static const char *const gs[] = {
		"Volume:", "Tempo:", "Transpose:", "CA inst.:"
	};
	static const char *const cs1[] = {
		"Volume:", "Panning:", "Transpose:",
		"Instrument:", "Vibrato:", "Tremolo:"
	};
	static const char *const cs2[] = {
		"Note length:", "Note style:", "Fine tune:",
		"Subroutine:", "Vib. fadein:", "Portamento:"
	};

	switch (uMsg) {
	case WM_CREATE:
		hwndState = hWnd;
		create_controls(hWnd, &state_template, lParam);
		closeMidiInDevice();
		openMidiInDevice(midiDevice, MidiInProc);
		break;
	case WM_ERASEBKGND: {
		DefWindowProc(hWnd, uMsg, wParam, lParam);
		hdcState = (HDC)wParam;
		set_up_hdc(hdcState);
		int i;
		for (i = 0x01; i <= 0x04; i++) show_state(i, gs[i-0x01]);
		for (i = 0x21; i <= 0x26; i++) show_state(i, cs1[i-0x21]);
		for (i = 0x41; i <= 0x46; i++) show_state(i, cs2[i-0x41]);
		reset_hdc(hdcState);
		return 1;
	}
	case WM_PAINT: {
		char buf[11];
		hdcState = BeginPaint(hWnd, &ps);
		set_up_hdc(hdcState);

		show_slider_state(0x11, &state.volume);
		show_slider_state(0x12, &state.tempo);
		show_simple_state(0x13, state.transpose);
		show_simple_state(0x14, state.first_CA_inst);

		struct channel_state *c = &state.chan[cursor_chan];
		show_slider_state(0x31, &c->volume);
		show_slider_state(0x32, &c->panning);
		show_simple_state(0x33, c->transpose);
		show_simple_state(0x34, c->inst);
		show_oscillator_state(0x35, c->vibrato_start, c->vibrato_speed, c->vibrato_max_range);
		show_oscillator_state(0x36, c->tremolo_start, c->tremolo_speed, c->tremolo_range);
		show_simple_state(0x51, c->note_len);
		show_simple_state(0x52, c->note_style);
		show_simple_state(0x53, c->finetune);
		if (c->sub_count) {
			sprintf(buf, "%d x%d", c->sub_start, c->sub_count);
			show_state(0x54, buf);
		} else {
			show_state(0x54, "No");
		}
		show_simple_state(0x55, c->vibrato_fadein);
		if (c->port_length)
			sprintf(buf, "%02X %02X %02X", c->port_start, c->port_length, c->port_range);
		else
			strcpy(buf, "Off");
		show_state(0x56, buf);
		reset_hdc(hdcState);
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_DESTROY:
		closeMidiInDevice();
	break;
	default: return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}
