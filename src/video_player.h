#pragma once
// ============================================================
//  video_player.h  —  Sightline PoC video backend
//  FFmpeg XP-mod + yt-dlp (nicolaasjan) + waveOut
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
#include <mmsystem.h>   // waveOut — winmm.lib — present on XP SP3
#include <d3d9.h>
#include <string>

// ── Opaque forward declarations ──────────────────────────────
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

// ── State enum ───────────────────────────────────────────────
enum VideoState {
    VS_IDLE    = 0,
    VS_LOADING,
    VS_PLAYING,
    VS_PAUSED,
    VS_ERROR,
    VS_EOF
};

// ── Audio double-buffer ───────────────────────────────────────
#define AUDIO_BLOCK_SAMPLES  4096
#define AUDIO_BLOCK_COUNT    2
#define AUDIO_CHANNELS       2
#define AUDIO_SAMPLE_RATE    44100

struct AudioBlock {
    WAVEHDR  hdr;
    short    data[AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS];
};

// ── Main player struct ────────────────────────────────────────
struct VideoPlayer {
    // ── Public API (call from Win32 main thread) ──────────────

    // Open a YouTube URL.  ytdlpPath and ffmpegBinDir are paths to
    // yt-dlp.exe and the directory containing ffmpeg DLLs / the
    // ffmpeg executable (used only for --get-url; decoding goes
    // through libavformat loaded at compile time).
    bool Open(const std::string& ytUrl,
              LPDIRECT3DDEVICE9  device,
              const std::string& ytdlpPath);

    // Must be called once per render frame on the main thread.
    // Returns true when a new frame was uploaded to the texture.
    bool Update();

    // Release everything (safe to call when already idle).
    void Close();

    // Playback control
    void SetPaused(bool paused);
    void Seek(float t);  // 0.0 – 1.0 normalised

    // Getters
    LPDIRECT3DTEXTURE9 GetTexture() const { return tex_; }
    VideoState         GetState()   const { return state_; }
    double             GetPos()     const { return pos_sec_; }   // seconds
    double             GetDur()     const { return dur_sec_; }   // seconds
    int                GetWidth()   const { return vid_w_; }
    int                GetHeight()  const { return vid_h_; }
    // Normalised position 0-1 for the seekbar
    float              GetSeekPos() const {
        if (dur_sec_ <= 0.0) return 0.f;
        return (float)(pos_sec_ / dur_sec_);
    }
    const char*        GetError()   const { return err_buf_; }

    // ── Constructor / destructor ──────────────────────────────
    VideoPlayer();
    ~VideoPlayer() { Close(); }

    // Non-copyable
    VideoPlayer(const VideoPlayer&)            = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

private:
    // ── Device & texture ─────────────────────────────────────
    LPDIRECT3DDEVICE9  dev_  = nullptr;
    LPDIRECT3DTEXTURE9 tex_  = nullptr;
    int                tex_w_ = 0, tex_h_ = 0;

    // ── Video decode state ───────────────────────────────────
    AVFormatContext* fmt_ctx_  = nullptr;
    AVCodecContext*  vdec_ctx_ = nullptr;
    AVCodecContext*  adec_ctx_ = nullptr;
    SwsContext*      sws_ctx_  = nullptr;
    SwrContext*      swr_ctx_  = nullptr;
    int              vid_stream_ = -1;
    int              aud_stream_ = -1;
    int              vid_w_ = 0, vid_h_ = 0;
    double           dur_sec_ = 0.0;
    double           pos_sec_ = 0.0;
    double           vid_tb_  = 0.0;  // video time-base (seconds/tick)
    double           aud_tb_  = 0.0;

    // ── Ring buffer (video frames ready for upload) ───────────
    // Each slot: BGRA plane, vid_w_ * vid_h_ * 4 bytes
    static const int RING_SIZE = 4;
    struct RingSlot {
        unsigned char* buf    = nullptr;  // BGRA pixels
        int            w      = 0, h = 0;
        double         pts    = 0.0;      // seconds
        bool           ready  = false;
    };
    RingSlot  ring_[RING_SIZE];
    int       ring_write_ = 0;  // written by decode thread
    int       ring_read_  = 0;  // consumed by main thread
    CRITICAL_SECTION ring_cs_;
    HANDLE    ring_not_empty_;  // auto-reset event
    HANDLE    ring_not_full_;   // auto-reset event

    // ── Worker threads ────────────────────────────────────────
    HANDLE  decode_thread_ = nullptr;  // video+audio demux/decode
    HANDLE  audio_thread_  = nullptr;  // waveOut feeder
    volatile LONG stop_flag_  = 0;     // set to 1 to signal threads
    volatile LONG seek_flag_  = 0;
    volatile double seek_target_ = 0.0;
    volatile LONG paused_flag_  = 0;

    // ── Audio state ───────────────────────────────────────────
    HWAVEOUT    wave_out_  = nullptr;
    AudioBlock  audio_blocks_[AUDIO_BLOCK_COUNT];
    HANDLE      wave_done_event_ = nullptr;  // signalled by waveOutProc
    // Audio packet queue (simple ring of pointers to AVPacket)
    static const int AUD_QUEUE = 64;
    AVPacket*  aud_queue_[AUD_QUEUE];
    int        aud_q_write_ = 0, aud_q_read_ = 0;
    CRITICAL_SECTION aud_cs_;

    // ── Error buffer ─────────────────────────────────────────
    char err_buf_[256];

    // ── State ────────────────────────────────────────────────
    VideoState state_ = VS_IDLE;

    // ── Private helpers ──────────────────────────────────────
    bool        ResolveUrl(const std::string& ytdlpPath,
                            const std::string& ytUrl,
                            std::string& outVideoUrl,
                            std::string& outAudioUrl);
    bool        OpenStreams(const std::string& videoUrl,
                            const std::string& audioUrl);
    bool        CreateTexture(int w, int h);
    bool        UploadFrame(const RingSlot& slot);
    void        SetError(const char* msg);

    // ── Thread entry points (static trampolines) ──────────────
    static DWORD WINAPI DecodeThreadProc(LPVOID param);
    static DWORD WINAPI AudioThreadProc(LPVOID param);
    static void  CALLBACK WaveOutCallback(HWAVEOUT, UINT, DWORD_PTR,
                                           DWORD_PTR, DWORD_PTR);
    void  DecodeLoop();
    void  AudioLoop();
};

// ── Global singleton (extern — defined in video_player.cpp) ──
extern VideoPlayer g_player;
