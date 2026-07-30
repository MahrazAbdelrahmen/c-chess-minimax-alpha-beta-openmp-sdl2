CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
OPENMP_FLAG ?= -fopenmp
LDLIBS_COMMON := -lm

CONSOLE_SRCS := jeu.c main.c
SDL_SRCS := jeu.c ui.c main_sdl.c

ifeq ($(OS),Windows_NT)
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf 2>NUL)
SDL_LIBS ?= $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf 2>NUL)
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
	$(RM) chess_console chess_console.exe chess_sdl chess_sdl.exe *.o

help:
	@echo "Targets:"
	@echo "  make console   Build the terminal version"
	@echo "  make sdl       Build the SDL graphical version"
	@echo "  make all       Build both binaries"
	@echo "  make clean     Remove build outputs"
