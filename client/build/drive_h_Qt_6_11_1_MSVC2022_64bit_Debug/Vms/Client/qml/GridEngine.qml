// =============================================================================
// GridEngine — hyper-customizable video wall renderer.
//
// Applies a GridLayout.layout_json document from Django:
//   { "grid": { "cols": 4, "rows": 3 },
//     "cells": [ { "x":0, "y":0, "w":2, "h":2, "camera_uuid": "..." }, ... ] }
//
// Cells may span multiple columns/rows (merged cells). Coordinates are stored
// LTR in the database; because the whole app is mirrored (LayoutMirroring),
// cell x-positions are flipped here so the persisted layout stays
// direction-agnostic while the on-screen result is proper RTL.
// =============================================================================
import QtQuick
import QtQuick.Controls

Rectangle {
    id: engine
    color: "#0b0e12"

    // ---- Public API ----------------------------------------------------------
    // The validated layout_json object (already parsed to a JS object).
    property var layoutJson: ({ grid: { cols: 2, rows: 2 }, cells: [
        { x: 0, y: 0, w: 1, h: 1, camera_uuid: null },
        { x: 1, y: 0, w: 1, h: 1, camera_uuid: null },
        { x: 0, y: 1, w: 1, h: 1, camera_uuid: null },
        { x: 1, y: 1, w: 1, h: 1, camera_uuid: null } ] })

    // Index of the operator-focused cell (receives camera assignments).
    property int focusedCell: 0

    // Emitted when a cell requests a stream (GridEngine stays decode-agnostic;
    // the StreamController owns profile selection vs. the Media Relay).
    signal cellCameraChanged(int cellIndex, string cameraUuid)

    // Per-cell camera overrides, keyed by cell index. Updated in place so an
    // assignment only rebinds the target cell instead of tearing down every
    // delegate (which would stop and restart every running stream and race
    // deferred detach() against the new attach()).
    property var cameraAssignments: ({})

    function applyLayout(json) {
        // Accept either a parsed object or a JSON string from the REST layer.
        cameraAssignments = ({})
        layoutJson = (typeof json === "string") ? JSON.parse(json) : json
        focusedCell = 0
    }

    // Assign a camera into the focused cell (wired to CameraTree activation).
    function assignCameraToFocusedCell(cameraUuid) {
        if (focusedCell < 0 || focusedCell >= repeater.count)
            return
        var next = {}
        for (var k in cameraAssignments)
            next[k] = cameraAssignments[k]
        next[focusedCell] = cameraUuid
        cameraAssignments = next  // property change → only bindings re-evaluate
        cellCameraChanged(focusedCell, cameraUuid)
    }

    // ---- Geometry ------------------------------------------------------------
    readonly property int gridCols: layoutJson && layoutJson.grid
                                    ? layoutJson.grid.cols : 1
    readonly property int gridRows: layoutJson && layoutJson.grid
                                    ? layoutJson.grid.rows : 1
    readonly property real cellW: width / gridCols
    readonly property real cellH: height / gridRows
    readonly property bool rtl: LayoutMirroring.enabled

    Repeater {
        id: repeater
        model: layoutJson && layoutJson.cells ? layoutJson.cells : []

        delegate: Rectangle {
            id: cell

            required property var modelData
            required property int index

            // RTL flip: mirror the column origin so layout_json stays LTR.
            readonly property int flippedX:
                engine.rtl ? engine.gridCols - modelData.x - modelData.w
                           : modelData.x

            x: flippedX * engine.cellW
            y: modelData.y * engine.cellH
            width: modelData.w * engine.cellW
            height: modelData.h * engine.cellH

            color: "#12161c"
            border.width: engine.focusedCell === index ? 2 : 1
            border.color: engine.focusedCell === index ? "#2f81f7" : "#232a33"

            // Runtime assignment (drag/activation) wins over the persisted
            // layout document.
            readonly property string cameraUuid: {
                var assigned = engine.cameraAssignments[cell.index]
                if (assigned !== undefined && assigned !== null)
                    return assigned
                return modelData.camera_uuid ? modelData.camera_uuid : ""
            }

            // Video surface: VideoCell handles decode -> QSGTexture upload on
            // the RHI scene graph. Loaded only when a camera is assigned.
            Loader {
                anchors.fill: parent
                anchors.margins: 1
                active: cell.cameraUuid.length > 0
                source: "VideoCell.qml"
                onLoaded: {
                    // Order matters: cellIndex MUST be set before cameraUuid,
                    // because onCameraUuidChanged triggers attachLive() which
                    // keys the decoder session by cellIndex. Without this,
                    // every cell attaches with the default index (-1) and all
                    // cells share one decoder — assigning a second camera
                    // steals the decoder and blanks the first cell.
                    item.cellIndex = cell.index
                    // Adaptive profile: sub-stream for small tiles, mid/main
                    // when the merged cell is large enough to justify it.
                    item.preferredProfile = Qt.binding(function() {
                        return (cell.width > engine.width / 2) ? "main"
                             : (cell.width > engine.width / 4) ? "mid" : "sub"
                    })
                    item.cameraUuid = Qt.binding(function() {
                        return cell.cameraUuid
                    })
                }
            }

            // Empty-cell placeholder (Farsi hint)
            Label {
                anchors.centerIn: parent
                visible: cell.cameraUuid.length === 0
                text: "دوربینی انتخاب نشده است"
                color: "#4b5563"
                font.pixelSize: 13
            }

            TapHandler {
                onTapped: engine.focusedCell = cell.index
            }
        }
    }
}
