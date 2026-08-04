// Camera directory tree — RTL Farsi TreeView over the C++ cameraTreeModel.
// Group rows show name + camera-count pill; camera leaves are activated by
// double-click. All colors come from the Theme singleton.
import QtQuick
import QtQuick.Controls
import Vms.Client

Rectangle {
    id: root
    color: Theme.surface

    // Panel edge hairline (separates the tree from the video wall).
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.borderSoft
    }

    // Emitted when the operator activates a camera leaf (double-click).
    signal cameraActivated(string cameraUuid, string nameFa)

    Column {
        anchors.fill: parent

        // ---- Panel header -------------------------------------------------
        Item {
            width: parent.width
            height: 46

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 14
                text: "دوربین‌ها"
                font.pixelSize: 13
                font.bold: true
                color: Theme.text
            }

            // Refresh — custom-drawn so it can never fall back to a light
            // platform style. Spins subtly while a fetch is in flight.
            Rectangle {
                id: refreshBtn
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                width: 28; height: 28
                radius: Theme.radiusSm
                color: refreshTap.pressed ? Theme.surface3
                     : refreshHover.hovered ? Theme.surface2 : "transparent"

                Behavior on color { ColorAnimation { duration: 90 } }

                Text {
                    id: refreshGlyph
                    anchors.centerIn: parent
                    text: "\u21bb"
                    font.pixelSize: 15
                    color: cameraTreeModel.loading ? Theme.accent
                         : refreshHover.hovered ? Theme.text : Theme.textDim

                    RotationAnimation on rotation {
                        running: cameraTreeModel.loading
                        loops: Animation.Infinite
                        from: 0; to: 360
                        duration: 900
                        onRunningChanged: if (!running) refreshGlyph.rotation = 0
                    }
                }

                HoverHandler { id: refreshHover }
                TapHandler {
                    id: refreshTap
                    enabled: !cameraTreeModel.loading
                    onTapped: cameraTreeModel.reload()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSoft
            }
        }

        TreeView {
            id: treeView
            width: parent.width
            height: parent.height - 46
            clip: true
            model: cameraTreeModel

            // Delegate pooling is disabled on purpose: the directory model is
            // swapped wholesale (beginResetModel/endResetModel) when a server
            // fetch lands a few seconds after startup, and reused delegates
            // could keep stale required-property snapshots (hasChildren /
            // isTreeNode) — which made a group's expand arrow silently vanish
            // right after launch.
            reuseItems: false

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: 4
                    radius: 2
                    color: Theme.border
                }
            }

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
                implicitHeight: 34

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

                // Hover feedback
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    radius: Theme.radiusSm
                    color: hover.hovered ? Theme.surface2 : "transparent"
                    Behavior on color { ColorAnimation { duration: 80 } }
                }
                HoverHandler { id: hover }

                // Expand/collapse arrow — right-anchored, shown for EVERY
                // group row (not gated on hasChildren) so it can never blink
                // out when the model is rebuilt after a server fetch; a group
                // that is momentarily childless just dims it. ▾ expanded,
                // ◂ (pointing left = RTL "closed") collapsed. The glyph swaps
                // with no rotation animation — animated rotation caused
                // spurious spins when TreeView recycled delegates. The arrow
                // deliberately has NO TapHandler of its own — the row-level
                // handler below covers the whole row; a second handler here
                // made one click toggle the group twice.
                Text {
                    id: arrow
                    visible: !cell.isCamera
                    opacity: cell.hasChildren ? 1 : 0.35
                    anchors.right: parent.right
                    anchors.rightMargin: 12 + cell.depth * 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: cell.expanded ? "\u25be" : "\u25c2"   // ▾ / ◂
                    color: cell.expanded ? Theme.textDim : Theme.textMute
                    font.pixelSize: 13
                }

                Row {
                    spacing: 8
                    layoutDirection: Qt.RightToLeft
                    anchors.right: parent.right
                    // Reserve arrow width so labels align whether or not the
                    // row has an arrow; indent grows to the right per depth.
                    anchors.rightMargin: 32 + cell.depth * 16
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter

                    // Recording status dot for camera leaves
                    Rectangle {
                        visible: cell.isCamera
                        width: 7; height: 7; radius: 3.5
                        anchors.verticalCenter: parent.verticalCenter
                        color: cell.recordingEnabled ? Theme.success
                                                     : Theme.textMute
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - 44)
                        text: cell.nameFa
                        color: cell.isCamera
                               ? (hover.hovered ? Theme.text : Theme.textDim)
                               : Theme.text
                        font.pixelSize: 13
                        font.bold: !cell.isCamera
                        elide: Text.ElideLeft

                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    // Camera-count pill for group rows
                    Rectangle {
                        visible: !cell.isCamera
                        anchors.verticalCenter: parent.verticalCenter
                        width: countLabel.implicitWidth + 12
                        height: 18
                        radius: 9
                        color: Theme.surface3

                        Label {
                            id: countLabel
                            anchors.centerIn: parent
                            text: cell.cameraCount
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
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
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        height: 34
        radius: Theme.radiusSm
        color: Theme.dangerSoft
        border.width: 1
        border.color: Qt.alpha(Theme.danger, 0.35)

        Label {
            anchors.centerIn: parent
            width: parent.width - 16
            horizontalAlignment: Text.AlignHCenter
            text: cameraTreeModel.errorFa
            color: Theme.danger
            font.pixelSize: 11
            elide: Text.ElideLeft
        }
    }
}
