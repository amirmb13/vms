"""Central write-path for the immutable audit trail (always UTC)."""
from __future__ import annotations

from .models import AuditLog


def _client_ip(request) -> str | None:
    xff = request.META.get("HTTP_X_FORWARDED_FOR")
    if xff:
        return xff.split(",")[0].strip()
    return request.META.get("REMOTE_ADDR")


def record(
    request,
    action: str,
    message_fa: str,
    *,
    target: str = "",
    severity: str = AuditLog.Severity.INFO,
    payload: dict | None = None,
) -> AuditLog:
    return AuditLog.objects.create(
        actor=request.user if request.user.is_authenticated else None,
        action=action,
        target=target,
        severity=severity,
        message_fa=message_fa,
        payload=payload or {},
        source_ip=_client_ip(request),
    )
