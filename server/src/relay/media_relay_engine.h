#pragma once
// Media Relay Engine (Pub/Sub RTSP Proxy / Reflector).
//
// Zero-copy fan-out: each camera stream has ONE shared ring buffer of
// ref-counted AVPackets. When 50 control-room clients watch the same camera,
// the ingestor writes once and 50 subscriber cursors read the same memory —
// packets are never duplicated. Video-wall deployments switch to IGMP
// multicast so the packet leaves the NIC exactly once.
//
// 10k-camera scale design:
//   * The ring registry is SHARDED (64 shards, each with its own
//     std::shared_mutex). The per-packet hot path takes only a SHARED lock
//     on one shard — 10k ingest threads no longer serialize on one global
//     mutex hundreds of thousands of times per second.
//   * Rings are SUBSCRIBER-AWARE. Packets are buffered only for streams
//     somebody is actually watching (plus a linger window after the last
//     viewer leaves, so channel-surfing operators re-join warm rings).
//     Unwatched streams cost zero relay memory — without this, 10k cameras
//     x 512 retained packets pinned tens of GB of RAM for nobody.
//     Archiving is unaffected: RawArchiver is an independent sink.
//
// Adaptive streaming: SwitchProfile waits for the next I-frame on the target
// profile (H.264/H.265 keyframe alignment) before flipping the subscriber's
// cursor, guaranteeing flicker-free 360p -> 4K transitions.

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "ingest/stream_ingestor.h"
#include "relay/ring_buffer.h"

namespace vms {

class NtpClock;

struct RelaySession {
    std::string session_id;
    std::string camera_uuid;
    StreamProfile active_profile;
    StreamProfile pending_profile;    // set during I-frame-aligned switch
    bool multicast = false;
    uint64_t cursor = 0;              // position in the shared ring buffer
};

class MediaRelayEngine final : public IPacketSink {
public:
    explicit MediaRelayEngine(NtpClock& clock);

    // Ingest side — single producer per (camera, profile) ring buffer.
    // Hot path: one shared-lock on one shard, no global serialization.
    void on_packet(const TimedPacket& packet) override;

    // Session negotiation (backs the vms.relay.MediaRelay gRPC service; JWT
    // validation against Django happens in ControlService before reaching here).
    RelaySession open_live(const std::string& camera_uuid, StreamProfile profile,
                           bool prefer_multicast);
    void switch_profile_iframe_aligned(RelaySession& s, StreamProfile target);
    void close(const std::string& session_id);

    // Frees rings that have had no subscriber for longer than the linger
    // window. Invoked opportunistically from close(); also safe to call from
    // a housekeeping timer.
    void reap_idle_rings();

private:
    struct RingEntry {
        std::unique_ptr<PacketRingBuffer> ring;
        uint32_t subscribers = 0;
        uint64_t linger_until_us = 0;   // meaningful while subscribers == 0
    };
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, RingEntry> rings;
    };

    static constexpr size_t kShardCount = 64;
    // Keep a just-abandoned ring warm for 30 s so the next viewer of the
    // same camera still joins instantly at a buffered I-frame.
    static constexpr uint64_t kRingLingerUs = 30ull * 1000 * 1000;

    Shard& shard_for(const std::string& key);
    // Find-or-create the ring for `key` and add one subscriber.
    PacketRingBuffer* acquire_ring(const std::string& key);
    // Drop one subscriber; starts the linger countdown at zero subscribers.
    void release_ring(const std::string& key);

    NtpClock& clock_;
    std::array<Shard, kShardCount> shards_;

    // Session registry is low-traffic (open/close/switch only) and lives
    // behind its own mutex so it never contends with the packet hot path.
    std::mutex session_mutex_;
    std::unordered_map<std::string, RelaySession> sessions_;
    uint64_t next_session_seq_ = 1;
};

}  // namespace vms
