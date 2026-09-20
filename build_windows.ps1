# Builds chess_sdl.exe with MinGW-w64 gcc, fetching SDL2 and Clash Display on first run.
# Usage: powershell -ExecutionPolicy Bypass -File build_windows.ps1 [-Console]

param([switch]$Console)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$packages = @(
    @{ Name = 'SDL2-2.32.10';       Url = 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz' },
    @{ Name = 'SDL2_image-2.8.12';  Url = 'https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-devel-2.8.12-mingw.tar.gz' },
    @{ Name = 'SDL2_ttf-2.24.0';    Url = 'https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-devel-2.24.0-mingw.tar.gz' }
)

if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    throw 'gcc not found. Install MinGW-w64 (MSYS2 mingw64, WinLibs or Strawberry) and add it to PATH.'
}

New-Item -ItemType Directory -Force deps | Out-Null
foreach ($pkg in $packages) {
    if (Test-Path "deps/$($pkg.Name)") { continue }
    Write-Host "Downloading $($pkg.Name)..."
    $archive = "deps/$($pkg.Name).tar.gz"
    Invoke-WebRequest -Uri $pkg.Url -OutFile $archive -UseBasicParsing
    tar -xzf $archive -C deps
    Remove-Item $archive
}

# Clash Display may not be redistributed, so we fetch it instead of shipping it
$clash = @('fonts/ClashDisplay-Semibold.ttf', 'fonts/ClashDisplay-Medium.ttf')
if (($clash | Where-Object { -not (Test-Path $_) }).Count -gt 0) {
    Write-Host 'Downloading Clash Display from Fontshare...'
    try {
        $zip = 'deps/clash-display.zip'
        Invoke-WebRequest -Uri 'https://api.fontshare.com/v2/fonts/download/clash-display' -OutFile $zip -UseBasicParsing
        Expand-Archive -Force $zip deps/clash-display
        foreach ($font in $clash) {
            $source = Get-ChildItem -Recurse deps/clash-display -Filter (Split-Path $font -Leaf) | Select-Object -First 1
            Copy-Item $source.FullName $font
        }
    } catch {
        Write-Warning "Could not fetch Clash Display ($_). The UI will fall back to Segoe UI."
    }
}

$roots = Get-ChildItem deps -Directory -Filter 'SDL2*' | ForEach-Object { "deps/$($_.Name)/x86_64-w64-mingw32" }
$cflags = @('-O2', '-Wall', '-Wextra', '-std=c11', '-fopenmp')
$includes = $roots | ForEach-Object { "-I$_/include"; "-I$_/include/SDL2" }
$libdirs = $roots | ForEach-Object { "-L$_/lib" }
# search threads keep their move lists on the stack, hence 16 MB
$libs = @('-lmingw32', '-lSDL2main', '-lSDL2', '-lSDL2_image', '-lSDL2_ttf', '-lwinmm', '-lgdi32', '-mwindows', '-lm', '-Wl,--stack,16777216')

Write-Host 'Compiling chess_sdl.exe...'
& gcc @cflags @includes jeu.c ui.c main_sdl.c -o chess_sdl.exe @libdirs @libs
if ($LASTEXITCODE -ne 0) { throw 'SDL build failed.' }

if ($Console) {
    Write-Host 'Compiling chess_console.exe...'
    & gcc @cflags jeu.c main.c -o chess_console.exe -lm '-Wl,--stack,16777216'
    if ($LASTEXITCODE -ne 0) { throw 'Console build failed.' }
}

# ship the SDL and gcc runtime DLLs so the exe runs outside a toolchain shell
foreach ($root in $roots) {
    Copy-Item "$root/bin/*.dll" . -Force
}
$gccBin = Split-Path (Get-Command gcc).Source
foreach ($dll in @('libgomp-1.dll', 'libwinpthread-1.dll', 'libgcc_s_seh-1.dll')) {
    $path = Join-Path $gccBin $dll
    if (Test-Path $path) { Copy-Item $path . -Force }
}

Write-Host 'Done. Run .\chess_sdl.exe'
