"""Camera inventory, dual-stream profiles, recording servers, and ROI grids."""
import uuid

from django.db import models
from django.utils.translation import gettext_lazy as _


class RecordingServer(models.Model):
    """A registered C++ Recording Server / Media Relay node."""

    uuid = models.UUIDField(default=uuid.uuid4, unique=True, editable=False)
    hostname = models.CharField(
        _("نام میزبان"),
        max_length=255,
        help_text=_("نام یا آدرس شبکه‌ای سرور، مثلاً «سرور ضبط ساختمان مرکزی» یا rec-01."),
    )
    grpc_endpoint = models.CharField(
        _("آدرس gRPC"),
        max_length=255,
        help_text=_(
            "آدرس host:port که بقیه سامانه با این سرور ارتباط برقرار می‌کنند. "
            "این مقدار هنگام روشن شدن سرور به صورت خودکار ثبت می‌شود و نیازی به ورود دستی ندارد."
        ),
    )  # host:port
    storage_path = models.CharField(
        _("مسیر ذخیره‌سازی"),
        max_length=512,
        default="/mnt/vms-archive",
        help_text=_(
            "پوشه‌ای که ضبط‌ها در این سرور در آن نوشته می‌شوند (مثلاً /mnt/vms-archive). "
            "باید روی همان سرور وجود داشته باشد و قابل نوشتن باشد."
        ),
    )
    gpu_available = models.BooleanField(
        _("پردازنده گرافیکی"),
        default=False,
        help_text=_("به صورت خودکار هنگام روشن شدن سرور تشخیص داده می‌شود."),
    )
    simd_capabilities = models.JSONField(
        _("قابلیت‌های SIMD"),
        default=list,
        help_text=_(
            "شتاب‌های پردازنده (مثل AVX2 یا AVX-512) که تشخیص حرکت را سریع‌تر می‌کنند. "
            "به صورت خودکار تشخیص داده می‌شود."
        ),
    )  # ["AVX2"]
    is_online = models.BooleanField(
        _("آنلاین"),
        default=False,
        help_text=_("آخرین وضعیت اتصال سرور؛ به صورت خودکار به‌روزرسانی می‌شود."),
    )
    last_heartbeat = models.DateTimeField(null=True, blank=True)  # UTC

    class Meta:
        verbose_name = _("سرور ضبط")
        verbose_name_plural = _("سرورهای ضبط")

    def __str__(self) -> str:
        return self.hostname


class CameraGroup(models.Model):
    """Hierarchical directory tree shown in the Qt client (RTL tree view)."""

    name_fa = models.CharField(_("نام گروه"), max_length=255)
    parent = models.ForeignKey(
        "self", null=True, blank=True, on_delete=models.CASCADE, related_name="children"
    )

    class Meta:
        verbose_name = _("گروه دوربین")
        verbose_name_plural = _("گروه‌های دوربین")

    def __str__(self) -> str:
        return self.name_fa


