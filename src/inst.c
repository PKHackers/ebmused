#include <stdio.h>
#define _WIN32_WINNT 0x0500 // for VK_OEM_PERIOD ?????
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "ebmusv2.h"

#define IDC_SAMPLIST_CAPTION 1
#define IDC_SAMPLIST 2
#define IDC_INSTLIST_CAPTION 3
#define IDC_INSTLIST 4
#define IDC_MIDIINCOMBO 5

#define inst_list_template_num 10
#define inst_list_template_lower 10

static HWND samplist, instlist, insttest;
static int prev_chmask;
static int selectedInstrument = 0;

static const struct control_desc inst_list_controls[] = {
	{ "Static",  10, 10,100, 20, "Samples:", 0, 0 },
	{ "Static",  13, 30,180, 20, "    Strt Loop Size", 1, 0 },
	{ "ListBox", 10, 50,180,-60, NULL, 2, WS_BORDER | WS_VSCROLL },
	{ "Static", 200, 10,100, 20, "Instruments:", 0, 0 },
	{ "Static", 203, 30,270, 20, "    S# A1 A2 G  Freq", 3, 0 },
	{ "ListBox",200, 50,270,-60, NULL, 4, WS_BORDER | WS_VSCROLL },
	{ "Static", 480, 10,100, 20, "Instrument test:", 0, 0},
	{ "ebmused_insttest",480, 30,140,260, NULL, 3, 0 },
	{ "Static", 480, 300,100, 20, "MIDI In Device:", 0, 0},
	{ "ComboBox", 480, 320, 140, 200, NULL, IDC_MIDIINCOMBO, CBS_DROPDOWNLIST | WS_VSCROLL },
};
static struct window_template inst_list_template = {
	inst_list_template_num, inst_list_template_lower, 0, 0, inst_list_controls
};

static unsigned char valid_insts[64];
static int cnote[8];

int note_from_key(int key, BOOL shift) {
	if (key == VK_OEM_PERIOD) return 0x48; // continue
	if (key == VK_SPACE) return 0x49; // rest
	if (shift) {
		static const char drums[] = "1234567890\xBD\xBBQWERTYUIOP";
		char *p = strchr(drums, key);
		if (p) return 0x4A + (p-drums);
	} else {
		static const char low[]  = "ZSXDCVGBHNJM\xBCL";
		static const char high[] = "Q2W3ER5T6Y7UI9O0P";
		char *p = strchr(low, key);
		if (p) return octave*12 + (p-low);
		p = strchr(high, key);
		if (p) return (octave+1)*12 + (p-high);
	}
	return -1;
}

static void draw_square(int note, HBRUSH brush) {
	HDC hdc = GetDC(insttest);
	int x = (note / 12 + 1) * 20;
	int y = (note % 12 + 1) * 20;
	RECT rc = { x, y, x + 19, y + 19 };
	FillRect(hdc, &rc, brush);
	ReleaseDC(insttest, hdc);
}

static void note_off(int note) {
	for (int ch = 0; ch < 8; ch++)
		if (state.chan[ch].samp_pos >= 0 && cnote[ch] == note)
			state.chan[ch].note_release = 0;
	draw_square(note, GetStockObject(WHITE_BRUSH));
}

static void note_on(int note, int velocity) {
	int sel = SendMessage(instlist, LB_GETCURSEL, 0, 0);
	if (sel < 0) return;
	int inst = valid_insts[sel];

	int ch;
	for (ch = 0; ch < 8; ch++)
		if (state.chan[ch].samp_pos < 0) break;
	if (ch == 8) return;
	cnote[ch] = note;
	struct channel_state *c = &state.chan[ch];
	set_inst(&state, c, inst);
	c->samp_pos = 0;
	c->samp = &samp[spc[inst_base + 6*c->inst]];

	c->note_release = 1;
	c->env_height = 1;
	calc_freq(c, note << 8);
	c->left_vol = c->right_vol = min(max(velocity, 0), 127);
	draw_square(note, (HBRUSH)(COLOR_HIGHLIGHT + 1));
}

static void CALLBACK MidiInProc(HMIDIIN handle, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	if (wMsg == MIM_DATA)
	{
		unsigned char
			eventType = (dwParam1 & 0xFF),
			param1 = (dwParam1 >> 8) & 0xFF,
			param2 = (dwParam1 >> 16) & 0xFF;

		if ((eventType & 0x80) && eventType < 0xF0) {	// If not a system exclusive MIDI message
			switch (eventType & 0xF0) {
			case 0xC0:	// Instrument change event
				SendMessage(instlist, LB_SETCURSEL, param1, 0);
				break;
			case 0x90:	// Note On event
				if (param2 > 0)
					note_on(param1 + (octave - 4)*12, param2/2);
				else
					note_off(param1 + (octave - 4)*12);
				break;
			case 0x80:	// Note Off event
					note_off(param1 + (octave - 4)*12);
				break;
			}
		}
	}
}

