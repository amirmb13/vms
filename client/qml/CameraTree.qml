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

            delegate: TreeViewDelegate {
                id: row
                implicitWidth: treeView.width

                required property string nameFa
                required property bool isCamera
                required property string cameraUuid
                required property int cameraCount
                required property bool recordingEnabled

                contentItem: Row {
                    spacing: 6
                    layoutDirection: Qt.RightToLeft

                    // Recording status dot for camera leaves
                    Rectangle {
                        visible: row.isCamera
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.recordingEnabled ? "#3fb950" : "#6e7681"
                    }

                    Label {
                        text: row.isCamera
                              ? row.nameFa
                              : row.nameFa + " (" + row.cameraCount + ")"
                        color: row.isCamera ? "#c9d1d9" : "#e8edf2"
                        font.bold: !row.isCamera
                        elide: Text.ElideLeft
                    }
                }

                // Double-click a camera to place it in the focused grid cell.
                TapHandler {
                    enabled: row.isCamera
                    onDoubleTapped: root.cameraActivated(row.cameraUuid, row.nameFa)
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
