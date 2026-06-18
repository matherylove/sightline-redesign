# Sightline Player — Dear ImGui / Win32 / Direct3D9

Replica of `player-redesign-3.html` built in C++.  
One self-contained EXE — **Windows XP SP3 x86 → Windows 11**.

---

## Project Structure

```
sightline-redesign/
├── src/
│   └── main.cpp              ← Full ImGui UI (~700 lines)
├── CMakeLists.txt            ← CMake build (MSVC or MinGW)
├── build_mingw.bat           ← One-shot MinGW-w64 build
├── build_msvc.bat            ← One-shot MSVC build
└── imgui/                    ← YOU place Dear ImGui here
    ├── imgui.cpp / .h
    ├── imgui_draw.cpp
    ├── imgui_tables.cpp
    ├── imgui_widgets.cpp
    ├── imgui_internal.h
    ├── imconfig.h
    ├── imstb_*.h
    └── backends/
        ├── imgui_impl_win32.cpp / .h
        └── imgui_impl_dx9.cpp  / .h
```

---

## Step 1 — Get Dear ImGui

Download **v1.90.9** (best XP + D3D9 compat):

```
https://github.com/ocornut/imgui/archive/refs/tags/v1.90.9.zip
```

Extract and copy the files listed above into `./imgui/`.

> ⚠️ Do NOT use v1.91+ for XP targets — may pull Win7+ APIs.

---

## Step 2 — Build

### Option A: MinGW-w64 i686 (recommended — single portable EXE)

1. Install [MinGW-w64 i686](https://www.mingw-w64.org/) or via MSYS2:
   ```
   pacman -S mingw-w64-i686-gcc
   ```
2. Run from project root:
   ```
   build_mingw.bat
   ```
3. Output: `SightlinePlayer.exe` — no extra DLLs needed except `d3d9.dll` (built into XP SP3+).

### Option B: MSVC with XP Toolset

1. Install **Visual Studio 2017**, add component `v141_xp`.
2. Open an **x86 Developer Command Prompt**.
3. Run:
   ```
   build_msvc.bat
   ```

---

## What Is Rendered

| Section | Status |
|---|---|
| Titlebar (logo, nav, search, settings) | ✅ |
| Video area 16:9 (black + gradient overlay) | ✅ |
| Seekbar (buffer fill, drag thumb) | ✅ Interactive |
| Controls (play/pause, ±10s, vol slider, quality, fullscreen) | ✅ Clickable |
| Action bar (Like, Dislike, Share, Download, More, meta) | ✅ |
| Info zone (title, tabs, channel row, subscribe, description) | ✅ |
| Sidebar (Up Next, Now Playing card, 7 related items) | ✅ Scrollable |
| Status bar (playing dot, codec info) | ✅ |

---

## XP SP3 x86 Compatibility

- `WINVER=0x0501`, `_WIN32_WINNT=0x0501` — no Win7+ APIs
- DirectX 9 only (`d3d9.dll` — native on XP SP3)
- Static CRT (`/MT` / `-static`) — no `MSVCR*.dll` dependency
- 32-bit PE (`/MACHINE:X86` or `-m32`)
- Minimum PE subsystem: `/SUBSYSTEM:WINDOWS,5.01`
- Dear ImGui v1.90.x backend safe on XP

---

## Colour Palette

| Token | Hex | Role |
|---|---|---|
| `--bg` | `#0C1014` | App background |
| `--surface` | `#131920` | Titlebar, controls, sidebar |
| `--surface-2` | `#192028` | Cards, inputs |
| `--surface-3` | `#1E262F` | Hover states, rails |
| `--accent` | `#4EA8A8` | Teal primary |
| `--text` | `#E6EDF2` | Primary text |
| `--text-muted` | `#8A97A3` | Secondary labels |
| `--text-faint` | `#4D5E69` | Timestamps, meta |
