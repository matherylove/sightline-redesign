// ============================================================
//  video_player.cpp  —  Sightline PoC
//  FFmpeg XP-mod + nicolaasjan/yt-dlp playback engine
//  Target: Windows XP SP3 x86 and later
//
//  Design constraints (Atom N450 / XP SP3):
//    - Default 360p, selectable up to 2160p
//    - Prefer H.264/MP4 to minimise decode cost
//    - No audio in this first PoC pass
//    - 2-frame ring buffer: decode ahead, GUI presents
//    - Persistent D3DPOOL_MANAGED texture — no alloc per frame
//    - CRITICAL_SECTION (XP-safe) for cross-thread sync
//    - CreateProcess + anonymous pipe for yt-dlp stdout capture
//    - All FFmpeg calls via dynamically-typed C API (no C++ wrappers)
// ============================================================

#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>

// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "video_player.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ── Global instance ──────────────────────────────────────────
VideoPlayer g_player;

// ── Quality → yt-dlp height filter string ───────────────────
const char* VideoPlayer::QualityToHeightFilter(VideoQuality q)
{
    switch(q) {
        case VQ_360P:  return "bestvideo[height<=360][ext=mp4][vcodec^=avc]+bestaudio[ext=m4a]/bestvideo[height<=360]+bestaudio/best[height<=360]";
        case VQ_480P:  return "bestvideo[height<=480][ext=mp4][vcodec^=avc]+bestaudio[ext=m4a]/bestvideo[height<=480]+bestaudio/best[height<=480]";
        case VQ_720P:  return "bestvideo[height<=720][ext=mp4][vcodec^=avc]+bestaudio[ext=m4a]/bestvideo[height<=720]+bestaudio/best[height<=720]";
        case VQ_1080P: return "bestvideo[height<=1080][ext=mp4][vcodec^=avc]+bestaudio[ext=m4a]/bestvideo[height<=1080]+bestaudio/best[height<=1080]";
        case VQ_2160P: return "bestvideo[ext=mp4][vcodec^=avc]+bestaudio[ext=m4a]/bestvideo+bestaudio/best";
        default:       return "bestvideo[height<=360]+bestaudio/best";
    }
}

// ── Constructor / Destructor ─────────────────────────────────
VideoPlayer::VideoPlayer()
    : tex_(NULL), texW_(0), texH_(0)
    , writeIdx_(0), readIdx_(0)
    , decodeThread_(NULL), stopFlag_(0)
    , pos_(0.0), dur_(0.0)
    , vidW_(0), vidH_(0)
    , curQ_(VQ_360P)
    , seekPos_(0.f), seekReq_(0), pauseReq_(0)
{
    state_ = (LONG)VS_IDLE;
    ytdlpPath_[0] = '\0';
    ffmpegDir_[0] = '\0';
    errMsg_[0]    = '\0';
    curUrl_[0]    = '\0';
    InitializeCriticalSection(&cs_);
    for(int i = 0; i < kBufCount; i++) {
        buf_[i].data   = NULL;
        buf_[i].width  = 0;
        buf_[i].height = 0;
        buf_[i].pts    = 0.0;
    }
}

VideoPlayer::~VideoPlayer()
{
    Close();
    FreeFrameBuffers();
    DeleteCriticalSection(&cs_);
}

// ── Init ─────────────────────────────────────────────────────
void VideoPlayer::Init(const std::string& ytdlpPath,
                       const std::string& ffmpegDir)
{
    strncpy(ytdlpPath_, ytdlpPath.c_str(), MAX_PATH-1);
    strncpy(ffmpegDir_, ffmpegDir.c_str(), MAX_PATH-1);
    // Register all demuxers/decoders (FFmpeg 4.x style; no-op in 5+)
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58,9,100)
    av_register_all();
#endif
    avformat_network_init();
}

// ── SetState / SetError helpers ───────────────────────────────
void VideoPlayer::SetState(VideoState s)
{
    InterlockedExchange(&state_, (LONG)s);
}
void VideoPlayer::SetError(const char* msg)
{
    strncpy(errMsg_, msg, sizeof(errMsg_)-1);
    SetState(VS_ERROR);
}

// ── Frame buffer helpers ──────────────────────────────────────
void VideoPlayer::AllocFrameBuffer(int idx, int w, int h)
{
    VideoFrame& f = buf_[idx];
    if(f.data && f.width == w && f.height == h) return; // reuse
    free(f.data);
    f.data   = (unsigned char*)malloc(w * h * 4);
    f.width  = w;
    f.height = h;
    f.pts    = 0.0;
}
void VideoPlayer::FreeFrameBuffers()
{
    for(int i = 0; i < kBufCount; i++) {
        free(buf_[i].data);
        buf_[i].data   = NULL;
        buf_[i].width  = 0;
        buf_[i].height = 0;
    }
}

