// =============================================================================
// TimelineBar — master Sync Playback timeline (mandate §3.B).
//
// The slider position is a UTC microsecond NTP timestamp inside the reviewed
// window; dragging broadcasts a synchronized seek to EVERY active decoding
// engine via SyncPlayback -> StreamController. Labels render in Shamsi with
// Persian digits; internal math stays pure UTC. Every control is custom-drawn
// from the Theme singleton.
// =============================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vms.Client

Rectangle {
    id: bar
    color: Theme.surface

    // Top hairline only — no boxy full border.
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.borderSoft
    }

    readonly property bool hasWindow:
        syncPlayback.windowEndUtcUs > syncPlayback.windowStartUtcUs

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 8
        spacing: 6

        // --- Transport controls + Shamsi position readout --------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            UiButton {
                text: "پخش زنده"
                accent: true
                onClicked: syncPlayback.jumpToLive()
            }

            Rectangle { width: 1; height: 18; color: Theme.borderSoft }

            UiButton {
                text: syncPlayback.playing ? "توقف" : "پخش"
                active: syncPlayback.playing
                onClicked: syncPlayback.playing ? syncPlayback.pause()
                                                : syncPlayback.play()
            }
            UiButton {
                text: "قاب قبلی"
                onClicked: syncPlayback.stepFrames(-1)
            }
            UiButton {
                text: "قاب بعدی"
                onClicked: syncPlayback.stepFrames(1)
            }

            // Playback-rate selector — fully themed ComboBox.
            ComboBox {
                id: rateBox
                model: ["۰.۲۵×", "۰.۵×", "۱×", "۲×", "۴×", "۸×", "۱۶×"]
                property var rates: [0.25, 0.5, 1, 2, 4, 8, 16]
                currentIndex: 2
                implicitWidth: 84
                implicitHeight: 30
                onActivated: (i) => syncPlayback.rate = rates[i]

                background: Rectangle {
                    radius: Theme.radiusSm
                    color: rateBox.pressed ? Theme.surface3 : Theme.surface2
                    border.width: 1
                    border.color: rateBox.activeFocus ? Qt.alpha(Theme.accent, 0.5)
                         : rateBox.hovered ? Theme.border : Theme.borderSoft

                    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
                }

                contentItem: Text {
                    leftPadding: 8
                    rightPadding: 8
                    text: rateBox.displayText
                    color: Theme.text
                    font.family: "Vazirmatn"
                    font.pixelSize: Theme.fontSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                indicator: Text {
                    x: rateBox.mirrored ? rateBox.width - width - 6 : 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u25be"
                    color: Theme.textMute
                    font.pixelSize: 11
                }

                delegate: ItemDelegate {
                    id: rateItem
                    required property var model
                    required property int index
                    width: rateBox.width
                    height: 28

                    contentItem: Text {
                        text: rateItem.model[rateBox.textRole] !== undefined
                              ? rateItem.model[rateBox.textRole]
                              : rateItem.model.display !== undefined
                                ? rateItem.model.display
                                : rateBox.model[rateItem.index]
                        color: rateItem.index === rateBox.currentIndex
                               ? Theme.accent : Theme.text
                        font.family: "Vazirmatn"
                        font.pixelSize: Theme.fontSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm - 2
                        color: rateItem.hovered ? Theme.surface3 : "transparent"
                    }
                }

                popup: Popup {
                    y: rateBox.height + 4
                    width: rateBox.width
                    implicitHeight: contentItem.implicitHeight + 8
                    padding: 4

                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: rateBox.popup.visible ? rateBox.delegateModel : null
                        currentIndex: rateBox.highlightedIndex
                    }

                    background: Rectangle {
                        radius: Theme.radius
                        color: Theme.surface2
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Master position — full Shamsi stamp inside a quiet chip so the
            // readout has a stable footprint instead of floating text.
            Rectangle {
                width: posLabel.implicitWidth + 22
                height: 28
                radius: Theme.radiusSm
                color: Theme.surface2
                border.width: 1
                border.color: Theme.borderSoft

                Label {
                    id: posLabel
                    anchors.centerIn: parent
                    text: syncPlayback.positionUtcUs > 0
                          ? shamsi.toShamsi(syncPlayback.positionUtcUs)
                          : "—"
                    color: Theme.text
                    font.pixelSize: Theme.fontSm
                    font.bold: true
                }
            }
        }

        // --- Master timeline slider -------------------------------------------
        Slider {
            id: timeline
            Layout.fillWidth: true
            implicitHeight: 20
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
                height: 4
                radius: 2
                color: bar.hasWindow ? Theme.surface3 : Theme.surface2

                Rectangle {
                    width: timeline.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }

            handle: Rectangle {
                x: timeline.leftPadding +
                   timeline.visualPosition * (timeline.availableWidth - width)
                y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                width: timeline.pressed ? 16 : 14
                height: width
                radius: width / 2
                color: Theme.text
                border.width: 2
                border.color: Theme.accent
                visible: bar.hasWindow

                Behavior on width { NumberAnimation { duration: Theme.durFast } }
            }
        }

        // --- Window edge labels (Shamsi short times) --------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: bar.hasWindow

            Label {
                text: shamsi.toShamsiShort(syncPlayback.windowStartUtcUs)
                color: Theme.textMute
                font.pixelSize: Theme.fontXs
            }
            Item { Layout.fillWidth: true }
            Label {
                text: shamsi.toShamsiShort(syncPlayback.windowEndUtcUs)
                color: Theme.textMute
                font.pixelSize: Theme.fontXs
            }
        }
    }
}
