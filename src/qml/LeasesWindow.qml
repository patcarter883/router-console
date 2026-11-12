// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// DHCP leases: live dnsmasq/odhcpd leases merged with the `config host`
// reservations, one row per address.
Rectangle {
    id: win

    implicitWidth: 900
    implicitHeight: 520
    property int minimumWidth: 600
    property int minimumHeight: 320
    property string windowTitle: qsTr("DHCP leases")
    color: "#1e1e1e"
    property string toolId: ""

    readonly property var tool: AppEngine.leaseTool

    Component.onCompleted: win.tool.refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: win.tool.statusText
                color: "#a0a0a0"
                font.pixelSize: 11
            }
            Button {
                text: qsTr("Refresh")
                enabled: !win.tool.busy
                onClicked: win.tool.refresh()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: qsTr("Hostname"); color: "#8c8c8c"; font.pixelSize: 11
                Layout.preferredWidth: 200
            }
            Text {
                text: qsTr("Address"); color: "#8c8c8c"; font.pixelSize: 11
                Layout.preferredWidth: 260
            }
            Text {
                text: qsTr("MAC"); color: "#8c8c8c"; font.pixelSize: 11
                Layout.preferredWidth: 160
            }
            Text {
                text: qsTr("Remaining"); color: "#8c8c8c"; font.pixelSize: 11
                Layout.preferredWidth: 100
            }
            Text {
                text: qsTr("Source"); color: "#8c8c8c"; font.pixelSize: 11
                Layout.fillWidth: true
            }
        }

        ListView {
            id: leaseList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.leases
            spacing: 2
            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                required property var modelData
                width: leaseList.width
                spacing: 8

                Text {
                    text: modelData.hostname
                    color: "#e8e8e8"
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.preferredWidth: 200
                }
                Text {
                    text: modelData.address
                    color: "#7fb8e0"
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.preferredWidth: 260
                }
                Text {
                    text: modelData.mac.length > 0 ? modelData.mac : "-"
                    color: "#a0a0a0"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 160
                }
                Text {
                    text: modelData.remaining
                    color: modelData.dynamic ? "#e0c07a" : "#8c8c8c"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 100
                }
                Text {
                    text: modelData.kind
                    color: modelData.dynamic ? "#a0a0a0" : "#3fbf6a"
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.leases.length === 0
            text: AppEngine.connected
                  ? qsTr("no leases and no reservations on this router")
                  : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }

        Label {
            Layout.fillWidth: true
            text: win.tool.dynamicCount + qsTr(" dynamic, ") + win.tool.reservationCount
                  + qsTr(" reserved")
            color: "#6f6f6f"
            font.pixelSize: 11
        }
    }
}
