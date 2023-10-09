#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ebmusv2.h"
#include "misc.h"

const DWORD pack_orig_crc[] = {
	0x35994B97, 0xDB04D065, 0xC13D8165, 0xEEFF028E, 0x5330392D, 0x705AEBBC,
	0x4ED3BBAB, 0xFF11F6A1, 0x9E69B6C1, 0xBF0F580B, 0x0460DAD8, 0xD3EEC6FB,
	0x082C8FC1, 0x5B81C947, 0xE157E6C2, 0x641EB570, 0x79C6A5D2, 0xFE892ACA,
	0xEE4C1723, 0x947F5985, 0x20822EF9, 0xCF193A5F, 0x311520DA, 0x10765295,
	0xAA43B31F, 0xE72085EC, 0x324821DE, 0x73054B6A, 0x0AE457C7, 0xB9D0E9D4,
	0xC93C3678, 0x3DFED2B6, 0xDEB68F1C, 0x8C62B8B6, 0x7F35744F, 0x8D3E7AF9,
	0xFCAF31CD, 0x827F8B23, 0x347E2419, 0x9945AE89, 0x83245B62, 0x6432A069,
	0x58290E16, 0xCED6200A, 0x1424E797, 0x802E483A, 0x53583F0B, 0x1FB73242,
	0x9BA15381, 0x83BFCDBD, 0x07E9A480, 0x105074C2, 0xD90BBBC1, 0xE9E68007,
	0x9E7DAAC1, 0xEEECB692, 0x3B4F935F, 0x6B9CB808, 0x2625C94A, 0xA9210DC4,
	0x8EF74F19, 0x3EF02201, 0x7B8B59E9, 0xAA163725, 0x849B7F34, 0xE15EF409,
	0xC2774561, 0x8898F96B, 0x87344C8F, 0xCFF94FCF, 0x58907350, 0x7269B3F8,
	0x66C0991C, 0x4992871D, 0xB72E486D, 0xBF0BE12F, 0xA3509B6F, 0xBEF628D0,
	0x9452EC07, 0x032D37F3, 0x559DDB52, 0x7429ACCE, 0xF3FF8749, 0x6DDC7BB8,
	0x5BDA587E, 0x95B863A7, 0x35BD7758, 0x733A7B93, 0xFD61E984, 0x93B4834F,
	0xF446B13F, 0x1D3426C1, 0x9BA7D579, 0x2DB7314D, 0x2630298F, 0xB432655A,
	0xE2E071F4, 0x8B393217, 0x51033BB3, 0x1619C100, 0x8EB3F2FC, 0x82207885,
	0xEB4767C6, 0x6CDE8654, 0x61DB258E, 0x2DBA2FEF, 0x19F45E6D, 0xF90F25A4,
	0xDDE08443, 0xD187DFCB, 0x0630027E, 0xCEFFC22B, 0xF5F39A6D, 0x88C82FBA,
	0xF3B86811, 0x005EEE83, 0xBADE3AC4, 0xBE11ECEA, 0x2EA452C2, 0x7C09903E,
	0xD3055E99, 0x184714DA, 0x8C3E615A, 0x4FB3F125, 0x6BC1A993, 0x58BDDFE4,
	0x8E8ED38B, 0x10637EEB, 0xC654BDFA, 0x6AB0C2F7, 0xDFFF0971, 0x9855C03D,
	0xA36E5FD6, 0xF6A72D30, 0x80AAE5AD, 0x1195ED2F, 0x87A0336E, 0x824F38DB,
	0x2458ADC6, 0xCCD4AC63, 0xAB6C84DB, 0x90DFCA16, 0x55F1C184, 0x2BFFE745,
	0xE5F96BF9, 0x9BE7C8D6, 0x0F5DADC7, 0x02BEA184, 0x66CC6C71, 0x8100B1C5,
	0x2E894645, 0xF487A0B5, 0x60EDA440, 0x4CBA4829, 0xFD5F55ED, 0x37C5DEA7,
	0x664D83E7, 0x135D3B35, 0x7ED32ACE, 0x2D23FA7E, 0x5B969EA6, 0xDC7A49AD,
	0xEE1071E0, 0x28E8DB77, 0x02E1409C, 0x0665F2E2, 0xE01946DF, 0xB6E7A174,
	0xD07DBF27,
};

