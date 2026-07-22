"""
ControlPlaneEvents gRPC server — the Django-hosted half of the control channel.

The C++ Recording Server (server/src/control/control_service.cpp) calls:

  1. RegisterServer  — on boot: upserts the RecordingServer row, marks it
     online, and returns the AssignedCameraSet (every camera bound to that
     server in PostgreSQL, the single source of truth).
  2. ReportEvent     — long-lived client stream of motion / connectivity /
     storage / AI events, persisted into the immutable audit trail and (for
     face matches) the forensic DetectionRecord table.

Run with:  python manage.py run_control_grpc
(also wired as the `django-grpc` service in docker/docker-compose.yml)
"""
from __future__ import annotations

import json
import logging
from concurrent import futures
from datetime import datetime, timezone as dt_timezone

import grpc
from django.conf import settings
from django.db import close_old_connections
from django.utils import timezone

from generated import control_signals_pb2 as pb2
from generated import control_signals_pb2_grpc as pb2_grpc

logger = logging.getLogger("vms.orchestrator.grpc")

# Farsi labels for audit messages, keyed by proto EventType.
_EVENT_MESSAGES_FA = {
    pb2.EVENT_MOTION_STARTED: "تشخیص حرکت آغاز شد",
    pb2.EVENT_MOTION_STOPPED: "تشخیص حرکت پایان یافت",
    pb2.EVENT_CAMERA_OFFLINE: "دوربین از دسترس خارج شد",
    pb2.EVENT_CAMERA_ONLINE: "دوربین به شبکه بازگشت",
    pb2.EVENT_STORAGE_WARNING: "هشدار ظرفیت ذخیره‌سازی",
    pb2.EVENT_EDGE_GAP_STITCHED: "بازیابی آرشیو از حافظه لبه (ONVIF Profile G) تکمیل شد",
    pb2.EVENT_AI_FACE_MATCH: "تطبیق چهره توسط موتور هوش مصنوعی",
}

_EVENT_SEVERITY = {
    pb2.EVENT_CAMERA_OFFLINE: "warning",
    pb2.EVENT_STORAGE_WARNING: "critical",
    pb2.EVENT_AI_FACE_MATCH: "warning",
}

_PROFILE_BY_NAME = {
    "main": pb2.STREAM_PROFILE_MAIN,
    "mid": pb2.STREAM_PROFILE_MID,
    "sub": pb2.STREAM_PROFILE_SUB,
}


def _camera_to_config(camera) -> pb2.CameraConfigRequest:
    """Map a Camera row onto the CameraConfigRequest wire message."""
    streams = []
    for name, url in (
        ("main", camera.rtsp_main_url),
        ("mid", camera.rtsp_mid_url),
        ("sub", camera.rtsp_sub_url),
    ):
        if not url:
            continue
        streams.append(
            pb2.RtspEndpoint(
                url=url,
                profile=_PROFILE_BY_NAME[name],
                codec=camera.codec,
            )
        )
    return pb2.CameraConfigRequest(
        camera_uuid=str(camera.uuid),
        action=pb2.CAMERA_ACTION_ADD,
        onvif_endpoint=camera.onvif_endpoint,
        streams=streams,
        recording_enabled=camera.recording_enabled,
        retention_days=camera.retention_days,
        onvif_motion_events=camera.onvif_motion_events,
        config_revision=str(camera.config_revision),
    )


