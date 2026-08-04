"""Django admin — accounts: users, RBAC roles, Four-Eyes requests, and the
Auth Manager (Django groups & permissions)."""
from django.contrib import admin
from django.contrib.auth.admin import GroupAdmin as DjangoGroupAdmin
from django.contrib.auth.admin import UserAdmin as DjangoUserAdmin
from django.contrib.auth.models import Group, Permission
from django.db.models import Count
from django.utils.translation import gettext_lazy as _

from apps.common.shamsi import utc_to_shamsi

from .models import FourEyesRequest, Role, RoleAssignment, User


class RoleAssignmentInline(admin.TabularInline):
    """Manage a user's VMS role assignments directly on the user page."""

    model = RoleAssignment
    extra = 0
    autocomplete_fields = ("role", "camera_group")
    verbose_name = _("انتساب نقش")
    verbose_name_plural = _("نقش‌های سامانه نظارت")


@admin.register(User)
class UserAdmin(DjangoUserAdmin):
    inlines = (RoleAssignmentInline,)
    list_display = (
        "username",
        "full_name_fa",
        "phone",
        "group_list",
        "is_ad_account",
        "is_active",
        "is_staff",
    )
    list_filter = ("is_active", "is_staff", "is_ad_account", "groups")
    search_fields = ("username", "full_name_fa", "phone", "email")
    ordering = ("username",)

    def get_queryset(self, request):
        return super().get_queryset(request).prefetch_related("groups")

    @admin.display(description=_("گروه‌ها"))
    def group_list(self, obj):
        names = [g.name for g in obj.groups.all()]
        return "، ".join(names) if names else "—"
    fieldsets = DjangoUserAdmin.fieldsets + (
        (_("اطلاعات سامانه نظارت"), {"fields": ("full_name_fa", "phone", "is_ad_account")}),
    )
    # New-user form: capture VMS identity fields at creation time too.
    add_fieldsets = DjangoUserAdmin.add_fieldsets + (
        (_("اطلاعات سامانه نظارت"), {"fields": ("full_name_fa", "phone")}),
    )

    def get_inline_instances(self, request, obj=None):
        # Role assignments reference the user row — hide the inline on the
        # "add" form until the user actually exists.
        if obj is None:
            return []
        return super().get_inline_instances(request, obj)


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


# --- Auth Manager: Django groups & permissions ------------------------------
admin.site.unregister(Group)


@admin.register(Group)
class GroupAdmin(DjangoGroupAdmin):
    """Group management with member / permission counts at a glance."""

    list_display = ("name", "permission_count", "member_count")
    search_fields = ("name",)
    filter_horizontal = ("permissions",)

    def get_queryset(self, request):
        return (
            super()
            .get_queryset(request)
            .annotate(_perm_count=Count("permissions", distinct=True))
            .annotate(_member_count=Count("user", distinct=True))
        )

    @admin.display(description=_("تعداد مجوزها"), ordering="_perm_count")
    def permission_count(self, obj):
        return obj._perm_count

    @admin.display(description=_("تعداد اعضا"), ordering="_member_count")
    def member_count(self, obj):
        return obj._member_count


class AppLabelFilter(admin.SimpleListFilter):
    """Localized sidebar filter — replaces the raw 'content_type__app_label'."""

    title = _("اپلیکیشن")
    parameter_name = "app"

    def lookups(self, request, model_admin):
        labels = (
            model_admin.get_queryset(request)
            .values_list("content_type__app_label", flat=True)
            .distinct()
            .order_by("content_type__app_label")
        )
        return [(label, label) for label in labels]

    def queryset(self, request, queryset):
        if self.value():
            return queryset.filter(content_type__app_label=self.value())
        return queryset


@admin.register(Permission)
class PermissionAdmin(admin.ModelAdmin):
    """Browsable catalogue of system permissions (read-mostly)."""

    list_display = ("name", "codename", "app_label", "model_name")
    list_filter = (AppLabelFilter,)
    search_fields = ("name", "codename", "content_type__app_label")
    list_select_related = ("content_type",)
    ordering = ("content_type__app_label", "content_type__model", "codename")

    @admin.display(description=_("اپلیکیشن"), ordering="content_type__app_label")
    def app_label(self, obj):
        return obj.content_type.app_label

    @admin.display(description=_("مدل"), ordering="content_type__model")
    def model_name(self, obj):
        return obj.content_type.model

    # Permissions are code-defined; creating/deleting them by hand corrupts RBAC.
    def has_add_permission(self, request):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
