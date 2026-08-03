#pragma once
// Single-producer / multi-consumer ring buffer of ref-counted AVPackets.
// One instance per (camera, profile). Consumers hold cursors; slow consumers
// are dropped (never block the ingest thread).
//
// Concurrency model: head_/last_iframe_ are lock-free atomics; each slot is
// additionally protected by a tiny per-slot spinlock so a consumer taking a
// packet ref can never race the producer freeing that same packet (the old
// fully lock-free design had a use-after-free window between the consumer's
// av_packet_ref and the producer's av_packet_free one lap later). The
// producer only ever contends on a slot when a lapped consumer is copying
// that exact slot — statistically never in steady state.
#include <atomic>
#include <cstdint>
#include <vector>
#include "ingest/stream_ingestor.h"

namespace vms {
class PacketRingBuffer {
public:
    explicit PacketRingBuffer(size_t capacity = 512);
    ~PacketRingBuffer();                      // releases every retained ref

    PacketRingBuffer(const PacketRingBuffer&) = delete;
    PacketRingBuffer& operator=(const PacketRingBuffer&) = delete;

    void push(const TimedPacket& p);          // producer (ingest thread)
    bool read(uint64_t& cursor, TimedPacket& out) const;  // consumers
    uint64_t last_iframe_seq() const;         // for I-frame-aligned switching

private:
    struct Slot {
        TimedPacket packet;
        // Per-slot spinlock (see file header). Mutable: read() is logically
        // const but must hold the guard while taking its packet ref.
        mutable std::atomic<bool> busy{false};
    };

    std::vector<Slot> slots_;
    std::atomic<uint64_t> head_{0};
    std::atomic<uint64_t> last_iframe_{0};
};
}  // namespace vms
