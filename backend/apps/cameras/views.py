"""Camera inventory API. Every mutation bumps config_revision and pushes a
control signal to the responsible C++ Recording Server (no service restarts)."""
from django.db.models import F
from django.utils import timezone
from rest_framework import status, viewsets
from rest_framework.decorators import action
from rest_framework.permissions import AllowAny
from rest_framework.response import Response

from apps.audit import services as audit
from apps.common.permissions import ReadOnlyOrManageCameras
from apps.orchestrator import publisher

from .models import Camera, CameraGroup, MotionRoiGrid, RecordingServer
from .serializers import (
    CameraGroupSerializer,
    CameraSerializer,
    CameraStreamSerializer,
    MotionRoiGridSerializer,
    RecordingServerSerializer,
)


class RecordingServerViewSet(viewsets.ModelViewSet):
    queryset = RecordingServer.objects.all().order_by("hostname")
    serializer_class = RecordingServerSerializer
    permission_classes = [ReadOnlyOrManageCameras]
    lookup_field = "uuid"

    @action(detail=True, methods=["post"], permission_classes=[AllowAny])
    def heartbeat(self, request, uuid=None):
        """Called by the C++ server every N seconds (mTLS in production)."""
        server = self.get_object()
        server.is_online = True
        server.last_heartbeat = timezone.now()
        server.gpu_available = bool(request.data.get("gpu_available", server.gpu_available))
        caps = request.data.get("simd_capabilities")
        if isinstance(caps, list):
            server.simd_capabilities = caps
        server.save(update_fields=[
            "is_online", "last_heartbeat", "gpu_available", "simd_capabilities"
        ])
        return Response({"detail_fa": "ضربان دریافت شد."})


class CameraGroupViewSet(viewsets.ModelViewSet):
    serializer_class = CameraGroupSerializer
    permission_classes = [ReadOnlyOrManageCameras]

    def get_queryset(self):
        qs = CameraGroup.objects.prefetch_related("children", "cameras")
        # ?tree=1 -> only roots (children are nested recursively)
        if self.action == "list" and self.request.query_params.get("tree"):
            qs = qs.filter(parent__isnull=True)
        return qs


class CameraViewSet(viewsets.ModelViewSet):
    queryset = Camera.objects.select_related("group", "recording_server")
    serializer_class = CameraSerializer
    permission_classes = [ReadOnlyOrManageCameras]
    lookup_field = "uuid"
    filterset_fields = ["group", "recording_server", "recording_enabled", "codec"]

    def perform_create(self, serializer):
        camera = serializer.save()
        publisher.publish_camera_config(camera)
        audit.record(
            self.request, "camera.create",
            f"دوربین «{camera.name_fa}» ایجاد شد.", target=str(camera.uuid),
        )

    def perform_update(self, serializer):
        camera = serializer.save()
        # Monotonic revision bump, race-safe.
        Camera.objects.filter(pk=camera.pk).update(
            config_revision=F("config_revision") + 1
        )
        camera.refresh_from_db(fields=["config_revision"])
        publisher.publish_camera_config(camera)
        audit.record(
            self.request, "camera.update",
            f"پیکربندی دوربین «{camera.name_fa}» به‌روزرسانی شد "
            f"(نسخه {camera.config_revision}).",
            target=str(camera.uuid),
        )

    def perform_destroy(self, instance):
        name, cam_uuid = instance.name_fa, str(instance.uuid)
        instance.delete()
        audit.record(
            self.request, "camera.delete",
            f"دوربین «{name}» حذف شد.", target=cam_uuid,
            severity="warning",
        )

    @action(detail=True, methods=["get"], url_path="stream-info")
    def stream_info(self, request, uuid=None):
        """Relay endpoint info for the Qt client (never raw camera URLs)."""
        return Response(CameraStreamSerializer(self.get_object()).data)

    @action(detail=True, methods=["get", "put"], url_path="roi")
    def roi(self, request, uuid=None):
        """Motion ROI bitmask grid, consumed by the C++ SIMD engine."""
        camera = self.get_object()
        if request.method == "GET":
            roi = getattr(camera, "roi", None)
            if roi is None:
                return Response(
                    {"detail_fa": "برای این دوربین ناحیه حساس تعریف نشده است."},
                    status=status.HTTP_404_NOT_FOUND,
                )
            return Response(MotionRoiGridSerializer(roi).data)

        roi = getattr(camera, "roi", None)
        data = {**request.data, "camera": camera.pk}
        serializer = MotionRoiGridSerializer(instance=roi, data=data)
        serializer.is_valid(raise_exception=True)
        roi = serializer.save(camera=camera)
        publisher.publish_roi_update(roi)
        audit.record(
            request, "camera.roi_update",
            f"ناحیه حساس حرکتی دوربین «{camera.name_fa}» به‌روزرسانی شد.",
            target=str(camera.uuid),
        )
        return Response(MotionRoiGridSerializer(roi).data)
