// ============================================================
//  video_player.h  —  Sightline PoC video backend
//  Target: Windows XP SP3 x86 (WINVER=0x0501) and later
//  FFmpeg: 7.1 xpmod-sse win32 (static libs in ffmpeg-xp/lib)
//  yt-dlp: nicolaasjan fork  (yt-dlp.exe beside the binary)
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

// ─── Video player states ──────────────────────────────────────
enum VideoState {
    VS_IDLE    = 0,
    VS_LOADING,   // yt-dlp resolving URL
    VS_PLAYING,
    VS_PAUSED,
    VS_EOF,
    VS_ERROR
};

// ─── One decoded RGB frame sitting in the ring buffer ─────────
struct DecodedFrame {
    unsigned char* rgb;   // width*height*3 bytes (RGB24, top-down)
    double         pts;   // presentation time in seconds
    int            w, h;
};

// ─── Public interface ─────────────────────────────────────────
// All methods except Update() may be called from any thread.
// Update() MUST be called from the D3D9 render thread (main thread).
struct VideoPlayer {
    // ── Lifecycle ─────────────────────────────────────────────
    VideoPlayer();
    ~VideoPlayer();

    // Open a YouTube URL.  Spawns yt-dlp to resolve, then
    // starts the decode thread.
    //   ytUrl     — full YouTube watch URL
    //   dev       — Direct3D9 device (for texture creation)
    //   ytdlpExe  — path to yt-dlp.exe (e.g. "yt-dlp.exe")
    //   ffmpegBin — unused; FFmpeg is linked statically
    bool Open(const std::string& ytUrl,
              LPDIRECT3DDEVICE9 dev,
              const std::string& ytdlpExe = "yt-dlp.exe",
              const std::string& ffmpegBin = "");

    void Close();          // Stop + release all resources
    void SetPaused(bool p);
    // Seek to fraction 0..1 of total duration
    void Seek(float fraction);

    // ── Per-frame call (render thread only) ───────────────────
    // Returns true if the D3D9 texture was updated this frame.
    bool Update(LPDIRECT3DDEVICE9 dev);

    // ── Device-lost handling ──────────────────────────────────
    void OnDeviceLost();    // call from ResetDevice() before Reset
    void OnDeviceReset(LPDIRECT3DDEVICE9 dev); // after Reset

    // ── Getters ───────────────────────────────────────────────
    LPDIRECT3DTEXTURE9 GetTexture()  const { return tex_; }
    VideoState         GetState()    const { return state_; }
    double             GetPos()      const { return posSeconds_; }
    double             GetDur()      const { return durSeconds_; }
    int                GetWidth()    const { return vidW_; }
    int                GetHeight()   const { return vidH_; }
    const std::string& GetError()    const { return errorMsg_; }
    const std::string& GetTitle()    const { return title_; }

    // Progress 0..1  (meaningful during VS_PLAYING / VS_PAUSED)
    float GetProgress() const {
        if(durSeconds_ <= 0.0) return 0.f;
        return (float)(posSeconds_ / durSeconds_);
    }

private:
    // ── Internal helpers ──────────────────────────────────────

    // Spawn yt-dlp.exe and read its stdout into outUrls.
    // Returns true when at least one URL is found.
    bool ResolveYtUrl(const std::string& ytUrl,
                      std::string& videoUrl,
                      std::string& audioUrl);

    // The decode + audio thread entry point (static trampoline)
    static DWORD WINAPI DecodeThreadProc(LPVOID param);
    void DecodeLoop();   // actual loop running in the thread

    // Upload one RGB24 frame to the D3D9 managed texture
    bool UploadFrame(LPDIRECT3DDEVICE9 dev,
                     const unsigned char* rgb, int w, int h);

    // Audio output via waveOut (XP-compatible)
    void AudioInit(int sampleRate, int channels);
    void AudioFeed(const short* pcm, int frames);
    void AudioClose();

    // ── Ring buffer (3 slots) ─────────────────────────────────
    static const int kRingSize = 3;
    DecodedFrame     ring_[kRingSize];
    volatile LONG    writeIdx_;  // written by decode thread
    volatile LONG    readIdx_;   // read  by render thread
    CRITICAL_SECTION ringCs_;

    // ── State ─────────────────────────────────────────────────
    volatile VideoState state_;
    std::string         errorMsg_;
    std::string         title_;
    std::string         ytdlpExe_;
    std::string         videoUrl_;
    std::string         audioUrl_;

    // ── Decode thread ─────────────────────────────────────────
    HANDLE        decodeThread_;
    volatile LONG stopFlag_;    // set to 1 to signal the thread to exit
    volatile LONG pauseFlag_;   // set to 1 when paused
    volatile LONG seekFlag_;    // set to 1 when a seek is pending
    double        seekTarget_;  // seconds (written before seekFlag_)

    // ── Timing ────────────────────────────────────────────────
    volatile double posSeconds_;
    volatile double durSeconds_;

    // ── Video dimensions ──────────────────────────────────────
    int vidW_, vidH_;

    // ── D3D9 texture ──────────────────────────────────────────
    LPDIRECT3DTEXTURE9 tex_;
    int                texW_, texH_;

    // ── waveOut audio ─────────────────────────────────────────
    HWAVEOUT   hWaveOut_;
    static const int kWaveBufs  = 2;
    static const int kWaveSamples = 4096; // frames per buffer
    WAVEHDR    waveHdr_[kWaveBufs];
    short*     waveBuf_[kWaveBufs];
    int        waveBufIdx_;
    int        audioChannels_;
    int        audioSampleRate_;
};
