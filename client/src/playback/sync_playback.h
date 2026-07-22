#pragma once
// =============================================================================
// SyncPlayback — master NTP timeline broadcaster (mandate §3.B).
//
// One UI timeline slider drives EVERY active decoding engine: the master
// position (UTC microseconds, NTP-disciplined) is broadcast to all archive
// sessions via the StreamController so disparate cameras render the exact
// same instant simultaneously (millisecond-accurate Sync Playback).
//
// A 100ms QTimer advances the master clock at `rate` while playing; decoders
// self-correct drift against framePresented() feedback.
// =============================================================================
#include <QObject>
#include <QTimer>

namespace vms {

class StreamController;

class SyncPlayback : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(quint64 positionUtcUs READ positionUtcUs
                   NOTIFY positionChanged)
    Q_PROPERTY(quint64 windowStartUtcUs READ windowStartUtcUs
                   WRITE setWindowStartUtcUs NOTIFY windowChanged)
    Q_PROPERTY(quint64 windowEndUtcUs READ windowEndUtcUs
                   WRITE setWindowEndUtcUs NOTIFY windowChanged)

public:
    explicit SyncPlayback(QObject* parent = nullptr);

    // Wire once at startup (main.cpp) — fan-out target for broadcasts.
    void attachController(StreamController* controller);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(quint64 utcUs);     // slider drag / timeline click
    Q_INVOKABLE void stepFrames(int frames);  // frame-by-frame forensics (+/-)
    Q_INVOKABLE void jumpToLive();

    bool playing() const { return playing_; }
    double rate() const { return rate_; }
    void setRate(double rate);
    quint64 positionUtcUs() const { return position_us_; }

    quint64 windowStartUtcUs() const { return window_start_us_; }
    quint64 windowEndUtcUs() const { return window_end_us_; }
    void setWindowStartUtcUs(quint64 us);
    void setWindowEndUtcUs(quint64 us);

signals:
    void playingChanged();
    void rateChanged();
    void positionChanged();
    void windowChanged();
    void liveRequested();   // TimelineBar switches the grid back to live mode

private:
    void tick();

    StreamController* controller_ = nullptr;
    QTimer timer_;
    bool playing_ = false;
    double rate_ = 1.0;
    quint64 position_us_ = 0;
    quint64 window_start_us_ = 0;   // reviewed window (e.g. selected Shamsi day)
    quint64 window_end_us_ = 0;
};

}  // namespace vms
