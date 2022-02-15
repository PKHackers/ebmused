#ifdef _MSC_VER
	#define _ARGC __argc
	#define _ARGV __argv
#else
	#define _ARGC _argc
	#define _ARGV _argv
#endif

#include "id.h"
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include "ebmusv2.h"

struct song cur_song;
BYTE packs_loaded[3] = { 0xFF, 0xFF, 0xFF };
int current_block = -1;
struct song_state pattop_state, state;
int octave = 2;
int midiDevice = -1;
HINSTANCE hinstance;
HWND hwndMain;
HMENU hmenu, hcontextmenu;
HFONT hfont;
HWND tab_hwnd[NUM_TABS];

static int current_tab;
static const char *const tab_class[NUM_TABS] = {
	"ebmused_bgmlist",
	"ebmused_inst",
	"ebmused_editor",
	"ebmused_packs"
};
static char *const tab_name[NUM_TABS] = {
	"Song Table",
	"Instruments",
	"Sequence Editor",
	"Data Packs"
};
LRESULT CALLBACK BGMListWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InstrumentsWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditorWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PackListWndProc(HWND, UINT, WPARAM, LPARAM);
static const WNDPROC tab_wndproc[NUM_TABS] = {
	BGMListWndProc,
	InstrumentsWndProc,
	EditorWndProc,
	PackListWndProc,
};


static char filename[MAX_PATH];
static OPENFILENAME ofn;
static char *open_dialog(BOOL (WINAPI *func)(LPOPENFILENAME),
	char *filter, char *extension, DWORD flags)
{
	*filename = '\0';
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = hwndMain;
	ofn.lpstrFilter = filter;
	ofn.lpstrDefExt = extension;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = flags | OFN_NOCHANGEDIR;
	return func(&ofn) ? filename : NULL;
}

