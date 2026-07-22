from rest_framework import serializers

from apps.common.shamsi import utc_to_shamsi

from .models import GridLayout, validate_layout_json


class GridLayoutSerializer(serializers.ModelSerializer):
    owner_username = serializers.CharField(source="owner.username", read_only=True)
    updated_at_shamsi = serializers.SerializerMethodField()

    class Meta:
        model = GridLayout
        fields = [
            "uuid", "name_fa", "owner", "owner_username", "is_shared",
            "layout_json", "updated_at", "updated_at_shamsi",
        ]
        read_only_fields = ["uuid", "owner", "updated_at"]

    def get_updated_at_shamsi(self, obj):
        return utc_to_shamsi(obj.updated_at)

    def validate_layout_json(self, value):
        from django.core.exceptions import ValidationError as DjangoValidationError

        try:
            validate_layout_json(value)
        except DjangoValidationError as exc:
            raise serializers.ValidationError(exc.messages)
        return value
