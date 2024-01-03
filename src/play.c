#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ebmusv2.h"
#include "id.h"

BYTE spc[65536];
int inst_base = 0x6E00;

// note style tables, from 6F80
static const unsigned char release_table[] = {
	0x33, 0x66, 0x7f, 0x99, 0xb2, 0xcc, 0xe5, 0xfc
};
static const unsigned char volume_table[] = {
	0x19, 0x33, 0x4c, 0x66, 0x72, 0x7f, 0x8c, 0x99,
	0xa5, 0xb2, 0xbf, 0xcc, 0xd8, 0xe5, 0xf2, 0xfc
};

static void calc_total_vol(struct song_state *st, struct channel_state *c,
	signed char trem_phase)
{
	BYTE v = (trem_phase << 1 ^ trem_phase >> 7) & 0xFF;
	v = ~(v * c->tremolo_range >> 8) & 0xFF;

	v = v * (st->volume.cur >> 8) >> 8;
	v = v * volume_table[c->note_style & 15] >> 8;
	v = v * (c->volume.cur >> 8) >> 8;
	c->total_vol = v * v >> 8;
}

static int calc_vol_3(struct channel_state *c, int pan, int flag) {
	static const BYTE pan_table[] = {
		0x00, 0x01, 0x03, 0x07, 0x0D, 0x15, 0x1E, 0x29,
		0x34, 0x42, 0x51, 0x5E, 0x67, 0x6E, 0x73, 0x77,
		0x7A, 0x7C, 0x7D, 0x7E, 0x7F, 0xAA
	};
	const BYTE *ph = &pan_table[pan >> 8];
	int v = ph[0] + ((ph[1] - ph[0]) * (pan & 255) >> 8);
	v = v * c->total_vol >> 8;
	if (c->pan_flags & flag) v = -v;
	return v;
}

static void calc_vol_2(struct channel_state *c, int pan) {
	c->left_vol  = calc_vol_3(c, pan,          0x80);
	c->right_vol = calc_vol_3(c, 0x1400 - pan, 0x40);
}

static void make_slider(struct slider *s, int cycles, int target) {
	s->delta = cycles == 0 ? 0xFF : ((target << 8) - (s->cur & 0xFF00)) / cycles;
	s->cycles = cycles;
	s->target = target;
}

static void slide(struct slider *s) {
	if (s->cycles) {
		if (--s->cycles == 0)
			s->cur = s->target << 8;
		else
			s->cur += s->delta;
	}
}

void set_inst(struct song_state *st, struct channel_state *c, int inst) {
	static const short rates[32] = {
		  0, 2048, 1536, 1280, 1024, 768, 640, 512,
		384,  320,  256,  192,  160, 128,  96,  80,
		 64,   48,   40,   32,   24,  20,  16,  12,
		 10,    8,    6,    5,    4,   3,   2,   1
	};
	// CA and up is for instruments in the second pack (set with FA xx)
	if (inst >= 0x80)
		inst += st->first_CA_inst - 0xCA;

	BYTE *idata = &spc[inst_base + 6*inst];
	if (inst < 0 || inst >= MAX_INSTRUMENTS || !samp[idata[0]].data ||
		(idata[4] == 0 && idata[5] == 0))
	{
		printf("ch %d: bad inst %X\n", (int)(c - st->chan), inst);
		return;
	}

	c->inst = inst;
	c->inst_adsr1 = idata[1];
	c->inst_adsr2 = idata[2];
	c->inst_gain = idata[3];
	c->attack_rate = rates[(c->inst_adsr1 & 0xF) * 2 + 1];
	c->decay_rate = rates[((c->inst_adsr1 >> 4) & 7) * 2 + 16];
	c->sustain_level = ((c->inst_adsr2 >> 5) & 7) * 0x100 + 0x100;
	c->sustain_rate = rates[c->inst_adsr2 & 0x1F];
	c->gain_rate = rates[c->inst_gain & 0x1F];
}

