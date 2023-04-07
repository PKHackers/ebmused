SRCS = bgmlist.c brr.c ctrltbl.c help.c inst.c loadrom.c main.c metadata.c midi.c misc.c packlist.c packs.c parser.c play.c ranges.c resource.c song.c songed.c sound.c text.c tracker.c status.c

BUILD_DIR = ../build
RELEASE_DIR = $(BUILD_DIR)/release
DEBUG_DIR = $(BUILD_DIR)/debug
OBJ_DIR = obj
DEBUG_O_DIR = $(DEBUG_DIR)/$(OBJ_DIR)
RELEASE_O_DIR = $(RELEASE_DIR)/$(OBJ_DIR)

O = $(SRCS:%.c=%.o)
RELEASE_O = $(addprefix $(RELEASE_O_DIR)/, $O)
DEBUG_O = $(addprefix $(DEBUG_O_DIR)/, $O)

CC = gcc -W -Wall -Werror=vla -std=gnu99 -fno-exceptions

RELEASE_FLAGS = -DNDEBUG -Os
DEBUG_FLAGS = -Og -g
LINK_FLAGS = -lcomctl32 -lcomdlg32 -lgdi32 -lwinmm

release: $(RELEASE_DIR)/ebmused.exe
debug: $(DEBUG_DIR)/ebmused.exe

$(RELEASE_DIR)/ebmused.exe: $(RELEASE_O)
	$(CC) $(RELEASE_O) -static-libgcc -mwindows -s $(LINK_FLAGS) -o $@

$(RELEASE_O_DIR)/%.o: %.c | $(RELEASE_O_DIR)
	$(CC) $(RELEASE_FLAGS) -c $< -o $@

$(DEBUG_DIR)/ebmused.exe: $(DEBUG_O)
	$(CC) $(DEBUG_O) $(LINK_FLAGS) -o $@

$(DEBUG_O_DIR)/%.o: %.c | $(DEBUG_O_DIR)
	$(CC) $(DEBUG_FLAGS) -c $< -o $@

%/$(OBJ_DIR)/resource.o: resource.rc
	windres $< -o $@

%/$(OBJ_DIR):
	mkdir $(subst /,\,$(BUILD_DIR))
	mkdir $(subst /,\,$(DEBUG_DIR))
	mkdir $(subst /,\,$(DEBUG_O_DIR))
	mkdir $(subst /,\,$(RELEASE_DIR))
	mkdir $(subst /,\,$(RELEASE_O_DIR))
