#ifndef CreateWindow
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
typedef void *HWND;
#endif

// structure used for track or subroutine
// "size" does not include the ending [00] byte
struct track {
	int size;
	BYTE *track; // NULL for inactive track
};

struct song {
	WORD address;
	BYTE changed;
	int order_length;
	int *order;
	int repeat, repeat_pos;
	int patterns;
	struct track (*pattern)[8];
	int subs;
	struct track *sub;
};

struct parser {
	BYTE *ptr;
	BYTE *sub_ret;
	int sub_start;
	BYTE sub_count;
	BYTE note_len;
};

struct slider {
	WORD cur, delta;
	BYTE cycles, target;
};

struct song_state {
	struct channel_state {
		BYTE *ptr;

		int next; // time left in note

		struct slider note; BYTE cur_port_start_ctr;
		BYTE note_len, note_style;

		BYTE note_release; // time to release note, in cycles

		int sub_start; // current subroutine number
		BYTE *sub_ret; // where to return to after sub
		BYTE sub_count; // number of loops

		BYTE inst; // instrument
		BYTE inst_adsr1;
		BYTE finetune;
		signed char transpose;
		struct slider panning; BYTE pan_flags;
		struct slider volume;
		BYTE total_vol;
		signed char left_vol, right_vol;

		BYTE port_type, port_start, port_length, port_range;
		BYTE vibrato_start, vibrato_speed, vibrato_max_range, vibrato_fadein;
		BYTE tremolo_start, tremolo_speed, tremolo_range;

		BYTE vibrato_phase, vibrato_start_ctr, cur_vib_range;
		BYTE vibrato_fadein_ctr, vibrato_range_delta;
		BYTE tremolo_phase, tremolo_start_ctr;

		struct sample *samp;
		int samp_pos, note_freq;

		double env_height; // envelope height
		double decay_rate;
	} chan[16];
	signed char transpose;
	struct slider volume;
	struct slider tempo;
	int next_timer_tick, cycle_timer;
	BYTE first_CA_inst; // set with FA
	BYTE repeat_count;
	int ordnum;
	int patpos; // Number of cycles since top of pattern
};

struct sample {
	short *data;
	int length;
	int loop_len;
};

struct block {
	WORD size, spc_address;
	BYTE *data; // only used for inmem packs
};

// rom_packs contain info about the pack as it stands in the ROM file
// .status is one of these constants:
#define RPACK_ORIGINAL 0
#define RPACK_MODIFIED 1
#define RPACK_INVALID 2
#define RPACK_SAVED 3

// inmem_packs contain info about the pack as it currently is in the editor
// .status is a bitmask of these constants:
#define IPACK_INMEM 1	// blocks[i].data valid if set
#define IPACK_CHANGED 2
struct pack {
	int start_address;
	int status;	// See constants above
	int block_count;
	struct block *blocks;
};
