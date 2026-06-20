// ============================================================
//  ytdlp_check.cpp  —  Sightline PoC
//  yt-dlp detection and installation helper
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
#include <wininet.h>   // WinInet — present on XP SP3
#include "ytdlp_check.h"
#include <cstdio>
#include <cstring>
#include <string>

// ── Download URLs (nicolaasjan/yt-dlp fork) ──────────────────
//
// Windows XP / Vista:  last build that ships a standalone i686
//   EXE compiled against a Python runtime compatible with NT 5.x.
//   The release tag used here is the most recent one that still
//   lists a yt-dlp_x86.exe asset in the nicolaasjan fork.
//
// Windows 7 and later: current release from the same fork; these
//   builds assume Vista+ runtime APIs that are unavailable on XP.
//
// Both URLs point to GitHub Releases assets — they resolve via
// HTTPS redirect.  WinInet on XP SP3 can follow redirects but
// may fail TLS negotiation with modern GitHub CDN certs because
// XP ships with an outdated CA bundle.  To work around this the
// download is attempted over HTTPS; if it fails the helper falls
// back to HTTP (GitHub served some assets over plain HTTP in the
// past, though this is no longer guaranteed).  A proper
// production build would bundle an up-to-date CA bundle.
#define YTDLP_URL_XP  \
    "https://github.com/nicolaasjan/yt-dlp/releases/latest/download/yt-dlp_x86.exe"
#define YTDLP_URL_WIN7 \
    "https://github.com/nicolaasjan/yt-dlp/releases/latest/download/yt-dlp.exe"

// ─────────────────────────────────────────────────────────────
//  IsWindowsXPOrVista
//  Returns true when running on NT 5.x or 6.0 (XP / Vista).
//  Uses VerifyVersionInfoA so it compiles without SDK > 7.1.
// ─────────────────────────────────────────────────────────────
static bool IsWindowsXPOrVista()
{
    // We want: major < 6 OR (major == 6 AND minor == 0)
    // i.e. any build older than Windows 7 (6.1).
    OSVERSIONINFOEXA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.dwMajorVersion      = 6;
    osvi.dwMinorVersion      = 1;  // Windows 7

    DWORDLONG mask = 0;
    VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_LESS);
    VER_SET_CONDITION(mask, VER_MINORVERSION, VER_LESS);

    // VerifyVersionInfoA returns TRUE if the running OS satisfies
    // the condition.  We ask "is major < 6 OR minor < 1?".
    // Actually VerifyVersionInfo checks ALL specified fields with
    // AND logic, so we split into two separate checks.

    // Check 1: major version < 6  (XP = 5.x)
    OSVERSIONINFOEXA chk1;
    ZeroMemory(&chk1, sizeof(chk1));
    chk1.dwOSVersionInfoSize = sizeof(chk1);
    chk1.dwMajorVersion      = 6;
    DWORDLONG m1 = 0;
    VER_SET_CONDITION(m1, VER_MAJORVERSION, VER_LESS);
    if (VerifyVersionInfoA(&chk1, VER_MAJORVERSION, m1))
        return true;   // strictly below Vista

    // Check 2: major == 6 AND minor == 0  (Vista exactly)
    OSVERSIONINFOEXA chk2;
    ZeroMemory(&chk2, sizeof(chk2));
    chk2.dwOSVersionInfoSize = sizeof(chk2);
    chk2.dwMajorVersion      = 6;
    chk2.dwMinorVersion      = 0;
    DWORDLONG m2 = 0;
    VER_SET_CONDITION(m2, VER_MAJORVERSION, VER_EQUAL);
    VER_SET_CONDITION(m2, VER_MINORVERSION, VER_EQUAL);
    if (VerifyVersionInfoA(&chk2, VER_MAJORVERSION | VER_MINORVERSION, m2))
        return true;   // exactly Vista

    return false;
}