// ── Open ──────────────────────────────────────────────────────
void VideoPlayer::Open(const std::string& url, VideoQuality q)
{
    Close(); // stop any current playback

    strncpy(curUrl_, url.c_str(), sizeof(curUrl_)-1);
    curQ_    = q;
    errMsg_[0] = '\0';
    pos_     = 0.0;
    dur_     = 0.0;
    InterlockedExchange(&stopFlag_,  0);
    InterlockedExchange(&seekReq_,   0);
    InterlockedExchange(&pauseReq_,  0);
    InterlockedExchange(&writeIdx_,  0);
    InterlockedExchange(&readIdx_,   0);
    SetState(VS_LOADING);

    decodeThread_ = CreateThread(NULL, 0, DecodeThreadProc, this, 0, NULL);
    if(!decodeThread_) {
        SetError("Failed to create decode thread");
    }
}

// ── SetQuality ────────────────────────────────────────────────
void VideoPlayer::SetQuality(VideoQuality q)
{
    if(q == curQ_) return;
    if(curUrl_[0] == '\0') { curQ_ = q; return; }
    std::string url(curUrl_);
    Open(url, q); // re-open same URL at new quality
}

// ── Close ─────────────────────────────────────────────────────
void VideoPlayer::Close()
{
    if(decodeThread_) {
        InterlockedExchange(&stopFlag_, 1);
        // Unpause so thread can exit
        InterlockedExchange(&pauseReq_, 0);
        WaitForSingleObject(decodeThread_, 5000);
        CloseHandle(decodeThread_);
        decodeThread_ = NULL;
    }
    SetState(VS_IDLE);
    pos_ = 0.0;
    dur_ = 0.0;
}

// ── SetPaused ─────────────────────────────────────────────────
void VideoPlayer::SetPaused(bool paused)
{
    InterlockedExchange(&pauseReq_, paused ? 1 : 0);
    if(!paused && GetState() == VS_PAUSED)
        SetState(VS_PLAYING);
    else if(paused && GetState() == VS_PLAYING)
        SetState(VS_PAUSED);
}

// ── Seek ──────────────────────────────────────────────────────
void VideoPlayer::Seek(float t)
{
    if(t < 0.f) t = 0.f;
    if(t > 1.f) t = 1.f;
    EnterCriticalSection(&cs_);
    seekPos_ = t;
    LeaveCriticalSection(&cs_);
    InterlockedExchange(&seekReq_, 1);
}

// ── OnLostDevice / OnResetDevice ─────────────────────────────
void VideoPlayer::OnLostDevice()
{
    if(tex_) { tex_->Release(); tex_ = NULL; }
}
void VideoPlayer::OnResetDevice(LPDIRECT3DDEVICE9 dev)
{
    // Texture will be recreated on next Update() call
    texW_ = 0;
    texH_ = 0;
    (void)dev;
}

// ── Update (GUI thread) ───────────────────────────────────────
bool VideoPlayer::Update(LPDIRECT3DDEVICE9 dev)
{
    if(!dev) return false;

    // Check if decode thread produced a new frame
    LONG w = writeIdx_;
    LONG r = readIdx_;
    // writeIdx_ is advanced by decode thread after writing buf_[(w-1) % kBufCount]
    // We present the last fully written frame
    int presentIdx = ((int)w - 1 + kBufCount) % kBufCount;
    if(w == 0) return false; // no frame yet
    if(w == r) return false; // nothing new

    EnterCriticalSection(&cs_);
    VideoFrame& f = buf_[presentIdx];
    if(!f.data || f.width <= 0 || f.height <= 0) {
        LeaveCriticalSection(&cs_);
        return false;
    }
    int fw = f.width;
    int fh = f.height;
    pos_   = f.pts;

    // (Re)create texture if size changed
    if(!tex_ || texW_ != fw || texH_ != fh) {
        if(tex_) { tex_->Release(); tex_ = NULL; }
        HRESULT hr = dev->CreateTexture(
            fw, fh, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &tex_, NULL);
        if(FAILED(hr)) {
            LeaveCriticalSection(&cs_);
            return false;
        }
        texW_ = fw;
        texH_ = fh;
        vidW_ = fw;
        vidH_ = fh;
    }

    // Upload frame pixels
    D3DLOCKED_RECT lr;
    if(SUCCEEDED(tex_->LockRect(0, &lr, NULL, D3DLOCK_DISCARD))) {
        const int srcPitch = fw * 4;
        if(lr.Pitch == srcPitch) {
            memcpy(lr.pBits, f.data, fh * srcPitch);
        } else {
            unsigned char* dst = (unsigned char*)lr.pBits;
            unsigned char* src = f.data;
            for(int row = 0; row < fh; row++) {
                memcpy(dst, src, srcPitch);
                dst += lr.Pitch;
                src += srcPitch;
            }
        }
        tex_->UnlockRect(0);
    }

    InterlockedExchange(&readIdx_, w); // acknowledge frame
    LeaveCriticalSection(&cs_);
    return true;
}

