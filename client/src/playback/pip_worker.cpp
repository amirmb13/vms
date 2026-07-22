#include "playback/pip_worker.h"

#include "stream/video_decoder.h"

#include <QVideoSink>

namespace vms {

PipWorker::PipWorker(QObject* parent) : QObject(parent) {}

PipWorker::~PipWorker() { close(); }

void PipWorker::setRelayHost(const QString& host) { relay_host_ = host; }

void PipWorker::open(const QString& cameraUuid, quint64 startUtcUs,
                     QVideoSink* sink) {
    close();

    decoder_ = std::make_unique<VideoDecoder>();
    connect(decoder_.get(), &VideoDecoder::framePresented, this,
            [this](quint64 utcUs) {
                position_us_ = utcUs;
                emit positionChanged();
            });

    // Independent worker thread; the parent cell's live decoder is untouched.
    const QString url = QStringLiteral("rtsp://%1:8554/archive/%2?start=%3")
                            .arg(relay_host_, cameraUuid,
                                 QString::number(startUtcUs));
    decoder_->startPlayback(url, sink, startUtcUs);

    active_ = true;
    emit activeChanged();
}

void PipWorker::seek(quint64 utcUs) {
    if (decoder_) decoder_->seekTo(utcUs);
}

void PipWorker::setPaused(bool paused) {
    if (decoder_) decoder_->setPaused(paused);
}

void PipWorker::setRate(double rate) {
    if (decoder_) decoder_->setRate(rate);
}

void PipWorker::close() {
    if (decoder_) {
        decoder_->stop();
        decoder_.reset();
    }
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
}

}  // namespace vms
