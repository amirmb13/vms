from django.urls import path
from rest_framework.routers import DefaultRouter

from .views import (
    DetectionIngestView,
    ForensicSearchView,
    IdentityViewSet,
    SynopsisJobViewSet,
)

router = DefaultRouter()
router.register("identities", IdentityViewSet, basename="identity")
router.register("synopsis", SynopsisJobViewSet, basename="synopsis")

urlpatterns = router.urls + [
    path("forensic/search", ForensicSearchView.as_view(), name="forensic-search"),
    path("forensic/ingest", DetectionIngestView.as_view(), name="forensic-ingest"),
]
