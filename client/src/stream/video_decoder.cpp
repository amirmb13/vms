#include "stream/video_decoder.h"

#include <QVideoFrame>
#include <QVideoFrameFormat>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>

namespace vms {
namespace {

// Hardware decoder preference order — mandate §2 (GPU first, CPU fallback).
constexpr AVHWDeviceType kHwPreference[] = {
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2,
#endif
    AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_QSV,
};

}  // namespace

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
    if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
    if (hw_device_ctx_) { av_buffer_unref(&hw_device_ctx_); }
    emit decodingChanged();
}

// --- Input / hardware setup -----------------------------------------------------
bool VideoDecoder::openInput(const QString& url, AVFormatContext*& fmt,
                             AVCodecContext*& codec, int& videoStream) {
    fmt = nullptr;
    AVDictionary* opts = nullptr;
    // Relay is on a trusted LAN: prefer TCP interleave for loss-free delivery.
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);  // 5s socket timeout

    if (avformat_open_input(&fmt, url.toUtf8().constData(), nullptr, &opts) < 0) {
        av_dict_free(&opts);
        setStatusFa(QStringLiteral("اتصال به سرور بازپخش برقرار نشد"));
        return false;
    }
    av_dict_free(&opts);
    if (avformat_find_stream_info(fmt, nullptr) < 0) return false;

    videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
                                      nullptr, 0);
    if (videoStream < 0) return false;

    const AVCodec* dec =
        avcodec_find_decoder(fmt->streams[videoStream]->codecpar->codec_id);
    if (!dec) return false;

    codec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(codec, fmt->streams[videoStream]->codecpar);
    codec->thread_count = 0;  // auto — CPU fallback uses all cores

    initHardwareDecoder(codec);  // best-effort; CPU decode if all HW fails

    return avcodec_open2(codec, dec, nullptr) >= 0;
}

bool VideoDecoder::initHardwareDecoder(AVCodecContext* codec) {
    for (AVHWDeviceType type : kHwPreference) {
        AVBufferRef* ctx = nullptr;
        if (av_hwdevice_ctx_create(&ctx, type, nullptr, nullptr, 0) == 0) {
            hw_device_ctx_ = ctx;
            codec->hw_device_ctx = av_buffer_ref(ctx);
            setStatusFa(QStringLiteral("رمزگشایی سخت‌افزاری فعال شد"));
            return true;
        }
    }
    setStatusFa(QStringLiteral("رمزگشایی نرم‌افزاری (پردازنده) فعال شد"));
    return false;  // CPU software decode fallback
}

// --- Frame presentation -----------------------------------------------------------
void VideoDecoder::presentFrame(AVFrame* frame, quint64 utcUs) {
    if (!sink_) return;

    // If the frame lives in VRAM, transfer once (zero extra copies after this).
    AVFrame* cpu = frame;
    AVFrame* transfer = nullptr;
    if (frame->hw_frames_ctx) {
        transfer = av_frame_alloc();
        if (av_hwframe_transfer_data(transfer, frame, 0) < 0) {
            av_frame_free(&transfer);
            return;
        }
        cpu = transfer;
    }

    // Wrap into a mappable QVideoFrame; the scene graph uploads it as a
    // QSGTexture on the RHI thread.
    QVideoFrameFormat format(QSize(cpu->width, cpu->height),
                             QVideoFrameFormat::Format_NV12);
    QVideoFrame vf(format);
    if (vf.map(QVideoFrame::WriteOnly)) {
        // Convert whatever pixel format arrived into NV12 in-place.
        sws_ = sws_getCachedContext(
            sws_, cpu->width, cpu->height,
            static_cast<AVPixelFormat>(cpu->format), cpu->width, cpu->height,
            AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
        uint8_t* dst[2] = {vf.bits(0), vf.bits(1)};
        int dstStride[2] = {vf.bytesPerLine(0), vf.bytesPerLine(1)};
        sws_scale(sws_, cpu->data, cpu->linesize, 0, cpu->height, dst,
                  dstStride);
        vf.unmap();
        sink_->setVideoFrame(vf);
        emit framePresented(utcUs);
    }
    if (transfer) av_frame_free(&transfer);
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

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // Pending seamless switch state.
    AVFormatContext* nextFmt = nullptr;
    AVCodecContext* nextCodec = nullptr;
    int nextStream = -1;

    while (running_.load()) {
        // --- Seamless profile switch: open next session, swap at I-frame ----
        if (switch_pending_.load() && !nextFmt) {
            QString target;
            { std::lock_guard<std::mutex> l(url_mutex_); target = pending_url_; }
            if (openInput(target, nextFmt, nextCodec, nextStream)) {
                // Drain until the FIRST keyframe of the new session, then swap.
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

        // --- Sync-playback seek (archive mode) ------------------------------
        if (playbackMode) {
            const quint64 target = seek_target_us_.exchange(0);
            if (target != 0) {
                const int64_t ts = av_rescale_q(
                    static_cast<int64_t>(target), AVRational{1, 1000000},
                    fmt->streams[videoStream]->time_base);
                av_seek_frame(fmt, videoStream, ts, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codec);
            }
            if (paused_.load()) {
                std::this_thread::sleep_for(milliseconds(20));
                continue;
            }
        }

        if (av_read_frame(fmt, pkt) < 0) {
            std::this_thread::sleep_for(milliseconds(100));
            continue;
        }
        if (pkt->stream_index != videoStream) {
            av_packet_unref(pkt);
            continue;
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

            // Archive pacing honours the sync-playback rate.
            if (playbackMode) {
                const double r = rate_.load();
                const int frameMs = static_cast<int>(40.0 / (r > 0 ? r : 1.0));
                std::this_thread::sleep_for(milliseconds(frameMs));
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
    // Cross-thread safe: emit via queued connection semantics.
    QMetaObject::invokeMethod(this, [this] { emit statusFaChanged(); },
                              Qt::QueuedConnection);
}

}  // namespace vms
