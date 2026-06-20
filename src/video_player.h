// ============================================================
//  video_player.h  —  Sightline PoC
//  FFmpeg XP-mod + nicolaasjan/yt-dlp playback engine
//  Target: Windows XP SP3 x86 and later
// ============================================================
#pragma once

#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <vector>

// ── Quality levels (index matches g_qualityOpts in main.cpp) ─
// 0=360p  1=480p  2=720p  3=1080p  4=2160p
enum VideoQuality {
    VQ_360P  = 0,
    VQ_480P  = 1,
    VQ_720P  = 2,
    VQ_1080P = 3,
    VQ_2160P = 4,
    VQ_COUNT = 5
};

// ── Player state ──────────────────────────────────────────────
enum VideoState {
    VS_IDLE    = 0,   // nothing loaded
    VS_LOADING = 1,   // yt-dlp resolving URL
    VS_PLAYING = 2,
    VS_PAUSED  = 3,
    VS_ERROR   = 4,
    VS_EOF     = 5
};

// ── Internal frame buffer ─────────────────────────────────────
struct VideoFrame {
    unsigned char* data;  // BGRA row-major
    int            width;
    int            height;
    double         pts;   // presentation time in seconds
};

// ── Main player struct ────────────────────────────────────────
struct VideoPlayer {
    // ── Public API (call from main / GUI thread) ─────────────

    // Initialize paths once at startup
    void Init(const std::string& ytdlpPath,
              const std::string& ffmpegDir);

    // Open a YouTube URL at the given quality.
    // Spins up yt-dlp + decode thread asynchronously.
    // Safe to call while already playing — closes previous first.
    void Open(const std::string& url, VideoQuality q = VQ_360P);

    // Change quality on the fly — re-opens the same URL
    void SetQuality(VideoQuality q);

    // Close / stop and release all resources
    void Close();

    // Pause / resume
    void SetPaused(bool paused);

    // Seek to normalised position [0,1]
    void Seek(float t);

    // Must be called once per render frame from the GUI thread.
    // Uploads next decoded frame to the D3D9 texture if available.
    // Returns true if the texture was updated.
    bool Update(LPDIRECT3DDEVICE9 dev);

    // D3D9 device lost / reset hooks
    void OnLostDevice();
    void OnResetDevice(LPDIRECT3DDEVICE9 dev);

    // ── Getters ───────────────────────────────────────────────
    LPDIRECT3DTEXTURE9 GetTexture()  const { return tex_; }
    VideoState         GetState()    const { return (VideoState)state_; }
    double             GetPos()      const { return pos_; }
    double             GetDur()      const { return dur_; }
    int                GetWidth()    const { return vidW_; }
    int                GetHeight()   const { return vidH_; }
    const char*        GetError()    const { return errMsg_; }
    VideoQuality       GetQuality()  const { return curQ_; }

    // ctor / dtor
    VideoPlayer();
    ~VideoPlayer();

private:
    // ── Paths ─────────────────────────────────────────────────
    char ytdlpPath_[MAX_PATH];
    char ffmpegDir_[MAX_PATH]; // unused for linking but kept for runtime dll check

    // ── State (volatile: written by decode thread, read by GUI thread) ──
    volatile LONG state_;    // cast to VideoState
    volatile LONG seekReq_;  // 0=none, 1=pending
    float         seekPos_;  // normalised [0,1]  — write under cs_
    volatile LONG pauseReq_; // 0=playing, 1=paused

    double pos_;
    double dur_;
    int    vidW_;
    int    vidH_;
    char   errMsg_[256];
    char   curUrl_[2048];
    VideoQuality curQ_;

    // ── D3D9 texture ─────────────────────────────────────────
    LPDIRECT3DTEXTURE9 tex_;    // present texture  (D3DPOOL_MANAGED)
    int texW_;
    int texH_;

    // ── Ring buffer: 2 frames  (decode writes, GUI reads) ────
    static const int kBufCount = 2;
    VideoFrame buf_[kBufCount];
    volatile LONG writeIdx_;  // decode thread advances this
    volatile LONG readIdx_;   // GUI thread advances this
    CRITICAL_SECTION cs_;

    // ── Decode thread ─────────────────────────────────────────
    HANDLE decodeThread_;
    volatile LONG stopFlag_;  // set to 1 to ask thread to exit

    // ── Internal helpers ──────────────────────────────────────
    bool ResolveUrl(const char* ytUrl, VideoQuality q,
                    char* outVideoUrl, int videoUrlLen,
                    char* outAudioUrl, int audioUrlLen);
    void AllocFrameBuffer(int idx, int w, int h);
    void FreeFrameBuffers();
    void SetState(VideoState s);
    void SetError(const char* msg);
    static DWORD WINAPI DecodeThreadProc(LPVOID param);
    void DecodeLoop();
    static const char* QualityToHeightFilter(VideoQuality q);
};

// Global instance — defined in video_player.cpp, declared here
extern VideoPlayer g_player;
