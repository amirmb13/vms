#include "playback/sync_playback.h"

#include "stream/stream_controller.h"

#include <QDateTime>

namespace vms {
namespace {
constexpr int kTickMs = 100;                 // master clock granularity
constexpr quint64 kFrameUs = 40'000;         // 25fps forensic frame step
}  // namespace

SyncPlayback::SyncPlayback(QObject* parent) : QObject(parent) {
    timer_.setInterval(kTickMs);
    connect(&timer_, &QTimer::timeout, this, &SyncPlayback::tick);
}

void SyncPlayback::attachController(StreamController* controller) {
    controller_ = controller;
}

void SyncPlayback::play() {
    if (playing_) return;
    playing_ = true;
    timer_.start();
    if (controller_) controller_->broadcastPaused(false);
    emit playingChanged();
}

void SyncPlayback::pause() {
    if (!playing_) return;
    playing_ = false;
    timer_.stop();
    if (controller_) controller_->broadcastPaused(true);
    emit playingChanged();
}

void SyncPlayback::seek(quint64 utcUs) {
    if (window_end_us_ > window_start_us_)
        utcUs = qBound(window_start_us_, utcUs, window_end_us_);
    position_us_ = utcUs;
    if (controller_) controller_->broadcastSeek(utcUs);  // NTP broadcast
    emit positionChanged();
}

void SyncPlayback::stepFrames(int frames) {
    pause();
    const qint64 delta = static_cast<qint64>(frames) *
                         static_cast<qint64>(kFrameUs);
    const qint64 next = static_cast<qint64>(position_us_) + delta;
    seek(next > 0 ? static_cast<quint64>(next) : 0);
}

void SyncPlayback::jumpToLive() {
    pause();
    emit liveRequested();
}

void SyncPlayback::setRate(double rate) {
    rate = qBound(0.25, rate, 16.0);
    if (qFuzzyCompare(rate_, rate)) return;
    rate_ = rate;
    if (controller_) controller_->broadcastRate(rate);
    emit rateChanged();
}

void SyncPlayback::setWindowStartUtcUs(quint64 us) {
    if (window_start_us_ == us) return;
    window_start_us_ = us;
    if (position_us_ < us) position_us_ = us;
    emit windowChanged();
}

void SyncPlayback::setWindowEndUtcUs(quint64 us) {
    if (window_end_us_ == us) return;
    window_end_us_ = us;
    emit windowChanged();
}

void SyncPlayback::tick() {
    // Advance master clock; decoders follow via their own pacing, and any
    // drift is corrected by the periodic broadcastSeek below.
    position_us_ += static_cast<quint64>(kTickMs * 1000.0 * rate_);
    if (window_end_us_ > 0 && position_us_ >= window_end_us_) {
        position_us_ = window_end_us_;
        pause();
    }
    emit positionChanged();

    // Hard re-sync every 5s keeps all engines within one GOP of the master.
    static int ticks = 0;
    if (++ticks % 50 == 0 && controller_)
        controller_->broadcastSeek(position_us_);
}

}  // namespace vms
