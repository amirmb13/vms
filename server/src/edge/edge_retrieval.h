#pragma once
// Edge Storage Retrieval (ONVIF Profile G):
// When a camera reconnects after a network outage, the archiver's .vidx
// timeline exposes the disconnection gap. This module queries the camera's
// on-board SD card recordings via the ONVIF Recording Search / Replay
// services, downloads the missing clips over RTSP replay, and hands them to
// RawArchiver::stitch_edge_clip() so the footage reappears seamlessly at its
// original NTP position on every client timeline.
//
// Flow (per reconnected camera):
//   1. RawArchiver::find_gaps(camera, outage_window)      -> [gaps]
//   2. ONVIF GetRecordingSearchResults(camera, gap)       -> [track tokens]
//   3. RTSP replay download (Range: clock=...) to a temp clip file
//   4. RawArchiver::stitch_edge_clip(camera, clip, gap)   -> timeline merge
//   5. ControlService event EVENT_EDGE_GAP_STITCHED       -> Django audit log

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "archiver/raw_archiver.h"

namespace vms {

class ControlService;

struct EdgeRetrievalTask {
    std::string camera_uuid;
    std::string onvif_endpoint;       // http://<cam>/onvif/recording_search
    std::string rtsp_replay_url;      // camera Replay service RTSP endpoint
    uint64_t reconnect_utc_us = 0;    // when the camera came back online
    // Look-back horizon: never backfill beyond the camera's SD retention.
    uint64_t max_lookback_us = 86'400'000'000ULL;  // 24h default
};

class EdgeStorageRetriever final {
public:
    EdgeStorageRetriever(RawArchiver& archiver, ControlService& control,
                         std::string scratch_dir);
    ~EdgeStorageRetriever();

    // Called by ControlService when a camera transitions offline -> online
    // (ONVIF event subscription or ingest reconnect). Non-blocking: tasks are
    // drained by a low-priority worker so live ingest bandwidth is untouched.
    void on_camera_reconnected(EdgeRetrievalTask task);

private:
    void worker_loop();

    // ONVIF Recording Search: FindRecordings + GetRecordingSearchResults
    // scoped to the gap window. Returns replay track tokens with their
    // camera-local time ranges (already NTP-aligned — this server IS the
    // camera's NTP source, so SD timestamps match archive timestamps).
    struct EdgeTrack { std::string token; uint64_t start_us, end_us; };
    std::vector<EdgeTrack> search_recordings(const EdgeRetrievalTask& task,
                                             const ArchiveGap& gap);

    // RTSP replay download (RFC 7826 Range: clock=) with bandwidth capping
    // (Profile G mandates the camera throttles replay; we additionally pace
    // reads so a 4K backfill never competes with live streams on the VLAN).
    bool download_track(const EdgeRetrievalTask& task, const EdgeTrack& track,
                        const std::string& clip_path);

    RawArchiver& archiver_;
    ControlService& control_;
    std::string scratch_dir_;

    std::deque<EdgeRetrievalTask> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_;
    std::atomic<bool> running_{true};
};

}  // namespace vms
