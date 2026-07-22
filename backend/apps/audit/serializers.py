from rest_framework import serializers

from apps.common.shamsi import utc_to_shamsi

from .models import AuditLog


class AuditLogSerializer(serializers.ModelSerializer):
    actor_username = serializers.CharField(
        source="actor.username", read_only=True, default=None
    )
    severity_fa = serializers.CharField(source="get_severity_display", read_only=True)
    created_at_shamsi = serializers.SerializerMethodField()

    class Meta:
        model = AuditLog
        fields = [
            "id", "actor", "actor_username", "action", "target",
            "severity", "severity_fa", "message_fa", "payload",
            "source_ip", "created_at", "created_at_shamsi",
        ]

    def get_created_at_shamsi(self, obj):
        return utc_to_shamsi(obj.created_at)