void free_pack(struct pack *p) {
	for (int i = 0; i < p->block_count; i++)
		free(p->blocks[i].data);
	free(p->blocks);
	p->status = 0;
}

struct pack *load_pack(int pack) {
	struct pack *mp = &inmem_packs[pack];
	if (!(mp->status & IPACK_INMEM)) {
		struct pack *rp = &rom_packs[pack];
		mp->start_address = rp->start_address;
		mp->block_count = rp->block_count;
		mp->blocks = memcpy(malloc(mp->block_count * sizeof(struct block)),
			rp->blocks, mp->block_count * sizeof(struct block));
		struct block *b = mp->blocks;
		fseek(rom, mp->start_address - 0xC00000 + rom_offset, SEEK_SET);
		for (int i = 0; i < mp->block_count; i++) {
			fseek(rom, 4, SEEK_CUR);
			b->data = malloc(b->size);
			fread(b->data, b->size, 1, rom);
			b++;
		}
		mp->status |= IPACK_INMEM;
	}

	return mp;
}

// Changes the current song pack.
// This should always be followed by a call to select_block()
void load_songpack(int new_pack) {
	if (packs_loaded[2] == new_pack)
		return;

	// Unload the current songpack unless it has been changed
	if (packs_loaded[2] < NUM_PACKS) {
		struct pack *old = &inmem_packs[packs_loaded[2]];
		if (!(old->status & IPACK_CHANGED))
			free_pack(old);
	}

	packs_loaded[2] = new_pack;
	if (new_pack >= NUM_PACKS)
		return;

	load_pack(new_pack);
}

struct block *get_cur_block() {
	if (packs_loaded[2] < NUM_PACKS) {
		struct pack *p = &inmem_packs[packs_loaded[2]];
		if (current_block >= 0 && current_block < p->block_count)
			return &p->blocks[current_block];
	}
	return NULL;
}

void select_block(int block) {
	current_block = block;

	free_song(&cur_song);

	struct block *b = get_cur_block();
	if (b != NULL) {
		memcpy(&spc[b->spc_address], b->data, b->size);
		decompile_song(&cur_song, b->spc_address, b->spc_address + b->size);
	}
	initialize_state();
}

void select_block_by_address(int spc_addr) {
	int bnum = -1;
	if (packs_loaded[2] < NUM_PACKS) {
		struct pack *p = &inmem_packs[packs_loaded[2]];
		for (bnum = p->block_count - 1; bnum >= 0; bnum--) {
			struct block *b = &p->blocks[bnum];
			if ((unsigned)(spc_addr - b->spc_address) < b->size) break;
		}
	}
	select_block(bnum);
}

struct block *save_cur_song_to_pack() {
	struct block *b = get_cur_block();
	if (b && cur_song.changed) {
		int size = compile_song(&cur_song);
		b->size = size;
		b->spc_address = cur_song.address;
		free(b->data);
		b->data = memcpy(malloc(size), &spc[cur_song.address], size);
		inmem_packs[packs_loaded[2]].status |= IPACK_CHANGED;
		cur_song.changed = FALSE;
	}
	return b;
}

int calc_pack_size(struct pack *p) {
	int size = 2;
	for (int i = 0; i < p->block_count; i++)
		size += 4 + p->blocks[i].size;
	return size;
}

// Adds a new block to the current pack
void new_block(struct block *b) {
	struct pack *p = &inmem_packs[packs_loaded[2]];
	int pos = 0;
	while (pos < p->block_count && p->blocks[pos].spc_address <= b->spc_address)
		pos++;

	struct block *newb = array_insert(&p->blocks, &p->block_count, sizeof(struct block), pos);
	*newb = *b;
	p->status |= IPACK_CHANGED;
	select_block(pos);
}

