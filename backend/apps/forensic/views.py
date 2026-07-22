"""Smart Forensic Search: JSONB attribute queries resolved to timestamps.

Also exposes the internal ingest endpoint the C++ server calls (relaying
InferenceResult payloads from the Python AI engine) to persist detections.
"""
from rest_framework import status, viewsets
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework.views import APIView

from apps.common.permissions import CanManageUsers, CanViewPlayback

from . import services
from .models import DetectionRecord, Identity, SynopsisJob
from .serializers import (
    DetectionRecordSerializer,
    ForensicSearchSerializer,
    IdentitySerializer,
    SynopsisJobSerializer,
    SynopsisProgressSerializer,
)


class IdentityViewSet(viewsets.ModelViewSet):
    queryset = Identity.objects.all()
    serializer_class = IdentitySerializer
    permission_classes = [CanManageUsers]
    filterset_fields = ["is_watchlisted"]


class ForensicSearchView(APIView):
    """POST /api/forensic/search — e.g. «مرد با پیراهن قرمز، درب شمالی، دیروز»"""

    permission_classes = [CanViewPlayback]

    def post(self, request):
        ser = ForensicSearchSerializer(data=request.data)
        ser.is_valid(raise_exception=True)
        q = ser.validated_data

        qs = DetectionRecord.objects.select_related("camera", "matched_identity")
        if q.get("camera_uuids"):
            qs = qs.filter(camera__uuid__in=q["camera_uuids"])
        if q.get("group_id"):
            qs = qs.filter(camera__group_id=q["group_id"])
        if q.get("label"):
            qs = qs.filter(label=q["label"])
        if q.get("identity_id"):
            qs = qs.filter(matched_identity_id=q["identity_id"])
        if q.get("from_utc"):
            qs = qs.filter(utc_timestamp__gte=q["from_utc"])
        if q.get("to_utc"):
            qs = qs.filter(utc_timestamp__lte=q["to_utc"])
        # PostgreSQL JSONB containment: attributes @> {"shirt_color": "red"}
        if q.get("attributes"):
            qs = qs.filter(attributes__contains=q["attributes"])

        results = qs.order_by("-utc_timestamp")[: q["limit"]]
        return Response({
            "count": len(results),
            "results": DetectionRecordSerializer(results, many=True).data,
        })


class SynopsisJobViewSet(viewsets.ModelViewSet):
    """Video Synopsis jobs: create -> orchestrator pushes SYNOPSIS_REQUEST to
    the responsible C++ recording server; the worker writes progress back via
    the `progress` action until the clickable-timestamp manifest lands."""

    queryset = SynopsisJob.objects.select_related("camera")
    serializer_class = SynopsisJobSerializer
    permission_classes = [CanViewPlayback]
    lookup_field = "uuid"
    http_method_names = ["get", "post", "delete", "head", "options"]
    filterset_fields = ["camera", "state"]

    def perform_create(self, serializer):
        from apps.orchestrator import publisher

        job = serializer.save(requested_by=self.request.user)
        publisher.publish_synopsis_request(job)

    def perform_destroy(self, instance):
        from apps.orchestrator import publisher

        publisher.publish_synopsis_cancel(instance)
        instance.delete()

    @action(detail=True, methods=["post"], permission_classes=[IsAuthenticated])
    def progress(self, request, uuid=None):
        """Internal write-back endpoint for the C++ TimeCompressor worker."""
        job = self.get_object()
        ser = SynopsisProgressSerializer(data=request.data)
        ser.is_valid(raise_exception=True)
        data = ser.validated_data

        job.state = data["state"]
        job.progress_percent = data["progress_percent"]
        if "result_manifest" in data:
            job.result_manifest = data["result_manifest"]
        if data.get("error_fa"):
            job.error_fa = data["error_fa"]
        job.save(update_fields=[
            "state", "progress_percent", "result_manifest", "error_fa",
            "updated_at",
        ])
        return Response({"detail_fa": "وضعیت خلاصه‌سازی به‌روزرسانی شد."})


class DetectionIngestView(APIView):
    """Internal endpoint for recording servers (mTLS / service token in prod)."""

    permission_classes = [IsAuthenticated]

    def post(self, request):
        many = isinstance(request.data, list)
        ser = DetectionRecordSerializer(data=request.data, many=many)
        ser.is_valid(raise_exception=True)
        saved = ser.save()
        records = saved if many else [saved]

        # Identity matching: face embeddings without a pre-matched identity
        # are compared against the enrolled watchlist (ArcFace cosine).
        matched = 0
        for record in records:
            if record.matched_identity_id is not None or not record.embedding:
                continue
            identity, similarity = services.match_identity(record.embedding)
            if identity is not None:
                record.matched_identity = identity
                record.similarity = similarity
                record.save(update_fields=["matched_identity", "similarity"])
                matched += 1

        return Response(
            {"detail_fa": "رکورد(های) تشخیص ثبت شد.", "matched_count": matched},
            status=status.HTTP_201_CREATED,
        )
