// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Ping tool window: echoes run on the router, replies arrive typed.
Rectangle {
    id: win

    implicitWidth: 620
    implicitHeight: 420
    property int minimumWidth: 420
    property int minimumHeight: 260
    property string windowTitle: qsTr("Ping")
    color: "#1e1e1e"
    property string toolId: ""

    readonly property var tool: AppEngine.pingTool

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label { text: qsTr("Host"); color: "#8c8c8c" }
            TextField {
                id: targetField
                Layout.fillWidth: true
                text: win.tool.target
                placeholderText: qsTr("address or hostname")
                onTextEdited: win.tool.target = text
            }
            Label { text: qsTr("Packets"); color: "#8c8c8c" }
            TextField {
                Layout.preferredWidth: 60
                text: win.tool.count
                validator: IntValidator { bottom: 1; top: 20 }
                onTextEdited: win.tool.count = parseInt(text, 10) || 1
            }
            Button {
                text: win.tool.running ? qsTr("Pinging…") : qsTr("Ping")
                enabled: !win.tool.running && AppEngine.connected
                onClicked: win.tool.start()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: win.tool.summaryText
                color: "#e8e8e8"
                elide: Text.ElideRight
            }
            Label { text: win.tool.statusText; color: "#6f6f6f"; font.pixelSize: 11 }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: replyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.replies
            spacing: 2

            delegate: RowLayout {
                required property var modelData
                width: replyList.width
                spacing: 14
                Text {
                    text: "#" + modelData.sequence
                    color: "#8c8c8c"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 50
                }
                Text {
                    text: modelData.address
                    color: "#e8e8e8"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                Text {
                    text: modelData.rtt
                    color: "#3fbf6a"
                    font.family: "monospace"
                    font.pixelSize: 12
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.replies.length === 0
            text: AppEngine.connected ? qsTr("no replies yet") : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }
    }
}
