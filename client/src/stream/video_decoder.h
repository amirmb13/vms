#pragma once
// =============================================================================
// VideoDecoder — one FFmpeg decode worker per video surface.
//
// - Opens an RTSP session against the C++ Media Relay (NEVER the camera).
// - Hardware-agnostic decode chain (mandate §2):
//     1. D3D11VA / DXVA2 (Windows)  2. CUDA/NVDEC  3. VAAPI  4. QSV
//     5. CPU software decode fallback (multi-threaded, AVX2-optimized libav)
// - Decoded frames are wrapped into QVideoFrame and handed to the QVideoSink
//   supplied by VideoCell.qml's VideoOutput — the Qt scene graph uploads them
//   as QSGTexture on the RHI backend (D3D12/Vulkan), keeping the GUI thread
//   free (60 FPS with 60 concurrent feeds).
// - Seamless profile switching: the next session is opened in parallel and
//   the swap happens exactly on the first keyframe (I-frame alignment) so no
//   black screens / flicker occur.
// - Playback mode: seeks the relay's archive endpoint and paces frames against
//   the master SyncPlayback NTP timeline instead of the wall clock.
// =============================================================================
#include <QObject>
#include <QString>
#include <QVideoSink>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

struct AVFormatContext;
struct AVCodecContext;
struct AVBufferRef;
struct SwsContext;

namespace vms {

class VideoDecoder : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool decoding READ decoding NOTIFY decodingChanged)
    Q_PROPERTY(QString statusFa READ statusFa NOTIFY statusFaChanged)

public:
    explicit VideoDecoder(QObject* parent = nullptr);
    ~VideoDecoder() override;

    // Live: open `url` (relay RTSP) and start pushing frames to `sink`.
    void startLive(const QString& url, QVideoSink* sink);

    // Archive playback: open the relay playback URL positioned at utc_us.
    void startPlayback(const QString& url, QVideoSink* sink,
                       quint64 startUtcUs);

    // Seamless switch — opens the new URL on a side thread and swaps on the
    // first decoded I-frame of the new session.
    void switchTo(const QString& url);

    // Sync playback control (driven by SyncPlayback broadcast).
    void seekTo(quint64 utcUs);
    void setPaused(bool paused);
    void setRate(double rate);

    void stop();

    bool decoding() const { return running_.load(); }
    QString statusFa() const { return status_fa_; }

signals:
    void decodingChanged();
    void statusFaChanged();
    // NTP timestamp of the most recently presented frame (Shamsi overlay +
    // sync-playback drift correction).
    void framePresented(quint64 utcUs);
    void keyframeAligned();   // fired when switchTo() completes the swap

private:
    void decodeLoop(QString url, bool playbackMode);
    bool openInput(const QString& url, AVFormatContext*& fmt,
                   AVCodecContext*& codec, int& videoStream);
    bool initHardwareDecoder(AVCodecContext* codec);   // tries HW list, CPU last
    void presentFrame(struct AVFrame* frame, quint64 utcUs);
    void setStatusFa(const QString& s);

    QVideoSink* sink_ = nullptr;              // owned by QML VideoOutput
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<double> rate_{1.0};
    std::atomic<quint64> seek_target_us_{0};  // 0 = no pending seek
    std::atomic<bool> switch_pending_{false};

    std::mutex url_mutex_;
    QString pending_url_;                     // target of switchTo()

    AVBufferRef* hw_device_ctx_ = nullptr;
    SwsContext* sws_ = nullptr;
    QString status_fa_;
};

}  // namespace vms