// calculate how far to advance the sample pointer on each output sample
void calc_freq(struct channel_state *c, int note16) {
	static const WORD note_freq_table[] = {
		0x085F, 0x08DE, 0x0965, 0x09F4, 0x0A8C, 0x0B2C, 0x0BD6, 0x0C8B,
		0x0D4A, 0x0E14, 0x0EEA, 0x0FCD, 0x10BE
	};

	// What is this for???
	if (note16 >= 0x3400)     note16 += (note16 >> 8) - 0x34;
	else if (note16 < 0x1300) note16 += ((note16 >> 8) - 0x13) << 1;

	if ((WORD)note16 >= 0x5400) {
		c->note_freq = 0;
		return;
	}

	int octave = (note16 >> 8) / 12;
	int tone = (note16 >> 8) % 12;
	int freq = note_freq_table[tone];
	freq += (note_freq_table[tone+1] - freq) * (note16 & 0xFF) >> 8;
	freq <<= 1;
	freq >>= 6 - octave;

	BYTE *inst_freq = &spc[inst_base + 6*c->inst + 4];
	freq *= (inst_freq[0] << 8 | inst_freq[1]);
	freq >>= 8;
	freq &= 0x3fff;

	c->note_freq = (freq * (32000U << (15 - 12))) / mixrate;
}

static int calc_vib_disp(struct channel_state *c, int phase) {
	int range = c->cur_vib_range;
	if (range > 0xF0)
		range = (range - 0xF0) * 256;

	int disp = (phase << 2) & 255;   /* //// */
	if (phase & 0x40) disp ^= 0xFF;  /* /\/\ */
	disp = (disp * range) >> 8;

	if (phase & 0x80) disp = -disp;  /* /\   */
	return disp;                     /*   \/ */
}

// do a Ex/Fx code
static void do_command(struct song_state *st, struct channel_state *c) {
	unsigned char *p = c->ptr;
	c->ptr += 1 + code_length[*p - 0xE0];
	switch (*p) {
		case 0xE0:
			set_inst(st, c, p[1]);
			break;
		case 0xE1:
			c->pan_flags = p[1];
			c->panning.cur = (p[1] & 0x1F) << 8;
			break;
		case 0xE2:
			make_slider(&c->panning, p[1], p[2]);
			break;
		case 0xE3:
			c->vibrato_start = p[1];
			c->vibrato_speed = p[2];
			c->cur_vib_range = c->vibrato_max_range = p[3];
			c->vibrato_fadein = 0;
			break;
		case 0xE4:
			c->cur_vib_range = c->vibrato_max_range = 0;
			c->vibrato_fadein = 0;
			break;
		case 0xE5:
			st->volume.cur = p[1] << 8;
			break;
		case 0xE6:
			make_slider(&st->volume, p[1], p[2]);
			break;
		case 0xE7:
			st->tempo.cur = p[1] << 8;
			break;
		case 0xE8:
			make_slider(&st->tempo, p[1], p[2]);
			break;
		case 0xE9:
			st->transpose = p[1];
			break;
		case 0xEA:
			c->transpose = p[1];
			break;
		case 0xEB:
			c->tremolo_start = p[1];
			c->tremolo_speed = p[2];
			c->tremolo_range = p[3];
			break;
		case 0xEC:
			c->tremolo_range = 0;
			break;
		case 0xED:
			c->volume.cur = p[1] << 8;
			break;
		case 0xEE:
			make_slider(&c->volume, p[1], p[2]);
			break;
		case 0xEF:
			c->sub_start = p[1] | (p[2] << 8);
			c->sub_ret = c->ptr;
			c->sub_count = p[3];
			c->ptr = cur_song.sub[c->sub_start].track;
			break;
		case 0xF0:
			c->vibrato_fadein = p[1];
			c->vibrato_range_delta = p[1] == 0 ? 0xFF : c->cur_vib_range / p[1];
			break;
		case 0xF1: case 0xF2:
			c->port_type = *p & 1;
			c->port_start = p[1];
			c->port_length = p[2];
			c->port_range = p[3];
			break;
		case 0xF3:
			c->port_length = 0;
			break;
		case 0xF4:
			c->finetune = p[1];
			break;
		case 0xF9: {
			c->cur_port_start_ctr = p[1];
			int target = p[3] + st->transpose;
			if (target >= 0x100) target -= 0xFF;
			target += c->transpose;
			make_slider(&c->note, p[2], target & 0x7F);
			break;
		}
		case 0xFA:
			st->first_CA_inst = p[1];
			break;
	}
}

static short initial_env_height(BOOL adsr_on, unsigned char gain) {
	if (adsr_on || (gain & 0x80))
		return 0;
	else
		return (gain & 0x7F) * 16;
}

void initialize_envelope(struct channel_state *c) {
	c->env_height = initial_env_height(c->inst_adsr1 & 0x80, c->inst_gain);
	c->next_env_height = c->env_height;
	c->env_state = (c->inst_adsr1 & 0x80) ? ENV_STATE_ATTACK : ENV_STATE_GAIN;
	c->next_env_state = c->env_state;
	c->env_counter = 0;
	c->env_fractional_counter = 0;
}

