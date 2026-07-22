"""
Zero-copy shared-memory frame access.

The native C++ Recording Server decodes frames and writes raw pixel data into
POSIX shared memory blocks (shm_open), then announces them over gRPC with a
FrameDescriptor (name + geometry + slot index). This module maps the block
and exposes it as a NumPy array WITHOUT copying pixel data.

Ownership protocol (ring buffer):
  - C++ owns a slot until it sends the FrameDescriptor.
  - Python owns the slot while processing.
  - Returning InferenceResult{slot_index} releases the slot back to C++.
"""
from __future__ import annotations

import logging
from multiprocessing import shared_memory

import numpy as np

logger = logging.getLogger("vms.ai.shm")

# PixelFormat enum values from proto/ai_signaling.proto
_CHANNELS = {
    1: 3,  # RGB24
    2: 3,  # BGR24
    3: 1,  # GRAY8
    4: 1,  # NV12 (handled specially: height*3/2 rows of stride bytes)
}


class ShmFrame:
    """Context manager mapping one shared-memory frame as a zero-copy ndarray."""

    def __init__(self, shm_name: str, size_bytes: int, width: int, height: int,
                 stride_bytes: int, pixel_format: int):
        # C++ creates "/vms_frame_..."; Python's API expects no leading slash.
        self._name = shm_name.lstrip("/")
        self._size = size_bytes
        self.width = width
        self.height = height
        self.stride = stride_bytes
        self.format = pixel_format
        self._shm: shared_memory.SharedMemory | None = None

    def __enter__(self) -> np.ndarray:
        self._shm = shared_memory.SharedMemory(name=self._name, create=False)
        buf = np.frombuffer(self._shm.buf, dtype=np.uint8, count=self._size)

        if self.format == 4:  # NV12: Y plane + interleaved UV half-height plane
            rows = self.height * 3 // 2
            planar = buf[: rows * self.stride].reshape(rows, self.stride)
            return planar[:, : self.width]

        channels = _CHANNELS.get(self.format, 3)
        rows = buf[: self.height * self.stride].reshape(self.height, self.stride)
        return rows[:, : self.width * channels].reshape(
            self.height, self.width, channels
        )

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._shm is not None:
            # close() only unmaps our view; the C++ side owns the lifecycle
            # (never unlink() here — that would destroy the ring buffer slot).
            self._shm.close()
            self._shm = None


def nv12_to_bgr(nv12_view: np.ndarray, width: int, height: int) -> np.ndarray:
    """Convert an NV12 zero-copy view to BGR (this step necessarily copies)."""
    import cv2

    return cv2.cvtColor(
        np.ascontiguousarray(nv12_view), cv2.COLOR_YUV2BGR_NV12
    )
