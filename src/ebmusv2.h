#include <windows.h>
#include "structs.h"

#ifdef NDEBUG
#define printf(x,...)
#endif

// EarthBound related constants
#define NUM_SONGS 0xBF
#define NUM_PACKS 0xA9
#define BGM_PACK_TABLE 0x4F70A
#define PACK_POINTER_TABLE 0x4F947
#define SONG_POINTER_TABLE 0x26298C

// other constants and stuff
#define MAX_TITLE_LEN 60
#define MAX_TITLE_LEN_STR "60"
#define WM_ROM_OPENED WM_USER
#define WM_ROM_CLOSED WM_USER+1
#define WM_SONG_IMPORTED WM_USER+2
#define WM_SONG_LOADED WM_USER+3
#define WM_SONG_NOT_LOADED WM_USER+4
#define WM_PACKS_SAVED WM_USER+5

// main.c
extern BYTE packs_loaded[3];
extern int current_block;
extern int octave;
extern int midiDevice;
extern int selected_bgm;
extern struct song cur_song;
extern struct song_state pattop_state, state;
extern HINSTANCE hinstance;
extern HWND hwndMain;
#ifdef CreateWindow
extern HMENU hmenu, hcontextmenu;
extern HFONT hfont;
#endif
#define NUM_TABS 4
extern HWND tab_hwnd[NUM_TABS];
#define hwndBGMList tab_hwnd[0]
#define hwndInstruments tab_hwnd[1]
#define hwndEditor tab_hwnd[2]
#define hwndPackList tab_hwnd[3]
BOOL get_original_rom(void);
BOOL save_all_packs(void);

// bgmlist.c

// brr.c
extern struct sample samp[128];
extern WORD *brr_table;
extern const unsigned int BRR_BLOCK_SIZE;
extern unsigned int count_brr_blocks(const BYTE *spc, WORD start);
void decode_samples(const unsigned char *ptrtable);
void free_samples(void);

// ctrltbl.c
struct control_desc {
	char *class; short x, y, xsize, ysize; char *title; DWORD id; DWORD style;
};
struct window_template {
	int num, lower, winsize, divy; const struct control_desc *controls;
};
#ifdef CreateWindow
void create_controls(HWND hWnd, struct window_template *t, LPARAM cs);
void move_controls(HWND hWnd, struct window_template *t, LPARAM lParam);
#endif

// inst.c
int note_from_key(int key, BOOL shift);

// midi.c
void closeMidiInDevice();
void openMidiInDevice(int deviceId, void* callback);

// parser.c
extern const BYTE code_length[];
void parser_init(struct parser *p, const struct channel_state *c);
BYTE *next_code(BYTE *p);
BOOL parser_advance(struct parser *p);

// play.c
extern BYTE spc[65536];
extern int inst_base;
void set_inst(struct song_state *st, struct channel_state *c, int inst);
void calc_freq(struct channel_state *c, int note16);
void load_pattern(void);
BOOL do_cycle_no_sound(struct song_state *st);
BOOL do_timer(void);
void initialize_state(void);

// loadrom.c
#ifdef EOF
extern FILE *rom;
#endif
extern int rom_size;
extern int rom_offset;
extern char *rom_filename;
extern unsigned char pack_used[NUM_SONGS][3];
extern unsigned short song_address[NUM_SONGS];
extern struct pack rom_packs[NUM_PACKS];
extern struct pack inmem_packs[NUM_PACKS];
BOOL close_rom(void);
BOOL open_rom(char *filename, BOOL readonly);
BOOL open_orig_rom(char *filename);

// metadata.c
extern char *bgm_title[NUM_SONGS];
extern BOOL metadata_changed;
#ifdef EOF
extern FILE *orig_rom;
extern int orig_rom_offset;
#endif
extern char *orig_rom_filename;
extern const char *const bgm_orig_title[NUM_SONGS];
void load_metadata(void);
void save_metadata(void);
void free_metadata(void);

// misc.c
void enable_menu_items(const BYTE *list, int flags);
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
// Don't declare parameters, to avoid pointer conversion warnings
// (you can't implicitly convert between foo** and void** because of
//  word-addressed architectures. This is x86 so it's ok)
void *array_insert(/*void **array, int *size, int elemsize, int index*/);
void array_delete(void *array, int *size, int elemsize, int index);

// packlist.c

// packs.c
extern const DWORD pack_orig_crc[];
void free_pack(struct pack *p);
struct pack *load_pack(int pack);
void load_songpack(int new_pack);
struct block *get_cur_block(void);
void select_block(int block);
void select_block_by_address(int spc_addr);
struct block *save_cur_song_to_pack(void);
int calc_pack_size(struct pack *p);
void new_block(struct block *b);
void delete_block(int block);
void move_block(int to);
BOOL save_pack(int pack);

// ranges.c
#define AREA_END -4
#define AREA_NOT_IN_FILE -3
#define AREA_NON_SPC -2
#define AREA_FREE -1
extern int area_count;
extern struct area { int address, pack; } *areas;
extern void init_areas(void);
extern void change_range(int start, int end, int from, int to);
extern int check_range(int start, int end, int pack);

// song.c
extern char *decomp_error;
BOOL validate_track(BYTE *data, int size, BOOL is_sub);
int compile_song(struct song *s);
void decompile_song(struct song *s, int start_addr, int end_addr);
void free_song(struct song *s);

// songed.c
void order_insert(int pos, int pat);
struct track *pattern_insert(int pat);
void pattern_delete(int pat);
BOOL split_pattern(int pos);
BOOL join_patterns(void);
int create_sub(BYTE *start, BYTE *end, int *count);
void order_delete(int pos);

// sound.c
extern int mixrate;
extern int chmask;
extern BOOL song_playing;
extern int timer_speed;
int sound_init(void);
void winmm_message(unsigned int uMsg);

// text.c
int calc_track_size_from_text(char *p);
BOOL text_to_track(char *str, struct track *t, BOOL is_sub);
int text_length(BYTE *start, BYTE *end);
void track_to_text(char *out, BYTE *track, int size);

// tracker.c
extern HWND hwndTracker;
void tracker_scrolled(void);
void load_pattern_into_tracker(void);
void editor_command(int id);
