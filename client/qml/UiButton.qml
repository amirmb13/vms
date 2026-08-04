// =============================================================================
// UiButton — the app's one flat button. Fully custom-drawn so colors are
// deterministic on every platform style (no white-on-white regressions).
//   active: current nav tab (filled + outlined, bright text)
//   accent: primary action (accent-tinted fill, accent text)
// =============================================================================
import QtQuick
import QtQuick.Controls
import Vms.Client

AbstractButton {
    id: btn

    property bool active: false
    property bool accent: false

    implicitHeight: 30
    implicitWidth: label.implicitWidth + 26
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: btn.down ? Theme.surface3
             : btn.accent ? Theme.accentSoft
             : (btn.hovered || btn.active) ? Theme.surface2
             : "transparent"
        border.width: (btn.active || btn.accent) ? 1 : 0
        border.color: btn.accent ? Qt.alpha(Theme.accent, 0.45) : Theme.border

        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Text {
        id: label
        text: btn.text
        color: !btn.enabled ? Theme.textMute
             : btn.accent ? Theme.accent
             : btn.active ? Theme.text
             : btn.hovered ? Theme.text : Theme.textDim
        font.family: "Vazirmatn"
        font.pixelSize: 13
        font.bold: btn.active
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
