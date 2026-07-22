"""
Smart Forensic Search: AI-extracted metadata + face embeddings in PostgreSQL.
Enables queries like "مرد با پیراهن قرمز در درب شمالی، دیروز" resolved to
precise archive timestamps. Vector similarity uses pgvector.
"""
import uuid as uuid_lib

from django.db import models
from django.utils.translation import gettext_lazy as _


class Identity(models.Model):
    """A known person enrolled for ArcFace recognition."""

    name_fa = models.CharField(_("نام"), max_length=255)
    national_id = models.CharField(_("کد ملی"), max_length=20, blank=True)
    is_watchlisted = models.BooleanField(_("در فهرست مراقبت"), default=False)
    # Canonical 512-d ArcFace embedding. Swap to pgvector's VectorField
    # (vector(512)) once the extension is enabled for ANN search.
    embedding = models.JSONField()
    created_at = models.DateTimeField(auto_now_add=True)  # UTC

    class Meta:
        verbose_name = _("هویت")
        verbose_name_plural = _("هویت‌ها")

    def __str__(self) -> str:
        return self.name_fa


class SynopsisJob(models.Model):
    """Video Synopsis (TimeCompressor) job: a 24-hour archive window condensed
    by the C++ recording server into a short clip whose objects carry their
    original NTP timestamps (clickable in the Qt client timeline)."""

    class State(models.TextChoices):
        QUEUED = "queued", _("در صف")
        EXTRACTING = "extracting", _("استخراج اشیاء متحرک")
        COMPACTING = "compacting", _("فشرده‌سازی زمانی")
        RENDERING = "rendering", _("رندر خروجی")
        DONE = "done", _("تکمیل شد")
        FAILED = "failed", _("ناموفق")

    uuid = models.UUIDField(default=uuid_lib.uuid4, unique=True, editable=False)
    camera = models.ForeignKey(
        "cameras.Camera", on_delete=models.CASCADE, related_name="synopsis_jobs",
        verbose_name=_("دوربین"),
    )
    requested_by = models.ForeignKey(
        "accounts.User", on_delete=models.PROTECT, related_name="synopsis_jobs",
        verbose_name=_("درخواست‌دهنده"),
    )
    from_utc = models.DateTimeField(_("شروع بازه (UTC)"))
    to_utc = models.DateTimeField(_("پایان بازه (UTC)"))
    target_duration_s = models.PositiveIntegerField(
        _("مدت کلیپ خلاصه (ثانیه)"), default=120
    )
    state = models.CharField(
        _("وضعیت"), max_length=16, choices=State.choices, default=State.QUEUED
    )
    progress_percent = models.PositiveSmallIntegerField(_("درصد پیشرفت"), default=0)
    # Written back by the C++ server on completion: clip path + the clickable
    # timestamp manifest (tube_id -> synopsis_offset_us -> source_utc_us).
    result_manifest = models.JSONField(_("مانیفست نتیجه"), null=True, blank=True)
    error_fa = models.CharField(_("پیام خطا"), max_length=255, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)  # UTC
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = _("خلاصه‌سازی ویدئو")
        verbose_name_plural = _("خلاصه‌سازی‌های ویدئو")
        indexes = [models.Index(fields=["camera", "state"])]
        ordering = ["-created_at"]

    def __str__(self) -> str:
        return f"synopsis {self.uuid} ({self.get_state_display()})"


class DetectionRecord(models.Model):
    """One AI detection event, written by the orchestrator when the C++
    server relays InferenceResult payloads from the Python AI engine."""

    camera = models.ForeignKey(
        "cameras.Camera", on_delete=models.CASCADE, related_name="detections"
    )
    utc_timestamp = models.DateTimeField(db_index=True)  # frame NTP timestamp
    label = models.CharField(max_length=64)              # "person", "car", ...
    # Forensic attributes: {"shirt_color": "red", "gender": "male", ...}
    attributes = models.JSONField(default=dict)
    matched_identity = models.ForeignKey(
        Identity, null=True, blank=True, on_delete=models.SET_NULL
    )
    similarity = models.FloatField(null=True, blank=True)
    embedding = models.JSONField(null=True, blank=True)  # for unenrolled faces
    bbox = models.JSONField()                            # normalized x,y,w,h

    class Meta:
        verbose_name = _("رکورد تشخیص")
        verbose_name_plural = _("رکوردهای تشخیص")
        indexes = [
            models.Index(fields=["camera", "utc_timestamp"]),
            models.Index(fields=["label", "utc_timestamp"]),
        ]
