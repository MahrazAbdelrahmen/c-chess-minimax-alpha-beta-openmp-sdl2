# SDL frontend notes

Build instructions live in [README.md](README.md). This page covers the SDL specifics.

## Dependencies

SDL2, SDL2_image and SDL2_ttf.

- Windows: `build_windows.ps1` downloads them into `deps/`
- MSYS2: `pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf`
- Debian/Ubuntu: `sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`
- macOS: `brew install sdl2 sdl2_image sdl2_ttf`

## Manual build

```bash
gcc jeu.c ui.c main_sdl.c -o chess_sdl $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -fopenmp -lm -O2
```

On Windows add `-lwinmm -lgdi32 -mwindows -Wl,--stack,16777216`. The stack matters: search threads
keep their move lists on it, and YBW tasks nest searches.

## Troubleshooting

**SDL2.dll not found.** Copy the SDL DLLs next to the exe, or rerun `build_windows.ps1`.

**Blank squares.** Run from the project directory so `chess_green/` is found; without it the UI
falls back to drawn discs.

**Text missing.** The UI loads `fonts/ClashDisplay-*.ttf` and `fonts/FragmentMono-Regular.ttf`,
then falls back to Segoe UI / Consolas on Windows and DejaVu on Linux.

**Thread crashes on Linux.** Raise the OpenMP stack with `export OMP_STACKSIZE=16M`.
