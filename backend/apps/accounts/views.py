"""Users, roles, and the Four-Eyes Authorization workflow."""
from django.utils import timezone
from rest_framework import status, viewsets
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response

from apps.audit import services as audit
from apps.common.permissions import CanApproveCritical, CanManageUsers
from apps.orchestrator import publisher

from .models import FourEyesRequest, Role, RoleAssignment, User
from .serializers import (
    ApprovalSerializer,
    FourEyesRequestSerializer,
    RoleAssignmentSerializer,
    RoleSerializer,
    UserSerializer,
)


class UserViewSet(viewsets.ModelViewSet):
    queryset = User.objects.prefetch_related("assignments__role")
    serializer_class = UserSerializer
    permission_classes = [CanManageUsers]
    lookup_field = "uuid"

    @action(detail=False, methods=["get"], permission_classes=[IsAuthenticated])
    def me(self, request):
        """Profile + effective permissions for the logged-in Qt client user."""
        return Response(UserSerializer(request.user).data)


class RoleViewSet(viewsets.ModelViewSet):
    queryset = Role.objects.all()
    serializer_class = RoleSerializer
    permission_classes = [CanManageUsers]


class RoleAssignmentViewSet(viewsets.ModelViewSet):
    queryset = RoleAssignment.objects.select_related("role", "camera_group")
    serializer_class = RoleAssignmentSerializer
    permission_classes = [CanManageUsers]


class FourEyesRequestViewSet(viewsets.ModelViewSet):
    """
    Lifecycle: PENDING -> (approve x2 by distinct admins) -> APPROVED
    -> orchestrator pushes gRPC to C++ -> EXECUTED.
    """

    queryset = FourEyesRequest.objects.select_related("requested_by")
    serializer_class = FourEyesRequestSerializer
    permission_classes = [IsAuthenticated]
    lookup_field = "uuid"
    http_method_names = ["get", "post", "head", "options"]

    def perform_create(self, serializer):
        req = serializer.save(requested_by=self.request.user)
        audit.record(
            self.request, "foureyes.create",
            f"درخواست عملیات حساس «{req.operation}» ثبت شد و در انتظار تأیید است.",
            target=str(req.uuid), severity="warning",
        )

    @action(detail=True, methods=["post"], permission_classes=[CanApproveCritical])
    def approve(self, request, uuid=None):
        req = self.get_object()
        if req.status != FourEyesRequest.Status.PENDING:
            return Response(
                {"detail_fa": "این درخواست در وضعیت انتظار نیست."},
                status=status.HTTP_409_CONFLICT,
            )
        ser = ApprovalSerializer(data=request.data)
        ser.is_valid(raise_exception=True)
        signature = ser.validated_data["signature"]

        if req.requested_by_id == request.user.id:
            return Response(
                {"detail_fa": "درخواست‌کننده نمی‌تواند خودش تأییدکننده باشد."},
                status=status.HTTP_403_FORBIDDEN,
            )
        if request.user.id in (req.approver_a_id, req.approver_b_id):
            return Response(
                {"detail_fa": "شما قبلاً این درخواست را تأیید کرده‌اید؛ "
                              "تأیید دوم باید توسط مدیر دیگری انجام شود."},
                status=status.HTTP_409_CONFLICT,
            )

        if req.approver_a_id is None:
            req.approver_a, req.signature_a = request.user, signature
            req.save(update_fields=["approver_a", "signature_a"])
            audit.record(
                request, "foureyes.approve_first",
                f"تأیید اول درخواست «{req.operation}» ثبت شد.",
                target=str(req.uuid), severity="warning",
            )
            return Response({"detail_fa": "تأیید اول ثبت شد؛ در انتظار مدیر دوم."})

        req.approver_b, req.signature_b = request.user, signature
        req.status = FourEyesRequest.Status.APPROVED
        req.save(update_fields=["approver_b", "signature_b", "status"])

        executed = False
        if req.operation == "archive.prune":
            executed = publisher.push_prune_grpc(req)
        if executed:
            req.status = FourEyesRequest.Status.EXECUTED
            req.executed_at = timezone.now()
            req.save(update_fields=["status", "executed_at"])

        audit.record(
            request, "foureyes.approve_second",
            f"تأیید دوم ثبت شد؛ عملیات «{req.operation}» "
            + ("اجرا شد." if executed else "آماده اجرا است."),
            target=str(req.uuid), severity="critical",
        )
        return Response(FourEyesRequestSerializer(req).data)

    @action(detail=True, methods=["post"], permission_classes=[CanApproveCritical])
    def reject(self, request, uuid=None):
        req = self.get_object()
        if req.status != FourEyesRequest.Status.PENDING:
            return Response(
                {"detail_fa": "این درخواست در وضعیت انتظار نیست."},
                status=status.HTTP_409_CONFLICT,
            )
        req.status = FourEyesRequest.Status.REJECTED
        req.save(update_fields=["status"])
        audit.record(
            request, "foureyes.reject",
            f"درخواست عملیات «{req.operation}» رد شد.",
            target=str(req.uuid), severity="warning",
        )
        return Response({"detail_fa": "درخواست رد شد."})