BOOL get_original_rom() {
	char *file = open_dialog(GetOpenFileName,
		"SNES ROM files (*.smc, *.sfc)\0*.smc;*.sfc\0All Files\0*.*\0",
		NULL,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
	BOOL ret = file && open_orig_rom(file);
	metadata_changed |= ret;
	return ret;
}

static void tab_selected(int new) {
	if (new < 0 || new >= NUM_TABS) return;
	current_tab = new;

	for (int i = 0; i < NUM_TABS; i++) {
		if (tab_hwnd[i]) {
			DestroyWindow(tab_hwnd[i]);
			tab_hwnd[i] = NULL;
		}
	}

	RECT rc;
	GetClientRect(hwndMain, &rc);
	tab_hwnd[new] = CreateWindow(tab_class[new], NULL,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 25, rc.right, rc.bottom - 25,
		hwndMain, NULL, hinstance, NULL);

	SendMessage(tab_hwnd[new], rom ? WM_ROM_OPENED : WM_ROM_CLOSED, 0, 0);
	SendMessage(tab_hwnd[new], cur_song.order_length ? WM_SONG_LOADED : WM_SONG_NOT_LOADED, 0, 0);
}

static void import() {
	if (packs_loaded[2] >= NUM_PACKS) {
		MessageBox2("No song pack selected", "Import", MB_ICONEXCLAMATION);
		return;
	}

	char *file = open_dialog(GetOpenFileName,
		"EarthBound Music files (*.ebm)\0*.ebm\0All Files\0*.*\0", NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
	if (!file) return;

	FILE *f = fopen(file, "rb");
	if (!f) {
		MessageBox2(strerror(errno), "Import", MB_ICONEXCLAMATION);
		return;
	}

	struct block b;
	if (!fread(&b, 4, 1, f) || b.spc_address + b.size > 0x10000 || _filelength(_fileno(f)) != 4 + b.size) {
		MessageBox2("File is not an EBmused export", "Import", MB_ICONEXCLAMATION);
		goto err1;
	}
	b.data = malloc(b.size);
	fread(b.data, b.size, 1, f);
	new_block(&b);
	SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0);
err1:
	fclose(f);
}

/*static void import_spc() {
	char *file = open_dialog(GetOpenFileName,
		"SPC Savestates (*.spc)\0*.spc\0All Files\0*.*\0",
		NULL,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
	if (!file) return;

	FILE *f = fopen(file, "rb");
	if (!f) {
		MessageBox2(strerror(errno), "Import", MB_ICONEXCLAMATION);
		return;
	}

	fseek(f, 0x100, SEEK_SET);
	fread(spc, 65536, 1, f);
	fseek(f, 0x1015D, SEEK_SET);
	int samp_ptrs = fgetc(f) << 8;
	decode_samples((WORD *)&spc[samp_ptrs]);
	for (int i = 0; i < 0xfff0; i++) {
		if (memcmp(&spc[i], "\x8D\x06\xCF\xDA\x14\x60\x98", 7) == 0) {
			inst_base = spc[i+7] | spc[i+10] << 8;
			printf("Instruments found: %X\n", inst_base);
			break;
		}
	}
	int song_addr = spc[0x40] | spc[0x41] << 8;
	free_song(&cur_song);
	decompile_song(&cur_song, song_addr - 2, 0xffff);
	initialize_state();
	SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0);

	fclose(f);
}*/

static void export() {
	struct block *b = save_cur_song_to_pack();
	if (!b) {
		MessageBox2("No song loaded", "Export", MB_ICONEXCLAMATION);
		return;
	}

	char *file = open_dialog(GetSaveFileName, "EarthBound Music files (*.ebm)\0*.ebm\0", "ebm", OFN_OVERWRITEPROMPT);
	if (!file) return;

	FILE *f = fopen(file, "wb");
	if (!f) {
		MessageBox2(strerror(errno), "Export", MB_ICONEXCLAMATION);
		return;
	}
	fwrite(b, 4, 1, f);
	fwrite(b->data, b->size, 1, f);
	fclose(f);
}

static void write_spc(FILE *f);
static void export_spc() {
	if (cur_song.order_length > 0) {
		char *file = open_dialog(GetSaveFileName, "SPC files (*.spc)\0*.spc\0", "spc", OFN_OVERWRITEPROMPT);
		if (file) {
			FILE *f = fopen(file, "wb");
			if (f) {
				write_spc(f);
				fclose(f);
			} else {
				MessageBox2(strerror(errno), "Export SPC", MB_ICONEXCLAMATION);
			}
		}
	} else {
		MessageBox2("No song loaded.", "Export SPC", MB_ICONEXCLAMATION);
	}
}

// Loads pack by index and copies its contents to "spc" at the appropriate locations.
static void pack_to_spc(BYTE pack, BYTE* spc) {
	if (pack < NUM_PACKS) {
		const WORD header_size = 0x100;

		struct pack *p = load_pack(pack);
		for (int block = 0; block < p->block_count; block++) {
			struct block *b = &p->blocks[block];

			if (b->size <= 0x10000 - b->spc_address) {
				memcpy(spc + header_size + b->spc_address, b->data, b->size);
			} else {
				printf("SPC pack %X block %d too large.\n", pack, block);
			}
		}
	}
}

static void write_spc(FILE *f) {
	// Load blank SPC file.
	HRSRC res = FindResource(hinstance, MAKEINTRESOURCE(IDRC_SPC), RT_RCDATA);
	HGLOBAL res_handle = res ? LoadResource(NULL, res) : NULL;

	if (res_handle) {
		BYTE* res_data = (BYTE*)LockResource(res_handle);
		DWORD spc_size = SizeofResource(NULL, res);

		// Copy blank SPC to byte array
		BYTE *new_spc = memcpy(malloc(spc_size), res_data, spc_size);

		// Copy packs/blocks to byte array
		for (int pack = 0; pack < 3; pack++) {
			pack_to_spc(packs_loaded[pack], new_spc);
		}

		// Set pattern repeat location. (Not absolutely necessary...)
		const WORD repeat_address = cur_song.address + 0x2*cur_song.repeat_pos;
		memcpy(new_spc + 0x140, &repeat_address, 2);

		// Set BGM to load
		const BYTE bgm = selected_bgm + 1;
		memcpy(new_spc + 0x1F4, &bgm, 1);

		// Update song address of current BGM within the music program
		memcpy(new_spc + 0x2F48 + 0x2*bgm, &cur_song.address, 2);

		// Save byte array to file
		fwrite(new_spc, spc_size, 1, f);

		free(new_spc);
	} else {
		MessageBox2("Blank SPC could not be loaded.", "Export SPC", MB_ICONEXCLAMATION);
	}
}

BOOL save_all_packs() {
	char buf[60];
	save_cur_song_to_pack();
	int packs = 0;
	BOOL success = TRUE;
	for (int i = 0; i < NUM_PACKS; i++) {
		if (inmem_packs[i].status & IPACK_CHANGED) {
			BOOL saved = save_pack(i);
			success &= saved;
			packs += saved;
		}
	}
	if (packs) {
		SendMessage(tab_hwnd[current_tab], WM_PACKS_SAVED, 0, 0);
		sprintf(buf, "%d pack(s) saved", packs);
		MessageBox2(buf, "Save", MB_OK);
	}
	save_metadata();
	return success;
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case 0x3BB: case 0x3BC: case 0x3BD: // MM_WOM_OPEN, CLOSE, DONE
		winmm_message(uMsg);
		break;
	case WM_CREATE: {
		HWND tabs = CreateWindow(WC_TABCONTROL, NULL,
			WS_CHILD | WS_VISIBLE | TCS_BUTTONS, 0, 0, 600, 25,
			hWnd, NULL, hinstance, NULL);
		TC_ITEM item;
		item.mask = TCIF_TEXT;
		for (int i = 0; i < NUM_TABS; i++) {
			item.pszText = tab_name[i];
			(void)TabCtrl_InsertItem(tabs, i, &item);
		}
		break;
	}
	case WM_SIZE:
		MoveWindow(tab_hwnd[current_tab], 0, 25, LOWORD(lParam), HIWORD(lParam) - 25, TRUE);
		break;
	case WM_COMMAND: {
		WORD id = LOWORD(wParam);
		switch (id) {
		case ID_OPEN: {
			char *file = open_dialog(GetOpenFileName,
				"SNES ROM files (*.smc, *.sfc)\0*.smc;*.sfc\0All Files\0*.*\0", NULL, OFN_FILEMUSTEXIST);
			if (file && open_rom(file, ofn.Flags & OFN_READONLY)) {
				SendMessage(tab_hwnd[current_tab], WM_ROM_CLOSED, 0, 0);
				SendMessage(tab_hwnd[current_tab], WM_ROM_OPENED, 0, 0);
			}
			break;
		}
		case ID_SAVE_ALL:
			save_all_packs();
			break;
		case ID_CLOSE:
			if (!close_rom()) break;
			SendMessage(tab_hwnd[current_tab], WM_ROM_CLOSED, 0, 0);
			SetWindowText(hWnd, "EarthBound Music Editor");
			break;
		case ID_IMPORT: import(); break;
//		case ID_IMPORT_SPC: import_spc(); break;
		case ID_EXPORT: export(); break;
		case ID_EXPORT_SPC: export_spc(); break;
		case ID_EXIT: DestroyWindow(hWnd); break;
		case ID_OPTIONS: {
			extern BOOL CALLBACK OptionsDlgProc(HWND,UINT,WPARAM,LPARAM);
			DialogBox(hinstance, MAKEINTRESOURCE(IDD_OPTIONS), hWnd, OptionsDlgProc);
			break;
		}
		case ID_CUT:
		case ID_COPY:
		case ID_PASTE:
		case ID_DELETE:
		case ID_SPLIT_PATTERN:
		case ID_JOIN_PATTERNS:
		case ID_MAKE_SUBROUTINE:
		case ID_UNMAKE_SUBROUTINE:
		case ID_TRANSPOSE:
		case ID_CLEAR_SONG:
		case ID_ZOOM_OUT:
		case ID_ZOOM_IN:
		case ID_INCREMENT_DURATION:
		case ID_DECREMENT_DURATION:
		case ID_SET_DURATION_1:
		case ID_SET_DURATION_2:
		case ID_SET_DURATION_3:
		case ID_SET_DURATION_4:
		case ID_SET_DURATION_5:
		case ID_SET_DURATION_6:
			editor_command(id);
			break;
		case ID_PLAY:
			if (cur_song.order_length == 0)
				MessageBox2("No song loaded", "Play", MB_ICONEXCLAMATION);
			else if (samp[0].data == NULL)
				MessageBox2("No instruments loaded", "Play", MB_ICONEXCLAMATION);
			else {
				if (sound_init()) song_playing = TRUE;
			}
			break;
		case ID_STOP:
			song_playing = FALSE;
			break;
		case ID_OCTAVE_1: case ID_OCTAVE_1+1: case ID_OCTAVE_1+2:
		case ID_OCTAVE_1+3: case ID_OCTAVE_1+4:
			octave = id - ID_OCTAVE_1;
			CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1+4,
				id, MF_BYCOMMAND);
			break;
		case ID_HELP:
			CreateWindow("ebmused_codelist", "Code list",
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
				NULL, NULL, hinstance, NULL);
			break;
		case ID_ABOUT: {
			extern BOOL CALLBACK AboutDlgProc(HWND,UINT,WPARAM,LPARAM);
			DialogBox(hinstance, MAKEINTRESOURCE(IDD_ABOUT), hWnd, AboutDlgProc);
			break;
		}
		default: printf("Command %d not yet implemented\n", id); break;
		}
		break;
	}
	case WM_NOTIFY: {
		NMHDR *pnmh = (LPNMHDR)lParam;
		if (pnmh->code == TCN_SELCHANGE)
			tab_selected(TabCtrl_GetCurSel(pnmh->hwndFrom));
		break;
	}
	case WM_CLOSE:
		if (!close_rom()) break;
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

/*LONG CALLBACK exfilter(EXCEPTION_POINTERS *exi) {
	printf("Unhandled exception\n");
	return EXCEPTION_EXECUTE_HANDLER;
}*/

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	hinstance = hInstance;
	WNDCLASS wc;
	MSG msg;

	wc.style         = 0;
	wc.lpfnWndProc   = MainWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(1));
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
	wc.lpszMenuName  = MAKEINTRESOURCE(IDM_MENU);
	wc.lpszClassName = "ebmused_main";
	RegisterClass(&wc);

	wc.lpszMenuName  = NULL;
	for (int i = 0; i < NUM_TABS; i++) {
		wc.lpfnWndProc   = tab_wndproc[i];
		wc.lpszClassName = tab_class[i];
		RegisterClass(&wc);
	}

	extern LRESULT CALLBACK InstTestWndProc(HWND,UINT,WPARAM,LPARAM);
	extern LRESULT CALLBACK StateWndProc(HWND,UINT,WPARAM,LPARAM);
	extern LRESULT CALLBACK CodeListWndProc(HWND,UINT,WPARAM,LPARAM);
	extern LRESULT CALLBACK OrderWndProc(HWND,UINT,WPARAM,LPARAM);
	extern LRESULT CALLBACK TrackerWndProc(HWND,UINT,WPARAM,LPARAM);
	wc.lpfnWndProc   = InstTestWndProc;
	wc.lpszClassName = "ebmused_insttest";
	RegisterClass(&wc);
	wc.lpfnWndProc   = StateWndProc;
	wc.lpszClassName = "ebmused_state";
	RegisterClass(&wc);

	wc.hbrBackground = NULL;
	wc.lpfnWndProc   = CodeListWndProc;
	wc.lpszClassName = "ebmused_codelist";
	RegisterClass(&wc);
	wc.lpfnWndProc   = OrderWndProc;
	wc.lpszClassName = "ebmused_order";
	RegisterClass(&wc);

	wc.style         = CS_HREDRAW;
	wc.lpfnWndProc   = TrackerWndProc;
	wc.lpszClassName = "ebmused_tracker";
	RegisterClass(&wc);

	InitCommonControls();

//	SetUnhandledExceptionFilter(exfilter);

	hwndMain = CreateWindow("ebmused_main", "EarthBound Music Editor",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 720, 540,
		NULL, NULL, hInstance, NULL);
	ShowWindow(hwndMain, nCmdShow);

	hmenu = GetMenu(hwndMain);
	CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1+4, ID_OCTAVE_1+2, MF_BYCOMMAND);

	hcontextmenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDM_CONTEXTMENU));

	hfont = GetStockObject(DEFAULT_GUI_FONT);

	HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDA_ACCEL));

	if (_ARGC > 1)
		open_rom(_ARGV[1], FALSE);
	tab_selected(0);

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!TranslateAccelerator(hwndMain, hAccel, &msg)) {
			TranslateMessage(&msg);
		}
		DispatchMessage(&msg);
	}
	DestroyMenu(hcontextmenu);
	return msg.wParam;
}
