// Camera directory tree — RTL Farsi TreeView over the C++ cameraTreeModel.
// Group rows show name + camera count; camera leaves are draggable into the
// Grid Engine (drag payload = camera UUID).
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#161b21"

    // Emitted when the operator activates a camera leaf (double-click).
    signal cameraActivated(string cameraUuid, string nameFa)

    Column {
        anchors.fill: parent

        // Header + manual refresh
        Rectangle {
            width: parent.width
            height: 40
            color: "#1d242c"

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 12
                text: "فهرست دوربین‌ها"
                font.bold: true
                color: "#e8edf2"
            }

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 4
                text: "\u21bb"
                enabled: !cameraTreeModel.loading
                onClicked: cameraTreeModel.reload()
            }
        }

        TreeView {
            id: treeView
            width: parent.width
            height: parent.height - 40
            clip: true
            model: cameraTreeModel

            // Fully custom RTL delegate. The stock TreeViewDelegate draws its
            // expand indicator with LTR geometry (left margin + depth
            // indentation), which fought the RightToLeft content row: the
            // arrow rendered in the wrong spot and jumped/vanished when a
            // group was toggled. Here the arrow and the depth indentation are
            // both anchored to the RIGHT edge, so RTL layout stays stable
            // across expand/collapse.
            delegate: Item {
                id: cell
                implicitWidth: treeView.width
                implicitHeight: 32

                required property TreeView treeView
                required property bool isTreeNode
                required property bool expanded
                required property bool hasChildren
                required property int depth
                required property int row
                required property int column

                required property string nameFa
                required property bool isCamera
                required property string cameraUuid
                required property int cameraCount
                required property bool recordingEnabled

                // Hover / selection feedback
                Rectangle {
                    anchors.fill: parent
                    color: hover.hovered ? "#222a33" : "transparent"
                }
                HoverHandler { id: hover }

                // Expand/collapse arrow — right-anchored. ▾ when expanded,
                // ◂ (pointing left = RTL "closed") when collapsed. The glyph
                // swaps directly with no rotation animation: animated
                // rotation caused spurious spins when TreeView reused
                // delegates during expand/collapse. The arrow deliberately
                // has NO TapHandler of its own — the row-level handler below
                // covers the whole row. A second handler here made a single
                // click toggle the group twice (open then instantly close).
                Text {
                    id: arrow
                    visible: cell.isTreeNode && cell.hasChildren
                    anchors.right: parent.right
                    anchors.rightMargin: 8 + cell.depth * 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: cell.expanded ? "\u25be" : "\u25c2"   // ▾ / ◂
                    color: "#8b949e"
                    font.pixelSize: 14
                }

                Row {
                    spacing: 6
                    layoutDirection: Qt.RightToLeft
                    anchors.right: parent.right
                    // Reserve arrow width so labels align whether or not the
                    // row has an arrow; indent grows to the right per depth.
                    anchors.rightMargin: 28 + cell.depth * 16
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter

                    // Recording status dot for camera leaves
                    Rectangle {
                        visible: cell.isCamera
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: cell.recordingEnabled ? "#3fb950" : "#6e7681"
                    }

                    Label {
                        width: Math.min(implicitWidth, parent.width - 20)
                        text: cell.isCamera
                              ? cell.nameFa
                              : cell.nameFa + " (" + cell.cameraCount + ")"
                        color: cell.isCamera ? "#c9d1d9" : "#e8edf2"
                        font.bold: !cell.isCamera
                        elide: Text.ElideLeft
                    }
                }

                // Groups: single click anywhere on the row (arrow included)
                // toggles expansion. Cameras: double-click places the camera
                // in the focused cell. singleTapped (not tapped) is used so a
                // camera double-click doesn't also fire two stray single-tap
                // events, and a group double-click doesn't toggle twice
                // (open + instantly close again).
                TapHandler {
                    onSingleTapped: {
                        if (!cell.isCamera && cell.hasChildren)
                            cell.treeView.toggleExpanded(cell.row)
                    }
                    onDoubleTapped: {
                        if (cell.isCamera)
                            root.cameraActivated(cell.cameraUuid, cell.nameFa)
                    }
                }
            }
        }
    }

    // Fetch the directory once the tree first appears.
    Component.onCompleted: cameraTreeModel.reload()

    // Farsi error strip (network failures)
    Rectangle {
        visible: cameraTreeModel.errorFa.length > 0
        anchors.bottom: parent.bottom
        width: parent.width
        height: 32
        color: "#5c1e1e"

        Label {
            anchors.centerIn: parent
            text: cameraTreeModel.errorFa
            color: "#ffb4b4"
            font.pixelSize: 12
        }
    }
}
