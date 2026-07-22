"""Django admin — layouts: user grid layouts served to the Qt client."""
from django.contrib import admin
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .models import GridLayout


@admin.register(GridLayout)
class GridLayoutAdmin(admin.ModelAdmin):
    list_display = ("name_fa", "owner", "is_shared", "grid_size", "updated_shamsi")
    list_filter = ("is_shared",)
    search_fields = ("name_fa", "owner__username", "owner__full_name_fa")
    readonly_fields = ("uuid", "updated_at")
    autocomplete_fields = ("owner",)

    @admin.display(description=_("ابعاد شبکه"))
    def grid_size(self, obj):
        grid = (obj.layout_json or {}).get("grid", {})
        return f'{grid.get("cols", "?")}×{grid.get("rows", "?")}'

    @admin.display(description=_("آخرین بروزرسانی (شمسی)"), ordering="updated_at")
    def updated_shamsi(self, obj):
        return utc_to_shamsi(obj.updated_at) if obj.updated_at else "—"
