# Linux build notes

Full instructions are in [README.md](README.md).

## Dependencies

```bash
sudo apt-get install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev   # Debian/Ubuntu
sudo pacman -S base-devel sdl2 sdl2_image sdl2_ttf                                   # Arch
sudo dnf install gcc SDL2-devel SDL2_image-devel SDL2_ttf-devel                      # Fedora
```

## Build and run

```bash
./build_linux.sh        # or: make all
./chess_sdl             # run from the project directory
```

## Notes

- `./chess_sdl --pcvpc --benchmark --depth 4` plays a scripted benchmark game.
- If a search thread crashes, raise the OpenMP stack: `export OMP_STACKSIZE=16M`.
- The UI falls back to DejaVu fonts when the bundled ones are missing.
