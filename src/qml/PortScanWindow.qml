// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Port scan tool window: the router probes each port, one second of grace each.
Rectangle {
    id: win

    implicitWidth: 520
    implicitHeight: 460
    property int minimumWidth: 380
    property int minimumHeight: 280
    property string windowTitle: qsTr("Port Scan")
    color: "#1e1e1e"
    property string toolId: ""

    readonly property var tool: AppEngine.portScanTool

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
                placeholderText: qsTr("address")
                onTextEdited: win.tool.target = text
            }
            Button {
                text: win.tool.running ? qsTr("Scanning…") : qsTr("Scan")
                enabled: !win.tool.running && AppEngine.connected
                onClicked: win.tool.start()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Ports"); color: "#8c8c8c" }
            TextField {
                Layout.fillWidth: true
                text: win.tool.portsText
                placeholderText: qsTr("22,80,443,8000-8010")
                onTextEdited: win.tool.portsText = text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: win.tool.statusText; color: "#e8e8e8" }
            Label { text: qsTr("probed from the router"); color: "#6f6f6f"; font.pixelSize: 11 }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.results
            spacing: 2

            delegate: Rectangle {
                required property var modelData
                width: resultList.width
                height: 24
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    spacing: 12
                    Text {
                        text: modelData.port
                        color: "#e8e8e8"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.preferredWidth: 60
                    }
                    Text {
                        text: modelData.state
                        color: modelData.isOpen ? "#3fbf6a" : "#7a7a7a"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.results.length === 0
            text: AppEngine.connected ? qsTr("no scan yet") : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }
    }
}
