"""Brand profiles: standard RTSP URL layouts per camera manufacturer.

Used as a fallback when ONVIF auto-detection cannot reach the camera
(offline, ONVIF disabled, blocked port, ...). The operator only enters the
camera IP and brand; these templates produce sensible stream URLs that the
vast majority of cameras accept without extra configuration.
"""
from __future__ import annotations

from urllib.parse import quote

# Default ONVIF device-service port per brand (most use 80).
DEFAULT_ONVIF_PORT = 80
DEFAULT_RTSP_PORT = 554


def _auth(user: str, password: str) -> str:
    """Embed credentials into an RTSP URL (percent-encoded, no @ conflicts)."""
    return f"{quote(user, safe='')}:{quote(password, safe='')}@"


def _base(ip: str, user: str, password: str, rtsp_port: int) -> str:
    return f"rtsp://{_auth(user, password)}{ip}:{rtsp_port}"


def _hikvision(ip, user, password, rtsp_port, **_):
    """Hikvision: channels 101 (main) / 102 (sub) / 103 (mid)."""
    b = _base(ip, user, password, rtsp_port)
    return {
        "main": f"{b}/Streaming/Channels/101",
        "sub": f"{b}/Streaming/Channels/102",
        "mid": f"{b}/Streaming/Channels/103",
    }


def _dahua(ip, user, password, rtsp_port, **_):
    """Dahua: realmonitor with subtype 0/1/2."""
    b = _base(ip, user, password, rtsp_port)
    return {
        "main": f"{b}/cam/realmonitor?channel=1&subtype=0",
        "sub": f"{b}/cam/realmonitor?channel=1&subtype=1",
        "mid": f"{b}/cam/realmonitor?channel=1&subtype=2",
    }


def _uniview(ip, user, password, rtsp_port, **_):
    """Uniview: unicast c1/s0 (main) s1 (sub) s2 (mid)."""
    b = _base(ip, user, password, rtsp_port)
    return {
        "main": f"{b}/unicast/c1/s0/live",
        "sub": f"{b}/unicast/c1/s1/live",
        "mid": f"{b}/unicast/c1/s2/live",
    }


def _axis(ip, user, password, rtsp_port, **_):
    """Axis: media.amp; single H.264 stream, sub via resolution override."""
    b = _base(ip, user, password, rtsp_port)
    return {
        "main": f"{b}/axis-media/media.amp",
        "sub": f"{b}/axis-media/media.amp?resolution=320x240",
        "mid": f"{b}/axis-media/media.amp?resolution=720x576",
    }


def _generic(ip, user, password, rtsp_port, **_):
    """Standard ONVIF: no well-known path — leave blank, rely on ONVIF."""
    return {"main": "", "sub": "", "mid": ""}


def _custom(ip, user, password, rtsp_port, **_):
    return {"main": "", "sub": "", "mid": ""}


# brand -> (onvif port, rtsp builder callable)
#
# Iranian brands (aria/cambiz/...) are ONVIF-compliant and are auto-detected
# by querying the camera itself; there is no well-known universal RTSP path
# shared across them, so the fallback builder stays blank (the detection
# service then guides the operator to enter URLs manually if ONVIF fails).
IRANIAN_BRANDS = ("aria", "cambiz", "atal", "radin", "parsan", "pejvak",
                  "sana", "arman", "mersad")

BRAND_PROFILES = {
    "hikvision": (DEFAULT_ONVIF_PORT, _hikvision),
    "dahua": (DEFAULT_ONVIF_PORT, _dahua),
    "uniview": (DEFAULT_ONVIF_PORT, _uniview),
    "axis": (DEFAULT_ONVIF_PORT, _axis),
    "generic-onvif": (DEFAULT_ONVIF_PORT, _generic),
    "custom": (DEFAULT_ONVIF_PORT, _custom),
}
for _brand in IRANIAN_BRANDS:
    BRAND_PROFILES[_brand] = (DEFAULT_ONVIF_PORT, _generic)


def brand_onvif_port(brand: str) -> int:
    """Default ONVIF device-service port for a brand."""
    return BRAND_PROFILES.get(brand, (DEFAULT_ONVIF_PORT, None))[0]


def build_brand_rtsp_urls(*, ip: str, brand: str, username: str,
                          password: str, rtsp_port: int = DEFAULT_RTSP_PORT) -> dict:
    """Return {main, sub, mid} RTSP URLs guessed from the brand layout."""
    _, builder = BRAND_PROFILES.get(brand, (DEFAULT_ONVIF_PORT, _generic))
    return builder(ip, username, password, rtsp_port)


def default_onvif_endpoint(ip: str, port: int | None = None) -> str:
    """Standard ONVIF device-service address for an IP."""
    port = port or DEFAULT_ONVIF_PORT
    return f"http://{ip}:{port}/onvif/device_service"
