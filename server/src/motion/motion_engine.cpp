// =============================================================================
// MotionEngine — 3-Layer Hybrid Motion Detection.
//
//   Layer 1 (OnvifEdge): camera pushes Profile M/T motion metadata — the
//     server does zero pixel work; on_packet() is a pass-through.
//   Layer 2 (GpuBgSub): GPU host — NVDEC decodes the sub-stream into VRAM and
//     MOG2/KNN background subtraction runs on CUDA cores (gpu_bg_subtraction.cu,
//     VMS_HAVE_CUDA builds). The luma plane never leaves VRAM; only a 4-byte
//     foreground counter crosses PCIe per analyzed frame.
//   Layer 3 (CpuSimd): CPU-only Spatio-Temporal Downsampling —
//     temporal gate to 4 fps, decode, libswscale (AVX2) to 320x240 GRAY8,
//     then the AVX-512/AVX2 3-frame difference in simd_frame_diff.cpp.
//     ROI bitmask grids from Django are pre-expanded to a per-pixel mask so
//     the SIMD kernel ANDs them in for free.
//
//   AI publication: for AI-enabled cameras, decoded frames are scaled by
//   libswscale DIRECTLY into ShmFrameWriter slots (the scaler's destination
//   buffer IS the shared memory) and announced via AiInferenceClient —
//   gated on active motion so idle scenes cost the AI engine nothing.
// =============================================================================
#include "motion/motion_engine.h"

#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "control/control_service.h"
#include "ipc/ai_inference_client.h"

