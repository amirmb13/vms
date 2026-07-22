#pragma once
// Single-producer / multi-consumer lock-free ring buffer of ref-counted
// AVPackets. One instance per (camera, profile). Consumers hold cursors;
// slow consumers are dropped (never block the ingest thread).
#include <atomic>
#include <cstdint>
#include <vector>
#include "ingest/stream_ingestor.h"

namespace vms {
class PacketRingBuffer {
public:
    explicit PacketRingBuffer(size_t capacity = 512);
    void push(const TimedPacket& p);          // producer (ingest thread)
    bool read(uint64_t& cursor, TimedPacket& out) const;  // consumers
    uint64_t last_iframe_seq() const;         // for I-frame-aligned switching
private:
    std::vector<TimedPacket> slots_;
    std::atomic<uint64_t> head_{0};
    std::atomic<uint64_t> last_iframe_{0};
};
}  // namespace vms
