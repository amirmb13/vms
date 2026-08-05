"""Django admin — cameras: inventory, recording servers, groups, ROI grids.

Reorganized around the operator's mental model: add a camera by name + IP +
credentials + brand; everything else is auto-detected or lives in collapsed
"advanced" fieldsets.
"""
from django.contrib import admin, messages
from django.http import JsonResponse
from django.urls import path
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .forms import CameraAdminForm, DETECT_EXPLANATION
from .models import Camera, CameraGroup, MotionRoiGrid, RecordingServer
from .services import auto_detect_camera


@admin.register(RecordingServer)
class RecordingServerAdmin(admin.ModelAdmin):
    """Recording servers register themselves with Django on boot (gRPC), so
    this page is mostly read-only monitoring — operators rarely add a server
    by hand."""

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

    fieldsets = (
        (_("شناسه سرور"), {
            "fields": ("hostname", "grpc_endpoint"),
            "description": (
                "سرورهای ضبط هنگام روشن شدن به صورت خودکار در سامانه ثبت می‌شوند؛ "
                "در حالت عادی نیازی به ثبت دستی ندارند."
            ),
        }),
        (_("سخت‌افزار"), {
            "fields": ("gpu_available", "simd_capabilities"),
            "classes": ("collapse",),
        }),
        (_("وضعیت"), {"fields": ("is_online", "last_heartbeat", "uuid")}),
    )

    @admin.display(description=_("آخرین ضربان (شمسی)"), ordering="last_heartbeat")
    def heartbeat_shamsi(self, obj):
        return utc_to_shamsi(obj.last_heartbeat) if obj.last_heartbeat else "—"


@admin.register(CameraGroup)
class CameraGroupAdmin(admin.ModelAdmin):
    list_display = ("name_fa", "parent", "camera_count")
    search_fields = ("name_fa",)
    autocomplete_fields = ("parent",)
    fieldsets = (
        (_("گروه / ناحیه"), {
            "fields": ("name_fa", "parent"),
            "description": (
                "گروه‌ها همان ناحیه‌ها یا زون‌های ساختمان‌ هستند "
                "(مثلاً «طبقه همکف»، «درب شمالی»)."
            ),
        }),
    )

    @admin.display(description=_("تعداد دوربین"))
    def camera_count(self, obj):
        return obj.cameras.count()


class MotionRoiGridInline(admin.StackedInline):
    model = MotionRoiGrid
    extra = 0
    fields = ("grid_cols", "grid_rows", "sensitivity")
    verbose_name = _("ناحیه حساس حرکت")
    verbose_name_plural = _("ناحیه حساس حرکت")


