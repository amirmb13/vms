"""python manage.py poll_server_health — periodic RecordingServerControl.GetServerHealth probe.

Marks recording servers online/offline in PostgreSQL and raises a CRITICAL
audit entry when free archive storage drops below the configured floor.
"""
from __future__ import annotations

import time

import grpc
from django.conf import settings
from django.core.management.base import BaseCommand
from django.utils import timezone

from generated import control_signals_pb2 as pb2
from generated import control_signals_pb2_grpc as pb2_grpc


class Command(BaseCommand):
    help = "پایش دوره‌ای سلامت سرورهای ضبط از طریق gRPC (GetServerHealth)."

    def add_arguments(self, parser):
        parser.add_argument(
            "--interval", type=float, default=15.0,
            help="فاصله بین دو پایش بر حسب ثانیه (پیش‌فرض: %(default)s)",
        )
        parser.add_argument(
            "--storage-floor-gb", type=float, default=50.0,
            help="حداقل فضای آزاد آرشیو؛ کمتر از این مقدار هشدار بحرانی ثبت می‌شود.",
        )
        parser.add_argument(
            "--once", action="store_true", help="فقط یک دور پایش و خروج."
        )

    def handle(self, *args, **options):
        interval = options["interval"]
        while True:
            self._poll_all(options["storage_floor_gb"])
            if options["once"]:
                break
            time.sleep(interval)

    def _poll_all(self, storage_floor_gb: float) -> None:
        from apps.audit.models import AuditLog
        from apps.cameras.models import RecordingServer

        timeout = getattr(settings, "RECORDING_SERVER_GRPC_TIMEOUT_S", 3.0)

        for server in RecordingServer.objects.all():
            try:
                with grpc.insecure_channel(server.grpc_endpoint) as channel:
                    stub = pb2_grpc.RecordingServerControlStub(channel)
                    report = stub.GetServerHealth(
                        pb2.HealthRequest(), timeout=timeout
                    )
            except grpc.RpcError:
                if server.is_online:
                    server.is_online = False
                    server.save(update_fields=["is_online"])
                    AuditLog.objects.create(
                        actor=None,
                        action="server.offline",
                        target=f"recording_server:{server.uuid}",
                        severity="critical",
                        message_fa=f"سرور ضبط «{server.hostname}» پاسخ نمی‌دهد.",
                        payload={"grpc_endpoint": server.grpc_endpoint},
                    )
                    self.stdout.write(self.style.WARNING(
                        f"OFFLINE  {server.hostname} ({server.grpc_endpoint})"
                    ))
                continue

            server.is_online = True
            server.gpu_available = report.gpu_available
            server.last_heartbeat = timezone.now()  # UTC
            server.save(
                update_fields=["is_online", "gpu_available", "last_heartbeat"]
            )
            self.stdout.write(
                f"OK       {server.hostname} cams={report.active_cameras} "
                f"relay={report.relay_clients} cpu={report.cpu_percent:.0f}% "
                f"free={report.storage_free_gb:.0f}GB "
                f"ntp_off={report.ntp_offset_us}us"
            )

            if report.storage_free_gb < storage_floor_gb:
                AuditLog.objects.create(
                    actor=None,
                    action="server.storage_warning",
                    target=f"recording_server:{server.uuid}",
                    severity="critical",
                    message_fa=(
                        f"فضای آزاد آرشیو سرور «{server.hostname}» به "
                        f"{report.storage_free_gb:.0f} گیگابایت رسیده است."
                    ),
                    payload={"storage_free_gb": report.storage_free_gb},
                )
