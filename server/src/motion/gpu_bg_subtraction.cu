// =============================================================================
// Layer 2 (GPU) — MOG2-style Gaussian Mixture background subtraction on CUDA.
//
// Design (mandated by the architecture):
//   - The sub-stream (360p) is decoded by NVDEC directly into VRAM; frames
//     NEVER round-trip through system RAM (zero-copy GPU pipeline).
//   - Each pixel keeps K=3 Gaussian modes {mean, variance, weight} resident
//     in device memory. The per-pixel update is embarrassingly parallel.
//   - The kernel writes a foreground bitmask which a warp-level reduction
//     collapses to a single "changed pixel" counter — only 4 bytes ever
//     cross PCIe back to the host per analyzed frame.
//   - The ROI bitmask grid (already expanded to per-pixel bytes by
//     MotionEngine::update_roi) is uploaded once per configuration change
//     and ANDed in before the counter, same contract as the CPU SIMD path.
// =============================================================================
#include <cuda_runtime.h>

#include <cstdint>

namespace vms {
namespace gpu {

namespace {

constexpr int kModes = 3;             // Gaussians per pixel (MOG2 default)
constexpr float kAlpha = 0.005f;      // learning rate (~200-frame history)
constexpr float kVarInit = 225.0f;    // initial variance (15^2 gray levels)
constexpr float kVarMin = 16.0f;
constexpr float kVarMax = 5.0f * kVarInit;
constexpr float kBackgroundRatio = 0.9f;
constexpr float kMahaThreshold2 = 16.0f;  // 4-sigma^2 match threshold

struct GaussianMode {
    float mean;
    float variance;
    float weight;
};

// One thread per pixel: match/update the mixture, emit foreground decision.
// `frame` is the NVDEC luma plane in VRAM; NVDEC surfaces are pitch-padded,
// so pixel (row, col) lives at frame[row * pitch + col] while the model and
// ROI mask stay tightly packed (width * height).
__global__ void mog2_update_kernel(const uint8_t* __restrict__ frame,
                                   const uint8_t* __restrict__ roi_mask,
                                   GaussianMode* __restrict__ model,
                                   uint32_t* __restrict__ changed_count,
                                   int width, int pitch, int pixel_count) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixel_count) return;

    const int row = idx / width;
    const int col = idx - row * width;
    const float value = static_cast<float>(frame[row * pitch + col]);
    GaussianMode* modes = model + static_cast<size_t>(idx) * kModes;

    // ---- Match against existing modes (weight-sorted invariant) -----------
    int matched = -1;
    float total_weight = 0.0f;
    for (int k = 0; k < kModes; ++k) {
        total_weight += modes[k].weight;
        if (matched >= 0) continue;
        const float d = value - modes[k].mean;
        if (d * d < kMahaThreshold2 * modes[k].variance) matched = k;
    }

    bool foreground;
    if (matched >= 0) {
        // ---- Update matched mode (standard MOG2 recursions) --------------
        GaussianMode& m = modes[matched];
        const float rho = kAlpha / fmaxf(m.weight, kAlpha);
        const float d = value - m.mean;
        m.mean += rho * d;
        m.variance = fminf(fmaxf(m.variance + rho * (d * d - m.variance),
                                 kVarMin), kVarMax);

        // Weight update: matched mode grows, others decay.
        float cumulative = 0.0f;
        for (int k = 0; k < kModes; ++k) {
            modes[k].weight += kAlpha * ((k == matched ? 1.0f : 0.0f)
                                         - modes[k].weight);
            cumulative += modes[k].weight;
        }
        // Renormalize so weights always sum to 1.
        for (int k = 0; k < kModes; ++k) modes[k].weight /= cumulative;

        // Foreground iff the matched mode is NOT part of the background set
        // (the top modes whose cumulative weight covers kBackgroundRatio).
        float bg = 0.0f;
        foreground = true;
        for (int k = 0; k < kModes; ++k) {
            bg += modes[k].weight;
            if (k == matched) { foreground = false; break; }
            if (bg > kBackgroundRatio) break;
        }
    } else {
        // ---- No match: replace the weakest mode with a new Gaussian ------
        int weakest = 0;
        for (int k = 1; k < kModes; ++k)
            if (modes[k].weight < modes[weakest].weight) weakest = k;
        modes[weakest].mean = value;
        modes[weakest].variance = kVarInit;
        modes[weakest].weight = kAlpha;
        foreground = true;
        (void)total_weight;
    }