static WNDPROC ListBoxWndProc;
// Custom window procedure for the instrument ListBox
static LRESULT CALLBACK InstListWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_KEYDOWN && !(lParam & (1 << 30))) {
		int note = note_from_key(wParam, FALSE);
		if (note >= 0 && note < 0x48)
			note_on(note, 24);
	}
	if (uMsg == WM_KEYUP) {
		int note = note_from_key(wParam, FALSE);
		if (note >= 0 && note < 0x48)
			note_off(note);
	}
	// so pressing 0 or 2 doesn't move the selection around
	if (uMsg == WM_CHAR) return 0;
	return CallWindowProc(ListBoxWndProc, hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK InstTestWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE: insttest = hWnd; break;
	case WM_ERASEBKGND: {
		DefWindowProc(hWnd, uMsg, wParam, lParam);
		HDC hdc = (HDC)wParam;
		set_up_hdc(hdc);
		for (char o = '1'; o <= '6'; o++)
			TextOut(hdc, 20 * (o - '0'), 0, &o, 1);
		for (int i = 0; i < 12; i++)
			TextOut(hdc, 0, 20 * (i + 1), "C C#D D#E F F#G G#A A#B " + 2*i, 2);
		Rectangle(hdc, 19, 19, 140, 260);
		reset_hdc(hdc);
		return 1;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP: {
		int octave = LOWORD(lParam) / 20 - 1;
		if (octave < 0 || octave > 5) break;
		int note = HIWORD(lParam) / 20 - 1;
		if (note < 0 || note > 11) break;
		note += 12 * octave;
		if (uMsg == WM_LBUTTONDOWN) note_on(note, 24);
		else note_off(note);
		break;
	}
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK InstrumentsWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE: {
		prev_chmask = chmask;
		WPARAM fixed = (WPARAM)GetStockObject(ANSI_FIXED_FONT);
		char buf[40];

		// HACK: For some reason when the compiler has optimization turned on, it doesn't initialize the values of inst_list_template correctly. So we'll reset them here. . .
		// NOTE: This may be due to a sprintf overflowing, as was the case with bgm_list_template when compiling in Visual Studio 2015
		inst_list_template.num = inst_list_template_num;
		inst_list_template.lower = inst_list_template_lower;

		create_controls(hWnd, &inst_list_template, lParam);

		SendDlgItemMessage(hWnd, IDC_SAMPLIST_CAPTION, WM_SETFONT, fixed, 0);
		samplist = GetDlgItem(hWnd, IDC_SAMPLIST);
		SendMessage(samplist, WM_SETFONT, fixed, 0);
		SendDlgItemMessage(hWnd, IDC_INSTLIST_CAPTION, WM_SETFONT, fixed, 0);
		instlist = GetDlgItem(hWnd, IDC_INSTLIST);
		SendMessage(instlist, WM_SETFONT, fixed, 0);

		// Insert a custom window procedure on the instrument list, so we
		// can see WM_KEYDOWN and WM_KEYUP messages for instrument testing.
		// (LBS_WANTKEYBOARDINPUT doesn't notify on WM_KEYUP)
		ListBoxWndProc = (WNDPROC)SetWindowLongPtr(instlist, GWLP_WNDPROC,
			(LONG_PTR)InstListWndProc);

		for (int i = 0; i < 128; i++) {
			if (samp[i].data == NULL) continue;
			WORD *ptr = (WORD *)&spc[0x6C00 + 4*i];
			sprintf(buf, "%02X: %04X %04X %4d", i,
				ptr[0], ptr[1], samp[i].length >> 4);
			SendMessage(samplist, LB_ADDSTRING, 0, (LPARAM)buf);
		}

		unsigned char *p = valid_insts;
		for (int i = 0; i < 64; i++) {
			unsigned char *inst = &spc[inst_base + i*6];
			if (inst[4] == 0 && inst[5] == 0) continue;

			sprintf(buf, "%02X: %02X %02X %02X %02X %2X%02X",
				i, inst[0], inst[1], inst[2], inst[3], inst[4], inst[5]);
			SendMessage(instlist, LB_ADDSTRING, 0, (LPARAM)buf);
			*p++ = i;
		}
		if (sound_init())
			song_playing = TRUE;
		timer_speed = 0;
		memset(&state.chan, 0, sizeof state.chan);
		for (int ch = 0; ch < 8; ch++) {
			state.chan[ch].samp_pos = -1;
		}

		// Restore the previous instrument selection
		if (SendMessage(instlist, LB_GETCOUNT, 0, 0) < selectedInstrument)
			selectedInstrument = 0;
		SendMessage(instlist, LB_SETCURSEL, selectedInstrument, 0);
		SetFocus(instlist);

		// Populate the MIDI In Devices combo box
		HWND cb = GetDlgItem(hWnd, IDC_MIDIINCOMBO);
		SendMessage(cb, CB_RESETCONTENT, 0, 0);
		SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)"None");

		MIDIINCAPS inCaps;
		unsigned int numDevices = midiInGetNumDevs();
		for (unsigned int i=0; i<numDevices; i++) {
			if (midiInGetDevCaps(i, &inCaps, sizeof(MIDIINCAPS)) == MMSYSERR_NOERROR)
				SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)inCaps.szPname);
		}

		SendMessage(cb, CB_SETCURSEL, midiDevice + 1, 0);
		closeMidiInDevice();
		openMidiInDevice(midiDevice, MidiInProc);

		break;
	}
	case WM_COMMAND: {
		WORD id = LOWORD(wParam), action = HIWORD(wParam);
		switch (id) {
		case IDC_MIDIINCOMBO:
			if (action == CBN_SELCHANGE) {
				midiDevice = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0) - 1;
				closeMidiInDevice();
				openMidiInDevice(midiDevice, MidiInProc);
			} else if (action == CBN_CLOSEUP) {
				SetFocus(instlist);
			}
			break;
		}
		break;
	}
	case WM_ROM_CLOSED:
		SendMessage(samplist, LB_RESETCONTENT, 0, 0);
		SendMessage(instlist, LB_RESETCONTENT, 0, 0);
		break;
	case WM_SIZE:
		move_controls(hWnd, &inst_list_template, lParam);
		break;
	case WM_DESTROY:
		song_playing = FALSE;
		state = pattop_state;
		timer_speed = 500;
		chmask = prev_chmask;
		closeMidiInDevice();

		// Store the current selected instrument.
		selectedInstrument = SendMessage(instlist, LB_GETCURSEL, 0, 0);
		break;
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}
