// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Traceroute tool window: hops as the router's own traceroute reported them.
Window {
    id: win

    width: 640
    height: 440
    minimumWidth: 440
    minimumHeight: 280
    title: qsTr("Traceroute")
    color: "#1e1e1e"
    visible: true
    property string toolId: ""

    readonly property var tool: AppEngine.tracerouteTool

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label { text: qsTr("Host"); color: "#8c8c8c" }
            TextField {
                Layout.fillWidth: true
                text: win.tool.target
                placeholderText: qsTr("address or hostname")
                onTextEdited: win.tool.target = text
            }
            Label { text: qsTr("Max hops"); color: "#8c8c8c" }
            TextField {
                Layout.preferredWidth: 60
                text: win.tool.maxHops
                validator: IntValidator { bottom: 1; top: 30 }
                onTextEdited: win.tool.maxHops = parseInt(text, 10) || 1
            }
            Button {
                text: win.tool.running ? qsTr("Tracing…") : qsTr("Trace")
                enabled: !win.tool.running && AppEngine.connected
                onClicked: win.tool.start()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: win.tool.statusText; color: "#e8e8e8" }
            Label { text: win.tool.target; color: "#6f6f6f"; font.pixelSize: 11 }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: hopList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.hops
            spacing: 2

            delegate: RowLayout {
                required property var modelData
                width: hopList.width
                spacing: 14
                Text {
                    text: modelData.ttl
                    color: "#8c8c8c"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 28
                    horizontalAlignment: Text.AlignRight
                }
                Text {
                    text: modelData.address
                    color: modelData.reached ? "#e8e8e8" : "#7a7a7a"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                Text {
                    text: modelData.note
                    color: "#e0a35e"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 40
                }
                Text {
                    text: modelData.rtt
                    color: "#4b9fe8"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 90
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.hops.length === 0
            text: AppEngine.connected ? qsTr("no hops yet") : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }
    }
}
