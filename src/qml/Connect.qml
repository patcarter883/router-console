import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Connect window: address + credentials, certificate trust prompt, inline
// progress and error. Stays open until the session is up.
Dialog {
    id: dialog
    modal: true
    title: qsTr("Connect")
    width: 460
    closePolicy: Popup.NoAutoClose
    padding: 16

    property bool connecting: AppEngine.busy

    function attempt() {
        AppEngine.connectTo(hostField.text, "", userField.text, passwordField.text)
    }

    contentItem: ColumnLayout {
        spacing: 12

        GridLayout {
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: qsTr("Host") }
            TextField {
                id: hostField
                Layout.fillWidth: true
                text: "127.0.0.1:10443"
                placeholderText: qsTr("192.168.77.1 or host:port")
            }

            Label { text: qsTr("Username") }
            TextField {
                id: userField
                Layout.fillWidth: true
                text: "root"
            }

            Label { text: qsTr("Password") }
            TextField {
                id: passwordField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                onAccepted: dialog.attempt()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: AppEngine.errorText.length > 0
            text: AppEngine.errorText
            color: "#e05252"
            wrapMode: Text.WordWrap
        }

        Frame {
            Layout.fillWidth: true
            visible: AppEngine.trustPending

            ColumnLayout {
                spacing: 8
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: qsTr("This router presented an untrusted certificate. Trust it only if the fingerprint matches the router you are configuring.")
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: "SHA-256 " + AppEngine.trustFingerprint
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
                RowLayout {
                    Button {
                        text: qsTr("Trust and connect")
                        onClicked: AppEngine.trustCertificate()
                    }
                    Button {
                        text: qsTr("Reject")
                        onClicked: AppEngine.rejectCertificate()
                    }
                }
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: dialog.connecting
            visible: dialog.connecting
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Connect")
            enabled: !dialog.connecting
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.attempt()
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    }
}
