#include "stream/video_decoder.h"

#include <QPointer>
#include <QVideoFrame>
#include <QVideoFrameFormat>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>

namespace vms {

namespace {

// Cap presentation backlog per cell. 2 frames keeps latency < 1 frame period
// while absorbing GUI-thread jitter; beyond that we drop (decode continues).
constexpr int kMaxFramesInFlight = 2;

// Overlay/position tick rate towards QML. The Shamsi overlay shows seconds,
// so 4 Hz is ample; per-frame signals at 25fps x 64 cells would flood the GUI
// event loop with 1600 cross-thread deliveries per second.
constexpr auto kTickInterval = std::chrono::milliseconds(250);

bool isNetworkUrl(const QString& url) {
    return url.startsWith(QLatin1String("rtsp://")) ||
           url.startsWith(QLatin1String("rtsps://")) ||
           url.startsWith(QLatin1String("http://")) ||
           url.startsWith(QLatin1String("https://"));
}

}  // namespace

VideoDecoder::VideoDecoder(QObject* parent) : QObject(parent) {}

VideoDecoder::~VideoDecoder() { stop(); }

// --- FFmpeg callbacks ----------------------------------------------------------

// Aborts blocking I/O (open / read on a stalled connection) the moment
// running_ flips false, so stop() joins in milliseconds instead of waiting
// out a multi-second network timeout on the GUI thread.
int VideoDecoder::interruptCb(void* opaque) {
    auto* self = static_cast<VideoDecoder*>(opaque);
    return (self && !self->running_.load(std::memory_order_relaxed)) ? 1 : 0;
}

AVPixelFormat VideoDecoder::selectHwFormat(AVCodecContext* ctx,
                                           const AVPixelFormat* fmts) {
    auto* self = static_cast<VideoDecoder*>(ctx->opaque);
    if (self) {
        for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == self->hw_pix_fmt_) return *p;
        }
        // Hardware format not offered for this stream: fall back to software.
        self->hw_pix_fmt_ = AV_PIX_FMT_NONE;
    }
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*p);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
    }
    return fmts[0];
}

// --- Public control -----------------------------------------------------------
void VideoDecoder::startLive(const QString& url, QVideoSink* sink) {
    stop();
    sink_ = sink;
    running_.store(true);
    worker_ = std::thread([this, url] { decodeLoop(url, /*playback=*/false); });
    emit decodingChanged();
}

void VideoDecoder::startPlayback(const QString& url, QVideoSink* sink,
                                 quint64 startUtcUs) {
    stop();
    sink_ = sink;
    seek_target_us_.store(startUtcUs);
    running_.store(true);
    worker_ = std::thread([this, url] { decodeLoop(url, /*playback=*/true); });
    emit decodingChanged();
}

void VideoDecoder::switchTo(const QString& url) {
    std::lock_guard<std::mutex> lock(url_mutex_);
    pending_url_ = url;
    switch_pending_.store(true);
}

void VideoDecoder::seekTo(quint64 utcUs) { seek_target_us_.store(utcUs); }
void VideoDecoder::setPaused(bool paused) { paused_.store(paused); }
void VideoDecoder::setRate(double rate) { rate_.store(rate); }

void VideoDecoder::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    emit decodingChanged();
}

void VideoDecoder::interruptibleSleep(int ms) {
    using namespace std::chrono;
    constexpr int kSliceMs = 50;
    for (int slept = 0; slept < ms && running_.load(); slept += kSliceMs) {
        std::this_thread::sleep_for(milliseconds(kSliceMs));
    }
}

// --- Hardware decoding ----------------------------------------------------------
// Enterprise-scale walls (16-64 concurrent tiles) are only feasible when the
// GPU decode ASIC (NVDEC / Quick Sync / AMD VCN) does the heavy lifting.
// Try the platform's native hwaccel; on any failure fall back to software
// with a bounded thread count.
bool VideoDecoder::initHardwareDecoder(AVCodecContext* codec, const AVCodec* dec) {
    static const AVHWDeviceType kPreferred[] = {
#if defined(_WIN32)
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2,
#elif defined(__APPLE__)
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
        AV_HWDEVICE_TYPE_VAAPI,
        AV_HWDEVICE_TYPE_VDPAU,
#endif
    };

    for (AVHWDeviceType type : kPreferred) {
        const AVCodecHWConfig* cfg = nullptr;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* c = avcodec_get_hw_config(dec, i);
            if (!c) break;
            if ((c->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                c->device_type == type) {
                cfg = c;
                break;
            }
        }
        if (!cfg) continue;

        AVBufferRef* device = nullptr;
        if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0)
            continue;

        codec->hw_device_ctx = device;  // ownership -> codec context
        hw_pix_fmt_ = cfg->pix_fmt;
        codec->opaque = this;
        codec->get_format = &VideoDecoder::selectHwFormat;
        return true;
    }
    return false;
}

