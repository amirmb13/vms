from django.contrib import admin
from django.urls import include, path
from rest_framework_simplejwt.views import TokenObtainPairView, TokenRefreshView

urlpatterns = [
    path("admin/", admin.site.urls),
    # Stateless JWT auth for the Qt desktop client
    path("api/auth/token/", TokenObtainPairView.as_view(), name="token_obtain"),
    path("api/auth/token/refresh/", TokenRefreshView.as_view(), name="token_refresh"),
    # VMS Control Plane REST APIs
    path("api/", include("apps.cameras.urls")),
    path("api/", include("apps.accounts.urls")),
    path("api/", include("apps.layouts.urls")),
    path("api/", include("apps.audit.urls")),
    path("api/", include("apps.forensic.urls")),
]

admin.site.site_header = "سامانه مدیریت تصاویر نظارتی"
admin.site.site_title = "مدیریت VMS"
admin.site.index_title = "پنل مدیریت"
