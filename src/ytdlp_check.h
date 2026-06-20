#pragma once
// ============================================================
//  ytdlp_check.h  —  Sightline PoC
//  Detect yt-dlp.exe at startup; offer download if missing.
//  Compatible: Windows XP SP3 x86 -> Windows 11
// ============================================================
#ifndef WINVER
#  define WINVER       0x0501
#  define _WIN32_WINNT 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// Returns the full path to yt-dlp.exe if it exists next to the
// running executable, or an empty string if not found.
std::string YtDlp_FindPath();

// Checks for yt-dlp.exe.  If missing, shows a MessageBox asking
// the user whether to install it.
//
// Returns:
//   true  — yt-dlp is available (path written to *outPath)
//   false — user declined or download failed; caller should exit.
bool YtDlp_EnsureAvailable(HWND hwndOwner, std::string* outPath);

// Low-level: download srcUrl into destPath using WinInet.
// Returns true on success.
bool YtDlp_Download(const char* srcUrl, const char* destPath);
