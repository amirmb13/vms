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
        height: 52
        color: Theme.surface

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.borderSoft
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 4

            // Brand mark + product name
            Rectangle {
                width: 28; height: 28
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
                Layout.leftMargin: 6
            }

            Rectangle {
                width: 1; height: 22; color: Theme.borderSoft
                Layout.leftMargin: 6; Layout.rightMargin: 6
            }

            // Nav tabs — active tab carries a slim accent underline; the
            // fill stays quiet so the header doesn't compete with the wall.
            UiButton {
                text: "پخش زنده"
                underlineWhenActive: true
                active: root.currentView === "live"
                Layout.fillHeight: true
                onClicked: root.currentView = "live"
            }
            UiButton { text: "بازبینی"; underlineWhenActive: true; Layout.fillHeight: true }
            UiButton { text: "جستجوی پیشرفته"; underlineWhenActive: true; Layout.fillHeight: true }
            UiButton { text: "نقشه هوشمند"; underlineWhenActive: true; Layout.fillHeight: true }

            Item { Layout.fillWidth: true }

            UiButton { text: "تنظیمات سرور" }

            // Live Shamsi clock chip (UTC internally; Shamsi at the edge).
            Rectangle {
                width: clockRow.implicitWidth + 24
                height: 30
                radius: 15
                color: Theme.surface2
                border.width: 1
                border.color: Theme.borderSoft
                Layout.leftMargin: 6

                Row {
                    id: clockRow
                    anchors.centerIn: parent
                    spacing: 8
                    layoutDirection: Qt.RightToLeft

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6; height: 6; radius: 3
                        color: Theme.success
                    }

                    Label {
                        id: clockLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: shamsi.nowShamsi()
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSm

                        Timer {
                            interval: 1000; running: true; repeat: true
                            onTriggered: clockLabel.text = shamsi.nowShamsi()
                        }
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
