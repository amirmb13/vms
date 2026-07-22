#pragma once
// Host-side C API of the Layer-2 CUDA MOG2 background subtractor
// (gpu_bg_subtraction.cu). Only compiled/linked when VMS_WITH_CUDA=ON;
// callers must guard usage behind VMS_HAVE_CUDA.
//
// Contract:
//   - create(width, height) allocates the per-pixel Gaussian mixture model,
//     the ROI mask, and the foreground counter in VRAM.
//   - set_roi() uploads a tightly packed width*height byte mask (0 / 0xFF),
//     same expansion contract as the CPU SIMD path.
//   - process() consumes an NVDEC luma plane that LIVES IN VRAM
//     (AVFrame::data[0] of an AV_PIX_FMT_CUDA frame) with its surface pitch
//     (AVFrame::linesize[0]); only a 4-byte counter returns over PCIe.

#include <cstdint>

extern "C" {

// Opaque VRAM context (defined as vms::gpu::GpuBgSubtractor in the .cu TU;
// extern "C" linkage makes the symbol names identical either way).
typedef struct VmsGpuBgSubtractor VmsGpuBgSubtractor;

VmsGpuBgSubtractor* vms_gpu_bgsub_create(int width, int height);

void vms_gpu_bgsub_set_roi(VmsGpuBgSubtractor* ctx,
                           const uint8_t* host_roi_mask);

uint32_t vms_gpu_bgsub_process(VmsGpuBgSubtractor* ctx,
                               const uint8_t* device_gray_frame,
                               int pitch_bytes);

void vms_gpu_bgsub_destroy(VmsGpuBgSubtractor* ctx);

}  // extern "C"
