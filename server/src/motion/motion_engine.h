#pragma once
// 3-Layer Hybrid Motion Detection Engine (per camera):
//   Layer 1: ONVIF Profile M/T edge motion metadata (zero server cost).
//   Layer 2: GPU MOG2/KNN background subtraction in VRAM (when GPU present).
//   Layer 3: CPU-only Spatio-Temporal Downsampling + SIMD 3-frame difference.
// Selection is automatic per camera per host (hardware-agnostic mandate).
//
// The engine also hosts the AI frame publication path: because it owns the
// only sub-stream decoders in the process, AI-enabled cameras get their
// decoded frames scaled DIRECTLY into ShmFrameWriter slots (zero-copy) and
// announced to the Python AI container — gated on active motion so idle
// scenes cost the AI engine nothing.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

#include "ingest/stream_ingestor.h"
#include "ipc/shm_frame_writer.h"

#ifdef VMS_HAVE_CUDA
#include "motion/gpu_bg_subtraction.h"
#endif

namespace vms {

class ControlService;
class AiInferenceClient;

struct RoiGrid {
    uint16_t cols = 32;
    uint16_t rows = 24;
    std::vector<uint8_t> bitmask;     // packed row-major, from Django
    uint8_t sensitivity = 50;         // 0..100 -> mapped to pixel threshold
};

enum class MotionLayer : uint8_t { OnvifEdge = 1, GpuBgSub = 2, CpuSimd = 3 };

class MotionEngine final : public IPacketSink {
public:
    MotionEngine(const HardwareCaps& caps, ControlService& control);
    ~MotionEngine() override;

    // Receives SUB-STREAM packets only; applies temporal downsampling
    // (3–5 fps) then decodes + libswscale spatial downsampling to 320x240 GRAY8.
    void on_packet(const TimedPacket& packet) override;

    // Pushed from Django via ControlService (MotionRoiRequest).
    void update_roi(const std::string& camera_uuid, RoiGrid grid);

    // Chooses Layer 1/2/3 per camera based on caps + camera ONVIF support.
    MotionLayer select_layer(const std::string& camera_uuid) const;

    // ---- AI frame publication (zero-copy shm handoff) ----------------------
    // Wire the gRPC signaling client once at startup (before ingest starts).
    void set_ai_client(AiInferenceClient* client);

    // Enable/disable AI analysis for a camera. Enabling arms lazy creation of
    // the per-camera ShmFrameWriter ring on the first decoded frame (geometry
    // is only known then); disabling tears the ring down and unregisters it.
    void set_ai_enabled(const std::string& camera_uuid, bool enabled);

private:
    struct CameraMotionState {
        // Rolling window of 3 downsampled grayscale frames (320*240 each).
        std::array<std::vector<uint8_t>, 3> frames;
        int filled = 0;
        uint64_t last_sample_us = 0;   // temporal downsampling gate
        RoiGrid roi;
        std::vector<uint8_t> roi_pixel_mask;  // grid expanded to 320x240
        bool motion_active = false;
        bool onvif_edge_capable = false;      // Layer 1 available?

        // Lazy sub-stream decoder (Layer 2/3 only) + AVX2 libswscale context.
        AVCodecContext* decoder = nullptr;
        SwsContext* scaler = nullptr;

        // ---- Layer 2 (GPU) state -------------------------------------------
#ifdef VMS_HAVE_CUDA
        AVBufferRef* hw_device = nullptr;      // CUDA hwdevice for NVDEC
        VmsGpuBgSubtractor* gpu_ctx = nullptr; // VRAM MOG2 model (lazy)
        bool gpu_roi_dirty = true;             // re-upload ROI on next frame
#endif
        bool gpu_failed = false;               // NVDEC init failed -> Layer 3

        // ---- AI zero-copy publication state --------------------------------
        bool ai_enabled = false;
        uint64_t ai_sequence = 0;
        std::unique_ptr<ShmFrameWriter> ai_writer;  // lazy: needs geometry
        SwsContext* ai_scaler = nullptr;            // decoder fmt -> BGR24
    };

    void run_layer3_simd(CameraMotionState& st, const std::string& camera_uuid,
                         uint64_t utc_us);
#ifdef VMS_HAVE_CUDA
    void run_layer2_gpu(CameraMotionState& st, const TimedPacket& packet);
#endif
    // Scales `sw_frame` straight into a shm slot and announces it — called
    // from decoder paths ONLY while st.motion_active (motion-gated).
    void maybe_announce_ai_frame(CameraMotionState& st,
                                 const std::string& camera_uuid,
                                 const AVFrame* sw_frame, uint64_t utc_us);
    void emit_event(const std::string& camera_uuid, bool started, uint64_t utc_us);

    static void expand_roi_mask(const RoiGrid& grid, int width, int height,
                                std::vector<uint8_t>& out);

    HardwareCaps caps_;
    ControlService& control_;
    AiInferenceClient* ai_client_ = nullptr;
    std::unordered_map<std::string, CameraMotionState> states_;
};

// simd_frame_diff.cpp
uint32_t simd_three_frame_diff(const uint8_t* f0, const uint8_t* f1,
                               const uint8_t* f2, const uint8_t* roi_mask,
                               size_t pixel_count, uint8_t threshold);
bool evaluate_motion(uint32_t changed_pixels, size_t pixel_count,
                     float trigger_ratio);

}  // namespace vms
