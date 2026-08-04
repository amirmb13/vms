"""
VMS Control Plane — Django settings.
Single source of truth for users, RBAC, cameras, layouts, audit and forensic
metadata. Data-plane work (video) is NEVER done here.
"""
import os
from datetime import timedelta
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent

SECRET_KEY = os.environ.get("DJANGO_SECRET_KEY", "insecure-dev-only-change-me")
DEBUG = os.environ.get("DJANGO_DEBUG", "1") == "1"
ALLOWED_HOSTS = os.environ.get("DJANGO_ALLOWED_HOSTS", "*").split(",")

INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.staticfiles",
    # 3rd party
    "rest_framework",
    "rest_framework_simplejwt",
    "django_filters",
    # VMS apps
    "apps.accounts",
    "apps.cameras",
    "apps.layouts",
    "apps.audit",
    "apps.forensic",
    "apps.orchestrator",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    # Serves admin static files (CSS/JS) under gunicorn without nginx.
    "whitenoise.middleware.WhiteNoiseMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "django.middleware.locale.LocaleMiddleware",  # Farsi localization
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
]

ROOT_URLCONF = "config.urls"
WSGI_APPLICATION = "config.wsgi.application"

TEMPLATES = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [BASE_DIR / "templates"],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
            ],
        },
    },
]

# --- PostgreSQL (provisioned via docker/docker-compose.yml) -----------------
DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.postgresql",
        "NAME": os.environ.get("POSTGRES_DB", "vms"),
        "USER": os.environ.get("POSTGRES_USER", "vms"),
        "PASSWORD": os.environ.get("POSTGRES_PASSWORD", "vms"),
        "HOST": os.environ.get("POSTGRES_HOST", "localhost"),
        "PORT": os.environ.get("POSTGRES_PORT", "5432"),
        "CONN_MAX_AGE": 60,
    }
}

AUTH_USER_MODEL = "accounts.User"

# --- Auth: stateless JWT for Qt client + optional Active Directory (LDAP) ---
REST_FRAMEWORK = {
    "DEFAULT_AUTHENTICATION_CLASSES": (
        "rest_framework_simplejwt.authentication.JWTAuthentication",
    ),
    "DEFAULT_PERMISSION_CLASSES": ("rest_framework.permissions.IsAuthenticated",),
    "DEFAULT_FILTER_BACKENDS": ("django_filters.rest_framework.DjangoFilterBackend",),
    # Enterprise scale: 10k+ cameras must never be serialized in one response.
    # Clients (Qt CameraTreeModel) follow the `next` link until exhausted.
    "DEFAULT_PAGINATION_CLASS": "rest_framework.pagination.PageNumberPagination",
    "PAGE_SIZE": 1000,
    # All API errors are rendered in Farsi via the custom exception handler.
    "EXCEPTION_HANDLER": "config.exceptions.farsi_exception_handler",
}

SIMPLE_JWT = {
    "ACCESS_TOKEN_LIFETIME": timedelta(minutes=30),
    "REFRESH_TOKEN_LIFETIME": timedelta(days=1),
    "ROTATE_REFRESH_TOKENS": True,
}

# Microsoft Active Directory (django-auth-ldap) — enabled when AD_LDAP_URI set
if os.environ.get("AD_LDAP_URI"):
    AUTHENTICATION_BACKENDS = [
        "django_auth_ldap.backend.LDAPBackend",
        "django.contrib.auth.backends.ModelBackend",
    ]
    AUTH_LDAP_SERVER_URI = os.environ["AD_LDAP_URI"]
    AUTH_LDAP_BIND_DN = os.environ.get("AD_BIND_DN", "")
    AUTH_LDAP_BIND_PASSWORD = os.environ.get("AD_BIND_PASSWORD", "")

# --- Redis Pub/Sub used by the orchestrator to signal C++ servers -----------
REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379/0")
CONTROL_SIGNAL_CHANNEL = "vms:control-signals"

# gRPC endpoints of registered C++ Recording Servers (fallback direct push)
RECORDING_SERVER_GRPC_TIMEOUT_S = 3.0

# ControlPlaneEvents gRPC server (RegisterServer / ReportEvent) — served by
# `python manage.py run_control_grpc` (docker service: django-grpc).
CONTROL_PLANE_GRPC_PORT = int(os.environ.get("CONTROL_PLANE_GRPC_PORT", "50060"))
# Well-known RecordingServerControl port every C++ node listens on.
RECORDING_SERVER_CONTROL_PORT = int(
    os.environ.get("RECORDING_SERVER_CONTROL_PORT", "50051")
)

# ArcFace identity matching: minimum cosine similarity for a watchlist hit.
FACE_MATCH_THRESHOLD = float(os.environ.get("FACE_MATCH_THRESHOLD", "0.45"))

# --- Mandatory Farsi localization -------------------------------------------
LANGUAGE_CODE = "fa"
LANGUAGES = [("fa", "فارسی"), ("en", "English")]
LOCALE_PATHS = [BASE_DIR / "locale"]
USE_I18N = True

# Internal storage is ALWAYS UTC; Shamsi rendering happens at the edges
# (serializers / client) via jdatetime.
TIME_ZONE = "UTC"
USE_TZ = True

STATIC_URL = "static/"
# Project-level static assets (custom admin theme lives here).
STATICFILES_DIRS = [BASE_DIR / "static"]
# collectstatic target — WhiteNoise serves from here under gunicorn.
STATIC_ROOT = BASE_DIR / "staticfiles"
STORAGES = {
    "default": {"BACKEND": "django.core.files.storage.FileSystemStorage"},
    "staticfiles": {
        # Compressed (gzip/brotli) static serving without nginx.
        "BACKEND": "whitenoise.storage.CompressedStaticFilesStorage",
    },
}

DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"

# Four-Eyes Authorization: operations listed here require two distinct
# administrator cryptographic approvals before execution.
FOUR_EYES_PROTECTED_OPERATIONS = [
    "archive.prune",
    "camera.bulk_delete",
    "user.role_escalation",
]
