O = bgmlist.o brr.o ctrltbl.o help.o inst.o loadrom.o main.o metadata.o midi.o misc.o packlist.o packs.o parser.o play.o ranges.o resource.o song.o songed.o sound.o text.o tracker.o
CC = gcc -DNDEBUG -Os -W -Wall -std=gnu99

a.exe: $O
	gcc -mwindows -s -fno-exceptions $O -lcomctl32 -lcomdlg32 -lgdi32 -lwinmm -Wl,ldscript
	
resource.o: resource.rc
	windres $< -o $@
