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

# -----------------------------------------------------------------------------
# 1. Qt6 Configuration & Setup
# -----------------------------------------------------------------------------
find_package(Qt6 6.6 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Network)
qt_standard_project_setup(REQUIRES 6.6)

# -----------------------------------------------------------------------------
# 2. FFmpeg Direct Configuration (Scoop Shared Build)
# -----------------------------------------------------------------------------
set(FFMPEG_DIR "C:/Users/Sahra/scoop/apps/ffmpeg-shared/current")

# معرفی فایل‌های lib برای کامپایلر
set(FFMPEG_LIBRARIES
    "${FFMPEG_DIR}/lib/avcodec.lib"
    "${FFMPEG_DIR}/lib/avformat.lib"
    "${FFMPEG_DIR}/lib/avutil.lib"
    "${FFMPEG_DIR}/lib/swscale.lib"
)

# -----------------------------------------------------------------------------
# 3. Executable & Source Files
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# 4. QML Module & Resources
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# 5. Include Directories & Link Libraries
# -----------------------------------------------------------------------------
target_include_directories(vms_client PRIVATE
    src
    "${FFMPEG_DIR}/include"
)

target_link_libraries(vms_client PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Network
    ${FFMPEG_LIBRARIES}
)

# -----------------------------------------------------------------------------
# 6. RHI Backend Definitions
# -----------------------------------------------------------------------------
if (WIN32)
    target_compile_definitions(vms_client PRIVATE VMS_RHI_D3D12=1)
else()
    target_compile_definitions(vms_client PRIVATE VMS_RHI_VULKAN=1)
endif()