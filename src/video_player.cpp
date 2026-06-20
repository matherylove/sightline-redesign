// ============================================================
//  video_player.cpp  —  Sightline PoC video backend
//  Target: Windows XP SP3 x86 (WINVER=0x0501) and later
//
//  Build requirements:
//    - FFmpeg 7.1 xpmod-sse win32 dev pkg extracted to ffmpeg-xp/
//      Headers: ffmpeg-xp/include/
//      Static libs: ffmpeg-xp/lib/  (libav*.a, libsw*.a)
//    - CMakeLists.txt already links: avcodec avformat avutil
//                                    swscale swresample winmm
//    - yt-dlp.exe (nicolaasjan fork) beside the binary
// ============================================================

#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>   // waveOut — needs winmm at link time
#include <d3d9.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "video_player.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// ─── helpers ─────────────────────────────────────────────────
static inline void SafeFree(void** p)
{ if(*p){ free(*p); *p=NULL; } }

// ─── ctor / dtor ─────────────────────────────────────────────
VideoPlayer::VideoPlayer()
    : state_(VS_IDLE), decodeThread_(NULL)
    , stopFlag_(0), pauseFlag_(0), seekFlag_(0), seekTarget_(0.0)
    , posSeconds_(0.0), durSeconds_(0.0)
    , vidW_(0), vidH_(0)
    , tex_(NULL), texW_(0), texH_(0)
    , hWaveOut_(NULL), waveBufIdx_(0)
    , audioChannels_(0), audioSampleRate_(0)
    , writeIdx_(0), readIdx_(0)
{
    InitializeCriticalSection(&ringCs_);
    for(int i=0;i<kRingSize;i++){
        ring_[i].rgb = NULL;
        ring_[i].pts = 0.0;
        ring_[i].w   = 0;
        ring_[i].h   = 0;
    }
    for(int i=0;i<kWaveBufs;i++){
        waveBuf_[i]  = NULL;
        memset(&waveHdr_[i],0,sizeof(waveHdr_[i]));
    }
}

VideoPlayer::~VideoPlayer()
{
    Close();
    DeleteCriticalSection(&ringCs_);
}

// ─── Close ───────────────────────────────────────────────────
void VideoPlayer::Close()
{
    // Signal decode thread to stop
    InterlockedExchange(&stopFlag_, 1);
    if(decodeThread_){
        WaitForSingleObject(decodeThread_, 8000);
        CloseHandle(decodeThread_);
        decodeThread_ = NULL;
    }
    InterlockedExchange(&stopFlag_, 0);
    InterlockedExchange(&pauseFlag_, 0);
    InterlockedExchange(&seekFlag_, 0);

    AudioClose();

    // Release ring buffer frames
    EnterCriticalSection(&ringCs_);
    for(int i=0;i<kRingSize;i++){ SafeFree((void**)&ring_[i].rgb); }
    writeIdx_ = readIdx_ = 0;
    LeaveCriticalSection(&ringCs_);

    // Release D3D9 texture
    if(tex_){ tex_->Release(); tex_=NULL; }
    texW_ = texH_ = 0;

    posSeconds_ = durSeconds_ = 0.0;
    vidW_ = vidH_ = 0;
    state_ = VS_IDLE;
    errorMsg_.clear();
    title_.clear();
    videoUrl_.clear();
    audioUrl_.clear();
}

// ─── SetPaused ────────────────────────────────────────────────
void VideoPlayer::SetPaused(bool p)
{
    InterlockedExchange(&pauseFlag_, p ? 1 : 0);
    if(state_ == VS_PLAYING && p) state_ = VS_PAUSED;
    if(state_ == VS_PAUSED  && !p) state_ = VS_PLAYING;
}

// ─── Seek ─────────────────────────────────────────────────────
void VideoPlayer::Seek(float fraction)
{
    if(durSeconds_ <= 0.0) return;
    seekTarget_ = fraction * durSeconds_;
    InterlockedExchange(&seekFlag_, 1);
}