namespace vms {

namespace {

constexpr int kMotionW = 320;
constexpr int kMotionH = 240;
constexpr size_t kMotionPixels = static_cast<size_t>(kMotionW) * kMotionH;
constexpr uint64_t kSampleIntervalUs = 250'000;  // temporal gate: 4 fps
constexpr float kTriggerRatio = 0.015f;          // 1.5% of ROI pixels
constexpr float kReleaseRatio = 0.005f;          // hysteresis release level

inline uint8_t sensitivity_to_threshold(uint8_t sensitivity /*0..100*/) {
    // Higher sensitivity => lower pixel-delta threshold. Clamped to [8, 58].
    return static_cast<uint8_t>(58 - (sensitivity / 2));
}

}  // namespace

MotionEngine::MotionEngine(const HardwareCaps& caps, ControlService& control)
    : caps_(caps), control_(control) {}

MotionEngine::~MotionEngine() {
    for (auto& [uuid, st] : states_) {
        if (st.decoder != nullptr) avcodec_free_context(&st.decoder);
        if (st.scaler != nullptr) sws_freeContext(st.scaler);
        if (st.ai_scaler != nullptr) sws_freeContext(st.ai_scaler);
        st.ai_writer.reset();  // unregisters from ai_client_, unlinks shm
#ifdef VMS_HAVE_CUDA
        if (st.gpu_ctx != nullptr) vms_gpu_bgsub_destroy(st.gpu_ctx);
        if (st.hw_device != nullptr) av_buffer_unref(&st.hw_device);
#endif
    }
}

MotionLayer MotionEngine::select_layer(const std::string& camera_uuid) const {
    // Priority mandated by the architecture: edge metadata beats any server
    // computation; GPU beats CPU; SIMD CPU path is the universal fallback.
    const auto it = states_.find(camera_uuid);
    if (it != states_.end() && it->second.onvif_edge_capable)
        return MotionLayer::OnvifEdge;
    if (caps_.gpu_available &&
        (it == states_.end() || !it->second.gpu_failed))
        return MotionLayer::GpuBgSub;
    return MotionLayer::CpuSimd;
}

void MotionEngine::set_ai_client(AiInferenceClient* client) {
    ai_client_ = client;
}

void MotionEngine::set_ai_enabled(const std::string& camera_uuid, bool enabled) {
    CameraMotionState& st = states_[camera_uuid];
    st.ai_enabled = enabled;
    if (!enabled) {
        // Tear the ring down NOW: destructor unregisters from the gRPC client
        // and shm_unlink()s the slots, reclaiming /dev/shm immediately.
        st.ai_writer.reset();
        if (st.ai_scaler != nullptr) {
            sws_freeContext(st.ai_scaler);
            st.ai_scaler = nullptr;
        }
    }
    // enabled: the writer is created lazily on the first decoded frame — the
    // sub-stream geometry is only authoritative once a frame materializes.
}

void MotionEngine::expand_roi_mask(const RoiGrid& grid, int width, int height,
                                   std::vector<uint8_t>& out) {
    // Pre-expand the packed grid bitmask (e.g. 32x24 bits) into a full
    // per-pixel mask ONCE, so hot loops (SIMD / CUDA) just stream bytes.
    const size_t pixels = static_cast<size_t>(width) * height;
    out.assign(pixels, 0);
    if (grid.cols == 0 || grid.rows == 0) return;
    const int cell_w = width / grid.cols;
    const int cell_h = height / grid.rows;
    for (int gy = 0; gy < grid.rows; ++gy) {
        for (int gx = 0; gx < grid.cols; ++gx) {
            const size_t bit = static_cast<size_t>(gy) * grid.cols + gx;
            if (bit / 8 >= grid.bitmask.size()) continue;
            const bool enabled = (grid.bitmask[bit / 8] >> (bit % 8)) & 1;
            if (!enabled) continue;
            for (int py = gy * cell_h; py < (gy + 1) * cell_h; ++py)
                std::memset(&out[static_cast<size_t>(py) * width +
                                 static_cast<size_t>(gx) * cell_w],
                            0xFF, static_cast<size_t>(cell_w));
        }
    }
}

void MotionEngine::update_roi(const std::string& camera_uuid, RoiGrid grid) {
    CameraMotionState& st = states_[camera_uuid];
    st.roi = std::move(grid);
    expand_roi_mask(st.roi, kMotionW, kMotionH, st.roi_pixel_mask);
#ifdef VMS_HAVE_CUDA
    // The GPU mask is expanded at the NVDEC surface resolution on the next
    // processed frame (geometry may differ from the 320x240 CPU raster).
    st.gpu_roi_dirty = true;
#endif
}

void MotionEngine::on_packet(const TimedPacket& packet) {
    // Motion analysis consumes the SUB stream exclusively (360p) — decoding
    // 4K for motion would violate the CPU budget by an order of magnitude.
    if (packet.profile != StreamProfile::Sub || packet.pkt == nullptr) return;

    CameraMotionState& st = states_[packet.camera_uuid];

    switch (select_layer(packet.camera_uuid)) {
        case MotionLayer::OnvifEdge:
            return;  // events arrive via the ONVIF event subscription instead
        case MotionLayer::GpuBgSub:
#ifdef VMS_HAVE_CUDA
            run_layer2_gpu(st, packet);
            if (!st.gpu_failed) return;
            break;   // NVDEC init failed mid-flight -> Layer 3 fallback
#else
            break;   // GPU flagged but not compiled in -> Layer 3 fallback
#endif
        case MotionLayer::CpuSimd:
            break;
    }

    // ---- Temporal downsampling: analyze at most 1 frame per 250 ms --------
    if (packet.utc_receive_us - st.last_sample_us < kSampleIntervalUs) return;

    // ---- Lazy per-camera decoder + AVX2 scaler init ------------------------
    if (st.decoder == nullptr) {
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        st.decoder = avcodec_alloc_context3(codec);
        st.decoder->thread_count = 1;  // 1 sub-stream decode is microseconds
        if (avcodec_open2(st.decoder, codec, nullptr) < 0) return;
    }

    if (avcodec_send_packet(st.decoder, packet.pkt) < 0) return;

    AVFrame* frame = av_frame_alloc();
    if (avcodec_receive_frame(st.decoder, frame) < 0) {
        av_frame_free(&frame);  // needs more packets (inter-frame) — fine
        return;
    }
    st.last_sample_us = packet.utc_receive_us;

    // ---- Spatial downsampling: SIMD libswscale to 320x240 GRAY8 -----------
    if (st.scaler == nullptr) {
        st.scaler = sws_getContext(frame->width, frame->height,
                                   static_cast<AVPixelFormat>(frame->format),
                                   kMotionW, kMotionH, AV_PIX_FMT_GRAY8,
                                   SWS_FAST_BILINEAR, nullptr, nullptr,
                                   nullptr);
    }

    const int slot = st.filled % 3;
    st.frames[slot].resize(kMotionPixels);
    uint8_t* dst_planes[4] = {st.frames[slot].data(), nullptr, nullptr, nullptr};
    int dst_strides[4] = {kMotionW, 0, 0, 0};
    sws_scale(st.scaler, frame->data, frame->linesize, 0, frame->height,
              dst_planes, dst_strides);

    if (++st.filled >= 3)
        run_layer3_simd(st, packet.camera_uuid, packet.utc_receive_us);

    // ---- AI handoff: reuse THIS decode; announce only while motion active --
    maybe_announce_ai_frame(st, packet.camera_uuid, frame,
                            packet.utc_receive_us);
    av_frame_free(&frame);
}

#ifdef VMS_HAVE_CUDA
void MotionEngine::run_layer2_gpu(CameraMotionState& st,
                                  const TimedPacket& packet) {
    // Same temporal gate as Layer 3: MOG2 with alpha=0.005 at 4 fps gives a
    // ~50 s background history — right for surveillance scenes.
    if (packet.utc_receive_us - st.last_sample_us < kSampleIntervalUs) return;

    // ---- Lazy NVDEC decoder: frames decode INTO VRAM (AV_PIX_FMT_CUDA) ----
    if (st.decoder == nullptr) {
        if (av_hwdevice_ctx_create(&st.hw_device, AV_HWDEVICE_TYPE_CUDA,
                                   nullptr, nullptr, 0) < 0) {
            st.gpu_failed = true;  // no usable CUDA device -> Layer 3
            return;
        }
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        st.decoder = avcodec_alloc_context3(codec);
        st.decoder->hw_device_ctx = av_buffer_ref(st.hw_device);
        st.decoder->thread_count = 1;
        if (avcodec_open2(st.decoder, codec, nullptr) < 0) {
            avcodec_free_context(&st.decoder);
            av_buffer_unref(&st.hw_device);
            st.gpu_failed = true;
            return;
        }
    }

    if (avcodec_send_packet(st.decoder, packet.pkt) < 0) return;

    AVFrame* frame = av_frame_alloc();
    if (avcodec_receive_frame(st.decoder, frame) < 0) {
        av_frame_free(&frame);  // inter-frame, needs more packets — fine
        return;
    }
    st.last_sample_us = packet.utc_receive_us;

    if (frame->format != AV_PIX_FMT_CUDA) {
        // Driver silently fell back to software surfaces: treat as GPU
        // failure so select_layer() routes this camera to Layer 3.
        av_frame_free(&frame);
        st.gpu_failed = true;
        return;
    }

    // ---- Lazy VRAM MOG2 model at the true NVDEC surface geometry ----------
    if (st.gpu_ctx == nullptr) {
        st.gpu_ctx = vms_gpu_bgsub_create(frame->width, frame->height);
        if (st.gpu_ctx == nullptr) {  // VRAM exhausted -> Layer 3
            av_frame_free(&frame);
            st.gpu_failed = true;
            return;
        }
        st.gpu_roi_dirty = true;
    }
    if (st.gpu_roi_dirty) {
        // Expand Django's grid at the GPU raster size (not 320x240) and
        // upload once; the kernel ANDs it in for free afterwards.
        std::vector<uint8_t> gpu_mask;
        if (st.roi.bitmask.empty()) {
            gpu_mask.assign(static_cast<size_t>(frame->width) * frame->height,
                            0xFF);  // default ROI = everything enabled
        } else {
            expand_roi_mask(st.roi, frame->width, frame->height, gpu_mask);
        }
        vms_gpu_bgsub_set_roi(st.gpu_ctx, gpu_mask.data());
        st.gpu_roi_dirty = false;
    }

    // ---- NV12 in VRAM: data[0] IS the luma plane; linesize[0] is the pitch.
    //      MOG2 runs on CUDA cores; only a 4-byte counter returns via PCIe.
    const uint32_t changed =
        vms_gpu_bgsub_process(st.gpu_ctx, frame->data[0], frame->linesize[0]);

    const size_t pixels = static_cast<size_t>(frame->width) * frame->height;
    const bool trigger = evaluate_motion(changed, pixels, kTriggerRatio);
    const bool release = !evaluate_motion(changed, pixels, kReleaseRatio);

    if (!st.motion_active && trigger) {
        st.motion_active = true;
        emit_event(packet.camera_uuid, /*started=*/true, packet.utc_receive_us);
    } else if (st.motion_active && release) {
        st.motion_active = false;
        emit_event(packet.camera_uuid, /*started=*/false, packet.utc_receive_us);
    }

    // ---- AI handoff: pay the VRAM->RAM copy ONLY when motion is active and
    //      the camera is AI-enabled (motion-gated by contract).
    if (st.ai_enabled && st.motion_active && ai_client_ != nullptr) {
        AVFrame* sw = av_frame_alloc();
        if (av_hwframe_transfer_data(sw, frame, 0) == 0) {
            sw->width = frame->width;
            sw->height = frame->height;
            maybe_announce_ai_frame(st, packet.camera_uuid, sw,
                                    packet.utc_receive_us);
        }
        av_frame_free(&sw);
    }
    av_frame_free(&frame);
}
#endif  // VMS_HAVE_CUDA

void MotionEngine::maybe_announce_ai_frame(CameraMotionState& st,
                                           const std::string& camera_uuid,
                                           const AVFrame* sw_frame,
                                           uint64_t utc_us) {
    // Motion gate: idle scenes never wake the AI engine (architecture rule).
    if (!st.ai_enabled || !st.motion_active || ai_client_ == nullptr ||
        sw_frame == nullptr || sw_frame->width <= 0) {
        return;
    }

    // ---- Lazy per-camera shm ring: geometry known only after first decode --
    if (st.ai_writer == nullptr) {
        st.ai_writer = std::make_unique<ShmFrameWriter>(
            camera_uuid, static_cast<uint32_t>(sw_frame->width),
            static_cast<uint32_t>(sw_frame->height),
            /*bytes_per_pixel=*/3 /* -> PIXEL_FORMAT_BGR24 */,
            /*slot_count=*/4);
        st.ai_writer->set_inference_client(ai_client_);  // register_writer()
    }

    ShmSlot* slot = st.ai_writer->acquire_slot();
    if (slot == nullptr) return;  // all 4 slots in flight: AI saturated, drop

    // ---- Zero-copy render: libswscale's destination IS the shared memory --
    if (st.ai_scaler == nullptr) {
        st.ai_scaler = sws_getContext(
            sw_frame->width, sw_frame->height,
            static_cast<AVPixelFormat>(sw_frame->format),
            static_cast<int>(st.ai_writer->width()),
            static_cast<int>(st.ai_writer->height()), AV_PIX_FMT_BGR24,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    }
    if (st.ai_scaler == nullptr) {
        // Scaler init failed: hand the slot straight back to the ring.
        slot->in_flight.store(false, std::memory_order_release);
        return;
    }

    uint8_t* dst_planes[4] = {slot->mapped, nullptr, nullptr, nullptr};
    int dst_strides[4] = {static_cast<int>(st.ai_writer->stride()), 0, 0, 0};
    sws_scale(st.ai_scaler, sw_frame->data, sw_frame->linesize, 0,
              sw_frame->height, dst_planes, dst_strides);

    // announce() emits the gRPC FrameDescriptor; on stream failure it frees
    // the slot itself so the ring never leaks under reconnects.
    st.ai_writer->announce(*slot, utc_us / 1000, st.ai_sequence++);
}

void MotionEngine::run_layer3_simd(CameraMotionState& st,
                                   const std::string& camera_uuid,
                                   uint64_t utc_us) {
    // Default ROI = everything enabled (operators narrow it from the client).
    if (st.roi_pixel_mask.size() != kMotionPixels)
        st.roi_pixel_mask.assign(kMotionPixels, 0xFF);

    // Map the rolling window to chronological order f0 -> f1 -> f2.
    const int newest = (st.filled - 1) % 3;
    const uint8_t* f2 = st.frames[newest].data();
    const uint8_t* f1 = st.frames[(newest + 2) % 3].data();
    const uint8_t* f0 = st.frames[(newest + 1) % 3].data();

    const uint32_t changed = simd_three_frame_diff(
        f0, f1, f2, st.roi_pixel_mask.data(), kMotionPixels,
        sensitivity_to_threshold(st.roi.sensitivity));

    // Hysteresis: trigger high, release low — a person pausing mid-frame
    // doesn't strobe MOTION_STARTED/STOPPED events into the audit log.
    const bool trigger = evaluate_motion(changed, kMotionPixels, kTriggerRatio);
    const bool release = !evaluate_motion(changed, kMotionPixels, kReleaseRatio);

    if (!st.motion_active && trigger) {
        st.motion_active = true;
        emit_event(camera_uuid, /*started=*/true, utc_us);
    } else if (st.motion_active && release) {
        st.motion_active = false;
        emit_event(camera_uuid, /*started=*/false, utc_us);
    }
}

void MotionEngine::emit_event(const std::string& camera_uuid, bool started,
                              uint64_t utc_us) {
    // ControlService streams this as EVENT_MOTION_STARTED/STOPPED to Django
    // (audit trail + alert fan-out + motion-gated AI frame announcements).
    control_.report_motion(camera_uuid, started, utc_us);
}

}  // namespace vms
