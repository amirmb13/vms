"""python manage.py run_control_grpc — serve ControlPlaneEvents to C++ nodes."""
from __future__ import annotations

import signal
import threading

from django.conf import settings
from django.core.management.base import BaseCommand

from apps.orchestrator import grpc_server


class Command(BaseCommand):
    help = (
        "اجرای سرور gRPC صفحه کنترل (ControlPlaneEvents) برای ثبت‌نام "
        "سرورهای ضبط C++ و دریافت جریان رویدادها."
    )

    def add_arguments(self, parser):
        parser.add_argument(
            "--port",
            type=int,
            default=int(getattr(settings, "CONTROL_PLANE_GRPC_PORT", 50060)),
            help="پورت شنود gRPC (پیش‌فرض: %(default)s)",
        )
        parser.add_argument(
            "--workers", type=int, default=16, help="تعداد نخ‌های کارگر gRPC"
        )

    def handle(self, *args, **options):
        server = grpc_server.serve(
            port=options["port"], max_workers=options["workers"]
        )
        self.stdout.write(
            self.style.SUCCESS(
                f"سرور gRPC صفحه کنترل روی پورت {options['port']} در حال اجراست."
            )
        )

        stop = threading.Event()

        def _shutdown(*_):
            stop.set()

        signal.signal(signal.SIGINT, _shutdown)
        signal.signal(signal.SIGTERM, _shutdown)
        stop.wait()

        self.stdout.write("در حال خاموش‌سازی امن سرور gRPC…")
        server.stop(grace=5).wait()
        self.stdout.write(self.style.SUCCESS("سرور gRPC متوقف شد."))
