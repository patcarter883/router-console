// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// BGP window: `show bgp summary` as a typed peers table with state and prefix
// counters.
Window {
    id: win

    width: 900
    height: 520
    minimumWidth: 600
    minimumHeight: 320
    title: qsTr("BGP")
    color: "#1e1e1e"
    visible: true
    property string viewId: ""

    readonly property var tool: AppEngine.bgpTool

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
                text: "local AS " + (win.tool.localAs > 0 ? win.tool.localAs : "—")
                      + "   router-id " + (win.tool.routerId.length > 0 ? win.tool.routerId : "—")
                      + "   peers " + win.tool.peerCount
                      + "   established " + win.tool.establishedCount
                      + "   RIB " + win.tool.ribEntries
                color: win.tool.establishedCount > 0 ? "#3fbf6a" : "#a0a0a0"
                font.family: "monospace"
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
            spacing: 0
            Text { text: qsTr("Neighbor"); color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 150 }
            Text { text: qsTr("AS");       color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 70 }
            Text { text: qsTr("State");    color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 110 }
            Text { text: qsTr("Up/Down");  color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 120 }
            Text { text: qsTr("Pfx in");   color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 70 }
            Text { text: qsTr("Pfx out");  color: "#8c8c8c"; font.pixelSize: 11; Layout.preferredWidth: 70 }
            Text { text: qsTr("Msgs in/out"); color: "#8c8c8c"; font.pixelSize: 11; Layout.fillWidth: true }
        }

        ListView {
            id: peerList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.peers

            delegate: RowLayout {
                required property string address
                required property int remoteAs
                required property string bgpState
                required property string uptime
                required property int pfxRcvd
                required property int pfxSnt
                required property int msgRcvd
                required property int msgSent
                required property bool established

                width: peerList.width
                height: 22
                spacing: 0
                Text { text: address; color: "#e8e8e8"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 150 }
                Text { text: remoteAs; color: "#a0a0a0"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 70 }
                Text { text: bgpState; color: established ? "#3fbf6a" : "#e0c07a"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 110 }
                Text { text: uptime; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 120 }
                Text { text: pfxRcvd; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 70 }
                Text { text: pfxSnt; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.preferredWidth: 70 }
                Text { text: msgRcvd + "/" + msgSent; color: "#8c8c8c"; font.family: "monospace"; font.pixelSize: 12; Layout.fillWidth: true }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.peerCount === 0
            text: qsTr("no BGP peer configured on this router")
            color: "#7a7a7a"
            font.pixelSize: 11
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
