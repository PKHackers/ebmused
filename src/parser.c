#include "ebmusv2.h"

// number of bytes following a Ex/Fx code
const BYTE code_length[] = {
	1, 1, 2, 3, 0, 1, 2, 1, 2, 1, 1, 3, 0, 1, 2, 3,
	1, 3, 3, 0, 1, 3, 0, 3, 3, 3, 1, 2, 0, 0, 0, 0
};

void parser_init(struct parser *p, const struct channel_state *c) {
	p->ptr = c->ptr;
	p->sub_start = c->sub_start;
	p->sub_ret = c->sub_ret;
	p->sub_count = c->sub_count;
	p->note_len = c->note_len;
}

BYTE *next_code(BYTE *p) {
	BYTE chr = *p++;
	if (chr < 0x80)
		p += *p < 0x80;
	else if (chr >= 0xE0)
		p += code_length[chr - 0xE0];
	return p;
}

BOOL parser_advance(struct parser *p) {
	int chr = *p->ptr;
	if (chr == 0) {
		if (p->sub_count == 0) return FALSE;
		p->ptr = --p->sub_count ? cur_song.sub[p->sub_start].track : p->sub_ret;
	} else if (chr == 0xEF) {
		p->sub_ret = p->ptr + 4;
		p->sub_start = *(WORD *)&p->ptr[1];
		p->sub_count = p->ptr[3];
		p->ptr = cur_song.sub[p->sub_start].track;
	} else {
		if (chr < 0x80)
			p->note_len = chr;
		p->ptr = next_code(p->ptr);
	}
	return TRUE;
}
