#include "stream/stream_controller.h"

#include "stream/video_decoder.h"

#include <QVideoSink>

namespace vms {

StreamController::StreamController(QObject* parent) : QObject(parent) {}

StreamController::~StreamController() { detachAll(); }

// --- URL construction (relay only — never a camera address) -------------------
QString StreamController::liveUrl(const QString& cameraUuid,
                                  const QString& profile) const {
    return QStringLiteral("rtsp://%1:8554/live/%2/%3")
        .arg(relay_host_, cameraUuid, profile);
}

QString StreamController::archiveUrl(const QString& cameraUuid,
                                     quint64 startUtcUs) const {
    return QStringLiteral("rtsp://%1:8554/archive/%2?start=%3")
        .arg(relay_host_, cameraUuid, QString::number(startUtcUs));
}

StreamController::CellSession& StreamController::sessionFor(int cellIndex) {
    auto it = sessions_.find(cellIndex);
    if (it != sessions_.end()) return it->second;

    CellSession session;
    session.decoder = std::make_unique<VideoDecoder>();

    // Bubble decoder telemetry up with the cell index attached.
    VideoDecoder* dec = session.decoder.get();
    connect(dec, &VideoDecoder::framePresented, this,
            [this, cellIndex](quint64 utcUs) {
                emit cellFramePresented(cellIndex, utcUs);
            });
    connect(dec, &VideoDecoder::statusFaChanged, this, [this, cellIndex, dec] {
        emit cellStatusChanged(cellIndex, dec->statusFa());
    });

    return sessions_.emplace(cellIndex, std::move(session)).first->second;
}

// --- Session management ---------------------------------------------------------
void StreamController::attachLive(int cellIndex, const QString& cameraUuid,
                                  const QString& profile, QVideoSink* sink) {
    CellSession& s = sessionFor(cellIndex);
    s.cameraUuid = cameraUuid;
    s.profile = profile;
    s.playbackMode = false;
    s.decoder->startLive(liveUrl(cameraUuid, profile), sink);
}

void StreamController::switchProfile(int cellIndex, const QString& profile) {
    auto it = sessions_.find(cellIndex);
    if (it == sessions_.end() || it->second.playbackMode) return;
    CellSession& s = it->second;
    if (s.profile == profile) return;
    s.profile = profile;
    // I-frame-aligned swap inside the decoder — no black frames.
    s.decoder->switchTo(liveUrl(s.cameraUuid, profile));
}

void StreamController::attachPlayback(int cellIndex,
                                      const QString& cameraUuid,
                                      QVideoSink* sink, quint64 startUtcUs) {
    CellSession& s = sessionFor(cellIndex);
    s.cameraUuid = cameraUuid;
    s.playbackMode = true;
    s.decoder->startPlayback(archiveUrl(cameraUuid, startUtcUs), sink,
                             startUtcUs);
}

void StreamController::detach(int cellIndex) {
    auto it = sessions_.find(cellIndex);
    if (it == sessions_.end()) return;
    it->second.decoder->stop();
    sessions_.erase(it);
}

void StreamController::detachAll() {
    for (auto& [idx, s] : sessions_) s.decoder->stop();
    sessions_.clear();
}

// --- Sync playback fan-out --------------------------------------------------------
void StreamController::broadcastSeek(quint64 utcUs) {
    for (auto& [idx, s] : sessions_)
        if (s.playbackMode) s.decoder->seekTo(utcUs);
}

void StreamController::broadcastPaused(bool paused) {
    for (auto& [idx, s] : sessions_)
        if (s.playbackMode) s.decoder->setPaused(paused);
}

void StreamController::broadcastRate(double rate) {
    for (auto& [idx, s] : sessions_)
        if (s.playbackMode) s.decoder->setRate(rate);
}

// --- Adaptive profile policy --------------------------------------------------------
QString StreamController::profileForCellSize(qreal cellWidth,
                                             qreal gridWidth) const {
    if (gridWidth <= 0) return QStringLiteral("sub");
    const qreal ratio = cellWidth / gridWidth;
    if (ratio > 0.5) return QStringLiteral("main");   // fullscreen / 2x2 merged
    if (ratio > 0.25) return QStringLiteral("mid");   // 4-camera grid
    return QStringLiteral("sub");                     // dense wall (16..64)
}

void StreamController::setRelayHost(const QString& host) {
    if (relay_host_ == host) return;
    relay_host_ = host;
    emit relayHostChanged();
}

}  // namespace vms
