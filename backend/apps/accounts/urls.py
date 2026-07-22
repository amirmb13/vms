from rest_framework.routers import DefaultRouter

from .views import (
    FourEyesRequestViewSet,
    RoleAssignmentViewSet,
    RoleViewSet,
    UserViewSet,
)

router = DefaultRouter()
router.register("users", UserViewSet, basename="user")
router.register("roles", RoleViewSet, basename="role")
router.register("role-assignments", RoleAssignmentViewSet, basename="role-assignment")
router.register("four-eyes", FourEyesRequestViewSet, basename="four-eyes")

urlpatterns = router.urls
