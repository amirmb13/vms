#include "stream/video_decoder.h"

#include <QPointer>
#include <QVideoFrame>
#include <QVideoFrameFormat>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <map>

namespace vms {
namespace {

// Process-wide hardware decode device cache. Creating one GPU device context
// PER DECODER (av_hwdevice_ctx_create is expensive: driver handshake + GPU
// memory per context) multiplies driver overhead by cell count — a 64-cell
// wall allocated 64 D3D11/VAAPI devices. Enterprise engines share ONE device
// per type across every decoder; each codec context gets its own refcounted
// handle (av_buffer_ref) and the master ref lives for the process lifetime.
AVBufferRef* acquireSharedHwDevice(AVHWDeviceType type) {
    static std::mutex mutex;
    static std::map<AVHWDeviceType, AVBufferRef*> cache;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(type);
    if (it != cache.end()) {
        return it->second ? av_buffer_ref(it->second) : nullptr;
    }

    AVBufferRef* device = nullptr;
    if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) {
        device = nullptr;  // negative-cache: don't re-probe a missing GPU
    }
    cache[type] = device;
    return device ? av_buffer_ref(device) : nullptr;
}

// Abort blocking FFmpeg I/O (open / read / seek) the moment running_ drops.
// Without this an RTSP open or read can pin the worker thread for the full
// 3s socket timeout while StreamController::detach() joins it — a layout
// switch across a large wall froze the GUI for seconds per cell.
int interruptCallback(void* opaque) {
    const auto* running = static_cast<const std::atomic<bool>*>(opaque);
    return running->load(std::memory_order_relaxed) ? 0 : 1;
}

// GUI-thread backpressure: at most this many frames may sit in the GUI event
// queue per cell. Beyond it the decoder drops frames instead of flooding the
// event loop — bounded latency and bounded memory under load, which is the
// standard enterprise-VMS policy (drop, never queue unbounded).
constexpr int kMaxFramesInFlight = 2;

// Cap decode threads by resolution. thread_count = 0 (auto) spawns one
// thread per CPU core PER DECODER; a 64-cell wall on a 16-core machine
// would create ~1000 decode threads. Sub-streams need no frame threading.
int threadCountForHeight(int height) {
    if (height >= 1440) return 4;
    if (height >= 720) return 2;
    return 1;
}

}  // namespace

VideoDecoder::VideoDecoder(QObject* parent)
    : QObject(parent),
      frames_in_flight_(std::make_shared<std::atomic<int>>(0)) {}

VideoDecoder::~VideoDecoder() { stop(); }

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

void VideoDecoder::requestStop() { running_.store(false); }

void VideoDecoder::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    emit decodingChanged();
}

// --- Hardware decode negotiation ----------------------------------------------
AVPixelFormat VideoDecoder::getHwFormat(AVCodecContext* ctx,
                                        const AVPixelFormat* formats) {
    const auto* self = static_cast<const VideoDecoder*>(ctx->opaque);
    for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == self->hw_pix_fmt_) return *p;
    }
    // Hardware surface unavailable for this stream — software fallback.
    return formats[0];
}

bool VideoDecoder::initHardwareDecoder(AVCodecContext* codec,
                                       const AVCodec* dec) {
    hw_pix_fmt_ = AV_PIX_FMT_NONE;

    // Platform-preferred device types, best first. GPU decode is what lets
    // enterprise walls (Genetec-class) run 16–64 simultaneous streams: the
    // CPU only demuxes while the video engine decodes.
    static const AVHWDeviceType kPreferred[] = {
#if defined(_WIN32)
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2,
#elif defined(__APPLE__)
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
        AV_HWDEVICE_TYPE_VAAPI,
        AV_HWDEVICE_TYPE_CUDA,
#endif
    };

    for (AVHWDeviceType type : kPreferred) {
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(dec, i);
            if (!cfg) break;
            if (!(cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) ||
                cfg->device_type != type) {
                continue;
            }
            // Shared per-type GPU device: this codec context receives its
            // own refcounted handle; avcodec_free_context() releases the
            // handle while the master device survives for the next decoder.
            AVBufferRef* device = acquireSharedHwDevice(type);
            if (!device) continue;
            codec->hw_device_ctx = device;
            codec->opaque = this;
            codec->get_format = &VideoDecoder::getHwFormat;
            hw_pix_fmt_ = cfg->pix_fmt;
            return true;
        }
    }
    return false;
}

