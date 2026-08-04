// =============================================================================
// SmartMap — interactive GIS floor plan (mandate §5).
//
// - Renders an offline floor plan (exported AutoCAD raster / OSM tile image).
// - Camera markers carry a directional Field-of-View cone (Canvas-drawn).
// - Clicking a marker opens a floating live video popup fed by the relay.
// - Marker positions come from Camera.map_x / map_y / fov_direction /
//   fov_angle fields in the Django Control Plane.
// =============================================================================
import QtQuick
import QtQuick.Controls
import QtMultimedia
import Vms.Client

Rectangle {
    id: map
    color: Theme.bg

    // Injected from Django: [{camera_uuid, name_fa, map_x, map_y,
    //                          fov_direction, fov_angle}, ...] (0..1 coords)
    property var cameras: []
    property url floorPlanSource: ""

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: plan.width * plan.scale
        contentHeight: plan.height * plan.scale
        clip: true

        Image {
            id: plan
            source: map.floorPlanSource
            transformOrigin: Item.TopLeft

            // Wheel zoom (0.5x .. 4x) — offline plans stay crisp.
            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                onWheel: (event) => {
                    var s = plan.scale * (event.angleDelta.y > 0 ? 1.15 : 0.87)
                    plan.scale = Math.max(0.5, Math.min(4, s))
                }
            }

            // --- Camera markers + FOV cones --------------------------------
            Repeater {
                model: map.cameras

                delegate: Item {
                    required property var modelData
                    x: modelData.map_x * plan.width
                    y: modelData.map_y * plan.height

                    // Directional FOV cone.
                    Canvas {
                        id: cone
                        width: 120; height: 120
                        x: -60; y: -60
                        opacity: 0.45
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var dir = (modelData.fov_direction || 0) *
                                      Math.PI / 180
                            var half = ((modelData.fov_angle || 60) / 2) *
                                       Math.PI / 180
                            ctx.beginPath()
                            ctx.moveTo(60, 60)
                            ctx.arc(60, 60, 55, dir - half, dir + half)
                            ctx.closePath()
                            ctx.fillStyle = String(Theme.accent)
                            ctx.fill()
                        }
                    }

                    // Marker dot.
                    Rectangle {
                        width: 14; height: 14; radius: 7
                        x: -7; y: -7
                        color: Theme.accent
                        border.color: Theme.text
                        border.width: 2

                        TapHandler {
                            onTapped: {
                                popup.cameraUuid = modelData.camera_uuid
                                popup.nameFa = modelData.name_fa
                                popup.open()
                            }
                        }
                    }

                    // Farsi camera label.
                    Label {
                        y: 10
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.name_fa
                        color: Theme.text
                        font.pixelSize: 11
                        font.family: "Vazirmatn"
                    }
                }
            }
        }
    }

    // --- Floating live video popup --------------------------------------------
    Popup {
        id: popup
        property string cameraUuid: ""
        property string nameFa: ""

        width: 420
        height: 280
        x: (map.width - width) / 2
        y: (map.height - height) / 2
        modal: false
        closePolicy: Popup.CloseOnEscape

        // Map popups use a reserved high cell index range so they never
        // collide with grid cell sessions.
        readonly property int popupCellIndex: 10000

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            radius: Theme.radius
        }

        contentItem: Column {
            spacing: 6

            Row {
                width: parent.width
                layoutDirection: Qt.RightToLeft
                spacing: 8

                Label {
                    text: popup.nameFa
                    color: Theme.text
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Item { width: parent.width - 150; height: 1 }
                UiButton {
                    text: "بستن"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: popup.close()
                }
            }

            VideoOutput {
                id: popupVideo
                width: parent.width
                height: parent.height - 36
                fillMode: VideoOutput.PreserveAspectFit
            }
        }

        onOpened: streamController.attachLive(
                      popupCellIndex, cameraUuid, "mid", popupVideo.videoSink)
        onClosed: streamController.detach(popupCellIndex)
    }

    // Empty-state hint.
    Label {
        anchors.centerIn: parent
        visible: map.floorPlanSource.toString().length === 0
        text: "نقشه‌ای بارگذاری نشده است — از تنظیمات سرور، نقشه طبقه را بارگذاری کنید"
        color: "#4b5563"
        font.pixelSize: 13
    }
}
