#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ebmusv2.h"

char *bgm_title[NUM_SONGS];
BOOL metadata_changed;
static char md_filename[MAX_PATH+8];
FILE *orig_rom;
char *orig_rom_filename;

const char *const bgm_orig_title[NUM_SONGS] = {
	"Gas Station (Part 1, Changes cause SPC stall?)",
	"Naming screen",
	"File Select screen",
	"None",
	"You Win! (Version 1)",
	"Level Up",
	"You Lose",
	"Battle Swirl (Boss)",
	"Battle Swirl (Ambushed)",
	"(Unused)",
	"Fanfare",
	"You Win! (Version 2)",
	"Teleport, Departing",
	"Teleport, Failure",
	"Falling Underground",
	"Doctor Andonuts' Lab",
	"Monotoli Building",
	"Sloppy House",
	"Neighbor's House",
	"Arcade",
	"Pokey's House",
	"Hospital",
	"Ness' House (Pollyanna)",
	"Paula's Theme",
	"Chaos Theater",
	"Hotel",
	"Good Morning, Eagleland",
	"Department Store",
	"Onett at Night (Version 1)",
	"Your Sanctuary (Pre-recording)",
	"Your Sanctuary (Post-recording)",
	"Giant Step Melody",
	"Lilliput Steps Melody",
	"Milky Well Melody",
	"Rainy Circle Melody",
	"Magnet Hill Melody",
	"Pink Cloud Melody",
	"Lumine Hall Melody",
	"Fire Spring Melody",
	"Near a Boss",
	"Alien Investigation (Stonehenge Base)",
	"Fire Springs",
	"Belch's Base",
	"Zombie Threed",
	"Spooky Cave",
	"Onett",
	"Fourside",
	"Saturn Valley",
	"Monkey Caves",
	"Moonside",
	"Dusty Dunes Desert",
	"Peaceful Rest Valley",
	"Happy Happy Village", // was "Zombie Threed"
	"Winters",
	"Cave Near a Boss",
	"Summers",
	"Jackie's Cafe",
	"Sailing to Scaraba (Part 1)",
	"Dalaam",
	"Mu Training",
	"Bazaar",
	"Scaraba Desert",
	"Pyramid",
	"Deep Darkness",
	"Tenda Village",
	"Welcome Home (Magicant Part 1)",
	"Dark Side of One's Mind (Magicant Part 2)",
	"Lost Underworld",
	"First Step Back (Cave of the Past)",
	"Second Step Back (Ten Years Ago)",
	"The Place",
	"Giygas Awakens",
	"Giygas Phase 2",
	"Giygas is Weakened",
	"Giygas' Death",
	"Runaway Five Concert (1)",
	"Runaway Five Tour Bus",
	"Runaway Five Concert (2)",
	"Power (Level Up at the Sea of Eden)",
	"Venus' Concert",
	"Yellow Submarine",
	"Bicycle",
	"Sky Runner",
	"Sky Runner, Falling",
	"Bulldozer",
	"Tessie",
	"City Bus",
	"Fuzzy Pickles",
	"Delivery",
	"Return to your Body",
	"Phase Distorter III",
	"Coffee Break",
	"Because I Love You",
	"Good Friends, Bad Friends",
	"Smiles and Tears",
	"Battle versus Cranky Lady",
	"Battle versus Spinning Robo",
	"Battle versus Strutting Evil Mushroom",
	"Battle versus Master Belch",
	"Battle versus New Age Retro Hippie",
	"Battle versus Runaway Dog",
	"Battle versus Cave Boy",
	"Battle versus Your Sanctuary Boss",
	"Battle versus Kraken",
	"Giygas (The Devil's Machine)",
	"Inside the Dungeon",
	"Megaton Walk",
	"The Sea of Eden (Magicant Part 3)",
	"Explosion?",
	"Sky Runner Crash",
	"Magic Cake",
	"Pokey's House (Buzz Buzz present)",
	"Buzz Buzz Swatted",
	"Onett at Night (Version 2, Buzz Buzz present)",
	"Phone Call",
	"Annoying Knock (Right)",
	"Rabbit Cave",
	"Onett at Night (Version 3, Buzzy appears; fade into 0x77)",
	"Apple of Enlightenment",
	"Hotel of the Living Dead",
	"Onett Intro",
	"Sunrise, Onett",
	"New Party Member",
	"Enter Starman Junior",
	"Snow Wood",
	"Phase Distorter (Failed Attempt)",
	"Phase Distorter II (Teleport to Lost Underworld)",
	"Boy Meets Girl (Twoson)",
	"Happy Threed",
	"Runaway Five are Freed",
	"Flying Man",
	"Cavern Theme (\"Onett at Night Version 2\")",
	"Hidden Song (\"Underground\" Track from Mother)",
	"Greeting the Sanctuary Boss",
	"Teleport, Arriving",
	"Saturn Valley Cave",
	"Elevator, Going Down",
	"Elevator, Going Up",
	"Elevator, Stopping",
	"Topolla Theater",
	"Battle versus Master Barf",
	"Teleporting to Magicant",
	"Leaving Magicant",
	"Sailing to Scaraba (Part 2)",
	"Stonehenge Shutdown",
	"Tessie Sighting",
	"Meteor Fall",
	"Battle versus Starman Junior",
	"Runaway Five defeat Clumsy Robot",
	"Annoying Knock (Left)",
	"Onett After Meteor",
	"Ness' House After Meteor",
	"Pokey's Theme",
	"Onett at Night (Version 4, Buzz Buzz present)",
	"Greeting the Sanctuary Boss (2?)",
	"Meteor Strike (Fade into 0x98)",
	"Attract Mode (Opening Credits)",
	"Are You Sure?  Yep!",
	"Peaceful Rest Valley",
	"Sound Stone's Giant Step Recording",
	"Sound Stone's Lilliput Steps Recording",
	"Sound Stone's Milky Well Recording",
	"Sound Stone's Rainy Circle Recording",
	"Sound Stone's Magnet Hill Recording",
	"Sound Stone's Pink Cloud Recording",
	"Sound Stone's Lumine Hall Recording",
	"Sound Stone's Fire Spring Recording",
	"Sound Stone Background Noise",
	"Eight Melodies",
	"Dalaam Intro",
	"Winters Intro",
	"Pokey Escapes",
	"Good Morning, Moonside",
	"Gas Station (Part 2)",
	"Title Screen",
	"Battle Swirl (Normal)",
	"Pokey Springs Into Action",
	"Good Morning, Scaraba",
	"Robotomy",
	"Helicopter Warming Up",
	"The War Is Over",
	"Giygas Static",
	"Instant Victory",
	"You Win! (Version 3, versus Boss)",
	"Giygas Phase 3",
	"Giygas Phase 1",
	"Give Us Strength",
	"Good Morning, Winters",
	"Sound Stone Background Noise",
	"Giygas Dying",
	"Giygas Weakened",
};