// --- Input setup --------------------------------------------------------------
bool VideoDecoder::openInput(const QString& url, AVFormatContext*& fmt,
                             AVCodecContext*& codec, int& videoStream) {
    codec = nullptr;
    fmt = avformat_alloc_context();
    if (!fmt) return false;

    // Interrupt callback: makes every blocking FFmpeg call abort promptly
    // when stop()/requestStop() flips running_.
    fmt->interrupt_callback.callback = interruptCallback;
    fmt->interrupt_callback.opaque = &running_;

    const bool isNetwork = url.contains(QLatin1String("://"));
    if (isNetwork) {
        // Bound stream probing: FFmpeg's defaults can analyze several
        // seconds of stream before the first frame appears. 0.5s / 512KB is
        // plenty for H.264/H.265 RTSP and makes large layout switches show
        // video near-instantly in every cell.
        fmt->probesize = 512 * 1024;
        fmt->max_analyze_duration = 500 * 1000;  // µs
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "3000000", 0);
    av_dict_set(&opts, "timeout", "3000000", 0);
    av_dict_set(&opts, "rw_timeout", "3000000", 0);

    if (avformat_open_input(&fmt, url.toUtf8().constData(), nullptr, &opts) < 0) {
        av_dict_free(&opts);
        fmt = nullptr;  // avformat_open_input frees the context on failure
        setStatusFa(QStringLiteral("خطا در باز کردن استریم"));
        return false;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStream < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    const AVCodec* dec =
        avcodec_find_decoder(fmt->streams[videoStream]->codecpar->codec_id);
    if (!dec) {
        avformat_close_input(&fmt);
        return false;
    }

    codec = avcodec_alloc_context3(dec);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }
    avcodec_parameters_to_context(codec, fmt->streams[videoStream]->codecpar);

    // GPU decode first; capped software frame-threading as the fallback.
    const bool hw = initHardwareDecoder(codec, dec);
    if (!hw) {
        codec->thread_count = threadCountForHeight(codec->height);
        codec->thread_type = FF_THREAD_FRAME;
    }

    if (avcodec_open2(codec, dec, nullptr) < 0) {
        avcodec_free_context(&codec);
        avformat_close_input(&fmt);
        return false;
    }

    setStatusFa(hw ? QStringLiteral("رمزگشایی سخت‌افزاری فعال شد")
                   : QStringLiteral("رمزگشایی فعال شد"));
    return true;
}

// --- Frame presentation (thread-safe; each decoder owns its own sink) --------
bool VideoDecoder::presentFrame(AVFrame* frame, quint64 utcUs) {
    if (!sink_ || !frame || frame->width <= 0 || frame->height <= 0 ||
        frame->format == AV_PIX_FMT_NONE || !frame->data[0]) return true;

    // Backpressure: if the GUI thread hasn't consumed the previous frames
    // yet, drop this one. Queuing unboundedly grows latency and memory until
    // the whole wall stalls — dropping keeps every cell realtime. The caller
    // uses the `false` return to engage decoder-level frame skipping so we
    // stop paying full decode cost for frames that are dropped anyway.
    if (frames_in_flight_->load(std::memory_order_acquire) >= kMaxFramesInFlight)
        return false;

    const int width = frame->width;
    const int height = frame->height;
    const auto srcFmt = static_cast<AVPixelFormat>(frame->format);

    // Zero-conversion path: Qt's RHI video pipeline consumes planar YUV
    // natively and does YUV→RGB in the fragment shader on the GPU. Running
    // sws_scale to RGBA on the CPU for every frame of every cell quadruples
    // memory traffic and burns cores — only rare formats fall back to it.
    QVideoFrameFormat::PixelFormat qtFmt = QVideoFrameFormat::Format_Invalid;
    if (srcFmt == AV_PIX_FMT_NV12)
        qtFmt = QVideoFrameFormat::Format_NV12;
    else if (srcFmt == AV_PIX_FMT_YUV420P || srcFmt == AV_PIX_FMT_YUVJ420P)
        qtFmt = QVideoFrameFormat::Format_YUV420P;

    QVideoFrame vf;
    if (qtFmt != QVideoFrameFormat::Format_Invalid) {
        QVideoFrameFormat fmtDesc(QSize(width, height), qtFmt);
        vf = QVideoFrame(fmtDesc);
        if (!vf.isValid() || !vf.map(QVideoFrame::WriteOnly)) return true;

        const int planeCount = (qtFmt == QVideoFrameFormat::Format_NV12) ? 2 : 3;
        for (int p = 0; p < planeCount; ++p) {
            const int planeHeight = (p == 0) ? height : height / 2;
            const int copyBytes =
                std::min(vf.bytesPerLine(p), frame->linesize[p]);
            av_image_copy_plane(vf.bits(p), vf.bytesPerLine(p),
                                frame->data[p], frame->linesize[p],
                                copyBytes, planeHeight);
        }
        vf.unmap();
    } else {
        // Fallback: CPU conversion for uncommon pixel formats.
        sws_ = sws_getCachedContext(sws_, width, height, srcFmt,
                                    width, height, AV_PIX_FMT_RGBA,
                                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) return true;

        QVideoFrameFormat fmtDesc(QSize(width, height),
                                  QVideoFrameFormat::Format_RGBA8888);
        vf = QVideoFrame(fmtDesc);
        if (!vf.isValid() || !vf.map(QVideoFrame::WriteOnly)) return true;

        uint8_t* dst[4] = { vf.bits(0), nullptr, nullptr, nullptr };
        int dstStride[4] = { vf.bytesPerLine(0), 0, 0, 0 };
        sws_scale(sws_, frame->data, frame->linesize, 0, height, dst, dstStride);
        vf.unmap();
    }

    // Guard BOTH the sink and this decoder: the queued lambda may run on the
    // GUI thread after either has been destroyed. The in-flight counter is a
    // shared_ptr so the decrement stays valid regardless.
    frames_in_flight_->fetch_add(1, std::memory_order_release);
    QPointer<QVideoSink> safeSink = sink_;
    QPointer<VideoDecoder> safeSelf = this;
    std::shared_ptr<std::atomic<int>> inFlight = frames_in_flight_;

    QMetaObject::invokeMethod(
        sink_,
        [safeSink, safeSelf, inFlight, vf, utcUs]() {
            inFlight->fetch_sub(1, std::memory_order_release);
            if (safeSink) safeSink->setVideoFrame(vf);
            if (safeSelf) emit safeSelf->framePresented(utcUs);
        },
        Qt::QueuedConnection);
    return true;
}

