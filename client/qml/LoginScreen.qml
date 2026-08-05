// =============================================================================
// LoginScreen — full-window connection overlay.
//
// Lets the operator point the client at a backend (Django control plane),
// sign in with a username/password, and see the live connection state.
// Visible whenever there is no session, or when opened from the header
// ("تنظیمات سرور") to change the server or sign out.
// =============================================================================
import QtQuick
import QtQuick.Controls
import Vms.Client

Item {
    id: root

    // Set by Main.qml to reopen the panel while already connected.
    property bool forceVisible: false

    visible: !session.connected || forceVisible
    anchors.fill: parent
    z: 100

    // Dim the video wall behind the card.
    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(Theme.bg, 0.94)
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 430
        height: cardColumn.implicitHeight + 56
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Column {
            id: cardColumn
            anchors.centerIn: parent
            width: parent.width - 64
            spacing: 14

            // Title + connection status chip.
            Row {
                width: parent.width
                spacing: 10

                Column {
                    width: parent.width - 130
                    spacing: 2
                    Label {
                        text: "اتصال به سرور"
                        color: Theme.text
                        font.pixelSize: Theme.fontLg
                        font.bold: true
                    }
                    Label {
                        text: "سامانه مدیریت تصاویر نظارتی"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSm
                    }
                }

                Rectangle {
                    width: 110
                    height: 26
                    radius: 13
                    color: Theme.surface2
                    border.width: 1
                    border.color: session.connected ? Qt.alpha(Theme.success, 0.4)
                                                    : Qt.alpha(Theme.danger, 0.4)
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8; height: 8; radius: 4
                            color: session.connected ? Theme.success : Theme.danger
                        }
                        Label {
                            text: session.connected ? "متصل" : "قطع"
                            color: session.connected ? Theme.success : Theme.danger
                            font.pixelSize: Theme.fontSm
                        }
                    }
                }
            }

            // Server address.
            Label {
                text: "آدرس سرور"
                color: Theme.textDim
                font.pixelSize: Theme.fontSm
            }
            TextField {
                id: serverField
                width: parent.width
                height: 38
                text: session.apiBaseUrl
                placeholderText: "http://192.168.1.10:8000"
                placeholderTextColor: Theme.textMute
                color: Theme.text
                font.family: "Vazirmatn"
                font.pixelSize: Theme.fontMd
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: Theme.surface2
                    border.width: 1
                    border.color: serverField.activeFocus ? Theme.accent : Theme.border
                }
            }

            // Username.
            Label {
                text: "نام کاربری"
                color: Theme.textDim
                font.pixelSize: Theme.fontSm
            }
            TextField {
                id: userField
                width: parent.width
                height: 38
                text: session.username
                placeholderText: "admin"
                placeholderTextColor: Theme.textMute
                color: Theme.text
                font.family: "Vazirmatn"
                font.pixelSize: Theme.fontMd
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: Theme.surface2
                    border.width: 1
                    border.color: userField.activeFocus ? Theme.accent : Theme.border
                }
            }

            // Password.
            Label {
                text: "رمز عبور"
                color: Theme.textDim
                font.pixelSize: Theme.fontSm
            }
            TextField {
                id: passwordField
                width: parent.width
                height: 38
                echoMode: TextInput.Password
                placeholderText: "••••••••"
                placeholderTextColor: Theme.textMute
                color: Theme.text
                font.family: "Vazirmatn"
                font.pixelSize: Theme.fontMd
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: Theme.surface2
                    border.width: 1
                    border.color: passwordField.activeFocus ? Theme.accent : Theme.border
                }
                Keys.onReturnPressed: submit()
            }

            // Farsi error strip.
            Rectangle {
                visible: session.errorFa.length > 0
                width: parent.width
                height: 34
                radius: Theme.radiusSm
                color: Theme.dangerSoft
                border.width: 1
                border.color: Qt.alpha(Theme.danger, 0.35)

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    text: session.errorFa
                    color: Theme.danger
                    font.pixelSize: Theme.fontSm
                    elide: Text.ElideMiddle
                }
            }

            // Actions.
            Row {
                width: parent.width
                spacing: 10

                UiButton {
                    text: session.busy ? "در حال اتصال..." : "ورود"
                    accent: true
                    enabled: !session.busy
                    LayoutMirroring.enabled: false
                    onClicked: submit()
                }

                UiButton {
                    visible: session.connected
                    text: "قطع اتصال"
                    onClicked: session.logout()
                }
            }
        }
    }

    function submit() {
        session.setServerUrl(serverField.text)
        session.login(userField.text, passwordField.text)
    }
}
