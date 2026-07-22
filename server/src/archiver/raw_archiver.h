#pragma once
// Direct Raw Archiving: writes UNDECODED media packets straight to SAN/NAS
// with asynchronous I/O (io_uring/epoll on Linux, IOCP on Windows) —
// near-zero CPU during storage. Segments are indexed by NTP receive
// timestamp for millisecond-accurate Sync Playback and ONVIF Profile G
// gap detection/stitching.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "ingest/stream_ingestor.h"

namespace vms {

struct ArchiveGap {                    // detected disconnection window
    std::string camera_uuid;
    uint64_t start_utc_us;
    uint64_t end_utc_us;
};

class RawArchiver final : public IPacketSink {
public:
    explicit RawArchiver(std::string storage_root);
    ~RawArchiver();

    // MAIN-stream packets only; enqueued to the async writer, no decode.
    void on_packet(const TimedPacket& packet) override;

    // Timeline query for MediaRelay.GetTimelineSegments.
    // (segment index kept in an embedded per-server index, mirrored to Django)
    std::vector<ArchiveGap> find_gaps(const std::string& camera_uuid,
                                      uint64_t from_utc_us, uint64_t to_utc_us);

    // ONVIF Profile G: after reconnection, downloaded SD-card clips are
    // stitched into the timeline at their original NTP positions.
    void stitch_edge_clip(const std::string& camera_uuid,
                          const std::string& clip_path,
                          uint64_t start_utc_us, uint64_t end_utc_us);

private:
    // Per-packet index entry (16 bytes) kept alongside each segment file so
    // Sync Playback can seek by NTP timestamp in O(log n).
    struct IndexEntry {
        uint64_t utc_us;
        uint32_t offset;      // byte offset inside the segment file
        uint32_t size_flags;  // size << 1 | keyframe bit
    };

    struct QueuedWrite {
        std::string camera_uuid;
        uint64_t utc_us;
        bool keyframe;
        std::vector<uint8_t> payload;   // moved ref of the raw AVPacket bytes
    };

    void writer_loop();                 // async I/O thread (epoll/IOCP class)
    std::string segment_dir(const std::string& camera_uuid,
                            uint64_t utc_us) const;

    std::string storage_root_;
    std::deque<QueuedWrite> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread writer_thread_;
    std::atomic<bool> running_{true};

    // camera_uuid -> in-memory tail of the on-disk index (flushed per segment)
    std::unordered_map<std::string, std::vector<IndexEntry>> index_;
    std::mutex index_mutex_;
};

}  // namespace vms
