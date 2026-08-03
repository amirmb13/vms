#include "stream/video_decoder.h"

#include <QPointer>
#include <QVideoFrame>
#include <QVideoFrameFormat>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>

namespace vms {

VideoDecoder::VideoDecoder(QObject* parent) : QObject(parent) {}

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

void VideoDecoder::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    emit decodingChanged();
}

// --- Input setup --------------------------------------------------------------
bool VideoDecoder::openInput(const QString& url, AVFormatContext*& fmt,
                             AVCodecContext*& codec, int& videoStream) {
    fmt = nullptr;
    AVDictionary* opts = nullptr;

    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "3000000", 0);
    av_dict_set(&opts, "timeout", "3000000", 0);
    av_dict_set(&opts, "rw_timeout", "3000000", 0);

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

    // دکود نرم‌افزاری چندتردی برای پایداری کامل
    codec->thread_count = 0;
    codec->thread_type = FF_THREAD_FRAME;

    setStatusFa(QStringLiteral("رمزگشایی فعال شد"));
    return avcodec_open2(codec, dec, nullptr) >= 0;
}

bool VideoDecoder::initHardwareDecoder(AVCodecContext*) {
    return false;
}

// --- Frame presentation (Thread-safe; each decoder owns its own sink) --------
void VideoDecoder::presentFrame(AVFrame* frame, quint64 utcUs) {
    if (!sink_ || !frame || frame->width <= 0 || frame->height <= 0 ||
        frame->format == AV_PIX_FMT_NONE || !frame->data[0]) return;

    const int width = frame->width;
    const int height = frame->height;

    // Cache the scaler across frames (only this worker thread touches sws_).
    // Recreating SwsContext per frame costs milliseconds per call and cripples
    // multi-camera walls.
    sws_ = sws_getCachedContext(sws_, width, height,
                                static_cast<AVPixelFormat>(frame->format),
                                width, height, AV_PIX_FMT_RGBA,
                                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) return;

    // Scale straight into the QVideoFrame's own buffer: zero intermediate
    // copies. Each QVideoFrame owns its memory, so cells stay fully isolated.
    QVideoFrameFormat fmtDesc(QSize(width, height),
                              QVideoFrameFormat::Format_RGBA8888);
    QVideoFrame vf(fmtDesc);
    if (!vf.isValid() || !vf.map(QVideoFrame::WriteOnly)) return;

    uint8_t* dst[4] = { vf.bits(0), nullptr, nullptr, nullptr };
    int dstStride[4] = { vf.bytesPerLine(0), 0, 0, 0 };
    sws_scale(sws_, frame->data, frame->linesize, 0, height, dst, dstStride);
    vf.unmap();

    // Guard BOTH the sink and this decoder: the queued lambda may run on the
    // GUI thread after either has been destroyed.
    QPointer<QVideoSink> safeSink = sink_;
    QPointer<VideoDecoder> safeSelf = this;

    QMetaObject::invokeMethod(
        sink_,
        [safeSink, safeSelf, vf, utcUs]() {
            if (safeSink) safeSink->setVideoFrame(vf);
            if (safeSelf) emit safeSelf->framePresented(utcUs);
        },
        Qt::QueuedConnection);
}

// --- Decode loop -----------------------------------------------------------------
void VideoDecoder::decodeLoop(QString url, bool playbackMode) {
    using namespace std::chrono;

    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    int videoStream = -1;

    if (!openInput(url, fmt, codec, videoStream)) {
        running_.store(false);
        return;
    }
    setStatusFa(QStringLiteral("در حال پخش"));

    AVRational fr = av_guess_frame_rate(fmt, fmt->streams[videoStream], nullptr);
    double sourceFps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;

    const bool isLocalFile = !url.startsWith(QLatin1String("rtsp://")) &&
                             !url.startsWith(QLatin1String("http://")) &&
                             !url.startsWith(QLatin1String("https://"));

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    AVFormatContext* nextFmt = nullptr;
    AVCodecContext* nextCodec = nullptr;
    int nextStream = -1;

    bool waiting_for_keyframe = true;
    auto lastFrameTime = steady_clock::now();

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
                av_seek_frame(fmt, videoStream, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codec);
                waiting_for_keyframe = true;
                continue;
            }
            std::this_thread::sleep_for(milliseconds(100));
            continue;
        }

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

            presentFrame(frame, utcUs);

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

    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (nextCodec) avcodec_free_context(&nextCodec);
    if (nextFmt) avformat_close_input(&nextFmt);
    avcodec_free_context(&codec);
    avformat_close_input(&fmt);
}

void VideoDecoder::setStatusFa(const QString& s) {
    if (status_fa_ == s) return;
    status_fa_ = s;
    QMetaObject::invokeMethod(this, [this] { emit statusFaChanged(); },
                              Qt::QueuedConnection);
}

}  // namespace vms
