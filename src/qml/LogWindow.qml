// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Log viewer: a polled `logread` window, new lines appended by the tool.
Window {
    id: win

    width: 980
    height: 620
    minimumWidth: 640
    minimumHeight: 360
    title: qsTr("Log")
    color: "#1e1e1e"
    visible: true
    property string toolId: ""

    readonly property var tool: AppEngine.logTool

    function levelColor(severity) {
        switch (severity) {
        case "err": return "#e05252"
        case "warn": return "#e0c07a"
        case "debug": return "#6f6f6f"
        }
        return "#7fb8e0"
    }

    Component.onCompleted: win.tool.refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: win.tool.paused ? qsTr("Resume") : qsTr("Pause")
                onClicked: win.tool.paused = !win.tool.paused
            }
            Button {
                text: qsTr("Clear")
                onClicked: win.tool.clear()
            }
            Button {
                text: qsTr("Fetch now")
                enabled: !win.tool.busy && !win.tool.paused
                onClicked: win.tool.refresh()
            }
            CheckBox {
                text: qsTr("follow tail")
                checked: win.tool.follow
                onToggled: win.tool.follow = checked
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("filter (facility, source or message)")
                text: win.tool.filter
                onTextEdited: win.tool.filter = text
            }
            Label {
                text: win.tool.totalCount + qsTr(" lines")
                color: "#8c8c8c"
                font.pixelSize: 11
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.entries
            spacing: 1
            ScrollBar.vertical: ScrollBar {}

            onCountChanged: {
                if (win.tool.follow && count > 0)
                    positionViewAtEnd()
            }

            delegate: RowLayout {
                required property var modelData
                width: logList.width
                spacing: 8

                Text {
                    text: modelData.time
                    color: "#6f6f6f"
                    font.family: "monospace"
                    font.pixelSize: 11
                    Layout.preferredWidth: 130
                }
                Text {
                    text: modelData.level
                    color: win.levelColor(modelData.severity)
                    font.family: "monospace"
                    font.pixelSize: 11
                    Layout.preferredWidth: 130
                    elide: Text.ElideRight
                }
                Text {
                    text: modelData.source
                    color: "#9a9a9a"
                    font.family: "monospace"
                    font.pixelSize: 11
                    Layout.preferredWidth: 150
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.message
                    color: "#e8e8e8"
                    font.family: "monospace"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.entries.length === 0
            text: AppEngine.connected ? qsTr("no log lines matching the filter")
                                      : qsTr("not connected")
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
