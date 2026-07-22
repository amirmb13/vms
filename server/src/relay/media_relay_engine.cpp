// =============================================================================
// MediaRelayEngine — Pub/Sub RTSP Proxy / Reflector.
//
// The zero-copy fan-out core of the "No Direct Client-to-Camera" mandate:
// the camera sees exactly ONE consumer (the ingest thread) regardless of how
// many operators watch. 50 control-room clients on the same camera share one
// PacketRingBuffer; their subscriber cursors read the same ref-counted
// AVPackets. Video walls flip `multicast` so the packet leaves the NIC once
// (IGMP) instead of 50 unicast copies.
//
// Adaptive streaming: switch_profile_iframe_aligned() arms a pending profile
// and only flips the cursor at the target ring's newest keyframe — the
// decoder starts on an IDR frame, so 360p -> 4K transitions are seamless
// (no black frame, no flicker).
// =============================================================================
#include "relay/media_relay_engine.h"

#include <functional>

#include "ntp/ntp_clock.h"

namespace vms {

namespace {

inline std::string stream_key(const std::string& camera_uuid,
                              StreamProfile profile) {
    return camera_uuid + ":" + std::to_string(static_cast<int>(profile));
}

}  // namespace

size_t MediaRelayEngine::StreamKeyHash::operator()(
    const std::string& k) const {
    return std::hash<std::string>{}(k);
}

MediaRelayEngine::MediaRelayEngine(NtpClock& clock) : clock_(clock) {}

// ---------------------------------------------------------------------------
// Ingest side — called from every reader thread, must stay near-lock-free.
// ---------------------------------------------------------------------------
void MediaRelayEngine::on_packet(const TimedPacket& packet) {
    const std::string key = stream_key(packet.camera_uuid, packet.profile);

    PacketRingBuffer* ring = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rings_.find(key);
        if (it == rings_.end()) {
            it = rings_.emplace(key, std::make_unique<PacketRingBuffer>())
                     .first;
        }
        ring = it->second.get();
    }
    // push() itself is lock-free single-producer; the map lock above is only
    // hit on the hash lookup and is uncontended in steady state.
    ring->push(packet);
}

// ---------------------------------------------------------------------------
// Session negotiation (invoked by the vms.relay.MediaRelay gRPC service after
// ControlService has validated the client's Django-issued JWT).
// ---------------------------------------------------------------------------
RelaySession MediaRelayEngine::open_live(const std::string& camera_uuid,
                                         StreamProfile profile,
                                         bool prefer_multicast) {
    std::lock_guard<std::mutex> lock(mutex_);

    RelaySession session;
    session.session_id =
        camera_uuid + "#" + std::to_string(next_session_seq_++) + "@" +
        std::to_string(clock_.now_utc_us());
    session.camera_uuid = camera_uuid;
    session.active_profile = profile;
    session.pending_profile = profile;
    session.multicast = prefer_multicast;

    // Join at the newest I-frame so the client's decoder starts instantly on
    // an IDR instead of waiting (up to a GOP) for the next keyframe.
    const std::string key = stream_key(camera_uuid, profile);
    auto it = rings_.find(key);
    if (it == rings_.end()) {
        it = rings_.emplace(key, std::make_unique<PacketRingBuffer>()).first;
    }
    session.cursor = it->second->last_iframe_seq();

    sessions_[session.session_id] = session;
    return session;
}

void MediaRelayEngine::switch_profile_iframe_aligned(RelaySession& s,
                                                     StreamProfile target) {
    if (target == s.active_profile) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Arm the switch. The relay pump keeps serving the OLD profile until the
    // TARGET ring exposes a keyframe at/after this instant — then flips the
    // cursor to that exact sequence. H.264/H.265 decoders resume cleanly on
    // the IDR: no black screen, no flicker, no reference-frame corruption.
    s.pending_profile = target;

    const std::string key = stream_key(s.camera_uuid, target);
    auto it = rings_.find(key);
    if (it == rings_.end()) {
        // Mid-stream is opened on demand: first 4-camera-grid subscriber
        // triggers ingest of the 720p/1080p profile.
        it = rings_.emplace(key, std::make_unique<PacketRingBuffer>()).first;
    }

    const uint64_t iframe_seq = it->second->last_iframe_seq();
    if (iframe_seq > 0) {
        s.cursor = iframe_seq;              // land exactly on the keyframe
        s.active_profile = target;          // switch is complete
        s.pending_profile = target;
    }
    // else: target ring has no keyframe yet — pending_profile stays armed and
    // the pump re-checks on every packet until the first IDR arrives.

    auto stored = sessions_.find(s.session_id);
    if (stored != sessions_.end()) stored->second = s;
}

void MediaRelayEngine::close(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session_id);
    // Ring buffers intentionally outlive sessions: the next subscriber for
    // the same camera reuses the warm ring (and the archiver never stops).
}

}  // namespace vms