// --- Input setup --------------------------------------------------------------
bool VideoDecoder::openInput(const QString& url, AVFormatContext*& fmt,
                             AVCodecContext*& codec, int& videoStream) {
    fmt = avformat_alloc_context();
    if (!fmt) return false;
    fmt->interrupt_callback.callback = &VideoDecoder::interruptCb;
    fmt->interrupt_callback.opaque = this;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "3000000", 0);
    av_dict_set(&opts, "timeout", "3000000", 0);
    av_dict_set(&opts, "rw_timeout", "3000000", 0);

    // avformat_open_input frees fmt and nulls it on failure.
    if (avformat_open_input(&fmt, url.toUtf8().constData(), nullptr, &opts) < 0) {
        av_dict_free(&opts);
        setStatusFa(QStringLiteral("خطا در باز کردن استریم"));
        return false;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(fmt, nullptr) < 0) return false;

    videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStream < 0) return false;

    const AVCodec* dec = avcodec_find_decoder(fmt->streams[videoStream]->codecpar->codec_id);
    if (!dec) return false;

    codec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(codec, fmt->streams[videoStream]->codecpar);

    hw_pix_fmt_ = AV_PIX_FMT_NONE;
    if (!initHardwareDecoder(codec, dec)) {
        // Software fallback: bound the thread count. thread_count = 0 (auto)
        // grabs every core PER DECODER — 64 tiles x N cores explodes into
        // thousands of threads and context-switch thrash on a video wall.
        codec->thread_count = 2;
        codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    setStatusFa(QStringLiteral("رمزگشایی فعال شد"));
    return avcodec_open2(codec, dec, nullptr) >= 0;
}

// --- Frame presentation (Thread-safe; each decoder owns its own sink) --------
void VideoDecoder::presentFrame(AVFrame* frame, quint64 utcUs) {
    if (!sink_ || !frame || frame->width <= 0 || frame->height <= 0 ||
        frame->format == AV_PIX_FMT_NONE || !frame->data[0]) return;

    // Backpressure: if the GUI thread hasn't consumed prior frames, drop this
    // one. Decode keeps running so recovery is instant; memory stays bounded.
    if (frames_in_flight_.load(std::memory_order_relaxed) >= kMaxFramesInFlight)
        return;

    const int width = frame->width;
    const int height = frame->height;

    // Zero-conversion path: hand YUV to the scene graph directly and let the
    // GPU do the YUV->RGB conversion in the fragment shader. The old
    // swscale->RGBA path cost a full CPU colorspace conversion plus 4 bytes/px
    // (vs 1.5) of memory traffic for EVERY frame of EVERY cell.
    QVideoFrameFormat::PixelFormat qtFmt = QVideoFrameFormat::Format_Invalid;
    switch (frame->format) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            qtFmt = QVideoFrameFormat::Format_YUV420P;
            break;
        case AV_PIX_FMT_NV12:
            qtFmt = QVideoFrameFormat::Format_NV12;
            break;
        default:
            break;
    }

    QVideoFrame vf;
    if (qtFmt != QVideoFrameFormat::Format_Invalid) {
        QVideoFrameFormat fmtDesc(QSize(width, height), qtFmt);
        if (frame->format == AV_PIX_FMT_YUVJ420P ||
            frame->color_range == AVCOL_RANGE_JPEG) {
            fmtDesc.setColorRange(QVideoFrameFormat::ColorRange_Full);
        }
        vf = QVideoFrame(fmtDesc);
        if (!vf.isValid() || !vf.map(QVideoFrame::WriteOnly)) return;

        const int chromaH = (height + 1) / 2;
        if (qtFmt == QVideoFrameFormat::Format_YUV420P) {
            av_image_copy_plane(vf.bits(0), vf.bytesPerLine(0),
                                frame->data[0], frame->linesize[0], width, height);
            av_image_copy_plane(vf.bits(1), vf.bytesPerLine(1),
                                frame->data[1], frame->linesize[1],
                                (width + 1) / 2, chromaH);
            av_image_copy_plane(vf.bits(2), vf.bytesPerLine(2),
                                frame->data[2], frame->linesize[2],
                                (width + 1) / 2, chromaH);
        } else {  // NV12 (typical hardware-decoder output)
            av_image_copy_plane(vf.bits(0), vf.bytesPerLine(0),
                                frame->data[0], frame->linesize[0], width, height);
            av_image_copy_plane(vf.bits(1), vf.bytesPerLine(1),
                                frame->data[1], frame->linesize[1], width, chromaH);
        }
        vf.unmap();
    } else {
        // Exotic pixel formats only: cached swscale fallback to RGBA.
        sws_ = sws_getCachedContext(sws_, width, height,
                                    static_cast<AVPixelFormat>(frame->format),
                                    width, height, AV_PIX_FMT_RGBA,
                                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) return;

        QVideoFrameFormat fmtDesc(QSize(width, height),
                                  QVideoFrameFormat::Format_RGBA8888);
        vf = QVideoFrame(fmtDesc);
        if (!vf.isValid() || !vf.map(QVideoFrame::WriteOnly)) return;

        uint8_t* dst[4] = { vf.bits(0), nullptr, nullptr, nullptr };
        int dstStride[4] = { vf.bytesPerLine(0), 0, 0, 0 };
        sws_scale(sws_, frame->data, frame->linesize, 0, height, dst, dstStride);
        vf.unmap();
    }

    // Throttled overlay tick: the Shamsi clock shows seconds, so QML doesn't
    // need a cross-thread signal per frame. Always tick the first frame so
    // cells clear their "connecting" placeholder immediately.
    const auto now = std::chrono::steady_clock::now();
    const bool emitTick = !first_frame_presented_ ||
                          (now - last_tick_) >= kTickInterval;
    if (emitTick) {
        first_frame_presented_ = true;
        last_tick_ = now;
    }

    frames_in_flight_.fetch_add(1, std::memory_order_relaxed);

    // Guard BOTH the sink and this decoder: the queued lambda may run on the
    // GUI thread after either has been destroyed.
    QPointer<QVideoSink> safeSink = sink_;
    QPointer<VideoDecoder> safeSelf = this;

    QMetaObject::invokeMethod(
        sink_,
        [safeSink, safeSelf, vf, utcUs, emitTick]() {
            if (safeSink) safeSink->setVideoFrame(vf);
            if (safeSelf) {
                safeSelf->frames_in_flight_.fetch_sub(1, std::memory_order_relaxed);
                if (emitTick) emit safeSelf->framePresented(utcUs);
            }
        },
        Qt::QueuedConnection);
}

