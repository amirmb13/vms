"""Django admin — audit: read-only immutable audit trail viewer."""
from django.contrib import admin
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .models import AuditLog


@admin.register(AuditLog)
class AuditLogAdmin(admin.ModelAdmin):
    list_display = (
        "created_shamsi",
        "actor",
        "action",
        "target",
        "severity",
        "message_fa",
        "source_ip",
    )
    list_filter = ("severity", "action")
    search_fields = ("action", "target", "message_fa", "actor__username")
    date_hierarchy = "created_at"

    @admin.display(description=_("زمان رویداد (شمسی)"), ordering="created_at")
    def created_shamsi(self, obj):
        return utc_to_shamsi(obj.created_at) if obj.created_at else "—"

    # Immutable trail: no create / edit / delete from the admin.
    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