// --- Decode loop -----------------------------------------------------------------
// Outer supervisor: live network streams auto-reconnect with exponential
// backoff (1s → 2s → … → 10s cap). Without this, a dropped RTSP connection
// or a camera reboot left the cell permanently black until the operator
// manually reassigned it — enterprise VMS walls self-heal. The backoff
// resets to the minimum whenever a session actually presented frames, so a
// brief network blip recovers in ~1s while a dead camera is probed gently.
void VideoDecoder::decodeLoop(QString url, bool playbackMode) {
    using namespace std::chrono;

    {
        std::lock_guard<std::mutex> lock(url_mutex_);
        active_url_ = url;
    }

    const bool isNetwork = url.contains(QLatin1String("://"));
    constexpr int kReconnectMinMs = 1000;
    constexpr int kReconnectMaxMs = 10000;
    int backoffMs = kReconnectMinMs;

    while (running_.load()) {
        QString target;
        {
            std::lock_guard<std::mutex> lock(url_mutex_);
            target = active_url_;  // profile switches survive reconnects
        }

        bool presentedAnyFrame = false;
        const SessionResult result =
            runSession(target, playbackMode, presentedAnyFrame);

        if (result == SessionResult::Stopped ||
            result == SessionResult::Finished) {
            break;
        }
        // Only live network streams self-heal: local files can't "reconnect"
        // and archive playback restarting from its original start time would
        // silently rewind the review position.
        if (!isNetwork || playbackMode) break;

        if (presentedAnyFrame) backoffMs = kReconnectMinMs;

        setStatusFa(QStringLiteral("قطع ارتباط — تلاش برای اتصال مجدد..."));

        // Interruptible backoff: stop() never waits behind the full delay.
        int remainMs = backoffMs;
        while (remainMs > 0 && running_.load()) {
            const int chunk = std::min(remainMs, 50);
            std::this_thread::sleep_for(milliseconds(chunk));
            remainMs -= chunk;
        }
        backoffMs = std::min(backoffMs * 2, kReconnectMaxMs);
    }

    running_.store(false);
}

