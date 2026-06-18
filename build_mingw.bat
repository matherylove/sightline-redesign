@echo off
REM ─────────────────────────────────────────────────────────
REM  Build Sightline Player — MinGW-w64 x86 (Win XP target)
REM  Requires: MinGW-w64 i686 toolchain on PATH
REM  Run this from the project root directory
REM ─────────────────────────────────────────────────────────

set IMGUI=imgui
set OUT=SightlinePlayer.exe

set SRC=src\main.cpp ^
  %IMGUI%\imgui.cpp ^
  %IMGUI%\imgui_draw.cpp ^
  %IMGUI%\imgui_tables.cpp ^
  %IMGUI%\imgui_widgets.cpp ^
  %IMGUI%\backends\imgui_impl_win32.cpp ^
  %IMGUI%\backends\imgui_impl_dx9.cpp

set INC=-I%IMGUI% -I%IMGUI%\backends

set DEFS=-DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DWIN32_LEAN_AND_MEAN

set FLAGS=-m32 -O2 -mwindows -static -static-libgcc -static-libstdc++

set LIBS=-ld3d9 -luser32 -lgdi32 -lshell32

g++ %FLAGS% %DEFS% %INC% %SRC% -o %OUT% %LIBS%

if %ERRORLEVEL% == 0 (
  echo.
  echo *** Build successful: %OUT% ***
) else (
  echo.
  echo *** Build FAILED ***
)
pause
