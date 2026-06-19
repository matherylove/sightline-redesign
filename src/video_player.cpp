// ============================================================
//  video_player.cpp  —  Sightline PoC video backend
//  FFmpeg XP-mod (7.1 win32 dev) + yt-dlp (nicolaasjan)
//  waveOut audio — Windows XP SP3 x86 compatible
// ============================================================
#ifndef WINVER
#  define WINVER       0x0501
#  define _WIN32_WINNT 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <d3d9.h>

// FFmpeg C headers — must be extern "C" in C++ translation unit
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "video_player.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// ── Global singleton ─────────────────────────────────────────
VideoPlayer g_player;

// ── Constructor ──────────────────────────────────────────────
VideoPlayer::VideoPlayer()
{
    ZeroMemory(err_buf_, sizeof(err_buf_));
    ZeroMemory(ring_, sizeof(ring_));
    ZeroMemory(aud_queue_, sizeof(aud_queue_));
    InitializeCriticalSection(&ring_cs_);
    InitializeCriticalSection(&aud_cs_);
    ring_not_empty_ = CreateEventA(NULL, FALSE, FALSE, NULL);
    ring_not_full_  = CreateEventA(NULL, FALSE, TRUE,  NULL);
    wave_done_event_= CreateEventA(NULL, FALSE, FALSE, NULL);
}

// ── SetError ─────────────────────────────────────────────────
void VideoPlayer::SetError(const char* msg)
{
    strncpy(err_buf_, msg, sizeof(err_buf_) - 1);
    state_ = VS_ERROR;
}

// ─────────────────────────────────────────────────────────────
//  ResolveUrl  — run yt-dlp.exe via CreateProcessA + pipe
//  Returns the direct video URL (and optionally audio URL for
//  DASH streams) into outVideoUrl / outAudioUrl.
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::ResolveUrl(const std::string& ytdlpPath,
                              const std::string& ytUrl,
                              std::string& outVideoUrl,
                              std::string& outAudioUrl)
{
    // Build command line:
    // yt-dlp.exe -f "bestvideo[ext=mp4][vcodec^=avc]+bestaudio/best"
    //            --get-url <ytUrl>
    //
    // The format selector prefers AVC (H.264) because FFmpeg XP-mod
    // has a reliable AVC decoder; VP9/AV1 may work but aren't
    // guaranteed on all XP builds.
    std::string cmd;
    cmd  = "\"";
    cmd += ytdlpPath;
    cmd += "\"";
    cmd += " -f \"bestvideo[ext=mp4][vcodec^=avc]+bestaudio/best\""
            " --no-playlist --get-url \"";
    cmd += ytUrl;
    cmd += "\"";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return false;

    // Ensure the read end is not inherited by the child
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    // CreateProcessA requires a mutable buffer
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(
        NULL, cmdBuf.data(),
        NULL, NULL,
        TRUE,                     // inherit handles
        CREATE_NO_WINDOW,         // no console window on XP
        NULL, NULL,
        &si, &pi
    );
    CloseHandle(hWrite);  // close write end before reading

    if (!ok) {
        CloseHandle(hRead);
        SetError("yt-dlp: CreateProcess failed");
        return false;
    }

    // Read all stdout (max 8 KB — two URLs fit easily)
    std::string output;
    output.reserve(2048);
    char buf[1024];
    DWORD bytesRead = 0;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL)
           && bytesRead > 0)
    {
        buf[bytesRead] = '\0';
        output += buf;
    }
    CloseHandle(hRead);

    // Wait for yt-dlp to finish (give it 60 s)
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0 || output.empty()) {
        SetError("yt-dlp: failed to resolve URL (check network / yt-dlp version)");
        return false;
    }

    // Split output on newlines — yt-dlp returns 1 URL (muxed) or 2
    // (video URL \n audio URL) depending on the format selected.
    size_t nl = output.find('\n');
    if (nl == std::string::npos) {
        // Strip trailing CR/LF
        while (!output.empty() &&
               (output.back() == '\r' || output.back() == '\n'))
            output.pop_back();
        outVideoUrl = output;
        outAudioUrl = "";
    } else {
        outVideoUrl = output.substr(0, nl);
        outAudioUrl = output.substr(nl + 1);
        // Strip CR/LF
        while (!outVideoUrl.empty() &&
               (outVideoUrl.back() == '\r' || outVideoUrl.back() == '\n'))
            outVideoUrl.pop_back();
        while (!outAudioUrl.empty() &&
               (outAudioUrl.back() == '\r' || outAudioUrl.back() == '\n'))
            outAudioUrl.pop_back();
    }

    return !outVideoUrl.empty();
}

