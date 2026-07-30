# Chess Engine with SDL2 UI - Linux Build Instructions

## Quick Build

```bash
chmod +x build_linux.sh
./build_linux.sh
```

## Manual Build

### 1. Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev build-essential
```

**Arch/Manjaro:**
```bash
sudo pacman -S sdl2 sdl2_image sdl2_ttf base-devel
```

**Fedora:**
```bash
sudo dnf install SDL2-devel SDL2_image-devel SDL2_ttf-devel gcc openmp
```

### 2. Compile

```bash
gcc -c jeu.c -o jeu.o -fopenmp -O2
gcc -c ui.c -o ui.o $(pkg-config --cflags sdl2 SDL2_image SDL2_ttf) -O2
gcc -c main_sdl.c -o main_sdl.o $(pkg-config --cflags sdl2 SDL2_image SDL2_ttf) -O2
gcc jeu.o ui.o main_sdl.o -o chess_sdl $(pkg-config --libs sdl2 SDL2_image SDL2_ttf) -fopenmp -lm
```

Or single command:
```bash
gcc jeu.c ui.c main_sdl.c -o chess_sdl $(pkg-config --libs sdl2 SDL2_image SDL2_ttf) -fopenmp -lm -O2
```

### 3. Run

```bash
./chess_sdl
```

**Important:** Run from the project directory so it can find the `chess_green/` folder with piece images.

## Controls

### Menu
- **UP/DOWN arrows**: Navigate options
- **ENTER**: Select
- **ESC**: Quit

### Gameplay
- **Left Click**: Select piece / Move
- **Right Click**: Cancel selection
- **ESC**: Return to menu

## Game Modes

1. **PC vs PC**: Watch AI play itself
2. **Human (Black) vs PC**: You play Black
3. **Human (White) vs PC**: You play White  
4. **Quit**: Exit to desktop

## Troubleshooting

### "Piece images not loading"
Make sure you're running from the project directory:
```bash
cd /path/to/chess
./chess_sdl
```

Check that `chess_green/` folder exists with the PNG files.

### "SDL2 not found"
Install the development packages (see step 1 above).

### "undefined reference to SDL_..."
Make sure pkg-config can find SDL2:
```bash
pkg-config --libs sdl2
```

### Pieces show as circles instead of images
The PNG files aren't being found. Check:
1. `chess_green/` folder is in the same directory as the executable
2. File names match exactly (case-sensitive on Linux)

### Console output
The game prints loading status to console. Run from terminal to see:
```bash
./chess_sdl
```

## Original Console Version

```bash
gcc jeu.c main.c -o chess_console -fopenmp -O2
./chess_console
```
