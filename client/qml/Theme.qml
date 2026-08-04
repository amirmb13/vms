pragma Singleton
// =============================================================================
// Theme — single source of truth for the client's visual language.
//
// Minimal dark control-room palette: one deep blue-black canvas ramp, a single
// blue accent, and green/red reserved strictly for recording/error status.
// Every control in the app is custom-drawn from these tokens — no platform
// style ever paints its own (light) colors, so white-on-white is impossible.
// =============================================================================
import QtQuick

QtObject {
    // ---- Surfaces (canvas -> raised panels) --------------------------------
    readonly property color bg:         "#0a0d13"   // window / video canvas
    readonly property color surface:    "#10151d"   // side panels, bars
    readonly property color surface2:   "#171e28"   // hover / chips
    readonly property color surface3:   "#212b38"   // pressed / highlighted
    readonly property color border:     "#2b3644"   // control outlines
    readonly property color borderSoft: "#1b222c"   // hairline separators

    // ---- Text ---------------------------------------------------------------
    readonly property color text:     "#f0f4f9"     // primary
    readonly property color textDim:  "#9dabbc"     // secondary
    readonly property color textMute: "#5e6a7a"     // hints / disabled

    // ---- Accent + status ------------------------------------------------------
    readonly property color accent:     "#549aff"
    readonly property color accentSoft: "#152943"   // accent-tinted fill
    readonly property color success:    "#3ecf7f"
    readonly property color danger:     "#ff5c52"
    readonly property color dangerSoft: "#321719"

    // ---- Shape ----------------------------------------------------------------
    readonly property int radius: 10
    readonly property int radiusSm: 7

    // ---- Type scale (Vazirmatn) ------------------------------------------------
    readonly property int fontXs: 10
    readonly property int fontSm: 12
    readonly property int fontMd: 13
    readonly property int fontLg: 15

    // ---- Motion -----------------------------------------------------------------
    readonly property int durFast: 100
    readonly property int durMed: 180
}
