#pragma once
// =============================================================================
// StreamController — adaptive streaming brain of the desktop client.
//
// Owns one VideoDecoder per grid cell and negotiates relay sessions with the
// C++ Media Relay Engine (mandate §1.3: clients NEVER touch cameras).
//
// Adaptive profile policy (mandate §3.B):
//   - 64-camera grid  -> Sub-Stream  (360p)
//   - 4-camera grid   -> Mid-Stream  (720p/1080p)
//   - fullscreen      -> Main-Stream (4K), swapped on I-frame alignment
//
// Relay URL scheme (RTSP proxy/reflector):
//   rtsp://<relay-host>:8554/live/<camera_uuid>/<profile>       (live)
//   rtsp://<relay-host>:8554/archive/<camera_uuid>?start=<utc>  (playback)
// =============================================================================
#include <QObject>
#include <QString>

#include <memory>
#include <unordered_map>

class QVideoSink;

namespace vms {

class VideoDecoder;

class StreamController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString relayHost READ relayHost WRITE setRelayHost
                   NOTIFY relayHostChanged)

public:
    explicit StreamController(QObject* parent = nullptr);
    ~StreamController() override;

    // Attach a live stream to a cell surface. `profile` is "sub"|"mid"|"main".
    Q_INVOKABLE void attachLive(int cellIndex, const QString& cameraUuid,
                                const QString& profile, QVideoSink* sink);

    // Seamless in-place profile change (double-click fullscreen etc.).
    Q_INVOKABLE void switchProfile(int cellIndex, const QString& profile);

    // Switch a cell into archive playback at a UTC microsecond position.
    Q_INVOKABLE void attachPlayback(int cellIndex, const QString& cameraUuid,
                                    QVideoSink* sink, quint64 startUtcUs);

    Q_INVOKABLE void detach(int cellIndex);
    Q_INVOKABLE void detachAll();

    // Sync-playback fan-out (called by SyncPlayback broadcast).
    void broadcastSeek(quint64 utcUs);
    void broadcastPaused(bool paused);
    void broadcastRate(double rate);

    // Auto profile decision from cell pixel size (used by VideoCell.qml).
    Q_INVOKABLE QString profileForCellSize(qreal cellWidth,
                                           qreal gridWidth) const;

    QString relayHost() const { return relay_host_; }
    void setRelayHost(const QString& host);

signals:
    void relayHostChanged();
    // Bubbled up from decoders — VideoCell overlays bind to these.
    void cellFramePresented(int cellIndex, quint64 utcUs);
    void cellStatusChanged(int cellIndex, const QString& statusFa);

private:
    struct CellSession {
        std::unique_ptr<VideoDecoder> decoder;
        QString cameraUuid;
        QString profile;
        bool playbackMode = false;
    };

    QString liveUrl(const QString& cameraUuid, const QString& profile) const;
    QString archiveUrl(const QString& cameraUuid, quint64 startUtcUs) const;
    CellSession& sessionFor(int cellIndex);

    QString relay_host_ = QStringLiteral("127.0.0.1");
    std::unordered_map<int, CellSession> sessions_;
};

}  // namespace vms
