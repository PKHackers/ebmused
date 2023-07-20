#ifndef MISC_H
#define MISC_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

void enable_menu_items(const BYTE *list, int flags);
void update_menu_item(UINT item, LPTSTR label);
#ifdef CreateWindow
void set_up_hdc(HDC hdc);
void reset_hdc(HDC hdc);
#endif
#ifdef EOF
int fgetw(FILE *f);
#endif
BOOL SetDlgItemHex(HWND hwndDlg, int idControl, unsigned int uValue, int size);
int GetDlgItemHex(HWND hwndDlg, int idControl);
int MessageBox2(char *error, char *title, int flags);
char *open_dialog(BOOL (WINAPI *func)(LPOPENFILENAME ofn), char *filter, char *extension, DWORD flags);
void setup_dpi_scale_values(void);
int scale_x(int n);
int scale_y(int n);
void set_up_fonts(void);
void destroy_fonts(void);
HFONT fixed_font(void);
HFONT default_font(void);
HFONT tabs_font(void);
HFONT order_font(void);
// Don't declare parameters, to avoid pointer conversion warnings
// (you can't implicitly convert between foo** and void** because of
//  word-addressed architectures. This is x86 so it's ok)
void *array_insert(/*void **array, int *size, int elemsize, int index*/);
void array_delete(void *array, int *size, int elemsize, int index);

#endif // MISC_H
