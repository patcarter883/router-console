// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Console for one router: each Enter is a single /bin/sh -c over rpcd file exec
// (no tty on that surface), so the header says what the window does and the
// prompt shows the directory the window is carrying.
Rectangle {
    id: win

    implicitWidth: 980
    implicitHeight: 620
    property int minimumWidth: 640
    property int minimumHeight: 360
    property string windowTitle: qsTr("Terminal")
    color: "#1e1e1e"
    property string toolId: ""

    readonly property var tool: AppEngine.terminalTool

    // -1 while the user is typing a fresh line; otherwise an index into
    // tool.history, with `draft` holding the unfinished line.
    property int historyIndex: -1
    property string draft: ""

    function lineColor(kind) {
        switch (kind) {
        case "err": return "#e07a7a"
        case "prompt": return "#8fc7ea"
        case "failed": return "#e0a05a"
        case "done": return "#6f9f6f"
        case "note": return "#8c8c8c"
        }
        return "#e8e8e8"
    }

    function submit() {
        const text = input.text
        if (win.tool.busy || text.trim() === "")
            return
        win.tool.run(text)
        input.text = ""
        win.historyIndex = -1
        win.draft = ""
    }

    function recall(delta) {
        const h = win.tool.history
        if (h.length === 0)
            return
        if (win.historyIndex === -1) {
            if (delta > 0)
                return
            win.draft = input.text
            win.historyIndex = h.length - 1
        } else {
            const next = win.historyIndex + delta
            if (next < 0)
                return
            if (next >= h.length) {
                win.historyIndex = -1
                input.text = win.draft
                return
            }
            win.historyIndex = next
        }
        input.text = h[win.historyIndex]
        input.cursorPosition = input.text.length
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("One /bin/sh -c per Enter (rpcd file exec): no tty, no stdin, no full-screen programs. This window keeps the directory, not the shell.")
                color: "#7a7a7a"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("Clear")
                onClicked: {
                    win.tool.clear()
                    win.historyIndex = -1
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: output
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.lines
            spacing: 1
            ScrollBar.vertical: ScrollBar {}

            onCountChanged: if (count > 0) positionViewAtEnd()

            delegate: Text {
                required property var modelData
                width: output.width
                text: modelData.text
                color: win.lineColor(modelData.kind)
                font.family: "monospace"
                font.pixelSize: 12
                font.italic: modelData.kind === "note"
                wrapMode: Text.WrapAnywhere
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: win.tool.prompt
                color: "#8fc7ea"
                font.family: "monospace"
                font.pixelSize: 12
            }
            TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: win.tool.busy ? qsTr("running…") : qsTr("command")
                readOnly: win.tool.busy
                selectByMouse: true
                font.family: "monospace"
                font.pixelSize: 12
                onAccepted: win.submit()
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Up) {
                        win.recall(-1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down) {
                        win.recall(1)
                        event.accepted = true
                    }
                }
            }
            Button {
                text: qsTr("Run")
                enabled: !win.tool.busy
                onClicked: {
                    win.submit()
                    input.forceActiveFocus()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: win.tool.statusText
            color: win.tool.lastExitCode === 0 ? "#6f6f6f" : "#c98a6a"
            font.pixelSize: 11
        }
    }

    Component.onCompleted: input.forceActiveFocus()
}
