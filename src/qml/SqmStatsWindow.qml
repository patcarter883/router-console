// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// SQM/CAKE stats window: the qdisc tree the router reports, cake tins expanded.
Window {
    id: win

    width: 780
    height: 520
    minimumWidth: 520
    minimumHeight: 320
    title: qsTr("SQM / CAKE stats")
    color: "#1e1e1e"
    visible: true
    property string toolId: ""

    readonly property var tool: AppEngine.sqmStatsTool

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: win.tool.cakeSummary
                color: win.tool.cakeActive ? "#3fbf6a" : "#a0a0a0"
                elide: Text.ElideRight
            }
            CheckBox {
                text: qsTr("1 Hz")
                checked: win.tool.autoRefresh
                onToggled: win.tool.autoRefresh = checked
            }
            Button {
                text: qsTr("Refresh")
                enabled: !win.tool.busy
                onClicked: win.tool.refresh()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: qdiscList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.qdiscs
            spacing: 6

            delegate: ColumnLayout {
                required property var modelData
                width: qdiscList.width
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    spacing: 12
                    Text {
                        text: modelData.device + "  " + modelData.kind + " " + modelData.handle
                        color: modelData.cake ? "#e0c07a" : "#e8e8e8"
                        font.family: "monospace"
                        font.pixelSize: 12
                        font.bold: modelData.cake
                        Layout.preferredWidth: 220
                    }
                    Text {
                        text: modelData.detail
                        color: "#8c8c8c"
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "drops " + modelData.dropped
                        color: modelData.dropped > 0 ? "#e05252" : "#6f6f6f"
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    spacing: 18
                    Text {
                        text: "sent " + modelData.sent + " / " + modelData.packets + " pkt"
                        color: "#a0a0a0"
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                    Text {
                        text: "backlog " + modelData.backlog
                        color: "#a0a0a0"
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                    Text {
                        text: "requeues " + modelData.requeues + " overlimits " + modelData.overlimits
                        color: "#a0a0a0"
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                    Text {
                        visible: modelData.cake
                        text: "memory " + modelData.memory + (modelData.capacity.length > 0
                                                             ? "  capacity " + modelData.capacity : "")
                        color: "#a0a0a0"
                        font.family: "monospace"
                        font.pixelSize: 11
                        Layout.fillWidth: true
                    }
                }

                Repeater {
                    model: modelData.tins
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 40
                        spacing: 16
                        Text {
                            text: "tin " + modelData.index + "  thresh " + modelData.threshold
                            color: "#8c8c8c"
                            font.family: "monospace"
                            font.pixelSize: 11
                            Layout.preferredWidth: 200
                        }
                        Text {
                            text: modelData.bytes + "  pkts " + modelData.packets
                            color: "#8c8c8c"
                            font.family: "monospace"
                            font.pixelSize: 11
                            Layout.preferredWidth: 200
                        }
                        Text {
                            text: "drops " + modelData.drops + "  marks " + modelData.marks
                                  + "  delay " + modelData.delay
                            color: "#8c8c8c"
                            font.family: "monospace"
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.qdiscs.length === 0
            text: AppEngine.connected ? qsTr("no qdisc data yet") : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }

        Label {
            Layout.fillWidth: true
            text: win.tool.statusText
            color: "#6f6f6f"
            font.pixelSize: 11
        }
    }
}
