#!/bin/bash
# Builds chess_sdl (and chess_console with --console) on Linux.
set -e

if ! pkg-config --exists sdl2 SDL2_image SDL2_ttf; then
    echo "SDL2 not found. Install the dev packages:"
    echo "  Debian/Ubuntu: sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev"
    echo "  Arch:          sudo pacman -S sdl2 sdl2_image sdl2_ttf"
    echo "  Fedora:        sudo dnf install SDL2-devel SDL2_image-devel SDL2_ttf-devel"
    exit 1
fi

CFLAGS="-O2 -Wall -Wextra -std=c11 -fopenmp"
SDL_FLAGS=$(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf)

gcc $CFLAGS jeu.c ui.c main_sdl.c -o chess_sdl $SDL_FLAGS -lm
echo "built chess_sdl"

if [ "$1" = "--console" ]; then
    gcc $CFLAGS jeu.c main.c -o chess_console -lm
    echo "built chess_console"
fi

echo "run it from this directory so chess_green/ and fonts/ are found"
