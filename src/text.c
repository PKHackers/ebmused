#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ebmusv2.h"
#include "misc.h"

static int unhex(int chr) {
	if (chr >= '0' && chr <= '9')
		return chr - '0';
	chr |= 0x20; // fold to lower case
	if (chr >= 'a' && chr <= 'f')
		return chr - 'a' + 10;
	return -1;
}

int calc_track_size_from_text(char *p) {
	char buf[60];
	int size = 0;
	while (*p) {
		int c = *p++;
		if (unhex(c) >= 0) {
			if (unhex(*p) >= 0) p++;
			size++;
		} else if (c == '[' || c == ']' || isspace(c)) {
			// nothing
		} else if (c == '*') {
			strtol(p, &p, 10);
			if (*p == ',') strtol(p + 1, &p, 10);
			size += 4;
		} else {
			sprintf(buf, "Bad character: '%c'", c);
			MessageBox2(buf, NULL, 48);
			return -1;
		}
	}
	return size;
}

// returns 1 if successful
BOOL text_to_track(char *str, struct track *t, BOOL is_sub) {
	BYTE *data;
	int size = calc_track_size_from_text(str);
	if (size < 0)
		return FALSE;

	int pos;
	if (size == 0 && !is_sub) {
		data = NULL;
	} else {
		data = malloc(size + 1);
		char *p = str;
		pos = 0;
		while (*p) {
			int c = *p++;
			int h = unhex(c);
			if (h >= 0) {
				int h2 = unhex(*p);
				if (h2 >= 0) { h = h << 4 | h2; p++; }
				data[pos++] = h;
			} else if (c == '*') {
				int sub = strtol(p, &p, 10);
				int count = *p == ',' ? strtol(p + 1, &p, 10) : 1;
				data[pos++] = 0xEF;
				data[pos++] = sub & 0xFF;
				data[pos++] = sub >> 8;
				data[pos++] = count;
			}
		}
		data[pos] = '\0';
	}

	if (!validate_track(data, size, is_sub)) {
		free(data);
		return FALSE;
	}

	if (size != t->size || memcmp(data, t->track, size)) {
		t->size = size;
		free(t->track);
		t->track = data;
	} else {
		free(data);
	}
	return TRUE;
}

// includes ending '\0'
int text_length(BYTE *start, BYTE *end) {
	int textlength = 0;
	for (BYTE *p = start; p < end; ) {
		int byte = *p;
		int len;
		if (byte < 0x80) {
			len = p[1] < 0x80 ? 2 : 1;
			textlength += 3*len + 2;
		} else if (byte < 0xE0) {
			len = 1;
			textlength += 3;
		} else {
			len = 1 + code_length[byte - 0xE0];
			if (byte == 0xEF) {
				char buf[12];
				textlength += sprintf(buf, "*%d,%d ", p[1] | p[2] << 8, p[3]);
			} else {
				textlength += 3*len + 2;
			}
		}
		p += len;
	}
	return textlength;
}

// convert a track to text. size must not be 0
void track_to_text(char *out, BYTE *track, int size) {
	for (int len, pos = 0; pos < size; pos += len) {
		int byte = track[pos];

		len = next_code(&track[pos]) - &track[pos];

		if (byte == 0xEF) {
			int sub = track[pos+1] | track[pos+2] << 8;
			out += sprintf(out, "*%d,%d", sub, track[pos + 3]);
		} else {
			int i;
			if (byte < 0x80 || byte >= 0xE0) *out++ = '[';
			for (i = 0; i < len; i++) {
				int byte = track[pos + i];
				if (i != 0) *out++ = ' ';
				*out++ = "0123456789ABCDEF"[byte >> 4];
				*out++ = "0123456789ABCDEF"[byte & 15];
			}
			if (byte < 0x80 || byte >= 0xE0) *out++ = ']';
		}

		*out++ = ' ';
	}
	out[-1] = '\0';
}
