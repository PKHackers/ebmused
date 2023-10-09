#define _WIN32_WINNT 0x600
#include <stdio.h>
#include <stdlib.h>
#include "ebmusv2.h"
#include "misc.h"

void enable_menu_items(const BYTE *list, int flags) {
	while (*list) EnableMenuItem(hmenu, *list++, flags);
}

void update_menu_item(UINT item, LPTSTR label) {
	MENUITEMINFO menuiteminfo = { sizeof(MENUITEMINFO) };
	GetMenuItemInfo(hmenu, item, FALSE, &menuiteminfo);
	menuiteminfo.fMask = MIIM_STRING;
	menuiteminfo.dwTypeData = label;
	SetMenuItemInfo(hmenu, item, FALSE, &menuiteminfo);
}

HFONT oldfont;
COLORREF oldtxt, oldbk;

static int dpi_x;
static int dpi_y;

static HFONT hFixedFont;
static HFONT hDefaultGUIFont;
static HFONT hTabsFont;
static HFONT hOrderFont;

void set_up_hdc(HDC hdc) {
	oldfont = SelectObject(hdc, default_font());
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

void set_up_fonts(void) {
	LOGFONT lf = {0};
	LOGFONT lf2 = {0};
	NONCLIENTMETRICS ncm = {0};
	// This size is different in 2000 and XP. That could be causing different values to be returned
	// between the Windows SDK and MinGW builds for the new iPaddedBorderWidth field?
	// So don't use that field for now.
	// https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-nonclientmetricsa#remarks
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	BOOL ncmInitialized = SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

	HFONT h = GetStockObject(ANSI_FIXED_FONT);
	int err = GetObject(h, sizeof(LOGFONT), &lf);
	if (err != sizeof(LOGFONT)) {
		printf("ANSI_FIXED_FONT: only %d bytes written to lf!\n", err);
		hFixedFont = h;
	} else {
		strcpy(lf.lfFaceName, "Consolas");
		if (!ncmInitialized) {
			lf.lfHeight = scale_y(lf.lfHeight + 3);
			lf.lfWidth = 0;
		} else {
			// Make the font wide enough to nearly fill the instrument view
			// (Courier New/Consolas are roughly twice as tall as they are wide, and the header has
			// 20 characters)
			lf.lfWidth = (scale_x(180) - ncm.iScrollWidth) / 20;
			lf.lfHeight = lf.lfWidth * 2;
		}
		hFixedFont = CreateFontIndirect(&lf);
	}

	hDefaultGUIFont = GetStockObject(DEFAULT_GUI_FONT);

	err = GetObject(hDefaultGUIFont, sizeof(LOGFONT), &lf);
	if (err != sizeof(LOGFONT)) {
		printf("DEFAULT_GUI_FONT: only %d bytes written to lf!\n", err);
		hOrderFont = GetStockObject(SYSTEM_FONT);
	} else {
		lf.lfWeight = FW_BOLD;
		lf.lfHeight = scale_y(16);
		hOrderFont = CreateFontIndirect(&lf);
	}

	if (!ncmInitialized) {
		err = GetObject(GetStockObject(SYSTEM_FONT), sizeof(LOGFONT), &lf2);
		if (err != sizeof(LOGFONT)) {
			printf("SYSTEM_FONT: only %d bytes written to lf2!\n", err);
			hTabsFont = hDefaultGUIFont;
		}
		lf.lfHeight = scale_y(lf2.lfHeight - 1);
		hTabsFont = CreateFontIndirect(&lf);
	} else {
		lf = ncm.lfMessageFont;
		lf.lfHeight = scale_y(16);
		hTabsFont = CreateFontIndirect(&lf);
	}
}

void destroy_fonts(void) {
	DeleteObject(hFixedFont);
	DeleteObject(hDefaultGUIFont);
	DeleteObject(hTabsFont);
	DeleteObject(hOrderFont);
}

HFONT fixed_font(void) {
	return hFixedFont;
}

HFONT default_font(void) {
	return hDefaultGUIFont;
}

HFONT tabs_font(void) {
	return hTabsFont;
}

HFONT order_font(void) {
	return hOrderFont;
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
