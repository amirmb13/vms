"""Minimal ONVIF (WS-Discovery-less) SOAP client for camera auto-detection.

Implements exactly the three calls the admin needs to configure a camera
from just its IP + credentials:

  GetDeviceInformation  -> manufacturer / model / firmware
  GetProfiles           -> list of media profiles (with resolutions)
  GetStreamUri          -> real RTSP URLs for the best profile per tier

No third-party dependency: plain urllib + xml.etree. Kept deliberately small;
for advanced management use a full ONVIF client.
"""
from __future__ import annotations

import base64
import hashlib
import secrets
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

WSES = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd"
WSSU = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"
WSTU = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0"
SOAP_ENV = "http://www.w3.org/2003/05/soap-envelope"
XSI = "http://www.w3.org/2001/XMLSchema-instance"
XSD = "http://www.w3.org/2001/XMLSchema"

DEVICE_NS = "http://www.onvif.org/ver10/device/wsdl"
MEDIA_NS = "http://www.onvif.org/ver10/media/wsdl"

_NS = {
    "s": SOAP_ENV,
    "a": "http://www.w3.org/2005/08/addressing",
    "tds": DEVICE_NS,
    "trt": MEDIA_NS,
    "wsse": WSES,
    "wsu": WSSU,
    "xsi": XSI,
    "xsd": XSD,
}


class OnvifError(Exception):
    """Raised when a camera is unreachable or answers with an error."""


def _q(ns_prefix: str, tag: str) -> str:
    return f"{{{_NS[ns_prefix]}}}{tag}"


def _local(el: ET.Element, tag: str) -> ET.Element | None:
    """Find the first descendant (or self) whose local tag matches."""
    local = tag.rsplit("}", 1)[-1]
    for node in el.iter():
        if node.tag.rsplit("}", 1)[-1] == local:
            return node
    return None


def _envelope(action: str, body: str, username: str, password: str) -> str:
    """Build a WS-UsernameToken (PasswordDigest) secured SOAP envelope."""
    nonce_raw = secrets.token_bytes(16)
    nonce_b64 = base64.b64encode(nonce_raw).decode("ascii")
    created = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    digest = base64.b64encode(
        hashlib.sha1(nonce_raw + created.encode("ascii") + password.encode("utf-8")).digest()
    ).decode("ascii")

    return f"""<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="{SOAP_ENV}" xmlns:a="http://www.w3.org/2005/08/addressing"
 xmlns:tds="{DEVICE_NS}" xmlns:trt="{MEDIA_NS}" xmlns:wsse="{WSES}" xmlns:wsu="{WSSU}">
<s:Header>
 <a:Action s:mustUnderstand="1">{action}</a:Action>
 <wsse:Security s:mustUnderstand="1">
  <wsse:UsernameToken wsu:Id="vms-token">
   <wsse:Username>{username}</wsse:Username>
   <wsse:Password Type="{WSTU}#PasswordDigest">{digest}</wsse:Password>
   <wsse:Nonce EncodingType="{WSTU}#Base64Binary">{nonce_b64}</wsse:Nonce>
   <wsu:Created>{created}</wsu:Created>
  </wsse:UsernameToken>
 </wsse:Security>
</s:Header>
<s:Body xmlns:xsi="{XSI}" xmlns:xsd="{XSD}">
{body}
</s:Body>
</s:Envelope>"""


