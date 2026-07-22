"""Django admin — accounts: users, RBAC roles, and Four-Eyes requests."""
from django.contrib import admin
from django.contrib.auth.admin import UserAdmin as DjangoUserAdmin
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .models import FourEyesRequest, Role, RoleAssignment, User


@admin.register(User)
class UserAdmin(DjangoUserAdmin):
    list_display = (
        "username",
        "full_name_fa",
        "phone",
        "is_ad_account",
        "is_active",
        "is_staff",
    )
    list_filter = ("is_active", "is_staff", "is_ad_account")
    search_fields = ("username", "full_name_fa", "phone", "email")
    ordering = ("username",)
    fieldsets = DjangoUserAdmin.fieldsets + (
        (_("اطلاعات سامانه نظارت"), {"fields": ("full_name_fa", "phone", "is_ad_account")}),
    )


@admin.register(Role)
class RoleAdmin(admin.ModelAdmin):
    list_display = (
        "name_fa",
        "can_view_live",
        "can_view_playback",
        "can_export_video",
        "can_control_ptz",
        "can_manage_cameras",
        "can_manage_users",
        "can_approve_critical",
    )
    search_fields = ("name_fa",)


@admin.register(RoleAssignment)
class RoleAssignmentAdmin(admin.ModelAdmin):
    list_display = ("user", "role", "camera_group")
    list_filter = ("role",)
    search_fields = ("user__username", "user__full_name_fa", "role__name_fa")
    autocomplete_fields = ("user", "role", "camera_group")


@admin.register(FourEyesRequest)
class FourEyesRequestAdmin(admin.ModelAdmin):
    list_display = (
        "uuid",
        "operation",
        "requested_by",
        "approver_a",
        "approver_b",
        "status",
        "created_shamsi",
        "executed_shamsi",
    )
    list_filter = ("status", "operation")
    search_fields = ("uuid", "operation", "requested_by__username")
    readonly_fields = ("uuid", "created_at", "executed_at")

    @admin.display(description=_("زمان ایجاد (شمسی)"), ordering="created_at")
    def created_shamsi(self, obj):
        return utc_to_shamsi(obj.created_at) if obj.created_at else "—"

    @admin.display(description=_("زمان اجرا (شمسی)"), ordering="executed_at")
    def executed_shamsi(self, obj):
        return utc_to_shamsi(obj.executed_at) if obj.executed_at else "—"
