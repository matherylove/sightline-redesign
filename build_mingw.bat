@echo off
REM ─────────────────────────────────────────────────────────
REM  Build Sightline Player — MinGW-w64 x86 (Win XP target)
REM  Requires: MinGW-w64 i686 toolchain on PATH (includes xxd)
REM  Run this from the project root directory
REM ─────────────────────────────────────────────────────────

set IMGUI=imgui
set OUT=SightlinePlayer.exe
set FONT_TTF=%IMGUI%\misc\fonts\ProggyVector.ttf
set FONT_H=src\font_proggy.h

REM ── Step 1: Generate embedded font header ─────────────────
echo Generating %FONT_H% ...
if exist "%FONT_TTF%" (
    REM xxd.exe must be on PATH (ships with Git for Windows / MinGW)
    xxd -i "%FONT_TTF%" > "%FONT_H%.tmp"
    powershell -NoProfile -Command ^
        "(Get-Content '%FONT_H%.tmp') ^
        -replace 'unsigned char imgui_misc_fonts_ProggyVector_ttf','const unsigned char proggy_vector_ttf' ^
        -replace 'unsigned int imgui_misc_fonts_ProggyVector_ttf_len','const unsigned int proggy_vector_ttf_len' ^
        | Set-Content '%FONT_H%'"
    del "%FONT_H%.tmp"
    echo   OK: %FONT_H% generated.
) else (
    echo   WARNING: %FONT_TTF% not found.
    echo   Font will fall back to system fonts at runtime.
    echo // font_proggy.h -- placeholder, ProggyVector.ttf not found > "%FONT_H%"
)

REM ── Step 2: Compile ───────────────────────────────────────
set SRC=src\main.cpp ^
  %IMGUI%\imgui.cpp ^
  %IMGUI%\imgui_draw.cpp ^
  %IMGUI%\imgui_tables.cpp ^
  %IMGUI%\imgui_widgets.cpp ^
  %IMGUI%\backends\imgui_impl_win32.cpp ^
  %IMGUI%\backends\imgui_impl_dx9.cpp

set INC=-Isrc -I%IMGUI% -I%IMGUI%\backends

set DEFS=-DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DWIN32_LEAN_AND_MEAN

set FLAGS=-m32 -O2 -mwindows -static -static-libgcc -static-libstdc++

set LIBS=-ld3d9 -ldwmapi -luser32 -lgdi32 -lshell32

g++ %FLAGS% %DEFS% %INC% %SRC% -o %OUT% %LIBS%

if %ERRORLEVEL% == 0 (
  echo.
  echo *** Build successful: %OUT% ***
) else (
  echo.
  echo *** Build FAILED ***
)
pause
