// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Files window: browse the router's filesystem, edit a text file, move bytes in
// and out, and stage/verify a sysupgrade image or install packages.
Window {
    id: win

    width: 900
    height: 620
    minimumWidth: 640
    minimumHeight: 420
    title: qsTr("Files")
    color: "#1e1e1e"
    visible: true
    property string toolId: ""

    readonly property var tool: AppEngine.filesTool
    property string localPath: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: "\u2191"
                enabled: win.tool.parentPath.length > 0
                onClicked: win.tool.up()
            }
            TextField {
                Layout.fillWidth: true
                text: win.tool.path
                onAccepted: win.tool.navigate(text)
            }
            Button { text: qsTr("Go"); onClicked: win.tool.navigate(win.tool.path) }
            Button {
                text: qsTr("Refresh")
                enabled: !win.tool.busy
                onClicked: win.tool.refresh()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            ListView {
                id: fileList
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                clip: true
                model: win.tool.entries
                spacing: 1

                delegate: Rectangle {
                    required property var modelData
                    width: fileList.width
                    height: 24
                    color: rowMouse.containsMouse ? "#2f2f2f" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        spacing: 8
                        Text {
                            text: modelData.directory ? "\u25b8 " + modelData.name : modelData.name
                            color: modelData.directory ? "#e0c07a" : "#e8e8e8"
                            font.family: "monospace"
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: modelData.directory ? "" : modelData.size
                            color: "#8c8c8c"
                            font.family: "monospace"
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: modelData.directory
                                   ? win.tool.navigate(modelData.path)
                                   : win.tool.openFile(modelData.name)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: win.tool.openPath.length > 0 ? win.tool.openPath : qsTr("select a file")
                        color: "#8c8c8c"
                        elide: Text.ElideMiddle
                    }
                    Button {
                        text: qsTr("Save")
                        enabled: win.tool.openPath.length > 0 && !win.tool.busy
                        onClicked: win.tool.saveOpenFile(editor.text)
                    }
                }

                TextArea {
                    id: editor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: win.tool.contentText
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: "#e8e8e8"
                    selectByMouse: true
                    wrapMode: TextArea.NoWrap
                    background: Rectangle { color: "#191919"; border.color: "#333333" }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        // Transfer: local path is the client's own filesystem.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Local file"); color: "#8c8c8c" }
            TextField {
                Layout.fillWidth: true
                text: win.localPath
                placeholderText: qsTr("/path/to/file")
                onTextEdited: win.localPath = text
            }
            Button {
                text: qsTr("Upload here")
                enabled: win.localPath.length > 0 && !win.tool.busy
                onClicked: win.tool.upload(win.localPath, "")
            }
            Button {
                text: qsTr("Download selection")
                enabled: win.tool.openPath.length > 0 && win.localPath.length > 0
                onClicked: win.tool.download(win.tool.openPath.split("/").pop(), win.localPath)
            }
        }

        // Sysupgrade: upload the image to /tmp/firmware.bin, then verify it here.
        // The flash button only unlocks after rpc-sys upgrade_test accepted it.
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Sysupgrade")
            background: Rectangle { color: "#232323"; border.color: "#333333" }

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        text: qsTr("Upload as /tmp/firmware.bin")
                        enabled: win.localPath.length > 0 && !win.tool.busy
                        onClicked: win.tool.navigate("/tmp"), win.tool.upload(win.localPath, "firmware.bin")
                    }
                    Button {
                        text: qsTr("Verify image")
                        enabled: !win.tool.busy
                        onClicked: win.tool.verifyImage()
                    }
                    CheckBox {
                        id: flashConfirm
                        text: qsTr("I understand this reboots the router")
                    }
                    Button {
                        text: qsTr("Flash")
                        highlighted: true
                        enabled: win.tool.imageVerified && flashConfirm.checked
                        onClicked: win.tool.flashImage()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: win.tool.imageStatus.length > 0 ? win.tool.imageStatus : qsTr("no image staged")
                    color: win.tool.imageVerified ? "#3fbf6a" : "#c8c8c8"
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                }
            }
        }

        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Packages")
            background: Rectangle { color: "#232323"; border.color: "#333333" }

            RowLayout {
                anchors.fill: parent
                spacing: 8
                TextField {
                    id: packageField
                    Layout.fillWidth: true
                    placeholderText: qsTr("package names, space separated")
                }
                Button {
                    text: qsTr("Install")
                    enabled: packageField.text.length > 0 && !win.tool.busy
                    onClicked: win.tool.installPackages(packageField.text)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: win.tool.statusText
            color: "#a0a0a0"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
