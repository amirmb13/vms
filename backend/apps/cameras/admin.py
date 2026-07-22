"""Django admin — cameras: inventory, recording servers, groups, ROI grids."""
from django.contrib import admin
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .models import Camera, CameraGroup, MotionRoiGrid, RecordingServer


@admin.register(RecordingServer)
class RecordingServerAdmin(admin.ModelAdmin):
    list_display = (
        "hostname",
        "grpc_endpoint",
        "gpu_available",
        "is_online",
        "heartbeat_shamsi",
    )
    list_filter = ("is_online", "gpu_available")
    search_fields = ("hostname", "grpc_endpoint")
    readonly_fields = ("uuid", "last_heartbeat")

    @admin.display(description=_("آخرین ضربان (شمسی)"), ordering="last_heartbeat")
    def heartbeat_shamsi(self, obj):
        return utc_to_shamsi(obj.last_heartbeat) if obj.last_heartbeat else "—"


@admin.register(CameraGroup)
class CameraGroupAdmin(admin.ModelAdmin):
    list_display = ("name_fa", "parent", "camera_count")
    search_fields = ("name_fa",)
    autocomplete_fields = ("parent",)

    @admin.display(description=_("تعداد دوربین"))
    def camera_count(self, obj):
        return obj.cameras.count()


class MotionRoiGridInline(admin.StackedInline):
    model = MotionRoiGrid
    extra = 0
    fields = ("grid_cols", "grid_rows", "sensitivity")


@admin.register(Camera)
class CameraAdmin(admin.ModelAdmin):
    list_display = (
        "name_fa",
        "group",
        "recording_server",
        "codec",
        "recording_enabled",
        "retention_days",
        "config_revision",
        "updated_shamsi",
    )
    list_filter = ("recording_enabled", "codec", "recording_server", "group")
    search_fields = ("name_fa", "uuid", "onvif_endpoint")
    readonly_fields = ("uuid", "config_revision", "created_at", "updated_at")
    autocomplete_fields = ("group", "recording_server")
    inlines = [MotionRoiGridInline]

    @admin.display(description=_("آخرین بروزرسانی (شمسی)"), ordering="updated_at")
    def updated_shamsi(self, obj):
        return utc_to_shamsi(obj.updated_at) if obj.updated_at else "—"


@admin.register(MotionRoiGrid)
class MotionRoiGridAdmin(admin.ModelAdmin):
    list_display = ("camera", "grid_cols", "grid_rows", "sensitivity")
    search_fields = ("camera__name_fa",)
    autocomplete_fields = ("camera",)