// ── yt-dlp URL resolution ─────────────────────────────────────
//  Runs: yt-dlp.exe -f <format> --get-url <ytUrl>
//  Captures stdout (one or two lines: video URL [\n audio URL])
bool VideoPlayer::ResolveUrl(
    const char* ytUrl, VideoQuality q,
    char* outVideoUrl, int videoUrlLen,
    char* outAudioUrl, int audioUrlLen)
{
    outVideoUrl[0] = '\0';
    outAudioUrl[0] = '\0';

    const char* fmt = QualityToHeightFilter(q);

    // Build command line
    char cmd[4096];
    _snprintf(cmd, sizeof(cmd)-1,
        "\"%s\" -f \"%s\" -S vcodec:h264,ext:mp4 --get-url \"%s\" --no-playlist",
        ytdlpPath_, fmt, ytUrl);

    // Set up pipe
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE hRead = NULL, hWrite = NULL;
    if(!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdOutput  = hWrite;
    si.hStdError   = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(
        NULL, cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWrite);
    if(!ok) { CloseHandle(hRead); return false; }

    // Read output
    char buf[8192]; buf[0] = '\0';
    DWORD totalRead = 0, bytesRead = 0;
    while(ReadFile(hRead, buf + totalRead,
                   (DWORD)(sizeof(buf)-1-totalRead), &bytesRead, NULL)
          && bytesRead > 0) {
        totalRead += bytesRead;
        if(totalRead >= sizeof(buf)-1) break;
    }
    buf[totalRead] = '\0';
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if(totalRead == 0) return false;

    // Split lines
    char* nl = strchr(buf, '\n');
    if(nl) {
        int len1 = (int)(nl - buf);
        if(len1 > 0 && buf[len1-1] == '\r') len1--;
        if(len1 > 0 && len1 < videoUrlLen) {
            strncpy(outVideoUrl, buf, len1);
            outVideoUrl[len1] = '\0';
        }
        // second line = audio URL (if any)
        char* line2 = nl + 1;
        int len2 = (int)strlen(line2);
        if(len2 > 0 && line2[len2-1] == '\n') len2--;
        if(len2 > 0 && line2[len2-1] == '\r') len2--;
        if(len2 > 0 && len2 < audioUrlLen) {
            strncpy(outAudioUrl, line2, len2);
            outAudioUrl[len2] = '\0';
        }
    } else {
        // single URL
        int len = (int)strlen(buf);
        if(len > 0 && buf[len-1] == '\n') len--;
        if(len > 0 && buf[len-1] == '\r') len--;
        if(len > 0 && len < videoUrlLen) {
            strncpy(outVideoUrl, buf, len);
            outVideoUrl[len] = '\0';
        }
    }
    return (outVideoUrl[0] != '\0');
}

// ── Decode thread entry ───────────────────────────────────────
DWORD WINAPI VideoPlayer::DecodeThreadProc(LPVOID param)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    VideoPlayer* self = (VideoPlayer*)param;
    self->DecodeLoop();
    CoUninitialize();
    return 0;
}

