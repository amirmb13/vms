// Main window — global RTL mirroring, Farsi UI, 60fps scene-graph rendering.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1600
    height: 900
    title: "سامانه مدیریت تصاویر نظارتی"
    color: "#101418"

    // Mandatory global RTL mirroring — every child inherits.
    LayoutMirroring.enabled: true
    LayoutMirroring.childrenInherit: true

    font.family: "Vazirmatn"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: "پخش زنده"
                font.bold: true
                Layout.leftMargin: 16
            }
            ToolButton { text: "بازبینی" }
            ToolButton { text: "جستجوی پیشرفته" }
            ToolButton { text: "نقشه هوشمند" }
            Item { Layout.fillWidth: true }
            ToolButton { text: "تنظیمات سرور" }

            // Live Shamsi clock (UTC internally; Shamsi at the edge).
            Label {
                id: clockLabel
                Layout.rightMargin: 16
                text: shamsi.nowShamsi()
                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: clockLabel.text = shamsi.nowShamsi()
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Camera directory tree (flows right-to-left; tree is on the right)
        CameraTree {
            id: cameraTree
            Layout.preferredWidth: 280
            Layout.fillHeight: true

            // Double-clicked camera lands in the focused grid cell.
            onCameraActivated: (cameraUuid, nameFa) =>
                gridEngine.assignCameraToFocusedCell(cameraUuid)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Hyper-customizable grid engine applying GridLayout.layout_json
            GridEngine {
                id: gridEngine
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // Master sync-playback timeline (broadcasts NTP timestamps)
            TimelineBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 88
            }
        }
    }
}
