#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ebmusv2.h"

const unsigned int BRR_BLOCK_SIZE = 9;

enum {
	BRR_FLAG_END = 1,
	BRR_FLAG_LOOP = 2
};

struct sample samp[128];
WORD sample_ptr_base = 0x6C00;

// Counts and returns the number of BRR blocks from a specific location in memory.
// This makes no attempt to simulate the behavior of the SPC on key on. It ignores the header of a
// block on key on. That would complicate decoding, because you could have a loop that results in a
// sample that ends, or that has a second loop point, and... no one does that. Right?
// The count should never be greater than 7281 since that would take more memory than the SPC has.
unsigned int count_brr_blocks(const uint8_t *spc_memory, uint16_t start) {
	unsigned int count = 0;
	uint8_t b = 0;
	// Count blocks until one has the end flag or there's not enough space for another to be in RAM.
	while (!(b & BRR_FLAG_END) && start + (count + 1) * BRR_BLOCK_SIZE <= 0xFFFF) {
		b = spc_memory[start + count*BRR_BLOCK_SIZE];
		count++;
	}

	// Should we return 0 if we reached the end of RAM and the last block doesn't have the end flag?
	return count;
}

static void decode_brr_block(int16_t *buffer, const uint8_t *block, BOOL first_block) {
	int range = block[0] >> 4;
	int filter = (block[0] >> 2) & 3;

	if (first_block) {
		// According to SPC_DSP, the header is ignored on key on.
		// Not enforcing this could result in a read out of bounds, if the filter is nonzero.
		range = 0;
		filter = 0;
	}

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

static int get_full_loop_len(const struct sample *sa, const int16_t *next_block, int first_loop_start) {
	int loop_start = sa->length - sa->loop_len;
	int no_match_found = TRUE;
	while (loop_start >= first_loop_start && no_match_found) {
		// If the first two samples in a loop are the same, the rest all will be too.
		// BRR filters can rely on, at most, two previous samples.
		if (sa->data[loop_start] == next_block[0] &&
				sa->data[loop_start + 1] == next_block[1]) {
			no_match_found = FALSE;
		} else {
			loop_start -= sa->loop_len;
		}
	}

	if (loop_start >= first_loop_start)
		return sa->length - loop_start;
	else
		return -1;
}

void decode_samples(const unsigned char *ptrtable) {
	for (unsigned sn = 0; sn < 128; sn++) {
		struct sample *sa = &samp[sn];
		uint16_t start = ptrtable[0] | (ptrtable[1] << 8);
		uint16_t loop  = ptrtable[2] | (ptrtable[3] << 8);
		ptrtable += 4;

		sa->data = NULL;
		if (start == 0 || start == 0xffff)
			continue;

		unsigned int num_blocks = count_brr_blocks(spc, start);
		if (num_blocks == 0)
			continue;

		uint16_t end = start + num_blocks * BRR_BLOCK_SIZE;
		sa->length = num_blocks * 16;
		// The LOOP bit only matters for the last brr block
		if (spc[start + (num_blocks - 1) * BRR_BLOCK_SIZE] & BRR_FLAG_LOOP) {
			if (loop < start || loop >= end || (loop - start) % BRR_BLOCK_SIZE)
				continue;
			sa->loop_len = ((end - loop) / BRR_BLOCK_SIZE) * 16;
		} else
			sa->loop_len = 0;

		size_t allocation_size = sizeof(int16_t) * (sa->length + 1);

		int16_t *p = malloc(allocation_size);
		if (!p) {
			printf("malloc failed in BRR decoding (sn: %02X)\n", sn);
			continue;
		}

		sa->data = p;

		int needs_another_loop;
		int first_block = TRUE;
		int decoding_start = 0; // Index of BRR where decoding begins.
		int times = 0;

		do {
			needs_another_loop = FALSE;

			for (int i = decoding_start; i < num_blocks; i++) {
				decode_brr_block(p, &spc[start + i*BRR_BLOCK_SIZE], first_block);
				p += 16;
				first_block = FALSE;
			}

			if (sa->loop_len != 0) {
				decoding_start = (loop - start) / BRR_BLOCK_SIZE; // Start decoding from "loop" BRR block.

				int16_t after_loop[18];
				after_loop[0] = p[-2];
				after_loop[1] = p[-1];

				decode_brr_block(&after_loop[2], &spc[loop], FALSE);
				int full_loop_len = get_full_loop_len(sa, &after_loop[2], (loop - start) / BRR_BLOCK_SIZE * 16);

				if (full_loop_len == -1) {
					needs_another_loop = TRUE;
					ptrdiff_t diff = p - sa->data;
					int16_t *new_stuff = realloc(sa->data, (sa->length + sa->loop_len + 1) * sizeof(int16_t));
					if (new_stuff == NULL) {
						printf("realloc failed in BRR decoding (sn: %02X)\n", sn);
						// TODO What do we do now? Replace this with something better
						needs_another_loop = FALSE;
						break;
					}
					p = new_stuff + diff;
					sa->length += sa->loop_len;
					sa->data = new_stuff;
				} else {
					sa->loop_len = full_loop_len;
					// needs_another_loop is already false
				}
			}

			// In the vanilla game, the most iterations needed is 48 (for sample 0x17 in pack 5).
			// Most samples need less than 10.
			++times;
		} while (needs_another_loop && times < 64);

		if (needs_another_loop) {
			printf("Sample %02X took too many iterations to get into a cycle\n", sn);
		}

		// Put an extra sample at the end for easier interpolation
		*p = sa->loop_len != 0 ? sa->data[sa->length - sa->loop_len] : 0;
	}
}

void free_samples(void) {
	for (int sn = 0; sn < 128; sn++) {
		free(samp[sn].data);
		samp[sn].data = NULL;
	}
}
