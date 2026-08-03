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
// Scale notes (10k cameras):
//   * on_packet() is called from EVERY ingest reader thread for EVERY packet
//     (~hundreds of thousands of calls/sec fleet-wide). It therefore takes
//     only a shared lock on 1-of-64 shards and buffers only WATCHED streams.
//   * Ring memory is bounded by (concurrently watched streams x capacity),
//     not by the total camera count.
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

MediaRelayEngine::MediaRelayEngine(NtpClock& clock) : clock_(clock) {}

MediaRelayEngine::Shard& MediaRelayEngine::shard_for(const std::string& key) {
    return shards_[std::hash<std::string>{}(key) % kShardCount];
}

// ---------------------------------------------------------------------------
// Ingest side — called from every reader thread, must stay near-lock-free.
// ---------------------------------------------------------------------------
void MediaRelayEngine::on_packet(const TimedPacket& packet) {
    const std::string key = stream_key(packet.camera_uuid, packet.profile);
    Shard& shard = shard_for(key);

    // SHARED lock only: ingest threads for different streams in the same
    // shard proceed in parallel; exclusive locks are taken solely by the
    // rare open/close/reap paths. The lock is held across push() so a
    // concurrent reap can never free the ring out from under us.
    std::shared_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.rings.find(key);
    if (it == shard.rings.end()) {
        // Nobody has ever watched this stream — drop. The archiver and the
        // motion engine are independent sinks and still get every packet.
        return;
    }
    const RingEntry& entry = it->second;
    if (entry.subscribers == 0 &&
        clock_.now_utc_us() > entry.linger_until_us) {
        return;  // linger expired — stop buffering until the reap frees it
    }
    entry.ring->push(packet);
}

// ---------------------------------------------------------------------------
// Ring registry bookkeeping (exclusive-lock paths, low traffic).
// ---------------------------------------------------------------------------
PacketRingBuffer* MediaRelayEngine::acquire_ring(const std::string& key) {
    Shard& shard = shard_for(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.rings.find(key);
    if (it == shard.rings.end()) {
        RingEntry entry;
        entry.ring = std::make_unique<PacketRingBuffer>();
        it = shard.rings.emplace(key, std::move(entry)).first;
    }
    ++it->second.subscribers;
    return it->second.ring.get();
}

void MediaRelayEngine::release_ring(const std::string& key) {
    Shard& shard = shard_for(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.rings.find(key);
    if (it == shard.rings.end()) return;
    RingEntry& entry = it->second;
    if (entry.subscribers > 0) --entry.subscribers;
    if (entry.subscribers == 0) {
        // Keep the ring warm for the linger window, then reap_idle_rings()
        // frees the retained packet refs.
        entry.linger_until_us = clock_.now_utc_us() + kRingLingerUs;
    }
}

void MediaRelayEngine::reap_idle_rings() {
    const uint64_t now_us = clock_.now_utc_us();
    for (Shard& shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        for (auto it = shard.rings.begin(); it != shard.rings.end();) {
            const RingEntry& entry = it->second;
            if (entry.subscribers == 0 && now_us > entry.linger_until_us) {
                it = shard.rings.erase(it);  // ~PacketRingBuffer frees refs
            } else {
                ++it;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Session negotiation (invoked by the vms.relay.MediaRelay gRPC service after
// ControlService has validated the client's Django-issued JWT).
// ---------------------------------------------------------------------------
RelaySession MediaRelayEngine::open_live(const std::string& camera_uuid,
                                         StreamProfile profile,
                                         bool prefer_multicast) {
    // Register interest FIRST so on_packet starts buffering this stream.
    // Join at the newest I-frame so the client's decoder starts instantly on
    // an IDR (cold rings deliver within one GOP of the first push).
    PacketRingBuffer* ring = acquire_ring(stream_key(camera_uuid, profile));

    RelaySession session;
    session.camera_uuid = camera_uuid;
    session.active_profile = profile;
    session.pending_profile = profile;
    session.multicast = prefer_multicast;
    session.cursor = ring->last_iframe_seq();

    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session.session_id =
            camera_uuid + "#" + std::to_string(next_session_seq_++) + "@" +
            std::to_string(clock_.now_utc_us());
        sessions_[session.session_id] = session;
    }
    return session;
}

void MediaRelayEngine::switch_profile_iframe_aligned(RelaySession& s,
                                                     StreamProfile target) {
    if (target == s.active_profile && target == s.pending_profile) return;

    // Arm the switch: subscribing to the target ring makes on_packet start
    // buffering it (mid-profile ingest is opened on demand — the first
    // 4-camera-grid subscriber triggers the 720p/1080p profile).
    const std::string target_key = stream_key(s.camera_uuid, target);
    PacketRingBuffer* target_ring = nullptr;
    if (target != s.pending_profile) {
        target_ring = acquire_ring(target_key);
        // Drop the previously armed-but-incomplete pending profile, if any.
        if (s.pending_profile != s.active_profile) {
            release_ring(stream_key(s.camera_uuid, s.pending_profile));
        }
        s.pending_profile = target;
    } else {
        Shard& shard = shard_for(target_key);
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        auto it = shard.rings.find(target_key);
        if (it != shard.rings.end()) target_ring = it->second.ring.get();
    }

    // The relay pump keeps serving the OLD profile until the TARGET ring
    // exposes a keyframe — then flips the cursor to that exact sequence.
    // H.264/H.265 decoders resume cleanly on the IDR: no black screen, no
    // flicker, no reference-frame corruption.
    if (target_ring != nullptr) {
        const uint64_t iframe_seq = target_ring->last_iframe_seq();
        if (iframe_seq > 0) {
            const StreamProfile old_profile = s.active_profile;
            s.cursor = iframe_seq;      // land exactly on the keyframe
            s.active_profile = target;  // switch is complete
            s.pending_profile = target;
            if (old_profile != target) {
                release_ring(stream_key(s.camera_uuid, old_profile));
            }
        }
        // else: target ring has no keyframe yet — pending_profile stays armed
        // and the pump re-checks on every packet until the first IDR arrives.
    }

    std::lock_guard<std::mutex> lock(session_mutex_);
    auto stored = sessions_.find(s.session_id);
    if (stored != sessions_.end()) stored->second = s;
}

void MediaRelayEngine::close(const std::string& session_id) {
    RelaySession session;
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return;
        session = it->second;
        sessions_.erase(it);
    }

    // Release every ring this session subscribed to (active + a possibly
    // still-armed pending profile). Rings outlive sessions by the linger
    // window so the next subscriber for the same camera reuses a warm ring.
    release_ring(stream_key(session.camera_uuid, session.active_profile));
    if (session.pending_profile != session.active_profile) {
        release_ring(stream_key(session.camera_uuid, session.pending_profile));
    }

    // Opportunistic housekeeping — bounded by shard count, not camera count.
    reap_idle_rings();
}

}  // namespace vms
