"""Camera inventory API. Every mutation bumps config_revision and pushes a
control signal to the responsible C++ Recording Server (no service restarts)."""
from django.db.models import Count, F
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
    CameraListSerializer,
    CameraSerializer,
    CameraStreamSerializer,
    MotionRoiGridSerializer,
    RecordingServerSerializer,
)


class RecordingServerViewSet(viewsets.ModelViewSet):
    queryset = (
        RecordingServer.objects.all()
        .annotate(camera_count_annotated=Count("cameras"))
        .order_by("hostname")
    )
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
        return CameraGroup.objects.annotate(
            camera_count_annotated=Count("cameras")
        ).order_by("id")

    def list(self, request, *args, **kwargs):
        # ?tree=1 -> full nested tree assembled from ONE query. The naive
        # recursive serializer issued O(groups) child/count queries, which
        # collapses with thousands of groups.
        if request.query_params.get("tree"):
            groups = list(self.get_queryset())
            by_id = {}
            for group in groups:
                group._tree_children = []
                by_id[group.pk] = group
            roots = []
            for group in groups:
                parent = by_id.get(group.parent_id)
                if parent is not None:
                    parent._tree_children.append(group)
                else:
                    roots.append(group)
            return Response(CameraGroupSerializer(roots, many=True).data)
        return super().list(request, *args, **kwargs)


class CameraViewSet(viewsets.ModelViewSet):
    queryset = Camera.objects.select_related("group", "recording_server")
    serializer_class = CameraSerializer
    permission_classes = [ReadOnlyOrManageCameras]
    lookup_field = "uuid"
    filterset_fields = ["group", "recording_server", "recording_enabled", "codec"]

    def get_serializer_class(self):
        # LIST at 10k cameras must stay slim; detail/mutations keep the full
        # serializer (URLs, ONVIF endpoint, GIS fields, ...).
        if self.action == "list":
            return CameraListSerializer
        return CameraSerializer

    def get_queryset(self):
        qs = Camera.objects.all()
        if self.action == "list":
            # Slim serializer touches no relations — skip the JOINs entirely.
            return qs.only(
                "id", "uuid", "name_fa", "group_id",
                "recording_enabled", "codec", "config_revision",
            )
        return qs.select_related("group", "recording_server")

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
