// The original MinGW project uses a different name for this symbol for some reason.
// Feature check macros from the MinGW-w64 wiki:
// https://sourceforge.net/p/mingw-w64/wiki2/Answer%20Check%20For%20Mingw-w64/
#ifdef __MINGW32__
#include <_mingw.h>
#endif
#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
	#define _ARGC _argc
	#define _ARGV _argv
#else
	#define _ARGC __argc
	#define _ARGV __argv
#endif

#include "id.h"
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <mmsystem.h>
#include "ebmusv2.h"
#include "misc.h"

enum {
	MAIN_WINDOW_WIDTH = 720,
	MAIN_WINDOW_HEIGHT = 540,
	TAB_CONTROL_WIDTH = 600,
	TAB_CONTROL_HEIGHT = 25,
	STATUS_WINDOW_HEIGHT = 24,
	CODELIST_WINDOW_WIDTH = 640,
	CODELIST_WINDOW_HEIGHT = 480
};

struct song cur_song;
BYTE packs_loaded[3] = { 0xFF, 0xFF, 0xFF };
int current_block = -1;
struct song_state pattop_state, state;
int octave = 2;
int midiDevice = -1;
HINSTANCE hinstance;
HWND hwndMain;
HWND hwndStatus;
HMENU hmenu, hcontextmenu;
HWND tab_hwnd[NUM_TABS];

