// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// OSPF window: neighbors with their adjacency state, the interfaces OSPF runs
// on (cost, DR state) and the LSA database summary per area.
Rectangle {
    id: win

    implicitWidth: 940
    implicitHeight: 640
    property int minimumWidth: 620
    property int minimumHeight: 380
    property string windowTitle: qsTr("OSPF")
    color: "#1e1e1e"
    property string viewId: ""

    readonly property var tool: AppEngine.ospfTool

    // The window shows the router's current state as soon as it opens; the
    // Refresh button (or the 1 Hz toggle) updates it after that.
    Component.onCompleted: win.tool.refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "router-id " + (win.tool.routerId.length > 0 ? win.tool.routerId : "—")
                      + "   neighbors " + win.tool.neighborCount
                      + "   full " + win.tool.fullCount
                color: win.tool.fullCount > 0 ? "#3fbf6a" : "#a0a0a0"
                font.family: "monospace"
            }
            Button {
                text: qsTr("Refresh")
                enabled: !win.tool.busy
                onClicked: win.tool.refresh()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        Label { text: qsTr("Neighbors"); color: "#e8e8e8"; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { text: qsTr("Neighbor ID"); color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 150 }
            Text { text: qsTr("State");       color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 120 }
            Text { text: qsTr("Address");     color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 140 }
            Text { text: qsTr("Interface");   color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 120 }
            Text { text: qsTr("Up");          color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 100 }
            Text { text: qsTr("Dead");        color: "#8c8c8c"; font.pixelSize: 11; Layout.fillWidth: true }
        }

        ListView {
            id: neighborList
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            clip: true
            model: win.tool.neighbors

            delegate: RowLayout {
                required property string neighborId
                required property string ospfState
                required property string address
                required property string iface
                required property string uptime
                required property int deadTime
                required property bool full

                width: neighborList.width
                height: 22
                spacing: 0
                Text { text: neighborId; color: "#e8e8e8"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 150 }
                Text { text: ospfState; color: full ? "#3fbf6a" : "#e0c07a"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 120 }
                Text { text: address; color: "#a0a0a0"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 140 }
                Text { text: iface; color: "#a0a0a0"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 120 }
                Text { text: uptime; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 100 }
                Text { text: deadTime + "s"; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.fillWidth: true }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.neighborCount === 0
            text: qsTr("no OSPF neighbor (single router, or adjacency not formed yet)")
            color: "#7a7a7a"
            font.pixelSize: 11
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        Label { text: qsTr("Interfaces"); color: "#e8e8e8"; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { text: qsTr("Interface"); color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 130 }
            Text { text: qsTr("Area");      color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 110 }
            Text { text: qsTr("Address");   color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 160 }
            Text { text: qsTr("State");     color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 90 }
            Text { text: qsTr("Cost");      color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 60 }
            Text { text: qsTr("Prio");      color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 60 }
            Text { text: qsTr("Nbrs");      color: "#8c8c8c"; font.pixelSize: 11; Layout.fillWidth: true }
        }

        ListView {
            id: interfaceList
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            clip: true
            model: win.tool.interfaces

            delegate: RowLayout {
                required property string name
                required property string area
                required property string address
                required property string ospfState
                required property int cost
                required property int priority
                required property int neighborCount
                required property int adjacentCount
                required property bool up

                width: interfaceList.width
                height: 22
                spacing: 0
                Text { text: name; color: up ? "#e8e8e8" : "#7a7a7a"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 130 }
                Text { text: area; color: "#a0a0a0"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 110 }
                Text { text: address; color: "#a0a0a0"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 160 }
                Text { text: ospfState; color: "#e0c07a"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 90 }
                Text { text: cost; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 60 }
                Text { text: priority; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 60 }
                Text {
                    text: neighborCount + " (" + adjacentCount + " adjacent)"
                    color: "#8c8c8c"
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        Label { text: qsTr("Database"); color: "#e8e8e8"; font.bold: true }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 2

            Repeater {
                model: win.tool.database
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 16
                    Text {
                        text: modelData.kind + " (Area " + modelData.area + ")"
                        color: "#a0a0a0"
                        font.family: "monospace"
                        font.pixelSize: 11
                        Layout.preferredWidth: 320
                    }
                    Text {
                        text: modelData.count + " LSA(s)"
                        color: "#8c8c8c"
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        Label {
            Layout.fillWidth: true
            text: win.tool.statusText
            color: "#6f6f6f"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