class Camera(models.Model):
    class Codec(models.TextChoices):
        H264 = "h264", "H.264"
        H265 = "h265", "H.265"

    class Brand(models.TextChoices):
        HIKVISION = "hikvision", _("هیک‌ویژن (Hikvision)")
        DAHUA = "dahua", _("داهوا (Dahua)")
        UNIVIEW = "uniview", _("یونی‌ویو (Uniview)")
        AXIS = "axis", _("اکسیس (Axis)")
        # Iranian manufacturers — all ONVIF compliant; auto-detected.
        ARIA = "aria", _("آریا (Aria)")
        CAMBIZ = "cambiz", _("کامبیز (Cambiz)")
        ATAL = "atal", _("آتال (Atal)")
        RADIN = "radin", _("رادین (Radin)")
        PARSAN = "parsan", _("پارسان (Parsan)")
        PEJVAK = "pejvak", _("پژواک (Pejvak)")
        SANA = "sana", _("سانا (Sana)")
        ARMAN = "arman", _("آرمان (Arman)")
        MERSAD = "mersad", _("مرصاد (Mersad)")
        GENERIC_ONVIF = "generic-onvif", _("استاندارد (ONVIF)")
        CUSTOM = "custom", _("سفارشی (ورود دستی URL)")

    uuid = models.UUIDField(default=uuid.uuid4, unique=True, editable=False)
    name_fa = models.CharField(_("نام دوربین"), max_length=255)
    group = models.ForeignKey(
        CameraGroup, on_delete=models.PROTECT, related_name="cameras"
    )
    recording_server = models.ForeignKey(
        RecordingServer, on_delete=models.PROTECT, related_name="cameras"
    )

    # --- Operator-facing connection info -------------------------------------
    # The ONLY two fields an operator needs: IP + brand. Everything else is
    # auto-detected (ONVIF) or derived from the brand's standard RTSP layout.
    ip_address = models.CharField(_("آدرس IP"), max_length=64, blank=True)
    brand = models.CharField(
        _("مدل / برند"),
        max_length=32,
        choices=Brand.choices,
        default=Brand.GENERIC_ONVIF,
        help_text=_(
            "برند دوربین را انتخاب کنید. برای دوربین‌های ایرانی و استاندارد "
            "ONVIF، تنظیمات به صورت خودکار از خود دوربین تشخیص داده می‌شود."
        ),
    )

    # ONVIF credentials — used for auto-detection and remote management.
    onvif_endpoint = models.CharField(
        _("آدرس سرویس ONVIF"), max_length=512, blank=True,
        help_text=_("در صورت خالی بودن، به صورت خودکار ساخته می‌شود."),
    )
    username = models.CharField(_("نام کاربری دوربین"), max_length=128)
    # NOTE: encrypt at rest in production (e.g. django-fernet-fields / KMS).
    password_encrypted = models.TextField(_("رمز عبور دوربین"))

    # Dual-stream mandate: main (4K/25fps archive) + sub (360p/15fps grid+motion)
    # These are auto-filled by detection; operators may leave them blank.
    rtsp_main_url = models.CharField(
        _("جریان اصلی (۴K)"), max_length=512, blank=True,
        help_text=_("به صورت خودکار پر می‌شود — به سلیقه می‌توانید دستی تغییر دهید."),
    )
    rtsp_sub_url = models.CharField(
        _("جریان فرعی (۳۶۰p)"), max_length=512, blank=True,
        help_text=_("به صورت خودکار پر می‌شود — به سلیقه می‌توانید دستی تغییر دهید."),
    )
    rtsp_mid_url = models.CharField(
        _("جریان میانی (۷۲۰p/۱۰۸۰p)"), max_length=512, blank=True,
        help_text=_("اختیاری — برای شبکه‌های ۴ دوربینه استفاده می‌شود."),
    )
    codec = models.CharField(max_length=8, choices=Codec.choices, default=Codec.H265)

    recording_enabled = models.BooleanField(_("ضبط فعال"), default=True)
    retention_days = models.PositiveIntegerField(_("مدت نگهداری (روز)"), default=30)
    onvif_motion_events = models.BooleanField(
        _("رویداد حرکتی ONVIF (لایه ۱)"), default=False
    )
    edge_storage_profile_g = models.BooleanField(
        _("ذخیره‌سازی لبه (ONVIF Profile G)"), default=False
    )

    # GIS Smart Maps: position + FOV cone on floor plans / OpenStreetMap
    map_latitude = models.FloatField(null=True, blank=True)
    map_longitude = models.FloatField(null=True, blank=True)
    fov_direction_deg = models.FloatField(_("جهت دید (درجه)"), null=True, blank=True)
    fov_angle_deg = models.FloatField(_("زاویه دید (درجه)"), null=True, blank=True)

    # Monotonic revision — bumped on every change; pushed to C++ via orchestrator
    config_revision = models.PositiveBigIntegerField(default=1)
    created_at = models.DateTimeField(auto_now_add=True)  # UTC
    updated_at = models.DateTimeField(auto_now=True)      # UTC

    class Meta:
        verbose_name = _("دوربین")
        verbose_name_plural = _("دوربین‌ها")
        # Stable ordering is mandatory for consistent pagination at 10k+ rows.
        ordering = ["id"]
        indexes = [
            # Composite index backing the most common inventory filter path:
            # "all recording-enabled cameras of group X" (tree + wall loads).
            models.Index(fields=["group", "recording_enabled"],
                         name="camera_group_rec_idx"),
            models.Index(fields=["codec"], name="camera_codec_idx"),
        ]

    def __str__(self) -> str:
        return self.name_fa


class MotionRoiGrid(models.Model):
    """UI-defined Region-of-Interest bitmask consumed by the C++ SIMD engine."""

    camera = models.OneToOneField(Camera, on_delete=models.CASCADE, related_name="roi")
    grid_cols = models.PositiveSmallIntegerField(default=32)
    grid_rows = models.PositiveSmallIntegerField(default=24)
    # Packed row-major bitmask, base64 in JSON transit, raw bytes to gRPC.
    bitmask = models.BinaryField()
    sensitivity = models.PositiveSmallIntegerField(_("حساسیت"), default=50)  # 0..100

    class Meta:
        verbose_name = _("شبکه ناحیه حساس حرکت")
        verbose_name_plural = _("شبکه‌های ناحیه حساس حرکت")