// --- Decode loop -----------------------------------------------------------------
// Outer connection loop: enterprise VMS behavior is automatic reconnection
// with exponential backoff when an RTSP session drops — a cell must never
// freeze forever because the network blipped.
void VideoDecoder::decodeLoop(QString url, bool playbackMode) {
    int backoffMs = 500;

    while (running_.load()) {
        const SessionResult result = decodeSession(url, playbackMode);
        if (result == SessionResult::Stopped || !running_.load()) break;

        if (result == SessionResult::OpenFailed && !isNetworkUrl(url)) {
            // A local file that won't open will never heal itself.
            setStatusFa(QStringLiteral("فایل ویدیو در دسترس نیست"));
            break;
        }

        setStatusFa(QStringLiteral("اتصال قطع شد؛ تلاش مجدد..."));
        interruptibleSleep(backoffMs);
        backoffMs = std::min(backoffMs * 2, 8000);
    }
    running_.store(false);
}

VideoDecoder::SessionResult VideoDecoder::decodeSession(const QString& url,
                                                        bool playbackMode) {
    using namespace std::chrono;

    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    int videoStream = -1;

    if (!openInput(url, fmt, codec, videoStream)) {
        if (codec) avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        return running_.load() ? SessionResult::OpenFailed
                               : SessionResult::Stopped;
    }
    setStatusFa(QStringLiteral("در حال پخش"));

    AVRational fr = av_guess_frame_rate(fmt, fmt->streams[videoStream], nullptr);
    double sourceFps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;

    const bool isLocalFile = !isNetworkUrl(url);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* swFrame = av_frame_alloc();  // hw->system-memory transfer target

    AVFormatContext* nextFmt = nullptr;
    AVCodecContext* nextCodec = nullptr;
    int nextStream = -1;

    bool waiting_for_keyframe = true;
    int consecutiveReadErrors = 0;
    auto lastFrameTime = steady_clock::now();
    SessionResult result = SessionResult::Stopped;

    while (running_.load()) {
        // --- Seamless profile switch ----
        if (switch_pending_.load() && !nextFmt) {
            QString target;
            { std::lock_guard<std::mutex> l(url_mutex_); target = pending_url_; }
            if (openInput(target, nextFmt, nextCodec, nextStream)) {
                AVPacket* p2 = av_packet_alloc();
                while (running_.load() && av_read_frame(nextFmt, p2) >= 0) {
                    const bool key = (p2->flags & AV_PKT_FLAG_KEY) &&
                                     p2->stream_index == nextStream;
                    if (key) {
                        avcodec_free_context(&codec);
                        avformat_close_input(&fmt);
                        fmt = nextFmt; codec = nextCodec;
                        videoStream = nextStream;
                        nextFmt = nullptr; nextCodec = nullptr;
                        avcodec_send_packet(codec, p2);
                        switch_pending_.store(false);
                        waiting_for_keyframe = false;
                        emit keyframeAligned();
                        break;
                    }
                    av_packet_unref(p2);
                }
                av_packet_free(&p2);
            } else {
                if (nextCodec) avcodec_free_context(&nextCodec);
                if (nextFmt) avformat_close_input(&nextFmt);
                switch_pending_.store(false);
            }
        }

        // --- Sync-playback seek ----------------------------------------------
        if (playbackMode) {
            const quint64 target = seek_target_us_.exchange(0);
            if (target != 0) {
                const int64_t ts = av_rescale_q(
                    static_cast<int64_t>(target), AVRational{1, 1000000},
                    fmt->streams[videoStream]->time_base);
                av_seek_frame(fmt, videoStream, ts, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codec);
                waiting_for_keyframe = true;
            }
            if (paused_.load()) {
                std::this_thread::sleep_for(milliseconds(20));
                continue;
            }
        }

        if (av_read_frame(fmt, pkt) < 0) {
            if (fmt->pb && avio_feof(fmt->pb)) {
                if (isLocalFile) {
                    // Loop local demo files.
                    av_seek_frame(fmt, videoStream, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(codec);
                    waiting_for_keyframe = true;
                    consecutiveReadErrors = 0;
                    continue;
                }
                result = SessionResult::Reconnect;  // network EOF = drop
                break;
            }
            // Persistent read errors on a live connection: reconnect instead
            // of spinning forever on a dead socket (frozen cell).
            if (++consecutiveReadErrors >= 25) {
                result = SessionResult::Reconnect;
                break;
            }
            std::this_thread::sleep_for(milliseconds(100));
            continue;
        }
        consecutiveReadErrors = 0;

        if (pkt->stream_index != videoStream) {
            av_packet_unref(pkt);
            continue;
        }

        if (waiting_for_keyframe) {
            if (pkt->flags & AV_PKT_FLAG_KEY) {
                waiting_for_keyframe = false;
            } else {
                av_packet_unref(pkt);
                continue;
            }
        }

        avcodec_send_packet(codec, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(codec, frame) >= 0 && running_.load()) {
            const AVRational tb = fmt->streams[videoStream]->time_base;
            const quint64 utcUs = frame->pts > 0
                                      ? static_cast<quint64>(av_rescale_q(frame->pts, tb,
                                                                          AVRational{1, 1000000}))
                                      : 0;

            // Hardware frames live in GPU memory; transfer to system memory
            // (typically NV12) before presentation.
            AVFrame* out = frame;
            if (hw_pix_fmt_ != AV_PIX_FMT_NONE &&
                frame->format == hw_pix_fmt_) {
                av_frame_unref(swFrame);
                out = (av_hwframe_transfer_data(swFrame, frame, 0) == 0)
                          ? swFrame
                          : nullptr;
            }
            if (out) presentFrame(out, utcUs);

            // کنترل نرخ فریم برای فایل‌های محلی (Pacing)
            if (isLocalFile || playbackMode) {
                const double currentRate = rate_.load();
                const double effectiveFps = sourceFps * (currentRate > 0 ? currentRate : 1.0);
                const int frameDelayMs = static_cast<int>(1000.0 / (effectiveFps > 0 ? effectiveFps : 25.0));

                auto now = steady_clock::now();
                auto elapsedMs = duration_cast<milliseconds>(now - lastFrameTime).count();
                int sleepMs = frameDelayMs - static_cast<int>(elapsedMs);

                if (sleepMs > 0 && sleepMs < 500) {
                    std::this_thread::sleep_for(milliseconds(sleepMs));
                }
                lastFrameTime = steady_clock::now();
            }

            av_frame_unref(frame);
        }
    }

    av_frame_free(&swFrame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (nextCodec) avcodec_free_context(&nextCodec);
    if (nextFmt) avformat_close_input(&nextFmt);
    avcodec_free_context(&codec);
    avformat_close_input(&fmt);
    return result;
}

void VideoDecoder::setStatusFa(const QString& s) {
    if (status_fa_ == s) return;
    status_fa_ = s;
    QMetaObject::invokeMethod(this, [this] { emit statusFaChanged(); },
                              Qt::QueuedConnection);
}

}  // namespace vms
