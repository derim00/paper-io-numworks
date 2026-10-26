# Paper.io pour NumWorks (EADK, bare-metal)
#   make          -> build/paper.nwa
#   make install  -> installe sur la calculatrice (USB, via nwlink)
#   make clean

CC      := arm-none-eabi-gcc
NWLINK  ?= npx --yes -- nwlink@0.0.19

BUILD   := build
SRC     := src/main.c src/game_core.c
OBJ     := $(SRC:%.c=$(BUILD)/%.o)

# Flags fournis par le SDK (-mthumb -mcpu=cortex-m7 ..., -Wl,--relocatable -nostartfiles)
EADK_CFLAGS  := $(shell $(NWLINK) eadk-cflags-device)
EADK_LDFLAGS := $(shell $(NWLINK) eadk-ldflags-device)

CFLAGS  := -Ieadk $(EADK_CFLAGS) -std=c11 -Os -Wall -Wextra \
           -ffreestanding -fno-builtin \
           -fno-tree-loop-distribute-patterns \
           -ffunction-sections -fdata-sections \
           -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables

# Pas de --gc-sections ici : avec --relocatable il supprime eadk_app_name,
# eadk_api_level et l'icône (jamais référencés), et nwlink ne trouve plus rien.
LDFLAGS := $(EADK_LDFLAGS) -nostdlib \
           -Wl,--undefined=eadk_main

.PHONY: all install check clean
all: $(BUILD)/paper.nwa

$(BUILD)/%.o: %.c eadk/eadk.h src/game_core.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/icon.o: src/icon.png
	@mkdir -p $(BUILD)
	$(NWLINK) png-icon-o $< $@

$(BUILD)/paper.nwa: $(OBJ) $(BUILD)/icon.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

check: $(BUILD)/paper.nwa
	$(NWLINK) nwa-name $<

install: $(BUILD)/paper.nwa
	$(NWLINK) install-nwa $<

clean:
	rm -rf $(BUILD)
