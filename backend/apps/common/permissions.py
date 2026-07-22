"""
Granular RBAC permission classes.

A user's effective permission for a camera group is the union of all
RoleAssignments that are either global (camera_group is NULL) or scoped to
the group / any of its ancestors.
"""
from __future__ import annotations

from rest_framework.permissions import BasePermission


def _user_has_flag(user, flag: str, camera_group=None) -> bool:
    """True if any assignment grants `flag`, respecting group scoping."""
    if not user or not user.is_authenticated:
        return False
    if user.is_superuser:
        return True

    assignments = user.assignments.select_related("role", "camera_group")

    # Collect the ancestor chain of the target group (scoped roles cascade down)
    ancestor_ids: set[int] = set()
    node = camera_group
    while node is not None:
        ancestor_ids.add(node.id)
        node = node.parent

    for a in assignments:
        if not getattr(a.role, flag, False):
            continue
        if a.camera_group_id is None:  # global role
            return True
        if a.camera_group_id in ancestor_ids:
            return True
    return False


def _flag_permission(flag: str, message_fa: str):
    class _Perm(BasePermission):
        message = message_fa

        def has_permission(self, request, view):
            return _user_has_flag(request.user, flag)

    _Perm.__name__ = f"Can_{flag}"
    return _Perm


CanViewLive = _flag_permission("can_view_live", "شما مجوز مشاهده پخش زنده را ندارید.")
CanViewPlayback = _flag_permission("can_view_playback", "شما مجوز بازبینی آرشیو را ندارید.")
CanExportVideo = _flag_permission("can_export_video", "شما مجوز خروجی گرفتن از ویدئو را ندارید.")
CanControlPtz = _flag_permission("can_control_ptz", "شما مجوز کنترل PTZ را ندارید.")
CanManageCameras = _flag_permission("can_manage_cameras", "شما مجوز مدیریت دوربین‌ها را ندارید.")
CanManageUsers = _flag_permission("can_manage_users", "شما مجوز مدیریت کاربران را ندارید.")
CanApproveCritical = _flag_permission(
    "can_approve_critical", "شما مجوز تأیید عملیات حساس (چهارچشمی) را ندارید."
)


class ReadOnlyOrManageCameras(BasePermission):
    """SAFE methods for any authenticated user; writes need can_manage_cameras."""

    message = "شما مجوز مدیریت دوربین‌ها را ندارید."

    def has_permission(self, request, view):
        if not request.user or not request.user.is_authenticated:
            return False
        if request.method in ("GET", "HEAD", "OPTIONS"):
            return True
        return _user_has_flag(request.user, "can_manage_cameras")
