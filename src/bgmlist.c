#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ebmusv2.h"
#include "misc.h"
#include "id.h"

#define IDC_ROM_FILE 17
#define IDC_ORIG_ROM_FILE 18
#define IDC_ROM_SIZE 19

#define IDC_LIST 20
#define IDC_SEARCH_TEXT 21
#define IDC_SEARCH 22
#define IDC_TITLE 23
#define IDC_TITLE_CHANGE 24

#define IDC_BGM_NUMBER 25
#define IDC_BGM_IPACK_1 30
#define IDC_BGM_IPACK_2 31
#define IDC_BGM_SPACK 32
#define IDC_BGM_SPCADDR 33
#define IDC_SAVE_INFO 34

#define IDC_CUR_IPACK_1 35
#define IDC_CUR_IPACK_2 36
#define IDC_CUR_SPACK 37
#define IDC_CUR_SPCADDR 38

#define IDC_LOAD_BGM 40
#define IDC_CHANGE_BGM 41

int selected_bgm = 0;
static char bgm_num_text[32] = "BGM --:";

static const struct control_desc bgm_list_controls[] = {
	{ "ListBox", 10, 10,300,-20, NULL, IDC_LIST, WS_BORDER | LBS_NOTIFY | WS_VSCROLL },

	{ "Static", 310, 10, 90, 20, "Current ROM:", 0, SS_RIGHT },
	{ "Static", 410, 10,1000,20, NULL, IDC_ROM_FILE, 0 },
	{ "Static", 310, 30, 90, 20, "Original ROM:", 0, SS_RIGHT },
	{ "Static", 410, 30,1000,20, NULL, IDC_ORIG_ROM_FILE, SS_NOTIFY },
	{ "Static", 310, 50, 90, 20, "Size:", 0, SS_RIGHT },
	{ "Static", 410, 50,100, 20, NULL, IDC_ROM_SIZE, 0 },

	{ "Static", 410,110,100, 20, bgm_num_text, IDC_BGM_NUMBER, 0 },
	{ "Static", 530,110,100, 20, "Currently loaded:", IDC_BGM_NUMBER+1, 0 },
	{ "Static", 315,133, 90, 20, "Inst. packs:", 0, SS_RIGHT },
	{ "Edit",   410,130, 25, 20, NULL, IDC_BGM_IPACK_1, WS_BORDER }, //(ROM) Main Pack textbox
	{ "Edit",   440,130, 25, 20, NULL, IDC_BGM_IPACK_2, WS_BORDER }, //(ROM) Secondary Pack textbox
	{ "Edit",   530,130, 25, 20, NULL, IDC_CUR_IPACK_1, WS_BORDER }, //(Current) Main Pack textbox
	{ "Edit",   560,130, 25, 20, NULL, IDC_CUR_IPACK_2, WS_BORDER }, //(Current) Secondary Pack textbox
	{ "Static", 325,157, 80, 20, "Music pack:", 0, SS_RIGHT },
	{ "Edit",   410,155, 25, 20, NULL, IDC_BGM_SPACK, WS_BORDER }, //(ROM) Music Pack textbox
	{ "Edit",   530,155, 25, 20, NULL, IDC_CUR_SPACK, WS_BORDER }, //(Current) Music Pack textbox
	{ "Static", 325,182, 80, 20, "Start address:", 0, SS_RIGHT },
	{ "Edit",   410,180, 55, 20, NULL, IDC_BGM_SPCADDR, WS_BORDER }, //(ROM) Music ARAM textbox
	{"ComboBox",530,180, 55, 200, NULL, IDC_CUR_SPCADDR, CBS_DROPDOWNLIST | WS_VSCROLL }, //(Current) Music ARAM ComboBox
	{ "Button", 485,130, 25, 30, "-->", IDC_LOAD_BGM, 0 },
	{ "Button", 485,170, 25, 30, "<--", IDC_CHANGE_BGM, 0 },
	{ "Button", 353,205,112, 20, "Update BGM Table", IDC_SAVE_INFO, 0 },
	{ "Edit",   320,250,230, 20, NULL, IDC_SEARCH_TEXT, WS_BORDER },
	{ "Button", 560,250, 60, 20, "Search", IDC_SEARCH, 0 },
	{ "Edit",   320,275,230, 20, NULL, IDC_TITLE, WS_BORDER | ES_AUTOHSCROLL },
	{ "Button", 560,275, 60, 20, "Rename", IDC_TITLE_CHANGE, 0 },
};
static struct window_template bgm_list_template = {
	sizeof(bgm_list_controls) / sizeof(*bgm_list_controls),
	sizeof(bgm_list_controls) / sizeof(*bgm_list_controls),
	0, 0, bgm_list_controls
};

