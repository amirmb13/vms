"""
Hardware-agnostic inference backends for the VMS AI engine.

Backend selection order (AI_BACKEND=auto):
  1. openvino-int8  — Intel CPU/iGPU via OpenVINO Runtime (preferred on CPU)
  2. onnx-cpu-int8  — ONNX Runtime CPUExecutionProvider fallback

Models (AI_MODELS_DIR, default /models):
  - detector: face/person detector, e.g. yolov8n-face.onnx / .xml
  - arcface : ArcFace-R100 recognition model producing 512-d embeddings
Missing model files degrade gracefully: the engine still serves gRPC and
reports its capabilities, returning empty detections.
"""
from __future__ import annotations

import logging
import os
import threading

import numpy as np

logger = logging.getLogger("vms.ai.inference")

MODELS_DIR = os.environ.get("AI_MODELS_DIR", "/models")
REQUESTED_BACKEND = os.environ.get("AI_BACKEND", "auto")

ARCFACE_INPUT = (112, 112)  # standard ArcFace aligned-face input


def _l2_normalize(vec: np.ndarray) -> np.ndarray:
    norm = np.linalg.norm(vec)
    return vec / norm if norm > 0 else vec


class _OpenVinoModel:
    def __init__(self, xml_path: str):
        import openvino as ov

        core = ov.Core()
        self._compiled = core.compile_model(core.read_model(xml_path), "AUTO")
        self._lock = threading.Lock()

    def infer(self, blob: np.ndarray) -> np.ndarray:
        with self._lock:
            return next(iter(self._compiled(blob).values()))


class _OnnxModel:
    def __init__(self, onnx_path: str):
        import onnxruntime as ort

        self._session = ort.InferenceSession(
            onnx_path, providers=["CPUExecutionProvider"]
        )
        self._input_name = self._session.get_inputs()[0].name

    def infer(self, blob: np.ndarray) -> np.ndarray:
        return self._session.run(None, {self._input_name: blob})[0]


def _load_model(basename: str):
    """Try OpenVINO IR first, then ONNX. Returns (model, backend) or (None, None)."""
    xml = os.path.join(MODELS_DIR, f"{basename}.xml")
    onnx = os.path.join(MODELS_DIR, f"{basename}.onnx")

    if REQUESTED_BACKEND in ("auto", "openvino-int8") and os.path.exists(xml):
        try:
            return _OpenVinoModel(xml), "openvino-int8"
        except Exception as exc:  # openvino not installed / bad IR
            logger.warning("OpenVINO load failed for %s: %s", basename, exc)

    if os.path.exists(onnx):
        # OpenVINO can also run raw ONNX directly
        if REQUESTED_BACKEND in ("auto", "openvino-int8"):
            try:
                return _OpenVinoModel(onnx), "openvino-int8"
            except Exception:
                pass
        try:
            return _OnnxModel(onnx), "onnx-cpu-int8"
        except Exception as exc:
            logger.warning("ONNX Runtime load failed for %s: %s", basename, exc)

    return None, None


class InferenceEngine:
    """Face detection + ArcFace embedding extraction on shared-memory frames."""

    def __init__(self):
        self.detector, det_backend = _load_model("detector")
        self.arcface, arc_backend = _load_model("arcface")
        self.backend = arc_backend or det_backend or "none"
        self.models = []
        if self.detector:
            self.models.append("detector")
        if self.arcface:
            self.models.append("arcface-r100")
        logger.info("[ai] backend=%s models=%s", self.backend, self.models)

    # ------------------------------------------------------------------ #

    def detect_faces(self, bgr: np.ndarray) -> list[dict]:
        """Run the face detector. Returns normalized boxes with confidence."""
        if self.detector is None:
            return []

        h, w = bgr.shape[:2]
        blob = self._preprocess(bgr, (640, 640))
        raw = self.detector.infer(blob)

        detections: list[dict] = []
        # Expected layout: rows of [x1, y1, x2, y2, score] in 640-space.
        for row in np.asarray(raw).reshape(-1, raw.shape[-1]):
            score = float(row[4])
            if score < 0.5:
                continue
            x1, y1, x2, y2 = (float(v) for v in row[:4])
            detections.append({
                "x": max(x1 / 640, 0.0),
                "y": max(y1 / 640, 0.0),
                "w": min((x2 - x1) / 640, 1.0),
                "h": min((y2 - y1) / 640, 1.0),
                "confidence": score,
            })
        return detections

    def embed_face(self, bgr: np.ndarray, box: dict) -> np.ndarray | None:
        """Crop a detected face and produce a 512-d L2-normalized embedding."""
        if self.arcface is None:
            return None

        h, w = bgr.shape[:2]
        x1 = int(box["x"] * w)
        y1 = int(box["y"] * h)
        x2 = min(int((box["x"] + box["w"]) * w), w)
        y2 = min(int((box["y"] + box["h"]) * h), h)
        if x2 - x1 < 16 or y2 - y1 < 16:
            return None

        crop = bgr[y1:y2, x1:x2]
        blob = self._preprocess(crop, ARCFACE_INPUT, scale=1 / 127.5, mean=127.5)
        embedding = np.asarray(self.arcface.infer(blob)).reshape(-1).astype(np.float32)
        return _l2_normalize(embedding)

    # ------------------------------------------------------------------ #

    @staticmethod
    def _preprocess(bgr: np.ndarray, size: tuple[int, int],
                    scale: float = 1 / 255.0, mean: float = 0.0) -> np.ndarray:
        import cv2

        resized = cv2.resize(bgr, size, interpolation=cv2.INTER_LINEAR)
        rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.float32)
        normalized = (rgb - mean) * scale
        return np.expand_dims(normalized.transpose(2, 0, 1), axis=0)  # NCHW
