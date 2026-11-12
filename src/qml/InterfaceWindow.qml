// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// One interface tool window: identity fields, live rates and the rolling graph.
// Data comes from InterfacesModel; the window only reads.
Rectangle {
    id: win

    property string ifName: ""
    property var detail: ({})

    implicitWidth: 760
    implicitHeight: 480
    property int minimumWidth: 480
    property int minimumHeight: 320
    property string windowTitle: qsTr("Interface %1").arg(ifName)
    color: "#1e1e1e"

    function refresh() {
        detail = AppEngine.interfaces.interfaceData(ifName)
    }

    function formatBytes(bytes) {
        if (bytes >= 1e9) return (bytes / 1e9).toFixed(2) + " GB";
        if (bytes >= 1e6) return (bytes / 1e6).toFixed(2) + " MB";
        if (bytes >= 1e3) return (bytes / 1e3).toFixed(1) + " kB";
        return (bytes || 0) + " B";
    }

    Component.onCompleted: refresh()

    Connections {
        target: AppEngine.interfaces
        function onSampled() { win.refresh() }
        function onCountChanged() { win.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        GridLayout {
            columns: 4
            columnSpacing: 18
            rowSpacing: 4
            Layout.fillWidth: true

            Label { text: qsTr("Name"); color: "#8c8c8c" }
            Label { text: win.detail.ifName || ""; color: "#e8e8e8"; font.bold: true }
            Label { text: qsTr("Device"); color: "#8c8c8c" }
            Label { text: win.detail.device || ""; color: "#e8e8e8" }

            Label { text: qsTr("Protocol"); color: "#8c8c8c" }
            Label { text: win.detail.protocol || ""; color: "#e8e8e8" }
            Label { text: qsTr("State"); color: "#8c8c8c" }
            Label {
                text: win.detail.up ? qsTr("up") : qsTr("down")
                color: win.detail.up ? "#3fbf6a" : "#7a7a7a"
            }

            Label { text: qsTr("Addresses"); color: "#8c8c8c" }
            Label {
                Layout.columnSpan: 3
                Layout.fillWidth: true
                text: win.detail.addresses || ""
                color: "#e8e8e8"
                elide: Text.ElideMiddle
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            spacing: 22
            Label { text: qsTr("RX"); color: "#3fbf6a"; font.bold: true }
            Label {
                text: (win.detail.rxRateText || "0 bps") + "   " + win.formatBytes(win.detail.rxBytes)
                color: "#e8e8e8"
            }
            Label { text: qsTr("TX"); color: "#4b9fe8"; font.bold: true }
            Label {
                text: (win.detail.txRateText || "0 bps") + "   " + win.formatBytes(win.detail.txBytes)
                color: "#e8e8e8"
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("1 s samples"); color: "#6f6f6f"; font.pixelSize: 11 }
        }

        TrafficGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            rxSeries: win.detail.rxHistory || []
            txSeries: win.detail.txHistory || []
        }
    }
}
