#ifndef VMS_STREAM_VIDEO_DECODER_H
#define VMS_STREAM_VIDEO_DECODER_H

#include <QObject>
#include <QString>
#include <QVideoSink>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// FFmpeg C headers MUST be included in global scope inside extern "C"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

namespace vms {

class VideoDecoder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusFa READ statusFa NOTIFY statusFaChanged)

public:
    explicit VideoDecoder(QObject* parent = nullptr);
    ~VideoDecoder() override;

    Q_INVOKABLE void startLive(const QString& url, QVideoSink* sink);
    Q_INVOKABLE void startPlayback(const QString& url, QVideoSink* sink, quint64 startUtcUs);
    Q_INVOKABLE void switchTo(const QString& url);
    Q_INVOKABLE void seekTo(quint64 utcUs);
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void setRate(double rate);
    Q_INVOKABLE void stop();

    QString statusFa() const { return status_fa_; }

signals:
    void decodingChanged();
    void framePresented(quint64 utcUs);
    void keyframeAligned();
    void statusFaChanged();

private:
    // Outcome of one decode session; Reconnect triggers the backoff loop.
    enum class SessionResult { Stopped, Reconnect, OpenFailed };

    bool openInput(const QString& url, AVFormatContext*& fmt,
                   AVCodecContext*& codec, int& videoStream);
    bool initHardwareDecoder(AVCodecContext* codec, const AVCodec* dec);
    void presentFrame(AVFrame* frame, quint64 utcUs);
    void decodeLoop(QString url, bool playbackMode);
    SessionResult decodeSession(const QString& url, bool playbackMode);
    void setStatusFa(const QString& s);
    void interruptibleSleep(int ms);

    // FFmpeg C callbacks (opaque = this)
    static int interruptCb(void* opaque);
    static AVPixelFormat selectHwFormat(AVCodecContext* ctx,
                                        const AVPixelFormat* fmts);

    QVideoSink* sink_{nullptr};
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> switch_pending_{false};
    std::atomic<quint64> seek_target_us_{0};
    std::atomic<double> rate_{1.0};

    // Backpressure: frames handed to the GUI thread but not yet consumed.
    // When the render thread falls behind we drop presentation (never decode),
    // keeping latency and memory bounded on large video walls.
    std::atomic<int> frames_in_flight_{0};

    std::mutex url_mutex_;
    QString pending_url_;
    QString status_fa_;

    SwsContext* sws_{nullptr};

    // Negotiated hardware pixel format (AV_PIX_FMT_NONE = software decode).
    AVPixelFormat hw_pix_fmt_{AV_PIX_FMT_NONE};

    // Overlay tick throttling (worker thread only).
    std::chrono::steady_clock::time_point last_tick_{};
    bool first_frame_presented_{false};
};

}  // namespace vms

#endif  // VMS_STREAM_VIDEO_DECODER_H
