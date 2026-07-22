"""Django admin — forensic: enrolled identities and AI detection records."""
from django.contrib import admin
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .models import DetectionRecord, Identity, SynopsisJob


@admin.register(Identity)
class IdentityAdmin(admin.ModelAdmin):
    list_display = ("name_fa", "national_id", "is_watchlisted", "created_shamsi")
    list_filter = ("is_watchlisted",)
    search_fields = ("name_fa", "national_id")
    readonly_fields = ("created_at",)

    @admin.display(description=_("زمان ثبت (شمسی)"), ordering="created_at")
    def created_shamsi(self, obj):
        return utc_to_shamsi(obj.created_at) if obj.created_at else "—"


@admin.register(DetectionRecord)
class DetectionRecordAdmin(admin.ModelAdmin):
    list_display = (
        "detected_shamsi",
        "camera",
        "label",
        "matched_identity",
        "similarity",
        "attributes",
    )
    list_filter = ("label", "camera")
    search_fields = ("label", "camera__name_fa", "matched_identity__name_fa")
    date_hierarchy = "utc_timestamp"
    autocomplete_fields = ("camera", "matched_identity")
    readonly_fields = ("embedding",)

    @admin.display(description=_("زمان تشخیص (شمسی)"), ordering="utc_timestamp")
    def detected_shamsi(self, obj):
        return utc_to_shamsi(obj.utc_timestamp) if obj.utc_timestamp else "—"


@admin.register(SynopsisJob)
class SynopsisJobAdmin(admin.ModelAdmin):
    list_display = (
        "uuid",
        "camera",
        "state",
        "progress_percent",
        "window_shamsi",
        "requested_by",
    )
    list_filter = ("state", "camera")
    readonly_fields = ("uuid", "result_manifest", "created_at", "updated_at")
    autocomplete_fields = ("camera",)

    @admin.display(description=_("بازه (شمسی)"))
    def window_shamsi(self, obj):
        return f"{utc_to_shamsi(obj.from_utc)} تا {utc_to_shamsi(obj.to_utc)}"
