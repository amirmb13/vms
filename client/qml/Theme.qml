pragma Singleton
// =============================================================================
// Theme — single source of truth for the client's visual language.
// Minimal dark control-room palette: near-black canvas, one blue accent,
// green/red reserved strictly for recording/error status.
// =============================================================================
import QtQuick

QtObject {
    // Surfaces (canvas -> raised panels)
    readonly property color bg:         "#0c1016"   // window / video canvas
    readonly property color surface:    "#12161d"   // side panels, bars
    readonly property color surface2:   "#1a202a"   // hover / chips
    readonly property color surface3:   "#232b38"   // pressed / highlighted
    readonly property color border:     "#28303d"   // control outlines
    readonly property color borderSoft: "#1d232d"   // hairline separators

    // Text
    readonly property color text:     "#eaeef4"     // primary
    readonly property color textDim:  "#96a1b3"     // secondary
    readonly property color textMute: "#5b6675"     // hints / disabled

    // Accent + status
    readonly property color accent:     "#4c8dff"
    readonly property color accentSoft: "#16283f"   // accent-tinted fill
    readonly property color success:    "#3fb96f"
    readonly property color danger:     "#e5534b"
    readonly property color dangerSoft: "#331716"

    // Shape
    readonly property int radius: 8
    readonly property int radiusSm: 6
}
