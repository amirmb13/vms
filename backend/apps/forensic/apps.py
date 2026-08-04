from django.apps import AppConfig
from django.utils.translation import gettext_lazy as _


class ForensicConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "apps.forensic"
    verbose_name = _("تحلیل هوشمند و جرم‌شناسی")
