// Main window — global RTL mirroring, Farsi UI, 60fps scene-graph rendering.
// All chrome is custom-drawn from the Theme singleton — no platform-styled
// controls, so colors are deterministic on every OS (no white-on-white).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vms.Client

ApplicationWindow {
    id: root
    visible: true
    width: 1600
    height: 900
    title: "سامانه مدیریت تصاویر نظارتی"
    color: Theme.bg

    // Mandatory global RTL mirroring — every child inherits.
    LayoutMirroring.enabled: true
    LayoutMirroring.childrenInherit: true

    font.family: "Vazirmatn"

    // Current nav section (only "live" is implemented today; the active
    // state keeps the header honest instead of five identical dead buttons).
    property string currentView: "live"

    header: Rectangle {
        height: 50
        color: Theme.surface

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.borderSoft
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 6

            // Brand mark + product name
            Rectangle {
                width: 26; height: 26
                radius: Theme.radiusSm
                color: Theme.accentSoft
                border.width: 1
                border.color: Qt.alpha(Theme.accent, 0.4)

                Text {
                    anchors.centerIn: parent
                    text: "\u25a3"          // ▣ — video-wall mark
                    color: Theme.accent
                    font.pixelSize: 13
                }
            }

            Label {
                text: "سامانه نظارت تصویری"
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
                Layout.rightMargin: 10
            }

            Rectangle { width: 1; height: 22; color: Theme.border }

            UiButton {
                text: "پخش زنده"
                active: root.currentView === "live"
                onClicked: root.currentView = "live"
            }
            UiButton { text: "بازبینی" }
            UiButton { text: "جستجوی پیشرفته" }
            UiButton { text: "نقشه هوشمند" }

            Item { Layout.fillWidth: true }

            UiButton { text: "تنظیمات سرور" }

            // Live Shamsi clock chip (UTC internally; Shamsi at the edge).
            Rectangle {
                width: clockLabel.implicitWidth + 22
                height: 28
                radius: Theme.radiusSm
                color: Theme.surface2
                border.width: 1
                border.color: Theme.borderSoft

                Label {
                    id: clockLabel
                    anchors.centerIn: parent
                    text: shamsi.nowShamsi()
                    color: Theme.textDim
                    font.pixelSize: 12

                    Timer {
                        interval: 1000; running: true; repeat: true
                        onTriggered: clockLabel.text = shamsi.nowShamsi()
                    }
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
                Layout.preferredHeight: 92
            }
        }
    }
}
