@echo off
REM ─────────────────────────────────────────────────────────
REM  Build Sightline Player — MSVC x86 (Win XP SP3 target)
REM  Run from Developer Command Prompt for VS (x86)
REM  MSVC v141_xp toolset required (VS 2017 or earlier with XP tools)
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

set INC=/I%IMGUI% /I%IMGUI%\backends

set DEFS=/DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS

set FLAGS=/O2 /MT /EHsc /W3 /nologo

set LIBS=d3d9.lib user32.lib gdi32.lib shell32.lib

set LINK=/SUBSYSTEM:WINDOWS,5.01 /MACHINE:X86

cl %FLAGS% %DEFS% %INC% %SRC% /Fe:%OUT% /link %LINK% %LIBS%

if %ERRORLEVEL% == 0 (
  echo.
  echo *** Build successful: %OUT% ***
) else (
  echo.
  echo *** Build FAILED ***
)
pause