static void set_bgm_info(BYTE *packs_used, int spc_addr) {
	for (int i = 0; i < 3; i++)
		SetDlgItemHex(hwndBGMList, IDC_BGM_IPACK_1+i, packs_used[i], 2);
	SetDlgItemHex(hwndBGMList, IDC_BGM_SPCADDR, spc_addr, 4);
}

static void show_bgm_info() {
	sprintf(bgm_num_text + 4, "%d (0x%02X):", selected_bgm+1, selected_bgm+1);
	SetDlgItemText(hwndBGMList, IDC_BGM_NUMBER, bgm_num_text);
	SetDlgItemText(hwndBGMList, IDC_TITLE, bgm_title[selected_bgm]);
	set_bgm_info(pack_used[selected_bgm], song_address[selected_bgm]);
}

static void show_cur_info() {
	for (int i = 0; i < 3; i++)
		SetDlgItemHex(hwndBGMList, IDC_CUR_IPACK_1+i, packs_loaded[i], 2);

	HWND cb = GetDlgItem(hwndBGMList, IDC_CUR_SPCADDR);
	SendMessage(cb, CB_RESETCONTENT, 0, 0);
	SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)"----");
	int song_pack = packs_loaded[2];
	if (song_pack < NUM_PACKS) {
		struct pack *p = &inmem_packs[song_pack];
		for (int i = 0; i < p->block_count; i++) {
			char buf[5];
			sprintf(buf, "%04X", p->blocks[i].spc_address);
			SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)buf);
		}
	}
	SendMessage(cb, CB_SETCURSEL, current_block + 1, 0);
}

void load_instruments() {
	free_samples();
	memset(spc, 0, 0x10000);
	for (int i = 0; i < 2; i++) {
		int p = packs_loaded[i];
		if (p >= NUM_PACKS) continue;
		int addr, size;
		fseek(rom, rom_packs[p].start_address - 0xC00000 + rom_offset, 0);
		while ((size = fgetw(rom))) {
			addr = fgetw(rom);
			if (size + addr >= 0x10000) {
				MessageBox2("Invalid SPC block", "Error loading instruments", MB_ICONERROR);
				return;
			}
			fread(&spc[addr], size, 1, rom);
		}
	}
	sample_ptr_base = 0x6C00;
	decode_samples(&spc[sample_ptr_base]);
	inst_base = 0x6E00;
	if (samp[0].data == NULL) {
		stop_playing();
		EnableMenuItem(hmenu, ID_PLAY, MF_ENABLED);
	}
	initialize_state();
}

static void load_music(BYTE *packs_used, int spc_addr) {
	packs_loaded[0] = packs_used[0];
	packs_loaded[1] = packs_used[1];
	load_songpack(packs_used[2]);
	select_block_by_address(spc_addr);
	show_cur_info();
	load_instruments();
}

static void song_selected(int index) {
	selected_bgm = index;
	show_bgm_info();
	load_music(pack_used[index], song_address[index]);
}

static void song_search() {
	char str[MAX_TITLE_LEN+1];
	char *endhex;
	GetDlgItemText(hwndBGMList, IDC_SEARCH_TEXT, str, MAX_TITLE_LEN+1);
	int num = strtol(str, &endhex, 16) - 1;
	if (*endhex != '\0' || num < 0 || num >= NUM_SONGS) {
		num = selected_bgm;
		_strlwr(str);
		do {
			char title[MAX_TITLE_LEN+1];
			if (++num == NUM_SONGS) num = 0;
			if (strstr(_strlwr(strcpy(title, bgm_title[num])), str))
				break;
		} while (num != selected_bgm);
	}
	SendDlgItemMessage(hwndBGMList, IDC_LIST, LB_SETCURSEL, num, 0);
	song_selected(num);
}