// ── Main decode loop ──────────────────────────────────────────
void VideoPlayer::DecodeLoop()
{
    // 1. Resolve YouTube URL via yt-dlp
    char videoUrl[4096] = {};
    char audioUrl[4096] = {};
    if(!ResolveUrl(curUrl_, curQ_, videoUrl, sizeof(videoUrl),
                   audioUrl, sizeof(audioUrl))) {
        SetError("yt-dlp failed to resolve URL");
        return;
    }
    if(stopFlag_) return;

    // 2. Open video stream with FFmpeg
    AVFormatContext* fmtCtx = NULL;
    AVDictionary*    opts   = NULL;
    // Give FFmpeg a generous timeout for network streams
    av_dict_set(&opts, "timeout",          "10000000", 0); // 10s microseconds
    av_dict_set(&opts, "reconnect",        "1",        0);
    av_dict_set(&opts, "reconnect_streamed","1",        0);

    if(avformat_open_input(&fmtCtx, videoUrl, NULL, &opts) < 0) {
        av_dict_free(&opts);
        SetError("avformat_open_input failed");
        return;
    }
    av_dict_free(&opts);

    if(avformat_find_stream_info(fmtCtx, NULL) < 0) {
        avformat_close_input(&fmtCtx);
        SetError("avformat_find_stream_info failed");
        return;
    }

    // 3. Find video stream
    int videoStreamIdx = -1;
    for(unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if(fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = (int)i;
            break;
        }
    }
    if(videoStreamIdx < 0) {
        avformat_close_input(&fmtCtx);
        SetError("No video stream found");
        return;
    }

    AVStream*    vStream  = fmtCtx->streams[videoStreamIdx];
    AVCodecParameters* cp = vStream->codecpar;

    // 4. Open codec
    const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
    if(!codec) {
        avformat_close_input(&fmtCtx);
        SetError("No decoder found");
        return;
    }
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, cp);
    // Single-threaded to keep Atom N450 pressure lower
    codecCtx->thread_count = 1;
    if(avcodec_open2(codecCtx, codec, NULL) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        SetError("avcodec_open2 failed");
        return;
    }

    int fw = codecCtx->width;
    int fh = codecCtx->height;
    dur_   = (fmtCtx->duration != AV_NOPTS_VALUE)
             ? (double)fmtCtx->duration / AV_TIME_BASE
             : 0.0;

    // Pre-allocate both frame buffers
    EnterCriticalSection(&cs_);
    AllocFrameBuffer(0, fw, fh);
    AllocFrameBuffer(1, fw, fh);
    LeaveCriticalSection(&cs_);

    // 5. SWS context: decode native → BGRA
    SwsContext* swsCtx = sws_getContext(
        fw, fh, codecCtx->pix_fmt,
        fw, fh, AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if(!swsCtx) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        SetError("sws_getContext failed");
        return;
    }

    AVFrame*  frame  = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    AVRational tb    = vStream->time_base;

    SetState(VS_PLAYING);
    InterlockedExchange(&writeIdx_, 0);
    InterlockedExchange(&readIdx_,  0);

    // 6. Decode loop
    while(!stopFlag_) {
        // Handle pause
        while(pauseReq_ && !stopFlag_) Sleep(10);
        if(stopFlag_) break;

        // Handle seek
        if(seekReq_) {
            float t = 0.f;
            EnterCriticalSection(&cs_);
            t = seekPos_;
            LeaveCriticalSection(&cs_);
            if(dur_ > 0.0) {
                int64_t ts = (int64_t)(t * dur_ * AV_TIME_BASE);
                av_seek_frame(fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecCtx);
            }
            InterlockedExchange(&seekReq_, 0);
        }

        int ret = av_read_frame(fmtCtx, packet);
        if(ret == AVERROR_EOF) {
            SetState(VS_EOF);
            break;
        }
        if(ret < 0) break;

        if(packet->stream_index != videoStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

        avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);

        while(avcodec_receive_frame(codecCtx, frame) == 0 && !stopFlag_) {
            // Determine write slot
            int slot = (int)(writeIdx_) % kBufCount;

            // Compute pts in seconds
            double pts = 0.0;
            if(frame->pts != AV_NOPTS_VALUE)
                pts = (double)frame->pts * av_q2d(tb);
            else if(frame->best_effort_timestamp != AV_NOPTS_VALUE)
                pts = (double)frame->best_effort_timestamp * av_q2d(tb);

            // Convert to BGRA in-place into ring buffer slot
            EnterCriticalSection(&cs_);
            if(buf_[slot].data && buf_[slot].width == fw && buf_[slot].height == fh) {
                uint8_t* dstData[1] = { buf_[slot].data };
                int      dstLinesize[1] = { fw * 4 };
                sws_scale(swsCtx,
                    (const uint8_t* const*)frame->data, frame->linesize,
                    0, fh,
                    dstData, dstLinesize);
                buf_[slot].pts = pts;
            }
            LeaveCriticalSection(&cs_);

            // Advance write index (GUI thread will pick it up)
            InterlockedExchange(&writeIdx_, (LONG)((slot + 1) % kBufCount + 1));
            // Simple frame pacing: sleep if GUI hasn't consumed yet
            // This prevents the decode thread from starving the GUI thread
            int spins = 0;
            while(writeIdx_ != readIdx_ && !stopFlag_ && spins < 40) {
                Sleep(1);
                spins++;
            }

            av_frame_unref(frame);
        }
    }

    // 7. Cleanup
    av_frame_free(&frame);
    av_packet_free(&packet);
    sws_freeContext(swsCtx);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    if(GetState() != VS_ERROR && GetState() != VS_EOF)
        SetState(VS_IDLE);
}