class ControlPlaneEventsServicer(pb2_grpc.ControlPlaneEventsServicer):
    """Django-side implementation of vms.control.ControlPlaneEvents."""

    # -- RegisterServer -------------------------------------------------------
    def RegisterServer(self, request, context):
        from apps.cameras.models import Camera, RecordingServer

        close_old_connections()

        # The gRPC control port every C++ server listens on (host from peer).
        peer_host = _peer_host(context) or request.hostname
        control_port = getattr(settings, "RECORDING_SERVER_CONTROL_PORT", 50051)

        server, created = RecordingServer.objects.update_or_create(
            uuid=request.server_uuid,
            defaults={
                "hostname": request.hostname,
                "grpc_endpoint": f"{peer_host}:{control_port}",
                "gpu_available": request.gpu_available,
                "simd_capabilities": list(request.simd_capabilities),
                "is_online": True,
                "last_heartbeat": timezone.now(),  # UTC
            },
        )
        logger.info(
            "[grpc] server %s uuid=%s registered (created=%s gpu=%s simd=%s)",
            request.hostname, request.server_uuid, created,
            request.gpu_available, list(request.simd_capabilities),
        )

        _audit(
            action="server.register",
            target=f"recording_server:{server.uuid}",
            message_fa=f"سرور ضبط «{server.hostname}» به سامانه متصل و ثبت شد.",
            payload={
                "version": request.version,
                "gpu_available": request.gpu_available,
                "simd_capabilities": list(request.simd_capabilities),
                "created": created,
            },
        )

        cameras = (
            Camera.objects.filter(recording_server=server)
            .select_related("recording_server")
            .order_by("id")
        )
        return pb2.AssignedCameraSet(
            cameras=[_camera_to_config(cam) for cam in cameras]
        )

    # -- ReportEvent (client-streaming) ---------------------------------------
    def ReportEvent(self, request_iterator, context):
        from apps.cameras.models import RecordingServer

        close_old_connections()
        count = 0
        for event in request_iterator:
            try:
                self._persist_event(event)
                count += 1
            except Exception:  # never kill the stream over one bad event
                logger.exception(
                    "[grpc] failed to persist event type=%s camera=%s",
                    event.type, event.camera_uuid,
                )
            # Every event doubles as a liveness heartbeat.
            RecordingServer.objects.filter(uuid=event.server_uuid).update(
                is_online=True, last_heartbeat=timezone.now()
            )

        return pb2.ControlAck(
            ok=True,
            message_fa=f"{count} رویداد با موفقیت ثبت شد.",
        )

    # -- helpers ---------------------------------------------------------------
    @staticmethod
    def _persist_event(event) -> None:
        from apps.audit.models import AuditLog

        payload = {}
        if event.payload_json:
            try:
                payload = json.loads(event.payload_json)
            except json.JSONDecodeError:
                payload = {"raw": event.payload_json}

        event_utc = datetime.fromtimestamp(
            event.utc_epoch_ms / 1000.0, tz=dt_timezone.utc
        )

        AuditLog.objects.create(
            actor=None,  # machine-generated (data plane)
            action=f"server.event.{pb2.EventType.Name(event.type).lower()}",
            target=f"camera:{event.camera_uuid}" if event.camera_uuid
                   else f"recording_server:{event.server_uuid}",
            severity=_EVENT_SEVERITY.get(event.type, "info"),
            message_fa=_EVENT_MESSAGES_FA.get(event.type, "رویداد سرور ضبط"),
            payload={
                "server_uuid": event.server_uuid,
                "camera_uuid": event.camera_uuid,
                "utc_epoch_ms": event.utc_epoch_ms,
                "event_utc": event_utc.isoformat(),
                **payload,
            },
        )

        if event.type == pb2.EVENT_AI_FACE_MATCH:
            ControlPlaneEventsServicer._persist_face_match(event, payload, event_utc)

    @staticmethod
    def _persist_face_match(event, payload: dict, event_utc: datetime) -> None:
        """AI face-match events also land in the forensic detection table so
        they are reachable through Smart Forensic Search."""
        from apps.cameras.models import Camera
        from apps.forensic import services as forensic_services
        from apps.forensic.models import DetectionRecord

        try:
            camera = Camera.objects.get(uuid=event.camera_uuid)
        except Camera.DoesNotExist:
            logger.warning(
                "[grpc] face match references unknown camera=%s", event.camera_uuid
            )
            return

        embedding = payload.get("embedding")
        record = DetectionRecord.objects.create(
            camera=camera,
            utc_timestamp=event_utc,
            label=payload.get("label", "person"),
            attributes=payload.get("attributes", {}),
            similarity=payload.get("similarity"),
            embedding=embedding,
            bbox=payload.get("bbox", {}),
        )

        # Watchlist matching (ArcFace cosine) if the AI engine did not
        # pre-resolve the identity.
        if record.matched_identity_id is None and embedding:
            identity, similarity = forensic_services.match_identity(embedding)
            if identity is not None:
                record.matched_identity = identity
                record.similarity = similarity
                record.save(update_fields=["matched_identity", "similarity"])


def _peer_host(context) -> str | None:
    """Extract the caller IP from a gRPC peer string like 'ipv4:1.2.3.4:5555'."""
    peer = context.peer() or ""
    if peer.startswith("ipv4:"):
        return peer[len("ipv4:"):].rsplit(":", 1)[0]
    if peer.startswith("ipv6:"):
        host = peer[len("ipv6:"):].rsplit(":", 1)[0]
        return host.strip("[]")
    return None


def _audit(*, action: str, target: str, message_fa: str, payload: dict) -> None:
    from apps.audit.models import AuditLog

    AuditLog.objects.create(
        actor=None,
        action=action,
        target=target,
        severity="info",
        message_fa=message_fa,
        payload=payload,
    )


def serve(port: int | None = None, max_workers: int = 16) -> grpc.Server:
    """Build and start the ControlPlaneEvents gRPC server (non-blocking)."""
    port = port or int(getattr(settings, "CONTROL_PLANE_GRPC_PORT", 50060))
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=max_workers),
        options=[
            ("grpc.keepalive_time_ms", 30_000),
            ("grpc.keepalive_permit_without_calls", 1),
            ("grpc.max_receive_message_length", 8 * 1024 * 1024),
        ],
    )
    pb2_grpc.add_ControlPlaneEventsServicer_to_server(
        ControlPlaneEventsServicer(), server
    )
    server.add_insecure_port(f"[::]:{port}")  # mTLS termination in production
    server.start()
    logger.info("[grpc] ControlPlaneEvents serving on :%d", port)
    return server