LRESULT CALLBACK BGMListWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	char buf[MAX_TITLE_LEN+5];
	switch (uMsg) {
	case WM_CREATE:
		create_controls(hWnd, &bgm_list_template, lParam);
		break;
	case WM_SIZE:
		move_controls(hWnd, &bgm_list_template, lParam);
		break;
	case WM_ROM_OPENED:
		SetDlgItemText(hWnd, IDC_ROM_FILE, rom_filename);
		SetDlgItemText(hWnd, IDC_ORIG_ROM_FILE, orig_rom_filename
			? orig_rom_filename : "None specified (click to set)");
		sprintf(buf, "%.2f MB", rom_size / 1048576.0);
		SetDlgItemText(hWnd, IDC_ROM_SIZE, buf);
		HWND list = GetDlgItem(hWnd, IDC_LIST);
		SendMessage(list, WM_SETREDRAW, FALSE, 0);
		for (int i = 0; i < NUM_SONGS; i++) {
			sprintf(buf, "%02X: %s", i+1, bgm_title[i]);
			SendMessage(list, LB_ADDSTRING, 0, (LPARAM)buf);
		}
		SendMessage(list, WM_SETREDRAW, TRUE, 0);
		SendMessage(list, LB_SETCURSEL, selected_bgm, 0);
		SetFocus(list);
		show_bgm_info();
		for (int i = 20; i <= 41; i++)
			EnableWindow(GetDlgItem(hWnd, i), TRUE);
		// fallthrough
	case WM_SONG_IMPORTED:
		show_cur_info();
		break;
	case WM_ROM_CLOSED:
		SetDlgItemText(hWnd, IDC_ROM_FILE, NULL);
		SetDlgItemText(hWnd, IDC_ORIG_ROM_FILE, NULL);
		SetDlgItemText(hWnd, IDC_ROM_SIZE, NULL);
		SendDlgItemMessage(hWnd, IDC_LIST, LB_RESETCONTENT, 0, 0);
		for (int i = 20; i <= 41; i++)
			EnableWindow(GetDlgItem(hWnd, i), FALSE);
		break;
	case WM_COMMAND: {
		WORD id = LOWORD(wParam), action = HIWORD(wParam);
		switch (id) {
		case IDC_ORIG_ROM_FILE:
			if (!rom) break;
			if (get_original_rom())
				SetWindowText((HWND)lParam, orig_rom_filename);
			break;
		case IDC_SEARCH:
			song_search();
			break;
		case IDC_LIST:
			if (action == LBN_SELCHANGE)
				song_selected(SendMessage((HWND)lParam, LB_GETCURSEL, 0, 0));
			break;
		case IDC_TITLE_CHANGE: {
			if (bgm_title[selected_bgm] != bgm_orig_title[selected_bgm])
				free(bgm_title[selected_bgm]);
			GetDlgItemText(hWnd, IDC_TITLE, buf+4, MAX_TITLE_LEN+1);
			bgm_title[selected_bgm] = _strdup(buf+4);
			sprintf(buf, "%02X:", selected_bgm + 1);
			buf[3] = ' ';
			SendDlgItemMessage(hWnd, IDC_LIST, LB_DELETESTRING, selected_bgm, 0);
			SendDlgItemMessage(hWnd, IDC_LIST, LB_INSERTSTRING, selected_bgm, (LPARAM)buf);
			SendDlgItemMessage(hWnd, IDC_LIST, LB_SETCURSEL, selected_bgm, 0);

			metadata_changed = TRUE;
			break;
		}
		case IDC_SAVE_INFO: {
			BYTE new_pack_used[3];
			for (int i = 0; i < 3; i++) {
				int pack = GetDlgItemHex(hWnd, IDC_BGM_IPACK_1 + i);
				if (pack < 0) break;
				new_pack_used[i] = pack;
			}
			int new_spc_address = GetDlgItemHex(hWnd, IDC_BGM_SPCADDR);
			if (new_spc_address < 0 || new_spc_address > 0xFFFF) break;

			fseek(rom, BGM_PACK_TABLE + rom_offset + 3 * selected_bgm, SEEK_SET);
			if (!fwrite(new_pack_used, 3, 1, rom)) {
write_error:	MessageBox2(strerror(errno), "Save", MB_ICONERROR);
				break;
			}
			memcpy(&pack_used[selected_bgm], new_pack_used, 3);
			fseek(rom, song_pointer_table_offset + 2 * selected_bgm, SEEK_SET);
			if (!fwrite(&new_spc_address, 2, 1, rom))
				goto write_error;
			song_address[selected_bgm] = new_spc_address;
			fflush(rom);
			sprintf(buf, "Info for BGM %02X saved!", selected_bgm + 1);
			MessageBox2(buf, "BGM Table Updated", MB_OK);
			break;
		}
		case IDC_CUR_IPACK_1:
		case IDC_CUR_IPACK_2:
		case IDC_CUR_SPACK:
			if (action == EN_KILLFOCUS) {
				int num = GetDlgItemHex(hWnd, id);
				if (num < 0 || packs_loaded[id - IDC_CUR_IPACK_1] == num)
					break;
				if (id == IDC_CUR_SPACK) {
					load_songpack(num);
					select_block(-1);
					show_cur_info();
				} else {
					packs_loaded[id - IDC_CUR_IPACK_1] = num;
					load_instruments();
				}
			}
			break;
		case IDC_CUR_SPCADDR:
			if (action == CBN_SELCHANGE) {
				select_block(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0) - 1);
			}
			break;
		case IDC_LOAD_BGM: {
			BYTE pack_used[3];
			int spc_address;
			for (int i = 0; i < 3; i++) {
				pack_used[i] = GetDlgItemHex(hWnd, IDC_BGM_IPACK_1 + i);
			}
			spc_address = GetDlgItemHex(hWnd, IDC_BGM_SPCADDR);
			load_music(pack_used, spc_address);
			break;
		}
		case IDC_CHANGE_BGM:
			{	struct block *b = get_cur_block();
				if (b) set_bgm_info(packs_loaded, b->spc_address);
			}
			break;
		}
		break;
	}
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}
