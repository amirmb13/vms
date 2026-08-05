"""Camera auto-configuration service.

The operator only enters IP + credentials + brand; this module figures out
the rest the way mainstream VMS do:

  1. Try ONVIF: query device info and ask the camera for its own stream URLs.
  2. Fall back to the brand's standard RTSP layout when the camera is
     unreachable or has ONVIF disabled.
"""
from __future__ import annotations

import logging

from .brand_profiles import (
    brand_onvif_port,
    build_brand_rtsp_urls,
    default_onvif_endpoint,
)
from .onvif import OnvifCamera, OnvifError

logger = logging.getLogger("vms.cameras.services")

ONVIF_DETECT_TIMEOUT_S = 4.0


def auto_detect_camera(*, ip: str, username: str, password: str,
                       brand: str, onvif_endpoint: str = "") -> dict:
    """Return a config dict for a camera; never raises.

    Result keys: method, device_info, onvif_endpoint, rtsp_main_url,
    rtsp_mid_url, rtsp_sub_url, codec, warnings (list of Farsi strings).
    """
    warnings: list[str] = []
    device_info: dict = {}
    method = "brand"
    codec = "h265"

    if not ip.strip():
        return {
            "method": None, "device_info": {}, "onvif_endpoint": "",
            "rtsp_main_url": "", "rtsp_mid_url": "", "rtsp_sub_url": "",
            "codec": "h265",
            "warnings": ["آدرس IP دوربین را وارد کنید."],
        }

    ip = ip.strip()
    endpoint = onvif_endpoint.strip() or default_onvif_endpoint(ip, brand_onvif_port(brand))
    rtsp_urls = {"main": "", "mid": "", "sub": ""}

    # --- 1. ONVIF (best quality, uses real camera-reported URLs) --------------
    try:
        cam = OnvifCamera(
            ip, username, password,
            port=brand_onvif_port(brand),
            device_service=endpoint,
            timeout=ONVIF_DETECT_TIMEOUT_S,
        )
        device_info = cam.get_device_information()
        rtsp_urls = cam.detect_stream_urls()
        method = "onvif"
        if not any(rtsp_urls.values()):
            warnings.append(
                "دوربین به ONVIF پاسخ داد اما URL استریم برنگرداند؛ "
                "از الگوی استاندارد برند استفاده شد."
            )
    except OnvifError as exc:
        logger.info("[detect] ONVIF unavailable for %s: %s", ip, exc)
        warnings.append(f"تشخیص ONVIF ممکن نشد ({exc})")

    # --- 2. Brand fallback for any tier ONVIF did not fill --------------------
    guessed = build_brand_rtsp_urls(ip=ip, brand=brand, username=username,
                                    password=password)
    for tier in ("main", "mid", "sub"):
        if not rtsp_urls[tier] and guessed.get(tier):
            rtsp_urls[tier] = guessed[tier]

    # Brands with no well-known RTSP path (generic ONVIF, Iranian brands,
    # custom) have nothing to guess — if ONVIF also failed, guide the operator.
    has_guessed = any(guessed.values())
    if not has_guessed and not any(rtsp_urls.values()) and not device_info:
        warnings.append(
            "دوربین قابل تشخیص نبود. اگر دوربین از ONVIF پشتیبانی می‌کند "
            "آدرس IP و نام کاربری/رمز را بررسی کنید، یا نشانی جریان را دستی "
            "وارد کنید."
        )
        method = None
    elif method == "brand" and has_guessed:
        warnings.append(
            "URL استریم از الگوی استاندارد برند ساخته شد؛ "
            "اگر دوربین قابل دسترسی بود آن را بررسی کنید."
        )

    return {
        "method": method,
        "device_info": device_info,
        "onvif_endpoint": endpoint,
        "rtsp_main_url": rtsp_urls["main"],
        "rtsp_mid_url": rtsp_urls["mid"],
        "rtsp_sub_url": rtsp_urls["sub"],
        "codec": codec,
        "warnings": warnings,
    }
