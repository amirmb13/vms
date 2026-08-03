#include "stream/stream_controller.h"
#include "stream/video_decoder.h"
#include <QVideoSink>

namespace vms {

StreamController::StreamController(QObject* parent) : QObject(parent) {}

StreamController::~StreamController() { detachAll(); }

QString StreamController::liveUrl(const QString& cameraUuid,
                                  const QString& profile) const {
    if (cameraUuid.startsWith(QLatin1String("http://")) ||
        cameraUuid.startsWith(QLatin1String("https://")) ||
        cameraUuid.startsWith(QLatin1String("rtsp://"))) {
        return cameraUuid;
    }
    return QStringLiteral("rtsp://%1:8554/live/%2/%3")
        .arg(relay_host_, cameraUuid, profile);
}

QString StreamController::archiveUrl(const QString& cameraUuid,
                                     quint64 startUtcUs) const {
    return QStringLiteral("rtsp://%1:8554/archive/%2?start=%3")
    .arg(relay_host_, cameraUuid, QString::number(startUtcUs));
}

std::shared_ptr<VideoDecoder> StreamController::getOrCreateDecoder(int cellIndex) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    auto it = sessions_.find(cellIndex);
    if (it != sessions_.end() && it->second.decoder) {
        return it->second.decoder;
    }

    CellSession session;
    session.decoder = std::make_shared<VideoDecoder>();

    VideoDecoder* decPtr = session.decoder.get();
    connect(decPtr, &VideoDecoder::framePresented, this,
            [this, cellIndex](quint64 utcUs) {
                emit cellFramePresented(cellIndex, utcUs);
            });
    connect(decPtr, &VideoDecoder::statusFaChanged, this, [this, cellIndex, decPtr] {
        emit cellStatusChanged(cellIndex, decPtr->statusFa());
    });

    sessions_[cellIndex] = session;
    return session.decoder;
}

void StreamController::attachLive(int cellIndex, const QString& cameraUuid,
                                  const QString& profile, QVideoSink* sink) {
    if (!sink || cameraUuid.isEmpty()) return;

    std::shared_ptr<VideoDecoder> decoder = getOrCreateDecoder(cellIndex);

    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        CellSession& s = sessions_[cellIndex];
        s.cameraUuid = cameraUuid;
        s.profile = profile;
        s.playbackMode = false;
    }

    decoder->startLive(liveUrl(cameraUuid, profile), sink);
}

void StreamController::switchProfile(int cellIndex, const QString& profile) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    auto it = sessions_.find(cellIndex);
    if (it == sessions_.end() || it->second.playbackMode || !it->second.decoder) return;

    CellSession& s = it->second;
    if (s.profile == profile) return;

    s.profile = profile;
    s.decoder->switchTo(liveUrl(s.cameraUuid, profile));
}

void StreamController::attachPlayback(int cellIndex, const QString& cameraUuid,
                                      QVideoSink* sink, quint64 startUtcUs) {
    if (!sink || cameraUuid.isEmpty()) return;

    std::shared_ptr<VideoDecoder> decoder = getOrCreateDecoder(cellIndex);

    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        CellSession& s = sessions_[cellIndex];
        s.cameraUuid = cameraUuid;
        s.playbackMode = true;
    }

    decoder->startPlayback(archiveUrl(cameraUuid, startUtcUs), sink, startUtcUs);
}

void StreamController::detach(int cellIndex) {
    std::shared_ptr<VideoDecoder> decToStop;
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        auto it = sessions_.find(cellIndex);
        if (it != sessions_.end()) {
            decToStop = it->second.decoder;
            sessions_.erase(it);
        }
    }
    if (decToStop) {
        decToStop->stop();
    }
}

void StreamController::detachAll() {
    std::vector<std::shared_ptr<VideoDecoder>> decodersToStop;
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        for (auto& [idx, s] : sessions_) {
            if (s.decoder) decodersToStop.push_back(s.decoder);
        }
        sessions_.clear();
    }
    for (auto& dec : decodersToStop) {
        dec->stop();
    }
}

void StreamController::broadcastSeek(quint64 utcUs) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    for (auto& [idx, s] : sessions_) {
        if (s.playbackMode && s.decoder) s.decoder->seekTo(utcUs);
    }
}

void StreamController::broadcastPaused(bool paused) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    for (auto& [idx, s] : sessions_) {
        if (s.playbackMode && s.decoder) s.decoder->setPaused(paused);
    }
}

void StreamController::broadcastRate(double rate) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    for (auto& [idx, s] : sessions_) {
        if (s.playbackMode && s.decoder) s.decoder->setRate(rate);
    }
}

QString StreamController::profileForCellSize(qreal cellWidth, qreal gridWidth) const {
    if (gridWidth <= 0) return QStringLiteral("sub");
    const qreal ratio = cellWidth / gridWidth;
    if (ratio > 0.5) return QStringLiteral("main");
    if (ratio > 0.25) return QStringLiteral("mid");
    return QStringLiteral("sub");
}

void StreamController::setRelayHost(const QString& host) {
    if (relay_host_ == host) return;
    relay_host_ = host;
    emit relayHostChanged();
}

}  // namespace vms