#pragma once
// Zero-Copy Shared Memory frame handoff to the Python AI container.
//
// Protocol (see proto/ai_signaling.proto):
//   1. C++ allocates a ring of N shared-memory slots per camera
//      (POSIX shm_open on Linux, CreateFileMapping/MMF on Windows).
//   2. Decoded raw RGB/GRAY frames are written DIRECTLY into a free slot —
//      the decoder's output buffer IS the shared memory (no intermediate copy).
//   3. A lightweight gRPC FrameDescriptor (shm name, geometry, NTP timestamp,
//      slot index) is sent to AiInference.ProcessFrame.
//   4. Python maps the block via multiprocessing.shared_memory and views it
//      as a NumPy array — zero copies end to end.
//   5. The InferenceResult's slot_index releases the slot back to this ring.

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace vms {

class AiInferenceClient;

struct ShmSlot {
    std::string name;                 // e.g. "/vms_frame_<cam>_<slot>"
    uint8_t* mapped = nullptr;        // process-mapped base pointer
    size_t size_bytes = 0;
    std::atomic<bool> in_flight{false};  // owned by Python until released
#ifdef _WIN32
    void* mapping_handle = nullptr;   // CreateFileMapping handle
#else
    int fd = -1;                      // shm_open descriptor
#endif

    ShmSlot() = default;
    ShmSlot(ShmSlot&& o) noexcept
        : name(std::move(o.name)), mapped(o.mapped), size_bytes(o.size_bytes),
          in_flight(o.in_flight.load()) {
#ifdef _WIN32
        mapping_handle = o.mapping_handle; o.mapping_handle = nullptr;
#else
        fd = o.fd; o.fd = -1;
#endif
        o.mapped = nullptr;
    }
    ShmSlot(const ShmSlot&) = delete;
    ShmSlot& operator=(const ShmSlot&) = delete;
};

class ShmFrameWriter {
public:
    ShmFrameWriter(const std::string& camera_uuid, uint32_t width,
                   uint32_t height, uint32_t bytes_per_pixel,
                   uint32_t slot_count = 4);
    ~ShmFrameWriter();  // shm_unlink all slots

    // Wire the gRPC signaling channel used by announce(). The client also
    // calls release_slot() when InferenceResults return. Must be set (and
    // register_writer()'d on the client) before the first announce().
    void set_inference_client(AiInferenceClient* client);

    // Returns a writable slot pointer for the decoder to render into, or
    // nullptr if all slots are in flight (backpressure -> drop frame).
    ShmSlot* acquire_slot();

    // After the decoder fills the slot: emit the gRPC FrameDescriptor.
    void announce(ShmSlot& slot, uint64_t utc_epoch_ms, uint64_t sequence);

    // Called when InferenceResult returns the slot_index.
    void release_slot(uint32_t slot_index);

    // Geometry accessors — decoders scale directly into slot memory, so they
    // must render with THIS stride (64-byte aligned for SIMD stores).
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t stride() const { return stride_; }

private:
    std::string camera_uuid_;
    uint32_t width_, height_, stride_;
    uint32_t bytes_per_pixel_ = 1;
    std::vector<ShmSlot> slots_;
    AiInferenceClient* client_ = nullptr;
};

}  // namespace vms
