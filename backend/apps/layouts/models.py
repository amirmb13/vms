"""UI grid layouts stored as validated JSON schemas, served to the Qt client."""
import uuid

from django.core.exceptions import ValidationError
from django.db import models
from django.utils.translation import gettext_lazy as _

# JSON Schema enforced on every layout save. The Qt Grid Engine
# (QAbstractListModel) applies these instantaneously, including merged cells.
LAYOUT_JSON_SCHEMA = {
    "type": "object",
    "required": ["grid", "cells"],
    "properties": {
        "grid": {
            "type": "object",
            "required": ["cols", "rows"],
            "properties": {
                "cols": {"type": "integer", "minimum": 1, "maximum": 16},
                "rows": {"type": "integer", "minimum": 1, "maximum": 16},
            },
        },
        "cells": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["x", "y", "w", "h"],
                "properties": {
                    "x": {"type": "integer", "minimum": 0},
                    "y": {"type": "integer", "minimum": 0},
                    "w": {"type": "integer", "minimum": 1},  # cell merging
                    "h": {"type": "integer", "minimum": 1},
                    "camera_uuid": {"type": ["string", "null"]},
                },
            },
        },
    },
}


def validate_layout_json(value: dict) -> None:
    import jsonschema

    try:
        jsonschema.validate(value, LAYOUT_JSON_SCHEMA)
    except jsonschema.ValidationError as exc:
        raise ValidationError(
            _("چیدمان نامعتبر است: %(err)s"), params={"err": exc.message}
        )


class GridLayout(models.Model):
    uuid = models.UUIDField(default=uuid.uuid4, unique=True, editable=False)
    name_fa = models.CharField(_("نام چیدمان"), max_length=255)
    owner = models.ForeignKey(
        "accounts.User", on_delete=models.CASCADE, related_name="layouts"
    )
    is_shared = models.BooleanField(_("اشتراک‌گذاری با سایر کاربران"), default=False)
    layout_json = models.JSONField(_("ساختار چیدمان"), validators=[validate_layout_json])
    updated_at = models.DateTimeField(auto_now=True)  # UTC

    class Meta:
        verbose_name = _("چیدمان نمایش")
        verbose_name_plural = _("چیدمان‌های نمایش")
        # Stable ordering is mandatory for consistent DRF pagination
        # (the Qt GridModel follows `next` links page-by-page).
        ordering = ["-updated_at", "id"]
        indexes = [
            # Backs the catalogue query `owner = me OR is_shared = true`
            # without a sequential scan once thousands of layouts exist.
            models.Index(fields=["is_shared"], name="layout_shared_idx"),
            models.Index(fields=["owner", "-updated_at"],
                         name="layout_owner_updated_idx"),
        ]

    def __str__(self) -> str:
        return self.name_fa