// ─── Device lost / reset ─────────────────────────────────────
void VideoPlayer::OnDeviceLost()
{
    // D3DPOOL_MANAGED textures survive device loss on most hardware,
    // but to be safe we release and will recreate on next Upload.
    if(tex_){ tex_->Release(); tex_=NULL; }
    texW_ = texH_ = 0;
}
void VideoPlayer::OnDeviceReset(LPDIRECT3DDEVICE9)
{
    // Texture will be recreated lazily on next UploadFrame call.
}

// ─── ResolveYtUrl ─────────────────────────────────────────────
// Runs yt-dlp.exe synchronously (blocking, but called from
// the decode thread, not the render thread).
bool VideoPlayer::ResolveYtUrl(const std::string& ytUrl,
                                std::string& videoUrl,
                                std::string& audioUrl)
{
    // We ask for best h264 video + best audio, separate streams.
    // yt-dlp prints one URL per line (video first, audio second).
    std::string cmd =
        "\"" + ytdlpExe_ + "\""
        " -f \"bestvideo[ext=mp4][vcodec^=avc1]+bestaudio/best\""
        " --no-playlist"
        " --no-warnings"
        " --get-url "
        " \"" + ytUrl + "\"";

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

    // cmd buffer must be writable for CreateProcessA
    char cmdBuf[2048];
    strncpy(cmdBuf, cmd.c_str(), sizeof(cmdBuf)-1);
    cmdBuf[sizeof(cmdBuf)-1] = '\0';

    BOOL ok = CreateProcessA(
        NULL, cmdBuf, NULL, NULL,
        TRUE,            // inherit handles
        CREATE_NO_WINDOW,
        NULL, NULL, &si, &pi);

    CloseHandle(hWrite); // parent must close its write end

    if(!ok){
        CloseHandle(hRead);
        return false;
    }

    // Read yt-dlp output (up to 8 KB)
    std::string output;
    output.reserve(512);
    char buf[512];
    DWORD bytesRead = 0;
    while(ReadFile(hRead, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0){
        buf[bytesRead] = '\0';
        output += buf;
        if(stopFlag_) break;
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Parse lines
    std::string line;
    int lineNo = 0;
    for(size_t i = 0; i <= output.size(); i++){
        char c = (i < output.size()) ? output[i] : '\n';
        if(c == '\r') continue;
        if(c == '\n'){
            if(!line.empty()){
                if(lineNo == 0) videoUrl = line;
                else            audioUrl = line;
                lineNo++;
            }
            line.clear();
        } else {
            line += c;
        }
    }

    // If only one URL was returned, it carries both video+audio
    if(!videoUrl.empty() && audioUrl.empty())
        audioUrl = videoUrl;

    return !videoUrl.empty();
}

// ─── AudioInit ────────────────────────────────────────────────
void VideoPlayer::AudioInit(int sampleRate, int channels)
{
    if(hWaveOut_) AudioClose();

    audioSampleRate_ = sampleRate;
    audioChannels_   = channels;

    WAVEFORMATEX wfx;
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)channels;
    wfx.nSamplesPerSec  = (DWORD)sampleRate;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (WORD)(channels * 2);
    wfx.nAvgBytesPerSec = (DWORD)(sampleRate * channels * 2);
    wfx.cbSize          = 0;

    if(waveOutOpen(&hWaveOut_, WAVE_MAPPER, &wfx,
                   0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR){
        hWaveOut_ = NULL;
        return;
    }

    int bufBytes = kWaveSamples * channels * 2;
    for(int i = 0; i < kWaveBufs; i++){
        waveBuf_[i] = (short*)malloc(bufBytes);
        memset(waveBuf_[i], 0, bufBytes);
        waveHdr_[i].lpData          = (LPSTR)waveBuf_[i];
        waveHdr_[i].dwBufferLength  = (DWORD)bufBytes;
        waveHdr_[i].dwFlags         = 0;
        waveOutPrepareHeader(hWaveOut_, &waveHdr_[i], sizeof(WAVEHDR));
    }
    waveBufIdx_ = 0;
}

// ─── AudioFeed ────────────────────────────────────────────────
void VideoPlayer::AudioFeed(const short* pcm, int frames)
{
    if(!hWaveOut_ || pauseFlag_) return;

    WAVEHDR* hdr = &waveHdr_[waveBufIdx_];
    // Wait until this buffer is done playing
    while(!(hdr->dwFlags & WHDR_DONE) && !(hdr->dwFlags & 0))
    {
        // On first use dwFlags==0 (not yet queued) — fall through
        if(hdr->dwFlags == 0) break;
        if(hdr->dwFlags & WHDR_DONE) break;
        if(stopFlag_) return;
        Sleep(1);
    }

    int bufFrames = kWaveSamples;
    int samples   = frames * audioChannels_;
    if(samples > bufFrames * audioChannels_)
        samples = bufFrames * audioChannels_;

    memcpy(waveBuf_[waveBufIdx_], pcm, samples * 2);
    hdr->dwBufferLength = (DWORD)(samples * 2);
    hdr->dwFlags &= ~WHDR_DONE;

    waveOutWrite(hWaveOut_, hdr, sizeof(WAVEHDR));
    waveBufIdx_ = (waveBufIdx_ + 1) % kWaveBufs;
}

// ─── AudioClose ───────────────────────────────────────────────
void VideoPlayer::AudioClose()
{
    if(!hWaveOut_) return;
    waveOutReset(hWaveOut_);
    for(int i = 0; i < kWaveBufs; i++){
        waveOutUnprepareHeader(hWaveOut_, &waveHdr_[i], sizeof(WAVEHDR));
        SafeFree((void**)&waveBuf_[i]);
        memset(&waveHdr_[i], 0, sizeof(waveHdr_[i]));
    }
    waveOutClose(hWaveOut_);
    hWaveOut_ = NULL;
}

// ─── Open ─────────────────────────────────────────────────────
bool VideoPlayer::Open(const std::string& ytUrl,
                        LPDIRECT3DDEVICE9  dev,
                        const std::string& ytdlpExe,
                        const std::string& /*ffmpegBin*/)
{
    Close();
    ytdlpExe_ = ytdlpExe;
    state_    = VS_LOADING;

    // Store URL for the thread
    videoUrl_ = ytUrl; // re-used as the raw YT URL until resolved

    // Start the decode thread; it will call ResolveYtUrl internally.
    // We pass 'this' as the param.
    (void)dev; // device is used inside Update(), not here
    decodeThread_ = CreateThread(
        NULL, 0, DecodeThreadProc, this, 0, NULL);
    if(!decodeThread_){
        state_ = VS_ERROR;
        errorMsg_ = "CreateThread failed";
        return false;
    }
    return true;
}

// ─── DecodeThreadProc (static trampoline) ────────────────────
DWORD WINAPI VideoPlayer::DecodeThreadProc(LPVOID param)
{
    VideoPlayer* self = static_cast<VideoPlayer*>(param);
    self->DecodeLoop();
    return 0;
}

// ─── DecodeLoop ───────────────────────────────────────────────
void VideoPlayer::DecodeLoop()
{
    // ── Step 1: resolve YouTube URL via yt-dlp ────────────────
    std::string rawYt = videoUrl_; // saved in Open()
    videoUrl_.clear();
    audioUrl_.clear();

    if(!ResolveYtUrl(rawYt, videoUrl_, audioUrl_)){
        state_   = VS_ERROR;
        errorMsg_ = "yt-dlp failed to resolve URL";
        return;
    }
    if(stopFlag_) return;

    // ── Step 2: open video stream with FFmpeg ─────────────────
    AVFormatContext* fmtCtx = NULL;
    AVFormatContext* afmtCtx = NULL; // separate audio context

    AVDictionary* opts = NULL;
    av_dict_set(&opts, "reconnect",        "1", 0);
    av_dict_set(&opts, "reconnect_streamed","1", 0);
    av_dict_set(&opts, "timeout",          "15000000", 0); // 15s in µs

    if(avformat_open_input(&fmtCtx, videoUrl_.c_str(), NULL, &opts) < 0){
        av_dict_free(&opts);
        state_   = VS_ERROR;
        errorMsg_ = "avformat_open_input (video) failed";
        return;
    }
    av_dict_free(&opts);

    if(avformat_find_stream_info(fmtCtx, NULL) < 0){
        avformat_close_input(&fmtCtx);
        state_   = VS_ERROR;
        errorMsg_ = "avformat_find_stream_info failed";
        return;
    }

    // ── Find video + audio stream indices ─────────────────────
    int vIdx = -1, aIdx = -1;
    for(unsigned i = 0; i < fmtCtx->nb_streams; i++){
        AVMediaType t = fmtCtx->streams[i]->codecpar->codec_type;
        if(t == AVMEDIA_TYPE_VIDEO && vIdx < 0) vIdx = (int)i;
        if(t == AVMEDIA_TYPE_AUDIO && aIdx < 0) aIdx = (int)i;
    }

    // If audio is in a separate URL, open it
    bool separateAudio = (audioUrl_ != videoUrl_) && aIdx < 0;
    if(separateAudio){
        AVDictionary* aopts = NULL;
        av_dict_set(&aopts, "reconnect",        "1", 0);
        av_dict_set(&aopts, "reconnect_streamed","1", 0);
        if(avformat_open_input(&afmtCtx, audioUrl_.c_str(), NULL, &aopts) == 0){
            avformat_find_stream_info(afmtCtx, NULL);
            for(unsigned i = 0; i < afmtCtx->nb_streams; i++){
                if(afmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
                    aIdx = (int)i | 0x1000; // mark as "from afmtCtx"
                    break;
                }
            }
        }
        av_dict_free(&aopts);
    }

    if(vIdx < 0){
        avformat_close_input(&fmtCtx);
        if(afmtCtx) avformat_close_input(&afmtCtx);
        state_   = VS_ERROR;
        errorMsg_ = "No video stream found";
        return;
    }

    // ── Open video codec ──────────────────────────────────────
    AVCodecParameters* vpar  = fmtCtx->streams[vIdx]->codecpar;
    const AVCodec*     vCodec = avcodec_find_decoder(vpar->codec_id);
    AVCodecContext*    vCtx   = avcodec_alloc_context3(vCodec);
    avcodec_parameters_to_context(vCtx, vpar);
    vCtx->thread_count = 2;
    if(avcodec_open2(vCtx, vCodec, NULL) < 0){
        avcodec_free_context(&vCtx);
        avformat_close_input(&fmtCtx);
        if(afmtCtx) avformat_close_input(&afmtCtx);
        state_   = VS_ERROR;
        errorMsg_ = "Could not open video codec";
        return;
    }

    vidW_ = vCtx->width;
    vidH_ = vCtx->height;

    // Duration
    if(fmtCtx->duration != AV_NOPTS_VALUE)
        durSeconds_ = (double)fmtCtx->duration / AV_TIME_BASE;

    // ── Open audio codec (optional) ───────────────────────────
    AVFormatContext* aFmtToUse = NULL;
    int              aIdxReal  = -1;
    AVCodecContext*  aCtx      = NULL;
    SwrContext*      swrCtx    = NULL;

    if(aIdx >= 0){
        aFmtToUse = (aIdx & 0x1000) ? afmtCtx : fmtCtx;
        aIdxReal  = aIdx & 0x0FFF;
        AVCodecParameters* apar  = aFmtToUse->streams[aIdxReal]->codecpar;
        const AVCodec*     aCodec = avcodec_find_decoder(apar->codec_id);
        aCtx = avcodec_alloc_context3(aCodec);
        avcodec_parameters_to_context(aCtx, apar);
        if(avcodec_open2(aCtx, aCodec, NULL) < 0){
            avcodec_free_context(&aCtx);
            aCtx = NULL;
        } else {
            // waveOut: stereo 44100 s16
            int outRate = 44100, outCh = 2;
            AudioInit(outRate, outCh);

            // Build SwrContext using channel counts (XP-compatible,
            // avoids AVChannelLayout which requires FFmpeg >= 5.1 API)
            swrCtx = swr_alloc_set_opts(
                NULL,
                // out
                (int64_t)AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, outRate,
                // in  (use default layout for channel count)
                av_get_default_channel_layout(aCtx->channels),
                aCtx->sample_fmt, aCtx->sample_rate,
                0, NULL);
            swr_init(swrCtx);
        }
    }

    // ── SwsContext: video → RGB24 ──────────────────────────────
    SwsContext* swsCtx = sws_getContext(
        vCtx->width, vCtx->height, vCtx->pix_fmt,
        vCtx->width, vCtx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
    if(!swsCtx){
        avcodec_free_context(&vCtx);
        if(aCtx) avcodec_free_context(&aCtx);
        if(swrCtx) swr_free(&swrCtx);
        avformat_close_input(&fmtCtx);
        if(afmtCtx) avformat_close_input(&afmtCtx);
        AudioClose();
        state_   = VS_ERROR;
        errorMsg_ = "sws_getContext failed";
        return;
    }

    // ── Allocate decode frames ────────────────────────────────
    AVFrame* vFrame   = av_frame_alloc();
    AVFrame* aFrame   = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    int rgbSize = av_image_get_buffer_size(
                    AV_PIX_FMT_RGB24, vCtx->width, vCtx->height, 1);
    unsigned char* rgbBuf = (unsigned char*)av_malloc(rgbSize);
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize,
                         rgbBuf,
                         AV_PIX_FMT_RGB24,
                         vCtx->width, vCtx->height, 1);

    // Audio PCM buffer (2*kWaveSamples per channel, stereo s16)
    int pcmCapacity = kWaveSamples * 2 * 2;
    short* pcmBuf   = (short*)malloc(pcmCapacity * sizeof(short));

    // ── Timing ────────────────────────────────────────────────
    // Use wall-clock to pace video delivery.
    // startWall = the real time (µs) when pts=0 should display.
    double startWall = (double)av_gettime_relative() * 1e-6;
    AVRational vTb   = fmtCtx->streams[vIdx]->time_base;

    AVPacket* pkt = av_packet_alloc();

    state_ = VS_PLAYING;

    // ── Main decode loop ──────────────────────────────────────
    while(!stopFlag_){
        // Pause: spin-wait cheaply
        if(pauseFlag_){
            Sleep(5);
            continue;
        }

        // Seek request
        if(seekFlag_){
            InterlockedExchange(&seekFlag_, 0);
            int64_t ts = (int64_t)(seekTarget_ * AV_TIME_BASE);
            av_seek_frame(fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(vCtx);
            if(aCtx) avcodec_flush_buffers(aCtx);
            if(afmtCtx) av_seek_frame(afmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
            posSeconds_  = seekTarget_;
            startWall    = (double)av_gettime_relative() * 1e-6 - seekTarget_;
        }

        // Read one packet
        int ret = av_read_frame(fmtCtx, pkt);
        if(ret == AVERROR_EOF){
            state_ = VS_EOF;
            break;
        }
        if(ret < 0){ Sleep(1); continue; }

        // ── Video packet ──────────────────────────────────────
        if(pkt->stream_index == vIdx){
            avcodec_send_packet(vCtx, pkt);
            while(avcodec_receive_frame(vCtx, vFrame) == 0){
                if(stopFlag_) goto done;

                // Compute presentation time
                double pts = 0.0;
                if(vFrame->best_effort_timestamp != AV_NOPTS_VALUE)
                    pts = vFrame->best_effort_timestamp
                          * av_q2d(vTb);
                posSeconds_ = pts;

                // ── A/V sync: sleep until display time ────────
                double wallNow = (double)av_gettime_relative() * 1e-6;
                double targetT = startWall + pts;
                double diff    = targetT - wallNow;
                if(diff > 0.002 && diff < 0.5){
                    DWORD ms = (DWORD)(diff * 1000.0);
                    Sleep(ms);
                }
                if(stopFlag_) goto done;
                if(pauseFlag_) goto done; // recheck at top of loop

                // ── Convert to RGB24 ──────────────────────────
                sws_scale(swsCtx,
                    (const uint8_t* const*)vFrame->data,
                    vFrame->linesize, 0, vCtx->height,
                    rgbFrame->data, rgbFrame->linesize);

                // ── Push to ring buffer ───────────────────────
                int nextWrite = (writeIdx_ + 1) % kRingSize;
                // If buffer is full, overwrite the oldest readable frame
                // (better to drop than to block the decode thread).
                EnterCriticalSection(&ringCs_);
                int slot = (int)writeIdx_;
                if(ring_[slot].rgb) free(ring_[slot].rgb);
                ring_[slot].rgb = (unsigned char*)malloc(rgbSize);
                if(ring_[slot].rgb){
                    memcpy(ring_[slot].rgb, rgbBuf, rgbSize);
                    ring_[slot].pts = pts;
                    ring_[slot].w   = vCtx->width;
                    ring_[slot].h   = vCtx->height;
                    InterlockedExchange(&writeIdx_, nextWrite);
                }
                LeaveCriticalSection(&ringCs_);

                av_frame_unref(vFrame);
            }
        }
        // ── Audio packet ──────────────────────────────────────
        else if(aCtx && pkt->stream_index == aIdxReal && aFmtToUse == fmtCtx){
            avcodec_send_packet(aCtx, pkt);
            while(avcodec_receive_frame(aCtx, aFrame) == 0){
                if(stopFlag_) goto done;

                int outFrames = swr_convert(
                    swrCtx,
                    (uint8_t**)&pcmBuf, kWaveSamples,
                    (const uint8_t**)aFrame->data,
                    aFrame->nb_samples);
                if(outFrames > 0)
                    AudioFeed(pcmBuf, outFrames);

                av_frame_unref(aFrame);
            }
        }

        av_packet_unref(pkt);
    }

done:
    // ── Cleanup ───────────────────────────────────────────────
    av_packet_free(&pkt);
    av_free(rgbBuf);
    av_frame_free(&vFrame);
    av_frame_free(&aFrame);
    av_frame_free(&rgbFrame);
    free(pcmBuf);
    sws_freeContext(swsCtx);
    if(swrCtx) swr_free(&swrCtx);
    avcodec_free_context(&vCtx);
    if(aCtx) avcodec_free_context(&aCtx);
    avformat_close_input(&fmtCtx);
    if(afmtCtx) avformat_close_input(&afmtCtx);
    AudioClose();

    if(state_ == VS_PLAYING) state_ = VS_EOF;
}

// ─── UploadFrame ─────────────────────────────────────────────
bool VideoPlayer::UploadFrame(LPDIRECT3DDEVICE9 dev,
                               const unsigned char* rgb,
                               int w, int h)
{
    if(!dev || !rgb || w<=0 || h<=0) return false;

    // (Re)create texture if size changed or not yet created
    if(!tex_ || texW_ != w || texH_ != h){
        if(tex_){ tex_->Release(); tex_=NULL; }
        HRESULT hr = dev->CreateTexture(
            (UINT)w, (UINT)h, 1, 0,
            D3DFMT_X8R8G8B8,   // no alpha needed for video
            D3DPOOL_MANAGED,    // survives device reset on XP
            &tex_, NULL);
        if(FAILED(hr)) return false;
        texW_ = w;
        texH_ = h;
    }

    D3DLOCKED_RECT lr;
    if(FAILED(tex_->LockRect(0, &lr, NULL, D3DLOCK_DISCARD)))
        return false;

    // Convert RGB24 → XRGB8 (D3D row-major)
    const unsigned char* src = rgb;
    BYTE* dst = (BYTE*)lr.pBits;
    for(int y = 0; y < h; y++){
        BYTE* row = dst + y * lr.Pitch;
        for(int x = 0; x < w; x++){
            row[x*4+0] = src[2]; // B
            row[x*4+1] = src[1]; // G
            row[x*4+2] = src[0]; // R
            row[x*4+3] = 0xFF;   // X
            src += 3;
        }
    }
    tex_->UnlockRect(0);
    return true;
}

// ─── Update (render-thread call) ─────────────────────────────
bool VideoPlayer::Update(LPDIRECT3DDEVICE9 dev)
{
    if(state_ == VS_IDLE || state_ == VS_LOADING) return false;

    // Check if the decode thread has a new frame for us
    int rIdx = (int)readIdx_;
    int wIdx = (int)writeIdx_;
    if(rIdx == wIdx) return false; // no new frame

    EnterCriticalSection(&ringCs_);
    DecodedFrame& f = ring_[rIdx];
    bool ok = false;
    if(f.rgb && f.w > 0 && f.h > 0){
        ok = UploadFrame(dev, f.rgb, f.w, f.h);
        free(f.rgb); f.rgb = NULL;
        InterlockedExchange(&readIdx_, (rIdx + 1) % kRingSize);
    }
    LeaveCriticalSection(&ringCs_);
    return ok;
}
