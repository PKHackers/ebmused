#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ebmusv2.h"

void enable_menu_items(const BYTE *list, int flags) {
	while (*list) EnableMenuItem(hmenu, *list++, flags);
}

HFONT oldfont;
COLORREF oldtxt, oldbk;

void set_up_hdc(HDC hdc) {
	oldfont = SelectObject(hdc, hfont);
	oldtxt = SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
	oldbk = SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
}

void reset_hdc(HDC hdc) {
	SelectObject(hdc, oldfont);
	SetTextColor(hdc, oldtxt);
	SetBkColor(hdc, oldbk);
}

int fgetw(FILE *f) {
	int lo, hi;
	lo = fgetc(f); if (lo < 0) return -1;
	hi = fgetc(f); if (hi < 0) return -1;
	return lo | hi<<8;
}

// Like Set/GetDlgItemInt but for hex.
// (Why isn't this in the Win32 API? Darned decimal fascists)
BOOL SetDlgItemHex(HWND hwndDlg, int idControl, UINT uValue, int size) {
	char buf[9];
	sprintf(buf, "%0*X", size, uValue);
	return SetDlgItemText(hwndDlg, idControl, buf);
}

int GetDlgItemHex(HWND hwndDlg, int idControl) {
	char buf[9];
	int n = -1;
	if (GetDlgItemText(hwndDlg, idControl, buf, 9)) {
		char *endp;
		n = strtol(buf, &endp, 16);
		if (*endp != '\0') n = -1;
	}
	return n;
}

// MessageBox takes the focus away and doesn't restore it - annoying,
// since the user will probably want to correct the error.
int MessageBox2(char *error, char *title, int flags) {
	HWND focus = GetFocus();
	int ret = MessageBox(hwndMain, error, title, flags);
	SetFocus(focus);
	return ret;
}

static int dpi_x;
static int dpi_y;

void setup_dpi_scale_values(void) {
	// Use the old DPI system, which works as far back as Windows 2000 Professional
	HDC screen;
	if (0) {
		// Per-monitor DPI awareness checking would go here
	} else if ((screen = GetDC(0)) != NULL) {
		// https://docs.microsoft.com/en-us/previous-versions/ms969894(v=msdn.10)
		dpi_x = GetDeviceCaps(screen, LOGPIXELSX);
		dpi_y = GetDeviceCaps(screen, LOGPIXELSY);

		ReleaseDC(0, screen);
	} else {
		printf("GetDC failed; filling in default values for DPI.\n");
		dpi_x = 96;
		dpi_y = 96;
	}

	printf("DPI values initialized: %d %d\n", dpi_x, dpi_y);
}

int scale_x(int n) {
	return MulDiv(n, dpi_x, 96);
}

int scale_y(int n) {
	return MulDiv(n, dpi_y, 96);
}

void *array_insert(void **array, int *size, int elemsize, int index) {
	int new_size = elemsize * ++*size;
	char *a = realloc(*array, new_size);
	index *= elemsize;
	*array = a;
	a += index;
	memmove(a + elemsize, a, new_size - (index + elemsize));
	return a;
}

/*void array_delete(void *array, int *size, int elemsize, int index) {
	int new_size = elemsize * --*size;
	char *a = array;
	index *= elemsize;
	a += index;
	memmove(a, a + elemsize, new_size - index);
}*/