// ─────────────────────────────────────────────────────────────
//  OpenStreams — avformat_open_input on the resolved URL(s)
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::OpenStreams(const std::string& videoUrl,
                               const std::string& audioUrl)
{
    // Open the video (or muxed) stream
    if (avformat_open_input(&fmt_ctx_,
                             videoUrl.c_str(),
                             nullptr, nullptr) < 0)
    {
        SetError("avformat: cannot open video URL");
        return false;
    }
    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
        SetError("avformat: cannot find stream info");
        return false;
    }

    // Duration
    if (fmt_ctx_->duration != AV_NOPTS_VALUE)
        dur_sec_ = (double)fmt_ctx_->duration / (double)AV_TIME_BASE;

    // Find best video stream
    const AVCodec* vcodec = nullptr;
    vid_stream_ = av_find_best_stream(
        fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
    if (vid_stream_ < 0 || !vcodec) {
        SetError("avformat: no video stream found");
        return false;
    }

    // Find best audio stream (may be in the same container)
    const AVCodec* acodec = nullptr;
    aud_stream_ = av_find_best_stream(
        fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &acodec, 0);
    // Audio is optional — some streams are video-only

    // Video decoder context
    vdec_ctx_ = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(
        vdec_ctx_,
        fmt_ctx_->streams[vid_stream_]->codecpar);
    vdec_ctx_->thread_count = 1;  // safe on XP, avoids thread API issues
    if (avcodec_open2(vdec_ctx_, vcodec, nullptr) < 0) {
        SetError("avcodec: cannot open video decoder");
        return false;
    }
    vid_w_  = vdec_ctx_->width;
    vid_h_  = vdec_ctx_->height;
    vid_tb_ = av_q2d(fmt_ctx_->streams[vid_stream_]->time_base);

    // Audio decoder context (if stream found)
    if (aud_stream_ >= 0 && acodec) {
        adec_ctx_ = avcodec_alloc_context3(acodec);
        avcodec_parameters_to_context(
            adec_ctx_,
            fmt_ctx_->streams[aud_stream_]->codecpar);
        adec_ctx_->thread_count = 1;
        if (avcodec_open2(adec_ctx_, acodec, nullptr) < 0) {
            avcodec_free_context(&adec_ctx_);
            aud_stream_ = -1;  // audio unavailable, continue without
        } else {
            aud_tb_ = av_q2d(
                fmt_ctx_->streams[aud_stream_]->time_base);
        }
    }

    // SwsContext: decoded pixel format -> BGRA (D3D9 D3DFMT_A8R8G8B8)
    sws_ctx_ = sws_getContext(
        vid_w_, vid_h_, vdec_ctx_->pix_fmt,
        vid_w_, vid_h_, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        SetError("sws_getContext failed");
        return false;
    }

    // SwrContext: decoded audio -> PCM s16 stereo 44100 Hz
    if (adec_ctx_) {
        swr_ctx_ = swr_alloc();
        AVChannelLayout stereo_layout = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout src_layout    = adec_ctx_->ch_layout;
        swr_alloc_set_opts2(&swr_ctx_,
            &stereo_layout,      AV_SAMPLE_FMT_S16,  AUDIO_SAMPLE_RATE,
            &src_layout, adec_ctx_->sample_fmt, adec_ctx_->sample_rate,
            0, nullptr);
        if (swr_init(swr_ctx_) < 0) {
            swr_free(&swr_ctx_);
            swr_ctx_ = nullptr;
        }
    }

    // Allocate ring buffer pixel planes
    int plane_sz = vid_w_ * vid_h_ * 4;
    for (int i = 0; i < RING_SIZE; i++) {
        ring_[i].buf   = (unsigned char*)malloc(plane_sz);
        ring_[i].w     = vid_w_;
        ring_[i].h     = vid_h_;
        ring_[i].ready = false;
        if (!ring_[i].buf) {
            SetError("out of memory allocating ring buffer");
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
//  CreateTexture — MANAGED pool so it survives device Reset()
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::CreateTexture(int w, int h)
{
    if (tex_) { tex_->Release(); tex_ = nullptr; }
    HRESULT hr = dev_->CreateTexture(
        (UINT)w, (UINT)h, 1, 0,
        D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        &tex_, nullptr);
    if (FAILED(hr)) {
        SetError("D3D9: CreateTexture failed");
        return false;
    }
    tex_w_ = w;
    tex_h_ = h;
    return true;
}

// ─────────────────────────────────────────────────────────────
//  UploadFrame — copy BGRA pixels into the D3D9 texture
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::UploadFrame(const RingSlot& slot)
{
    if (!tex_) return false;
    D3DLOCKED_RECT lr;
    if (FAILED(tex_->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
        return false;

    int src_stride = slot.w * 4;
    if (lr.Pitch == src_stride) {
        memcpy(lr.pBits, slot.buf, (size_t)slot.w * slot.h * 4);
    } else {
        // Pitch may differ — copy row by row
        const unsigned char* src = slot.buf;
        unsigned char*       dst = (unsigned char*)lr.pBits;
        for (int y = 0; y < slot.h; y++) {
            memcpy(dst, src, src_stride);
            src += src_stride;
            dst += lr.Pitch;
        }
    }
    tex_->UnlockRect(0);
    pos_sec_ = slot.pts;
    return true;
}

// ─────────────────────────────────────────────────────────────
//  waveOut callback — fired when a buffer finishes playing
//  (called from a system thread, keep it tiny)
// ─────────────────────────────────────────────────────────────
void CALLBACK VideoPlayer::WaveOutCallback(
    HWAVEOUT, UINT uMsg,
    DWORD_PTR dwInstance, DWORD_PTR, DWORD_PTR)
{
    if (uMsg == WOM_DONE) {
        VideoPlayer* self = reinterpret_cast<VideoPlayer*>(dwInstance);
        SetEvent(self->wave_done_event_);
    }
}

// ─────────────────────────────────────────────────────────────
//  AudioLoop — feeds decoded PCM to waveOut
// ─────────────────────────────────────────────────────────────
void VideoPlayer::AudioLoop()
{
    if (!wave_out_) return;

    AVFrame*  frame  = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    if (!frame || !packet) return;

    int cur_block = 0;

    while (!InterlockedCompareExchange(&stop_flag_, 0, 0)) {
        // Wait for a waveOut buffer to become free
        WaitForSingleObject(wave_done_event_, 50);

        if (InterlockedCompareExchange(&stop_flag_, 0, 0)) break;
        if (InterlockedCompareExchange(&paused_flag_, 0, 0)) {
            Sleep(10); continue;
        }

        // Grab an audio packet from the queue
        AVPacket* apkt = nullptr;
        EnterCriticalSection(&aud_cs_);
        if (aud_q_read_ != aud_q_write_) {
            apkt = aud_queue_[aud_q_read_];
            aud_q_read_ = (aud_q_read_ + 1) % AUD_QUEUE;
        }
        LeaveCriticalSection(&aud_cs_);

        if (!apkt) { Sleep(5); continue; }

        // Decode
        avcodec_send_packet(adec_ctx_, apkt);
        av_packet_free(&apkt);

        while (avcodec_receive_frame(adec_ctx_, frame) == 0) {
            if (!swr_ctx_) break;

            // Resample to s16 stereo 44100
            AudioBlock& blk = audio_blocks_[cur_block];
            uint8_t* dst[1] = { (uint8_t*)blk.data };
            int out_samples = swr_convert(
                swr_ctx_,
                dst, AUDIO_BLOCK_SAMPLES,
                (const uint8_t**)frame->extended_data,
                frame->nb_samples);
            if (out_samples <= 0) continue;

            // Submit to waveOut
            // Unprepare if previously submitted
            if (blk.hdr.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(wave_out_, &blk.hdr, sizeof(WAVEHDR));

            ZeroMemory(&blk.hdr, sizeof(WAVEHDR));
            blk.hdr.lpData         = (LPSTR)blk.data;
            blk.hdr.dwBufferLength = (DWORD)(out_samples
                                      * AUDIO_CHANNELS
                                      * sizeof(short));
            waveOutPrepareHeader(wave_out_, &blk.hdr, sizeof(WAVEHDR));
            waveOutWrite(wave_out_,        &blk.hdr, sizeof(WAVEHDR));
            cur_block = (cur_block + 1) % AUDIO_BLOCK_COUNT;

            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
}

// ─────────────────────────────────────────────────────────────
//  DecodeLoop — demux + video decode + push to ring buffer
// ─────────────────────────────────────────────────────────────
void VideoPlayer::DecodeLoop()
{
    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) {
        SetError("av_alloc failed in decode thread");
        return;
    }

    // Scale-destination buffer (single BGRA plane)
    uint8_t*  dst_data[4]     = {};
    int       dst_linesize[4] = {};
    av_image_alloc(dst_data, dst_linesize,
                   vid_w_, vid_h_, AV_PIX_FMT_BGRA, 1);

    state_ = VS_PLAYING;

    while (!InterlockedCompareExchange(&stop_flag_, 0, 0)) {

        // ── Seek request ─────────────────────────────────────
        if (InterlockedCompareExchange(&seek_flag_, 0, 0)) {
            double target = seek_target_;
            InterlockedExchange(&seek_flag_, 0);
            int64_t ts = (int64_t)(target
                * fmt_ctx_->streams[vid_stream_]->duration);
            av_seek_frame(fmt_ctx_, vid_stream_, ts,
                          AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(vdec_ctx_);
            if (adec_ctx_) avcodec_flush_buffers(adec_ctx_);
            // Drain ring so stale frames don't appear
            EnterCriticalSection(&ring_cs_);
            for (int i = 0; i < RING_SIZE; i++)
                ring_[i].ready = false;
            ring_read_ = ring_write_ = 0;
            LeaveCriticalSection(&ring_cs_);
        }

        // ── Pause ────────────────────────────────────────────
        if (InterlockedCompareExchange(&paused_flag_, 0, 0)) {
            state_ = VS_PAUSED;
            Sleep(10); continue;
        }
        state_ = VS_PLAYING;

        // ── Demux ────────────────────────────────────────────
        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret == AVERROR_EOF) {
            state_ = VS_EOF; break;
        }
        if (ret < 0) { Sleep(1); continue; }

        // ── Audio packet → audio queue ────────────────────────
        if (pkt->stream_index == aud_stream_) {
            AVPacket* apkt = av_packet_clone(pkt);
            if (apkt) {
                EnterCriticalSection(&aud_cs_);
                int next = (aud_q_write_ + 1) % AUD_QUEUE;
                if (next != aud_q_read_) {
                    aud_queue_[aud_q_write_] = apkt;
                    aud_q_write_ = next;
                } else {
                    av_packet_free(&apkt);  // queue full, drop
                }
                LeaveCriticalSection(&aud_cs_);
            }
            av_packet_unref(pkt);
            continue;
        }

        // ── Video packet ──────────────────────────────────────
        if (pkt->stream_index != vid_stream_) {
            av_packet_unref(pkt); continue;
        }

        avcodec_send_packet(vdec_ctx_, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(vdec_ctx_, frame) == 0) {
            // ── Wait for a free ring slot ─────────────────────
            // (non-blocking poll with back-off to avoid spinning)
            for (;;) {
                if (InterlockedCompareExchange(&stop_flag_, 0, 0)) goto done;
                EnterCriticalSection(&ring_cs_);
                int next = (ring_write_ + 1) % RING_SIZE;
                bool full = (next == ring_read_);
                LeaveCriticalSection(&ring_cs_);
                if (!full) break;
                WaitForSingleObject(ring_not_full_, 10);
            }

            // Convert pixel format
            sws_scale(sws_ctx_,
                      (const uint8_t* const*)frame->data,
                      frame->linesize,
                      0, vid_h_,
                      dst_data, dst_linesize);

            // Write into ring slot
            EnterCriticalSection(&ring_cs_);
            RingSlot& slot = ring_[ring_write_];
            memcpy(slot.buf, dst_data[0],
                   (size_t)vid_w_ * vid_h_ * 4);
            slot.pts   = (frame->pts != AV_NOPTS_VALUE)
                           ? (double)frame->pts * vid_tb_
                           : pos_sec_;
            slot.ready = true;
            ring_write_ = (ring_write_ + 1) % RING_SIZE;
            LeaveCriticalSection(&ring_cs_);
            SetEvent(ring_not_empty_);

            // Frame-rate throttle: target ~60 fps max in decode thread
            // to avoid running ahead too far and filling the ring.
            // Real A/V sync is handled in Update() on the main thread.
            Sleep(4);

            av_frame_unref(frame);
        }
    }

done:
    av_freep(&dst_data[0]);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

// ─────────────────────────────────────────────────────────────
//  Thread trampolines
// ─────────────────────────────────────────────────────────────
DWORD WINAPI VideoPlayer::DecodeThreadProc(LPVOID param)
{
    reinterpret_cast<VideoPlayer*>(param)->DecodeLoop();
    return 0;
}
DWORD WINAPI VideoPlayer::AudioThreadProc(LPVOID param)
{
    reinterpret_cast<VideoPlayer*>(param)->AudioLoop();
    return 0;
}

// ─────────────────────────────────────────────────────────────
//  Open  —  public entry point
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::Open(const std::string& ytUrl,
                        LPDIRECT3DDEVICE9  device,
                        const std::string& ytdlpPath)
{
    Close();  // safe no-op if already idle

    dev_   = device;
    state_ = VS_LOADING;
    InterlockedExchange(&stop_flag_,  0);
    InterlockedExchange(&seek_flag_,  0);
    InterlockedExchange(&paused_flag_,0);

    // Step 1 — resolve YouTube URL via yt-dlp
    std::string videoUrl, audioUrl;
    if (!ResolveUrl(ytdlpPath, ytUrl, videoUrl, audioUrl))
        return false;  // state_ already set to VS_ERROR

    // Step 2 — open streams with libavformat
    if (!OpenStreams(videoUrl, audioUrl))
        return false;

    // Step 3 — create D3D9 texture
    if (!CreateTexture(vid_w_, vid_h_))
        return false;

    // Step 4 — open waveOut for audio playback
    if (adec_ctx_ && swr_ctx_) {
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = AUDIO_CHANNELS;
        wfx.nSamplesPerSec  = AUDIO_SAMPLE_RATE;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = wfx.nChannels * (wfx.wBitsPerSample / 8);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        MMRESULT mr = waveOutOpen(
            &wave_out_,
            WAVE_MAPPER,
            &wfx,
            (DWORD_PTR)WaveOutCallback,
            (DWORD_PTR)this,
            CALLBACK_FUNCTION);
        if (mr != MMSYSERR_NOERROR) {
            wave_out_ = nullptr;  // audio unavailable, continue without
        }
    }

    // Step 5 — start worker threads
    decode_thread_ = CreateThread(
        nullptr, 0, DecodeThreadProc, this, 0, nullptr);
    if (!decode_thread_) {
        SetError("CreateThread failed for decode thread");
        return false;
    }
    if (wave_out_) {
        // Prime the waveOut done event so AudioLoop starts immediately
        SetEvent(wave_done_event_);
        audio_thread_ = CreateThread(
            nullptr, 0, AudioThreadProc, this, 0, nullptr);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────
//  Update  —  called once per render frame from main thread
//  Uploads the next ready ring-buffer frame to the D3D9 texture.
//  Returns true if the texture was updated.
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::Update()
{
    if (state_ == VS_IDLE || state_ == VS_LOADING ||
        state_ == VS_ERROR)
        return false;

    // Check if there is a frame ready to consume
    EnterCriticalSection(&ring_cs_);
    bool has_frame = (ring_read_ != ring_write_) &&
                      ring_[ring_read_].ready;
    LeaveCriticalSection(&ring_cs_);

    if (!has_frame) return false;

    // Simple A/V sync: compare video PTS against waveOut position.
    // If video is running more than 100 ms ahead, skip this frame.
    // If video is running more than 100 ms behind, display immediately.
    if (wave_out_) {
        MMTIME mmt = {};
        mmt.wType = TIME_BYTES;
        if (waveOutGetPosition(wave_out_, &mmt, sizeof(mmt))
            == MMSYSERR_NOERROR)
        {
            double audio_pos_sec =
                (double)mmt.u.cb
                / (double)(AUDIO_CHANNELS
                           * sizeof(short)
                           * AUDIO_SAMPLE_RATE);

            EnterCriticalSection(&ring_cs_);
            double vid_pts = ring_[ring_read_].pts;
            LeaveCriticalSection(&ring_cs_);

            double diff = vid_pts - audio_pos_sec;
            if (diff > 0.1) return false;   // video ahead — wait
            // If diff < -0.1 the video is behind; fall through and display
        }
    }

    // Upload the frame
    EnterCriticalSection(&ring_cs_);
    RingSlot& slot = ring_[ring_read_];
    bool ok = UploadFrame(slot);
    slot.ready = false;
    ring_read_ = (ring_read_ + 1) % RING_SIZE;
    LeaveCriticalSection(&ring_cs_);
    SetEvent(ring_not_full_);

    return ok;
}

// ─────────────────────────────────────────────────────────────
//  SetPaused
// ─────────────────────────────────────────────────────────────
void VideoPlayer::SetPaused(bool p)
{
    InterlockedExchange(&paused_flag_, p ? 1 : 0);
    if (wave_out_) {
        if (p) waveOutPause(wave_out_);
        else   waveOutRestart(wave_out_);
    }
}

// ─────────────────────────────────────────────────────────────
//  Seek
// ─────────────────────────────────────────────────────────────
void VideoPlayer::Seek(float t)
{
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    seek_target_ = (double)t;
    InterlockedExchange(&seek_flag_, 1);
    if (wave_out_)
        waveOutReset(wave_out_);
}

// ─────────────────────────────────────────────────────────────
//  Close  —  stop threads, release all resources
// ─────────────────────────────────────────────────────────────
void VideoPlayer::Close()
{
    if (state_ == VS_IDLE) return;

    // Signal threads to stop
    InterlockedExchange(&stop_flag_, 1);
    SetEvent(ring_not_empty_);
    SetEvent(ring_not_full_);
    SetEvent(wave_done_event_);

    // Stop audio immediately so waveOut callbacks don't fire after cleanup
    if (wave_out_) {
        waveOutReset(wave_out_);
        // Unprepare any prepared headers
        for (int i = 0; i < AUDIO_BLOCK_COUNT; i++) {
            if (audio_blocks_[i].hdr.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(wave_out_,
                    &audio_blocks_[i].hdr, sizeof(WAVEHDR));
        }
        waveOutClose(wave_out_);
        wave_out_ = nullptr;
    }

    // Wait for threads
    if (decode_thread_) {
        WaitForSingleObject(decode_thread_, 5000);
        CloseHandle(decode_thread_);
        decode_thread_ = nullptr;
    }
    if (audio_thread_) {
        WaitForSingleObject(audio_thread_, 5000);
        CloseHandle(audio_thread_);
        audio_thread_ = nullptr;
    }

    // Free FFmpeg resources
    if (sws_ctx_)  { sws_freeContext(sws_ctx_); sws_ctx_ = nullptr; }
    if (swr_ctx_)  { swr_free(&swr_ctx_); }
    if (vdec_ctx_) { avcodec_free_context(&vdec_ctx_); }
    if (adec_ctx_) { avcodec_free_context(&adec_ctx_); }
    if (fmt_ctx_)  { avformat_close_input(&fmt_ctx_); }

    // Free ring buffer memory
    for (int i = 0; i < RING_SIZE; i++) {
        if (ring_[i].buf) { free(ring_[i].buf); ring_[i].buf = nullptr; }
        ring_[i].ready = false;
    }
    ring_read_ = ring_write_ = 0;

    // Release audio packet queue
    EnterCriticalSection(&aud_cs_);
    while (aud_q_read_ != aud_q_write_) {
        if (aud_queue_[aud_q_read_])
            av_packet_free(&aud_queue_[aud_q_read_]);
        aud_q_read_ = (aud_q_read_ + 1) % AUD_QUEUE;
    }
    LeaveCriticalSection(&aud_cs_);

    // Release D3D9 texture
    if (tex_) { tex_->Release(); tex_ = nullptr; }

    // Reset all fields
    vid_stream_ = aud_stream_ = -1;
    vid_w_ = vid_h_ = tex_w_ = tex_h_ = 0;
    dur_sec_ = pos_sec_ = vid_tb_ = aud_tb_ = 0.0;
    aud_q_read_ = aud_q_write_ = 0;
    ZeroMemory(audio_blocks_, sizeof(audio_blocks_));
    ZeroMemory(err_buf_, sizeof(err_buf_));

    InterlockedExchange(&stop_flag_,  0);
    InterlockedExchange(&seek_flag_,  0);
    InterlockedExchange(&paused_flag_,0);
    state_ = VS_IDLE;
}