    // ---- ROI gate + warp-aggregated global counter -------------------------
    const bool hit = foreground && (roi_mask[idx] != 0);
    const unsigned ballot = __ballot_sync(__activemask(), hit);
    if ((threadIdx.x & 31u) == 0u && ballot != 0u)
        atomicAdd(changed_count, __popc(ballot));
}

}  // namespace

// -----------------------------------------------------------------------------
// Host-side wrapper, exposed to motion_engine.cpp behind VMS_HAVE_CUDA.
// -----------------------------------------------------------------------------
struct GpuBgSubtractor {
    GaussianMode* d_model = nullptr;
    uint8_t* d_roi = nullptr;
    uint32_t* d_count = nullptr;
    int width = 0;
    int pixel_count = 0;
    cudaStream_t stream = nullptr;
};

extern "C" GpuBgSubtractor* vms_gpu_bgsub_create(int width, int height) {
    auto* ctx = new GpuBgSubtractor();
    ctx->width = width;
    ctx->pixel_count = width * height;
    const size_t model_bytes =
        static_cast<size_t>(ctx->pixel_count) * kModes * sizeof(GaussianMode);

    if (cudaStreamCreateWithFlags(&ctx->stream, cudaStreamNonBlocking) !=
            cudaSuccess ||
        cudaMalloc(&ctx->d_model, model_bytes) != cudaSuccess ||
        cudaMalloc(&ctx->d_roi, ctx->pixel_count) != cudaSuccess ||
        cudaMalloc(&ctx->d_count, sizeof(uint32_t)) != cudaSuccess) {
        delete ctx;
        return nullptr;
    }
    cudaMemsetAsync(ctx->d_model, 0, model_bytes, ctx->stream);
    cudaMemsetAsync(ctx->d_roi, 0xFF, ctx->pixel_count, ctx->stream);
    return ctx;
}

extern "C" void vms_gpu_bgsub_set_roi(GpuBgSubtractor* ctx,
                                      const uint8_t* host_roi_mask) {
    if (ctx == nullptr) return;
    cudaMemcpyAsync(ctx->d_roi, host_roi_mask, ctx->pixel_count,
                    cudaMemcpyHostToDevice, ctx->stream);
}

// `device_gray_frame` points into VRAM (NVDEC output plane after NV12->GRAY
// extraction — the luma plane IS the gray frame, no conversion needed).
// `pitch_bytes` is the NVDEC surface pitch (AVFrame::linesize[0] on the
// AV_PIX_FMT_CUDA frame); pass width if the plane is tightly packed.
// Returns the number of foreground pixels inside the ROI.
extern "C" uint32_t vms_gpu_bgsub_process(GpuBgSubtractor* ctx,
                                          const uint8_t* device_gray_frame,
                                          int pitch_bytes) {
    if (ctx == nullptr) return 0;
    if (pitch_bytes <= 0) pitch_bytes = ctx->width;

    cudaMemsetAsync(ctx->d_count, 0, sizeof(uint32_t), ctx->stream);

    constexpr int kBlock = 256;
    const int grid = (ctx->pixel_count + kBlock - 1) / kBlock;
    mog2_update_kernel<<<grid, kBlock, 0, ctx->stream>>>(
        device_gray_frame, ctx->d_roi, ctx->d_model, ctx->d_count,
        ctx->width, pitch_bytes, ctx->pixel_count);

    // The ONLY device->host transfer: a single 4-byte counter.
    uint32_t changed = 0;
    cudaMemcpyAsync(&changed, ctx->d_count, sizeof(uint32_t),
                    cudaMemcpyDeviceToHost, ctx->stream);
    cudaStreamSynchronize(ctx->stream);
    return changed;
}

extern "C" void vms_gpu_bgsub_destroy(GpuBgSubtractor* ctx) {
    if (ctx == nullptr) return;
    if (ctx->d_model != nullptr) cudaFree(ctx->d_model);
    if (ctx->d_roi != nullptr) cudaFree(ctx->d_roi);
    if (ctx->d_count != nullptr) cudaFree(ctx->d_count);
    if (ctx->stream != nullptr) cudaStreamDestroy(ctx->stream);
    delete ctx;
}

}  // namespace gpu
}  // namespace vms