// $0654 + $08D4-$8EF
static void do_note(struct song_state *st, struct channel_state *c, int note) {
	// using >=CA as a note switches to that instrument and plays note A4
	if (note >= 0xCA) {
		set_inst(st, c, note);
		note = 0xA4;
	}

	if (note < 0xC8) {
		c->vibrato_phase = c->vibrato_fadein & 1 ? 0x80 : 0;
		c->vibrato_start_ctr = 0;
		c->vibrato_fadein_ctr = 0;
		c->tremolo_phase = 0;
		c->tremolo_start_ctr = 0;

		c->samp_pos = 0;
		c->samp = &samp[spc[inst_base + 6*c->inst]];
		initialize_envelope(c);

		note &= 0x7F;
		note += st->transpose + c->transpose;
		c->note.cur = note << 8 | c->finetune;

		c->note.cycles = c->port_length;
		if (c->note.cycles) {
			int target = note;
			c->cur_port_start_ctr = c->port_start;
			if (c->port_type == 0)
				c->note.cur -= c->port_range << 8;
			else
				target += c->port_range;
			make_slider(&c->note, c->port_length, target & 0x7F);
		}

		calc_freq(c, c->note.cur);
	}

	// Search forward for the next note (to see if it's C8). This is annoying
	// but necessary - C8 can continue the last note of a subroutine as well
	// as a normal note.
	int next_note;
	{	struct parser p;
		parser_init(&p, c);
		do {
			if (*p.ptr >= 0x80 && *p.ptr < 0xE0)
				break;
		} while (parser_advance(&p));
		next_note = *p.ptr;
	}

	int rel;
	if (next_note == 0xC8) {
		// if the note will be continued, don't release yet
		rel = c->note_len;
	} else {
		rel = (c->note_len * release_table[c->note_style >> 4]) >> 8;
		if (rel > c->note_len - 2)
			rel = c->note_len - 2;
		if (rel < 1)
			rel = 1;
	}
	c->note_release = rel;
}


void load_pattern() {
	state.ordnum++;
	if (state.ordnum >= cur_song.order_length) {
		if (--state.repeat_count >= 0x80)
			state.repeat_count = cur_song.repeat;
		if (state.repeat_count == 0) {
			state.ordnum--;
			stop_playing();
			EnableMenuItem(hmenu, ID_PLAY, MF_ENABLED);
			return;
		}
		state.ordnum = cur_song.repeat_pos;
	}

	int pat = cur_song.order[state.ordnum];
	printf("Order %d: pattern %d\n", state.ordnum, pat);

	int ch;
	for (ch = 0; ch < 8; ch++) {
		state.chan[ch].ptr = cur_song.pattern[pat][ch].track;
		state.chan[ch].sub_count = 0;
		state.chan[ch].volume.cycles = 0;
		state.chan[ch].panning.cycles = 0;
		state.chan[ch].next = 0;
	}
	state.patpos = 0;

	pattop_state = state;
}

static void CF7(struct channel_state *c) {
	if (c->note_release) {
		c->note_release--;
	}
	if (c->note_release == 0) {
		c->next_env_state = ENV_STATE_KEY_OFF;
	}

	// 0D60
	if (c->note.cycles) {
		if (c->cur_port_start_ctr == 0) {
			slide(&c->note);
			calc_freq(c, c->note.cur);
		} else {
			c->cur_port_start_ctr--;
		}
	}

	// 0D79
	if (c->cur_vib_range) {
		if (c->vibrato_start_ctr == c->vibrato_start) {
			int range;
			if (c->vibrato_fadein_ctr == c->vibrato_fadein) {
				range = c->vibrato_max_range;
			} else {
				range = c->cur_vib_range;
				if (c->vibrato_fadein_ctr == 0)
					range = 0;
				range += c->vibrato_range_delta;
				c->vibrato_fadein_ctr++;
			} // DA0
			c->cur_vib_range = range;
			c->vibrato_phase += c->vibrato_speed;
			calc_freq(c, c->note.cur + calc_vib_disp(c, c->vibrato_phase));
		} else {
			c->vibrato_start_ctr++;
		}
	}
}

