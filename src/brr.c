#include <stdio.h>
#include <stdlib.h>
#include "ebmusv2.h"

struct sample samp[128];

void decode_samples(WORD *ptrtable) {
	for (int sn = 0; sn < 128; sn++) {
		struct sample *sa = &samp[sn];
		int start = *ptrtable++;
		int loop  = *ptrtable++;

		sa->data = NULL;
		if (start == 0 || start == 0xffff)
			continue;

		int end = start;
		int b;
		do {
			b = spc[end];
			end += 9;
		} while ((b & 1) == 0);

		sa->length = ((end - start) / 9) * 16;
		if (b & 2) { // The LOOP bit only matters for the last brr block
			if (loop < start || loop >= end)
				continue;
			sa->loop_len = ((end - loop) / 9) * 16;
		} else
			sa->loop_len = 0;

		short *p = malloc(2 * (sa->length + 1));
/*		printf("Sample %2d: %04X(%04X)-%04X length %d looplen %d\n",
			sn, start, loop, end, sa->length, sa->loop_len);*/

		sa->data = p;
		for (int pos = start; pos < end; pos += 9) {
			int range = spc[pos] >> 4;
			int filter = (spc[pos] >> 2) & 3;
			for (int i = 2; i < 18; i++) {
				int s = spc[pos + (i >> 1)];
				if (i & 1)
					s &= 15;
				else
					s >>= 4;

				if (s >= 8) s -= 16;

				s <<= range;
				if (range > 12) {
					if (s < 0) s = -4096;
					else       s = 0;
				}

				if (filter) {
					switch (filter) {
						case 1: s += p[-1] * 15 >> 4; break;
						case 2: s += (p[-1] * 61 >> 5) - (p[-2] * 15 >> 4); break;
						case 3: s += (p[-1] * 115 >> 6) - (p[-2] * 13 >> 4); break;
					}
					// Clamp to [-65536, 65534] and then have it wrap around at
					// [-32768, 32767]
					if (s < -65536) s = (-65536 + 65536);
					else if (s > 65534) s = (65534 - 65536);
					else if (s < -32768) s += 65536;
					else if (s > 32767) s -= 65536;
				}

				*p++ = s;
			}
		}

		// Put an extra sample at the end for easier interpolation
		*p = sa->loop_len ? sa->data[sa->length - sa->loop_len] : 0;
	}
}

void free_samples() {
	for (int sn = 0; sn < 128; sn++) {
		free(samp[sn].data);
		samp[sn].data = NULL;
	}
}
