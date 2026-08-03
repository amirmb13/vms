// =============================================================================
// PacketRingBuffer — single-producer / multi-consumer ring of ref-counted
// AVPackets. One instance per (camera, profile).
//
// Zero-copy contract: av_packet_ref() only bumps the underlying AVBufferRef
// refcount — payload bytes are NEVER duplicated. 50 subscribers reading the
// same camera all point at the same compressed buffer.
//
// Slow-consumer policy: consumers that fall more than `capacity` packets
// behind are jumped forward to the most recent I-frame so their decoder can
// re-sync cleanly. The producer NEVER blocks on consumers in steady state —
// the per-slot spinlock is only contended when a lapped consumer is copying
// the exact slot being overwritten (see ring_buffer.h).
// =============================================================================
#include "relay/ring_buffer.h"

extern "C" {
#include <libavcodec/packet.h>
}

namespace vms {

namespace {

// Minimal spinlock over a Slot::busy flag. Critical sections here are a few
// pointer swaps / refcount bumps, so spinning (never sleeping) is correct.
class SlotGuard {
public:
    explicit SlotGuard(std::atomic<bool>& flag) : flag_(flag) {
        while (flag_.exchange(true, std::memory_order_acquire)) {
            // busy-wait; contention window is nanoseconds
        }
    }
    ~SlotGuard() { flag_.store(false, std::memory_order_release); }

private:
    std::atomic<bool>& flag_;
};

}  // namespace

PacketRingBuffer::PacketRingBuffer(size_t capacity) : slots_(capacity) {}

PacketRingBuffer::~PacketRingBuffer() {
    // Release every retained ref. Without this, a reaped ring leaked its
    // whole window (`capacity` compressed packets — megabytes per stream).
    for (Slot& slot : slots_) {
        if (slot.packet.pkt != nullptr) {
            av_packet_free(&slot.packet.pkt);
        }
    }
}

void PacketRingBuffer::push(const TimedPacket& p) {
    const uint64_t seq = head_.load(std::memory_order_relaxed);
    Slot& slot = slots_[seq % slots_.size()];

    // Take the incoming ref BEFORE entering the slot critical section so the
    // spinlock hold time stays minimal.
    AVPacket* fresh = av_packet_alloc();
    av_packet_ref(fresh, p.pkt);  // refcount bump — no byte copy

    AVPacket* stale = nullptr;
    {
        SlotGuard guard(slot.busy);
        stale = slot.packet.pkt;  // packet that lived here one lap ago
        slot.packet.pkt = fresh;
        slot.packet.utc_receive_us = p.utc_receive_us;  // NTP receive stamp
        slot.packet.profile = p.profile;
        slot.packet.camera_uuid = p.camera_uuid;
    }
    // Free the recycled ref OUTSIDE the critical section.
    if (stale != nullptr) av_packet_free(&stale);

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
        const uint64_t iframe = last_iframe_.load(std::memory_order_acquire);
        cursor = (head - iframe <= slots_.size()) ? iframe
                                                  : head - slots_.size() / 2;
    }

    const Slot& slot = slots_[cursor % slots_.size()];
    {
        SlotGuard guard(slot.busy);
        if (slot.packet.pkt == nullptr) {
            ++cursor;  // startup hole — skip
            return false;
        }
        // Safe under the slot guard: the producer cannot free this packet
        // while we hold it, so the ref below can never touch freed memory.
        out.pkt = av_packet_alloc();
        av_packet_ref(out.pkt, slot.packet.pkt);  // shared payload, own ref
        out.utc_receive_us = slot.packet.utc_receive_us;
        out.profile = slot.packet.profile;
        out.camera_uuid = slot.packet.camera_uuid;
    }

    // Lap check: if the producer overwrote this slot while we were copying,
    // the ref we took belongs to a NEWER packet than `cursor` claims —
    // discard it and re-sync from the latest keyframe to keep packet order
    // coherent for the decoder.
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
