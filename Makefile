# we override make's built-in default of "cc", which is missing on most Windows toolchains
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -O2 -Wall -Wextra -std=c11
OPENMP_FLAG ?= -fopenmp
LDLIBS_COMMON := -lm

CONSOLE_SRCS := jeu.c main.c
SDL_SRCS := jeu.c ui.c main_sdl.c

ifeq ($(OS),Windows_NT)
# we prefer the SDL2 MinGW dev packages that build_windows.ps1 unpacks into deps/
SDL_DEPS := $(wildcard deps/SDL2*/x86_64-w64-mingw32)
ifneq ($(SDL_DEPS),)
SDL_CFLAGS ?= $(foreach d,$(SDL_DEPS),-I$(d)/include -I$(d)/include/SDL2)
SDL_LIBS ?= $(foreach d,$(SDL_DEPS),-L$(d)/lib) -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_ttf
else
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf 2>NUL)
SDL_LIBS ?= $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf 2>NUL)
endif
# we link the Win32 multimedia/GDI libs and build a GUI-subsystem exe (no console window)
SDL_LIBS += -lwinmm -lgdi32 -mwindows
# Windows gives new threads the exe's default stack (2 MB with MinGW's linker); each search
# node keeps its move list on the stack, and YBW tasks can nest searches on a worker thread
LDLIBS_COMMON += -Wl,--stack,16777216
else
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null)
SDL_LIBS ?= $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf 2>/dev/null)
endif

.PHONY: all console sdl clean help

all: chess_console chess_sdl

console: chess_console

sdl: chess_sdl

chess_console: $(CONSOLE_SRCS)
	$(CC) $(CFLAGS) $(OPENMP_FLAG) $(CONSOLE_SRCS) -o $@ $(LDLIBS_COMMON)

chess_sdl: $(SDL_SRCS)
	$(CC) $(CFLAGS) $(OPENMP_FLAG) $(SDL_CFLAGS) $(SDL_SRCS) -o $@ $(SDL_LIBS) $(LDLIBS_COMMON)

clean:
	-rm -f chess_console chess_console.exe chess_sdl chess_sdl.exe *.o || del /Q /F chess_console.exe chess_sdl.exe

help:
	@echo "Targets:"
	@echo "  make console   Build the terminal version"
	@echo "  make sdl       Build the SDL graphical version"
	@echo "  make all       Build both binaries"
	@echo "  make clean     Remove build outputs"
