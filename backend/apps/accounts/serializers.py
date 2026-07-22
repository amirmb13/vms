from rest_framework import serializers

from apps.common.shamsi import utc_to_shamsi

from .models import FourEyesRequest, Role, RoleAssignment, User


class RoleSerializer(serializers.ModelSerializer):
    class Meta:
        model = Role
        fields = [
            "id", "name_fa", "can_view_live", "can_view_playback",
            "can_export_video", "can_control_ptz", "can_manage_cameras",
            "can_manage_users", "can_approve_critical",
        ]


class RoleAssignmentSerializer(serializers.ModelSerializer):
    role_name_fa = serializers.CharField(source="role.name_fa", read_only=True)
    group_name_fa = serializers.CharField(
        source="camera_group.name_fa", read_only=True, default=None
    )

    class Meta:
        model = RoleAssignment
        fields = ["id", "user", "role", "role_name_fa", "camera_group", "group_name_fa"]


class UserSerializer(serializers.ModelSerializer):
    assignments = RoleAssignmentSerializer(many=True, read_only=True)
    password = serializers.CharField(write_only=True, required=False)

    class Meta:
        model = User
        fields = [
            "uuid", "username", "full_name_fa", "email", "phone",
            "is_active", "is_ad_account", "assignments", "password",
        ]
        read_only_fields = ["uuid", "is_ad_account"]

    def create(self, validated_data):
        password = validated_data.pop("password", None)
        user = User(**validated_data)
        if password:
            user.set_password(password)
        else:
            user.set_unusable_password()
        user.save()
        return user

    def update(self, instance, validated_data):
        password = validated_data.pop("password", None)
        user = super().update(instance, validated_data)
        if password:
            user.set_password(password)
            user.save(update_fields=["password"])
        return user


class FourEyesRequestSerializer(serializers.ModelSerializer):
    requested_by_name = serializers.CharField(
        source="requested_by.full_name_fa", read_only=True
    )
    status_fa = serializers.CharField(source="get_status_display", read_only=True)
    created_at_shamsi = serializers.SerializerMethodField()

    class Meta:
        model = FourEyesRequest
        fields = [
            "uuid", "operation", "payload", "requested_by", "requested_by_name",
            "approver_a", "approver_b", "status", "status_fa",
            "created_at", "created_at_shamsi", "executed_at",
        ]
        read_only_fields = [
            "uuid", "requested_by", "approver_a", "approver_b",
            "status", "created_at", "executed_at",
        ]

    def get_created_at_shamsi(self, obj):
        return utc_to_shamsi(obj.created_at)

    def validate_operation(self, value):
        from django.conf import settings

        if value not in settings.FOUR_EYES_PROTECTED_OPERATIONS:
            raise serializers.ValidationError(
                "این عملیات در فهرست عملیات حساس (چهارچشمی) تعریف نشده است."
            )
        return value


class ApprovalSerializer(serializers.Serializer):
    """Detached cryptographic signature over the request payload digest."""

    signature = serializers.CharField(min_length=16)
