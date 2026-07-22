"""Read-only audit trail API (logs are immutable by design)."""
from rest_framework import viewsets

from apps.common.permissions import CanManageUsers

from .models import AuditLog
from .serializers import AuditLogSerializer


class AuditLogViewSet(viewsets.ReadOnlyModelViewSet):
    queryset = AuditLog.objects.select_related("actor")
    serializer_class = AuditLogSerializer
    permission_classes = [CanManageUsers]
    filterset_fields = ["action", "severity", "actor"]
