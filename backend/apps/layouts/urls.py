from rest_framework.routers import DefaultRouter

from .views import GridLayoutViewSet

router = DefaultRouter()
router.register("layouts", GridLayoutViewSet, basename="layout")

urlpatterns = router.urls
