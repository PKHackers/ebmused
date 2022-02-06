#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ebmusv2.h"

enum {
	BRR_BLOCK_SIZE = 9,

	BRR_FLAG_END = 1,
	BRR_FLAG_LOOP = 2
};

struct sample samp[128];

// Returns the length of a BRR sample, in bytes
static int32_t sample_length(const uint8_t *spc_memory, uint16_t start) {
	int32_t end = start;
	uint8_t b;
	do {
		b = spc_memory[end];
		end += BRR_BLOCK_SIZE;
	} while ((b & BRR_FLAG_END) == 0 && end < 0x10000 - 9);

	if (end < 0x10000 - 9)
		return end - start;
	else
		return -1;
}

static void decode_brr_block(int16_t *buffer, const uint8_t *block) {
	int range = block[0] >> 4;
	int filter = (block[0] >> 2) & 3;

	for (int i = 2; i < 18; i++) {
		int32_t s = block[i / 2];

		if (i % 2 == 0) {
			s >>= 4;
		} else {
			s &= 0x0F;
		}

		if (s >= 8) {
			s -= 16;
		}

		s <<= range - 1;
		if (range > 12) {
			s = (s < 0) ? -(1 << 11) : 0;
		}

		switch (filter) {
			case 1: s += (buffer[-1] * 15) >> 5; break;
			case 2: s += ((buffer[-1] * 61) >> 6) - ((buffer[-2] * 15) >> 5); break;
			case 3: s += ((buffer[-1] * 115) >> 7) - ((buffer[-2] * 13) >> 5); break;
		}

		s *= 2;

		// Clamp to [-65536, 65534] and then have it wrap around at
		// [-32768, 32767]
		if (s < -0x10000) s = (-0x10000 + 0x10000);
		else if (s > 0xFFFE) s = (0xFFFE - 0x10000);
		else if (s < -0x8000) s += 0x10000;
		else if (s > 0x7FFF) s -= 0x10000;

		*buffer++ = s;
	}
}

void decode_samples(const WORD *ptrtable) {
	for (int sn = 0; sn < 128; sn++) {
		struct sample *sa = &samp[sn];
		int start = *ptrtable++;
		int loop  = *ptrtable++;

		sa->data = NULL;
		if (start == 0 || start == 0xffff)
			continue;

		int length = sample_length(spc, start);
		if (length == -1)
			continue;

		int end = start + length;
		sa->length = (length / BRR_BLOCK_SIZE) * 16;
		// The LOOP bit only matters for the last brr block
		if (spc[start + length - BRR_BLOCK_SIZE] & BRR_FLAG_LOOP) {
			if (loop < start || loop >= end)
				continue;
			sa->loop_len = ((end - loop) / BRR_BLOCK_SIZE) * 16;
		} else
			sa->loop_len = 0;

		size_t allocation_size = sizeof(int16_t) * (2 + sa->length + 1);

		int16_t *p = malloc(allocation_size);
		if (!p)
			continue;
/*		printf("Sample %2d: %04X(%04X)-%04X length %d looplen %d\n",
			sn, start, loop, end, sa->length, sa->loop_len);*/

		// A custom sample might try to rely on past data that doesn't exist, so provide some.
		p[0] = 0;
		p[1] = 0;
		sa->data = &p[2];
		p = &p[2];

		int needs_another_loop = FALSE;

		do {
			for (int pos = start; pos < end; pos += BRR_BLOCK_SIZE) {
				decode_brr_block(p, &spc[pos]);
				p += 16;
			}

			if (sa->loop_len) {
				int16_t after_loop[18] = {0};
				after_loop[0] = p[-2];
				after_loop[1] = p[-1];

				decode_brr_block(&after_loop[2], &spc[loop]);
				needs_another_loop = after_loop[2] != sa->data[sa->length - sa->loop_len] ||
									after_loop[3] != sa->data[sa->length - sa->loop_len + 1];
				if (needs_another_loop) {
					printf("We need another loop! sample %02X\n", (unsigned)sn);
				}
			}
		} while (0 /* needs_another_loop */);

		// Put an extra sample at the end for easier interpolation
		*p = sa->loop_len ? sa->data[sa->length - sa->loop_len] : 0;
	}
}

void free_samples(void) {
	for (int sn = 0; sn < 128; sn++) {
		if (samp[sn].data) {
			// static_assert(sizeof(samp[sn].data[0]) == sizeof(int16_t));
			free(samp[sn].data - 2);
		}
		samp[sn].data = NULL;
	}
}
