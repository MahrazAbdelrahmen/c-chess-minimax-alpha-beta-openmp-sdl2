# Chess Engine

A C chess engine with alpha-beta pruning, multiple evaluation functions, a console interface, and an SDL2 graphical frontend.

This repository contains:
- an original pedagogical MinMax / alpha-beta chess base
- later project work including SDL UI integration, parallel search support, refactors, fixes, and cleanup

## Overview

The project provides two ways to run the engine:

- `main.c`: console version
- `main_sdl.c` + `ui.c`: SDL2 graphical version

Main features:

- alpha-beta search
- optional parallel search with OpenMP
- multiple evaluation functions
- transposition table with Zobrist hashing
- console and SDL frontends
- human play support in the SDL version, including captures and promotion

## Project Layout

- `jeu.c` / `jeu.h`: core chess engine
- `main.c`: console program
- `main_sdl.c`: SDL main loop
- `ui.c` / `ui.h`: SDL rendering and UI logic
- `chess_green/`: chess piece and board assets
- `build_linux.sh`: Linux build helper
- `Makefile`: common build targets

## Build

### Linux

Install dependencies:

```bash
sudo apt-get install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

Build with the helper script:

```bash
chmod +x build_linux.sh
./build_linux.sh
```

Or build manually:

```bash
gcc jeu.c main.c -o chess_console -fopenmp -O2
gcc jeu.c ui.c main_sdl.c -o chess_sdl $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -fopenmp -lm -O2
```

### Windows (MSYS2 / MinGW)

Install dependencies:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf
```

Build:

```bash
gcc jeu.c main.c -o chess_console.exe -fopenmp -O2
gcc jeu.c ui.c main_sdl.c -o chess_sdl.exe -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_ttf -fopenmp -lm -O2
```

### Makefile

Common targets:

```bash
make console
make sdl
make all
make clean
```

## Run

Run from the project directory so the SDL version can find `chess_green/`.

Console:

```bash
./chess_console
```

SDL:

```bash
./chess_sdl
```

On Windows:

```bash
chess_console.exe
chess_sdl.exe
```

## Controls

### SDL Menu

- `Up` / `Down`: change mode
- `Left` / `Right`: change depth
- `Q` / `A`: change White evaluation
- `W` / `S`: change Black evaluation
- `E` / `D`: change width
- `R`: reset width
- `Enter`: start
- mouse click: interact with cards and controls

### SDL Gameplay

- left click: select a piece or play a move
- right click: cancel current selection
- `Esc`: return to menu

### Console Gameplay

The console version asks for source and destination coordinates in a format such as:

```text
d2d4
```

or spaced input:

```text
d 2 d 4
```

## Notes

- The SDL frontend expects assets in the `chess_green/` folder.
- If SDL assets are missing, UI rendering may be incomplete.
- The console version is useful for testing the engine without graphics.

## Credits

### Original pedagogical base

Original MinMax / alpha-beta chess demonstration base:

- Hidouci W.K. / ESI 2025

This original work is the pedagogical base of the engine.

### Later project work

This repository also includes later work that is not part of the original pedagogical base, including:

- SDL frontend integration
- parallel search additions
- refactors and naming cleanup
- correctness fixes
- maintenance and repository cleanup

These newer additions should not be attributed entirely to the original pedagogical base alone.

### Chess pieces and graphics

Credits for chess pieces:

- Ajay Karat | Devil's Work.shop
- http://devilswork.shop

## License

This project is released under:

- CC0 1.0 Universal (CC0 1.0)

Attribution is not required under CC0, but credit to the original authors and contributors is always appreciated.

