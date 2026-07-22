// =============================================================================
// TimelineBar — master Sync Playback timeline (mandate §3.B).
//
// The slider position is a UTC microsecond NTP timestamp inside the reviewed
// window; dragging broadcasts a synchronized seek to EVERY active decoding
// engine via SyncPlayback -> StreamController. Labels render in Shamsi with
// Persian digits; internal math stays pure UTC.
// =============================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    color: "#0d1117"
    border.color: "#232a33"
    border.width: 1

    readonly property bool hasWindow:
        syncPlayback.windowEndUtcUs > syncPlayback.windowStartUtcUs

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        // --- Transport controls + Shamsi position readout --------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ToolButton {
                text: "پخش زنده"
                font.pixelSize: 12
                onClicked: syncPlayback.jumpToLive()
            }

            ToolSeparator {}

            ToolButton {
                text: syncPlayback.playing ? "توقف" : "پخش"
                font.pixelSize: 12
                onClicked: syncPlayback.playing ? syncPlayback.pause()
                                                : syncPlayback.play()
            }
            ToolButton {
                text: "قاب قبلی"
                font.pixelSize: 12
                onClicked: syncPlayback.stepFrames(-1)
            }
            ToolButton {
                text: "قاب بعدی"
                font.pixelSize: 12
                onClicked: syncPlayback.stepFrames(1)
            }

            ComboBox {
                id: rateBox
                model: ["۰.۲۵×", "۰.۵×", "۱×", "۲×", "۴×", "۸×", "۱۶×"]
                property var rates: [0.25, 0.5, 1, 2, 4, 8, 16]
                currentIndex: 2
                font.pixelSize: 12
                implicitWidth: 84
                onActivated: (i) => syncPlayback.rate = rates[i]
            }

            Item { Layout.fillWidth: true }

            // Master position — full Shamsi stamp.
            Label {
                text: syncPlayback.positionUtcUs > 0
                      ? shamsi.toShamsi(syncPlayback.positionUtcUs)
                      : "—"
                color: "#e5e7eb"
                font.pixelSize: 13
                font.bold: true
            }
        }

        // --- Master timeline slider -------------------------------------------
        Slider {
            id: timeline
            Layout.fillWidth: true
            enabled: bar.hasWindow
            from: 0
            to: bar.hasWindow
                ? Number(syncPlayback.windowEndUtcUs -
                         syncPlayback.windowStartUtcUs)
                : 1

            // Follow the master clock unless the operator is dragging.
            value: pressed ? value
                 : Number(syncPlayback.positionUtcUs -
                          syncPlayback.windowStartUtcUs)

            onMoved: syncPlayback.seek(
                syncPlayback.windowStartUtcUs + Math.round(value))

            background: Rectangle {
                x: timeline.leftPadding
                y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                width: timeline.availableWidth
                height: 6
                radius: 3
                color: "#1c232b"

                Rectangle {
                    width: timeline.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: "#2f81f7"
                }
            }
        }

        // --- Window edge labels (Shamsi short times) --------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: bar.hasWindow

            Label {
                text: shamsi.toShamsiShort(syncPlayback.windowStartUtcUs)
                color: "#6b7280"
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
            Label {
                text: shamsi.toShamsiShort(syncPlayback.windowEndUtcUs)
                color: "#6b7280"
                font.pixelSize: 10
            }
        }
    }
}
