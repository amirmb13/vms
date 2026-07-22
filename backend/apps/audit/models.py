"""Immutable audit trail. Stored in UTC; rendered as Shamsi at the edges."""
from django.db import models
from django.utils.translation import gettext_lazy as _


class AuditLog(models.Model):
    class Severity(models.TextChoices):
        INFO = "info", _("اطلاع")
        WARNING = "warning", _("هشدار")
        CRITICAL = "critical", _("بحرانی")

    actor = models.ForeignKey(
        "accounts.User", null=True, blank=True, on_delete=models.SET_NULL
    )
    action = models.CharField(_("عملیات"), max_length=128)  # e.g. "camera.update"
    target = models.CharField(_("هدف"), max_length=255, blank=True)
    severity = models.CharField(
        max_length=16, choices=Severity.choices, default=Severity.INFO
    )
    message_fa = models.TextField(_("شرح رویداد"))
    payload = models.JSONField(default=dict, blank=True)
    source_ip = models.GenericIPAddressField(null=True, blank=True)
    created_at = models.DateTimeField(auto_now_add=True, db_index=True)  # UTC

    class Meta:
        verbose_name = _("گزارش ممیزی")
        verbose_name_plural = _("گزارش‌های ممیزی")
        ordering = ["-created_at"]

    def save(self, *args, **kwargs):
        if self.pk:
            raise RuntimeError("Audit logs are immutable and cannot be modified.")
        super().save(*args, **kwargs)