// $07F9 + $0625
static BOOL do_cycle(struct song_state *st) {
	int ch;
	struct channel_state *c;
	for (ch = 0; ch < 8; ch++) {
		c = &st->chan[ch];
		if (c->ptr == NULL) continue; //8F0

		if (--c->next >= 0) {
			CF7(c);
		} else while (1) {
			unsigned char *p = c->ptr;

			if (*p == 0) { // end of sub or pattern
				if (c->sub_count) // end of sub
					c->ptr = --c->sub_count
						? cur_song.sub[c->sub_start].track
						: c->sub_ret;
				else
					return FALSE;
			} else if (*p < 0x80) {
				c->note_len = *p;
				if (p[1] < 0x80) {
					c->note_style = p[1];
					c->ptr += 2;
				} else {
					c->ptr++;
				}
			} else if (*p < 0xE0) {
				c->ptr++;
				c->next = c->note_len - 1;
				do_note(st, c, *p);
				break;
			} else { // E0-FF
				do_command(st, c);
			}
		}
		// $0B84
		if (c->note.cycles == 0 && *c->ptr == 0xF9)
			do_command(st, c);
	}

	st->patpos++;

	slide(&st->tempo);
	slide(&st->volume);

	for (c = &st->chan[0]; c != &st->chan[8]; c++) {
		if (c->ptr == NULL) continue;

		// @ 0C40
		slide(&c->volume);

		// @ 0C4D
		int tphase = 0;
		if (c->tremolo_range) {
			if (c->tremolo_start_ctr == c->tremolo_start) {
				if (c->tremolo_phase >= 0x80 && c->tremolo_range == 0xFF)
					c->tremolo_phase = 0x80;
				else
					c->tremolo_phase += c->tremolo_speed;
				tphase = c->tremolo_phase;
			} else {
				c->tremolo_start_ctr++;
			}
		}
		calc_total_vol(st, c, tphase);

		// 0C79
		slide(&c->panning);

		// 0C86: volume stuff
		calc_vol_2(c, c->panning.cur);
	}
	return TRUE;
}

BOOL do_cycle_no_sound(struct song_state *st) {
	BOOL ret = do_cycle(st);
	if (ret) {
		int ch;
		for (ch = 0; ch < 8; ch++)
			if (st->chan[ch].note_release == 0)
				st->chan[ch].samp_pos = -1;
	}
	return ret;
}

static int sub_cycle_calc(struct song_state *st, int delta) {
	if (delta < 0x8000)
		return st->cycle_timer * delta >> 8;
	else
		return -(st->cycle_timer * (0x10000 - delta) >> 8);
}

static void do_sub_cycle(struct song_state *st) {
	struct channel_state *c;
	for (c = &st->chan[0]; c != &st->chan[8]; c++) {
		if (c->ptr == NULL) continue;
		// $0DD0

		BOOL changed = FALSE;
		if (c->tremolo_range && c->tremolo_start_ctr == c->tremolo_start) {
			int p = c->tremolo_phase + sub_cycle_calc(st, c->tremolo_speed);
			changed = TRUE;
			calc_total_vol(st, c, p);
		}
		int pan = c->panning.cur;
		if (c->panning.cycles) {
			pan += sub_cycle_calc(st, c->panning.delta);
			changed = TRUE;
		}
		if (changed) calc_vol_2(c, pan);

		changed = FALSE;
		int note = c->note.cur; // $0BBC
		if (c->note.cycles && c->cur_port_start_ctr == 0) {
			note += sub_cycle_calc(st, c->note.delta);
			changed = TRUE;
		}
		if (c->cur_vib_range && c->vibrato_start_ctr == c->vibrato_start) {
			int p = c->vibrato_phase + sub_cycle_calc(st, c->vibrato_speed);
			note += calc_vib_disp(c, p);
			changed = TRUE;
		}
		if (changed) calc_freq(c, note);
	}
}

BOOL do_timer() {
	state.cycle_timer += state.tempo.cur >> 8;
	if (state.cycle_timer >= 256) {
		state.cycle_timer -= 256;
		while (!do_cycle(&state)) {
			load_pattern();
			if (!is_playing()) return FALSE;
			load_pattern_into_tracker();
		}
	} else {
		do_sub_cycle(&state);
	}
	return TRUE;
}

void initialize_state() {
	memset(&state, 0, sizeof(state));
	int i;
	for (i = 0; i < 8; i++) {
		state.chan[i].volume.cur = 0xFF00;
		state.chan[i].panning.cur = 0x0A00;
		state.chan[i].samp_pos = -1;
		set_inst(&state, &state.chan[i], 0);
	}
	state.volume.cur = 0xC000;
	state.tempo.cur = 0x2000;
	state.cycle_timer = 255;

	state.ordnum = -1;
	if (cur_song.order_length) {
		load_pattern();
	} else {
		pattop_state = state;
		stop_playing();
		EnableMenuItem(hmenu, ID_PLAY, MF_ENABLED);
	}
}
