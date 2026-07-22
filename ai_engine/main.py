"""
VMS AI Engine — gRPC server implementing vms.ai.AiInference.

Zero-copy pipeline:
  C++ writes decoded frames into POSIX shared memory (--ipc=host container),
  sends a FrameDescriptor here, and we map the block as a NumPy view.
  Only detections + embeddings travel back over gRPC; pixels never do.
  Returning InferenceResult{slot_index} releases the ring-buffer slot.
"""
from __future__ import annotations

import logging
import os
import sys
import time
from concurrent import futures

import grpc

# Generated stubs live next to this file (see Dockerfile / gen_stubs command).
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "generated"))
import ai_signaling_pb2  # noqa: E402
import ai_signaling_pb2_grpc  # noqa: E402

from inference import InferenceEngine  # noqa: E402
from shm_reader import ShmFrame, nv12_to_bgr  # noqa: E402

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(name)s %(levelname)s %(message)s")
logger = logging.getLogger("vms.ai.server")

GRPC_PORT = int(os.environ.get("AI_GRPC_PORT", "50061"))
MAX_PARALLEL = int(os.environ.get("AI_MAX_PARALLEL_FRAMES", "4"))


class AiInferenceServicer(ai_signaling_pb2_grpc.AiInferenceServicer):
    def __init__(self):
        self.engine = InferenceEngine()

    # ------------------------------------------------------------------ #

    def ProcessFrame(self, request, context):
        return self._process(request)

    def ProcessFrameStream(self, request_iterator, context):
        for descriptor in request_iterator:
            yield self._process(descriptor)

    def GetCapabilities(self, request, context):
        return ai_signaling_pb2.AiCapabilities(
            gpu_available=False,  # OpenVINO/ONNX CPU deployment profile
            backend=self.engine.backend,
            models=self.engine.models,
            max_parallel_frames=MAX_PARALLEL,
        )

    # ------------------------------------------------------------------ #

    def _process(self, desc) -> "ai_signaling_pb2.InferenceResult":
        started = time.perf_counter()
        result = ai_signaling_pb2.InferenceResult(
            camera_uuid=desc.camera_uuid,
            utc_epoch_ms=desc.utc_epoch_ms,
            frame_sequence=desc.frame_sequence,
            slot_index=desc.slot_index,  # releases the slot back to C++
            backend=self.engine.backend,
        )

        try:
            frame = ShmFrame(desc.shm_name, desc.shm_size_bytes, desc.width,
                             desc.height, desc.stride_bytes, desc.format)
            with frame as view:
                if desc.format == ai_signaling_pb2.PIXEL_FORMAT_NV12:
                    bgr = nv12_to_bgr(view, desc.width, desc.height)
                elif desc.format == ai_signaling_pb2.PIXEL_FORMAT_RGB24:
                    bgr = view[:, :, ::-1]
                elif desc.format == ai_signaling_pb2.PIXEL_FORMAT_GRAY8:
                    import cv2
                    bgr = cv2.cvtColor(view, cv2.COLOR_GRAY2BGR)
                else:  # BGR24 — fully zero-copy path
                    bgr = view

                for box in self.engine.detect_faces(bgr):
                    face = result.faces.add()
                    face.box.x = box["x"]
                    face.box.y = box["y"]
                    face.box.w = box["w"]
                    face.box.h = box["h"]
                    face.box.confidence = box["confidence"]
                    embedding = self.engine.embed_face(bgr, box)
                    if embedding is not None:
                        # Identity matching happens in Django (pgvector);
                        # we ship the raw 512-d embedding for forensic storage.
                        face.embedding.extend(embedding.tolist())
        except FileNotFoundError:
            logger.warning("[ai] shm block missing: %s (slot=%s)",
                           desc.shm_name, desc.slot_index)
        except Exception:
            logger.exception("[ai] inference failed camera=%s seq=%s",
                             desc.camera_uuid, desc.frame_sequence)

        result.inference_ms = (time.perf_counter() - started) * 1000.0
        return result


def serve() -> None:
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=MAX_PARALLEL),
        options=[
            ("grpc.max_receive_message_length", 1 << 20),  # descriptors only
            ("grpc.keepalive_time_ms", 30_000),
        ],
    )
    ai_signaling_pb2_grpc.add_AiInferenceServicer_to_server(
        AiInferenceServicer(), server
    )
    server.add_insecure_port(f"[::]:{GRPC_PORT}")
    server.start()
    logger.info("[ai] AiInference serving on :%s (max_parallel=%s)",
                GRPC_PORT, MAX_PARALLEL)
    server.wait_for_termination()


if __name__ == "__main__":
    serve()
