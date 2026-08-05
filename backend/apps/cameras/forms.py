"""Django admin form for Camera — the operator-facing surface.

Goal: an operator adds a camera by typing name + IP + credentials + brand
and pressing save. Every advanced field is either auto-filled by the
detection service or left for the collapsed "advanced" fieldset.
"""
from __future__ import annotations

from django import forms

from .models import Camera
from .services import auto_detect_camera

DETECT_EXPLANATION = (
    "این دکمه یا دکمه «تشخیص خودکار» کنار فیلد آدرس IP، ابتدا با پروتکل ONVIF "
    "با دوربین ارتباط می‌گیرد و اگر در دسترس نبود، URL استریم را طبق الگوی "
    "استاندارد برند شما می‌سازد. دوربین‌های ایرانی (آریا، کامبیز، آتال و ...) "
    "و استاندارد ONVIF به صورت خودکار از خود دوربین تشخیص داده می‌شوند."
)


class CameraAdminForm(forms.ModelForm):
    """Form whose save() auto-detects stream URLs when they are left blank."""

    password = forms.CharField(
        label="رمز عبور دوربین",
        widget=forms.PasswordInput(render_value=True),
        required=False,
        help_text="رمز دوربین برای تشخیص خودکار و اتصال به استریم استفاده می‌شود.",
    )

    class Meta:
        model = Camera
        fields = "__all__"
        widgets = {
            "ip_address": forms.TextInput(attrs={
                "placeholder": "مثال: 192.168.1.64",
                "dir": "ltr",
            }),
            "rtsp_main_url": forms.TextInput(attrs={"dir": "ltr"}),
            "rtsp_mid_url": forms.TextInput(attrs={"dir": "ltr"}),
            "rtsp_sub_url": forms.TextInput(attrs={"dir": "ltr"}),
            "onvif_endpoint": forms.TextInput(attrs={"dir": "ltr"}),
        }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # The raw stored-password column is managed by the `password` form
        # field instead; never render it as a plaintext textarea.
        self.fields.pop("password_encrypted", None)
        instance = kwargs.get("instance")
        if instance is not None and instance.pk:
            # Prefill the password box from the stored value so saving a
            # camera does not silently wipe its credential.
            self.fields["password"].initial = instance.password_encrypted
        if instance is None or not instance.pk:
            self.fields["password"].required = True

    def clean(self):
        cleaned = super().clean()
        # A blank URL set triggers auto-detection on save; make sure the
        # operator knows detection needs an IP + credentials.
        has_blank = not (
            cleaned.get("rtsp_main_url") or cleaned.get("rtsp_sub_url")
            or cleaned.get("rtsp_mid_url")
        )
        if has_blank and not (cleaned.get("ip_address") or "").strip():
            self.add_error(
                "ip_address",
                "برای تشخیص خودکار، آدرس IP دوربین را وارد کنید.",
            )
        return cleaned

    def save(self, commit=True):
        camera = super().save(commit=False)
        password = self.cleaned_data.get("password", "")
        if password:
            camera.password_encrypted = password

        # Auto-detect whenever the operator left the stream URLs blank.
        self.detection_warnings: list[str] = []
        self.detection_method: str | None = None
        self.detection_device: dict = {}
        needs_detect = not (
            camera.rtsp_main_url or camera.rtsp_sub_url or camera.rtsp_mid_url
        )
        if needs_detect and camera.ip_address.strip() and camera.username:
            result = auto_detect_camera(
                ip=camera.ip_address,
                username=camera.username,
                password=password or camera.password_encrypted,
                brand=camera.brand,
                onvif_endpoint=camera.onvif_endpoint,
            )
            self.detection_method = result["method"]
            self.detection_device = result["device_info"]
            self.detection_warnings = result["warnings"]
            camera.onvif_endpoint = result["onvif_endpoint"] or camera.onvif_endpoint
            camera.rtsp_main_url = result["rtsp_main_url"]
            camera.rtsp_mid_url = result["rtsp_mid_url"]
            camera.rtsp_sub_url = result["rtsp_sub_url"]
            if result["codec"]:
                camera.codec = result["codec"]

        if commit:
            camera.save()
            self.save_m2m()
        return camera
