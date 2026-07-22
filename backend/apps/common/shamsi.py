"""
Solar Hijri (Shamsi) rendering helpers.

Internal storage and stream sync are ALWAYS UTC. Shamsi formatting with
Persian digits happens only at presentation boundaries (API serializers,
notification payloads), e.g. `۱۴۰۵/۰۴/۳۰ - ۱۸:۵۱:۱۵`.
"""
from datetime import datetime
from zoneinfo import ZoneInfo

import jdatetime

IRAN_TZ = ZoneInfo("Asia/Tehran")  # IRST/IRDT

_PERSIAN_DIGITS = str.maketrans("0123456789", "۰۱۲۳۴۵۶۷۸۹")


def to_persian_digits(text: str) -> str:
    return text.translate(_PERSIAN_DIGITS)


def utc_to_shamsi(dt_utc: datetime, with_time: bool = True) -> str:
    """Convert a UTC datetime to a Shamsi string with Persian digits."""
    local = dt_utc.astimezone(IRAN_TZ)
    j = jdatetime.datetime.fromgregorian(datetime=local)
    fmt = "%Y/%m/%d - %H:%M:%S" if with_time else "%Y/%m/%d"
    return to_persian_digits(j.strftime(fmt))