BOOL open_orig_rom(char *filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		MessageBox2(strerror(errno), filename, MB_ICONEXCLAMATION);
		return FALSE;
	}
	if (_filelength(_fileno(f)) != rom_size) {
		MessageBox2("File is not same size as current ROM", filename, MB_ICONEXCLAMATION);
		fclose(f);
		return FALSE;
	}
	if (orig_rom) fclose(orig_rom);
	orig_rom = f;
	free(orig_rom_filename);
	orig_rom_filename = _strdup(filename);
	return TRUE;
}

void load_metadata() {
	for (int i = 0; i < NUM_SONGS; i++)
		bgm_title[i] = (char *)bgm_orig_title[i];
	metadata_changed = FALSE;
	
	// We want an absolute path here, so we don't get screwed by
	// GetOpenFileName's current-directory shenanigans when we update.
	char *lastpart;
	GetFullPathName(rom_filename, MAX_PATH, md_filename, &lastpart);
	char *ext = strrchr(lastpart, '.');
	if (!ext) ext = lastpart + strlen(lastpart);
	strcpy(ext, ".ebmused");
	
	FILE *mf = fopen(md_filename, "r");
	if (!mf) return;
	
	int c;
	while ((c = fgetc(mf)) >= 0) {
		char buf[MAX_PATH];
#if MAX_TITLE_LEN >= MAX_PATH
#error
#endif
		if (c == 'O') {
			fgetc(mf);
			fgets(buf, MAX_PATH, mf);
			{ char *p = strchr(buf, '\n'); if (p) *p = '\0'; }
			open_orig_rom(buf);
		} else if (c == 'R') {
			int start, end;
			fscanf(mf, "%X %X", &start, &end);
			change_range(start, end, AREA_NON_SPC, AREA_FREE);
			while ((c = fgetc(mf)) >= 0 && c != '\n');
		} else if (c == 'T') {
			unsigned int bgm;
			fscanf(mf, "%X %" MAX_TITLE_LEN_STR "[^\n]", &bgm, buf);
			if (--bgm < NUM_SONGS)
				bgm_title[bgm] = _strdup(buf);
			while ((c = fgetc(mf)) >= 0 && c != '\n');
		} else {
			printf("unrecognized metadata line %c\n", c);
		}
	}
	fclose(mf);
}

void save_metadata() {
	if (!metadata_changed) return;
	FILE *mf = fopen(md_filename, "w");
	if (!mf) {
		MessageBox2(strerror(errno), md_filename, MB_ICONEXCLAMATION);
		return;
	}
	
	if (orig_rom_filename)
		fprintf(mf, "O %s\n", orig_rom_filename);
	
	// SPC ranges containing at least one free area
	for (int i = 0; i < area_count; i++) {
		int start = areas[i].address;
		int has_free = 0;
		for (; areas[i].pack >= AREA_FREE; i++)
			has_free |= areas[i].pack == AREA_FREE;
		if (has_free)
			fprintf(mf, "R %06X %06X\n", start, areas[i].address);
	}

	for (int i = 0; i < NUM_SONGS; i++)
		if (strcmp(bgm_title[i], bgm_orig_title[i]) != 0)
			fprintf(mf, "T %02X %s\n", i+1, bgm_title[i]);

	int size = ftell(mf);
	fclose(mf);
	if (size == 0) remove(md_filename);
	metadata_changed = FALSE;
}

void free_metadata() {
	if (orig_rom) { fclose(orig_rom); orig_rom = NULL; }
	free(orig_rom_filename);
	orig_rom_filename = NULL;
	for (int i = 0; i < NUM_SONGS; i++)
		if (bgm_title[i] != bgm_orig_title[i])
			free(bgm_title[i]);
}
