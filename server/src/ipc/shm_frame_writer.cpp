// =============================================================================
// ShmFrameWriter — Zero-Copy shared-memory frame ring (C++ -> Python AI).
//
// Contract (see shm_frame_writer.h + proto/ai_signaling.proto):
//   - One writer per camera. Each writer owns `slot_count` OS shared-memory
//     blocks named "/vms_frame_<camera_uuid>_<slot>".
//   - acquire_slot() hands the DECODER a raw mapped pointer, so the decoder's
//     output buffer IS the shared memory: zero intermediate copies.
//   - announce() emits only a lightweight gRPC FrameDescriptor; pixels never
//     traverse the wire. Python maps the block by name via
//     multiprocessing.shared_memory and views it as a NumPy array.
//   - release_slot() is invoked by AiInferenceClient's reader thread when the
//     InferenceResult returns the slot_index, handing ownership back here.
//
// Platform notes:
//   - Linux/macOS: shm_open + ftruncate + mmap (unlinked on destruction).
//   - Windows: CreateFileMappingA backed by the paging file + MapViewOfFile.
//     Python's multiprocessing.shared_memory uses the same Win32 namespace,
//     so the block name (without the leading '/') is directly mappable.
// =============================================================================
#include "ipc/shm_frame_writer.h"

#include <cstdio>

#include "ipc/ai_inference_client.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vms {

namespace {

// FFmpeg decoders want linesizes aligned for SIMD stores; 64 bytes covers
// AVX-512 stores and typical cache-line alignment on both x86 and ARM.
constexpr uint32_t kStrideAlign = 64;

inline uint32_t aligned_stride(uint32_t width, uint32_t bytes_per_pixel) {
    const uint32_t raw = width * bytes_per_pixel;
    return (raw + kStrideAlign - 1) & ~(kStrideAlign - 1);
}

}  // namespace

ShmFrameWriter::ShmFrameWriter(const std::string& camera_uuid, uint32_t width,
                               uint32_t height, uint32_t bytes_per_pixel,
                               uint32_t slot_count)
    : camera_uuid_(camera_uuid),
      width_(width),
      height_(height),
      stride_(aligned_stride(width, bytes_per_pixel)),
      bytes_per_pixel_(bytes_per_pixel) {
    const size_t slot_bytes = static_cast<size_t>(stride_) * height_;

    slots_.reserve(slot_count);
    for (uint32_t i = 0; i < slot_count; ++i) {
        ShmSlot slot;
        char name[128];
        std::snprintf(name, sizeof(name), "/vms_frame_%s_%u",
                      camera_uuid_.c_str(), i);
        slot.name = name;
        slot.size_bytes = slot_bytes;

#ifdef _WIN32
        // Windows MMF: name without the POSIX leading slash; Local\ session
        // namespace is shared with the Docker AI container via --ipc=host
        // equivalent (process isolation disabled / host-mode container).
        slot.mapping_handle = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(slot_bytes), slot.name.c_str() + 1);
        if (slot.mapping_handle != nullptr) {
            slot.mapped = static_cast<uint8_t*>(::MapViewOfFile(
                slot.mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, slot_bytes));
        }
#else
        // POSIX shm: visible to the AI container through --ipc=host, which
        // shares the host's /dev/shm namespace with the container.
        slot.fd = ::shm_open(slot.name.c_str(), O_CREAT | O_RDWR, 0660);
        if (slot.fd >= 0 &&
            ::ftruncate(slot.fd, static_cast<off_t>(slot_bytes)) == 0) {
            void* p = ::mmap(nullptr, slot_bytes, PROT_READ | PROT_WRITE,
                             MAP_SHARED, slot.fd, 0);
            if (p != MAP_FAILED) slot.mapped = static_cast<uint8_t*>(p);
        }
#endif
        slots_.push_back(std::move(slot));
    }
}

ShmFrameWriter::~ShmFrameWriter() {
    // Deregister from the gRPC client FIRST so its reader thread can never
    // call release_slot() on a writer that is tearing down its mappings.
    if (client_ != nullptr) client_->unregister_writer(camera_uuid_);

    for (ShmSlot& slot : slots_) {
#ifdef _WIN32
        if (slot.mapped != nullptr) ::UnmapViewOfFile(slot.mapped);
        if (slot.mapping_handle != nullptr) ::CloseHandle(slot.mapping_handle);
        slot.mapping_handle = nullptr;
#else
        if (slot.mapped != nullptr) ::munmap(slot.mapped, slot.size_bytes);
        if (slot.fd >= 0) ::close(slot.fd);
        ::shm_unlink(slot.name.c_str());  // reclaim /dev/shm on server exit
        slot.fd = -1;
#endif
        slot.mapped = nullptr;
    }
}

void ShmFrameWriter::set_inference_client(AiInferenceClient* client) {
    client_ = client;
    if (client_ != nullptr) client_->register_writer(camera_uuid_, this);
}

ShmSlot* ShmFrameWriter::acquire_slot() {
    // Lock-free claim: first slot whose in_flight flips false->true is ours.
    // If every slot is owned by Python, the AI engine is saturated — apply
    // backpressure by dropping this frame (live video never blocks on AI).
    for (ShmSlot& slot : slots_) {
        if (slot.mapped == nullptr) continue;  // mapping failed at startup
        bool expected = false;
        if (slot.in_flight.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return &slot;
        }
    }
    return nullptr;
}

void ShmFrameWriter::announce(ShmSlot& slot, uint64_t utc_epoch_ms,
                              uint64_t sequence) {
    if (client_ == nullptr || slot.mapped == nullptr) {
        slot.in_flight.store(false, std::memory_order_release);
        return;
    }

    // slot_index = position inside our ring (matches FrameDescriptor).
    const uint32_t slot_index =
        static_cast<uint32_t>(&slot - slots_.data());

    // Pixel format is implied by the decoder's output plane depth:
    // 1 byte/px => GRAY8 (motion/forensic crops), 3 => BGR24 (full-color AI).
    const vms::ai::PixelFormat format =
        bytes_per_pixel_ == 1 ? vms::ai::PIXEL_FORMAT_GRAY8
                              : vms::ai::PIXEL_FORMAT_BGR24;

    const bool ok = client_->announce_frame(
        camera_uuid_, slot.name, slot.size_bytes, width_, height_, stride_,
        format, utc_epoch_ms, sequence, slot_index);

    // Stream down -> ownership never left this process; free the slot so the
    // decoder keeps a full ring while the supervisor reconnects gRPC.
    if (!ok) slot.in_flight.store(false, std::memory_order_release);
}

void ShmFrameWriter::release_slot(uint32_t slot_index) {
    if (slot_index >= slots_.size()) return;  // defensive: corrupt result
    slots_[slot_index].in_flight.store(false, std::memory_order_release);
}

}  // namespace vms
