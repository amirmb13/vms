#include "stream/stream_controller.h"
#include "stream/video_decoder.h"
#include <QDateTime>
#include <QVideoSink>

namespace vms {

StreamController::StreamController(QObject* parent) : QObject(parent) {}

StreamController::~StreamController() { detachAll(); }

namespace {

// A "direct source" plays as-is instead of going through the Media Relay:
// full URLs (rtsp/http/file/...), Windows drive paths ("E:/video.mp4" or
// "E:\video.mp4"), and absolute POSIX paths ("/media/video.mp4").
// Mock/offline camera entries use local file paths as their uuid; routing
// those through the relay produced an unopenable URL and a black cell.
bool isDirectSource(const QString& source) {
    if (source.contains(QLatin1String("://"))) return true;
    if (source.startsWith(QLatin1Char('/'))) return true;
    if (source.size() > 2 && source.at(1) == QLatin1Char(':') &&
        (source.at(2) == QLatin1Char('/') || source.at(2) == QLatin1Char('\\'))) {
        return true;
    }
    return false;
}

}  // namespace

QString StreamController::liveUrl(const QString& cameraUuid,
                                  const QString& profile) const {
    if (isDirectSource(cameraUuid)) {
        return cameraUuid;
    }
    return QStringLiteral("rtsp://%1:8554/live/%2/%3")
        .arg(relay_host_, cameraUuid, profile);
}

QString StreamController::archiveUrl(const QString& cameraUuid,
                                     quint64 startUtcUs) const {
    // Local files have no relay-side archive endpoint; play the file itself
    // (startPlayback seeks within it).
    if (isDirectSource(cameraUuid)) {
        return cameraUuid;
    }
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
    // Per-frame timestamps are recorded for drift detection, but the QML-facing
    // signal is throttled to ~4Hz. Un-throttled it fires (cells × fps) queued
    // signals per second, and every VideoCell's Connections handler runs for
    // every one of them — O(cells²) JS invocations that saturate the GUI
    // thread on a large wall. The timestamp overlay only shows seconds anyway.
    connect(decPtr, &VideoDecoder::framePresented, this,
            [this, cellIndex](quint64 utcUs) {
                bool forward = false;
                {
                    std::lock_guard<std::mutex> lock(session_mutex_);
                    auto it = sessions_.find(cellIndex);
                    if (it != sessions_.end()) {
                        CellSession& s = it->second;
                        s.lastPresentedUs = utcUs;
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        if (nowMs - s.lastEmitMs >= 250) {
                            s.lastEmitMs = nowMs;
                            forward = true;
                        }
                    }
                }
                if (forward) emit cellFramePresented(cellIndex, utcUs);
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

    // For direct sources (local files, explicit URLs) every profile maps to
    // the same URL — record the profile but skip the pointless keyframe-align
    // stream restart, which would blank the cell for no benefit.
    const QString currentUrl = liveUrl(s.cameraUuid, s.profile);
    const QString nextUrl = liveUrl(s.cameraUuid, profile);
    s.profile = profile;
    if (nextUrl == currentUrl) return;

    s.decoder->switchTo(nextUrl);
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
    // Two-phase shutdown: signal EVERY worker first (the AVIO interrupt
    // callback aborts any blocking open/read immediately), THEN join. A
    // serial stop-and-join pays each decoder's shutdown latency back-to-back
    // on the GUI thread — on a full wall that froze the UI for seconds.
    for (auto& dec : decodersToStop) {
        dec->requestStop();
    }
    for (auto& dec : decodersToStop) {
        dec->stop();
    }
}

void StreamController::resyncDrifted(quint64 masterUs, quint64 toleranceUs) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    for (auto& [idx, s] : sessions_) {
        if (!s.playbackMode || !s.decoder) continue;
        const quint64 last = s.lastPresentedUs;
        if (last == 0) continue;  // no frame yet — initial seek is in flight
        const quint64 drift = last > masterUs ? last - masterUs : masterUs - last;
        if (drift > toleranceUs) s.decoder->seekTo(masterUs);
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
