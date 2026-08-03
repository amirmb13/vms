#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <unordered_map>
#include <mutex>

class QVideoSink;

namespace vms {

class VideoDecoder;

class StreamController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString relayHost READ relayHost WRITE setRelayHost NOTIFY relayHostChanged)

public:
    explicit StreamController(QObject* parent = nullptr);
    ~StreamController() override;

    Q_INVOKABLE void attachLive(int cellIndex, const QString& cameraUuid,
                                const QString& profile, QVideoSink* sink);

    Q_INVOKABLE void switchProfile(int cellIndex, const QString& profile);

    Q_INVOKABLE void attachPlayback(int cellIndex, const QString& cameraUuid,
                                    QVideoSink* sink, quint64 startUtcUs);

    Q_INVOKABLE void detach(int cellIndex);
    Q_INVOKABLE void detachAll();

    void broadcastSeek(quint64 utcUs);
    void broadcastPaused(bool paused);
    void broadcastRate(double rate);

    Q_INVOKABLE QString profileForCellSize(qreal cellWidth, qreal gridWidth) const;

    QString relayHost() const { return relay_host_; }
    void setRelayHost(const QString& host);

signals:
    void relayHostChanged();
    void cellFramePresented(int cellIndex, quint64 utcUs);
    void cellStatusChanged(int cellIndex, const QString& statusFa);

private:
    struct CellSession {
        std::shared_ptr<VideoDecoder> decoder;
        QString cameraUuid;
        QString profile;
        bool playbackMode = false;
    };

    QString liveUrl(const QString& cameraUuid, const QString& profile) const;
    QString archiveUrl(const QString& cameraUuid, quint64 startUtcUs) const;
    std::shared_ptr<VideoDecoder> getOrCreateDecoder(int cellIndex);

    QString relay_host_ = QStringLiteral("127.0.0.1");
    std::unordered_map<int, CellSession> sessions_;
    std::mutex session_mutex_;
};

}  // namespace vms