@admin.register(Camera)
class CameraAdmin(admin.ModelAdmin):
    form = CameraAdminForm
    list_display = (
        "name_fa",
        "ip_address",
        "brand",
        "group",
        "recording_server",
        "codec",
        "recording_enabled",
        "retention_days",
        "updated_shamsi",
    )
    list_filter = ("brand", "recording_enabled", "codec", "recording_server", "group")
    search_fields = ("name_fa", "uuid", "ip_address", "onvif_endpoint")
    readonly_fields = ("uuid", "config_revision", "created_at", "updated_at")
    autocomplete_fields = ("group", "recording_server")
    inlines = [MotionRoiGridInline]
    actions = ["action_auto_detect"]

    fieldsets = (
        (_("اطلاعات اصلی"), {
            "fields": ("name_fa", "group", "recording_server"),
            "description": (
                "فقط نام دوربین، گروه (ناحیه) و سرور ضبط را انتخاب کنید."
            ),
        }),
        (_("اتصال به دوربین"), {
            "fields": ("ip_address", "brand", "username", "password", "onvif_endpoint"),
            "description": DETECT_EXPLANATION,
        }),
        (_("جریان‌های تصویر (پیشرفته)"), {
            "fields": ("rtsp_main_url", "rtsp_mid_url", "rtsp_sub_url", "codec"),
            "classes": ("collapse",),
            "description": (
                "این فیلدها هنگام ذخیره به صورت خودکار پر می‌شوند؛ "
                "فقط در صورت نیاز به تنظیم دستی آنها را تغییر دهید."
            ),
        }),
        (_("ضبط و ذخیره‌سازی"), {
            "fields": ("recording_enabled", "retention_days",
                       "onvif_motion_events", "edge_storage_profile_g"),
        }),
        (_("نقشه و زاویه دید (پیشرفته)"), {
            "fields": ("map_latitude", "map_longitude",
                       "fov_direction_deg", "fov_angle_deg"),
            "classes": ("collapse",),
            "description": (
                "فقط برای نقشه هوشمند لازم است؛ می‌توانید خالی بگذارید."
            ),
        }),
        (_("مشخصات سیستمی"), {
            "fields": ("uuid", "config_revision", "created_at", "updated_at"),
            "classes": ("collapse",),
        }),
    )

    @admin.display(description=_("آخرین بروزرسانی (شمسی)"), ordering="updated_at")
    def updated_shamsi(self, obj):
        return utc_to_shamsi(obj.updated_at) if obj.updated_at else "—"

    # ------------------------------------------------------------------ #
    # Live auto-detect button: POSTs form fields, fills them with results.
    # ------------------------------------------------------------------ #
    def get_urls(self):
        urls = super().get_urls()
        custom = [
            path(
                "detect-camera/",
                self.admin_site.admin_view(self.detect_camera_view),
                name="cameras_camera_detect",
            ),
        ]
        return custom + urls

    def detect_camera_view(self, request):
        """Run auto_detect_camera with form values; return JSON for the UI."""
        data = request.POST
        result = auto_detect_camera(
            ip=data.get("ip_address", ""),
            username=data.get("username", ""),
            password=data.get("password", ""),
            brand=data.get("brand", "generic-onvif"),
            onvif_endpoint=data.get("onvif_endpoint", ""),
        )
        return JsonResponse(result)

    # ------------------------------------------------------------------ #
    # List action: (re)detect a batch of cameras from stored credentials.
    # ------------------------------------------------------------------ #
    @admin.action(
        description=_("تشخیص خودکار اتصال (ONVIF / برند) برای انتخاب‌شده‌ها")
    )
    def action_auto_detect(self, request, queryset):
        ok = 0
        for camera in queryset:
            if not camera.ip_address.strip():
                continue
            result = auto_detect_camera(
                ip=camera.ip_address,
                username=camera.username,
                password=camera.password_encrypted,
                brand=camera.brand,
                onvif_endpoint=camera.onvif_endpoint,
            )
            changed = False
            for field, value in (
                ("rtsp_main_url", result["rtsp_main_url"]),
                ("rtsp_mid_url", result["rtsp_mid_url"]),
                ("rtsp_sub_url", result["rtsp_sub_url"]),
            ):
                if value and getattr(camera, field) != value:
                    setattr(camera, field, value)
                    changed = True
            if result["onvif_endpoint"] and camera.onvif_endpoint != result["onvif_endpoint"]:
                camera.onvif_endpoint = result["onvif_endpoint"]
                changed = True
            if changed:
                camera.save(update_fields=[
                    "rtsp_main_url", "rtsp_mid_url", "rtsp_sub_url",
                    "onvif_endpoint", "updated_at",
                ])
            if result["method"] is not None:
                ok += 1
        if ok:
            self.message_user(
                request,
                f"تنظیمات {ok} دوربین به صورت خودکار تکمیل شد.",
                level=messages.SUCCESS,
            )
        else:
            self.message_user(
                request,
                "هیچ دوربینی قابل تشخیص نبود؛ آدرس IP و برند را بررسی کنید.",
                level=messages.WARNING,
            )

    # ------------------------------------------------------------------ #
    # Surface detection results as admin messages after a form save.
    # ------------------------------------------------------------------ #
    def save_model(self, request, obj, form, change):
        super().save_model(request, obj, form, change)
        warnings = getattr(form, "detection_warnings", [])
        method = getattr(form, "detection_method", None)
        device = getattr(form, "detection_device", {})
        if method == "onvif":
            model = device.get("model") or ""
            firm = device.get("firmware") or ""
            detail = f" ({device.get('manufacturer')} {model} - {firm})".strip()
            self.message_user(
                request,
                f"دوربین با ONVIF شناسایی شد{detail} و URL استریم‌ها تکمیل شد.",
                level=messages.SUCCESS,
            )
        elif method == "brand":
            self.message_user(
                request,
                "URL استریم‌ها طبق الگوی برند ساخته شد. برای دقت بیشتر، "
                "دکمه «تشخیص خودکار» را هم امتحان کنید.",
                level=messages.INFO,
            )
        for warn in warnings:
            self.message_user(request, warn, level=messages.WARNING)


@admin.register(MotionRoiGrid)
class MotionRoiGridAdmin(admin.ModelAdmin):
    list_display = ("camera", "grid_cols", "grid_rows", "sensitivity")
    search_fields = ("camera__name_fa",)
    autocomplete_fields = ("camera",)
