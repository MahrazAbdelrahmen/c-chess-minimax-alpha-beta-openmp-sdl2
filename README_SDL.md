# Chess Engine with SDL2 UI - Compilation Instructions

## Overview
This chess engine now includes a graphical SDL2 UI for a better user experience.

## Files Created/Modified
- `ui.h` - SDL2 UI header file
- `ui.c` - SDL2 UI implementation
- `main_sdl.c` - Main game loop with SDL2 UI integration

## Prerequisites

### Windows (MinGW/MSYS2)
Install SDL2 libraries:
```bash
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf
```

### Windows (Visual Studio)
Download and install SDL2 development libraries from:
- https://www.libsdl.org/download-2.0.php
- https://www.libsdl.org/projects/SDL_image/
- https://www.libsdl.org/projects/SDL_ttf/

### Linux (Ubuntu/Debian)
```bash
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

### Linux (Arch/Manjaro)
```bash
sudo pacman -S sdl2 sdl2_image sdl2_ttf
```

### macOS (Homebrew)
```bash
brew install sdl2 sdl2_image sdl2_ttf
```

## Compilation Commands

### Windows (MinGW/MSYS2)
```bash
gcc -c jeu.c -o jeu.o -fopenmp
gcc -c ui.c -o ui.o `sdl2-config --cflags`
gcc -c main_sdl.c -o main_sdl.o `sdl2-config --cflags`
gcc main_sdl.o jeu.o ui.o -o chess_sdl.exe `sdl2-config --libs` -lSDL2_image -lSDL2_ttf -fopenmp
```

### Windows (Manual SDL2 paths - adjust paths as needed)
```bash
gcc -c jeu.c -o jeu.o -fopenmp
gcc -c ui.c -o ui.o -IC:/SDL2/include -Dmain=SDL_main
gcc -c main_sdl.c -o main_sdl.o -IC:/SDL2/include -Dmain=SDL_main
gcc main_sdl.o jeu.o ui.o -o chess_sdl.exe -LC:/SDL2/lib -lSDL2 -lSDL2_image -lSDL2_ttf -mwindows -fopenmp
```

### Linux/macOS
```bash
gcc -c jeu.c -o jeu.o -fopenmp
gcc -c ui.c -o ui.o $(sdl2-config --cflags)
gcc -c main_sdl.c -o main_sdl.o $(sdl2-config --cflags)
gcc main_sdl.o jeu.o ui.o -o chess_sdl $(sdl2-config --libs) -lSDL2_image -lSDL2_ttf -fopenmp
```

### Alternative: Single Command (Linux/macOS)
```bash
gcc jeu.c ui.c main_sdl.c -o chess_sdl $(sdl2-config --libs) -lSDL2_image -lSDL2_ttf -fopenmp -O2
```

## Running the Game

### Windows
1. Copy SDL2 DLLs to the executable directory:
   - SDL2.dll
   - SDL2_image.dll
   - SDL2_ttf.dll
2. Run: `chess_sdl.exe`

### Linux/macOS
```bash
./chess_sdl
```

## Game Controls

### Menu
- **UP/DOWN arrows**: Navigate menu options
- **ENTER**: Select option
- **ESC**: Back to menu

### Gameplay
- **Left Click**: Select piece / Move piece
- **Right Click**: Cancel selection
- **ESC**: Return to menu

### Game Modes
1. **PC vs PC**: Watch the AI play against itself
2. **Human (Black) vs PC**: Play as Black against the AI
3. **Human (White) vs PC**: Play as White against the AI

## Assets

The SDL frontend looks for piece and board images in the bundled `chess_green/` folder.

If those assets are missing, the game falls back to simple rendered shapes and text.

## Troubleshooting

### "SDL2.dll not found" (Windows)
Copy the SDL2 DLL files to the same directory as your executable.

### "Cannot find -lSDL2" 
Make sure SDL2 is installed and the library path is in your compiler's search path.

### Blank squares / no pieces
The fallback rendering will show colored circles. For proper pieces, add sprite images.

### Font not loading
The UI tries multiple system fonts. If none are found, text won't display but the game will work.

## Original Console Version

To compile the original console version:
```bash
gcc jeu.c main.c -o chess_console -fopenmp -O2
```
