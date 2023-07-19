#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "ebmusv2.h"
#include "misc.h"

void create_controls(HWND hWnd, struct window_template *t, LPARAM cs) {
	int width = ((CREATESTRUCT *)cs)->cx;
	int winheight = ((CREATESTRUCT *)cs)->cy;

	assert(width <= 0xFFFF);
	assert(winheight <= 0xFFFF);
	t->winsize = MAKELONG(width, winheight);

	// Start with the upper half of the window
	int top = 0;
	int height = t->divy;

	const struct control_desc *c = t->controls;

	for (int num = t->num; num; num--, c++) {
		int x = scale_x(c->x);
		int y = scale_y(c->y);
		int xsize = scale_x(c->xsize);
		int ysize = scale_y(c->ysize);
		if (num == t->lower) {
			// Switch to the lower half of the window
			top = height;
			height = winheight - top;
		}
		if (x < 0) x += width;
		if (y < 0) y += height;
		if (xsize <= 0) xsize += width;
		if (ysize <= 0) ysize += height;

		HWND w = CreateWindow(c->class, c->title,
			WS_CHILD | WS_VISIBLE | c->style,
			x, top + y, xsize, ysize,
			hWnd, (HMENU)c->id, hinstance, NULL);

		// Override the font, if it's not a "Sys" class that handles that normally
		if (c->class[1] != 'y')
			SendMessage(w, WM_SETFONT, (WPARAM)default_font(), 0);
	}
}

void move_controls(HWND hWnd, struct window_template *t, LPARAM lParam) {
	int width = LOWORD(lParam);
	int top, height;
	int i = 0;
	int dir = 1;
	int end = t->num;
	// move controls in reverse order when making the window larger,
	// so that they don't get drawn on top of each other
	if (lParam > t->winsize) {
		i = t->num - 1;
		dir = -1;
		end = -1;
	}
	for (; i != end; i += dir) {
		const struct control_desc *c = &t->controls[i];
		int x = scale_x(c->x);
		int y = scale_y(c->y);
		int xsize = scale_x(c->xsize);
		int ysize = scale_y(c->ysize);
		if (i < (t->num - t->lower)) {
			top = 0;
			height = t->divy;
		} else {
			top = t->divy;
			height = HIWORD(lParam) - t->divy;
		}

		// Don't resize controls in the upper half of the window, unless they are positioned/sized
		// relative to the bottom or right side of the window
		if (top == 0 && x >= 0 && y >= 0 && xsize > 0 && ysize > 0)
			continue;
		if (x < 0) x += width;
		if (y < 0) y += height;
		if (xsize <= 0) xsize += width;
		if (ysize <= 0) ysize += height;
		MoveWindow(GetDlgItem(hWnd, c->id), x, top + y, xsize, ysize, TRUE);
	}
	t->winsize = lParam;
}
