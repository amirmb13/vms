// =============================================================================
// UiButton — the app's one flat button. Fully custom-drawn so colors are
// deterministic on every platform style (no white-on-white regressions).
//   active: current nav tab (soft fill + accent underline, bright text)
//   accent: primary action (accent-tinted fill, accent text + outline)
// =============================================================================
import QtQuick
import QtQuick.Controls
import Vms.Client

AbstractButton {
    id: btn

    property bool active: false
    property bool accent: false
    // Nav tabs draw a 2px accent underline instead of a heavy outline.
    property bool underlineWhenActive: false

    implicitHeight: 30
    implicitWidth: label.implicitWidth + 28
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: btn.down ? Theme.surface3
             : btn.accent ? Theme.accentSoft
             : (btn.hovered || btn.active) ? Theme.surface2
             : "transparent"
        border.width: btn.accent ? 1
                    : (btn.active && !btn.underlineWhenActive) ? 1 : 0
        border.color: btn.accent ? Qt.alpha(Theme.accent, 0.45) : Theme.border

        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        // Active-tab underline — a calmer signal than a filled box.
        Rectangle {
            visible: btn.underlineWhenActive && btn.active
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 16
            height: 2
            radius: 1
            color: Theme.accent
        }
    }

    contentItem: Text {
        id: label
        text: btn.text
        color: !btn.enabled ? Theme.textMute
             : btn.accent ? Theme.accent
             : btn.active ? Theme.text
             : btn.hovered ? Theme.text : Theme.textDim
        font.family: "Vazirmatn"
        font.pixelSize: Theme.fontMd
        font.bold: btn.active || btn.accent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
}