void delete_block(int block) {
	struct pack *p = &inmem_packs[packs_loaded[2]];
	select_block(-1);
	memmove(&p->blocks[block], &p->blocks[block+1], (--p->block_count - block) * sizeof(struct block));
	p->status |= IPACK_CHANGED;
}

// Moves the current block to a different location within the pack
void move_block(int to) {
	struct pack *p = &inmem_packs[packs_loaded[2]];
	int from = current_block;
	struct block b = p->blocks[from];
	if (to > from) {
		memmove(&p->blocks[from], &p->blocks[from + 1], (to - from) * sizeof(struct block));
	} else {
		memmove(&p->blocks[to + 1], &p->blocks[to], (from - to) * sizeof(struct block));
	}
	p->blocks[to] = b;
	current_block = to;
	p->status |= IPACK_CHANGED;
}

BOOL save_pack(int pack) {
	static char error[512];
	struct pack *p = &inmem_packs[pack];
	struct pack *rp = &rom_packs[pack];
	if (!(p->status & IPACK_CHANGED))
		return FALSE;

	if (!orig_rom) {
		MessageBox2("Before saving a pack, the original ROM file needs to be specified so that it can be used to ensure that no unused remnants of previous versions of the pack are left in the file in such a way that they would increase the patch size.", "Save", 48);
		return FALSE;
	}
	int size = calc_pack_size(p);
	int conflict = check_range(p->start_address, p->start_address + size, pack);
	if (conflict != AREA_FREE) {
		char *p = error;
		p += sprintf(p, "Pack %02X could not be saved:\n", pack);
		if (conflict == AREA_NOT_IN_FILE)
			strcpy(p, "The ROM address is invalid");
		else if (conflict == AREA_NON_SPC)
			strcpy(p, "The ROM address is not in a range designated for SPC data");
		else
			sprintf(p, "Would overlap with pack %02X", conflict);
		MessageBox2(error, "Save", 48);
		return FALSE;
	}

	int old_start = rp->start_address;
	int old_size = calc_pack_size(rp);
	BYTE *filler = malloc(old_size);

	fseek(orig_rom, old_start - 0xC00000 + orig_rom_offset, SEEK_SET);
	if (!fread(filler, old_size, 1, orig_rom)) {
error:
		MessageBox2(strerror(errno), "Save", 16);
		return FALSE;
	}
	fseek(rom, old_start - 0xC00000 + rom_offset, SEEK_SET);
	if (!fwrite(filler, old_size, 1, rom))
		goto error;
	free(filler);

	fseek(rom, PACK_POINTER_TABLE + rom_offset + 3*pack, SEEK_SET);
	fputc(p->start_address >> 16, rom);
	fputc(p->start_address, rom);
	fputc(p->start_address >> 8, rom);

	fseek(rom, p->start_address - 0xC00000 + rom_offset, SEEK_SET);
	for (int i = 0; i < p->block_count; i++) {
		struct block *b = &p->blocks[i];
		if (!fwrite(b, 4, 1, rom))
			goto error;
		if (!fwrite(b->data, b->size, 1, rom))
			goto error;
	}
	if (!fwrite("\0", 2, 1, rom))
		goto error;
	fflush(rom);

	p->status &= ~IPACK_CHANGED;

	rp->start_address = p->start_address;
	rp->status = RPACK_SAVED;
	rp->block_count = p->block_count;
	free(rp->blocks);
	rp->blocks = memcpy(malloc(sizeof(struct block) * p->block_count),
		p->blocks, sizeof(struct block) * p->block_count);

	change_range(old_start, old_start + old_size, pack, AREA_FREE);
	change_range(p->start_address, p->start_address + size, AREA_FREE, pack);

	if (pack != packs_loaded[2])
		free_pack(p);

	// an SPC range that previously had no free blocks might have some now
	metadata_changed = TRUE;

	return TRUE;
}