// One connect→decode→teardown pass.
VideoDecoder::SessionResult VideoDecoder::runSession(const QString& url,
                                                     bool playbackMode,
                                                     bool& presentedAnyFrame) {
    using namespace std::chrono;

    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    int videoStream = -1;

    if (!openInput(url, fmt, codec, videoStream)) {
        return running_.load() ? SessionResult::OpenFailed
                               : SessionResult::Stopped;
    }
    setStatusFa(QStringLiteral("در حال پخش"));

    AVRational fr = av_guess_frame_rate(fmt, fmt->streams[videoStream], nullptr);
    double sourceFps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;

    const bool isLocalFile = !url.startsWith(QLatin1String("rtsp://")) &&
                             !url.startsWith(QLatin1String("http://")) &&
                             !url.startsWith(QLatin1String("https://"));

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* swFrame = av_frame_alloc();  // GPU→CPU transfer target

    AVFormatContext* nextFmt = nullptr;
    AVCodecContext* nextCodec = nullptr;
    int nextStream = -1;

    bool waiting_for_keyframe = true;
    auto lastFrameTime = steady_clock::now();

    SessionResult result = SessionResult::Stopped;
    int consecutiveReadErrors = 0;
    constexpr int kMaxConsecutiveReadErrors = 5;

    // Adaptive decode-load shedding: when the GUI can't keep up (backpressure
    // drops), the decoder switches to AVDISCARD_NONREF so non-reference
    // frames are never decoded at all instead of being fully decoded and then
    // thrown away — the enterprise policy that keeps a saturated wall from
    // burning CPU/GPU on invisible frames. Restored the moment frames flow.
    int dropStreak = 0;
    bool skippingNonRef = false;
    constexpr int kDropStreakForSkip = 3;

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
                        // New codec context: reset load-shedding state and
                        // record the new URL so a later auto-reconnect
                        // re-opens THIS profile, not the original one.
                        dropStreak = 0;
                        skippingNonRef = false;
                        {
                            std::lock_guard<std::mutex> l(url_mutex_);
                            active_url_ = target;
                        }
                        emit keyframeAligned();
                        break;
                    }
                    av_packet_unref(p2);
                }
                av_packet_free(&p2);
            } else {
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

        const int readErr = av_read_frame(fmt, pkt);
        if (readErr < 0) {
            if (!running_.load()) break;
            if (fmt->pb && avio_feof(fmt->pb)) {
                if (isLocalFile) {
                    // Loop local files for demo/kiosk playback.
                    av_seek_frame(fmt, videoStream, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(codec);
                    waiting_for_keyframe = true;
                    continue;
                }
                // Network EOF = peer closed the connection. The old code
                // retried av_read_frame on the dead context in a 100ms loop
                // forever — the cell froze on its last frame permanently.
                result = SessionResult::StreamError;
                break;
            }
            if (readErr == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(milliseconds(10));
                continue;
            }
            // Persistent read failures on a network stream mean the
            // connection is gone — hand control to the reconnect supervisor
            // instead of spinning here.
            if (!isLocalFile &&
                ++consecutiveReadErrors >= kMaxConsecutiveReadErrors) {
                result = SessionResult::StreamError;
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

            // GPU-decoded frames live in video memory; transfer to a CPU
            // frame (typically NV12) before presentation.
            AVFrame* out = frame;
            if (hw_pix_fmt_ != AV_PIX_FMT_NONE && frame->format == hw_pix_fmt_) {
                if (av_hwframe_transfer_data(swFrame, frame, 0) >= 0) {
                    out = swFrame;
                }
            }

            const bool presented = presentFrame(out, utcUs);
            if (presented) {
                presentedAnyFrame = true;
                dropStreak = 0;
                if (skippingNonRef) {
                    codec->skip_frame = AVDISCARD_DEFAULT;
                    skippingNonRef = false;
                }
            } else if (!playbackMode && !skippingNonRef &&
                       ++dropStreak >= kDropStreakForSkip) {
                // GUI is saturated: stop decoding non-reference frames
                // entirely instead of decoding and discarding them.
                codec->skip_frame = AVDISCARD_NONREF;
                skippingNonRef = true;
            }

            // کنترل نرخ فریم برای فایل‌های محلی (Pacing)
            if (isLocalFile || playbackMode) {
                const double currentRate = rate_.load();
                const double effectiveFps = sourceFps * (currentRate > 0 ? currentRate : 1.0);
                const int frameDelayMs = static_cast<int>(1000.0 / (effectiveFps > 0 ? effectiveFps : 25.0));

                auto now = steady_clock::now();
                auto elapsedMs = duration_cast<milliseconds>(now - lastFrameTime).count();
                int sleepMs = frameDelayMs - static_cast<int>(elapsedMs);

                // Interruptible pacing: sleep in small chunks so stop() never
                // waits behind a long uninterruptible sleep.
                if (sleepMs > 0 && sleepMs < 500) {
                    while (sleepMs > 0 && running_.load()) {
                        const int chunk = std::min(sleepMs, 20);
                        std::this_thread::sleep_for(milliseconds(chunk));
                        sleepMs -= chunk;
                    }
                }
                lastFrameTime = steady_clock::now();
            }

            av_frame_unref(frame);
            av_frame_unref(swFrame);
        }
    }

    av_frame_free(&swFrame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (nextCodec) avcodec_free_context(&nextCodec);
    if (nextFmt) avformat_close_input(&nextFmt);
    avcodec_free_context(&codec);
    avformat_close_input(&fmt);

    if (!running_.load()) return SessionResult::Stopped;
    return result;
}

void VideoDecoder::setStatusFa(const QString& s) {
    if (status_fa_ == s) return;
    status_fa_ = s;
    QMetaObject::invokeMethod(this, [this] { emit statusFaChanged(); },
                              Qt::QueuedConnection);
}

}  // namespace vms
