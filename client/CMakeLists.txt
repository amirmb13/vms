# =============================================================================
# VMS Windows Desktop Client — C++ & Qt Quick (QML) on RHI (D3D12/Vulkan)
# Farsi/RTL end-to-end, Vazirmatn typography, Shamsi timestamps.
# =============================================================================
cmake_minimum_required(VERSION 3.24)
project(vms_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)

qt_standard_project_setup(REQUIRES 6.6)

qt_add_executable(vms_client
    src/main.cpp
    src/grid/grid_model.cpp          # QAbstractListModel grid engine
    src/models/camera_tree_model.cpp # QAbstractItemModel RTL directory tree
    src/stream/stream_controller.cpp # adaptive profile switching
    src/stream/video_decoder.cpp     # FFmpeg HW-accel decode -> QSGTexture
    src/playback/sync_playback.cpp   # master NTP timeline broadcast
    src/playback/pip_worker.cpp      # independent PiP decode threads
    src/i18n/shamsi_formatter.cpp    # Shamsi + Persian digits QML helper
)

qt_add_qml_module(vms_client
    URI Vms.Client
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/GridEngine.qml
        qml/VideoCell.qml
        qml/TimelineBar.qml
        qml/CameraTree.qml
        qml/SmartMap.qml
    RESOURCES
        resources/fonts/Vazirmatn-Regular.ttf
        resources/fonts/Vazirmatn-Bold.ttf
)

target_include_directories(vms_client PRIVATE src)
target_link_libraries(vms_client PRIVATE
    Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network
    PkgConfig::FFMPEG)

# Force the RHI backend: DirectX 12 on Windows, Vulkan elsewhere
# (also set at runtime via QSG_RHI_BACKEND / QQuickWindow::setGraphicsApi)
if (WIN32)
    target_compile_definitions(vms_client PRIVATE VMS_RHI_D3D12=1)
else()
    target_compile_definitions(vms_client PRIVATE VMS_RHI_VULKAN=1)
endif()
