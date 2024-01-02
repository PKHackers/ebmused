#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "ebmusv2.h"
#include "id.h"
#include "misc.h"

#define IDC_HELPTEXT 1

const char help_text[] = {
"00: End of pattern or subroutine\r\n"
"    (Don't use this in the editor; it automatically\r\n"
"    inserts this where necessary when compiling)\r\n"
"\r\n"
"01-7F: Set note length\r\n"
"\r\n"
"		normal	triplet\r\n"
"	whole	60	40\r\n"
"	half	30	20\r\n"
"	quarter	18	10\r\n"
"	eighth	0C	08\r\n"
"	16th	06	04\r\n"
"	32nd	03	02\r\n"
"\r\n"
"	May be optionally followed by another byte to set note style:\r\n"
"		First nybble: 0-7, release time\r\n"
"		Second nybble: 0-F, volume\r\n"
"\r\n"
"-----------------------\r\n"
"\r\n"
"80-C7: Notes\r\n"
"\r\n"
"	  C  C# D  D# E  F  F# G  G# A  A# B\r\n"
"	1 80 81 82 83 84 85 86 87 88 89 8A 8B\r\n"
"	2 8C 8D 8E 8F 90 91 92 93 94 95 96 97\r\n"
"	3 98 99 9A 9B 9C 9D 9E 9F A0 A1 A2 A3\r\n"
"	4 A4 A5 A6 A7 A8 A9 AA AB AC AD AE AF\r\n"
"	5 B0 B1 B2 B3 B4 B5 B6 B7 B8 B9 BA BB\r\n"
"	6 BC BD BE BF C0 C1 C2 C3 C4 C5 C6 C7\r\n"
"(note C-1 is too low to play without a finetune of at least 26)"
"\r\n"
"C8: Continue previous note\r\n"
"C9: Rest\r\n"
"CA-DF: Set instrument and play note C-4. Usually used for drums.\r\n"
"       The instrument used is equal to the code minus CA plus\r\n"
"       the base instrument number set by [FA].\r\n"
"\r\n"
"-----------------------\r\n"
"\r\n"
"[E0 instrument]\r\n"
"	Set instrument. The parameter can either specify an instrument\r\n"
"	number directly, or it can be a value from CA onward, in which case\r\n"
"	it is relative to the CA base instrument set by [FA].\r\n"
"[E1 panning]\r\n"
"	Set channel panning. 00 = right, 0A = middle, 14 = left\r\n"
"	The top two bits set if the left and/or right stereo channels\r\n"
"	should be inverted.\r\n"
"[E2 time panning]\r\n"
"	Slide channel panning\r\n"
"[E3 start speed range]\r\n"
"	Vibrato on. If range is <= F0, it is in 1/256s of a semitone;\r\n"
"	if range is F1-FF then it is in semitones.\r\n"
"[E4]\r\n"
"	Vibrato off\r\n"
"[E5 volume]\r\n"
"	Set global volume\r\n"
"[E6 time volume]\r\n"
"	Slide global volume\r\n"
"[E7 tempo]\r\n"
"	Set tempo\r\n"
"[E8 time tempo]\r\n"
"	Slide tempo\r\n"
"[E9 transpose]\r\n"
"	Set global transpose\r\n"
"[EA transpose]\r\n"
"	Set channel transpose\r\n"
"[EB start speed range]\r\n"
"	Tremolo on\r\n"
"[EC]\r\n"
"	Tremolo off\r\n"
"[ED volume]\r\n"
"	Set channel volume\r\n"
"[EE time volume]\r\n"
"	Slide channel volume\r\n"
"[EF addr-lo addr-hi count]\r\n"
"	Call a subroutine the given number of times.\r\n"
"	In the editor, use the *s,n syntax instead.\r\n"
"[F0 time]\r\n"
"	Set vibrato fadein time\r\n"
"[F1 start length range]\r\n"
"	Portamento on. Goes from note to note+range\r\n"
"[F2 start length range]\r\n"
"	Portamento on. Goes from note-range to note\r\n"
"[F3]\r\n"
"	Portamento off\r\n"
"[F4 finetune]\r\n"
"	Sets channel finetune (in 1/256 of a semitone)\r\n"
"[F5 channels lvol rvol]\r\n"
"	Echo on (not implemented)\r\n"
"[F6]\r\n"
"	Echo off (not implemented)\r\n"
"[F7 delay feedback filter]\r\n"
"	Set echo settings (not implemented)\r\n"
"[F8 time lvol rvol]\r\n"
"	Slide echo volumes (not implemented)\r\n"
"[F9 start length note]\r\n"
"	Pitch bend\r\n"
"[FA instrument]\r\n"
"	Set the first instrument to be used by CA-DF codes\r\n"
"	In EarthBound, this is always set to the first instrument of the\r\n"
"	second pack, but this is not required.\r\n"
"[FB ?? ??]\r\n"
"	Does nothing\r\n"
"[FC]\r\n"
"	Mute channel (debug code, not implemented)\r\n"
"[FD]\r\n"
"	Fast-forward on (debug code, not implemented)\r\n"
"[FE]\r\n"
"	Fast-forward off (debug code, not implemented)\r\n"
"[FF]\r\n"
"	Invalid"
};

