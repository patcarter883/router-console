// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Torch window: per-host live bandwidth from conntrack deltas. Each row is a
// remote peer the router is talking to right now, newest load first.
Window {
    id: win

    width: 760
    height: 520
    minimumWidth: 520
    minimumHeight: 320
    title: qsTr("Torch")
    color: "#1e1e1e"
    visible: true
    property string toolId: ""

    readonly property var torch: AppEngine.torchModel

    // Sampling starts with the window; it stops when the session drops.
    Component.onCompleted: {
        if (AppEngine.connected && !torch.sampling)
            torch.start()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 18

            Label { text: qsTr("RX total"); color: "#8c8c8c" }
            Label {
                text: win.torch.totalRxText
                color: "#3fbf6a"
                font.bold: true
                font.family: "monospace"
            }
            Label { text: qsTr("TX total"); color: "#8c8c8c" }
            Label {
                text: win.torch.totalTxText
                color: "#4b9fe8"
                font.bold: true
                font.family: "monospace"
            }
            Item { Layout.fillWidth: true }
            Label { text: win.torch.statusText; color: "#6f6f6f"; font.pixelSize: 11 }
            Button {
                text: win.torch.sampling ? qsTr("Stop") : qsTr("Start")
                enabled: AppEngine.connected
                onClicked: win.torch.sampling ? win.torch.stop() : win.torch.start()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            spacing: 10
            Text {
                text: qsTr("host")
                color: "#8c8c8c"
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("conns")
                color: "#8c8c8c"
                font.pixelSize: 11
                Layout.preferredWidth: 50
                horizontalAlignment: Text.AlignRight
            }
            Text {
                text: qsTr("RX")
                color: "#8c8c8c"
                font.pixelSize: 11
                Layout.preferredWidth: 100
                horizontalAlignment: Text.AlignRight
            }
            Text {
                text: qsTr("TX")
                color: "#8c8c8c"
                font.pixelSize: 11
                Layout.preferredWidth: 100
                horizontalAlignment: Text.AlignRight
            }
            Text {
                text: qsTr("total")
                color: "#8c8c8c"
                font.pixelSize: 11
                Layout.preferredWidth: 110
                horizontalAlignment: Text.AlignRight
            }
        }

        ListView {
            id: hostList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.torch
            spacing: 1

            delegate: Rectangle {
                required property string address
                required property int connections
                required property string rxRateText
                required property string txRateText
                required property var rxBytes
                required property var txBytes
                required property bool isActive
                required property string lastSeen
                required property int index

                width: hostList.width
                height: 26
                color: isActive ? (index === 0 ? "#2a3327" : "#232323") : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    spacing: 10

                    Text {
                        text: address
                        color: isActive ? "#e8e8e8" : "#6f6f6f"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: connections
                        color: "#8c8c8c"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.preferredWidth: 50
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        text: rxRateText
                        color: "#3fbf6a"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        text: txRateText
                        color: "#4b9fe8"
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        text: lastSeen
                        color: "#6f6f6f"
                        font.family: "monospace"
                        font.pixelSize: 11
                        Layout.preferredWidth: 110
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.torch.count === 0
            text: AppEngine.connected
                  ? qsTr("no peers yet — start to sample conntrack at 1 Hz")
                  : qsTr("not connected")
            color: "#7a7a7a"
            font.pixelSize: 11
        }
    }
}
