"""Users, granular RBAC, and Four-Eyes Authorization primitives."""
import uuid

from django.contrib.auth.models import AbstractUser
from django.db import models
from django.utils.translation import gettext_lazy as _


class User(AbstractUser):
    uuid = models.UUIDField(default=uuid.uuid4, unique=True, editable=False)
    full_name_fa = models.CharField(_("نام کامل (فارسی)"), max_length=255, blank=True)
    is_ad_account = models.BooleanField(_("حساب اکتیو دایرکتوری"), default=False)
    phone = models.CharField(_("شماره تماس"), max_length=32, blank=True)

    class Meta:
        verbose_name = _("کاربر")
        verbose_name_plural = _("کاربران")


class Role(models.Model):
    """Granular RBAC role, assignable per camera-group / site."""

    name_fa = models.CharField(_("عنوان نقش"), max_length=128, unique=True)
    can_view_live = models.BooleanField(_("مشاهده پخش زنده"), default=True)
    can_view_playback = models.BooleanField(_("بازبینی آرشیو"), default=False)
    can_export_video = models.BooleanField(_("خروجی ویدئو"), default=False)
    can_control_ptz = models.BooleanField(_("کنترل PTZ"), default=False)
    can_manage_cameras = models.BooleanField(_("مدیریت دوربین‌ها"), default=False)
    can_manage_users = models.BooleanField(_("مدیریت کاربران"), default=False)
    can_approve_critical = models.BooleanField(
        _("تأیید عملیات حساس (چهارچشمی)"), default=False
    )

    class Meta:
        verbose_name = _("نقش")
        verbose_name_plural = _("نقش‌ها")

    def __str__(self) -> str:
        return self.name_fa


class RoleAssignment(models.Model):
    """Binds a user to a role, optionally scoped to a camera group."""

    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name="assignments")
    role = models.ForeignKey(Role, on_delete=models.CASCADE)
    camera_group = models.ForeignKey(
        "cameras.CameraGroup",
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        help_text=_("در صورت خالی بودن، نقش در کل سامانه اعمال می‌شود."),
    )

    class Meta:
        unique_together = ("user", "role", "camera_group")
        verbose_name = _("انتساب نقش")
        verbose_name_plural = _("انتساب نقش‌ها")


class FourEyesRequest(models.Model):
    """
    Four-Eyes Authorization: a critical operation (e.g. archive pruning)
    stays PENDING until TWO DISTINCT administrators cryptographically
    approve it. Only then does the orchestrator push the control signal
    to the C++ Recording Server.
    """

    class Status(models.TextChoices):
        PENDING = "pending", _("در انتظار تأیید")
        APPROVED = "approved", _("تأیید شده")
        REJECTED = "rejected", _("رد شده")
        EXECUTED = "executed", _("اجرا شده")

    uuid = models.UUIDField(default=uuid.uuid4, unique=True, editable=False)
    operation = models.CharField(_("عملیات"), max_length=64)  # e.g. "archive.prune"
    payload = models.JSONField(_("پارامترهای عملیات"))
    requested_by = models.ForeignKey(
        User, on_delete=models.PROTECT, related_name="foureyes_requested"
    )
    approver_a = models.ForeignKey(
        User, null=True, blank=True, on_delete=models.PROTECT, related_name="foureyes_a"
    )
    signature_a = models.TextField(blank=True)  # detached crypto signature
    approver_b = models.ForeignKey(
        User, null=True, blank=True, on_delete=models.PROTECT, related_name="foureyes_b"
    )
    signature_b = models.TextField(blank=True)
    status = models.CharField(
        max_length=16, choices=Status.choices, default=Status.PENDING
    )
    created_at = models.DateTimeField(auto_now_add=True)  # stored UTC
    executed_at = models.DateTimeField(null=True, blank=True)

    class Meta:
        verbose_name = _("درخواست احراز چهارچشمی")
        verbose_name_plural = _("درخواست‌های احراز چهارچشمی")

    @property
    def is_fully_approved(self) -> bool:
        return (
            self.approver_a_id is not None
            and self.approver_b_id is not None
            and self.approver_a_id != self.approver_b_id
            and bool(self.signature_a)
            and bool(self.signature_b)
        )
