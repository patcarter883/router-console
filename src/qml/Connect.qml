// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

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

    function useDiscovered(target) {
        hostField.text = target
        passwordField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 12

        // Auto-discovery: routers announced on the local segment (mDNS). The
        // list is advisory — manual entry below always works.
        Frame {
            Layout.fillWidth: true
            background: Rectangle {
                color: "#252525"
                radius: 4
                border.color: "#3a3a3a"
            }

            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true

                Label {
                    text: AppEngine.discoveredCount > 0
                          ? qsTr("Discovered routers (%1)").arg(AppEngine.discoveredCount)
                          : qsTr("Searching the local network…")
                    font.bold: true
                    color: "#c8c8c8"
                }

                ListView {
                    id: discoveredList
                    objectName: "discoveredList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 132)
                    visible: AppEngine.discoveredCount > 0
                    clip: true
                    model: AppEngine.discoveredHosts
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: ItemDelegate {
                        required property string hostname
                        required property string address
                        required property int port
                        required property string target
                        required property string services

                        width: discoveredList.width
                        height: 44
                        onClicked: dialog.useDiscovered(target)

                        contentItem: ColumnLayout {
                            spacing: 0
                            Label {
                                text: hostname
                                font.bold: true
                            }
                            Label {
                                text: address + (port === 443 ? "" : ":" + port)
                                      + (services.length > 0 ? "  ·  " + services : "")
                                color: "#9a9a9a"
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

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