class OnvifCamera:
    """A tiny ONVIF device client over HTTP SOAP."""

    def __init__(self, host: str, username: str, password: str,
                 port: int = 80, timeout: float = 3.0,
                 device_service: str | None = None):
        self.host = host
        self.username = username
        self.password = password
        self.timeout = timeout
        self.device_service = device_service or f"http://{host}:{port}/onvif/device_service"

    # -- low-level SOAP -------------------------------------------------------

    def _call(self, service_url: str, body: str, action: str) -> ET.Element:
        envelope = _envelope(action, body, self.username, self.password)
        req = urllib.request.Request(
            service_url,
            data=envelope.encode("utf-8"),
            headers={
                "Content-Type": 'application/soap+xml; charset=utf-8',
                "SOAPAction": action,
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = resp.read()
        except urllib.error.HTTPError as exc:
            # Some cameras answer faults with HTTP 200; HTTP errors still
            # carry a body we can parse for a Fault message.
            payload = exc.read() or b""
        except (urllib.error.URLError, TimeoutError, ConnectionError) as exc:
            raise OnvifError(f"اتصال به دوربین برقرار نشد ({self.host}).") from exc

        try:
            root = ET.fromstring(payload)
        except ET.ParseError as exc:
            raise OnvifError(
                f"پاسخ دوربین قابل خواندن نبود — احتمالاً دوربین ONVIF را پشتیبانی نمی‌کند."
            ) from exc

        fault = root.find(f".//{_q('s', 'Fault')}")
        if fault is not None:
            reason = fault.find(f"{_q('s', 'Reason')}//{_q('s', 'Text')}")
            msg = reason.text if reason is not None and reason.text else "خطای ناشناخته"
            raise OnvifError(f"دوربین خطا داد: {msg}")
        return root

    def _device(self, body: str, action: str) -> ET.Element:
        return self._call(self.device_service, body, action)

    # -- device ---------------------------------------------------------------

    def get_device_information(self) -> dict:
        root = self._device("<tds:GetDeviceInformation/>",
                            "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation")
        info = root.find(f".//{_q('tds', 'GetDeviceInformationResponse')}")
        if info is None:
            raise OnvifError("پاسخ دستگاه خوانده نشد.")
        pick = lambda tag: (info.findtext(f"{_q('tds', tag)}") or "").strip()
        return {
            "manufacturer": pick("Manufacturer"),
            "model": pick("Model"),
            "firmware": pick("FirmwareVersion"),
            "serial": pick("SerialNumber"),
        }

    def get_media_service_url(self) -> str:
        """Locate the media XAddr from GetCapabilities (fallback standard path)."""
        root = self._device("<tds:GetCapabilities><tds:Category>All</tds:Category></tds:GetCapabilities>",
                            "http://www.onvif.org/ver10/device/wsdl/GetCapabilities")
        xaddr = _local(root, "XAddr")
        return (xaddr.text or "").strip() if xaddr is not None else self._default_media_url()

    def _default_media_url(self) -> str:
        from urllib.parse import urlsplit, urlunsplit
        parts = urlsplit(self.device_service)
        base = urlunsplit((parts.scheme, parts.netloc, "/onvif/media_service", "", ""))
        return base

    def get_profiles(self, media_url: str) -> list[dict]:
        """Return [{token, width, height}] for every media profile."""
        body = "<trt:GetProfiles/>"
        root = self._call(media_url, body,
                          "http://www.onvif.org/ver10/media/wsdl/GetProfiles")
        profiles = []
        for prof in root.findall(f".//{_q('trt', 'Profiles')}"):
            token = prof.get("token") or ""
            res = _local(prof, "Resolution")
            width_el = _local(res, "Width") if res is not None else None
            height_el = _local(res, "Height") if res is not None else None
            width = int(width_el.text) if width_el is not None and width_el.text else 0
            height = int(height_el.text) if height_el is not None and height_el.text else 0
            profiles.append({"token": token, "width": width, "height": height})
        return profiles

    def get_stream_uri(self, media_url: str, profile_token: str) -> str:
        body = (
            f"<trt:GetStreamUri>"
            f"<trt:StreamSetup><trt:Stream>RTP-Unicast</trt:Stream>"
            f"<trt:Transport><trt:Protocol>RTSP</trt:Protocol></trt:Transport></trt:StreamSetup>"
            f"<trt:ProfileToken>{profile_token}</trt:ProfileToken>"
            f"</trt:GetStreamUri>"
        )
        root = self._call(media_url, body,
                          "http://www.onvif.org/ver10/media/wsdl/GetStreamUri")
        uri = root.findtext(f".//{_q('trt', 'Uri')}")
        return (uri or "").strip()

    # -- high-level -----------------------------------------------------------

    def detect_stream_urls(self) -> dict:
        """Best-effort RTSP URLs, tiered by resolution.

        main = highest-resolution profile, sub = lowest, mid = middle.
        Returns {main, mid, sub} with empty strings for missing profiles.
        """
        media_url = self.get_media_service_url()
        profiles = self.get_profiles(media_url)
        if not profiles:
            return {"main": "", "mid": "", "sub": ""}

        ranked = sorted(profiles, key=lambda p: p["width"] * p["height"], reverse=True)
        tiers = {"main": None, "mid": None, "sub": None}
        if len(ranked) == 1:
            tiers["main"] = ranked[0]
        elif len(ranked) == 2:
            tiers["main"], tiers["sub"] = ranked
        else:
            tiers["main"], tiers["mid"], tiers["sub"] = ranked[0], ranked[1], ranked[-1]

        urls = {"main": "", "mid": "", "sub": ""}
        for tier, prof in tiers.items():
            if prof is None:
                continue
            try:
                urls[tier] = self.get_stream_uri(media_url, prof["token"])
            except OnvifError:
                continue
        return urls
