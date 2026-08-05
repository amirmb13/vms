"""DRF serializers for the camera inventory. Shamsi timestamps at the edges."""
import base64

from rest_framework import serializers

from apps.common.shamsi import utc_to_shamsi

from .models import Camera, CameraGroup, MotionRoiGrid, RecordingServer


class RecordingServerSerializer(serializers.ModelSerializer):
    last_heartbeat_shamsi = serializers.SerializerMethodField()
    camera_count = serializers.SerializerMethodField()

    class Meta:
        model = RecordingServer
        fields = [
            "uuid", "hostname", "grpc_endpoint", "storage_path", "gpu_available",
            "simd_capabilities", "is_online", "last_heartbeat",
            "last_heartbeat_shamsi", "camera_count",
        ]
        read_only_fields = ["uuid", "is_online", "last_heartbeat"]

    def get_last_heartbeat_shamsi(self, obj):
        return utc_to_shamsi(obj.last_heartbeat) if obj.last_heartbeat else None

    def get_camera_count(self, obj):
        # Prefer the queryset annotation (single aggregated query for the
        # whole list) over per-row COUNT(*) round-trips.
        annotated = getattr(obj, "camera_count_annotated", None)
        return annotated if annotated is not None else obj.cameras.count()


class CameraGroupSerializer(serializers.ModelSerializer):
    """Recursive directory tree for the Qt client's RTL tree view.

    Scale note: naive recursion (`obj.children.all()` + `cameras.count` per
    node) costs O(groups) queries. The tree endpoint pre-links children in
    memory (`_tree_children`) and annotates counts, so serializing thousands
    of groups touches the database exactly once.
    """

    children = serializers.SerializerMethodField()
    camera_count = serializers.SerializerMethodField()

    class Meta:
        model = CameraGroup
        fields = ["id", "name_fa", "parent", "children", "camera_count"]

    def get_children(self, obj):
        prelinked = getattr(obj, "_tree_children", None)
        children = prelinked if prelinked is not None else obj.children.all()
        return CameraGroupSerializer(children, many=True).data

    def get_camera_count(self, obj):
        annotated = getattr(obj, "camera_count_annotated", None)
        return annotated if annotated is not None else obj.cameras.count()


class MotionRoiGridSerializer(serializers.ModelSerializer):
    """Bitmask travels base64 in JSON; raw bytes over gRPC to the C++ engine."""

    bitmask_b64 = serializers.CharField(write_only=False, required=True)

    class Meta:
        model = MotionRoiGrid
        fields = ["camera", "grid_cols", "grid_rows", "bitmask_b64", "sensitivity"]

    def to_representation(self, instance):
        rep = super().to_representation(instance)
        rep["bitmask_b64"] = base64.b64encode(bytes(instance.bitmask)).decode()
        return rep

    def validate(self, attrs):
        b64 = attrs.pop("bitmask_b64", None)
        if b64 is not None:
            try:
                raw = base64.b64decode(b64, validate=True)
            except Exception:
                raise serializers.ValidationError(
                    {"bitmask_b64": "بیت‌مسک نامعتبر است (base64 صحیح نیست)."}
                )
            cols = attrs.get("grid_cols", getattr(self.instance, "grid_cols", 32))
            rows = attrs.get("grid_rows", getattr(self.instance, "grid_rows", 24))
            expected = (cols * rows + 7) // 8
            if len(raw) != expected:
                raise serializers.ValidationError(
                    {"bitmask_b64": f"طول بیت‌مسک باید {expected} بایت باشد."}
                )
            attrs["bitmask"] = raw
        return attrs


class CameraSerializer(serializers.ModelSerializer):
    group_name_fa = serializers.CharField(source="group.name_fa", read_only=True)
    server_hostname = serializers.CharField(
        source="recording_server.hostname", read_only=True
    )
    created_at_shamsi = serializers.SerializerMethodField()
    password = serializers.CharField(write_only=True, required=False, allow_blank=True)

    class Meta:
        model = Camera
        fields = [
            "uuid", "name_fa", "group", "group_name_fa",
            "recording_server", "server_hostname",
            "onvif_endpoint", "username", "password",
            "rtsp_main_url", "rtsp_mid_url", "rtsp_sub_url", "codec",
            "recording_enabled", "retention_days",
            "onvif_motion_events", "edge_storage_profile_g",
            "map_latitude", "map_longitude", "fov_direction_deg", "fov_angle_deg",
            "config_revision", "created_at", "created_at_shamsi",
        ]
        read_only_fields = ["uuid", "config_revision", "created_at"]

    def get_created_at_shamsi(self, obj):
        return utc_to_shamsi(obj.created_at)

    def create(self, validated_data):
        password = validated_data.pop("password", "")
        # NOTE: production must encrypt via KMS / fernet before persisting.
        validated_data["password_encrypted"] = password
        return super().create(validated_data)

    def update(self, instance, validated_data):
        password = validated_data.pop("password", None)
        if password:
            validated_data["password_encrypted"] = password
        return super().update(instance, validated_data)


class CameraListSerializer(serializers.ModelSerializer):
    """Slim payload for the inventory LIST endpoint (10k+ rows).

    The Qt camera tree only needs identity + placement + recording state.
    Shipping the full serializer for every row multiplies the response size
    ~8x and leaks operational fields (RTSP URLs, ONVIF endpoints, usernames)
    to every operator role. Detail view keeps the full serializer.
    """

    class Meta:
        model = Camera
        fields = [
            "id", "uuid", "name_fa", "group",
            "recording_enabled", "codec", "config_revision",
        ]


class CameraStreamSerializer(serializers.ModelSerializer):
    """Minimal payload the Qt client needs to request streams FROM THE RELAY.

    Clients never receive raw camera RTSP URLs — only the relay endpoint of
    the responsible recording server plus the camera UUID (Rule #3).
    """

    relay_endpoint = serializers.CharField(
        source="recording_server.grpc_endpoint", read_only=True
    )

    class Meta:
        model = Camera
        fields = ["uuid", "name_fa", "codec", "relay_endpoint"]
