from rest_framework import serializers

from apps.common.shamsi import utc_to_shamsi

from .models import DetectionRecord, Identity, SynopsisJob


class IdentitySerializer(serializers.ModelSerializer):
    class Meta:
        model = Identity
        fields = ["id", "name_fa", "national_id", "is_watchlisted", "embedding",
                  "created_at"]
        read_only_fields = ["created_at"]
        extra_kwargs = {"embedding": {"write_only": True}}

    def validate_embedding(self, value):
        if not isinstance(value, list) or len(value) != 512:
            raise serializers.ValidationError(
                "بردار چهره باید دقیقاً ۵۱۲ مقدار عددی (ArcFace) داشته باشد."
            )
        return value


class DetectionRecordSerializer(serializers.ModelSerializer):
    camera_name_fa = serializers.CharField(source="camera.name_fa", read_only=True)
    identity_name_fa = serializers.CharField(
        source="matched_identity.name_fa", read_only=True, default=None
    )
    timestamp_shamsi = serializers.SerializerMethodField()

    # Write-only: 512-d ArcFace embedding relayed by the C++ server. Persisted
    # for later enrollment / re-identification, never echoed back in reads.
    embedding = serializers.ListField(
        child=serializers.FloatField(), required=False, write_only=True,
        min_length=512, max_length=512,
    )

    class Meta:
        model = DetectionRecord
        fields = [
            "id", "camera", "camera_name_fa", "utc_timestamp", "timestamp_shamsi",
            "label", "attributes", "matched_identity", "identity_name_fa",
            "similarity", "bbox", "embedding",
        ]

    def get_timestamp_shamsi(self, obj):
        return utc_to_shamsi(obj.utc_timestamp)


class SynopsisJobSerializer(serializers.ModelSerializer):
    camera_name_fa = serializers.CharField(source="camera.name_fa", read_only=True)
    state_fa = serializers.CharField(source="get_state_display", read_only=True)
    from_shamsi = serializers.SerializerMethodField()
    to_shamsi = serializers.SerializerMethodField()

    class Meta:
        model = SynopsisJob
        fields = [
            "uuid", "camera", "camera_name_fa", "from_utc", "to_utc",
            "from_shamsi", "to_shamsi", "target_duration_s", "state",
            "state_fa", "progress_percent", "result_manifest", "error_fa",
            "created_at",
        ]
        read_only_fields = [
            "uuid", "state", "progress_percent", "result_manifest",
            "error_fa", "created_at",
        ]

    def get_from_shamsi(self, obj):
        return utc_to_shamsi(obj.from_utc)

    def get_to_shamsi(self, obj):
        return utc_to_shamsi(obj.to_utc)

    def validate(self, data):
        if data["to_utc"] <= data["from_utc"]:
            raise serializers.ValidationError(
                "پایان بازه باید بعد از شروع بازه باشد."
            )
        window = data["to_utc"] - data["from_utc"]
        if window.total_seconds() > 24 * 3600:
            raise serializers.ValidationError(
                "بازه خلاصه‌سازی حداکثر می‌تواند ۲۴ ساعت باشد."
            )
        return data


class SynopsisProgressSerializer(serializers.Serializer):
    """Internal write-back from the C++ TimeCompressor worker."""

    state = serializers.ChoiceField(choices=SynopsisJob.State.choices)
    progress_percent = serializers.IntegerField(min_value=0, max_value=100)
    result_manifest = serializers.JSONField(required=False)
    error_fa = serializers.CharField(required=False, allow_blank=True)


class ForensicSearchSerializer(serializers.Serializer):
    """Structured forensic query resolved to archive timestamps."""

    camera_uuids = serializers.ListField(
        child=serializers.UUIDField(), required=False, allow_empty=True
    )
    group_id = serializers.IntegerField(required=False)
    label = serializers.CharField(required=False)          # "person", "car"
    attributes = serializers.DictField(required=False)     # {"shirt_color": "red"}
    identity_id = serializers.IntegerField(required=False)
    from_utc = serializers.DateTimeField(required=False)
    to_utc = serializers.DateTimeField(required=False)
    limit = serializers.IntegerField(default=100, max_value=1000, min_value=1)