LRESULT CALLBACK CodeListWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_CTLCOLORSTATIC:
			return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
		case WM_CREATE: {
			HWND ed = CreateWindow("Edit", help_text,
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
				0, 0, 0, 0,
				hWnd, (HMENU)IDC_HELPTEXT, hinstance, NULL);
			HFONT font = fixed_font();;
			SendMessage(ed, WM_SETFONT, (WPARAM)font, 0);
			break;
		}
		case WM_SIZE:
			MoveWindow(GetDlgItem(hWnd, IDC_HELPTEXT),
				0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
			break;
		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static WNDPROC HomepageLinkWndProc;
static LRESULT CALLBACK HomepageLinkProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_SETCURSOR: {
			HCURSOR hCursor = LoadCursor(NULL, IDC_HAND);
			if (NULL == hCursor) hCursor = LoadCursor(NULL, IDC_ARROW);
			SetCursor(hCursor);
			return TRUE;
		}
	}

	return CallWindowProc(HomepageLinkWndProc, hWnd, uMsg, wParam, lParam);
}

BOOL CALLBACK AboutDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch(uMsg) {
		case WM_INITDIALOG: {
			HWND hwndLink = GetDlgItem(hWnd, IDC_HOMEPAGELINK);
			HomepageLinkWndProc = (WNDPROC)SetWindowLongPtr(hwndLink, GWLP_WNDPROC, (LONG_PTR)HomepageLinkProc);

			// Set font to underlined
			HFONT hFont = (HFONT)SendMessage(hwndLink, WM_GETFONT, 0, 0);
			LOGFONT lf;
			GetObject(hFont, sizeof(lf), &lf);
			lf.lfUnderline = TRUE;
			HFONT hUnderlinedFont = CreateFontIndirect(&lf);
			SendMessage(hwndLink, WM_SETFONT, (WPARAM)hUnderlinedFont, FALSE);
			SetTextColor(hwndLink, RGB(0, 0, 192));

			break;
		}
		case WM_COMMAND:
			switch(LOWORD(wParam)) {
				case IDC_HOMEPAGELINK:
					if (HIWORD(wParam) == BN_CLICKED) {
						ShellExecute(hWnd, "open", "https://github.com/PKHackers/ebmused/", NULL, NULL, SW_SHOWNORMAL);
					}
					break;
				case IDOK:
					EndDialog(hWnd, IDOK);
					break;
			}
			break;
		case WM_CTLCOLORSTATIC:
			if ((HWND)lParam == GetDlgItem(hWnd, IDC_HOMEPAGELINK))
			{
				SetBkMode((HDC)wParam, TRANSPARENT);
				SetTextColor((HDC)wParam, RGB(0, 0, 192));
				return (BOOL)GetSysColorBrush(COLOR_3DFACE);
			}
			return FALSE;
			break;
		default:
			return FALSE;
	}

	return TRUE;
}
