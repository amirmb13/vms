// =============================================================================
// PacketRingBuffer — single-producer / multi-consumer lock-free ring of
// ref-counted AVPackets. One instance per (camera, profile).
//
// Zero-copy contract: av_packet_ref() only bumps the underlying AVBufferRef
// refcount — payload bytes are NEVER duplicated. 50 subscribers reading the
// same camera all point at the same compressed buffer.
//
// Slow-consumer policy: consumers that fall more than `capacity` packets
// behind are jumped forward to the most recent I-frame so their decoder can
// re-sync cleanly. The producer NEVER blocks.
// =============================================================================
#include "relay/ring_buffer.h"

extern "C" {
#include <libavcodec/packet.h>
}

namespace vms {

PacketRingBuffer::PacketRingBuffer(size_t capacity) : slots_(capacity) {}

void PacketRingBuffer::push(const TimedPacket& p) {
    const uint64_t seq = head_.load(std::memory_order_relaxed);
    TimedPacket& slot = slots_[seq % slots_.size()];

    // Recycle the packet that lived in this slot one lap ago.
    if (slot.pkt != nullptr) {
        av_packet_free(&slot.pkt);
    }

    slot.pkt = av_packet_alloc();
    av_packet_ref(slot.pkt, p.pkt);          // refcount bump — no byte copy
    slot.utc_receive_us = p.utc_receive_us;  // NTP atomic receive timestamp
    slot.profile = p.profile;
    slot.camera_uuid = p.camera_uuid;

    // Track the newest keyframe for I-frame-aligned profile switching.
    if (p.pkt->flags & AV_PKT_FLAG_KEY) {
        last_iframe_.store(seq, std::memory_order_release);
    }

    // Publish AFTER the slot is fully written (release pairs with consumers'
    // acquire load in read()).
    head_.store(seq + 1, std::memory_order_release);
}

bool PacketRingBuffer::read(uint64_t& cursor, TimedPacket& out) const {
    const uint64_t head = head_.load(std::memory_order_acquire);
    if (cursor >= head) {
        return false;  // consumer is caught up — nothing new yet
    }

    // Slow-consumer drop policy: never block the producer. Jump the lagging
    // cursor to the latest I-frame (decoder re-sync point) inside the window.
    if (head - cursor > slots_.size()) {
        uint64_t iframe = last_iframe_.load(std::memory_order_acquire);
        cursor = (head - iframe <= slots_.size()) ? iframe
                                                  : head - slots_.size() / 2;
    }

    const TimedPacket& slot = slots_[cursor % slots_.size()];
    if (slot.pkt == nullptr) {
        ++cursor;  // startup hole — skip
        return false;
    }

    out.pkt = av_packet_alloc();
    av_packet_ref(out.pkt, slot.pkt);  // shared payload, private ref
    out.utc_receive_us = slot.utc_receive_us;
    out.profile = slot.profile;
    out.camera_uuid = slot.camera_uuid;

    // Overwrite race check: if the producer lapped us while copying, the ref
    // we took may belong to a newer packet — discard and let the caller retry
    // from the corrected cursor position.
    const uint64_t head_after = head_.load(std::memory_order_acquire);
    if (head_after - cursor > slots_.size()) {
        av_packet_free(&out.pkt);
        cursor = last_iframe_.load(std::memory_order_acquire);
        return false;
    }

    ++cursor;
    return true;
}

uint64_t PacketRingBuffer::last_iframe_seq() const {
    return last_iframe_.load(std::memory_order_acquire);
}

}  // namespace vms
