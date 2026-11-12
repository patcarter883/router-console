// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// One interface tool window: identity fields, live rates and the stored-history
// graph. Data comes from InterfacesModel; the window only reads, and the range
// selector changes the window the model is asked for.
Rectangle {
    id: win

    property string ifName: ""
    property string initialRange: "10m"
    property string rangeId: initialRange
    property var detail: ({})
    property var trace: ({})

    readonly property var ranges: [
        { id: "1m", ms: 60 * 1000 },
        { id: "10m", ms: 10 * 60 * 1000 },
        { id: "1h", ms: 60 * 60 * 1000 },
        { id: "24h", ms: 24 * 60 * 60 * 1000 }
    ]

    implicitWidth: 760
    implicitHeight: 480
    property int minimumWidth: 480
    property int minimumHeight: 320
    property string windowTitle: qsTr("Interface %1").arg(ifName)
    color: "#1e1e1e"

    function rangeMs() {
        for (let i = 0; i < ranges.length; ++i) {
            if (ranges[i].id === rangeId)
                return ranges[i].ms
        }
        return 10 * 60 * 1000
    }

    function refresh() {
        detail = AppEngine.interfaces.interfaceData(ifName)
        trace = AppEngine.interfaces.history(ifName, rangeMs())
        if (graphView.paintCount > 0)
            AppEngine.interfaces.reportGraphPaint(trace.bucketMs, trace.slots,
                                                  graphView.paintMs / graphView.paintCount,
                                                  graphView.paintCount - graphView.reported)
        graphView.reported = graphView.paintCount
    }

    function formatBytes(bytes) {
        if (bytes >= 1e9) return (bytes / 1e9).toFixed(2) + " GB";
        if (bytes >= 1e6) return (bytes / 1e6).toFixed(2) + " MB";
        if (bytes >= 1e3) return (bytes / 1e3).toFixed(1) + " kB";
        return (bytes || 0) + " B";
    }

    function formatSpan(ms) {
        if (ms <= 0) return "0 s";
        const seconds = Math.round(ms / 1000);
        if (seconds < 90) return seconds + " s";
        const minutes = Math.floor(seconds / 60);
        if (minutes < 90) return minutes + " m " + (seconds % 60) + " s";
        return Math.floor(minutes / 60) + " h " + (minutes % 60) + " m";
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

            // Time range: the same stored history at four zoom levels.
            Row {
                spacing: 2
                Repeater {
                    model: win.ranges
                    delegate: Button {
                        id: rangeButton
                        required property var modelData
                        objectName: "range-" + modelData.id
                        text: modelData.id
                        highlighted: win.rangeId === modelData.id
                        implicitHeight: 24
                        implicitWidth: 44
                        padding: 2
                        font.pixelSize: 11
                        onClicked: {
                            win.rangeId = modelData.id
                            win.refresh()
                        }
                    }
                }
            }
        }

        TrafficGraph {
            id: graphView
            Layout.fillWidth: true
            Layout.fillHeight: true
            rxSeries: win.trace.rx || []
            txSeries: win.trace.tx || []
            gridStartMs: win.trace.gridStartMs || 0
            fromMs: win.trace.fromMs || 0
            toMs: win.trace.toMs || 0
            bucketMs: win.trace.bucketMs || 1000
        }

        Label {
            objectName: "graphFooter"
            Layout.fillWidth: true
            font.pixelSize: 11
            color: "#6f6f6f"
            text: qsTr("%1 buckets · %2 points · sampled %3 of %4 · history keeps 24 h")
                      .arg(((win.trace.bucketMs || 1000) / 1000).toFixed(0) + " s")
                      .arg(win.trace.slots || 0)
                      .arg(win.formatSpan(win.trace.coveredMs || 0))
                      .arg(win.rangeId)
        }
    }
}
