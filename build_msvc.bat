@echo off
REM ─────────────────────────────────────────────────────────
REM  Build Sightline Player — MSVC x86 (Win XP SP3 target)
REM  Run from Developer Command Prompt for VS (x86)
REM  MSVC v141_xp toolset required (VS 2017 or earlier with XP tools)
REM ─────────────────────────────────────────────────────────

set IMGUI=imgui
set OUT=SightlinePlayer.exe
set FONT_TTF=%IMGUI%\misc\fonts\ProggyVector.ttf
set FONT_H=src\font_proggy.h

REM ── Step 1: Generate embedded font header ─────────────────
echo Generating %FONT_H% ...
if exist "%FONT_TTF%" (
    REM xxd.exe must be on PATH (ships with Git for Windows)
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
    echo // font_proggy.h -- placeholder > "%FONT_H%"
)

REM ── Step 2: Compile ───────────────────────────────────────
set SRC=src\main.cpp ^
  %IMGUI%\imgui.cpp ^
  %IMGUI%\imgui_draw.cpp ^
  %IMGUI%\imgui_tables.cpp ^
  %IMGUI%\imgui_widgets.cpp ^
  %IMGUI%\backends\imgui_impl_win32.cpp ^
  %IMGUI%\backends\imgui_impl_dx9.cpp

set INC=/Isrc /I%IMGUI% /I%IMGUI%\backends

set DEFS=/DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS

set FLAGS=/O2 /MT /EHsc /W3 /nologo

set LIBS=d3d9.lib dwmapi.lib user32.lib gdi32.lib shell32.lib

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