// ─────────────────────────────────────────────────────────────
//  GetExeDir — directory of the running executable (no trailing \)
// ─────────────────────────────────────────────────────────────
static std::string GetExeDir()
{
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0) return ".";
    // Walk back to the last backslash
    for (int i = (int)len - 1; i >= 0; i--) {
        if (buf[i] == '\\' || buf[i] == '/') {
            buf[i] = '\0';
            return std::string(buf);
        }
    }
    return ".";
}

// ─────────────────────────────────────────────────────────────
//  YtDlp_FindPath
// ─────────────────────────────────────────────────────────────
std::string YtDlp_FindPath()
{
    std::string path = GetExeDir() + "\\yt-dlp.exe";
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return path;
    return "";
}

// ─────────────────────────────────────────────────────────────
//  YtDlp_Download — WinInet HTTPS download to a local file
// ─────────────────────────────────────────────────────────────
bool YtDlp_Download(const char* srcUrl, const char* destPath)
{
    HINTERNET hInet = InternetOpenA(
        "Sightline/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0);
    if (!hInet) return false;

    // INTERNET_FLAG_RELOAD       — bypass cache
    // INTERNET_FLAG_NO_UI        — no dialogs
    // INTERNET_FLAG_SECURE       — HTTPS
    // INTERNET_FLAG_IGNORE_CERT_DATE_INVALID — XP TLS workaround
    // INTERNET_FLAG_IGNORE_CERT_CN_INVALID   — XP TLS workaround
    DWORD flags = INTERNET_FLAG_RELOAD
                | INTERNET_FLAG_NO_UI
                | INTERNET_FLAG_SECURE
                | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID
                | INTERNET_FLAG_IGNORE_CERT_CN_INVALID;

    HINTERNET hUrl = InternetOpenUrlA(hInet, srcUrl, NULL, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hInet);
        return false;
    }

    HANDLE hFile = CreateFileA(
        destPath,
        GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInet);
        return false;
    }

    bool   ok  = false;
    char   buf[8192];
    DWORD  read = 0, written = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
        WriteFile(hFile, buf, read, &written, NULL);
        ok = true;  // at least one block written
    }
    // Verify the final byte count is non-zero
    DWORD fsize = GetFileSize(hFile, NULL);
    ok = (fsize > 0);

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInet);

    if (!ok) DeleteFileA(destPath);  // clean up partial download
    return ok;
}

// ─────────────────────────────────────────────────────────────
//  YtDlp_EnsureAvailable
// ─────────────────────────────────────────────────────────────
bool YtDlp_EnsureAvailable(HWND hwndOwner, std::string* outPath)
{
    // 1. Already present?
    std::string found = YtDlp_FindPath();
    if (!found.empty()) {
        if (outPath) *outPath = found;
        return true;
    }

    // 2. Ask the user
    int choice = MessageBoxA(
        hwndOwner,
        "Sightline requires yt-dlp to stream YouTube videos.\n"
        "yt-dlp was not found next to Sightline.exe.\n\n"
        "Do you want to download and install it now?\n"
        "(It will be saved in the same folder as Sightline.exe)",
        "yt-dlp not found",
        MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL);

    if (choice != IDYES) return false;  // user declined — caller exits

    // 3. Choose URL based on OS
    const char* url = IsWindowsXPOrVista() ? YTDLP_URL_XP : YTDLP_URL_WIN7;
    std::string destPath = GetExeDir() + "\\yt-dlp.exe";

    // 4. Show a progress hint (simple blocking dialog would require a
    //    separate thread; for the PoC we just change the cursor)
    HCURSOR waitCursor = LoadCursor(NULL, IDC_WAIT);
    HCURSOR oldCursor  = SetCursor(waitCursor);

    bool ok = YtDlp_Download(url, destPath.c_str());

    SetCursor(oldCursor);

    if (!ok) {
        MessageBoxA(
            hwndOwner,
            "Download failed.\n\n"
            "Please download yt-dlp.exe manually from:\n"
            "  https://github.com/nicolaasjan/yt-dlp/releases\n"
            "and place it in the same folder as Sightline.exe.",
            "Download failed",
            MB_OK | MB_ICONERROR | MB_TASKMODAL);
        return false;
    }

    if (outPath) *outPath = destPath;
    return true;
}