static const int INST_TAB = 1;
static int current_tab;
static const char *const tab_class[NUM_TABS] = {
	"ebmused_bgmlist",
	"ebmused_inst",
	"ebmused_editor",
	"ebmused_packs"
};
static const char *const tab_name[NUM_TABS] = {
	"BGM Table",
	"Instruments",
	"Tracker",
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
char *open_dialog(BOOL (WINAPI *func)(LPOPENFILENAME),
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
	format_status(0, "%s", "");

	for (int i = 0; i < NUM_TABS; i++) {
		if (tab_hwnd[i]) {
			DestroyWindow(tab_hwnd[i]);
			tab_hwnd[i] = NULL;
		}
	}

	RECT rc;
	GetClientRect(hwndMain, &rc);
	int status_height = hwndStatus ? STATUS_WINDOW_HEIGHT : 0;
	tab_hwnd[new] = CreateWindow(tab_class[new], NULL,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, scale_y(TAB_CONTROL_HEIGHT), rc.right, rc.bottom - scale_y(TAB_CONTROL_HEIGHT + status_height),
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

struct spcDetails {
	WORD music_table_addr;
	WORD instrument_table_addr;
	WORD music_addr;
	BYTE music_index;
};

BOOL try_parse_music_table(const BYTE *spc, struct spcDetails *out_details) {
	if (memcmp(spc, "\x1C\x5D\xF5", 3) != 0) return FALSE;
	WORD addr_hi = *((WORD*)&spc[3]);

	// Check for Konami-only branch
	if (spc[5] == 0xF0) {
		spc += 2; // skip two bytes (beq ..)
	}

	if (spc[5] != 0xFD) return FALSE;

	// Check for Starfox branch
	if (memcmp(&spc[6], "\xD0\x03\xC4", 3) == 0 && spc[10] == 0x6F) {
		spc += 5; // skip these 5 bytes
	}

	if (spc[6] != 0xF5) return FALSE; // mov a,
	WORD addr_lo = *((WORD*)&spc[7]); //       $....+x

	if (spc[9] != 0xDA || spc[10] != 0x40) return FALSE; // mov $40,ya

	// Validate retrieved address
	if (addr_lo != addr_hi - 1) return FALSE;

	out_details->music_table_addr = addr_lo;
	return TRUE;
}

BOOL try_parse_music_address(const BYTE* spc, struct spcDetails *out_details) {
	WORD loop_addr = *(WORD *)&spc[0x40];
	WORD *terminator = (WORD *)&spc[loop_addr];
	while (terminator[0]) {
		// Make sure terminator stays in bounds and allows space for one pattern block after it.
		// Also arbitrarily limit number of patterns between loop and terminator to 256.
		if ((BYTE *)terminator < &spc[0x10000 - 16 - 2]
			&& terminator - (WORD *)&spc[loop_addr] <= 0x100
		) {
			terminator++;
		} else {
			return FALSE;
		}
	}

	// Count all unique patterns past the terminator.
	typedef WORD PATTERN[8];
	PATTERN *patterns = (PATTERN*)&terminator[1];
	unsigned int numPatterns = 0;
	const unsigned int maxPatterns = (&spc[0xFFFF] - (BYTE *)patterns) / sizeof(PATTERN); // Amount of patterns that can possibly fit.
	// Pattern is only valid if all channel addresses are ordered. (Ignore 0x0000 channel addresses)
	for (unsigned int i = 0;
		i < maxPatterns // Limit count to as many as can possibly fit in memory
			&& (patterns[i][0] < patterns[i][1] || !patterns[i][1])
			&& (patterns[i][1] < patterns[i][2] || !patterns[i][2])
			&& (patterns[i][2] < patterns[i][3] || !patterns[i][3])
			&& (patterns[i][3] < patterns[i][4] || !patterns[i][4])
			&& (patterns[i][4] < patterns[i][5] || !patterns[i][5])
			&& (patterns[i][5] < patterns[i][6] || !patterns[i][6])
			&& (patterns[i][6] < patterns[i][7] || !patterns[i][7])
			;
		i++) {
		numPatterns = i + 1;
	}

	// sanity check. Assert smallest pattern is greater than 0xFF and number of patterns is less than as many as can fit in memory
	if (patterns[0][0] <= 0xFF || numPatterns == 0 || numPatterns >= maxPatterns) return FALSE;

	// Find the first pattern by iterating backwards until one pattern doesn't point at a pattern address.
	WORD *music_addr_ptr = (WORD*)&spc[loop_addr];
	BOOL patternExists = TRUE;
	for (WORD* prev = &music_addr_ptr[-1]; prev && patternExists; prev--) {
		patternExists = FALSE;
		// if any patterns contain prev, continue
		for (unsigned int i = 0; i < numPatterns; i++) {
			if (patterns[i] == (WORD*)&spc[*prev]) {
				patternExists = TRUE;
				music_addr_ptr = prev;
				break;
			}
		}
	}

	// sanity check
	if ((BYTE*)music_addr_ptr - spc <= 0xFF) return FALSE;

	out_details->music_addr = (BYTE*)music_addr_ptr - spc;
	return TRUE;
}

BOOL try_parse_inst_directory(const BYTE *spc, struct spcDetails *out_details) {
	if (memcmp(spc, "\xCF\xDA\x14\x60\x98", 5) == 0 && memcmp(&spc[6], "\x14\x98", 2) == 0 && spc[9] == 0x15) {
		out_details->instrument_table_addr = spc[5] | (spc[8] << 8);
		return TRUE;
	}

	return FALSE;
}

enum SPC_RESULTS {
	HAS_MUSIC = 1 << 0,
	HAS_MUSIC_TABLE = 1 << 1,
	HAS_INSTRUMENTS = 1 << 2
};

enum SPC_RESULTS try_parse_spc(const BYTE* spc, struct spcDetails *out_details) {
	BOOL foundMusic = FALSE,
		foundMusicTable = FALSE,
		foundInst = FALSE;
	// For i in 0 .. 0xFF00, and also stop if all 3 things we're looking for have been found
	for (int i = 0; i < 0xFF00 && !(foundMusicTable && foundInst); i++) {
		if (!foundMusicTable && spc[i] == 0x1C)
			foundMusicTable = try_parse_music_table(&spc[i], out_details);
		else if (!foundInst && spc[i] == 0xCF)
			foundInst = try_parse_inst_directory(&spc[i], out_details);
	}

	foundMusic = try_parse_music_address(spc, out_details);

	// If we couldn't find the music via snooping the $40 address, try checking if we found a music table.
	if (!foundMusic && foundMusicTable) {
		// Try to get the bgm index from one of these locations, the first that isn't 0...
		BYTE bgm_index = spc[0x00] ? spc[0x00]
			: spc[0x04] ? spc[0x04]
			: spc[0x08] ? spc[0x08]
			: spc[0xF3] ? spc[0xF3]
			: spc[0xF4];
		if (bgm_index) {
			out_details->music_index = bgm_index;
			out_details->music_addr = ((WORD *)&spc[out_details->music_table_addr])[bgm_index];
			foundMusic = TRUE;
		} else {
			// If we couldn't find the bgm index, try to guess it from the table using the pointer at 0x40
			WORD music_addr = *((WORD*)&spc[0x40]);
			WORD closestDiff = 0xFFFF;
			for (unsigned int i = 0; i < 0xFF; i++) {
				WORD addr = ((WORD *)&spc[out_details->music_table_addr])[i];
				if (music_addr < addr && addr - music_addr < closestDiff) {
					closestDiff = addr - music_addr;
					bgm_index = i;
				}
			}

			if (music_addr > 0xFF) {
				out_details->music_addr = music_addr;
				out_details->music_index = 0;
				foundMusic = TRUE;
			}
		}
	}

	enum SPC_RESULTS results = 0;
	if (foundMusicTable) results |= HAS_MUSIC_TABLE;
	if (foundMusic) results |= HAS_MUSIC;
	if (foundInst) results |= HAS_INSTRUMENTS;
	return results;
}

static void import_spc() {
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

	// Backup the currently loaded SPC in case we need to restore it upon an error.
	// This can be updated once methods like decode_samples don't rely on the global "spc" variable.
	BYTE backup_spc[0x10000];
	memcpy(backup_spc, spc, 0x10000);
	WORD original_sample_ptr_base = sample_ptr_base;

	BYTE dsp[0x80];

	if (fseek(f, 0x100, SEEK_SET) == 0
		&& fread(spc, 0x10000, 1, f) == 1
		&& fread(dsp, 0x80, 1, f) == 1
	) {
		sample_ptr_base = dsp[0x5D] << 8;
		free_samples();
		decode_samples(&spc[sample_ptr_base]);

		struct spcDetails details;
		enum SPC_RESULTS results = try_parse_spc(spc, &details);
		if (results) {
			if (results & HAS_INSTRUMENTS) {
				printf("Instrument table found: %#X\n", details.instrument_table_addr);
				inst_base = details.instrument_table_addr;
			}
			if (results & HAS_MUSIC) {
				printf("Music table found: %#X\n", details.music_table_addr);
				printf("Music index found: %#X\n", details.music_index);
				printf("Music found: %#X\n", details.music_addr);

				free_song(&cur_song);
				decompile_song(&cur_song, details.music_addr, 0xffff);
			}

			initialize_state();
			cur_song.changed = TRUE;
			save_cur_song_to_pack();
			SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0);
		} else {
			// Restore SPC state and samples
			memcpy(spc, backup_spc, 0x10000);
			sample_ptr_base = original_sample_ptr_base;
			free_samples();
			decode_samples(&spc[sample_ptr_base]);

			MessageBox2("Could not parse SPC.", "SPC Import", MB_ICONEXCLAMATION);
		}
	} else {
		// Restore SPC state
		memcpy(spc, backup_spc, 0x10000);

		if (feof(f))
			MessageBox2("End-of-file reached while reading the SPC file", "Import", MB_ICONEXCLAMATION);
		else
			MessageBox2("Error reading the SPC file", "Import", MB_ICONEXCLAMATION);
	}

	fclose(f);
}

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
		MessageBox2("No song loaded", "Export SPC", MB_ICONEXCLAMATION);
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
//		for (int pack = 0; pack < 3; pack++) {
//			pack_to_spc(packs_loaded[pack], new_spc);
//		}

		// compile_song corrupts the spc and any potential samples/instruments, so we need to make a copy first...
		BYTE spc_copy[0x10000];
		memcpy(spc_copy, spc, 0x10000);
		struct SamplePointers { WORD start, loop; } *sample_pointers = (struct SamplePointers *)&spc_copy[sample_ptr_base];

		enum {
			SPC_HEADER_SIZE = 0x100, // Size of SPC file header
			BRR_BLOCK_SIZE = 9, // Size of a BRR block in bytes
			NUM_INSTRUMENTS = 80, // Number of instruments to export
			BUFFER = 0x10 // Generic buffer size between tables/data
		};

		// Move the music from wherever it was in the spc to 0x3200... (ripping out part of the program block in the process...)
		const WORD dstMusic = 0x3100;
		const int music_size = compile_song(&cur_song);
		memcpy(&spc[SPC_HEADER_SIZE + dstMusic], &spc_copy[cur_song.address], music_size);

		// recompile so the addresses are correct...
		cur_song.address = dstMusic;
		compile_song(&cur_song);

		// Size of the instruments table
		const WORD inst_size = NUM_INSTRUMENTS*6;
		// Calculate buffer size needed for sample pointer table to round to nearest 0x100.
		const WORD REMAINDER = 0x100 - ((dstMusic + music_size) & 0xFF);
		const WORD dstSamplePointers = dstMusic + music_size + REMAINDER;
		const WORD dstInstruments = dstSamplePointers + 0x4*NUM_INSTRUMENTS + BUFFER;
		const WORD dstSamples = dstInstruments + inst_size + BUFFER;

		// Blank out some space (for the buffer spaces)
		memset(&new_spc[SPC_HEADER_SIZE + dstMusic], 0, dstSamples - dstMusic);

		// Copy music data...
		printf("Packing music data to $%x\n", cur_song.address);
		memcpy(&new_spc[SPC_HEADER_SIZE + dstMusic], &spc[cur_song.address], music_size);
		// Use the copy we made to restore the working spc to before compile_song borked it with the dstMusic address...
		memcpy(spc, spc_copy, 0x10000);

		// Copy instrument data...
		printf("Packing instrument table to $%x\n", dstInstruments);
		memcpy(&new_spc[SPC_HEADER_SIZE + dstInstruments], &spc_copy[inst_base], inst_size);

		// Copy sample data and pointers...
		// Remap sample data to new locations to repack them
		struct {
			WORD src, len, dst;
		} sampleMap [NUM_INSTRUMENTS];
		WORD offset = 0;
		for (unsigned int i = 0; i < NUM_INSTRUMENTS && sample_pointers[i].start < 0xFF00 && sample_pointers[i].start > 0xFF; i++)
		{
			const WORD src_addr = sample_pointers[i].start;
			WORD len = sample_pointers[i].loop - sample_pointers[i].start;
			WORD dst_addr = 0;

			// Find if we've already stored this sample somewhere
			for (unsigned int j = 0; j < i; j++) {
				if (sampleMap[j].src == src_addr && sampleMap[j].len == len) {
					dst_addr = sampleMap[j].dst;
					len = sampleMap[j].len;
					break;
				}
			}

			// If we haven't, add it
			if (!dst_addr) {
				unsigned int num_brr_blocks = count_brr_blocks(spc_copy, src_addr);
				if (num_brr_blocks == 0) {
					printf("Invalid BRR block. Instrument: %d BRR source: $%x\n", i, src_addr);
				} else if (offset < 0xFFFF - dstSamples - num_brr_blocks * BRR_BLOCK_SIZE) {
					dst_addr = dstSamples + offset;
					printf("Packing sample #%d: $%x size: 0x%x\n", i, dst_addr, num_brr_blocks * BRR_BLOCK_SIZE);
					memcpy(&new_spc[SPC_HEADER_SIZE + dst_addr], &spc_copy[src_addr], num_brr_blocks * BRR_BLOCK_SIZE);

					offset += num_brr_blocks * BRR_BLOCK_SIZE;
				} else {
					printf("Sample #%d is too big. (Destination: $%x, size: 0x%x) Skipped...\n", i, dstSamples + offset, num_brr_blocks * BRR_BLOCK_SIZE);
				}
			}

			sampleMap[i].src = src_addr;
			sampleMap[i].dst = dst_addr;
			sampleMap[i].len = len;
			WORD loop_addr = dst_addr + len;
			printf("Packing sample pointer %d to $%x: %x %x\n", i, dstSamplePointers +0x4*i, dst_addr, loop_addr);
			memcpy(&new_spc[SPC_HEADER_SIZE + dstSamplePointers + 0x4*i], &dst_addr, 2);
			memcpy(&new_spc[SPC_HEADER_SIZE + dstSamplePointers + 0x4*i + 0x2], &loop_addr, 2);
		}

		// We're done copying the important stuff, now we just need to adjust some pointers in the music program.
		{
			// Set pattern repeat location
			const WORD repeat_address = dstMusic + 0x2*cur_song.repeat_pos;
			memcpy(&new_spc[SPC_HEADER_SIZE + 0x40], &repeat_address, 2);

			// Set BGM to load
			const BYTE bgm = selected_bgm + 1;
			memcpy(&new_spc[SPC_HEADER_SIZE + 0xF4], &bgm, 1);

			// Update song address of current BGM within the music program
			memcpy(&new_spc[0x2F48 + 0x2*bgm], &dstMusic, 2);

			// Update instrument loader addresses
			new_spc[SPC_HEADER_SIZE + 0x972] = dstInstruments & 0xFF;
			new_spc[SPC_HEADER_SIZE + 0x975] = dstInstruments >> 8;

			// Update sample address high byte
			new_spc[SPC_HEADER_SIZE + 0x1005D] = dstSamplePointers >> 8; // DSP
			new_spc[SPC_HEADER_SIZE + 0x52A] = dstSamplePointers >> 8; // Loader (for good measure)
		}

		// Save byte array to file
		fwrite(new_spc, spc_size, 1, f);

		free(new_spc);
	} else {
		MessageBox2("Template SPC could not be loaded", "Export SPC", MB_ICONEXCLAMATION);
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

static BOOL validate_playable(void) {
	if (cur_song.order_length == 0) {
		MessageBox2("No song loaded", "Play", MB_ICONEXCLAMATION);
		return FALSE;
	} else if (samp[0].data == NULL) {
		MessageBox2("No instruments loaded", "Play", MB_ICONEXCLAMATION);
		return FALSE;
	} else {
		return TRUE;
	}
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case MM_WOM_OPEN: case MM_WOM_CLOSE: case MM_WOM_DONE:
		winmm_message(uMsg);
		break;
	case WM_CREATE: {
		HWND tabs = CreateWindow(WC_TABCONTROL, NULL,
			WS_CHILD | WS_VISIBLE | TCS_BUTTONS, 0, 0, scale_x(TAB_CONTROL_WIDTH), scale_y(TAB_CONTROL_HEIGHT),
			hWnd, NULL, hinstance, NULL);

		TC_ITEM item = {0};
		item.mask = TCIF_TEXT;
		for (int i = 0; i < NUM_TABS; i++) {
			item.pszText = (char*)tab_name[i];
			(void)TabCtrl_InsertItem(tabs, i, &item);
		}
		SendMessage(tabs, WM_SETFONT, tabs_font(), TRUE);
		break;
	}
	case WM_SIZE: {
		int tabs_height = scale_y(TAB_CONTROL_HEIGHT);
		int status_height = hwndStatus ? scale_y(STATUS_WINDOW_HEIGHT) : 0;
		MoveWindow(tab_hwnd[current_tab], 0, tabs_height, LOWORD(lParam), HIWORD(lParam) - tabs_height - status_height, TRUE);
		SendMessage(hwndStatus, uMsg, wParam, lParam);
		break;
	}
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
		case ID_IMPORT_SPC: import_spc(); break;
		case ID_EXPORT: export(); break;
		case ID_EXPORT_SPC: export_spc(); break;
		case ID_EXIT: DestroyWindow(hWnd); break;
		case ID_OPTIONS: {
			extern BOOL CALLBACK OptionsDlgProc(HWND,UINT,WPARAM,LPARAM);
			DialogBox(hinstance, MAKEINTRESOURCE(IDD_OPTIONS), hWnd, OptionsDlgProc);
			if (current_tab == INST_TAB) {
				// Re-enable playback for the instruments tab...
				start_playing();
			}
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
			if (validate_playable()) {
				start_playing();
				EnableMenuItem(hmenu, ID_STOP, MF_ENABLED);
			}
			break;
		case ID_STOP:
			if (current_tab == INST_TAB) {
				stop_capturing_audio();
			} else {
				stop_playing();
				EnableMenuItem(hmenu, ID_PLAY, MF_ENABLED);
			}
			break;
		case ID_CAPTURE_AUDIO: {
			if (current_tab == INST_TAB) {
				if (is_capturing_audio()) {
					stop_capturing_audio();
				} else {
					start_capturing_audio();
				}
			} else {
				if (is_capturing_audio()) {
					stop_capturing_audio();
				} else {
					if (validate_playable() && start_capturing_audio()) {
						start_playing();
						EnableMenuItem(hmenu, ID_STOP, MF_ENABLED);
					}
				}
			}
			break;
		}
		case ID_OCTAVE_1: case ID_OCTAVE_1+1: case ID_OCTAVE_1+2:
		case ID_OCTAVE_1+3: case ID_OCTAVE_1+4:
			octave = id - ID_OCTAVE_1;
			CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1+4,
				id, MF_BYCOMMAND);
			break;
		case ID_HELP:
			CreateWindow("ebmused_codelist", "Code list",
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT, CW_USEDEFAULT, scale_x(CODELIST_WINDOW_WIDTH), scale_y(CODELIST_WINDOW_HEIGHT),
				NULL, NULL, hinstance, NULL);
			break;
		case ID_ABOUT: {
			extern BOOL CALLBACK AboutDlgProc(HWND,UINT,WPARAM,LPARAM);
			DialogBox(hinstance, MAKEINTRESOURCE(IDD_ABOUT), hWnd, AboutDlgProc);
			break;
		}
		case ID_STATUS_BAR: {
			if (hwndStatus) {
				DestroyWindow(hwndStatus);
				hwndStatus = NULL;
				CheckMenuItem(hmenu, ID_STATUS_BAR, MF_UNCHECKED);

				RECT rc;
				GetClientRect(hwndMain, &rc);
				MoveWindow(tab_hwnd[current_tab], 0, scale_y(TAB_CONTROL_HEIGHT), rc.right, rc.bottom - scale_y(TAB_CONTROL_HEIGHT), TRUE);
			} else {
				hwndStatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, NULL, hwndMain, IDS_STATUS);
				CheckMenuItem(hmenu, ID_STATUS_BAR, MF_CHECKED);

				RECT rc;
				GetClientRect(hwndMain, &rc);
				MoveWindow(tab_hwnd[current_tab], 0, scale_y(TAB_CONTROL_HEIGHT), rc.right, rc.bottom - scale_y(TAB_CONTROL_HEIGHT + STATUS_WINDOW_HEIGHT), TRUE);
			}
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

	setup_dpi_scale_values();
	InitCommonControls();

//	SetUnhandledExceptionFilter(exfilter);

	set_up_fonts();

	hwndMain = CreateWindow("ebmused_main", "EarthBound Music Editor",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, scale_x(MAIN_WINDOW_WIDTH), scale_y(MAIN_WINDOW_HEIGHT),
		NULL, NULL, hInstance, NULL);
	hwndStatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, NULL, hwndMain, IDS_STATUS);
	ShowWindow(hwndMain, nCmdShow);

	hmenu = GetMenu(hwndMain);
	CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1+4, ID_OCTAVE_1+2, MF_BYCOMMAND);

	hcontextmenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDM_CONTEXTMENU));

	HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDA_ACCEL));

	if (_ARGC > 1)
		open_rom(_ARGV[1], FALSE);
	tab_selected(0);

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!TranslateAccelerator(hwndMain, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	DestroyMenu(hcontextmenu);
	destroy_fonts();
	return msg.wParam;
}
