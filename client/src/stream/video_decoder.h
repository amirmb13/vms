#ifndef VMS_STREAM_VIDEO_DECODER_H
#define VMS_STREAM_VIDEO_DECODER_H

#include <QObject>
#include <QString>
#include <QVideoSink>
#include <atomic>
#include <memory>
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

    // Non-blocking stop request: flips running_ so the AVIO interrupt
    // callback aborts any blocking open/read immediately. Lets
    // StreamController::detachAll() signal EVERY worker first and only then
    // join them, so a full-wall layout switch pays one shutdown latency
    // instead of (cells × latency) serially on the GUI thread.
    void requestStop();

    QString statusFa() const { return status_fa_; }

signals:
    void decodingChanged();
    void framePresented(quint64 utcUs);
    void keyframeAligned();
    void statusFaChanged();

private:
    // Outcome of one connect-decode session; drives the reconnect policy.
    enum class SessionResult {
        Stopped,      // running_ dropped — clean shutdown, never reconnect
        OpenFailed,   // connect/handshake failed — reconnect (network live)
        StreamError,  // died mid-stream — reconnect (network live)
        Finished,     // local file / non-recoverable — no reconnect
    };

    bool openInput(const QString& url, AVFormatContext*& fmt,
                   AVCodecContext*& codec, int& videoStream);
    bool initHardwareDecoder(AVCodecContext* codec, const AVCodec* dec);
    static AVPixelFormat getHwFormat(AVCodecContext* ctx,
                                     const AVPixelFormat* formats);
    // Returns true if the frame was queued to the GUI, false if it was
    // dropped by backpressure (feeds the adaptive decode-skip policy).
    bool presentFrame(AVFrame* frame, quint64 utcUs);
    void decodeLoop(QString url, bool playbackMode);
    // One full open→decode→teardown pass. decodeLoop wraps it with the
    // exponential-backoff reconnect loop for live network streams.
    SessionResult runSession(const QString& url, bool playbackMode,
                             bool& presentedAnyFrame);
    void setStatusFa(const QString& s);

    QVideoSink* sink_{nullptr};
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> switch_pending_{false};
    std::atomic<quint64> seek_target_us_{0};
    std::atomic<double> rate_{1.0};

    std::mutex url_mutex_;
    QString pending_url_;
    // URL of the stream currently being decoded. Updated when a seamless
    // profile switch lands, so an auto-reconnect after a network drop
    // re-opens the CURRENT profile instead of the one startLive() began with.
    QString active_url_;
    QString status_fa_;

    SwsContext* sws_{nullptr};

    // Negotiated hardware surface format (AV_PIX_FMT_NONE = software decode).
    AVPixelFormat hw_pix_fmt_{AV_PIX_FMT_NONE};

    // Frames queued to the GUI thread but not yet consumed. shared_ptr so the
    // queued lambda can decrement safely even if this decoder is destroyed
    // before the GUI thread drains its queue.
    std::shared_ptr<std::atomic<int>> frames_in_flight_;
};

}  // namespace vms

#endif  // VMS_STREAM_VIDEO_DECODER_H
