// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Routing table window: every candidate of every prefix FRR reports, plus the
// kernel IPv6 routes it does not carry. Manual refresh by default; 1 Hz is
// opt-in because every refresh is three exec calls on the router.
Window {
    id: win

    width: 980
    height: 620
    minimumWidth: 640
    minimumHeight: 360
    title: qsTr("Routing table")
    color: "#1e1e1e"
    visible: true
    property string viewId: ""

    readonly property var tool: AppEngine.routesTool

    // The window shows the router's current state as soon as it opens; the
    // Refresh button (or the 1 Hz toggle) updates it after that.
    Component.onCompleted: win.tool.refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label { text: qsTr("Protocol"); color: "#a0a0a0" }
            ComboBox {
                id: filterBox
                implicitWidth: 150
                model: win.tool.protocolChoices
                onActivated: win.tool.protocolFilter = currentText
                Component.onCompleted: currentIndex = 0
            }
            Item { Layout.fillWidth: true }
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

        Label {
            Layout.fillWidth: true
            text: win.tool.daemonText
            color: "#6f6f6f"
            font.pixelSize: 11
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { text: qsTr("Prefix");       color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 230 }
            Text { text: qsTr("Protocol");     color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 90 }
            Text { text: qsTr("AD/Metric");    color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 90 }
            Text { text: qsTr("Next hop");     color: "#8c8c8c"; font.pixelSize: 11; Layout.fillWidth: true }
            Text { text: qsTr("Uptime");       color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 90 }
        }

        ListView {
            id: routeList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.routes
            spacing: 2

            delegate: Rectangle {
                required property string prefix
                required property string protocol
                required property int distance
                required property int metric
                required property bool selected
                required property string nexthop
                required property string uptime
                required property string source

                width: routeList.width
                height: 22
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Text {
                        text: (selected ? ">* " : "   ") + prefix
                        color: selected ? "#e8e8e8" : "#9a9a9a"
                        font.family: "monospace"
                        font.pixelSize: 12
                        font.bold: selected
                        Layout.preferredWidth: 230
                        elide: Text.ElideRight
                    }
                    Text {
                        text: protocol + (source === "kernel" ? " (kernel)" : "")
                        color: protocol === "ospf" ? "#3fbf6a"
                               : protocol === "bgp" ? "#e0c07a"
                               : protocol === "static" ? "#7fa8d8" : "#8c8c8c"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.preferredWidth: 90
                    }
                    Text {
                        text: distance + "/" + metric
                        color: "#8c8c8c"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.preferredWidth: 90
                    }
                    Text {
                        text: nexthop
                        color: "#a0a0a0"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: uptime
                        color: "#6f6f6f"
                        font.family: "monospace"
                        font.pixelSize: 11
                        Layout.preferredWidth: 90
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.routes.count === 0
            text: AppEngine.connected ? qsTr("no routes yet — press Refresh") : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: win.tool.statusText
                color: "#6f6f6f"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Label {
                text: win.tool.errorText
                color: "#e05252"
                font.pixelSize: 11
                visible: win.tool.errorText.length > 0
            }
        }
    }
}
