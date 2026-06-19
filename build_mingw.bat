@echo off
REM ---------------------------------------------------------
REM  Build Sightline Player - MinGW-w64 x86 (Win XP target)
REM  Requires: MinGW-w64 i686 toolchain on PATH (includes xxd)
REM  Run this from the project root directory
REM
REM  FFmpeg XP-mod:
REM    Extract ffmpeg-7.1-*-win32-dev-xpmod-sse.7z into ffmpeg-xp\
REM    Expected layout:
 REM     ffmpeg-xp\include\libavformat\avformat.h  (headers)
REM      ffmpeg-xp\lib\libavcodec.a               (static libs)
REM ---------------------------------------------------------

set IMGUI=imgui
set FFMPEG_DIR=ffmpeg-xp
set OUT=SightlinePlayer.exe
set FONT_TTF=%IMGUI%\misc\fonts\ProggyVector.ttf
set FONT_H=src\font_proggy.h
set RC_SRC=src\sightline.rc
set RC_OBJ=sightline_res.o
set LOGO_PNG=src\Sightline Logo.png
set LOGO_H=src\logo_data.h

REM -- Step 1: Generate embedded font header ----------------
echo Generating %FONT_H% ...
if exist "%FONT_TTF%" (
    xxd -i "%FONT_TTF%" > "%FONT_H%.tmp"
    powershell -NoProfile -Command ^
        "(Get-Content '%FONT_H%.tmp') ^
        -replace 'unsigned char imgui_misc_fonts_ProggyVector_ttf','const unsigned char proggy_vector_ttf' ^
        -replace 'unsigned int imgui_misc_fonts_ProggyVector_ttf_len','const unsigned int proggy_vector_ttf_len' ^
        | Set-Content '%FONT_H%'"
    del "%FONT_H%.tmp"
    echo   OK: %FONT_H% generated.
) else (
    echo   WARNING: %FONT_TTF% not found - font will fall back to default.
    echo // font_proggy.h -- placeholder > "%FONT_H%"
)

REM -- Step 1.5: Compile icon resource (.rc -> .o) ----------
REM  --include-dir src tells windres to find favicon.ico in src/
echo Compiling icon resource ...
set RC_OBJ=
if exist "%RC_SRC%" (
    if exist "src\favicon.ico" (
        windres "%RC_SRC%" --include-dir src -o sightline_res.o
        if %ERRORLEVEL% == 0 (
            echo   OK: sightline_res.o generated.
            set RC_OBJ=sightline_res.o
        ) else (
            echo   WARNING: windres failed - EXE will have no embedded icon.
        )
    ) else (
        echo   WARNING: src\favicon.ico not found - skipping icon.
    )
) else (
    echo   WARNING: %RC_SRC% not found - skipping icon.
)

REM -- Step 1.6: Generate embedded logo header from PNG -----
REM  This creates src\logo_data.h so HasLogo() returns true
REM  and the real PNG is shown in the titlebar at runtime.
echo Generating %LOGO_H% from logo PNG ...
if exist "%LOGO_PNG%" (
    xxd -i "%LOGO_PNG%" > "%LOGO_H%.tmp"
    powershell -NoProfile -Command ^
        "(Get-Content '%LOGO_H%.tmp') ^
        -replace 'unsigned char src_Sightline_Logo_png','const unsigned char sightline_logo_png' ^
        -replace 'unsigned int src_Sightline_Logo_png_len','const unsigned int sightline_logo_png_len' ^
        | Set-Content '%LOGO_H%'"
    del "%LOGO_H%.tmp"
    echo   OK: %LOGO_H% generated.
) else (
    echo   WARNING: "%LOGO_PNG%" not found - logo will use vector fallback.
    echo // logo_data.h -- placeholder > "%LOGO_H%"
)

REM -- Step 2: Compile --------------------------------------
set SRC=src\main.cpp ^
  src\video_player.cpp ^
  %IMGUI%\imgui.cpp ^
  %IMGUI%\imgui_draw.cpp ^
  %IMGUI%\imgui_tables.cpp ^
  %IMGUI%\imgui_widgets.cpp ^
  %IMGUI%\backends\imgui_impl_win32.cpp ^
  %IMGUI%\backends\imgui_impl_dx9.cpp

set INC=-Isrc -I%IMGUI% -I%IMGUI%\backends -I%FFMPEG_DIR%\include
set DEFS=-DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DWIN32_LEAN_AND_MEAN
set FLAGS=-m32 -O2 -mwindows -static -static-libgcc -static-libstdc++
set LIBS=-ld3d9 -ldwmapi -luser32 -lgdi32 -lgdiplus -lshell32 -lole32 -loleaut32 -luuid -lwinmm
set LIBS=%LIBS% -lavcodec -lavformat -lavutil -lswscale -lswresample
set LDIR=-L%FFMPEG_DIR%\lib

if "%RC_OBJ%"=="" (
    g++ %FLAGS% %DEFS% %INC% %SRC% %LDIR% -o %OUT% %LIBS%
) else (
    g++ %FLAGS% %DEFS% %INC% %SRC% %RC_OBJ% %LDIR% -o %OUT% %LIBS%
)

if %ERRORLEVEL% == 0 (
  echo.
  echo *** Build successful: %OUT% ***
) else (
  echo.
  echo *** Build FAILED ***
)
pause
