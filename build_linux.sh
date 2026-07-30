#!/bin/bash
# Build script for Chess SDL2 on Linux

echo "Building Chess SDL2..."

# Check for SDL2
if ! pkg-config --exists sdl2; then
    echo "Error: SDL2 not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev"
    echo "  Arch/Manjaro: sudo pacman -S sdl2 sdl2_image sdl2_ttf"
    echo "  Fedora: sudo dnf install SDL2-devel SDL2_image-devel SDL2_ttf-devel"
    exit 1
fi

# Compile
gcc -c jeu.c -o jeu.o -fopenmp -O2
if [ $? -ne 0 ]; then
    echo "Error compiling jeu.c"
    exit 1
fi

gcc -c ui.c -o ui.o $(pkg-config --cflags sdl2 SDL2_image SDL2_ttf) -O2
if [ $? -ne 0 ]; then
    echo "Error compiling ui.c"
    exit 1
fi

gcc -c main_sdl.c -o main_sdl.o $(pkg-config --cflags sdl2 SDL2_image SDL2_ttf) -O2
if [ $? -ne 0 ]; then
    echo "Error compiling main_sdl.c"
    exit 1
fi

# Link
gcc jeu.o ui.o main_sdl.o -o chess_sdl $(pkg-config --libs sdl2 SDL2_image SDL2_ttf) -fopenmp -lm -O2
if [ $? -ne 0 ]; then
    echo "Error linking"
    exit 1
fi

echo ""
echo "=========================================="
echo "Build successful!"
echo "=========================================="
echo ""
echo "Run with: ./chess_sdl"
echo ""
echo "IMPORTANT: The chess_green/ folder must be in the same directory"
echo "Run from the project directory:"
echo "  cd /path/to/chess"
echo "  ./chess_sdl"
echo ""

