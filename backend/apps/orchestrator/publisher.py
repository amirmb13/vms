"""
Asynchronous event orchestrator.

When camera settings change through the Django API, this module pushes
lightweight control signals to the responsible C++ Recording Server so it can
reset RTSP sessions dynamically — WITHOUT restarting any C++ service.

Two transports:
  1. Redis Pub/Sub  (fan-out, fire-and-forget)  -> channel settings.CONTROL_SIGNAL_CHANNEL
  2. Direct gRPC    (RecordingServerControl service in proto/control_signals.proto)
"""
from __future__ import annotations

import json
import logging

import redis
from django.conf import settings

logger = logging.getLogger("vms.orchestrator")

_redis = redis.Redis.from_url(settings.REDIS_URL, decode_responses=True)


def publish_camera_config(camera) -> None:
    """Fan out a CameraConfigRequest-shaped signal after a camera change."""
    signal = {
        "type": "camera_config",
        "server_uuid": str(camera.recording_server.uuid),
        "camera_uuid": str(camera.uuid),
        "config_revision": camera.config_revision,
        "recording_enabled": camera.recording_enabled,
        "retention_days": camera.retention_days,
        "onvif_motion_events": camera.onvif_motion_events,
        "streams": {
            "main": camera.rtsp_main_url,
            "mid": camera.rtsp_mid_url,
            "sub": camera.rtsp_sub_url,
        },
    }
    _redis.publish(settings.CONTROL_SIGNAL_CHANNEL, json.dumps(signal))
    logger.info("[orchestrator] pushed config rev=%s for camera=%s",
                camera.config_revision, camera.uuid)


def publish_roi_update(roi) -> None:
    import base64

    signal = {
        "type": "motion_roi",
        "server_uuid": str(roi.camera.recording_server.uuid),
        "camera_uuid": str(roi.camera.uuid),
        "grid_cols": roi.grid_cols,
        "grid_rows": roi.grid_rows,
        "sensitivity": roi.sensitivity,
        "roi_bitmask_b64": base64.b64encode(bytes(roi.bitmask)).decode(),
    }
    _redis.publish(settings.CONTROL_SIGNAL_CHANNEL, json.dumps(signal))


def publish_synopsis_request(job) -> None:
    """Push a Video Synopsis (TimeCompressor) job to the responsible
    C++ recording server. The worker reports progress back through
    POST /api/synopsis/<uuid>/progress until the manifest lands."""
    signal = {
        "type": "synopsis_request",
        "server_uuid": str(job.camera.recording_server.uuid),
        "job_uuid": str(job.uuid),
        "camera_uuid": str(job.camera.uuid),
        "from_utc_epoch_us": int(job.from_utc.timestamp() * 1_000_000),
        "to_utc_epoch_us": int(job.to_utc.timestamp() * 1_000_000),
        "target_duration_s": job.target_duration_s,
    }
    _redis.publish(settings.CONTROL_SIGNAL_CHANNEL, json.dumps(signal))
    logger.info("[orchestrator] pushed synopsis job=%s camera=%s",
                job.uuid, job.camera.uuid)


def publish_synopsis_cancel(job) -> None:
    signal = {
        "type": "synopsis_cancel",
        "server_uuid": str(job.camera.recording_server.uuid),
        "job_uuid": str(job.uuid),
    }
    _redis.publish(settings.CONTROL_SIGNAL_CHANNEL, json.dumps(signal))


def push_prune_grpc(four_eyes_request) -> bool:
    """
    Execute a fully-approved Four-Eyes archive prune via direct gRPC
    (RecordingServerControl.PruneArchive on the responsible C++ server).

    Stubs live in backend/generated/ (see generated/__init__.py for the
    regeneration command). Both admin signatures are forwarded so the C++
    side can log them into its immutable audit trail.
    """
    if not four_eyes_request.is_fully_approved:
        raise PermissionError("عملیات نیازمند تأیید دو مدیر مجزا است.")

    import grpc
    from django.utils import timezone

    from generated import control_signals_pb2, control_signals_pb2_grpc
    from apps.cameras.models import Camera

    payload = four_eyes_request.payload or {}
    camera_uuid = payload.get("camera_uuid", "")
    before_utc_epoch_ms = int(payload.get("before_utc_epoch_ms", 0))

    try:
        camera = Camera.objects.select_related("recording_server").get(
            uuid=camera_uuid
        )
    except Camera.DoesNotExist:
        logger.error(
            "[orchestrator] prune request=%s references unknown camera=%s",
            four_eyes_request.uuid, camera_uuid,
        )
        return False

    endpoint = camera.recording_server.grpc_endpoint
    request = control_signals_pb2.PruneRequest(
        camera_uuid=str(camera.uuid),
        before_utc_epoch_ms=before_utc_epoch_ms,
        approval_signature_a=four_eyes_request.signature_a,
        approval_signature_b=four_eyes_request.signature_b,
    )

    try:
        with grpc.insecure_channel(endpoint) as channel:
            stub = control_signals_pb2_grpc.RecordingServerControlStub(channel)
            ack = stub.PruneArchive(
                request, timeout=settings.RECORDING_SERVER_GRPC_TIMEOUT_S
            )
    except grpc.RpcError as exc:
        logger.error(
            "[orchestrator] prune gRPC failed request=%s server=%s code=%s",
            four_eyes_request.uuid, endpoint, exc.code(),
        )
        return False

    if not ack.ok:
        logger.error(
            "[orchestrator] prune rejected by server request=%s message=%s",
            four_eyes_request.uuid, ack.message_fa,
        )
        return False

    four_eyes_request.status = four_eyes_request.Status.EXECUTED
    four_eyes_request.executed_at = timezone.now()  # stored UTC
    four_eyes_request.save(update_fields=["status", "executed_at"])
    logger.info(
        "[orchestrator] prune executed request=%s server=%s ack=%s",
        four_eyes_request.uuid, endpoint, ack.message_fa,
    )
    return True
