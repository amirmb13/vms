#pragma once
// Media Relay Engine (Pub/Sub RTSP Proxy / Reflector).
//
// Zero-copy fan-out: each camera stream has ONE shared ring buffer of
// ref-counted AVPackets. When 50 control-room clients watch the same camera,
// the ingestor writes once and 50 subscriber cursors read the same memory —
// packets are never duplicated. Video-wall deployments switch to IGMP
// multicast so the packet leaves the NIC exactly once.
//
// Adaptive streaming: SwitchProfile waits for the next I-frame on the target
// profile (H.264/H.265 keyframe alignment) before flipping the subscriber's
// cursor, guaranteeing flicker-free 360p -> 4K transitions.

#include <cstdint>
#include <memory>
#include <mutex>
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
    void on_packet(const TimedPacket& packet) override;

    // Session negotiation (backs the vms.relay.MediaRelay gRPC service; JWT
    // validation against Django happens in ControlService before reaching here).
    RelaySession open_live(const std::string& camera_uuid, StreamProfile profile,
                           bool prefer_multicast);
    void switch_profile_iframe_aligned(RelaySession& s, StreamProfile target);
    void close(const std::string& session_id);

private:
    struct StreamKeyHash { size_t operator()(const std::string& k) const; };

    NtpClock& clock_;
    mutable std::mutex mutex_;   // guards rings_/sessions_ map topology only;
                                 // packet reads stay lock-free via cursors
    // key = camera_uuid + ":" + profile
    std::unordered_map<std::string, std::unique_ptr<PacketRingBuffer>> rings_;
    std::unordered_map<std::string, RelaySession> sessions_;
    uint64_t next_session_seq_ = 1;
};

}  // namespace vms
