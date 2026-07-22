"""Grid layouts: users see their own + shared layouts; Qt applies them live."""
from django.db.models import Q
from rest_framework import viewsets
from rest_framework.permissions import IsAuthenticated

from .models import GridLayout
from .serializers import GridLayoutSerializer


class GridLayoutViewSet(viewsets.ModelViewSet):
    serializer_class = GridLayoutSerializer
    permission_classes = [IsAuthenticated]
    lookup_field = "uuid"

    def get_queryset(self):
        return GridLayout.objects.filter(
            Q(owner=self.request.user) | Q(is_shared=True)
        ).select_related("owner")

    def perform_create(self, serializer):
        serializer.save(owner=self.request.user)

    def perform_update(self, serializer):
        if serializer.instance.owner_id != self.request.user.id:
            from rest_framework.exceptions import PermissionDenied

            raise PermissionDenied("فقط مالک چیدمان می‌تواند آن را ویرایش کند.")
        serializer.save()
