// =============================================================================
// VideoCell — one video surface inside the grid engine.
//
// - VideoOutput renders decoder frames as QSGTexture on the RHI scene graph.
// - Adaptive profile: sub/mid selected by cell size; double-click goes
//   fullscreen and switches to the 4K main stream via I-frame alignment.
// - Shamsi timestamp overlay (UTC internally, Shamsi at the edge).
// - PiP: long-press opens an independent archive review worker in a corner
//   window while the live stream continues underneath.
// =============================================================================
import QtQuick
import QtQuick.Controls
import QtMultimedia
import Vms.Client

Item {
    id: cell

    // Set by GridEngine's Loader.
    property string cameraUuid: ""
    property string preferredProfile: "sub"
    property int cellIndex: -1
    property bool fullscreen: false

    // Last presented frame's NTP timestamp (for the Shamsi overlay).
    property var lastFrameUtcUs: 0

    onCameraUuidChanged: reattach()
    onPreferredProfileChanged:
        if (cameraUuid.length > 0)
            streamController.switchProfile(cellIndex, preferredProfile)

    function reattach() {
        if (cameraUuid.length > 0)
            streamController.attachLive(cellIndex, cameraUuid,
                                        preferredProfile, videoOutput.videoSink)
    }

    Connections {
        target: streamController
        function onCellFramePresented(idx, utcUs) {
            if (idx === cell.cellIndex)
                cell.lastFrameUtcUs = utcUs
        }
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    // --- Shamsi timestamp overlay (bottom-right in RTL context) --------------
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 6
        color: "#000000"
        opacity: 0.65
        radius: 3
        width: overlayLabel.implicitWidth + 12
        height: overlayLabel.implicitHeight + 6

        Label {
            id: overlayLabel
            anchors.centerIn: parent
            color: "#e5e7eb"
            font.pixelSize: 11
            font.family: "Vazirmatn"
            text: cell.lastFrameUtcUs > 0
                  ? shamsi.toShamsi(cell.lastFrameUtcUs)
                  : shamsi.nowShamsi()
        }
    }

    // --- Fullscreen / main-stream on double-click ------------------------------
    TapHandler {
        gesturePolicy: TapHandler.WithinBounds
        onDoubleTapped: {
            cell.fullscreen = !cell.fullscreen
            // Seamless I-frame-aligned switch: 4K main when fullscreen,
            // back to the size-appropriate profile when restored.
            streamController.switchProfile(
                cell.cellIndex,
                cell.fullscreen ? "main" : cell.preferredProfile)
        }
        onLongPressed: pipPopup.openAt(syncPlayback.positionUtcUs)
    }

    // --- Picture-in-Picture: independent archive review worker -----------------
    PipWorker { id: pipWorker }

    Rectangle {
        id: pipPopup
        visible: pipWorker.active
        width: Math.max(parent.width * 0.35, 220)
        height: width * 9 / 16 + 28
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 8
        color: "#0b0e12"
        border.color: "#2f81f7"
        border.width: 1
        radius: 4
        z: 10

        function openAt(utcUs) {
            // Default review start: one minute before "now"/current position.
            var start = utcUs > 60000000 ? utcUs - 60000000 : utcUs
            pipWorker.open(cell.cameraUuid, start, pipVideo.videoSink)
        }

        Column {
            anchors.fill: parent
            anchors.margins: 2

            Row {
                width: parent.width
                height: 24
                spacing: 4
                layoutDirection: Qt.RightToLeft

                Label {
                    text: "بازبینی"
                    color: "#9ca3af"
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: pipWorker.positionUtcUs > 0
                          ? shamsi.toShamsiShort(pipWorker.positionUtcUs) : ""
                    color: "#e5e7eb"
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
                Item { width: parent.width - 140; height: 1 }
                ToolButton {
                    text: "بستن"
                    font.pixelSize: 10
                    onClicked: pipWorker.close()
                }
            }

            VideoOutput {
                id: pipVideo
                width: parent.width
                height: parent.height - 28
                fillMode: VideoOutput.PreserveAspectFit
            }
        }
    }

    Component.onCompleted: reattach()
    Component.onDestruction: {
        pipWorker.close()
        if (cellIndex >= 0)
            streamController.detach(cellIndex)
    }
}
