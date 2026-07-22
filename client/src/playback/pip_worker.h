#pragma once
// =============================================================================
// PipWorker — Picture-in-Picture archive review inside a live cell.
//
// Mandate §3.B: spawns an INDEPENDENT C++ worker (own VideoDecoder thread)
// that fetches + decodes archived video from the relay's archive endpoint
// while the parent cell's live stream continues uninterrupted.
//
// QML usage (VideoCell.qml):
//   pip.open(cameraUuid, startUtcUs, pipVideoOutput.videoSink)
//   pip.seek(us) / pip.setPaused(true) / pip.close()
// =============================================================================
#include <QObject>
#include <QString>

#include <memory>

class QVideoSink;

namespace vms {

class VideoDecoder;
class StreamController;

class PipWorker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(quint64 positionUtcUs READ positionUtcUs
                   NOTIFY positionChanged)

public:
    explicit PipWorker(QObject* parent = nullptr);
    ~PipWorker() override;

    // Relay host is shared with the main StreamController configuration.
    void setRelayHost(const QString& host);

    Q_INVOKABLE void open(const QString& cameraUuid, quint64 startUtcUs,
                          QVideoSink* sink);
    Q_INVOKABLE void seek(quint64 utcUs);
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void setRate(double rate);
    Q_INVOKABLE void close();

    bool active() const { return active_; }
    quint64 positionUtcUs() const { return position_us_; }

signals:
    void activeChanged();
    void positionChanged();

private:
    std::unique_ptr<VideoDecoder> decoder_;   // independent worker thread
    QString relay_host_ = QStringLiteral("127.0.0.1");
    bool active_ = false;
    quint64 position_us_ = 0;
};

}  // namespace vms
