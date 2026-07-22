from rest_framework.routers import DefaultRouter

from .views import CameraGroupViewSet, CameraViewSet, RecordingServerViewSet

router = DefaultRouter()
router.register("cameras", CameraViewSet, basename="camera")
router.register("camera-groups", CameraGroupViewSet, basename="camera-group")
router.register("recording-servers", RecordingServerViewSet, basename="recording-server")

urlpatterns = router.urls
