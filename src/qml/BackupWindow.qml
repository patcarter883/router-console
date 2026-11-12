// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Config backup/restore: one button writes a local .tar.gz of /etc/config plus
// the package manifest; restore stages the archive on the router and only
// writes /etc/config when the confirm button is pressed.
Window {
    id: win

    width: 900
    height: 600
    minimumWidth: 620
    minimumHeight: 380
    title: qsTr("Config backup")
    color: "#1e1e1e"
    visible: true
    property string toolId: ""

    readonly property var tool: AppEngine.backupTool
    property bool confirming: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: targetField
                Layout.fillWidth: true
                placeholderText: qsTr("local backup directory")
                text: win.tool.lastArchive.length > 0
                      ? win.tool.lastArchive.substring(0, win.tool.lastArchive.lastIndexOf("/"))
                      : ""
            }
            Button {
                text: qsTr("Backup now")
                enabled: AppEngine.connected && !win.tool.busy
                onClicked: win.tool.backup(targetField.text)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: archiveField
                Layout.fillWidth: true
                placeholderText: qsTr("archive to restore (.tar.gz)")
            }
            Button {
                text: qsTr("Stage restore")
                enabled: !win.tool.busy && archiveField.text.length > 0
                onClicked: win.tool.stageRestore(archiveField.text)
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: win.tool.manifestSummary.length > 0
                      ? win.tool.manifestSummary + qsTr("  ·  ") + win.tool.lastArchiveSize
                      : qsTr("no backup taken in this session")
                color: "#a0a0a0"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Button {
                text: qsTr("Confirm restore")
                visible: win.tool.restoreStaged
                enabled: !win.tool.busy
                onClicked: win.confirmDialog.open()
            }
            Button {
                text: qsTr("Discard staging")
                visible: win.tool.restoreStaged || win.tool.stagedFiles.length > 0
                enabled: !win.tool.busy
                onClicked: win.tool.discardRestore()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: win.tool.restoreStaged
            text: win.tool.stagedSummary
            color: "#e0c07a"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // Staged files: written under /tmp/router-console-restore, nothing in
        // /etc/config has changed yet.
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: win.tool.stagedFiles.length > 0 ? 90 : 0
            visible: win.tool.stagedFiles.length > 0
            clip: true
            model: win.tool.stagedFiles
            delegate: Text {
                required property var modelData
                text: modelData.path + "  " + modelData.size + " B"
                color: "#9a9a9a"
                font.family: "monospace"
                font.pixelSize: 11
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        ListView {
            id: logView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.tool.logLines
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: if (count > 0) positionViewAtEnd()

            delegate: Text {
                required property string modelData
                width: logView.width
                text: modelData
                color: "#c8c8c8"
                font.family: "monospace"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
        }

        Label {
            Layout.fillWidth: true
            text: win.tool.statusText
            color: "#6f6f6f"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("restore writes /etc/config only after confirmation; "
                       + "router services are not restarted")
            color: "#7a7a7a"
            font.pixelSize: 11
        }
    }

    Dialog {
        id: confirmDialog
        title: qsTr("Write the staged files to /etc/config?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            text: qsTr("This overwrites the router's config files with the archive contents. "
                       + "Services are not restarted and there is no automatic rollback.")
            wrapMode: Text.WordWrap
            width: 380
        }
        onAccepted: win.tool.confirmRestore()
    }
}
