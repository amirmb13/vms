#pragma once
#include <cstddef>
#include <cstdint>

namespace vms {
uint32_t simd_three_frame_diff(const uint8_t* f0, const uint8_t* f1,
                               const uint8_t* f2, const uint8_t* roi_mask,
                               size_t pixel_count, uint8_t threshold);
bool evaluate_motion(uint32_t changed_pixels, size_t pixel_count,
                     float trigger_ratio);
}  // namespace vms
